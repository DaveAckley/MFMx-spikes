from textual.app import App, ComposeResult
from textual.widgets import Header, Footer, Static
from textual.containers import Container
from textual.geometry import Size
from textual.events import Resize

class MyCustomWidget(Static):
    def on_mount(self) -> None:
        self.log(f"MyCustomWidget: Initial size on mount: {self.size}")

    def on_resize(self, event: Resize) -> None:
        self.log(f"MyCustomWidget: Received on_resize event. New size from event: {event.size}")
        self.log(f"MyCustomWidget: Current self.size after resize event: {self.size}")

class MyApp(App):
    CSS = """
    Screen {
        align: center middle;
    }
    Container {
        width: 1fr;
        height: 1fr;
        border: heavy magenta;
        layout: vertical;
    }
    MyCustomWidget {
        width: 1fr;
        height: 1fr;
        background: darkblue;
        color: white;
        border: solid green;
        text-align: center;
        # Corrected: Use 'align' or 'align-vertical' for vertical alignment
        align: center middle; # This centers both horizontally and vertically
    }
    """

    def compose(self) -> ComposeResult:
        yield Header()
        with Container():
            yield MyCustomWidget("Hello, I'm a custom widget!")
        yield Footer()

    def on_resize(self, event: Resize) -> None:
        self.log(f"App: Terminal resized to: {event.size}")
        if self.query_one(MyCustomWidget, None) is not None:
            widget = self.query_one(MyCustomWidget)
            self.log(f"App: MyCustomWidget's size after app resize: {widget.size}")

if __name__ == "__main__":
    app = MyApp()
    app.run()
