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

  def WriteEmmcImage(self, filename, device_path):
    """write the given filename into the partition for the
    given device path"""
    self.script.append(
            'write_emmc_image("%s", "%s");' % (filename, device_path))

  def AddToZipExt(self, input_zip, output_zip, input_path=None):
    """Write the accumulated script to the output_zip file.  input_zip
    is used as the source for the 'updater' binary needed to run
    script.  If input_path is not None, it will be used as a local
    path for the binary instead of input_zip."""

    if input_path is None:
      data_nv = input_zip.read("OTA/bin/nvmerge")
      data_cfg = input_zip.read("OTA/bin/nvmerge.cfg")
      data_spl = input_zip.read("OTA/bin/splmerge")
    else:
      data_nv = open(os.path.join(input_path, "nvmerge")).read()
      data_cfg = open(os.path.join(input_path, "nvmerge.cfg")).read()
      data_spl = open(os.path.join(input_path, "splmerge")).read()

    common.ZipWriteStr(output_zip, "META-INF/com/google/android/nvmerge",
                       data_nv, perms=0755)
    common.ZipWriteStr(output_zip, "META-INF/com/google/android/nvmerge.cfg",
                       data_cfg)
    common.ZipWriteStr(output_zip, "META-INF/com/google/android/splmerge",
                       data_spl, perms=0755)

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
  def IfNeedImageFile(cls, mount_point):
    if mount_point in ("/system", "/data", "/runtimenv", "/backupfixnv", "/productinfo"):
      return False
    else:
      return True

  @classmethod
  def ComputeSdcardSpace(cls, updater):
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

  def __init__(self, mount_point, file_name=None, target_ver_dir=None, source_ver_dir=None, extract=False, verbatim=False, mount_point2=None, mount_point3=None):
    self.script = PartitionUpdater.script
    self.options = PartitionUpdater.options

    self.mount_point = mount_point
    self.file_name = file_name
    self.extract = extract
    self.verbatim = verbatim
    self.update_flag = False
    self.inc_flag = False
    self.mount_point2 = mount_point2
    self.mount_point3 = mount_point3
    self.need_extract = None
    fstab = self.options.info_dict["fstab"]
    if fstab is None:
      raise common.ExternalError("no fstab")
    self.partition = fstab.get(mount_point, None)
    if mount_point == "/spl" or mount_point == "/fixnv" or mount_point == "/wfixnv" or mount_point == "/tdfixnv" or mount_point == "/wcnfixnv":
      self.need_extract = "/cache"
    if self.partition is None and mount_point2 is not None:
      self.partition = fstab.get(mount_point2, None)
    if self.partition is None:
      print ("[Warning:] no patition in fstab for mount point %s" % mount_point)
      self.partition = Partition()
      self.partition.mount_point = mount_point
      self.partition.extract = "/cache"
      self.partition.fs_type = "yaffs2"
      print ("[Warning:] auto create patition %s" % self.partition)
      self.extract = True
      self.verbatim = True

    if target_ver_dir is None and PartitionUpdater.IfNeedImageFile(mount_point):
      raise common.ExternalError("init PartitionUpdater error")

  def AddToOutputZip(self, output_zip, **kwargs):
    PartitionUpdater.ComputeSdcardSpace(self)

  def Check(self, **kwargs):
    pass

  def Update(self, **kwargs):
    pass

  def Formated(self):
    if self.partition.fs_type in ("yaffs2", "mtd", "ext4"):
      return True
    return False

  def FormatPartition(self, format_msg=None):
    if self.Formated():
      if format_msg is not None:
        self.script.Print(format_msg)
      self.script.FormatPartition(self.partition.mount_point)

  def IsExtract(self):
    if self.partition.fs_type in ("yaffs2", "ext4"):
      return True
    else:
      return self.extract

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
    if extract:
      self.script_ext.UnpackPackageFile(self.target.file_name, os.path.join(mount_point, self.target.file_name))
    else:
      self.script.WriteRawImage(mount_point, self.target.file_name)
    if(self.file_name == "nvitem.bin") or (self.file_name == "wnvitem.bin") or (self.file_name == "tdnvitem.bin"):
      nvmerge_cfg = "/cache/nvmerge.cfg"
      part = self.options.info_dict["fstab"].get(self.mount_point, None)
      if part is not None:
        p = self.options.info_dict["fstab"][self.mount_point]
      else:
        p = self.options.info_dict["fstab"][self.mount_point2]
        p1 = self.options.info_dict["fstab"][self.mount_point3]
        nv_dev1 = p1.device
      nv_dev = p.device
      new_nv = "/cache/" + self.target.file_name
      merged_nv = "/cache/" + "merged_" + self.target.file_name
      self.script_ext.Run_program("/cache/nvmerge", nvmerge_cfg, nv_dev, new_nv, merged_nv)
      self.script_ext.WriteEmmcImage(merged_nv, nv_dev)
      if part is None:
        self.script_ext.WriteEmmcImage(merged_nv, nv_dev1)
      self.script.DeleteFiles([new_nv, merged_nv])
    if self.file_name == "u-boot-spl-16k.bin":
      p = self.options.info_dict["fstab"][self.mount_point]
      spl_dev = p.device
      new_spl = "/cache/" + self.target.file_name
      merged_spl = "/cache/" + "merged_" + self.target.file_name
      self.script_ext.Run_program("/cache/splmerge", new_spl, merged_spl)
      self.script_ext.WriteEmmcImage(merged_spl, spl_dev)
      self.script.DeleteFiles([new_spl, merged_spl])
    if self.file_name == "wcnnvitem.bin":
      p = self.options.info_dict["fstab"][self.mount_point2]
      p1 = self.options.info_dict["fstab"][self.mount_point3]
      nv_dev = p.device
      nv_dev1 = p1.device
      self.script_ext.WriteEmmcImage("/cache/" + self.target.file_name, nv_dev)
      self.script_ext.WriteEmmcImage("/cache/" + self.target.file_name, nv_dev1)
      self.script.DeleteFiles(["/cache/" + self.target.file_name])

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
  def __init__(self, mount_point, file_name=None, input_dir=None, bootable=False, subdir=None, extract=False, verbatim=False, mount_point2=None, mount_point3=None):
    print "PartitionFullUpdater %s, %s" % (mount_point, file_name)
    PartitionUpdater.__init__(self, mount_point, file_name, input_dir, extract=extract, verbatim=verbatim, mount_point2=mount_point2, mount_point3=mount_point3)
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
  def __init__(self, mount_point, file_name=None, target_ver_dir=None, source_ver_dir=None, bootable=False, subdir=None, extract=False, verbatim=False, mount_point2=None, mount_point3=None):
    print "PartitionIncrementalUpdater %s, %s" % (mount_point, file_name)
    PartitionUpdater.__init__(self, mount_point, file_name, target_ver_dir, source_ver_dir, extract=extract, verbatim=verbatim, mount_point2=mount_point2, mount_point3=mount_point3)

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

  script.ShowProgress(0.2, 0)
  PartitionUpdater.SetupEnv(script, script_ext, OPTIONS)
  radio_dir = os.path.join(OPTIONS.input_tmp, "RADIO")
  script_ext.AddToZipExt(input_zip, output_zip)

  if OPTIONS.modem_update:
    script_ext.UnpackPackageFile("META-INF/com/google/android/nvmerge", "/cache/nvmerge")
    script_ext.UnpackPackageFile("META-INF/com/google/android/nvmerge.cfg", "/cache/nvmerge.cfg")
  if OPTIONS.uboot_update:
    script_ext.UnpackPackageFile("META-INF/com/google/android/splmerge", "/cache/splmerge")

  if OPTIONS.uboot_update:
    #spl.bin
    partion_spl = PartitionFullUpdater("/spl", "u-boot-spl-16k.bin", radio_dir, verbatim=True, extract=True)
    partion_spl.AddToOutputZip(output_zip)
    #uboot.bin
    partion_uboot = PartitionFullUpdater("/uboot", "u-boot.bin", radio_dir)
    partion_uboot.AddToOutputZip(output_zip)

  if OPTIONS.modem_update:
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
    partion_nvitem = PartitionFullUpdater("/fixnv", "nvitem.bin", radio_dir, extract=True, mount_point2="/fixnv1", mount_point3="/fixnv2")
    partion_nvitem.AddToOutputZip(output_zip)
	#dsp.bin(w)
    w_partion_dsp = PartitionFullUpdater("/wdsp", "wdsp.bin", radio_dir)
    w_partion_dsp.AddToOutputZip(output_zip)
    #modem.bin(w)
    w_partion_modem = PartitionFullUpdater("/wmodem", "wmodem.bin", radio_dir)
    w_partion_modem.AddToOutputZip(output_zip)
    #nvitem.bin(w)
    w_partion_nvitem = PartitionFullUpdater("/wfixnv", "wnvitem.bin", radio_dir, extract=True, mount_point2="/wfixnv1", mount_point3="/wfixnv2")
    w_partion_nvitem.AddToOutputZip(output_zip)
	#dsp.bin(td)
    td_partion_dsp = PartitionFullUpdater("/tddsp", "tddsp.bin", radio_dir)
    td_partion_dsp.AddToOutputZip(output_zip)
    #modem.bin(td)
    td_partion_modem = PartitionFullUpdater("/tdmodem", "tdmodem.bin", radio_dir)
    td_partion_modem.AddToOutputZip(output_zip)
    #nvitem.bin(td)
    td_partion_nvitem = PartitionFullUpdater("/tdfixnv", "tdnvitem.bin", radio_dir, extract=True, mount_point2="/tdfixnv1", mount_point3="/tdfixnv2")
    td_partion_nvitem.AddToOutputZip(output_zip)
    #modem.bin(wcn)
    wcn_partion_modem = PartitionFullUpdater("/wcnmodem", "wcnmodem.bin", radio_dir)
    wcn_partion_modem.AddToOutputZip(output_zip)
    #nvitem.bin(wcn)
    wcn_partion_nvitem = PartitionFullUpdater("/wcnfixnv", "wcnnvitem.bin", radio_dir, extract=True, mount_point2="/wcnfixnv1", mount_point3="/wcnfixnv2")
    wcn_partion_nvitem.AddToOutputZip(output_zip)

  PartitionUpdater.FreeSpaceCheck()
  script.ShowProgress(0.6, 0)

  if OPTIONS.uboot_update:
    #spl.bin
    partion_spl.Update()
    #uboot.bin
    partion_uboot.Update()

  if OPTIONS.modem_update:
    #dsp
    partion_dsp.Update()
    #modem
    partion_modem.Update()
    #vmjaluna.bin
    partion_vmjaluna.Update()
    #nvitem.bin
    partion_nvitem.Update()

    #dsp(w)
    w_partion_dsp.Update()
    #modem(w)
    w_partion_modem.Update()
    #nvitem.bin(w)
    w_partion_nvitem.Update()

    #dsp(td)
    td_partion_dsp.Update()
    #modem(td)
    td_partion_modem.Update()
    #nvitem.bin(td)
    td_partion_nvitem.Update()

    #modem(wcn)
    wcn_partion_modem.Update()
    #nvitem.bin(wcn)
    wcn_partion_nvitem.Update()

  if OPTIONS.wipe_product_info:
    partion_productinfo = PartitionUpdater("/productinfo")
    partion_productinfo.FormatPartition("format productinfo ....")

  if OPTIONS.modem_update:
    script.DeleteFiles(["/cache/nvmerge", "/cache/nvmerge.cfg"])
  if OPTIONS.uboot_update:
    script.DeleteFiles(["/cache/splmerge"])

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
    script_ext.UnpackPackageFile("META-INF/com/google/android/nvmerge", "/cache/nvmerge")
    script_ext.UnpackPackageFile("META-INF/com/google/android/nvmerge.cfg", "/cache/nvmerge.cfg")
  if OPTIONS.uboot_update:
    script_ext.UnpackPackageFile("META-INF/com/google/android/splmerge", "/cache/splmerge")

  if OPTIONS.uboot_update:
    #spl.bin
    partion_spl = PartitionIncrementalUpdater("/spl", "u-boot-spl-16k.bin", target_radio_dir, source_radio_dir, verbatim=True, extract=True)
    partion_spl.AddToOutputZip(output_zip)
    #uboot.bin
    partion_uboot = PartitionIncrementalUpdater("/uboot", "u-boot.bin", target_radio_dir, source_radio_dir)
    partion_uboot.AddToOutputZip(output_zip)

  if OPTIONS.modem_update:
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
    partion_nvitem = PartitionIncrementalUpdater("/fixnv", "nvitem.bin", target_radio_dir, source_radio_dir, extract=True, verbatim=True, mount_point2="/fixnv1", mount_point3="/fixnv2")
    partion_nvitem.AddToOutputZip(output_zip)
    #dsp.bin(w)
    w_partion_dsp = PartitionIncrementalUpdater("/wdsp", "wdsp.bin", target_radio_dir, source_radio_dir)
    w_partion_dsp.AddToOutputZip(output_zip)
    #modem.bin(w)
    w_partion_modem = PartitionIncrementalUpdater("/wmodem", "wmodem.bin", target_radio_dir, source_radio_dir)
    w_partion_modem.AddToOutputZip(output_zip)
    #nvitem.bin(w)
    w_partion_nvitem = PartitionIncrementalUpdater("/wfixnv", "wnvitem.bin", target_radio_dir, source_radio_dir, extract=True, verbatim=True, mount_point2="/wfixnv1", mount_point3="/wfixnv2")
    w_partion_nvitem.AddToOutputZip(output_zip)
    #dsp.bin(td)
    td_partion_dsp = PartitionIncrementalUpdater("/tddsp", "tddsp.bin", target_radio_dir, source_radio_dir)
    td_partion_dsp.AddToOutputZip(output_zip)
    #modem.bin(td)
    td_partion_modem = PartitionIncrementalUpdater("/tdmodem", "tdmodem.bin", target_radio_dir, source_radio_dir)
    td_partion_modem.AddToOutputZip(output_zip)
    #nvitem.bin(td)
    td_partion_nvitem = PartitionIncrementalUpdater("/tdfixnv", "tdnvitem.bin", target_radio_dir, source_radio_dir, extract=True, verbatim=True, mount_point2="/tdfixnv1", mount_point3="/tdfixnv2")
    td_partion_nvitem.AddToOutputZip(output_zip)
    #modem.bin(wcn)
    wcn_partion_modem = PartitionIncrementalUpdater("/wcnmodem", "wcnmodem.bin", target_radio_dir, source_radio_dir)
    wcn_partion_modem.AddToOutputZip(output_zip)
    #nvitem.bin(wcn)
    wcn_partion_nvitem = PartitionIncrementalUpdater("/wcnfixnv", "wcnnvitem.bin", target_radio_dir, source_radio_dir, extract=True, verbatim=True, mount_point2="/wcnfixnv1", mount_point3="/wcnfixnv2")
    wcn_partion_nvitem.AddToOutputZip(output_zip)

  script.Print("Verifying current system...")
  script.ShowProgress(0.2, 0)
  PartitionUpdater.FreeSpaceCheck()

  if OPTIONS.uboot_update:
    #spl.bin
    partion_spl.Check()
    #uboot.bin
    partion_uboot.Check()

  if OPTIONS.modem_update:
    #dsp.bin
    partion_dsp.Check()
    #modem.bin
    partion_modem.Check()
    #vmjaluna.bin
    partion_vmjaluna.Check()
    #nvitem.bin
    partion_nvitem.Check()

    #dsp.bin(w)
    w_partion_dsp.Check()
    #modem.bin(w)
    w_partion_modem.Check()
    #nvitem.bin(w)
    w_partion_nvitem.Check()

    #dsp.bin(td)
    td_partion_dsp.Check()
    #modem.bin(td)
    td_partion_modem.Check()
    #nvitem.bin(td)
    td_partion_nvitem.Check()

    #modem.bin(wcn)
    wcn_partion_modem.Check()
    #nvitem.bin(wcn)
    wcn_partion_nvitem.Check()

  script.Print("Patching current system...")
  script.ShowProgress(0.6, 0)

  if OPTIONS.uboot_update:
    #spl.bin
    partion_spl.Update()
    #uboot.bin
    partion_uboot.Update()

  if OPTIONS.modem_update:
    #dsp
    partion_dsp.Update()
    #modem
    partion_modem.Update()
    #vmjaluna.bin
    partion_vmjaluna.Update()
    #nvitem.bin
    partion_nvitem.Update()

    #dsp(w)
    w_partion_dsp.Update()
    #modem(w)
    w_partion_modem.Update()
    #nvitem.bin(w)
    w_partion_nvitem.Update()

    #dsp(td)
    td_partion_dsp.Update()
    #modem(td)
    td_partion_modem.Update()
    #nvitem.bin(td)
    td_partion_nvitem.Update()

    #modem(wcn)
    wcn_partion_modem.Update()
    #nvitem.bin(wcn)
    wcn_partion_nvitem.Update()

  if OPTIONS.wipe_product_info:
    partion_productinfo = PartitionUpdater("/productinfo")
    partion_productinfo.FormatPartition("format productinfo ....")

  if OPTIONS.modem_update:
    script.DeleteFiles(["/cache/nvmerge", "/cache/nvmerge.cfg"])
  if OPTIONS.uboot_update:
    script.DeleteFiles(["/cache/splmerge"])

def IncrementalOTA_InstallEnd(info):
  print "IncrementalOTA_InstallEnd"
