#!/data/ackley/PART4/code/D/blackholeSpikes/venv/bin/python
from mfmx import MFMx
b = MFMx.BlackHole(3)
b.setMFMxCodePath("./build_cross/bin/crossmain.bin")
b.startMFMxCode()
from time import sleep
sleep(0.1)
print("ELDONEBO",b.close())
