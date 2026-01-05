#!/data/ackley/PART4/code/D/blackholeSpikes/venv/bin/python
from mfmx import MFMx
from time import sleep

def logcb(key,text):
  print(f"{key}:<{text}>",end='')
MFMx.BHLog.setLogCallback(logcb)

def clearcb():
  MFMx.BHLog.clearLogCallback()

import atexit
atexit.register(clearcb)

import sys
argType = None
if len(sys.argv) == 2:
  argType = sys.argv[1]

class EWD:
  BHMIN = 3
  BHMAX = 3
  def __init__(self):
    self.bhs = [ MFMx.BlackHole(i) for i in range(self.BHMIN,self.BHMAX+1) ]
  def reset(self):
    global argType
    for bh in self.bhs:
      bh.setMFMxCodePath("../../../../build_cross/bin/crossmain.bin")
      bh.close()
      if argType:
        bh.setStartDecayType(int(argType))
      bh.deployMFMxCode()
  def start(self):
    for bh in self.bhs:
      bh.startMFMxCode()
  def stop(self):
    for bh in self.bhs:
      bh.stopMFMxCode()
  def slowScan(self,count):
    for bh in self.bhs:
      bh.runSlowScans(count)

ewd = EWD()
ewd.reset()

ewc = MFMx.EWControl.getEWControl()
print(ewc)

ewd.start()
print("STARTING EWPROC\n")
ewc.setActive(True)
for i in range(1000):
  sleep(1)
  ewd.slowScan(140);
print("STOPPING EWPROC\n")
ewc.setActive(False)
ewd.stop()

# ctr=ewc.pickEWCenter()
# print(ctr)
# for t in range(0,(1<<9)+2,33): # couple bogus ones
#   a = ewc.makeAtom(t)
#   print(t,hex(t),a,a.isValid())

# for x in range(-8,8+1):
#   for y in range(-8,8+1):
#     c = MFMx.S32C(x,y)
#     a = ewc.getAtom(c)
#     if (a.getType() != 0):
#       print("WOORD",c,a)
# at = ewc.pickEWCenter()
# print("dkkdsout",at, ewc.getAtom(at))
# ewc.setAtom(at,ewc.makeAtom(0))
# print("Nonedkkdsout",at, ewc.getAtom(at))
# print("Noc",ewc.pickEWCenter())

