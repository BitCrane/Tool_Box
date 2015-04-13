#! /usr/bin/env python3

import curses
import threading
from datetime import datetime
import time
from abc import ABCMeta, abstractmethod
from collections import deque

CWIDTH = 12

def ctx(col):
  return col*(CWIDTH)

def wtx(width):
  return width*(CWIDTH)

class RoundInfo:
  def __init__(self):
    self.clockrate = 0
    self.total_hashes = 0
    self.total_errors = 0
    self.hash_rate = 0

class DieInfo:
  def __init__(self, index, coremap):
    self.index    = index
    self.coremap  = coremap
    self.throttle = 0
    self.active   = 0
    self.pending  = 0
    self.temp     = 0
    self.thermal_cutoff = 0
    self.vm       = 0
    self.va       = 0
    self.vb       = 0
    self.vc       = 0
    self.vd       = 0
    self.ve       = 0

class BaseUI(metaclass=ABCMeta):

  @abstractmethod
  def setup_ui(self):
    pass

  @abstractmethod
  def update_ui(self):
    pass

  @abstractmethod
  def refresh_ui(self):
    pass

  def __init__(self):
    # setup curses window
    self.screen = curses.initscr()
    self.wlogo = None
    self.wmodule = None
    self.wasic = [None]*5
    self.wdie = [None]*5*4
    self.winfo = None
    self.log_buffer = deque(["Log Start"])
    self.wlog = None
    self.winput = None
    self.woutput = None
    self.wprevious = None
    self.wstats = None
    # device information
    #self.connected_die
    self.die_info = [None]*5*4
    self.current_round = RoundInfo()

  def setup_logo(self, c, y, name, version):
    self.wlogo = curses.newwin(7, 60, y, ctx(c))
    #self.wlogo.bkgd(' ', curses.color_pair(4))
    self.wlogo.addstr(0,0,"______  __             ______ __________             _____ ")
    self.wlogo.addstr(1,0,"___  / / /_____ __________  /____  ____/_____ _________  /_")
    self.wlogo.addstr(2,0,"__  /_/ /_  __ `/_  ___/_  __ \_  /_   _  __ `/_  ___/  __/")
    self.wlogo.addstr(3,0,"_  __  / / /_/ /_(__  )_  / / /  __/   / /_/ /_(__  )/ /_  ")
    self.wlogo.addstr(4,0,"/_/ /_/  \__,_/ /____/ /_/ /_//_/      \__,_/ /____/ \__/  ")
    self.wlogo.addstr(5,1, name)
    self.wlogo.addstr(5,50,version)
    self.wlogo.addstr(6,0,"                                          (c) 2014 HashFast")

  def setup_colors(self):
    curses.start_color()
    #0:black, 1:red, 2:green, 3:yellow, 4:blue, 5:magenta, 6:cyan, and 7:white
    #curses.init_pair(0, curses.COLOR_WHITE, curses.COLOR_BLACK)
    curses.init_pair( 1, curses.COLOR_WHITE, curses.COLOR_RED)
    curses.init_pair( 2, curses.COLOR_WHITE, curses.COLOR_GREEN)
    curses.init_pair( 3, curses.COLOR_WHITE, curses.COLOR_YELLOW)
    curses.init_pair( 4, curses.COLOR_WHITE, curses.COLOR_BLUE)
    curses.init_pair( 5, curses.COLOR_WHITE, curses.COLOR_MAGENTA)
    curses.init_pair( 6, curses.COLOR_WHITE, curses.COLOR_CYAN)
    ## Black Text on Colored Background
    curses.init_pair(10, curses.COLOR_BLACK, curses.COLOR_WHITE)
    curses.init_pair(11, curses.COLOR_BLACK, curses.COLOR_RED)
    curses.init_pair(12, curses.COLOR_BLACK, curses.COLOR_GREEN)
    curses.init_pair(13, curses.COLOR_BLACK, curses.COLOR_YELLOW)
    curses.init_pair(14, curses.COLOR_BLACK, curses.COLOR_BLUE)
    curses.init_pair(15, curses.COLOR_BLACK, curses.COLOR_MAGENTA)
    curses.init_pair(16, curses.COLOR_BLACK, curses.COLOR_CYAN)
    ## Red Text on Colored Background
    curses.init_pair(20, curses.COLOR_RED, curses.COLOR_WHITE)
    curses.init_pair(21, curses.COLOR_RED, curses.COLOR_BLACK)
    curses.init_pair(22, curses.COLOR_RED, curses.COLOR_GREEN)
    curses.init_pair(23, curses.COLOR_RED, curses.COLOR_YELLOW)
    curses.init_pair(24, curses.COLOR_RED, curses.COLOR_BLUE)
    curses.init_pair(25, curses.COLOR_RED, curses.COLOR_MAGENTA)
    curses.init_pair(26, curses.COLOR_RED, curses.COLOR_CYAN)

  def setup_module(self, c, y, w=10, nasic=1, coremap=True):
    if nasic > 5:
      raise Exception("Cannot display more than 5 ASICs")
    self.wmodule = curses.newwin(26, wtx(w), y, ctx(c))
    self.wmodule.bkgd(' ', curses.color_pair(10))
    dx = int(int(wtx(w) - wtx(2)*nasic)/2)
    print(str(dx))
    for asic in range(nasic):
      self.setup_asic(asic, 0, dx, coremap)
      dx += wtx(2)

  def setup_asic(self, asic, yo, xo, coremap):
    self.wasic[asic] = self.wmodule.derwin(26, 22, yo, xo)
    fdie = 0 + 4*asic
    if coremap:
      for d in range(fdie, fdie+4):
        if d % 4 == 0:
          self.setup_die(asic, d, 0, 0, coremap)
        if d % 4 == 1:
          self.setup_die(asic, d, 0, 11, coremap)
        if d % 4 == 2:
          self.setup_die(asic, d, 13, 11, coremap)
        if d % 4 == 3:
          self.setup_die(asic, d, 13, 0, coremap)
    else:
      for d in range(fdie, fdie+4):
        if d % 4 == 1: # 0:
          self.setup_die(asic, d, 0, 0, coremap)
        if d % 4 == 2: # 1:
          self.setup_die(asic, d, 0, 11, coremap)
        if d % 4 == 3: # 2:
          self.setup_die(asic, d, 13, 11, coremap)
        if d % 4 == 0: # 3:
          self.setup_die(asic, d, 13, 0, coremap)

  def setup_die(self, asic, die, y, x, coremap):
    self.wdie[die] = self.wasic[asic].derwin(13, 11, y, x)
    self.die_info[die] = DieInfo(die, coremap)
    self.wdie[die].bkgd(' ',curses.color_pair(10))
    self.wdie[die].box()
    if coremap:
      dstr = "D"+str(die)
    else:
      dstr = "D"+str(die+1)
    self.wdie[die].addstr(0,0,dstr)
    if die % 4 == 0:
      self.wdie[die].addstr(12,0,"USB",curses.color_pair(3))

  def setup_info(self, c, y, w=5):
    self.winfo = curses.newwin(8, wtx(w), y, ctx(c))
    self.winfo.box()
    self.winfo.addstr(0,0,"INFORMATION")
    self.winfo.addstr(1,1,"START")
    self.winfo.addstr(2,4,str(datetime.now()))
    self.winfo.addstr(3,1,"UPDATED")
    self.winfo.addstr(4,4,str(datetime.now()))

  def setup_log(self, c, y, w=2, h=100):
    self.wlog = curses.newwin(h, wtx(w), y, ctx(c))
    self.wlog.box()
    self.wlog.addstr(0,0,"LOG")

  def setup_input(self, c, y, w=10):
    self.winput = curses.newwin(4, wtx(w), y, ctx(c))
    self.winput.bkgd(' ',curses.color_pair(6))
    self.winput.box()
    self.winput.addstr(0,0,"INPUT")

  def setup_output(self, c, y, w=10):
    self.woutput = curses.newwin(4, wtx(w), y, ctx(c))
    self.woutput.bkgd(' ',curses.color_pair(2))
    self.woutput.box()
    self.woutput.addstr(0,0,"CURRENT")
    self.woutput.addstr(1,2,"FREQUENCY")
    self.woutput.addstr(1,20,"VOLTAGE")
    self.woutput.addstr(1,40,"HASHRATE")
    self.woutput.addstr(1,60,"LHW ERRORS")
    self.woutput.addstr(1,80,"DHW ERRORS")

  def setup_previous(self, c, y, w=10):
    self.wprevious = curses.newwin(60, wtx(w), y, ctx(c))
    #wprevious.bkgd(' ',curses.A_DIM)
    self.wprevious.box()
    self.wprevious.addstr(0,0,"PREVIOUS")
    self.wprevious.addstr(1,2,"FREQUENCY")
    self.wprevious.addstr(1,20,"VOLTAGE")
    self.wprevious.addstr(1,40,"HASHRATE")
    self.wprevious.addstr(1,60,"LHW ERRORS")
    self.wprevious.addstr(1,80,"DHW ERRORS")

  def setup_stats(self, c, y, w=10):
    self.wstats = curses.newwin(4, wtx(w), y, ctx(c))
    self.wstats.bkgd(' ',curses.color_pair(5))
    self.wstats.box()
    self.wstats.addstr(0,0,"STATS")
    self.wstats.addstr(1,2,"ENABLED")
    self.wstats.addstr(1,20,"DISABLED")
    self.wstats.addstr(1,40,"INFLIGHT")
    self.wstats.addstr(1,60,"ACTIVE")
    self.wstats.addstr(1,80,"PENDING")

  def setup(self):
    self.setup_colors()
    try:
      self.setup_ui()
    finally:
      # thread
      self.thread = threading.Thread(target=self.run_update)
      self.thread.daemon = True
      self.thread.start()

  def update_log(self):
    while len(self.log_buffer):
      msg = self.log_buffer.popleft()
      self.wlog.move(1,1)
      self.wlog.insertln()
      self.wlog.addstr(1,1,msg)
    self.wlog.box()
    self.wlog.addstr(0,0,"LOG")

  def update_info(self):
    self.winfo.addstr(4,4,str(datetime.now()))

  def update_current(self):
    cr = self.current_round
    self.woutput.addstr(2,2,str(cr.clockrate))
    #self.woutput.addstr(2,20,"")
    self.woutput.addstr(2,40,"{0:3.2f}GH/s".format(cr.hash_rate / 10**9))
    self.woutput.addstr(2,60,str(cr.total_errors))
    #self.woutput.addstr(2,80,"")

  def update_module(self):
    die_count = len(self.wdie)
    for die, wdie in enumerate(self.wdie):
      if wdie is not None:
        self.update_die(die)

  def update_die(self, n):
    di = self.die_info[n]
    wdie = self.wdie[n]
    # temperature
    temp = int(di.temp)
    if temp < 85:
      color = curses.color_pair(2)
    elif temp < 95:
      color = curses.color_pair(3)
    else:
      color = curses.color_pair(1)
    if di.coremap:
      for y in range(0,11):
        for x in range(0,9):
          if True: #die_status.core_xy(n,xy)[0]:
            wdie.addstr(y+1,x+1,'x')
      if n%4 == 0 or n%4 == 1:
        wdie.addstr(1, 4, "{0:03d}".format(temp), color)
      else:
        wdie.addstr(11,4, "{0:03d}".format(temp), color)
      # voltages
      color = curses.color_pair(0)
      #wdie.addstr(0, 4, "{0:03d}".format(di.vm), color)
      #wdie.addstr(0, 8, "{0:03d}".format(di.va), color)
      #wdie.addstr(12,0, "{0:03d}".format(di.vb), color)
      #wdie.addstr(12,4, "{0:03d}".format(di.vc), color)
      #wdie.addstr(12,8, "{0:03d}".format(di.vd), color)
    else:
      if di.thermal_cutoff:
        wdie.addstr(6, 4, "THM".format(temp), curses.color_pair(1))
      else:
        wdie.addstr(6, 4, "{0:03d}".format(temp), color)
      if n%4 == 1 or n%4 == 2:
        wdie.addstr(1, 1,"SQ    {0:02d}%".format(int(di.throttle)))
        wdie.addstr(2, 1,"ACT   {0:03d}".format(di.active))
        wdie.addstr(3, 1,"PEND  {0:03d}".format(di.pending))
        wdie.addstr(10,1,"VM   {0:.02f}".format(di.vm))
      else:
        wdie.addstr(2, 1,"VM   {0:.02f}".format(di.vm))
        wdie.addstr(9, 1,"SQ    {0:02d}%".format(int(di.throttle)))
        wdie.addstr(10,1,"ACT   {0:03d}".format(di.active))
        wdie.addstr(11,1,"PEND  {0:03d}".format(di.pending))


  def run_update(self):
    while 1:
      time.sleep(0.1)
      self.update()

  def update(self):
    # save cursor
    cy,cx = curses.getsyx()
    try:
      self.update_log()
      # run updates
      self.update_ui()
    except Exception as ex:
      self.log(str(ex))
    except:
      self.log("Unexpected Error")
    finally:
      self.refresh()
      # return cursor
      curses.setsyx(cy,cx)
      self.winput.refresh()

  def set_temperature(self, temperature):
    self.temperature = temperature

  def set_current(self, data):
    self.current = data

  def refresh(self):
    self.screen.refresh()
    self.wlog.refresh()
    self.wlogo.refresh()
    self.winfo.refresh()
    self.wmodule.refresh()
    for wasic in self.wasic:
      if wasic is not None:
        wasic.refresh()
    for wdie in self.wdie:
      if wdie is not None:
        wdie.refresh()
    self.winput.refresh()
    self.woutput.refresh()
    if self.wprevious is not None:
      self.wprevious.refresh()
    if self.wstats is not None:
      self.wstats.refresh()
    self.refresh_ui()

  def input(self, msg):
    self.winput.erase()
    self.winput.box()
    self.winput.addstr(0, 0, "INPUT")
    self.winput.addstr(1, 2, msg)
    self.winput.addstr(2, 2, "$")
    self.refresh()
    ret = self.winput.getstr(2, 4).decode(encoding="utf-8")
    return ret

  def input_single(self, msg):
    self.winput.erase()
    self.winput.box()
    self.winput.addstr(0, 0, "INPUT")
    self.winput.addstr(1, 2, msg)
    self.winput.addstr(2, 2, "$")
    self.refresh()
    ret = self.winput.getkey(2, 4)#.decode(encoding="utf-8")
    return ret

  def prompt_show(self, msg):
    self.winput.erase()
    self.winput.box()
    self.winput.addstr(0, 0, "INPUT")
    self.winput.addstr(1, 2, msg)
    self.winput.addstr(2, 2, "$")
    self.refresh()

  def prompt(self, msg, args):
    while 1:
      ret = self.input(msg)
      if ret in args:
        return ret
      if ret == 'q':
        self.end()
        break

  def prompt_yn(self, msg):
    ret = self.prompt(msg + " [y/n]", 'yn')
    if ret == 'y':
      return True
    else:
      return False

  def prompt_int(self, msg):
    while 1:
      ret = self.input(msg)
      try:
        ret_int = int(ret)
        return ret_int
      except ValueError:
        self.end()
        break

  def prompt_int_single(self, msg):
    while 1:
      ret = self.input_single(msg)
      try:
        ret_int = int(ret)
        return ret_int
      except ValueError:
        self.end()
        break

  def prompt_enter(self, msg):
    ret = self.input(msg + " [Enter Continues]")
    if ret == 'q':
      self.end()

  def log(self, msg):
    self.log_buffer.append(msg)

  def end(self):
    curses.endwin()