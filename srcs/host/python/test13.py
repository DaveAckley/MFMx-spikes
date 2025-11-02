#!/data/ackley/PART4/code/D/blackholeSpikes/venv/bin/python
from mfmx import MFMx
l = MFMx.BHLog.getLog()
var = 0
def cb(key,text):
  global var
  var = var + 1
  print(f"{var} CHUG({key} -> {text})")

cb("TEST","ZOOISI")
l.setLogCallback(cb)
l.setDefaultDestination(3)
print("COMMIBALM")
print(l)

print("BUILDBH2")
b = MFMx.BlackHole(2)
print("BUILDBH2 SET PATH")
b.setMFMxCodePath("./build_cross/bin/crossmain.bin")

print("START",b)
b.startMFMxCode()
print("STARTED",b)
from time import sleep
sleep(1)
print("ELDONEBO")
l.clearLogCallback()
l.setDefaultDestination(0)
print("NNNOOOW",b.close())
print("TRREEGO",b.open())
l.setLogCallback(cb)
b.startMFMxCode()
sleep(1)
l.clearLogCallback()
print("AND OUT",b.close())

