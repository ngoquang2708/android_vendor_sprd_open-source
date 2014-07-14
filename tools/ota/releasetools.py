import sys
import copy
import errno
import os
import re
import sha
import subprocess
import tempfile
import time
import zipfile

import common
import edify_generator

OPTIONS = common.OPTIONS
OPTIONS.modem_update = True
OPTIONS.uboot_update = True
OPTIONS.wipe_product_info = False
OPTIONS.cache_path = "/cache"

class EdifyGeneratorExt(object):
  def __init__(self, script):
    self.script = script.script

  def Run_program(self, binary, *args):
    """Run a program named binary"""
    self.script.append('assert(run_program("%s"' % (binary,) +
                      "".join([', "%s"' % (i,) for i in args]) +
                      '));')

  def SdcardFreeSpaceCheck(self, path, amount):
    self.script.append("assert(apply_disk_space(%s, %d));" % (path, amount,))

  def UnpackPackageFile(self, src, dst):
    """Unpack a given file from the OTA package into the given
    destination file."""
    self.script.append('package_extract_file("%s", "%s");' % (src, dst))

  def WritePartitionImage(self, p, fn, dev_path):
    """write the given filename into the partition for the
    given device path"""
    partition_type = common.PARTITION_TYPES[p.fs_type]
    args = {'dev_path':dev_path, 'fn': fn, 'pt':partition_type}
    if partition_type == "EMMC":
      self.script.append(
            'write_emmc_image("%(fn)s", "%(dev_path)s");' % args)
    else:
      self.script.append(
            'write_raw_image("%(fn)s", "%(dev_path)s", "%(pt)s");' % args)

  def MergeSpl(self, p, fn, dev_path):
    """merge spl patition data use given new spl filename"""
    partition_type = common.PARTITION_TYPES[p.fs_type]
    args = {'dev_path':dev_path, 'fn': fn, 'pt':partition_type}
    self.script.append(
          'merge_spl("%(fn)s", "%(dev_path)s", "%(pt)s");' % args)

  def AddToZipExt(self, input_zip, output_zip, input_path=None):
    """Write the accumulated script to the output_zip file.  input_zip
    is used as the source for the 'updater' binary needed to run
    script.  If input_path is not None, it will be used as a local
    path for the binary instead of input_zip."""

    if input_path is None:
      data_nv = input_zip.read("OTA/bin/nvmerge")
      data_cfg = input_zip.read("OTA/bin/nvmerge.cfg")
    else:
      data_nv = open(os.path.join(input_path, "nvmerge")).read()
      data_cfg = open(os.path.join(input_path, "nvmerge.cfg")).read()

    common.ZipWriteStr(output_zip, "META-INF/com/google/android/nvmerge",
                       data_nv, perms=0755)
    common.ZipWriteStr(output_zip, "META-INF/com/google/android/nvmerge.cfg",
                       data_cfg)

###################################################################################################

class Partition(object):
  def __str__(self):
    return "Partion[" +\
           "mount_point=" + self.mount_point + "," +\
           "fs_type=" + self.fs_type + "," +\
           "extract=" + self.extract + "]"

class PartitionFile(object):

  def __init__(self, mount_point, file_name, input_dir, bootable=False, subdir=None):
    self.file_name = file_name
    if file_name is None or input_dir is None:
      if PartitionUpdater.IfNeedImageFile(mount_point):
        raise common.ExternalError("init PartitionFile error")

      return

    self.full_name = os.path.join(input_dir, file_name)
    if os.path.exists(self.full_name):
      self.bin = common.File(file_name, open(self.full_name).read())
      self.size = len(self.bin.data)
    else:
      print ("[Warning:] no image file %s" % (self.full_name))
      self.bin = None
      self.size = 0

class PartitionUpdater(object):
  """Class for recovery update"""

  total_update_size = 0.0
  update_so_far = 0.0

  total_verify_size = 0.0
  verify_so_far = 0.0

  script = None
  script_ext = None
  recovery_script = None
  permission_script = None
  properties = None

  # used for free space check
  need_cache_space = 0
  need_sdcard_space = 0

  @classmethod
  def SetupEnv(cls, script, script_ext, options):
    cls.script = script
    cls.script_ext = script_ext;
    cls.options = options
    if(cls.script.info == None):
      cls.script.info = options.info_dict

  @classmethod
  def GetBuildProp(cls, key):
    if (cls.properties == None):
      cls.properties = cls.script.info.get("build.prop", {})
    try:
      return cls.properties[key]
    except KeyError:
      raise common.ExternalError("couldn't find %s in build.prop" % (key,))

  @classmethod
  def IfNeedImageFile(cls, mount_point):
    if mount_point in ("/system", "/data", "/runtimenv", "/backupfixnv", "/productinfo"):
      return False
    else:
      return True

  @classmethod
  def ComputeNeedSpace(cls, updater):
    if updater.extract and (updater.partition):
      if updater.need_extract == "/sdcard" or updater.need_extract == "/external":
        cls.need_sdcard_space += updater.target.size
      elif updater.need_extract == "/cache":
        cls.need_cache_space += updater.target.size

  @classmethod
  def FreeSpaceCheck(cls):
    if cls.need_cache_space > 0:
      cls.script.CacheFreeSpaceCheck(cls.need_cache_space)
    if cls.need_sdcard_space > 0:
      cls.script_ext.SdcardFreeSpaceCheck("/sdcard", cls.need_sdcard_space)

  def __init__(self, mount_point, file_name=None, target_ver_dir=None, source_ver_dir=None, extract=False, verbatim=False, mount_point2=None, mount_point3=None, nv_merge=None, spl_merge=False):
    self.script = PartitionUpdater.script
    self.options = PartitionUpdater.options

    self.mount_point = mount_point
    self.file_name = file_name
    self.extract = extract
    self.verbatim = verbatim
    self.nv_merge = nv_merge
    self.spl_merge = spl_merge
    self.update_flag = False
    self.inc_flag = False
    self.mount_point2 = mount_point2
    self.mount_point3 = mount_point3
    self.need_extract = None
    if nv_merge or spl_merge:
      self.extract = True
      self.verbatim = True
      self.need_extract = OPTIONS.cache_path
    fstab = self.options.info_dict["fstab"]
    if fstab is None:
      raise common.ExternalError("no fstab")
    self.partition = fstab.get(mount_point, None)
    if self.partition is None and mount_point2 is not None:
      self.partition = fstab.get(mount_point2, None)
    if self.partition is None:
      print ("[Warning:] no patition in fstab for mount point %s" % mount_point)
      self.partition = Partition()
      self.partition.mount_point = mount_point
      self.partition.extract = OPTIONS.cache_path
      self.partition.fs_type = "yaffs2"
      print ("[Warning:] auto create patition %s" % self.partition)
      self.extract = True
      self.verbatim = True

    if target_ver_dir is None and PartitionUpdater.IfNeedImageFile(mount_point):
      raise common.ExternalError("init PartitionUpdater error")

  def AddToOutputZip(self, output_zip, **kwargs):
    PartitionUpdater.ComputeNeedSpace(self)

  def Check(self, **kwargs):
    pass

  def Update(self, **kwargs):
    pass

  def GetFixNvSize(self):
    if self.nv_merge == None:
      raise common.ExternalError("internal error: no nv_merge given in GetFixNvSize()")
    if self.nv_merge == "" or self.nv_merge == "wcn":
      return "0x00"
    prop_key="ro.modem.%s.fixnv_size" % (self.nv_merge)
    return PartitionUpdater.GetBuildProp(prop_key)

  def Formated(self):
    if self.partition.fs_type in ("yaffs2", "mtd", "ubifs", "ext4"):
      return True
    return False

  def FormatPartition(self, format_msg=None):
    if self.Formated():
      if format_msg is not None:
        self.script.Print(format_msg)
      self.script.FormatPartition(self.partition.mount_point)

  def IsExtract(self):
    if self.partition.fs_type in ("yaffs2", "ubifs", "ext4"):
      return True
    else:
      return self.extract

  def GetRealDevicePath(self, p, dev_path):
    partition_type = common.PARTITION_TYPES[p.fs_type]
    if partition_type == "EMMC":
      return dev_path
    elif partition_type == "MTD":
      return dev_path
    elif partition_type == "UBI":
      return "/dev/ubi0_"+dev_path
    else:
      raise ValueError("don't know how to get \"%s\" partitions's device path" % (p.fs_type,))

  # if self.extract is true
  #   if extract dir exist
  #     UnpackPackageFile to extract dir
  #   else
  #     UnpackPackageFile to partition
  # else
  #   WriteRawImage file to partition
  def FullUpdateToPartition(self):
    mount_point = self.partition.mount_point
    extract = self.IsExtract()
    if extract:
      if self.need_extract is not None:
        mount_point = self.need_extract
      self.script.Print("extract "+ self.target.file_name + " to " + mount_point + " ....")
    else:
      self.script.Print("write " + self.target.file_name + " to partition " + mount_point +" ....")
      self.FormatPartition()
    if not extract or self.need_extract is None:
      mount_point_temp = mount_point[1:]
      common.CheckSize(self.target.bin.data, mount_point_temp, self.options.info_dict)

    p = self.options.info_dict["fstab"].get(self.mount_point, None)
    if p is not None:
      p1 = None
      pt_dev1 = None
    else:
      p = self.options.info_dict["fstab"][self.mount_point2]
      p1 = self.options.info_dict["fstab"][self.mount_point3]
      pt_dev1 = p1.device
      if p is None:
        raise common.ExternalError("no partion %s in fstab" % (self.mount_point2))
      if p1 is None:
        print ("no partion %s in fstab" % (self.mount_point3))
    pt_dev = p.device

    if extract:
      self.script_ext.UnpackPackageFile(self.target.file_name, os.path.join(mount_point, self.target.file_name))
    else:
      self.script.WriteRawImage(mount_point, self.target.file_name)

    if self.nv_merge:
      nvmerge_exe = os.path.join(OPTIONS.cache_path, "nvmerge")
      nvmerge_cfg = os.path.join(OPTIONS.cache_path, "nvmerge.cfg")
      new_nv = os.path.join(OPTIONS.cache_path, self.target.file_name)
      merged_nv = os.path.join(OPTIONS.cache_path, "merged_" + self.target.file_name)
      self.script_ext.Run_program(nvmerge_exe, nvmerge_cfg, self.GetRealDevicePath(p, pt_dev), new_nv, merged_nv, self.GetFixNvSize())
      self.script_ext.WritePartitionImage(p, merged_nv, pt_dev)
      if p1 is not None:
        self.script_ext.WritePartitionImage(p1, merged_nv, pt_dev1)
      self.script.DeleteFiles([new_nv, merged_nv])

    if self.spl_merge:
      new_spl =os.path.join(OPTIONS.cache_path, self.target.file_name)
      self.script_ext.MergeSpl(p, new_spl, pt_dev)
      self.script.DeleteFiles([new_spl])

    #if self.file_name == "wcnnvitem.bin":
    #  cache_nv = os.path.join(OPTIONS.cache_path, "wcnnvitem.bin")
    #  self.script_ext.WritePartitionImage(p, cache_nv, pt_dev)
    #  if p1 is not None:
    #    self.script_ext.WritePartitionImage(p1, cache_nv, pt_dev1)
    #  self.script.DeleteFiles([cache_nv])

  #verify patch file source
  def CheckPatchSource(self):
    partition_type, partition_device = common.GetTypeAndDevice(self.partition.mount_point, self.options.info_dict)
    self.script.PatchCheck("%s:%s:%d:%s:%d:%s" %
                      (partition_type, partition_device,
                       self.source.size, self.source.bin.sha1,
                       self.target.size, self.target.bin.sha1))

  # if self.extract is true
  #   if extract dir exist
  #     ApplyPatch to extract dir
  #   else
  #     ApplyPatch to mount point
  # else
  #   ApplyPatch file to device
  def PatchDiffToPartition(self):
    extract = self.IsExtract()
    if extract:
      if self.need_extract is None:
        target_dir = self.partition.mount_point
      else:
        target_dir = self.need_extract
      target = os.path.join(target_dir, self.target.file_name)
    else:
      target = "-"

    # Produce the image by applying a patch to the current
    # contents of the partition, and write it back to the
    # partition or some dir.
    partition_type, partition_device = common.GetTypeAndDevice(self.partition.mount_point, self.options.info_dict)
    self.script.Print("Patching image file %s to %s..." % (self.file_name,target))
    self.script.ApplyPatch("%s:%s:%d:%s:%d:%s"
                           % (partition_type, partition_device,
                              self.source.size, self.source.bin.sha1,
                              self.target.size, self.target.bin.sha1),
                           target,
                           self.target.size, self.target.bin.sha1,
                           self.source.bin.sha1, "patch/" + self.file_name + ".p")

  def RefreshVerifyProcess(self, add_size):
    PartitionUpdater.verify_so_far += add_size
    if PartitionUpdater.total_verify_size == 0:
      progress = 1.0
    else :
      progress = PartitionUpdater.verify_so_far / PartitionUpdater.total_verify_size
    self.script.SetProgress(progress)

  def RefreshUpdateProcess(self, add_size):
    PartitionUpdater.update_so_far += add_size
    if PartitionUpdater.total_update_size == 0:
      progress = 1.0
    else :
      progress = PartitionUpdater.update_so_far / PartitionUpdater.total_update_size
    self.script.SetProgress(progress)

class PartitionFullUpdater(PartitionUpdater):
  """Class for recovery full update"""
  def __init__(self, mount_point, file_name=None, input_dir=None, bootable=False, subdir=None, extract=False, verbatim=False, mount_point2=None, mount_point3=None, nv_merge=None, spl_merge=False):
    print "PartitionFullUpdater %s, %s" % (mount_point, file_name)
    PartitionUpdater.__init__(self, mount_point, file_name, input_dir, extract=extract, verbatim=verbatim, mount_point2=mount_point2, mount_point3=mount_point3,nv_merge=nv_merge,spl_merge=spl_merge)
    self.input = PartitionFile(mount_point, file_name, input_dir, bootable, subdir);
    self.target = self.input
    self.update_flag = True

  def AddToOutputZip(self, output_zip, **kwargs):
    self.verbatim = kwargs.get("verbatim", self.verbatim)
    if (self.input.bin):
      PartitionUpdater.AddToOutputZip(self, output_zip)
      common.ZipWriteStr(output_zip, self.input.file_name, self.input.bin.data)
      PartitionUpdater.total_update_size += self.input.size
    else:
      print ("no target %s; skipping." % self.file_name)

  def Update(self, **kwargs):
    self.extract = kwargs.get("extract", self.extract)
    if (self.input.bin):
      self.FullUpdateToPartition()
      self.RefreshUpdateProcess(self.input.size)

class PartitionIncrementalUpdater(PartitionUpdater):
  """Class for recovery incremental update"""
  def __init__(self, mount_point, file_name=None, target_ver_dir=None, source_ver_dir=None, bootable=False, subdir=None, extract=False, verbatim=False, mount_point2=None, mount_point3=None, nv_merge=None, spl_merge=False):
    print "PartitionIncrementalUpdater %s, %s" % (mount_point, file_name)
    PartitionUpdater.__init__(self, mount_point, file_name, target_ver_dir, source_ver_dir, extract=extract, verbatim=verbatim, mount_point2=mount_point2, mount_point3=mount_point3,nv_merge=nv_merge,spl_merge=spl_merge)

    self.source = PartitionFile(mount_point, file_name, source_ver_dir, bootable, subdir);
    self.target = PartitionFile(mount_point, file_name, target_ver_dir, bootable, subdir);

    self.inc_flag = True

    if (self.source.bin) and (self.target.bin):
      self.update_flag = (self.source.bin.data != self.target.bin.data)
    elif (self.target.bin):
      self.update_flag = True
      self.extract = True

  def AddToOutputZip(self, output_zip, **kwargs):
    self.verbatim = kwargs.get("verbatim", self.verbatim)
    if self.update_flag:
      PartitionUpdater.AddToOutputZip(self, output_zip)
      if self.verbatim:
        # if verbatim, do not make patch, use whole file
        common.ZipWriteStr(output_zip, self.target.file_name, self.target.bin.data)
        print self.file_name + " changed; verbatim."

      else:
        d = common.Difference(self.target.bin, self.source.bin)
        _,_, d = d.ComputePatch()
        print "%-20s target: %d  source: %d  diff: %d" % (
            self.file_name, self.target.bin.size, self.source.bin.size, len(d))

        common.ZipWriteStr(output_zip, "patch/" + self.file_name + ".p", d)
        PartitionUpdater.need_cache_space = max(PartitionUpdater.need_cache_space, self.source.size)

        PartitionUpdater.total_verify_size += self.source.size

      PartitionUpdater.total_update_size += self.target.size

      print ("%-20s changed; including." % self.file_name)
    elif (self.target.bin):
      print ("%-20s unchanged; skipping." % self.file_name)
    else:
      print ("no target %s; skipping." % self.file_name)

  # just patch files need check: verify file is source file or target file
  def Check(self, **kwargs):
    if self.update_flag:
      if self.verbatim:
        pass
      else:
        self.CheckPatchSource()
        self.RefreshVerifyProcess(self.source.size)

  def Update(self, **kwargs):
    self.extract = kwargs.get("extract", self.extract)
    if self.update_flag:
      if self.verbatim:
        self.FullUpdateToPartition()
      else:
        self.PatchDiffToPartition()
      self.RefreshUpdateProcess(self.target.size)

#####################################################################################################

def FullOTA_Assertions(info):
  print "FullOTA_Assertions"

def FullOTA_InstallBegin(info):
  print "FullOTA_InstallBegin"
  script = info.script;
  script_ext = EdifyGeneratorExt(script);
  output_zip = info.output_zip
  input_zip = info.input_zip

  #script.ShowProgress(0.2, 0)
  PartitionUpdater.SetupEnv(script, script_ext, OPTIONS)
  radio_dir = os.path.join(OPTIONS.input_tmp, "RADIO")
  script_ext.AddToZipExt(input_zip, output_zip)

  if OPTIONS.modem_update:
    script_ext.UnpackPackageFile("META-INF/com/google/android/nvmerge", os.path.join(OPTIONS.cache_path, "nvmerge"))
    script_ext.UnpackPackageFile("META-INF/com/google/android/nvmerge.cfg", os.path.join(OPTIONS.cache_path, "nvmerge.cfg"))

  if OPTIONS.uboot_update:
    #spl.bin
    partion_spl = PartitionFullUpdater("/spl", "u-boot-spl-16k.bin", radio_dir, spl_merge=True)
    partion_spl.AddToOutputZip(output_zip)
    #uboot.bin
    partion_uboot = PartitionFullUpdater("/uboot", "u-boot.bin", radio_dir)
    partion_uboot.AddToOutputZip(output_zip)

  if OPTIONS.modem_update:
    ######## GSM ########
    #dsp.bin
    partion_dsp = PartitionFullUpdater("/dsp", "dsp.bin", radio_dir)
    partion_dsp.AddToOutputZip(output_zip)
    #modem.bin
    partion_modem = PartitionFullUpdater("/modem", "modem.bin", radio_dir)
    partion_modem.AddToOutputZip(output_zip)
    #vmjaluna.bin
    partion_vmjaluna = PartitionFullUpdater("/vmjaluna", "vmjaluna.bin", radio_dir)
    partion_vmjaluna.AddToOutputZip(output_zip)
    #nvitem.bin
    partion_nvitem = PartitionFullUpdater("/fixnv", "nvitem.bin", radio_dir, mount_point2="/fixnv1", mount_point3="/fixnv2", nv_merge="")
    partion_nvitem.AddToOutputZip(output_zip)
    ######## WCDMA ########
    #wdsp.bin
    partion_w_dsp = PartitionFullUpdater("/wdsp", "wdsp.bin", radio_dir)
    partion_w_dsp.AddToOutputZip(output_zip)
    #wmodem.bin
    partion_w_modem = PartitionFullUpdater("/wmodem", "wmodem.bin", radio_dir)
    partion_w_modem.AddToOutputZip(output_zip)
    #wnvitem.bin
    partion_w_nvitem = PartitionFullUpdater("/wfixnv", "wnvitem.bin", radio_dir, mount_point2="/wfixnv1", mount_point3="/wfixnv2", nv_merge="w")
    partion_w_nvitem.AddToOutputZip(output_zip)
    ######## TDSCDMA ########
    #tddsp.bin
    partion_td_dsp = PartitionFullUpdater("/tddsp", "tddsp.bin", radio_dir)
    partion_td_dsp.AddToOutputZip(output_zip)
    #tdmodem.bin
    partion_td_modem = PartitionFullUpdater("/tdmodem", "tdmodem.bin", radio_dir)
    partion_td_modem.AddToOutputZip(output_zip)
    #tdnvitem.bin
    partion_td_nvitem = PartitionFullUpdater("/tdfixnv", "tdnvitem.bin", radio_dir, mount_point2="/tdfixnv1", mount_point3="/tdfixnv2", nv_merge="t")
    partion_td_nvitem.AddToOutputZip(output_zip)
    ######## LTE ########
    #ltedsp.bin
    partion_lte_dsp = PartitionFullUpdater("/ltedsp", "ltedsp.bin", radio_dir)
    partion_lte_dsp.AddToOutputZip(output_zip)
    #ltemodem.bin
    partion_lte_modem = PartitionFullUpdater("/ltemodem", "ltemodem.bin", radio_dir)
    partion_lte_modem.AddToOutputZip(output_zip)
    #ltenvitem.bin
    partion_lte_nvitem = PartitionFullUpdater("/ltefixnv", "ltenvitem.bin", radio_dir, mount_point2="/ltefixnv1", mount_point3="/ltefixnv2", nv_merge="l")
    partion_lte_nvitem.AddToOutputZip(output_zip)
    ######## LTEFDD ########
    #lfwarm.bin
    partion_lf_warm = PartitionFullUpdater("/lfwarm", "lfwarm.bin", radio_dir)
    partion_lf_warm.AddToOutputZip(output_zip)
    #lfgdsp.bin
    partion_lf_gdsp = PartitionFullUpdater("/lfgdsp", "lfgdsp.bin", radio_dir)
    partion_lf_gdsp.AddToOutputZip(output_zip)
    #lfldsp.bin
    partion_lf_ldsp = PartitionFullUpdater("/lfldsp", "lfldsp.bin", radio_dir)
    partion_lf_ldsp.AddToOutputZip(output_zip)
    #lfmodem.bin
    partion_lf_modem = PartitionFullUpdater("/lfmodem", "lfmodem.bin", radio_dir)
    partion_lf_modem.AddToOutputZip(output_zip)
    #lfnvitem.bin
    partion_lf_nvitem = PartitionFullUpdater("/lffixnv", "lfnvitem.bin", radio_dir, mount_point2="/lffixnv1", mount_point3="/lffixnv2", nv_merge="l")
    partion_lf_nvitem.AddToOutputZip(output_zip)
    ######## TDDLTE ########
    #tltdsp.bin
    partion_tl_tdsp = PartitionFullUpdater("/tltdsp", "tltdsp.bin", radio_dir)
    partion_tl_tdsp.AddToOutputZip(output_zip)
    #tlldsp.bin
    partion_tl_ldsp = PartitionFullUpdater("/tlldsp", "tlldsp.bin", radio_dir)
    partion_tl_ldsp.AddToOutputZip(output_zip)
    #tlmodem.bin
    partion_tl_modem = PartitionFullUpdater("/tlmodem", "tlmodem.bin", radio_dir)
    partion_tl_modem.AddToOutputZip(output_zip)
    #tlnvitem.bin
    partion_tl_nvitem = PartitionFullUpdater("/tlfixnv", "tlnvitem.bin", radio_dir, mount_point2="/tlfixnv1", mount_point3="/tlfixnvv2", nv_merge="l")
    partion_tl_nvitem.AddToOutputZip(output_zip)
    ######## WCN ########
    #wcnmodem.bin
    partion_wcn_modem = PartitionFullUpdater("/wcnmodem", "wcnmodem.bin", radio_dir)
    partion_wcn_modem.AddToOutputZip(output_zip)
    #wcnnvitem.bin
    partion_wcn_nvitem = PartitionFullUpdater("/wcnfixnv", "wcnnvitem.bin", radio_dir, mount_point2="/wcnfixnv1", mount_point3="/wcnfixnv2", nv_merge="wcn")
    partion_wcn_nvitem.AddToOutputZip(output_zip)

  PartitionUpdater.FreeSpaceCheck()
  #script.ShowProgress(0.6, 0)

  if OPTIONS.uboot_update:
    #spl.bin
    partion_spl.Update()
    #uboot.bin
    partion_uboot.Update()

  if OPTIONS.modem_update:
    ######## GSM ########
    #dsp
    partion_dsp.Update()
    #modem
    partion_modem.Update()
    #vmjaluna.bin
    partion_vmjaluna.Update()
    #nvitem.bin
    partion_nvitem.Update()

    ######## WCDMA ########
    #wdsp
    partion_w_dsp.Update()
    #wmodem
    partion_w_modem.Update()
    #wnvitem.bin
    partion_w_nvitem.Update()

    ######## TDSCDMA ########
    #tddsp
    partion_td_dsp.Update()
    #tdmodem
    partion_td_modem.Update()
    #tdnvitem.bin
    partion_td_nvitem.Update()

    ######## LTE ########
    #ltedsp
    partion_lte_dsp.Update()
    #ltemodem
    partion_lte_modem.Update()
    #ltenvitem.bin
    partion_lte_nvitem.Update()

    ######## LTEFDD ########
    #lfwarm.bin
    partion_lf_warm.Update()
    #lfgdsp.bin
    partion_lf_gdsp.Update()
    #lfldsp.bin
    partion_lf_ldsp.Update()
    #lfmodem.bin
    partion_lf_modem.Update()
    #lfnvitem.bin
    partion_lf_nvitem.Update()

    ######## TDDLTE ########
    #tltdsp.bin
    partion_tl_tdsp.Update()
    #tlldsp.bin
    partion_tl_ldsp.Update()
    #tlmodem.bin
    partion_tl_modem.Update()
    #tlnvitem.bin
    partion_tl_nvitem.Update()

    ######## WCN ########
    #wcnmodem
    partion_wcn_modem.Update()
    #wcnnvitem.bin
    partion_wcn_nvitem.Update()

  if OPTIONS.wipe_product_info:
    partion_productinfo = PartitionUpdater("/productinfo")
    partion_productinfo.FormatPartition("format productinfo ....")

  if OPTIONS.modem_update:
    script.DeleteFiles([os.path.join(OPTIONS.cache_path, "nvmerge"), os.path.join(OPTIONS.cache_path, "nvmerge.cfg")])

def FullOTA_InstallEnd(info):
  print "FullOTA_InstallEnd"

def IncrementalOTA_Assertions(info):
  print "IncrementalOTA_Assertions"

def IncrementalOTA_VerifyBegin(info):
  print "IncrementalOTA_VerifyBegin"

def IncrementalOTA_VerifyEnd(info):
  print "IncrementalOTA_VerifyEnd"

def IncrementalOTA_InstallBegin(info):
  print "IncrementalOTA_InstallBegin"
  script = info.script
  script_ext = EdifyGeneratorExt(script);
  target_zip = info.target_zip
  output_zip = info.output_zip

  PartitionUpdater.SetupEnv(script, script_ext, OPTIONS)
  target_radio_dir = os.path.join(OPTIONS.input_tmp, "RADIO")
  source_radio_dir = os.path.join(OPTIONS.source_tmp, "RADIO")
  script_ext.AddToZipExt(target_zip, output_zip)

  if OPTIONS.modem_update:
    script_ext.UnpackPackageFile("META-INF/com/google/android/nvmerge", os.path.join(OPTIONS.cache_path, "nvmerge"))
    script_ext.UnpackPackageFile("META-INF/com/google/android/nvmerge.cfg", os.path.join(OPTIONS.cache_path, "nvmerge.cfg"))

  if OPTIONS.uboot_update:
    #spl.bin
    partion_spl = PartitionIncrementalUpdater("/spl", "u-boot-spl-16k.bin", target_radio_dir, source_radio_dir, spl_merge=False)
    partion_spl.AddToOutputZip(output_zip)
    #uboot.bin
    partion_uboot = PartitionIncrementalUpdater("/uboot", "u-boot.bin", target_radio_dir, source_radio_dir, verbatim=True)
    partion_uboot.AddToOutputZip(output_zip)

  if OPTIONS.modem_update:
    ######## GSM ########
    #dsp.bin
    partion_dsp = PartitionIncrementalUpdater("/dsp", "dsp.bin", target_radio_dir, source_radio_dir)
    partion_dsp.AddToOutputZip(output_zip)
    #modem.bin
    partion_modem = PartitionIncrementalUpdater("/modem", "modem.bin", target_radio_dir, source_radio_dir)
    partion_modem.AddToOutputZip(output_zip)
    #vmjaluna.bin
    partion_vmjaluna = PartitionIncrementalUpdater("/vmjaluna", "vmjaluna.bin", target_radio_dir, source_radio_dir)
    partion_vmjaluna.AddToOutputZip(output_zip)
    #nvitem.bin
    partion_nvitem = PartitionIncrementalUpdater("/fixnv", "nvitem.bin", target_radio_dir, source_radio_dir, mount_point2="/fixnv1", mount_point3="/fixnv2", nv_merge="")
    partion_nvitem.AddToOutputZip(output_zip)
    ######## WCDMA ########
    #wdsp.bin
    partion_w_dsp = PartitionIncrementalUpdater("/wdsp", "wdsp.bin", target_radio_dir, source_radio_dir)
    partion_w_dsp.AddToOutputZip(output_zip)
    #wmodem.bin
    partion_w_modem = PartitionIncrementalUpdater("/wmodem", "wmodem.bin", target_radio_dir, source_radio_dir)
    partion_w_modem.AddToOutputZip(output_zip)
    #wnvitem.bin
    partion_w_nvitem = PartitionIncrementalUpdater("/wfixnv", "wnvitem.bin", target_radio_dir, source_radio_dir, mount_point2="/wfixnv1", mount_point3="/wfixnv2", nv_merge="w")
    partion_w_nvitem.AddToOutputZip(output_zip)
    ######## TDSCDMA ########
    #tddsp.bin
    partion_td_dsp = PartitionIncrementalUpdater("/tddsp", "tddsp.bin", target_radio_dir, source_radio_dir)
    partion_td_dsp.AddToOutputZip(output_zip)
    #tdmodem.bin
    partion_td_modem = PartitionIncrementalUpdater("/tdmodem", "tdmodem.bin", target_radio_dir, source_radio_dir)
    partion_td_modem.AddToOutputZip(output_zip)
    #tdnvitem.bin
    partion_td_nvitem = PartitionIncrementalUpdater("/tdfixnv", "tdnvitem.bin", target_radio_dir, source_radio_dir, mount_point2="/tdfixnv1", mount_point3="/tdfixnv2", nv_merge="t")
    partion_td_nvitem.AddToOutputZip(output_zip)
    ######## LTE ########
    #ltedsp.bin
    partion_lte_dsp = PartitionIncrementalUpdater("/ltedsp", "ltedsp.bin", target_radio_dir, source_radio_dir)
    partion_lte_dsp.AddToOutputZip(output_zip)
    #ltemodem.bin
    partion_lte_modem = PartitionIncrementalUpdater("/ltemodem", "ltemodem.bin", target_radio_dir, source_radio_dir)
    partion_lte_modem.AddToOutputZip(output_zip)
    #ltenvitem.bin
    partion_lte_nvitem = PartitionIncrementalUpdater("/ltefixnv", "ltenvitem.bin", target_radio_dir, source_radio_dir, mount_point2="/ltefixnv1", mount_point3="/ltefixnv2", nv_merge="l")
    partion_lte_nvitem.AddToOutputZip(output_zip)
    ######## LTEFDD ########
    #lfwarm.bin
    partion_lf_warm = PartitionIncrementalUpdater("/lfwarm", "lfwarm.bin", target_radio_dir, source_radio_dir)
    partion_lf_warm.AddToOutputZip(output_zip)
    #lfgdsp.bin
    partion_lf_gdsp = PartitionIncrementalUpdater("/lfgdsp", "lfgdsp.bin", target_radio_dir, source_radio_dir)
    partion_lf_gdsp.AddToOutputZip(output_zip)
    #lfldsp.bin
    partion_lf_ldsp = PartitionIncrementalUpdater("/lfldsp", "lfldsp.bin", target_radio_dir, source_radio_dir)
    partion_lf_ldsp.AddToOutputZip(output_zip)
    #lfmodem.bin
    partion_lf_modem = PartitionIncrementalUpdater("/lfmodem", "lfmodem.bin", target_radio_dir, source_radio_dir)
    partion_lf_modem.AddToOutputZip(output_zip)
    #lfnvitem.bin
    partion_lf_nvitem = PartitionIncrementalUpdater("/lffixnv", "lfnvitem.bin", target_radio_dir, source_radio_dir, mount_point2="/lffixnv1", mount_point3="/lffixnv2", nv_merge="l")
    partion_lf_nvitem.AddToOutputZip(output_zip)
    ######## TDDLTE ########
    #tltdsp.bin
    partion_tl_tdsp = PartitionIncrementalUpdater("/tltdsp", "tltdsp.bin", target_radio_dir, source_radio_dir)
    partion_tl_tdsp.AddToOutputZip(output_zip)
    #tlldsp.bin
    partion_tl_ldsp = PartitionIncrementalUpdater("/tlldsp", "tlldsp.bin", target_radio_dir, source_radio_dir)
    partion_tl_ldsp.AddToOutputZip(output_zip)
    #tlmodem.bin
    partion_tl_modem = PartitionIncrementalUpdater("/tlmodem", "tlmodem.bin", target_radio_dir, source_radio_dir)
    partion_tl_modem.AddToOutputZip(output_zip)
    #tlnvitem.bin
    partion_tl_nvitem = PartitionIncrementalUpdater("/tlfixnv", "tlnvitem.bin", target_radio_dir, source_radio_dir, mount_point2="/tlfixnv1", mount_point3="/tlfixnvv2", nv_merge="l")
    partion_tl_nvitem.AddToOutputZip(output_zip)
    ######## WCN ########
    #wcnmodem.bin
    partion_wcn_modem = PartitionIncrementalUpdater("/wcnmodem", "wcnmodem.bin", target_radio_dir, source_radio_dir)
    partion_wcn_modem.AddToOutputZip(output_zip)
    #wcnnvitem.bin
    partion_wcn_nvitem = PartitionIncrementalUpdater("/wcnfixnv", "wcnnvitem.bin", target_radio_dir, source_radio_dir, mount_point2="/wcnfixnv1", mount_point3="/wcnfixnv2", nv_merge="wcn")
    partion_wcn_nvitem.AddToOutputZip(output_zip)

  script.Print("Verifying current system...")
  #script.ShowProgress(0.2, 0)
  PartitionUpdater.FreeSpaceCheck()

  if OPTIONS.uboot_update:
    #spl.bin
    partion_spl.Check()
    #uboot.bin
    partion_uboot.Check()

  if OPTIONS.modem_update:
    ######## GSM ########
    #dsp.bin
    partion_dsp.Check()
    #modem.bin
    partion_modem.Check()
    #vmjaluna.bin
    partion_vmjaluna.Check()
    #nvitem.bin
    partion_nvitem.Check()

    ######## WCDMA ########
    #wdsp.bin
    partion_w_dsp.Check()
    #wmodem.bin
    partion_w_modem.Check()
    #wnvitem.bin
    partion_w_nvitem.Check()

    ######## TDSCDMA ########
    #tddsp.bin
    partion_td_dsp.Check()
    #tdmodem.bin
    partion_td_modem.Check()
    #tdnvitem.bin
    partion_td_nvitem.Check()

    ######## LTE ########
    #ltedsp.bin
    partion_lte_dsp.Check()
    #ltemodem.bin
    partion_lte_modem.Check()
    #ltenvitem.bin
    partion_lte_nvitem.Check()

    ######## LTEFDD ########
    #lfwarm.bin
    partion_lf_warm.Check()
    #lfgdsp.bin
    partion_lf_gdsp.Check()
    #lfldsp.bin
    partion_lf_ldsp.Check()
    #lfmodem.bin
    partion_lf_modem.Check()
    #lfnvitem.bin
    partion_lf_nvitem.Check()

    ######## TDDLTE ########
    #tltdsp.bin
    partion_tl_tdsp.Check()
    #tlldsp.bin
    partion_tl_ldsp.Check()
    #tlmodem.bin
    partion_tl_modem.Check()
    #tlnvitem.bin
    partion_tl_nvitem.Check()

    ######## WCN ########
    #wcnmodem.bin
    partion_wcn_modem.Check()
    #wcnnvitem.bin
    partion_wcn_nvitem.Check()

  script.Print("Patching current system...")
  #script.ShowProgress(0.6, 0)

  if OPTIONS.uboot_update:
    #spl.bin
    partion_spl.Update()
    #uboot.bin
    partion_uboot.Update()

  if OPTIONS.modem_update:
    ######## GSM ########
    #dsp
    partion_dsp.Update()
    #modem
    partion_modem.Update()
    #vmjaluna.bin
    partion_vmjaluna.Update()
    #nvitem.bin
    partion_nvitem.Update()

    ######## WCDMA ########
    #wdsp
    partion_w_dsp.Update()
    #wmodem
    partion_w_modem.Update()
    #wnvitem.bin
    partion_w_nvitem.Update()

    ######## TDSCDMA ########
    #tddsp
    partion_td_dsp.Update()
    #tdmodem
    partion_td_modem.Update()
    #tdnvitem.bin
    partion_td_nvitem.Update()

    ######## LTE ########
    #ltedsp
    partion_lte_dsp.Update()
    #ltemodem
    partion_lte_modem.Update()
    #ltenvitem.bin
    partion_lte_nvitem.Update()

    ######## LTEFDD ########
    #lfwarm.bin
    partion_lf_warm.Update()
    #lfgdsp.bin
    partion_lf_gdsp.Update()
    #lfldsp.bin
    partion_lf_ldsp.Update()
    #lfmodem.bin
    partion_lf_modem.Update()
    #lfnvitem.bin
    partion_lf_nvitem.Update()

    ######## TDDLTE ########
    #tltdsp.bin
    partion_tl_tdsp.Update()
    #tlldsp.bin
    partion_tl_ldsp.Update()
    #tlmodem.bin
    partion_tl_modem.Update()
    #tlnvitem.bin
    partion_tl_nvitem.Update()

    ######## WCN ########
    #wcnmodem
    partion_wcn_modem.Update()
    #wcnnvitem.bin
    partion_wcn_nvitem.Update()

  if OPTIONS.wipe_product_info:
    partion_productinfo = PartitionUpdater("/productinfo")
    partion_productinfo.FormatPartition("format productinfo ....")

  if OPTIONS.modem_update:
    script.DeleteFiles([os.path.join(OPTIONS.cache_path, "nvmerge"), os.path.join(OPTIONS.cache_path, "nvmerge.cfg")])

def IncrementalOTA_InstallEnd(info):
  print "IncrementalOTA_InstallEnd"
