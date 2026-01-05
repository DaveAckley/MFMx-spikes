from textual.app import App, ComposeResult
from textual.widgets import Header, Footer, Static
from textual.containers import Container
from textual.geometry import Size

class MyCustomWidget(Static):
    def on_mount(self) -> None:
        self.log(f"Initial size: {self.size}")

    def on_resize(self, event) -> None:
        # event.size contains the new size
        self.log(f"Widget resized to: {event.size}")
        # You can also access self.size directly here
        self.log(f"Current self.size: {self.size}")

class MyApp(App):
    CSS = """
    Container {
        layout: grid;
        grid-size: 1;
        grid-rows: 1fr;
    }
    MyCustomWidget {
        width: 1fr;
        height: 1fr;
        background: darkblue;
        color: white;
        border: solid green;
    }
    """

    def compose(self) -> ComposeResult:
        yield Header()
        with Container():
            yield MyCustomWidget("Hello, I'm a custom widget!")
        yield Footer()

if __name__ == "__main__":
    app = MyApp()
    app.run()
