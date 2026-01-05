from textual.app import App, ComposeResult
from textual.widgets import Header, Footer 
from textual.containers import Container
from textual.scroll_view import ScrollView
from textual.widget import Widget
from textual.reactive import reactive
from textual.timer import Timer
import time

# ... (AsciiAnimation widget definition remains largely the same,
#      but we'll adjust how scroll_x/y are handled slightly)

class AsciiApp(App):
    BINDINGS = [
        ("q", "quit", "Quit"),
    ]

    CSS = """
    Screen {
        layout: vertical; /* Use a vertical layout for the whole screen */
    }
    Header {
        height: auto;
    }
    Footer {
        height: auto;
    }
    #animation-scroll-view {
        flex: 1; /* Allow ScrollView to take available space */
        border: solid green;
        /* No need for explicit overflow here, ScrollView handles it */
    }
    AsciiAnimation {
        /* The animation widget itself should be large enough to represent the full grid */
        width: auto; /* Will be set by the animation widget's content */
        height: auto; /* Will be set by the animation widget's content */
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

        # Set the content size of the ScrollView to match your large grid
        # This tells the ScrollView how big the "scrollable area" is
        scroll_view.virtual_size = (animation_widget.grid_width, animation_widget.grid_height)

        # We no longer directly link scrollbars, as ScrollView manages them.
        # Instead, we need to update the animation widget's scroll_x/y
        # based on the ScrollView's scroll position.

        # Initial sync
        animation_widget.scroll_x = scroll_view.scroll_x
        animation_widget.scroll_y = scroll_view.scroll_y

    # We need to react to ScrollView's scroll changes
    def on_scroll_view_scrolled(self, event: ScrollView.Scrolled) -> None:
        animation_widget = self.query_one("#ascii-animation", AsciiAnimation)
        animation_widget.scroll_x = int(event.scroll_x)
        animation_widget.scroll_y = int(event.scroll_y)

# ... (AsciiAnimation widget definition)
class AsciiAnimation(Widget):
    # Reactive attributes for scroll position (now driven by ScrollView)
    scroll_x = reactive(0)
    scroll_y = reactive(0)
    grid_width = reactive(100)  # Example large grid width
    grid_height = reactive(50)  # Example large grid height

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.animation_timer: Timer | None = None

    def on_mount(self) -> None:
        self.animation_timer = self.set_interval(1 / 10, self.update_animation)

    def update_animation(self) -> None:
        # This method is still called at 10Hz
        # It will trigger a re-render of the widget
        self.update()

    def generate_ascii_frame(self, x: int, y: int, width: int, height: int) -> list[str]:
        """
        This function will be responsible for generating your ASCII art.
        It receives the top-left coordinates (x, y) and the dimensions (width, height)
        of the currently visible window.
        It should return a list of strings, where each string is a row of ASCII characters.
        """
        # Example: A simple checkerboard pattern
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
        """
        Textual calls this method to get the content to display for the widget.
        This is where we generate the *visible* portion based on scroll_x/y
        and the actual size of the AsciiAnimation widget (which is the viewport size).
        """
        # The size of the AsciiAnimation widget itself is now the visible viewport
        visible_width = self.size.width
        visible_height = self.size.height

        # Call your procedural generation function with the current scroll position
        # and the actual visible dimensions of the widget.
        visible_ascii_data = self.generate_ascii_frame(
            self.scroll_x, self.scroll_y, visible_width, visible_height
        )
        return "\n".join(visible_ascii_data)

    # No need for watch_scroll_x/y here, as they are updated by the ScrollView event.
    # The `render` method will automatically be called when `scroll_x` or `scroll_y` changes
    # because they are reactive attributes.
