from textual.widgets import Label
from textual.reactive import reactive
from textual.message import Message
from textual.events import Click
from textual import on, log

class LogFeeder(Label):
    logBuf = reactive("")
    logDestId : str

    def __init__(self,label,logtarget,**kwargs):
        super().__init__(label,**kwargs)
        # set reactive logDestId BEFORE setting reactive logBuf,
        # so watch_logBuf (below) can use it immediately?
        self.logDestId = logtarget 
        self.logBuf = ""
        self.add_class("unselected_logfeeder")
        #self.log(f"MAJORKONG({self}:::{self.logDestId})")

    class SelectedLogFeeder(Message):
        """A custom message to send when a LogFeeder is selected OR ITS CONTENT UDPATED DOH GREAT DESIGN."""
        def __init__(self, bufferContent: str, logDestId: str) -> None:
            super().__init__()
            self.bufferContent = bufferContent
            self.logDestId = logDestId
            self.sender: Widget
            #print(f"SLF MADE <{self.logDestId}> <{self.bufferContent}>")


    #@on(Click, selector=".hw-ish")
    @on(Click)
    def on_click(self) -> None:
        #self.log(f"FEEDERSAYS({self.logBuf})?\n")
        if len(self.logBuf) == 0:
            self.logBuf = f"I am I {self.id}\n"
        message = self.SelectedLogFeeder(self.logBuf, self.logDestId)
        message.sender = self
        self.post_message(message)

    def watch_logBuf(self, oldBuffer: str, newBuffer: str) -> None:
        """Watch for changes in the buffer and update the label content."""
        #self.log(f"WASLKSKLOGBUF({self}:::{self.__dict__.keys()})?\n")
        message = self.SelectedLogFeeder(self.logBuf, self.logDestId)
        message.sender = self
        self.post_message(message)
        #self.update(newBuffer)
        # also re-emit if we're the selected source
        #self.log(f"WATRCHING ({oldBuffer}) <{newBuffer}>")

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
        self.log(f"{self.id} logbuf now <{self.logBuf}>")
