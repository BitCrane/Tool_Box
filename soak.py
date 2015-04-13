#! /usr/bin/env python3

import sys
import time
import threading

from abc import ABCMeta, abstractmethod
from datetime import datetime

from hfui.base import BaseUI
from hfload import hf
from hfload import talkusb
from hfload.routines import temp
from hfusb import usbbulk
from hfusb import usbctrl

class HFProfilerData:
  def __init__(self):
    pass

class HFProfilerBase(object):
  __metaclass__ = ABCMeta
  def __init__(self):
    pass
  #@abstractmethod
  #def start(self, ui):
  #  pass

class HFSoakInteractive(HFProfilerBase):
  def __init__(self):
    pass

  def start(self, ui, dev):
    self.soak(ui, dev)

  def soak(self, ui, dev):
    while True:
      talkusb.talkusb(hf.INIT, None, 0);
      clockrate = ui.prompt_int_single("Clockrate? 1-9 for 100s of MHz, 0=950")
      if clockrate is 0:
        clockrate = 950
      if clockrate < 10:
        clockrate = clockrate*100
      #ui.prompt_enter("Press enter to start board soak with clockrate of "+str(clockrate))
      self.run(ui, dev, clockrate)

  def run(self, ui, dev, clockrate):
    self.test = temp.HashTempTest(talkusb.talkusb, clockrate, ui.log)
    ui.prompt_show("Running at "+str(clockrate)+"MHz. Press board 'RESET' or ctrl+c to end.")
    self.cr = ui.current_round
    self.cr.clockrate = clockrate
    # run soak with temperature monitor
    self.getting_warm = False
    self.throttle = 50
    rslt = True

    # thread
    thread = threading.Thread(target=self.monitor_temp, args={ui})
    thread.daemon = True
    thread.start()

    # run
    while rslt:
      if self.getting_warm is False:
        self.throttle -= 1
        if self.throttle < 0:
          self.throttle = 0
      else:
        # getting warm
        self.throttle = 50
        self.getting_warm = False
      rslt = self.test.one_cycle(self.throttle)
    #if rslt is -2:
    #  self.run(ui, dev, clockrate)
    # cycle loop complete
    #ui.prompt_enter("Round Complete. Check temperature.")

  def monitor_temp(self, ui):
    while True:
      time.sleep(0.1)
      self.cr.total_hashes = self.test.total_hashes
      self.cr.total_errors = self.test.total_errors
      self.cr.hash_rate = self.test.hash_rate
      if self.test.dies is not None:
        for dinfo in ui.die_info:
          if dinfo is not None:
            die = self.test.dies[dinfo.index]
            if die is not None:
              if die['monitor_data'] is not None:
                dinfo.thermal_cutoff = die['thermal_cutoff']
                dinfo.active = die['active']
                dinfo.pending = die['pending']
                dinfo.temp = die['monitor_data'].die_temperature
                dinfo.vm = die['monitor_data'].core_voltage_main
                dinfo.throttle = self.throttle
                if dinfo.temp > 104:
                  self.getting_warm = True

  def input(self, msg):
    pass

class HFSoakUI(BaseUI):

  def setup_ui(self):
    # column 0
    self.setup_log(   0, 0, w=4)
    # column 4
    self.setup_logo(    4, 1, "HashFast Soak Tool", "v0.1")
    self.setup_input(   4, 8 )
    self.setup_output(  4, 12)
    self.setup_module(  4, 16, nasic=1, coremap=False)
    self.setup_stats(   4, 42)
    # column 9
    self.setup_info(    9, 1 )

  def update_ui(self):
    self.update_module()
    self.update_info()
    self.update_current()

  def refresh_ui(self):
    pass

def main(argv):
  ui = HFSoakUI()
  try:
    ui.setup()
    ui.refresh()

    ui.prompt_show("Please connect device.")
    dev = usbctrl.poll_hf_ctrl_device(printer=ui.log)

    ret = ui.prompt("HashFast Soak Tool. Press 's' to start", "s")
    if ret:
      profiler = HFSoakInteractive()
      profiler.start(ui, dev)

  finally:
    ui.end()

if __name__ == "__main__":
   main(sys.argv[1:])