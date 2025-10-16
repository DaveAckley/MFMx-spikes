#include <pybind11/embed.h>

namespace py = pybind11;

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

int main() {
  py::scoped_interpreter guard{};
  py::print("FOIDID WORLD");
  py::exec(R"(
from textual.app import App, ComposeResult
from textual.widgets import Label, Footer
from textual.containers import Container
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

class GridApp(App):
    CSS = """
    #grid-container { /* A container to hold our grid */
        width: auto; /* Shrink to fit content horizontally */
        height: auto; /* Shrink to fit content vertically */
        border: round gold; /* Optional: to visualize the container boundaries */
    }

    #my-grid { /* Apply grid styles to a specific ID */
        layout: grid;
        grid-size: 16 12; /* 3 rows, 3 columns */
        grid-columns: 4 ; /* Each column sizes to its content */
        grid-rows: 1; /* Each row sizes to its content */
        grid-gutter: 0; /* No space between cells */
        border: solid green; /* Optional: to visualize grid boundaries */
    }

    Label {
        color: grey;
        background: black;
        color: white;
        text-align: right;
        content-align: center middle;
        width: 4; /* Give labels a fixed width */
        height: 4; /* Give labels a fixed height */
        &.T {
            color: green;
        }
        &.D {
            color: #404040;
        }
        &.E {
            color: #404040;
        }
        &.C {
            color: #404060;
        }
        &.S {
            color: #604040;
        }
        &.A {
            color: #604040;
        }
        &.P {
            color: #406040;
        }
    }
    """

    def compose(self) -> ComposeResult:
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
        current = int(targ.content)
        if event.button == 1: # left
            targ.update(str(current+1))
        elif event.button == 3: # right
            targ.update(str(current-1))
        
        
if __name__ == "__main__":
    app = GridApp()
    app.run()
)");
  return 0;
}

int add(int i, int j) {
    return i * j + 1;
}

int nsqr(int i) {
    return - i * i;
}

#if 0
PYBIND11_MODULE(c4i_mfm, m) {
    m.doc() = R"pbdoc(
        Pybind11 example plugin
        -----------------------

        .. currentmodule:: c4i_mfm

        .. autosummary::
           :toctree: _generate

           add
           subtract
    )pbdoc";

    m.def("run", &run, R"pbdoc(
        Run shit

    )pbdoc");

    m.def("add", &add, R"pbdoc(
        Add two numbers

        Some other explanation about the add function.
    )pbdoc");

    m.def("nsqr", &nsqr, R"pbdoc(
        Nsqr a number

        No other explanation about the nsqr function.
    )pbdoc");

    m.def("subtract", [](int i, int j) { return i - j; }, R"pbdoc(
        Subtract two numbers

        Some other explanation about the subtract function.
    )pbdoc");

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
#endif
