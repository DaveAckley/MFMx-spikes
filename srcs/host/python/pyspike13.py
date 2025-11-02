from datetime import datetime
from mfmx import MFMx
from rich.syntax import Syntax
from rich.text import Text
from textual import on, log
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Container, Horizontal, Vertical
from textual.events import Click
from textual.message import Message
from textual.reactive import reactive
from textual.widgets import Label, Footer, RichLog, Button, ContentSwitcher
from typing import Literal
import atexit
import os
import re

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

class LogBuf(RichLog):
    """Display bottom of log buffer"""
    selectedSource: reactive["Label | None"] = reactive(None)
    
    def __init__(self, **kwargs):
        super().__init__(highlight=True, markup=True,**kwargs)
        self.write(f"I am a LogBuf named {self.id}")
        self.tvid = None
        
    def watchSelectedSource(self, oldSource: "Label | None",
                            newSource: "Label | None") -> None:
        """
        Watches for changes in the selectedSource.
        This is primarily for internal state management, the actual content update
        is handled by the message.
        """
        if oldSource:
            oldSource.remove_class("selected")
            oldSource.add_class("unselected")
        if newSource:
            newSource.remove_class("unselected")
            newSource.add_class("selected")
            self.clear()
            self.write(newSource.buffer)

    # def selectTileView(self,tvid):
    #     self.tvid = tvid
    #     self.write(f"SELECTTILEVIEW({self.tvid})")

class LogFeeder(Label):
    logBuf = reactive("")
    logDestId : str

    def __init__(self,label,logtarget,**kwargs):
        super().__init__(label,**kwargs)
        self.logBuf = ""
        self.logDestId = logtarget
        self.add_class("unselected_logfeeder")

    class SelectedLogFeeder(Message):
        """A custom message to send when a LogFeeder is selected OR ITS CONTENT UDPATED DOH GREAT DESIGN."""
        def __init__(self, bufferContent: str, logDestId: str) -> None:
            super().__init__()
            self.bufferContent = bufferContent
            self.logDestId = logDestId
            self.sender: Widget
            print(f"SLF MADE <{self.logDestId}> <{self.bufferContent}>")


    #@on(Click, selector=".hw-ish")
    @on(Click)
    def on_click(self) -> None:
        self.log(f"FEEDERSAYS({self.logBuf})?\n")
        if len(self.logBuf) == 0:
            self.logBuf = f"I am I {self.id}\n"
        message = self.SelectedLogFeeder(self.logBuf, self.logDestId)
        message.sender = self
        self.post_message(message)

    def watch_buffer(self, oldBuffer: str, newBuffer: str) -> None:
        """Watch for changes in the buffer and update the label content."""
        self.update(newBuffer)
        # also re-emit if we're the selected source
        self.log(f"WATRCHING {oldBuffer} {newBuffer}")

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

class BHTileView(LogFeeder):
    def __init__(self,type,x,y,BHObj):
        self.BHObj = BHObj
        self.bhnum = self.BHObj.getCardNumber()
        self.t6key = self.BHObj.getT6Key(self.bhnum,x,y)
        label = type + str(self.t6key)[3:] if type else " - "
        destid = f"#tvlogbuf-bhcv{self.bhnum}itself"
        super().__init__(label,destid,id=f"bhtv-{self.t6key}",variant=type)

    # def on_click(self) -> None:
    #     pass

    def __repr__(self):
        return super().__repr__()+self.logBuf

class BHCardLogFeeder(LogFeeder):
    def __init__(self,BHObj):
        self.BHObj = BHObj
        self.bhnum = self.BHObj.getCardNumber()
        self.gdkey = f"gd{self.bhnum}00"
        label = f"BlackHole #{self.bhnum}"
        destid = f"#bhlogbuf-bhcv{self.bhnum}itself"
        super().__init__(label,destid,id=f"bhclabel{self.bhnum}")

    def addToLog(self,newtext):
        print(f"BHCLF.addToLog->{newtext[:20]}..{newtext[-20:]}")
        super().addToLog(newtext)

class BHCardView(Container):
    def __init__(self, BHObj):
        self.BHObj = BHObj
        self.bhnum = self.BHObj.getCardNumber()
        self.subid = f"bhcv{self.bhnum}itself"
        self.label = f"BlackHole#{self.bhnum}"
        super().__init__(id=self.subid)
        self.add_class(f"bhcv")
        
    def compose(self) -> ComposeResult:
        with Horizontal():
            with Vertical():
                yield BHCardLogFeeder(self.BHObj)
                yield LogBuf(id=f"bhlogbuf-{self.subid}")
                yield LogBuf(id=f"tvlogbuf-{self.subid}")
            with Container(classes="grid-container", id=f"grid-container-{self.subid}"):
                with Container(classes="my-grid", id=f"my-grid-{self.subid}"):
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

class MFMxC4I(App):
    def __init__(self):
        self.bhs = [ MFMx.BlackHole(i) for i in range(4) ]
        self.bhlog = MFMx.BHLog.getLog()
        self.scriptName = os.path.basename(__file__)
        self.timeFormat = " %F %T  " + self.scriptName + " "
        def logcb(key,text):
            tag=str(key)
            (t,(c,x,y)) = tag[:2],tag[2:]
            if t == "at":
                tvid = f"#bhtv-{key}"
                tv=self.query_one(tvid,BHTileView)
                tv.addToLog(text);
            elif t == "gd":
                bhid = f"#bhclabel{c}"
                bh=self.query_one(bhid,BHCardLogFeeder)
                bh.addToLog(text);
                print(f"DDDADD({bh},{bhid})->{text[:20]}..{text[-20:]}")
            else:
                print(f"ZUUUG{tag}->{text[:20]}..{text[-20:]}")
                
        self.bhlog.setLogCallback(logcb)
        self.bhlog.setDefaultDestination(2)

        def cleanup():
            self.bhlog.clearLogCallback();
            self.bhlog.setDefaultDestination(0);
        atexit.register(cleanup)

        super().__init__()

    CSS_PATH = "pyspike13.tcss"

    def compose(self) -> ComposeResult:
        with Horizontal():      # mode selections, log, overview
            # column 1: mode selections
            with Vertical(id="Vertical1",classes="col1v"):
                with Vertical(id="AppTitleBox",classes="col1"):
                    yield Label("MFMC4I",id="AppTitle",classes="col1")
                    yield Button("RUN",id="AppRunButton",classes="col1")
                with Vertical():
                    yield Label("Hardware",id="BlackHoleSelectorBox",classes="col1 hw-ish")
                    for i in range(4):
                        yield Button(f"BlackHole#{i}", id=f"bhcv{i}",
                                     classes="col1 hw-ish")
                    yield Label("Software",id="PhysicSelectorBox",classes="col1 sw-ish")
                    for i in range(8):
                        yield Button(f"Physics#{i}", id=f"PhysicsSelectButton-{i}",classes="col1 sw-ish")
            # column 2: mode detail
            with Vertical(id="Vertical2",classes="col2v"):
                with ContentSwitcher(initial="#bhcv0itself"):
                    for i in range(4):
                        yield BHCardView(self.bhs[i])
            # column 3: NYI
        with Horizontal(id="footer-outer"):
            yield Label(id="NameAndClock")
            with Horizontal(id="footer-inner"):
                yield Footer()

    def on_ready(self) -> None:
        self.updateClock()
        self.set_interval(1, self.updateClock)

    def updateClock(self) -> None:
        clock = datetime.now().strftime(self.timeFormat)
        self.query_one("#NameAndClock").update(clock)

    @on(Button.Pressed, selector="#AppRunButton")
    def on_run_button_clicked(self,event: Button.Pressed) -> None:
        self.log(f"ALL ALL OOOOOORUNME({event})")
        c = 0
        self.bhs[c].close()
        self.bhs[c].setMFMxCodePath("./build_cross/bin/crossmain.bin")
        self.bhs[c].startMFMxCode()
        self.log(f"DONEGO? ALL OOOOOORUNME({self.bhs[c]})")

    @on(Button.Pressed, selector=".hw-ish")
    def on_hw_button_clicked(self,event: Click) -> None:
        self.log(f"ONHWCONSW{event.button.id}")
        self.query_one(ContentSwitcher).current = event.button.id+"itself"

    @on(Button.Pressed, selector=".sw-ish")
    def on_sw_button_clicked(self,event: Click) -> None:
        log("SELECT PHYSICS",event.button)

    # @on(Click, selector="Label")
    # def on_label_clicked(self,event: Click) -> None:
    @on(LogFeeder.SelectedLogFeeder)
    def on_logfeeder_selected(self, message: LogFeeder.SelectedLogFeeder) -> None:
        """
        Handle the custom message from a LogFeeder
        This method acts as a central router to the correct LogBuf.
        """
        self.log(f"HANDLERONEEE {self.title} <{message.bufferContent}> <{message.logDestId}>")
        try:
            target_log = self.query_one(message.logDestId, LogBuf)
            if target_log.selectedSource is not message.sender:
                # If a new source is selected, update the reactive attribute.
                # This will trigger watch_selectedsource for visual updates.
                target_log.selectedSource = message.sender
                self.log(f"SOURCED2 {message.logDestId} {message.sender}")
            else:
                # If the same source is already selected, its buffer changed.
                # We need to explicitly update the RichLog content.
                pass # The watcher will handle visual, but we need to update content here
                self.log(f"SAMESOURCE {message.logDestId} {message.sender}")

            # Always update the RichLog content with the latest buffer from the message
            target_log.clear()
            target_log.write(message.bufferContent)
            self.log(f"updating zong {target_log} {target_log.selectedSource} WHATWHAT({message.bufferContent})")

        except Exception as e:
            self.log(f"Error finding target log {message.logDestId}: {e}")

        # targ = event.widget
        # targid = targ.id
        # log("clooooookd",targ,targid)
        # (t,(c,x,y)) = targid[-5:-3],(int(x,36) for x in targid[-3:]) # tag is at end of id
        # current = targ.content
        # tvid = f"#tvlogbuf-bhcv{c}itself"
        # log("pdududddd",current,tvid)
        # logguy=self.query_one(tvid,LogBuf)
        # logguy.selectTileView(targid)
        # #logguy = self.query_one(RichLog)
        # log("cleeeked",targid,event.button,self,targ,self.bhs[c])
        # if event.button == 3:
        #     self.bhs[c].close()
        #     self.bhs[c].setMFMxCodePath("./build_cross/bin/crossmain.bin")
        #     self.bhs[c].startMFMxCode()
        #     msg = Text.assemble((f"{event.button}@{targid}","red")," GOTS ",(f"{current}","bold green"), (" and out and out and out and out and out and out and out and out MORE and out and outand out and outand out and outand out and outand out and outand out and outand out and out", "blue"),no_wrap=True,overflow="ellipsis")
        #     logguy.write(msg) #f"[red]{event.button} GOTS [bold][green]{current}[/green]zot[/bold] and out and out and out and out and out and out and out and out")
        # else:
        #     logguy.write(f"{event.button}@{targid} GOTSS {current}")

if __name__ == "__main__":
    app = MFMxC4I()
    print("LKDSSDKL",app.bhlog)
    app.run()
