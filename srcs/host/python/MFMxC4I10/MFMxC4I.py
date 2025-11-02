import os
import atexit
import asyncio

from time import sleep
from datetime import datetime

from textual.containers import Horizontal, Vertical
from textual.app import App, ComposeResult
from textual import on, log, work
from textual.widgets import Button, Label, ContentSwitcher, Footer
from textual.events import Click

from mfmx import MFMx

from LogFeeder import LogFeeder
from LogBuf import LogBuf
from BHCardView import BHCardView
from BHTileView import BHTileView

class MFMxC4I(App):
    def __init__(self):
        self.bhs = [ MFMx.BlackHole(i) for i in range(4) ]
        self.bhlog = MFMx.BHLog.getLog()
        self.scriptName = os.path.basename(__file__)
        self.scriptDir = os.path.dirname(__file__)
        self.timeFormat = " %F %T  " + self.scriptName + " "
        def logcb(key,text):
            self.acceptLog(key,text)
                
        self.bhlog.setLogCallback(logcb)
        self.bhlog.setDefaultDestination(2)  # 2: use stderr if no logcb

        def cleanup():
            self.bhlog.clearLogCallback();
            self.bhlog.setDefaultDestination(0);
        atexit.register(cleanup)

        super().__init__()

        #    @work(exclusive=False)
    def acceptLog(self,key,text):
        tag=str(key)
        (t,(c,x,y)) = tag[:2],tag[2:]
        if t == "at":
            tvid = f"#bhtv-{key}"
            tv=self.query_one(tvid,BHTileView)
            tv.addToLog(text);
        elif t == "gd":
            bhid = f"#bhclogfeeder{c}"
            print(f"DDDADD({bhid})->{text[:20]}..{text[-20:]}")
            bh=self.query_one(bhid,BHCardView.BHCardLogFeeder)
            print(f"NEXTRONIC({bh})")
            bh.addToLog(text);
        else:
            print(f"ZUUUG{tag}->{text[:20]}..{text[-20:]}")

    CSS_PATH = "MFMxC4I.tcss"

    def compose(self) -> ComposeResult:
        with Horizontal():      # mode selections, log, overview
            # column 1: mode selections
            with Vertical(id="Vertical1",classes="col1v"):
                with Vertical(id="AppTitleBox",classes="col1"):
                    yield Label("MFMC4I",id="AppTitle",classes="col1")
                    with Horizontal(id="RunStopButtons",classes="col1"):
                        yield Button("RUN",id="AppRunButton",classes="rsbut")
                        yield Button("STOP",id="AppStopButton",classes="rsbut")
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
                with ContentSwitcher(initial="#bhcv0"):
                    for i in range(4):
                        yield BHCardView(self.bhs[i])
            # column 3: NYI
        with Horizontal(id="footer-outer"):
            yield Label(id="NameAndClock")
            with Horizontal(id="footer-inner"):
                yield Footer()

    def on_mount(self) -> None:
        from textual_dominfo import DOMInfo
        #DOMInfo.attach_to(self)
        
    def on_ready(self) -> None:
        self.updateClock()
        self.set_interval(1, self.updateClock)

        self.query_one("#bhcv0").action_press()

    def updateClock(self) -> None:
        clock = datetime.now().strftime(self.timeFormat)
        self.query_one("#NameAndClock").update(clock)

    def switchAllBHs(self,on):
        print("BLAAAMODS")
        self.log(f"INSWITCHALLBHS {on}")
        print("BLAAAMODS2222")
        for c in range(4):
            self.log(f"BH{c} TO CLOSE")
            self.bhs[c].close()
            self.bhs[c].setMFMxCodePath(f"{self.scriptDir}/../../../../build_cross/bin/crossmain.bin")
        self.log(f"ALL BHs CLOSED")
        if not on:
            return
        self.log(f"PREPARING TO START")
        for c in range(4):
            self.log(f"BH{c} TO START")
            self.bhs[c].startMFMxCode()

    @on(Button.Pressed, selector="#AppStopButton")
    def stop_button_clicked(self,event: Button.Pressed) -> None:
        self.log(f"DONESTOPSTOPSTOP({self.bhs})")
        #asyncio.create_task(self.switchAllBHs(False))
        self.switchAllBHs(False)

    @on(Button.Pressed, selector="#AppRunButton")
    def run_button_clicked(self,event: Button.Pressed) -> None:
        self.log(f"ALL ALL OOOOOORUNME({event})")
        #asyncio.create_task(self.switchAllBHs(True))
        self.switchAllBHs(True)
        #self.log(f"DONEGO? ALL OOOOOORUNME({self.bhs})")

    @on(Button.Pressed, selector=".hw-ish")
    def on_hw_button_clicked(self,event: Click) -> None:
        cs = self.query_one(ContentSwitcher)
        self.log(f"ONHWCONSW{event.button.id}cs({cs})cur({cs.current}")
        cs.current = event.button.id

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
        #self.log(f"HANDLERONEEE {self.title} <{message.bufferContent}> <{message.logDestId}>")
        try:
            target_log = self.query_one(message.logDestId, LogBuf)
            if target_log.selectedSource is not message.sender:
                # If a new source is selected, update the reactive attribute.
                # This will trigger watch_selectedsource for visual updates.
                if target_log.selectedSource is not None:
                    target_log.selectedSource.remove_class("selected_logfeeder")
                    target_log.selectedSource.add_class("unselected_logfeeder")
                target_log.selectedSource = message.sender
                #self.log(f"SOURCED2 {message.logDestId} {message.sender}")
            else:
                # If the same source is already selected, its buffer changed.
                # We need to explicitly update the RichLog content.
                pass # The watcher will handle visual, but we need to update content here
                #self.log(f"SAMESOURCE {message.logDestId} {message.sender}")

            target_log.selectedSource.remove_class("unselected_logfeeder")
            target_log.selectedSource.add_class("selected_logfeeder")
            # Always update the RichLog content with the latest buffer from the message
            target_log.clear()
            target_log.write(message.bufferContent)
            #self.log(f"updating zong {target_log} {target_log.selectedSource} WHATWHAT({message.bufferContent})")

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
    import faulthandler
    faulthandler.enable()
    app = MFMxC4I()
    print("LKDSSDKL",app.bhlog)
    app.run()
