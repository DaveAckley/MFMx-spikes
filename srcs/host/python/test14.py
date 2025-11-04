#!/data/ackley/PART4/code/D/blackholeSpikes/venv/bin/python
from mfmx import MFMx
from time import sleep
l = MFMx.BHLog.getLog()
var = 0
def cb(key,text):
  global var
  var = var + 1
  print(f"{var} CHUG:{key} -> {text}",end='')
  sleep(.02) # waste time to cause problems?

cb("TEST","ZOOISI")
l.setDefaultDestination(2) # stderr if no logcb
l.setLogCallback(cb)
print("COMMIBALM")
print(l)
print("BUILDBHS")
bhs = [MFMx.BlackHole(i) for i in range(4)]
print("BUILDBH SET PATH")
[ b.setMFMxCodePath("./build_cross/bin/crossmain.bin")
  for b in bhs]

[ (print("START",b),
   b.startMFMxCode(),
   print("STARTED",b))
   for b in bhs]

from time import sleep
sleep(1)
print("ELDONEBO")
l.setDefaultDestination(0) # discard if no logcb
l.clearLogCallback()
print("NNNOOOW",[ b.close() for b in bhs])
print("TRREEGO",[ b.open() for b in bhs])
l.setLogCallback(cb)
[ b.startMFMxCode() for b in bhs]
for i in range(2):
  sleep(1)
  [ b.monitorFleet() for b in reversed(bhs)]
sleep(2)
[ b.stopMFMxCode() for b in bhs]
sleep(2)
l.clearLogCallback()
print("AND OUT",[ b.close() for b in bhs])

