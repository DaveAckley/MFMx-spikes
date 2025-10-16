#!/data/ackley/PART4/code/D/blackholeSpikes/venv/bin/python
from textual.app import App, ComposeResult
from textual.containers import Grid
from textual.widgets import Footer, Markdown, Placeholder, DataTable
from typing import Any

HELP = """\
## CCC

A first hackadoodle toward a Command Control Communications app for MFM-BLACKHOLE

"""


HEADERS = tuple(hex(i)[2:] for i in range(1,12))
#ROWS = tuple(zip(range(1,16),"abcdefghijklmnop"))
ROWS = tuple((hex(i)[2:],*list(chr(i) for i in range(ord('a'),ord('k')))) for i in range(1,16))

class CCCApp(App):

    # A breakpoint consists of a width and a class name to set
    HORIZONTAL_BREAKPOINTS = [
        (0, "-narrow"),
        (40, "-normal"),
        (80, "-wide"),
        (120, "-very-wide"),
    ]

    CSS = """
    Screen {        
        Placeholder { padding: 1; }
        Grid { grid-rows: 2; height: auto; }
        # Change the styles according to the breakpoint classes
        &.-narrow {
            Grid { grid-size: 2; }
        }
        &.-normal {
            Grid { grid-size: 2; }
        }
        &.-wide {
            Grid { grid-size: 2; }
        }
        &.-very-wide {
            Grid { grid-size: 2; }
        }
    }
    DataTable {
        height: 3;
    }
    #datalayout {
        height: 16;
    }
    #layout10 {
        height: auto;
        grid-size: 2;
        border-bottom: solid $border;
    }
    #zongid {
        height: 3;
    }
    """

    def compose(self) -> ComposeResult:
        yield Markdown(HELP)
        #with Grid(id="layout10"):
        if True:
            table = DataTable[Any](id="datalayout")
            for h in HEADERS:
                table.add_column(h,width=1)
            table.add_rows(ROWS)
            table.zebra_stripes = True
            table.fixed_columns = 1
            table.cursor_type = "cell"
            yield table
            yield Placeholder(f"HEADER {HEADERS}\n",id="zongid")
        yield Footer()


if __name__ == "__main__":
    CCCApp().run()
