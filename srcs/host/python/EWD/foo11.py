from textual.app import App, ComposeResult
from textual.widgets import Header, Footer, Static
from textual.containers import Container
from textual.geometry import Size
from textual.events import Resize

class MyCustomWidget(Static):
    def on_mount(self) -> None:
        self.log(f"MyCustomWidget: Initial size on mount: {self.size}")

    # This method *should* be called when the widget's allocated space changes.
    # If it's not firing, it might be due to how the parent container's layout
    # is being re-evaluated, or the event being handled higher up.
    def on_resize(self, event: Resize) -> None:
        self.log(f"MyCustomWidget: Received on_resize event. New size from event: {event.size}")
        self.log(f"MyCustomWidget: Current self.size after resize event: {self.size}")
        # You can also update content or perform other actions here based on the new size

class MyApp(App):
    CSS = """
    Screen {
        align: center middle;
    }
    Container {
        width: 1fr;
        height: 1fr;
        border: heavy magenta;
        layout: vertical; /* Ensure the container itself can resize */
    }
    MyCustomWidget {
        width: 1fr;
        height: 1fr;
        background: darkblue;
        color: white;
        border: solid green;
        text-align: center;
        vertical-align: middle;
    }
    """

    def compose(self) -> ComposeResult:
        yield Header()
        with Container():
            yield MyCustomWidget("Hello, I'm a custom widget!")
        yield Footer()

    # Handle resize events at the App level to observe terminal window changes
    def on_resize(self, event: Resize) -> None:
        self.log(f"App: Terminal resized to: {event.size}")
        # After the app resizes, the layout system will update children.
        # You can then query the widget's size.
        if self.query_one(MyCustomWidget, None) is not None:
            widget = self.query_one(MyCustomWidget)
            self.log(f"App: MyCustomWidget's size after app resize: {widget.size}")

if __name__ == "__main__":
    app = MyApp()
    app.run()
