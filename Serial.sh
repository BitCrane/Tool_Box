#!/bin/bash
#This is a script write serial number to the board automatically.
#The serial number this batch is start from BPCB-1504-0000 to BPCB-1504-9999, you should only input the last 4 digit.
#The Script will automatically check if the Yoliboard is plugged.
#Will write all the serial number have writted into file serialwritten.txt within the same directory.
#It need "hcm" to placed in the user binary PATH(/usr/local/bin).

read -p "please enter the last 4 digits of a serial number >"
USB=$( lsusb | grep 297c ) #get the USB status
if [[ $REPLY =~ ^[0-9]{4}$ ]]; then 
	echo "The serial number will write is $REPLY "
	if [[ $USB =~ 0001 ]]; then #determin if the Yoliboard is plugged.
	hcm --putname "$REPLY"
	sleep 1s
	hcm --getname >> ~/serialwritten.txt
	cat ~/serialwritten.txt
	else
	echo "The Yoliboard is not plugged,please check the USB cable or press the reset button." >&2
	exit 1
	fi
else
	echo "The input serial number is invalid" >&2
	exit 1
fi


