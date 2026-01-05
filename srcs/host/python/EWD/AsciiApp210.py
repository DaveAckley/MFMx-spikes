from textual.app import App, ComposeResult
from textual.widgets import Header, Footer
from textual.containers import Container
from textual.scroll_view import ScrollView
from textual.widget import Widget
from textual.reactive import reactive
from textual.timer import Timer
from textual.geometry import Size
import time

# Define your custom animation widget
class AsciiAnimation(Widget):
    scroll_x = reactive(0, layout=True)
    scroll_y = reactive(0, layout=True)
    grid_width = reactive(100)
    grid_height = reactive(50)

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.animation_timer: Timer | None = None
        print(f"AsciiAnimation: Initialized with ID {self.id}") # Debugging

    def on_mount(self) -> None:
        print(f"AsciiAnimation: Mounted. Starting timer for ID {self.id}") # Debugging
        self.animation_timer = self.set_interval(1 / 10, self.update_animation)

    def on_unmount(self) -> None:
        print(f"AsciiAnimation: Unmounted. Stopping timer for ID {self.id}") # Debugging
        if self.animation_timer:
            self.animation_timer.stop()

    def update_animation(self) -> None:
        self.refresh()

    def generate_ascii_frame(self, x: int, y: int, width: int, height: int) -> list[str]:
        frame = []
        for row_idx in range(height):
            row_chars = []
            for col_idx in range(width):
                global_x = x + col_idx
                global_y = y + row_idx
                if (global_x // 5 + global_y // 5) % 2 == 0:
                    row_chars.append("#")
                else:
                    row_chars.append(".")
            frame.append("".join(row_chars))
        return frame

    def render(self) -> str:
        # Debugging: Print current scroll values and visible size when rendering
        # print(f"AsciiAnimation.render: scroll_x={self.scroll_x}, scroll_y={self.scroll_y}, visible_size=({self.size.width}, {self.size.height})")

        if not hasattr(self.size, 'width') or not hasattr(self.size, 'height'):
            return "Error: Widget size not properly initialized."

        visible_width = self.size.width
        visible_height = self.size.height

        visible_ascii_data = self.generate_ascii_frame(
            self.scroll_x, self.scroll_y, visible_width, visible_height
        )
        return "\n".join(visible_ascii_data)

class AsciiApp(App):
    BINDINGS = [
        ("q", "quit", "Quit"),
    ]

    CSS = """
    Screen {
        layout: vertical;
    }
    Header {
        height: auto;
    }
    Footer {
        height: auto;
    }
    #animation-scroll-view {
        height: 1fr;
        border: solid green;
        # Ensure ScrollView itself has a layout, even if it's default
        # This helps it manage its child correctly.
        layout: vertical; 
    }
    AsciiAnimation {
        # The key here is to ensure the AsciiAnimation widget is *larger*
        # than the ScrollView's visible area, so scrolling has an effect.
        # It should take its size from the virtual_size, not 100% of the viewport.
        # Setting explicit width/height to match grid_width/height is crucial.
        width: auto; # Let content determine width, or set to grid_width
        height: auto; # Let content determine height, or set to grid_height
        min-width: var(--grid-width); # Ensure it's at least the full grid size
        min-height: var(--grid-height); # Ensure it's at least the full grid size
    }
    """

    def compose(self) -> ComposeResult:
        yield Header()
        with ScrollView(id="animation-scroll-view"):
            # Pass grid_width and grid_height as CSS variables to the widget
            # This allows CSS to reference the dynamic size of the content.
            yield AsciiAnimation(id="ascii-animation", 
                                 styles={"--grid-width": f"{AsciiAnimation.grid_width}ch", 
                                         "--grid-height": f"{AsciiAnimation.grid_height}vh"})
        yield Footer()

    def on_mount(self) -> None:
        animation_widget = self.query_one("#ascii-animation", AsciiAnimation)
        scroll_view = self.query_one("#animation-scroll-view", ScrollView)

        # Set virtual_size using a Size object, which is correct.
        scroll_view.virtual_size = Size(animation_widget.grid_width, animation_widget.grid_height)
        print(f"ScrollView virtual_size set to: {scroll_view.virtual_size}") # Debugging

        # Explicitly watch the ScrollView's reactive attributes
        scroll_view.watch(scroll_view, "scroll_x", lambda x: setattr(animation_widget, "scroll_x", x))
        scroll_view.watch(scroll_view, "scroll_y", lambda y: setattr(animation_widget, "scroll_y", y))

        # Initial sync
        animation_widget.scroll_x = scroll_view.scroll_x
        animation_widget.scroll_y = scroll_view.scroll_y
        print(f"AsciiApp.on_mount: Initial animation_widget scroll_x={animation_widget.scroll_x}, scroll_y={animation_widget.scroll_y}")

if __name__ == "__main__":
    app = AsciiApp()
    app.run()
