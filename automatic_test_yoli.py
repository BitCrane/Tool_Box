#!/usr/bin/env python3

import sys
import os
import time
import csv
import usb.util
import subprocess
import traceback
from subprocess import Popen, PIPE
import struct
import re

#TODO: "learn" the current prefix from the scanner as the script runs on the test machine
SERIAL_PREFIX = "CUSTOMIZEME-"

#Bus 002 Device 020: ID 297c:0001  
USBID_HF_VID=0x297c
USBID_HF_PID=0x0001
USBID_HFU_PID=0x8001

#Bus 002 Device 021: ID 03eb:2ff6 Atmel Corp. 
USBID_DFU_VID=0x03eb
USBID_DFU_PID=0x2ff6

def randbyteshex(bytecount, source="/dev/urandom"):
    """Return bytecount good pseudorandom bytes as a hex string."""
    src = open(source, "rb")
    thebytes = struct.unpack("B" * bytecount, src.read(bytecount))
    hexstring = "%02x" * bytecount % thebytes
    src.close()
    return hexstring

def read_serial():
    print ("Reading serial number from module.")
    try:
        result = subprocess.check_output(['../utils/readserial'])
        result = result.rstrip()
    except subprocess.CalledProcessError as e:
        traceback.print_exc(file=sys.stdout)
        print(e)
        throw
    # TODO: sanity check input for "HF:0x32n:FH"
    print ("Got back this serial: '%s'" % result)
    return result.decode(encoding='UTF-8')


def write_serial(serial_number):
    # TODO: sanity check input for "HF:0x32n:FH"
    try:
        result = subprocess.check_output(['../utils/writeserial', serial_number])
    except subprocess.CalledProcessError as e:
        traceback.print_exc(file=sys.stdout)
        print(e)
        print(result)
        throw
    return

def wait_for_hf():
    dev = usb.core.find(idVendor=USBID_HF_VID, idProduct=USBID_HF_PID)
    if dev is None:
        print ("Please connect the module in HashFast Miner Mode to the test machine.")
    while usb.core.find(idVendor=USBID_HF_VID, idProduct=USBID_HF_PID) == None:
        time.sleep (1)

def head_down():
    pass

def head_up():
    print ("Remove cables, Raise the test head and press return.")
    waitforit = sys.stdin.readline().rstrip()

def get_board_id():
    board_id = sys.stdin.readline().rstrip()

    # If they have to manually key in the last digits of the serial
    while len(board_id) < 4:
        board_id = "0" + board_id
    if not board_id.startswith(SERIAL_PREFIX):
        board_id = SERIAL_PREFIX + board_id
    # TODO: check board ID for format or at least sanity
    return board_id

def write_die_settings():
    pass_fail_args=["../utils/hcm",
                    "--write-die-settings",
                    "*:880@800",
                    "--ref-clock",
                    "25"]
    result = "FAIL"
    try:
        result = subprocess.check_output(pass_fail_args, universal_newlines=True)
    except subprocess.CalledProcessError as e:
        # This happens when there's a "libusb_bulk_transfer() failed while receiving: Input/Output Error" from hcm
        traceback.print_exc(file=sys.stdout)
        print(e)
        result = "FAIL"

    return result

def cgminer_test():
    # Indications that work progressing:
    # "Submitting share"
    # "Share above target"
    # "Accepted "

    # "firmware_rev:    0.2"
    # "hardware_rev:    1.1"
    # "serial number:   nnnnnn"
    # "inflight_target: 768"

    # Fail conditions
    # "Thermal overload tripped! Resetting device"
    # "invalid nonce - HW error" -- lots of these
    # "Hardware errors: 141630"

    # Indications that the pool is online
    # "Testing pool stratum"
    # "Stratum authorisation success for pool"
    # "New block: "

    #Summary of runtime statistics:
    # 
    #[2014-01-27 17:49:51] Started at [2014-01-27 17:47:48]                    
    #[2014-01-27 17:49:51] Pool: stratum+tcp://stratum.mining.eligius.st:3334                    
    #[2014-01-27 17:49:51] Runtime: 0 hrs : 2 mins : 2 secs                    
    #[2014-01-27 17:49:51] Average hashrate: 366684.2 Megahash/s                    
    #[2014-01-27 17:49:51] Solved blocks: 0                    
    #[2014-01-27 17:49:51] Best share difficulty: 17.8K                    
    #[2014-01-27 17:49:51] Share submissions: 681                    
    #[2014-01-27 17:49:51] Accepted shares: 680                    
    #[2014-01-27 17:49:51] Rejected shares: 1                    
    #[2014-01-27 17:49:51] Accepted difficulty shares: 5240                    
    #[2014-01-27 17:49:51] Rejected difficulty shares: 4                    
    #[2014-01-27 17:49:51] Reject ratio: 0.1%                    
    #[2014-01-27 17:49:51] Hardware errors: 141630  
    #[2014-01-27 17:49:51] Utility (accepted shares / min): 343.83/min                    
    #[2014-01-27 17:49:51] Work Utility (diff1 shares solved / min): 6028.10/min
    #                   
    #[2014-01-27 17:49:51] Stale submissions discarded due to new blocks: 0                    
    #[2014-01-27 17:49:51] Unable to get work from server occasions: 0                    
    #[2014-01-27 17:49:51] Work items generated locally: 10545                    
    #[2014-01-27 17:49:51] Submitting work remotely delay occasions: 0                    
    #[2014-01-27 17:49:51] New blocks detected on network: 2
    #                   
    #[2014-01-27 17:49:51] Summary of per device statistics:
    #                   
    #[2014-01-27 17:49:51] HFA0                | (9s):350.5G (avg):366.7Gh/s | A:5240 R:4 HW:141630 WU:6028.1/m                    
    #[2014-01-27 17:49:51] Mined 5240 accepted shares of 5000 requested

    # Indication that can't connect to network:
    # [2014-02-01 23:21:08] Probing for an alive pool
    # [2014-02-01 23:21:08] Testing pool stratum+tcp://stratum.mining.eligius.st:3334
    # [2014-02-01 23:21:09] Failed to connect to stratum on stratum.mining.eligius.st:3334
    # [2014-02-01 23:21:18] Waiting for work to be available from pools.


    
    # Bad board example data:
    # 360 GH/sec
    # 500 shares in 11 seconds, 8972 hardware errors
    # 1000 shares in 23 seconds
    # 1500 shares in 25, 32000 HW errors
    # 2000 shares in 45 sec, 63000 HW errors
    # 5000 shares in 2 minutes, 141630 HW errors
    # rough metric: no more HW errors than shares accepted
    cgminer_cmd_line=["../../cgminer.hf/cgminer",
                    "--verbose",
                    "--text-only",
                    "-o",
                    "stratum+tcp://stratum.mining.eligius.st:3334",
                    "-u",
                    "DONATE_testbench",
                    "-p",
                    "x",
                    "-l",
                    "9",
                    "--shares",
                    "5000",
                    "--hfa-hash-clock",
                    "800",
                    "--hfa-temp-overheat",
                    "104",
                    "--hfa-temp-target",
                    "0",
                   ]
    try:
        resultStr = subprocess.check_output(cgminer_cmd_line, universal_newlines=True, timeout=240)
    except subprocess.TimeoutExpired:
        print("TIMEOUT waiting for cgminer to complete.  FAIL.")
        return ("FAIL", 0, 0, 0)
    except subprocess.CalledProcessError:
        print("ERROR running cgminer.  FAIL.")
        return ("FAIL", 0, 0, 0)

    result = resultStr.splitlines(True)

    with open('cgminer.out', 'w', encoding='utf8') as outfile:
        for line in result:
            outfile.write(line)

    # TODO: save full run output for later look

    verdict = "PASS"

    num_power_bad = len([line for line in result if 'Main board 12V power is bad' in line])

    num_thermal_overload = len([line for line in result if 'Thermal overload tripped' in line])
    pool_active_count = len([line for line in result if 'Testing pool stratum' in line])

    #[2014-01-27 17:49:51] Accepted shares: 680                    
    accepted_shares = [line for line in result if 'Accepted shares' in line]
    num_accepted_shares = 0
    if len(accepted_shares) == 1:
        num_accepted_shares = accepted_shares[0].rstrip().rsplit(' ', 1)[1]
    else:
        print ("Expected one line of accepted shares but got '%s'." % accepted_shares)
        verdict = "FAIL"

    #[2014-01-27 17:49:51] Hardware errors: 141630  
    hardware_errors = [line for line in result if 'Hardware errors:' in line]
    num_hardware_errors = 1000
    if len(hardware_errors) == 1:
        num_hardware_errors = hardware_errors[0].rstrip().rsplit(' ', 1)[1]
    else:
        print ("Expected one line of hardware errors but got '%s'." % hardware_errors)
        verdict = "FAIL"

    #[2014-01-27 17:49:51] Average hashrate: 366684.2 Megahash/s
    avg_hashrate = [line for line in result if 'Average hashrate' in line]
    num_avg_hashrate = 0
    if len(avg_hashrate) == 1:
        num_avg_hashrate = float(avg_hashrate[0].rstrip().rsplit(' ', 2)[1])
    else:
        print ("Expected one line of avg hash rate but got '%s'." % avg_hashrate)
        verdict = "FAIL"

    firmware_rev = [line for line in result if 'firmware_rev' in line]
    hardware_rev = [line for line in result if 'hardware_rev' in line]

    num_inflight_target = 0
    inflight_target = [line for line in result if 'inflight_target' in line]
    if len(inflight_target) >= 1:
        num_inflight_target = int(inflight_target[-1].rstrip().rsplit(' ', 1)[1])
    else:
        print ("Expected one or more line of inflight target but got '%s'." % inflight_target)
        verdict = "FAIL"

    if num_inflight_target < 653:
        print ("Found too few good cores.  Inflight target is '%d'." % num_inflight_target)
        verdict = "FAIL"

    if num_avg_hashrate < 600000.0:
        print ("Hash Rate too low: '%f'." % num_avg_hashrate)
        verdict = "FAIL"

    print ("num_avg_hashrate is '%s'" % num_avg_hashrate)
    print ("num_accepted_shares is '%s'" % num_accepted_shares)
    print ("num_hardware_errors is '%s'" % num_hardware_errors)

    if int(num_hardware_errors) > 500:
        print ("Too many hardware errors.  FAIL.")
        verdict = "FAIL"
    if num_thermal_overload > 0:
        print ("CHECK YOUR RIG: num_thermal_overload is '%s'" % num_thermal_overload)
        verdict = "FAIL"
    if num_power_bad > 0:
        print ("CHECK YOUR RIG: num_power_bad is '%s'" % num_power_bad)
        verdict = "FAIL"

    return (verdict, num_avg_hashrate, num_accepted_shares, num_hardware_errors)

def automatic_load_and_test():

    print ("Connect module and wait for firmware load to complete.")

    # TODO: reconsider '--confirm-reload'
    subprocess.check_output(['./firmware_update.py'])

    print("Firmware loaded.")

    wait_for_hf()

    print("Initializing die settings...")
    write_die_settings()

    print("Please make sure all cables are connected, the cooler head is lowered, and scan label to begin.")

    board_id=get_board_id()

    wait_for_hf()

    head_down()

    #print("Running automatic test phase 1/2...")

    test_result = "PASS"

    print("Running automatic cgminer test...")

    serial="NONE"
    result = cgminer_test()
    if "PASS" != result[0]:
        test_result = "FAIL"
    else:
        print ("")
        serial_rand = randbyteshex(16)
        serial = "HF::" + serial_rand + "::FH"
        print ('Writing serial: "%s".' % serial)
        time.sleep(3)

        write_serial(serial)
        serial_back=read_serial()
        if serial_back != serial:
            print ("Error: failed to read back serial correctly.")
            raise Exception("Error: failed to read back serial correctly.")

    print ("cgminer test result: '%s'." % test_result)

    if test_result == "PASS":
        print ("***GOOD BOARD")
    else:
        print ("***BAD BOARD")
    print ("")

    manual_check = "PASS"
    if test_result == "PASS":
        print ("Operator manual override -- manually FAIL this board for any reason?")
        print ("Please enter '' or 'FAIL': ")
        manual_check = sys.stdin.readline().rstrip()
        if manual_check == "":
            manual_check = "PASS"
        if manual_check != "PASS":
            test_result = "FAIL"
            print ("***BAD BOARD")


    firmware_release = os.path.realpath('uc3.cropped.hfu').split('/')[-2]
    with open('serial_number_db_autotest_v1.csv', 'a', encoding='utf8') as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow([serial, board_id, test_result, manual_check, firmware_release, result[1], result[2], result[3]])

    head_up()

    print ("")
    print ("***AUTOMATIC LOAD AND TEST COMPLETE")
    print ("")

    return

if __name__ == "__main__":

    while 1:
        print ("")
        try:
            print("Beginning Board Load and Test")
            automatic_load_and_test()
            print("Board Load and Test Completed")
        except Exception as e:
            traceback.print_exc(file=sys.stdout)
            print(e)
            print("Board Test had an error.  Please retry.")
        except KeyboardInterrupt:
            print ("")
            print("Board Test Cancelled.  Exiting.")
            exit(1)

        print ("Disconnect HF device from system.")
        while usb.core.find(idVendor=USBID_HF_VID, idProduct=USBID_HF_PID) != None:
            time.sleep (1)

