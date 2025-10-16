#!/data/ackley/PART4/code/D/blackholeSpikes/venv/bin/python
from textual.app import App, ComposeResult
from textual.widgets import Label
from textual.containers import Container

class GridApp(App):
    CSS = """
    Screen {
        layout: grid;
        grid-size: 3 3; /* 3 rows, 3 columns */
        grid-columns: 1fr 1fr 1fr; /* Equal width columns */
        grid-rows: 1fr 1fr 1fr; /* Equal height rows */
        grid-gutter: 0; /* No space between cells */
    }
    Label {
        background: darkblue;
        color: white;
        text-align: center;
        content-align: center middle;
        border: solid dodgerblue; /* Optional: to visualize cell boundaries */
    }
    """

    def compose(self) -> ComposeResult:
        for i in range(9):
            yield Label(f"Item {i+1}")

if __name__ == "__main__":
    app = GridApp()
    app.run()
