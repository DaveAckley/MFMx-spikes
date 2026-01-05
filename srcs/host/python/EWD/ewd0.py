#!/data/ackley/PART4/code/D/blackholeSpikes/venv/bin/python
from mfmx import MFMx
from time import sleep

bh = MFMx.BlackHole(2)
bh.setMFMxCodePath("../../../../build_cross/bin/crossmain.bin")
bh.deployMFMxCode()
exit(2)



ewc = MFMx.EWControl.getEWControl()
ctr=ewc.pickEWCenter()
print(ctr)
for t in range(0,(1<<9)+2,33): # couple bogus ones
  a = ewc.makeAtom(t)
  print(t,hex(t),a,a.isValid())

for x in range(-8,8+1):
  for y in range(-8,8+1):
    c = MFMx.S32C(x,y)
    a = ewc.getAtom(c)
    if (a.getType() != 0):
      print("WOORD",c,a)
at = ewc.pickEWCenter()
print("dkkdsout",at, ewc.getAtom(at))
ewc.setAtom(at,ewc.makeAtom(0))
print("Nonedkkdsout",at, ewc.getAtom(at))
print("Noc",ewc.pickEWCenter())

