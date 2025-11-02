#!/data/ackley/PART4/code/D/blackholeSpikes/venv/bin/python
from mfmx import MFMx
from random import randint
bhs = list(MFMx.BlackHole(c) for c in range(4))
for i in range(2):
    print("START",i)
    for bh in bhs:
        bh.setMFMxCodePath("./build_cross/bin/crossmain.bin")
    steps = 20
    for j in range(steps):
        n = randint(0,3)
        bh = bhs[n]
        np = randint(1,9)      # HAS_CARD_NUM..HAS_T6_CODE_RUNNING
        op = bh.getPhase()
        print(f"\n> {i} #{j}/{steps} PHASE CHANGE {n}: {op} -> {np}")
        bh.setPhase(np)
    for bh in bhs:
        bh.close()
    print("END",i)
    
