#!/data/ackley/PART4/code/D/blackholeSpikes/venv/bin/python
from mfmx import MFMx
from time import sleep
ewc = MFMx.EWControl.getEWControl()
ctr=ewc.pickEWCenter()
print("CTR",ctr)
for t in range(0,(1<<9)+142,33): # couple bogus ones
  a = ewc.makeAtom(t)
  print(t,hex(t),a,a.isValid())

for x in range(-8,8+1):
  for y in range(-8,8+1):
    c = MFMx.S32C(x,y)
    print("AAAAAAT",c)
    a = ewc.getAtom(c)
    if (a.getType() != 0):
      print("WOORD",c,a)
while True:
  at = ewc.pickEWCenter()
  if not at:
    print("MISS",ewc.statsLine())
  else:
    break
print("dkkdsout",at)
print("dkkdsoutatom", ewc.getAtom(at))
ewc.setAtom(at,ewc.makeAtom(0))
print("Nonedkkdsout",at, ewc.getAtom(at))
print("Noc",ewc.pickEWCenter())

