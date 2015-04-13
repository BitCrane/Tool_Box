#!/usr/bin/env python3

import sys
import os
import time
import argparse

# You see "ImportError: No module named usb.core"?
# You do:
# sudo apt-get install libusb-1.0-0-dev
# sudo apt-get install python-usb
#
# IF that doesn't resolve it, continue:
# sudo apt-get install python-pip
# sudo pip install --upgrade pyusb
#
# IF that doesn't work
# Go fetch pyusb 1.0+ and "sudo python3 setup.py install"

import usb.core
import usb.util

import subprocess
import traceback
from subprocess import Popen, PIPE
import struct
import shutil

pathlist = [".", os.path.join("..", "utils")]
os.environ["PATH"] += os.pathsep + os.pathsep.join(pathlist)

# HashFast Miner: ID 297c:0001  
USBID_HF_VID=0x297c
USBID_HF_PID=0x0001

# HFU Boot Loader: ID 297c:8001  
USBID_HFU_PID=0x8001

#DFU Boot Loader: ID 03eb:2ff6 Atmel Corp. 
USBID_DFU_VID=0x03eb
USBID_DFU_PID=0x2ff6

UC_PART='at32uc3b0512'

parser = argparse.ArgumentParser(description='HashFast Firmware Updater.')
parser.add_argument('--confirm-reload', action='store_true', default=False,
                   help='detect existing serial and confirm reload of firmware')
parser.add_argument('--firmware', action='store', default='.',
                   help='path to the firmware update directory')
args = parser.parse_args()

CONFIRM_RELOAD = args.confirm_reload
FIRMWARE_DIR = args.firmware

print ("confirm is ", CONFIRM_RELOAD)
print ("FIRMWARE_DIR is ", FIRMWARE_DIR)

if (not os.path.isdir(FIRMWARE_DIR)):
    print ("Specified firmware path '%s' is not a directory." % FIRMWARE_DIR)
    exit(1)

DFU_PROGRAMMER=shutil.which("dfu-programmer")
READSERIAL=shutil.which("readserial")
HFUPDATE=shutil.which("hfupdate")
ENTERLOADER=shutil.which("enterloader")

# HashFast Boot Loader, in HEX format
HFU_HEX_FILE=os.path.join(FIRMWARE_DIR,'uc3_loader_dfu_update.hex')
print ("HFU_HEX_FILE at '%s'." % HFU_HEX_FILE)

# HashFast uC HFU hex file for use with hfupdate
UC_HFU_FILE=os.path.join(FIRMWARE_DIR,'uc3.cropped.hfu')
print ("UC_HFU_FILE at '%s'." % UC_HFU_FILE)

print ("DFU_PROGRAMMER found at '%s'." % DFU_PROGRAMMER)
print ("READSERIAL found at '%s'." % READSERIAL)
print ("HFUPDATE found at '%s'." % HFUPDATE)
print ("ENTERLOADER found at '%s'." % ENTERLOADER)

def enterloader():
    subprocess.check_call([ENTERLOADER])

def enumerate_modules():
    try:
        result = subprocess.check_output([HFUPDATE, '-E'])
    except subprocess.CalledProcessError as e:
        return 0

    resultStr = str(result, encoding='utf8')

    num_modules = 0
    for mod_num in range(0,5):
        search_string = "module %d version" % mod_num
        if search_string in resultStr:
            num_modules = mod_num + 1
    return num_modules

def read_serial_hfu():
    # TODO
    return None

def read_serial_hf():
    print ("Reading serial number from module.")
    try:
        result = subprocess.check_output([READSERIAL])
        result = result.rstrip()
    except subprocess.CalledProcessError as e:
        return None
    # TODO: sanity check input for "HF:0x32n:FH"
    print ("Got back this serial: '%s'" % result)
    return result

def load_firmware_dfu(hex_file):
    print ("Initiating firmware load for device in DFU state.")

    subprocess.check_call([DFU_PROGRAMMER, UC_PART, 'erase'])
    subprocess.check_call([DFU_PROGRAMMER, UC_PART, 'flash', '--suppress-bootloader-mem', hex_file])
    subprocess.check_call([DFU_PROGRAMMER, UC_PART, 'reset'])

    return

def load_firmware_hfu(hex_file, num_modules=1):
    for module in range(0,num_modules):
        print("Updating module %d..." % module)
        subprocess.check_call([HFUPDATE, '-m%d' % module, hex_file])
    return

def restart_to_hf_mode():
    subprocess.check_call([HFUPDATE, '-r'])
    return

def wait_for_device(device_list, timeout=None):
    tries=0
    while True:
        for dev_type in device_list:
            dev = usb.core.find(idVendor=dev_type[0], idProduct=dev_type[1])
            if dev is not None:
                # TODO: check permissions, give udev advice if bad perms
                return (dev, dev_type)
        tries += 1
        if timeout and tries == timeout:
            raise Exception("Error: timeout.  Failed to find device.")

        time.sleep(1)

def firmware_updater():

    all_devices =[(USBID_HF_VID, USBID_HF_PID), (USBID_HF_VID, USBID_HFU_PID), (USBID_DFU_VID, USBID_DFU_PID)]
    try:
        device = wait_for_device(all_devices, 1)
    except Exception:
        print("Please connect HashFast device to update.")
        device = wait_for_device(all_devices)

    if (device[1] == (USBID_DFU_VID, USBID_DFU_PID)):
        # Found DFU
        print("Loading new Boot Loader...")
        load_firmware_dfu(HFU_HEX_FILE)
        device = wait_for_device([(USBID_HF_VID, USBID_HFU_PID)], 10)

    time.sleep(1)

    if (device[1] == (USBID_HF_VID, USBID_HF_PID)):
        serial=read_serial_hf()
        if (CONFIRM_RELOAD and serial):
            serial = "HF::" + str(serial) + "::FH"
            print ('Board Serial number is: "%s".' % serial)
            print ('Do you want to RELOAD this board?  ("YES" or "NO")')
            response = sys.stdin.readline().rstrip()
            if not response.lower() in ["yes", "y"]:
                print ('Cancelling...')
                sys.exit(1)
        print("Entering Boot Loader...")
        enterloader()
        time.sleep(3)

    print("Enumerating modules...")
    num_modules = enumerate_modules()
    if num_modules == 0:
        print ('Error enumerating modules.  Cancelling...')
        sys.exit(1)

    time.sleep(1)

    print("Found %d modules." % num_modules)
    print("Loading Firmware...")
    load_firmware_hfu(UC_HFU_FILE, num_modules)

    restart_to_hf_mode()

    wait_for_device([(USBID_HF_VID, USBID_HF_PID)], 10)

    print
    print ("***FIRMWARE UPDATE COMPLETE")
    print

    return

def check_deps():
    dep_list = [("HFU HEX File", HFU_HEX_FILE), ("UC HFU File", UC_HFU_FILE), ("DFU Programmer Utility", DFU_PROGRAMMER), ("readserial utility", READSERIAL), ("hfupdate utility", HFUPDATE), ("Enterloader utility", ENTERLOADER)]
    for dep in dep_list:
        if (dep[1] == None or not os.path.isfile(dep[1])):
            print ("Dependency '%s' not found.  Cannot run." % dep[0])
            exit(1)


if __name__ == "__main__":

    print
    check_deps()

    try:
        print("HashFast Firmware Updater")
        print
        firmware_updater()
        print
        print("HashFast Firmware Updater Completed")
        exit(0)
    except Exception as e:
        traceback.print_exc(file=sys.stdout)
        print(e)
        print("Firmware Update had an error.  Please retry or report to HashFast Support.")
        exit(1)
    except KeyboardInterrupt:
        print
        print("HashFast Firmware Update Cancelled.  Exiting.")
        exit(1)

