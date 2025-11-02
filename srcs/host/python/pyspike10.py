from mfmx import MFMx
from rich.text import Text
from rich.syntax import Syntax
from textual.app import App, ComposeResult
from textual.widgets import Label, Footer, RichLog
from textual.containers import Container, Horizontal
from textual import on
from textual.events import Click
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

class LogBuf(RichLog):
    """Display bottom of log buffer"""
    def __init__(self, **kwargs):
        super().__init__(highlight=True, markup=True)

    DEFAULT_CSS = """
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
class GridApp(App):
    CSS_PATH = "pyspike10.tcss"

    def compose(self) -> ComposeResult:
        with Horizontal():
            yield LogBuf()
            with Container(id="grid-container"):
                with Container(id="my-grid"):
                    for r in range(12):
                        for c in range(16):
                            type = BHTYPES.get((c,r),None)
                            yield Label(f"{type}{c:1x}{r:1x}" if type else " ",id=f"L{r}x{c}",variant=type)
        yield Footer()
    @on(Click, selector="Label")
    def on_label_clicked(self,event: Click) -> None:
        targ = event.widget
        current = targ.content
        logguy = self.query_one(RichLog)
        if event.button == 3:
            msg = Text.assemble((f"{event.button}","red")," GOTS ",(f"{current}","bold green"), (" and out and out and out and out and out and out and out and out MORE and out and outand out and outand out and outand out and outand out and outand out and outand out and out", "blue"),no_wrap=True,overflow="ellipsis")
            logguy.write(msg) #f"[red]{event.button} GOTS [bold][green]{current}[/green]zot[/bold] and out and out and out and out and out and out and out and out")
        else:
            logguy.write(f"{event.button} GOTSS {current}")
        
    def on_ready(self) -> None:
        logguy = self.query_one(RichLog)
        logguy.write("BONGO")
        logguy.write("SO")
        logguy.write("LONGO")
        logguy.write("BONGO!")
#        logguy.write(Syntax(CODE, "python", indent_guides = True))
        
if __name__ == "__main__":
    app = GridApp()
    app.run()
