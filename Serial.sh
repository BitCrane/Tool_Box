#!/bin/bash
#This is a script write serial number to the board automatically.
#The serial number start from BPCB-1504-0000 to BPCB-1504-9999, you should only input the last 4 digit.
#The Script will automatically check if the Yoliboard is plugged.
#Will write all the serial number have writted into file serialwritten.txt in the same directory with the script.

read -p "please enter serial number >"
USB=$( lsusb | grep 297c )
if [[ $REPLY =~ ^[0-9]{4}$ ]]; then
	echo "The serial number will write is BPCB-1504-$REPLY "
	if [[ $USB =~ 0001 ]]; then
	hcm --putname "BPCB-1504-$REPLY"
	sleep 2s#!/bin/bash
#This is a script write serial number to the board automatically

read -p "please enter serial number >"
USB=$( lsusb | grep 297c )
if [[ $REPLY =~ ^[0-9]{4}$ ]]; then
	echo "The serial number will write is BPCB-1504-$REPLY "
	if [[ $USB =~ 0001 ]]; then
	hcm --putname "BPCB-1504-$REPLY"
	sleep 2s
	hcm --getname >> ./serialwritten.txt
	cat ./serialwritten.txt
	else
	echo "The USB device is not plugged." >&2
	exit 1
	fi
else
	echo "The serial number input is invalid" >&2
	exit 1
fi


	hcm --getname >> ./serialwritten.txt
	cat ./serialwritten.txt
	else
	echo "The USB device is not plugged." >&2
	exit 1
	fi
else
	echo "The serial number input is invalid" >&2
	exit 1
fi

