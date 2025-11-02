from textual.widgets import Label, RichLog
from textual.reactive import reactive

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
