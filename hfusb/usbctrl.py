#! /usr/bin/env python3

# requires pyusb
#   pip install --pre pyusb

import usb.core
import usb.util
import sys
import time

HF_CTRL_DEVICE_NOT_FOUND     = 'HFCtrlDevice Not Found'
HF_CTRL_DEVICE_FOUND         = 'HFCtrlDevice Found!'

def poll_hf_ctrl_device(intv=1, printer=print):
  # look for device
  while 1:
    time.sleep(intv)
    try:
      dev = HFCtrlDevice()
      break
    except:
      printer(HF_CTRL_DEVICE_NOT_FOUND)
  # found device
  printer(HF_CTRL_DEVICE_FOUND)
  return dev

# libusb_direction
LIBUSB_ENDPOINT_IN               = 0x80
LIBUSB_ENDPOINT_OUT              = 0x00
# libusb_standard_request
LIBUSB_REQUEST_GET_STATUS        = 0x00
LIBUSB_REQUEST_CLEAR_FEATURE     = 0x01
LIBUSB_REQUEST_SET_FEATURE       = 0x03
LIBUSB_REQUEST_SET_ADDRESS       = 0x05
LIBUSB_REQUEST_GET_DESCRIPTOR    = 0x06
LIBUSB_REQUEST_SET_DESCRIPTOR    = 0x07
LIBUSB_REQUEST_GET_CONFIGURATION = 0x08
LIBUSB_REQUEST_SET_CONFIGURATION = 0x09
LIBUSB_REQUEST_GET_INTERFACE     = 0x0A
LIBUSB_REQUEST_SET_INTERFACE     = 0x0B
LIBUSB_REQUEST_SYNCH_FRAME       = 0x0C
LIBUSB_REQUEST_SET_SEL           = 0x30
LIBUSB_SET_ISOCH_DELAY           = 0x31
# libusb_request_type
LIBUSB_REQUEST_TYPE_STANDARD     = 0x00 # (0x00 << 5)
LIBUSB_REQUEST_TYPE_CLASS        = 0x20 # (0x01 << 5)
LIBUSB_REQUEST_TYPE_VENDOR       = 0x40 # (0x02 << 5)
LIBUSB_REQUEST_TYPE_RESERVED     = 0x60 # (0x03 << 5)
# libusb_request_recipient
LIBUSB_RECIPIENT_DEVICE          = 0x00
LIBUSB_RECIPIENT_INTERFACE       = 0x01
LIBUSB_RECIPIENT_ENDPOINT        = 0x02
LIBUSB_RECIPIENT_OTHER           = 0x03

# hf_usbctrl
HF_USBCTRL_REBOOT                = 0x60
HF_USBCTRL_VERSION               = 0x61
HF_USBCTRL_CONFIG                = 0x62
HF_USBCTRL_STATUS                = 0x63
HF_USBCTRL_SERIAL                = 0x67
HF_USBCTRL_NAME                  = 0x70
HF_USBCTRL_FAN                   = 0x71
HF_USBCTRL_POWER                 = 0x72
HF_USBCTRL_FAN_PARMS             = 0x73  
HF_USBCTRL_ASIC_PARMS            = 0x74
HF_USBCTRL_VOLTAGE               = 0x75 
HF_USBCTRL_CORE_OVERVIEW         = 0xa0
HF_USBCTRL_CORE_ENABLE           = 0xa1
HF_USBCTRL_CORE_DISABLE          = 0xa2
HF_USBCTRL_CORE_CLEAR            = 0xa3
HF_USBCTRL_CORE_STATUS           = 0xa4
HF_USBCTRL_CORE_DIE_STATUS       = 0xa5
HF_USBCTRL_CORE_ASIC_STATUS      = 0xa6
HF_USBCTRL_DEBUG_BUFFER          = 0xd0
HF_USBCTRL_DEBUG_STREAM          = 0xd1
HF_USBCTRL_DEBUG_CLI             = 0xd2

def to_uint16(b):
  result = 0
  for x in range(0,2):
    result |= b[x] << x*8
  return result

def to_uint32(b):
  result = 0
  for x in range(0,4):
    result |= b[x] << x*8
  return result

def to_uint64(b):
  result = 0
  for x in range(0,8):
    result |= b[x] << x*8
  return result

def irange(s,e,i=1):
  return list(range(s,e+i,i))

cores_die01 = [ irange(00,10),irange(21,11,-1),
                irange(22,32),irange(42,33,-1),
                irange(43,52),irange(62,53,-1),
                irange(63,73),irange(84,74,-1),
                irange(85,95)]
cores_die01[3].insert(0,-1)
cores_die01[4].insert(0,-1)
cores_die01[5].insert(0,-1)
cores_die23 = [ irange(95,85,-1),irange(74,84),
                irange(73,63,-1),irange(53,62),
                irange(52,43,-1),irange(33,42),
                irange(32,22,-1),irange(11,21),
                irange(10,00,-1)]
cores_die23[3].append(-1)
cores_die23[4].append(-1)
cores_die23[5].append(-1)

class HFCtrlReboot():
  pass

class HFCtrlVersion():
  def __init__(self, usbBuffer):
    if len(usbBuffer) >= 4:
      self.version                = to_uint32(usbBuffer[0:4])
    else: self.version            = None
    if len(usbBuffer) >= 9:
      self.crc                    = to_uint32(usbBuffer[5:9])
    else: self.crc                = None
    if len(usbBuffer) >= 10:
      self.type                   = usbBuffer[9]
    else: self.type               = None
  def __str__(self):
    string  = "HFCtrlVersion\n"
    string += "Version:           {}\n".format(self.version)
    string += "CRC:               {:#x}\n".format(self.crc)
    string += "Type:              {}\n".format(self.type)
    return string

class HFCtrlConfig():
  def __init__(self, usbBuffer):
    if len(usbBuffer) >= 4:
      self.type                   = usbBuffer[0]
      self.modules                = usbBuffer[1] + 1
      self.initialized            = usbBuffer[3]
    else:
      self.type                   = None
      self.modules                = 0
      self.initialized            = None
  def __str__(self):
    string  = "HFCtrlConfig\n"
    string += "Type:              {}\n".format(self.type)
    string += "Modules:           {}\n".format(self.modules)
    string += "Initialized:       {}\n".format(self.initialized)
    return string


class HFCtrlStatus():
  def __init__(self, usbBuffer):
    if len(usbBuffer) >= 4:
      self.power                  = to_uint32(usbBuffer[0:4])
    if len(usbBuffer) >= 6:
      self.fault_code             = usbBuffer[4]
      self.extra                  = usbBuffer[5]
  def __str__(self):
    string  = "HFCtrlStatus\n"
    string += "Power:             {}\n".format(self.power)
    string += "Fault Code:        {}\n".format(self.fault_code)
    string += "Extra:             {}\n".format(self.extra)
    return string

class HFCtrlSerial():
  def __init__(self, usbBuffer):
    if len(usbBuffer) >= 28:
      self.serial                 = usbBuffer
    else: self.serial             = None
  def __str__(self):
    string  = "HFCtrlSerial\n"
    string += "Serial:            "
    string += " ".join('{:x}'.format(x) for x in self.serial[8:-4])
    string += "\n"
    return string

class HFCtrlName():
  def __init__(self, usbBuffer):
    if len(usbBuffer) >= 0:
      self.name                   = usbBuffer
    else: self.name               = None
  def __str__(self):
      string  = "HFCtrlName\n"
      string += "Name:              "
      string += "".join('{:c}'.format(x) for x in self.name)
      string += "\n"
      return string

class HFCtrlFan():
  def __init__(self, usbBuffer):
    self.tachometers = [0]*4
    if len(usbBuffer) >= 10:
      for i in range(0,4):
        self.tachometers[i]       = usbBuffer[i*2 + 2] | (usbBuffer[i*2 + 3] << 8)
  def __str__(self):
    string  = "HFCtrlFan\n"
    string += "Tachometers:       {}\n".format(self.tachometers)
    return string

class HFCtrlPower():
  def __init__(self, usbBuffer):
    self.voltage_in = [0]*4
    self.voltage_out = [0]*4
    self.temperature = [0]*4
    if len(usbBuffer) >= 26:
      for i in range(0,4):
        self.voltage_in[i]        = usbBuffer[i*6 + 2] | (usbBuffer[i*6 + 3] << 8)
        self.voltage_out[i]       = usbBuffer[i*6 + 4] | (usbBuffer[i*6 + 5] << 8)
        self.temperature[i]       = usbBuffer[i*6 + 6] | (usbBuffer[i*6 + 7] << 8)
  def __str__(self):
    string  = "HFCtrlPower\n"
    string += "Voltage IN:        {}\n".format(self.voltage_in)
    string += "Voltage OUT:       {}\n".format(self.voltage_out)
    string += "Temperature:       {}\n".format(self.temperature)
    return string

class HFCtrlCoreOverview():
  def __init__(self, usbBuffer):
    if len(usbBuffer) >= 18:
      self.die_count              = usbBuffer[0]
      self.core_count             = usbBuffer[1]
      self.total_cores            = to_uint16(usbBuffer[2:4])
      self.total_good_cores       = to_uint16(usbBuffer[4:6])
      self.shed_supported         = usbBuffer[6]
      self.groups                 = to_uint16(usbBuffer[7:9])
      self.cores_per_group        = usbBuffer[9]
      self.cores_per_group_cycle  = to_uint16(usbBuffer[10:12])
      self.groups_per_group_cylce = usbBuffer[12]
      self.group_core_offset      = usbBuffer[13]
      self.inflight               = to_uint16(usbBuffer[14:16])
      self.active_jobs            = to_uint16(usbBuffer[16:18])
    else:
      self.die_count              = 0
      self.core_count             = 0
      self.total_cores            = 0
      self.total_good_cores       = 0
      self.shed_supported         = 0
      self.groups                 = 0
      self.cores_per_group        = 0
      self.cores_per_group_cycle  = 0
      self.groups_per_group_cylce = 0
      self.group_core_offset      = 0
      self.inflight               = 0
      self.active_jobs            = 0
    if len(usbBuffer) >= 21:
      self.group_mask             = to_uint16(usbBuffer[18:20])
      self.group_shift            = usbBuffer[20]
    else:
      self.group_mask             = 0
      self.group_shift            = 0
  def __str__(self):
    string  = "HFCtrlCoreOverview\n"
    string += "Die Count:         {}\n".format(self.die_count)
    string += "Core Count:        {}\n".format(self.core_count)
    string += "Total Cores:       {}\n".format(self.total_cores)
    string += "Total Good Cores:  {}\n".format(self.total_good_cores)
    string += "Shed Suppoted:     {}\n".format(self.shed_supported)
    string += "Inflight:          {}\n".format(self.inflight)
    string += "Active Jobs:       {}\n".format(self.active_jobs)
    return string

class HFCtrlCoreEnable():
  pass

class HFCtrlCoreDisable():
  pass

class HFCtrlCoreClear():
  pass

class HFCtrlCoreStatus():
  def __init__(self, usbBuffer):
    if len(usbBuffer) >= 2:
      self.core_good              = usbBuffer[0]
      self.core_persist           = usbBuffer[1]
    else:
      self.core_good              = None
      self.core_persist           = None
    if len(usbBuffer) >= 10:
      self.core_ranges            = to_uint32(usbBuffer[2:6])
      self.core_nonces            = to_uint32(usbBuffer[6:10])
    else:
      self.core_ranges            = None
      self.core_nonces            = None
  def __str__(self):
    string  = "HFCtrlCoreStatus\n"
    string += "Core Good:         {}\n".format(self.core_good)
    string += "Core Persist:      {}\n".format(self.core_persist)
    string += "Core Ranges:       {}\n".format(self.core_ranges)
    string += "Core Nonces:       {}\n".format(self.core_nonces)
    return string;

class HFCtrlCoreDieStatus():
  def __init__(self, usbBuffer):
    if len(usbBuffer) >= 48:
      self.core_good              = usbBuffer[0:12]
      self.core_persist           = usbBuffer[12:24]
      self.core_pending           = usbBuffer[24:36]
      self.core_active            = usbBuffer[36:48]
    else:
      self.core_good              = None
      self.core_persist           = None
      self.core_pending           = None
      self.core_active            = None
    if len(usbBuffer) >= 64:
      self.die_hashes             = to_uint64(usbBuffer[48:56])
      self.die_nonces             = to_uint64(usbBuffer[56:64])
    else:
      self.die_hashes             = None
      self.die_nonces             = None
  def core_num_xy(self, die, x, y):
    if die%4==0 or die%4==1:
      return cores_die01[x][y]
    else:
      return cores_die23[x][y]
  def core_xy(self, die, x, y):
    return self.core(self.core_num_xy(die,x,y))
  def core(self, core):
    return (bool(self.core_good[core >> 3] & (0x01 << (core & 0x07))),
            bool(self.core_persist[core >> 3] & (0x01 << (core & 0x07))),
            bool(self.core_pending[core >> 3] & (0x01 << (core & 0x07))),
            bool(self.core_active[core >> 3] & (0x01 << (core & 0x07))))
  def __str__(self):
    string  = "HFCtrlCoreDieStatus\n"
    string += "Cores Good         "
    string += " ".join('{:x}'.format(x) for x in self.core_good)
    string += "\n"
    string += "Cores Persist      "
    string += " ".join('{:x}'.format(x) for x in self.core_persist)
    string += "\n"
    string += "Cores Pending      "
    string += " ".join('{:x}'.format(x) for x in self.core_active)
    string += "\n"
    string += "Cores Active       "
    string += " ".join('{:x}'.format(x) for x in self.core_pending)
    string += "\n"
    string += "Die Hashes:        {}\n".format(self.die_hashes)
    string += "Die Nonces:        {}\n".format(self.die_nonces)
    return string

class HFCtrlCoreASICStatus():
  def __init__(self, usbBuffer):
    if len(usbBuffer) >= 48:
      self.core_good              = usbBuffer[0:48]
    else:
      self.core_good              = None
    if len(usbBuffer) >= 64:
      self.asic_hashes            = to_uint64(usbBuffer[48:56])
      self.asic_nonces            = to_uint64(usbBuffer[56:64])
    else:
      self.asic_hashes            = None
      self.asic_nonces            = None
  def __str__(self):
    string  = "HFCtrlCoreDieStatus\n"
    string += "Cores Good         "
    string += " ".join('{:x}'.format(x) for x in self.core_good)
    string += "\n"
    string += "ASIC Hashes:       {}\n".format(self.asic_hashes)
    string += "ASIC Nonces:       {}\n".format(self.asic_nonces)
    return string

class HFCtrlDevice():
  def __init__(self, idVendor=None, idProduct=None):
    # HashFast idVendor
    if idVendor is None:
      idVendor = 0x297c
    # HashFast idProduct
    if idProduct is None:
      idProduct = 0x0001
    # find our device
    self.dev = usb.core.find(idVendor=idVendor, idProduct=idProduct)
    # was it found?
    if self.dev is None:
      raise ValueError('Device not found')
    # set the active configuration. With no arguments, the first
    # configuration will be the active one
    #self.dev.set_configuration()
    # get an endpoint instance
    #self.cfg = self.dev.get_active_configuration()
    #self.intf = self.cfg[(0,0)]

  ##
  # information about the connected device
  ##
  def info(self):
    # loop through configurations
    #   lsusb -v -d 297C:0001
    string = ""
    for cfg in self.dev:
      string += "ConfigurationValue {0}\n".format(cfg.bConfigurationValue)
      for intf in cfg:
        string += "\tInterfaceNumber {0},{0}\n".format(intf.bInterfaceNumber, intf.bAlternateSetting)
        for ep in intf:
          string += "\t\tEndpointAddress {0}\n".format(ep.bEndpointAddress)
    return string

  ##
  # ask the device to reboot
  #   *module the module to reboot
  #   *mode app=0x0000 or loader=0x0001 
  ##
  def reboot(self, module, mode):
    module = int(module)
    request_type = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE
    ret = self.dev.ctrl_transfer(request_type, HF_USBCTRL_REBOOT, mode, module, None)
    # TODO: verify

  ##
  #
  ##
  def version(self, module):
    module = int(module)
    request_type = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE
    ret = self.dev.ctrl_transfer(request_type, HF_USBCTRL_VERSION, 0x0000, module, 64)
    return HFCtrlVersion(ret)

  ##
  #
  ##
  def config(self):
    request_type = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE
    ret = self.dev.ctrl_transfer(request_type, HF_USBCTRL_CONFIG, 0x0000, 0x0000, 64)
    return HFCtrlConfig(ret)

  ##
  #
  ##
  def status(self):
    request_type = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE
    ret = self.dev.ctrl_transfer(request_type, HF_USBCTRL_STATUS, 0x0000, 0x0000, 64)
    return HFCtrlStatus(ret)

  ##
  #
  ##
  def serial(self, module):
    module = int(module)
    request_type = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE
    ret = self.dev.ctrl_transfer(request_type, HF_USBCTRL_SERIAL, 0x0000, module, 64)
    return HFCtrlSerial(ret)

  ##
  # get name
  ##
  def name(self):
    request_type = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE
    ret = self.dev.ctrl_transfer(request_type, HF_USBCTRL_NAME, 0x0000, 0x0000, 64)
    return HFCtrlName(ret)

  ##
  # set name
  ##
  def name_set(self, name):
    request_type = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE
    ret = self.dev.ctrl_transfer(request_type, HF_USBCTRL_NAME, 0x0000, 0x0000, name)
    # TODO verfy

  ##
  # get fan
  ##
  def fan(self, module):
    module = int(module)
    request_type = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE
    ret = self.dev.ctrl_transfer(request_type, HF_USBCTRL_FAN, 0x0000, module, 64)
    return HFCtrlFan(ret)

  ##
  # set fan
  #   *module
  #   *fan
  #   *speed
  ##
  def fan_set(self, module, fan, speed):
    fan_module = ((int(fan) << 8) | int(module))
    speed = int(speed)
    request_type = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE
    ret = self.dev.ctrl_transfer(request_type, HF_USBCTRL_FAN, speed, fan_module, None)
    # TODO verify

  ##
  # get power
  ##
  def power(self, module):
    module = int(module)
    request_type = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE
    ret = self.dev.ctrl_transfer(request_type, HF_USBCTRL_POWER, 0x0001, module, 64)
    return HFCtrlPower(ret)

  ##
  # set voltage
  ##
  def voltage_set(self, module, die, mvolts):
    die_module = ((int(die) << 8) | int(module))
    mvolts = int(mvolts)
    request_type = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE
    ret = self.dev.ctrl_transfer(request_type, HF_USBCTRL_VOLTAGE, mvolts, die_module, None)
    # TODO verify

  ##
  # get core overview
  ##
  def core_overview(self):
    request_type = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE
    ret = self.dev.ctrl_transfer(request_type, HF_USBCTRL_CORE_OVERVIEW, 0x0000, 0x0000, 64)
    return HFCtrlCoreOverview(ret)

  ##
  # set core enable
  ##
  def core_enable(self, core, persist):
    core = int(core)
    persist = int(persist)
    request_type = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE
    ret = self.dev.ctrl_transfer(request_type, HF_USBCTRL_CORE_ENABLE, persist, core, None)
    # TODO verify

  ##
  # set core disable
  ##
  def core_disable(self, core, persist):
    core = int(core)
    persist = int(persist)
    request_type = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE
    ret = self.dev.ctrl_transfer(request_type, HF_USBCTRL_CORE_DISABLE, persist, core, None)
    # TODO verify

  ##
  # set core clear
  ##
  def core_clear(self, persist):
    persist = int(persist)
    request_type = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE
    ret = self.dev.ctrl_transfer(request_type, HF_USBCTRL_CORE_CLEAR, persist, 0x0000, None)
    # TODO verify

  ##
  # get core status
  ##
  def core_status(self, core):
    core = int(core)
    request_type = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE
    ret = self.dev.ctrl_transfer(request_type, HF_USBCTRL_CORE_STATUS, 0x0000, core, 64)
    return HFCtrlCoreStatus(ret)

  ##
  # get die status
  ##
  def core_die_status(self, die):
    die = int(die)
    request_type = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE
    ret = self.dev.ctrl_transfer(request_type, HF_USBCTRL_CORE_DIE_STATUS, 0x0000, die, 64)
    return HFCtrlCoreDieStatus(ret)

  ##
  # get asic status
  ##
  def core_asic_status(self, asic):
    asic = int(asic)
    request_type = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE
    ret = self.dev.ctrl_transfer(request_type, HF_USBCTRL_CORE_ASIC_STATUS, 0x0000, asic, 64)
    return HFCtrlCoreASICStatus(ret)

  ##
  #
  ##
  def debug_buffer(self):
    pass

  ##
  #
  ##
  def debug_stream(self):
    pass

  ##
  #
  ##
  def debug_cli(self):
    pass