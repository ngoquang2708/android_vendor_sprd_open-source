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
# format: type,mount_point,file_name,mount_point2,mount_point3,verbatim,nv_merge,spl_merge
#    type            update type, volid values is A B M means android bootloader modem
#    mount_point     update partition main mount point, used to recorgnize partition
#    file_name       the name of file to write to the partition
#    mount_point2    second mount point for partition
#    mount_point3    third mount point for partition
#    verbatim        when do incremental update, force do full update
#    nv_merge        this partition is need processed by nvmerge, and volid values is
#                      default, w, t, l, wcn and so on
#    spl_merge       this partition is need processed by splmerge

default_updaters_config="""
#### boot loader ####
B,/spl,u-boot-spl-16k.bin,,,,,true
B,/uboot,u-boot.bin,,,true,,
#### GSM ####
M,/dsp,dsp.bin,,,,,
M,/modem,modem.bin,,,,,
M,/vmjaluna,vmjaluna.bin,,,,,
M,/fixnv,nvitem.bin,/fixnv1,/fixnv2,,default,
#### WCDMA ####
M,/wdsp,wdsp.bin,,,,,
M,/wmodem,wmodem.bin,,,,,
M,/wfixnv,wnvitem.bin,/wfixnv1,/wfixnv2,,w,
#### TDSCDMA ####
M,/tddsp,tddsp.bin,,,,,
M,/tdmodem,tdmodem.bin,,,,,
M,/tdfixnv,tdnvitem.bin,/tdfixnv1,/tdfixnv2,,t,
#### LTE ####
M,/ltedsp,ltedsp.bin,,,,,
M,/ltemodem,ltemodem.bin,,,,,
M,/ltefixnv,ltenvitem.bin,/ltefixnv1,/ltefixnv2,,l,
#### LTEFDD ####
M,/lfwarm,lfwarm.bin,,,,,
M,/lfgdsp,lfgdsp.bin,,,,,
M,/lfldsp,lfldsp.bin,,,,,
M,/lfmodem,lfmodem.bin,,,,,
M,/lffixnv,lfnvitem.bin,/lffixnv1,/lffixnv2,,l,
#### TDDLTE ####
M,/tltdsp,tltdsp.bin,,,,,
M,/tltgdsp,tltgdsp.bin,,,,,
M,/tlldsp,tlldsp.bin,,,,,
M,/tlmodem,tlmodem.bin,,,,,
M,/tlfixnv,tlnvitem.bin,/tlfixnv1,/tlfixnv2,,tl,
#### WCN ####
M,/wcnmodem,wcnmodem.bin,,,,,
M,/wcnfixnv,wcnnvitem.bin,/wcnfixnv1,/wcnfixnv2,,wcn,
#### other ####
M,/pmsys,pmsys.bin,,,,,
"""

class UpdaterConfig(object):
  def __init__(self, update_type, mount_point, file_name, mount_point2, mount_point3, verbatim, nv_merge, spl_merge):
    print "UpdaterConfig(%s,%s,%s,%s,%s,%s,%s,%s)" % (update_type, mount_point, file_name, mount_point2, mount_point3, verbatim, nv_merge, spl_merge)
    self.update_type = update_type
    self.mount_point = mount_point
    self.file_name = file_name
    if verbatim == "true":
      self.verbatim = True
    else:
      self.verbatim = False
    self.mount_point2 = mount_point2
    self.mount_point3 = mount_point3
    if nv_merge == "default":
      self.nv_merge = ""
    else:
      self.nv_merge = nv_merge
    if spl_merge == "true":
      self.spl_merge = True
    else:
      self.spl_merge = False

def GetAllUpdaterConfigs():
  configs = []
  for line in default_updaters_config.split("\n"):
    line = line.strip()
    if not line or line.startswith("#"): continue
    pieces = line.split(",")
    config = UpdaterConfig(pieces[0],pieces[1],pieces[2],pieces[3],pieces[4],pieces[5],pieces[6],pieces[7])
    configs.append(config)

  return configs

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

  partitions = []
  configs = GetAllUpdaterConfigs()

  for config in configs:
    if (OPTIONS.uboot_update and config.update_type == "B") or (OPTIONS.modem_update and config.update_type == "M"):
      partition = PartitionFullUpdater(config.mount_point, config.file_name, radio_dir,
                                     verbatim=config.verbatim,
                                     mount_point2=config.mount_point2,
                                     mount_point3=config.mount_point3,
                                     nv_merge=config.nv_merge,
                                     spl_merge=config.spl_merge)
      partition.AddToOutputZip(output_zip)
      partitions.append(partition)

  PartitionUpdater.FreeSpaceCheck()
  #script.ShowProgress(0.6, 0)

  for partition in partitions:
    partition.Update()

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

  partitions = []
  configs = GetAllUpdaterConfigs()

  for config in configs:
    if (OPTIONS.uboot_update and config.update_type == "B") or (OPTIONS.modem_update and config.update_type == "M"):
      partition = PartitionIncrementalUpdater(config.mount_point, config.file_name, target_radio_dir, source_radio_dir,
                                     verbatim=config.verbatim,
                                     mount_point2=config.mount_point2,
                                     mount_point3=config.mount_point3,
                                     nv_merge=config.nv_merge,
                                     spl_merge=config.spl_merge)
      partition.AddToOutputZip(output_zip)
      partitions.append(partition)

  script.Print("Verifying current system...")
  #script.ShowProgress(0.2, 0)
  PartitionUpdater.FreeSpaceCheck()

  for partition in partitions:
    partition.Check()

  script.Print("Patching current system...")
  #script.ShowProgress(0.6, 0)

  for partition in partitions:
    partition.Update()

  if OPTIONS.wipe_product_info:
    partion_productinfo = PartitionUpdater("/productinfo")
    partion_productinfo.FormatPartition("format productinfo ....")

  if OPTIONS.modem_update:
    script.DeleteFiles([os.path.join(OPTIONS.cache_path, "nvmerge"), os.path.join(OPTIONS.cache_path, "nvmerge.cfg")])

def IncrementalOTA_InstallEnd(info):
  print "IncrementalOTA_InstallEnd"
