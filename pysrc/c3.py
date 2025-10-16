#!/data/ackley/PART4/code/D/blackholeSpikes/venv/bin/python
from textual.app import App, ComposeResult
from textual.widgets import Label
from textual.containers import Container

class GridApp(App):
    CSS = """
    #grid-container { /* A container to hold our grid */
        width: auto; /* Shrink to fit content horizontally */
        height: auto; /* Shrink to fit content vertically */
        border: thick red; /* Optional: to visualize the container boundaries */
    }

    #my-grid { /* Apply grid styles to a specific ID */
        layout: grid;
        grid-size: 4 4; /* 3 rows, 3 columns */
        grid-columns: auto auto auto; /* Each column sizes to its content */
        grid-rows: auto auto auto; /* Each row sizes to its content */
        grid-gutter: 0; /* No space between cells */
        border: solid green; /* Optional: to visualize grid boundaries */
    }

    Label {
        background: darkblue;
        color: white;
        text-align: center;
        content-align: center middle;
        width: 4; /* Give labels a fixed width */
        height: 4; /* Give labels a fixed height */
    }
    """

    def compose(self) -> ComposeResult:
        with Container(id="grid-container"):
            with Container(id="my-grid"):
                for i in range(16):
                    yield Label(f" {i+1}",id=f"L{i}")

    def on_mount(self) -> None:
        targ = self.query_one("#L5",Label)
        targ.update("foo")
        
if __name__ == "__main__":
    app = GridApp()
    app.run()
        
