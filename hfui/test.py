#! /usr/bin/env python3

import sys
from base import BaseUI
from datetime import datetime
import time

class TestUI(BaseUI):

  def setup_ui(self):
    # column 0
    self.setup_log(   0, 0)
    # column 2
    self.setup_logo(  2, 1, "Test UI", "v0.1")
    self.setup_module(2, 16, nasic=1)
    self.setup_input( 2, 8 )
    self.setup_output(2, 12)
    self.setup_stats( 2, 42)
    # column 7
    self.setup_info(  7, 1 )

  def update_ui(self):
    self.update_module()
    self.update_info()

  def refresh_ui(self):
    pass

def main(argv):
  hfui = TestUI()
  try:
    hfui.setup()
    hfui.refresh()

    ret = hfui.prompt("HashFast Profiling Tool. Type 'i' for interactive mode, 'a' for automatic. Type 'q' to quit.", "ai")

  finally:
    hfui.end()

if __name__ == "__main__":
   main(sys.argv[1:])