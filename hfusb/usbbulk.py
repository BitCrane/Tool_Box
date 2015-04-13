#! /usr/bin/env python3

# requires pyusb
#   pip install --pre pyusb

import usb.core
import usb.util
import sys
import time

HF_USBBULK_INIT         = 0
HF_USBBULK_SHUTDOWN     = 1
HF_USBBULK_SEND         = 2
HF_USBBULK_RECEIVE      = 3
HF_USBBULK_SEND_MAX     = 4
HF_USBBULK_RECEIVE_MAX  = 5

HF_BULK_DEVICE_NOT_FOUND     = 'HFBulkDevice Not Found'
HF_BULK_DEVICE_FOUND         = 'HFBulkDevice Found!'

def poll_hf_bulk_device(intv=1, printer=print):
  # look for device
  while 1:
    time.sleep(intv)
    try:
      dev = HFBulkDevice()
      break
    except:
      printer(HF_BULK_DEVICE_NOT_FOUND)
  # found device
  printer(HF_BULK_DEVICE_FOUND)
  return dev

class HFBulkDevice():
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

  def init(self):
    # detach kernel driver
    if self.dev.is_kernel_driver_active(intf):
      self.dev.detach_kernel_driver(intf)
    # set the active configuration. With no arguments, the first
    # configuration will be the active one
    self.dev.set_configuration()
    # get an endpoint instance
    self.cfg = self.dev.get_active_configuration()
    self.intf = self.cfg[(1,1)]
    # write endpoint
    self.epw = usb.util.find_descriptor(self.intf,
        # match the first OUT endpoint
        custom_match = \
        lambda e: \
            usb.util.endpoint_direction(e.bEndpointAddress) == \
            usb.util.ENDPOINT_OUT
    )
    assert epw is not None
    # read endpoint
    self.epr = usb.util.find_descriptor(self.intf,
        # match the first IN endpoint
        custom_match = \
        lambda e: \
            usb.util.endpoint_direction(e.bEndpointAddress) == \
            usb.util.ENDPOINT_IN
    )
    assert epr is not None
    return 0

  def shutdown(self):
    return 0

  def send(self, usbBuffer):
    try:
      ret = epw.write(usbBuffer, 0)
      return ret
    except usb.core.USBError:
      return -1

  def recieve(self, bufferLen):
    try:
      ret = epr.read(bufferLen, 0)
      return ret
    except usb.core.USBError:
      return -1

  def send_max(self):
    return 64

  def recieve_max(self):
    return 64
