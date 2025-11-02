import time
from mfmx import MFMx
from rich.text import Text
from rich.syntax import Syntax
from textual.app import App, ComposeResult, RenderResult
from textual.widgets import Label, Footer, RichLog, Button
from textual.containers import Container, Horizontal, Vertical
from textual.renderables.gradient import LinearGradient
from textual import on, log
from textual.events import Click
from typing import Literal
import re
import atexit
import os

TileTypes = Literal["D", "E", "T", "C", "A", "S", "P"]

COLORS = [
        "#881177",
        "#aa3355",
        "#cc6666",
        "#ee9944",
        "#eedd00",
        "#99dd55",
        "#44dd88",
        "#22ccbb",
        "#00bbcc",
        "#0099cc",
        "#3366bb",
        "#663399",
    ]
STOPS = [(i / (len(COLORS) - 1), color) for i, color in enumerate(COLORS)]

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

class LogBuf(RichLog):
    """Display bottom of log buffer"""
    def __init__(self, **kwargs):
        super().__init__(highlight=True, markup=True)

    DEFAULT_CSS_HOLD = """
    LogBuf {
        width: 40%;
        height: 100%;
        padding: 1 1;
        background: $panel;
        border: $secondary tall;
        content-align: left top;
    }
    """
CODE = '''\
def loop_first_last(values: Iterable[T]) -> Iterable[tuple[bool, bool, T]]:
    """Iterate and generate a tuple with a flag for first and last value."""
    iter_values = iter(values)
    try:
        previous_value = next(iter_values)
    except StopIteration:
        return
    first = True
    for value in iter_values:
        yield first, False, previous_value
        first = False
        previous_value = value
    yield first, True, previous_value\
'''

class BHTileView(Label):
    def __init__(self,type,x,y,BHObj):
        self.BHObj = BHObj
        self.bhnum = self.BHObj.getCardNumber()
        self.t6key = self.BHObj.getT6Key(self.bhnum,x,y)
        label = type + str(self.t6key)[3:] if type else " - "
        super().__init__(label,id=f"bhtv-{self.t6key}",variant=type)
        self.logBuf = "HARO"

    def __repr__(self):
        return super().__repr__()+self.logBuf

    def eatFrontOfLog(self):
        softmax = 1000
        if len(self.logBuf) > 2*softmax:
            i = self.logBuf.find('\n', softmax)
            if i < 0:
                i = softmax
            self.logBuf = self.logBuf[i:]
        
    def addToLog(self,newtext):
        self.eatFrontOfLog()
        self.logBuf += newtext

class BHCardView(Container):
    def __init__(self, BHObj):
        self.BHObj = BHObj
        self.bhnum = self.BHObj.getCardNumber()
        self.subid = f"bhcv{self.bhnum}"
        self.label = f"BlackHole#{self.bhnum}"
        super().__init__()
        
    def compose(self) -> ComposeResult:
        with Horizontal():
            with Vertical():
                yield Label("ZONG: "+self.label,classes="bh-label",id=f"bhlabel-{self.subid}")
                yield LogBuf()
            with Container(classes="grid-container", id=f"grid-container-{self.subid}"):
                with Container(classes="my-grid", id=f"my-grid-{self.subid}"):
                    for r in range(12):
                        for c in range(17):
                            type = BHTYPES.get((c,r),None)
                            yield BHTileView(type,c,r,self.BHObj)
        yield Footer()

  
    def on_ready(self) -> None:
        logguy = self.query_one(RichLog)
        logguy.write("BONGO")
        logguy.write("SO")
        logguy.write("LONGO")
        logguy.write("BONGO!")
#        logguy.write(Syntax(CODE, "python", indent_guides = True))
        
def validCSSId(id_string: str) -> bool:
    """
        Checks if a string is a valid CSS identifier that can be used as an ID.
        A valid CSS identifier can contain letters, digits, hyphens, and underscores.
        It cannot start with a digit or a hyphen.
    """
    if not id_string:
        return False
    # Regex for a valid CSS identifier
    # Starts with a letter, underscore, or non-ASCII character,
    # followed by letters, digits, hyphens, or underscores.
    # This is a simplified check, a full CSS identifier regex is more complex,
    # but this covers common Textual ID use cases.
    return bool(re.fullmatch(r"^[a-zA-Z_][a-zA-Z0-9_-]*$", id_string))

class Everything(Container):
    def __init__(self):
        self.bhs = [ MFMx.BlackHole(i) for i in range(4) ]
        self.bhlog = MFMx.BHLog.getLog()
        def logcb(key,text):
            tvid = f"#bhtv-{key}"
            print(f"ZUUUG{tvid}->{text[:20]}..")
            if not validCSSId(tvid):
                return
            matches = self.query(tvid)
            if len(matches) > 0:
                tv = matches.first()
                tv.addToLog(text)
        self.bhlog.setLogCallback(logcb)
        self.bhlog.setDefaultDestination(2)

        def cleanup():
            self.bhlog.clearLogCallback();
            self.bhlog.setDefaultDestination(0);
        atexit.register(cleanup)

        super().__init__()

    def render(self) -> RenderResult:
        return LinearGradient(time.time()*90, STOPS)
        
    CSS_PATH = "pyspike11.tcss"
    def compose(self) -> ComposeResult:
        with Horizontal():      # mode selections, log, overview
            # column 1: mode selections
            with Vertical(id="VVVV"):
                yield Label(os.path.basename(__file__),id="AppTitleBox",classes="col1")
                with Vertical():
                    yield Label("Hardware",id="BlackHoleSelectorBox",classes="col1 hw-ish")
                    for i in range(4):
                        yield Button(f"BlackHole#{i}", id=f"BHSelectButton-{i}",
                                     compact=True,
                                     classes="col1 hw-ish")
                    yield Label("Software",id="PhysicSelectorBox",classes="col1 sw-ish")
                    for i in range(8):
                        yield Button(f"Physics#{i}", id=f"PhysicsSelectButton-{i}",classes="col1 sw-ish")
            # column 2: mode detail
            with Vertical():
                yield BHCardView(self.bhs[3])
            # column 3: NYI

    @on(Click, selector="Label")
    def on_label_clicked(self,event: Click) -> None:
        targ = event.widget
        targid = targ.id
        current = targ.content
        logguy = self.query_one(RichLog)
        log("cleeeked",event.button,self,targ,targid,self.bhs[3])
        if event.button == 3:
            self.bhs[3].close()
            self.bhs[3].setMFMxCodePath("./build_cross/bin/crossmain.bin")
            self.bhs[3].startMFMxCode()
            msg = Text.assemble((f"{event.button}@{targid}","red")," GOTS ",(f"{current}","bold green"), (" and out and out and out and out and out and out and out and out MORE and out and outand out and outand out and outand out and outand out and outand out and outand out and out", "blue"),no_wrap=True,overflow="ellipsis")
            logguy.write(msg) #f"[red]{event.button} GOTS [bold][green]{current}[/green]zot[/bold] and out and out and out and out and out and out and out and out")
        else:
            logguy.write(f"{event.button}@{targid} GOTSS {current}")

class MFMxC4I(App):
    def compose(self) -> ComposeResult:
        yield Everything()

if __name__ == "__main__":
    app = MFMxC4I()
    #print("LKDSSDKL",app.bhlog)
    app.run()
