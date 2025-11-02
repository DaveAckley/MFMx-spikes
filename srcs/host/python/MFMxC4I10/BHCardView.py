from textual.containers import Container, Horizontal, Vertical
from textual.app import ComposeResult
from LogFeeder import LogFeeder
from LogBuf import LogBuf
from BHTileView import BHTileView
from typing import Literal

TileTypes = Literal["D", "E", "T", "C", "A", "S", "P"]

BHTYPES = {}
# DRAM
for xy in [(x,y) for x in (0,9) for y in range(12)]:
    BHTYPES[xy] = 'D'
# ENET
for xy in [(x,1) for x in list(range(1,8))+list(range(10,17))]:
    BHTYPES[xy] = 'E'
# T6
for xy in [(x,y) for x in list(range(1,8))+list(range(10,17)) for y in range(2,12)]:
    BHTYPES[xy] = 'T'
# CPU
for xy in [(x,y) for x in (8,) for y in range(3,10,2)]:
    BHTYPES[xy] = 'C'
# MISC
BHTYPES[(8,0)] = 'A'
BHTYPES[(8,2)] = 'S'
BHTYPES[(2,0)] = 'P'
BHTYPES[(11,0)] = 'P'

class BHCardView(Container):
    def __init__(self, BHObj):
        self.BHObj = BHObj
        self.bhnum = self.BHObj.getCardNumber()
        super().__init__(id=f"bhcv{self.bhnum}") #bhcv0
        self.add_class(f"bhcv")
        
    class BHCardLogFeeder(LogFeeder):
        def __init__(self,BHObj):
            self.BHObj = BHObj
            self.bhnum = self.BHObj.getCardNumber()
            self.gdkey = f"gd{self.bhnum}00"
            label = f"BlackHole #{self.bhnum}"
            destid = f"#bhlogbuf{self.bhnum}"
            super().__init__(label,destid,id=f"bhclogfeeder{self.bhnum}")

        def addToLog(self,newtext):
            print(f"BHCLF.addToLog->{newtext[:20]}..{newtext[-20:]}")
            super().addToLog(newtext)

    def compose(self) -> ComposeResult:
        with Horizontal():
            with Vertical():
                yield self.BHCardLogFeeder(self.BHObj)
                yield LogBuf(id=f"bhlogbuf{self.bhnum}")
                yield LogBuf(id=f"tvlogbuf{self.bhnum}")
            with Container(classes="grid-container", id=f"grid-container-{self.id}"):
                with Container(classes="my-grid", id=f"my-grid-{self.id}"):
                    for r in range(12):
                        for c in range(17):
                            type = BHTYPES.get((c,r),None)
                            yield BHTileView(type,c,r,self.BHObj)

  
    def on_ready(self) -> None:
        logguy = self.query_one(RichLog)
        logguy.write("BONGO")
        logguy.write("SO")
        logguy.write("LONGO")
        logguy.write("BONGO!")
#        logguy.write(Syntax(CODE, "python", indent_guides = True))
