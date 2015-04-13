#!/usr/bin/env python3

import random
import sys
import time

from hfload import hf
from hfload import talkusb
from hfload.routines import rate

talkusb.talkusb(hf.INIT, None, 0);

HRT = rate.HashRateTest(talkusb.talkusb, 600, print)

while HRT.one_cycle():
    pass

print("All done!")
