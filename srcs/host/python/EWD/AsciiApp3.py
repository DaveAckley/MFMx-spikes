from textual.app import App, ComposeResult
from textual.widgets import Header, Footer
from textual.containers import Container
from textual.scroll_view import ScrollView # Correct import for ScrollView
from textual.widget import Widget
from textual.reactive import reactive
from textual.timer import Timer
import time

# Define your custom animation widget
class AsciiAnimation(Widget):
    scroll_x = reactive(0)
    scroll_y = reactive(0)
    grid_width = reactive(100)
    grid_height = reactive(50)

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.animation_timer: Timer | None = None

    def on_mount(self) -> None:
        self.animation_timer = self.set_interval(1 / 10, self.update_animation)

    def update_animation(self) -> None:
        #XXX NO self.update()
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
        height: 1fr;/* XXX NO: flex: 1; */
        border: solid green;
    }
    AsciiAnimation {
        width: auto;
        height: auto;
    }
    """

    def compose(self) -> ComposeResult:
        yield Header()
        with ScrollView(id="animation-scroll-view"):
            yield AsciiAnimation(id="ascii-animation")
        yield Footer()

    def on_mount(self) -> None:
        animation_widget = self.query_one("#ascii-animation", AsciiAnimation)
        scroll_view = self.query_one("#animation-scroll-view", ScrollView)

        scroll_view.virtual_size = (animation_widget.grid_width, animation_widget.grid_height)

        # Initial sync
        animation_widget.scroll_x = scroll_view.scroll_x
        animation_widget.scroll_y = scroll_view.scroll_y

    # THIS IS THE CORRECTED WAY TO REACT TO SCROLLVIEW CHANGES
    def watch_scroll_view_scroll_x(self, scroll_x: int) -> None:
        """Called when the ScrollView's horizontal scroll position changes."""
        animation_widget = self.query_one("#ascii-animation", AsciiAnimation)
        animation_widget.scroll_x = scroll_x

    def watch_scroll_view_scroll_y(self, scroll_y: int) -> None:
        """Called when the ScrollView's vertical scroll position changes."""
        animation_widget = self.query_one("#ascii-animation", AsciiAnimation)
        animation_widget.scroll_y = scroll_y

if __name__ == "__main__":
    app = AsciiApp()
    app.run()
