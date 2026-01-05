import time
from textual.app import App, ComposeResult
from textual.geometry import Size
from textual.reactive import reactive
from textual.scroll_view import ScrollView
from textual.timer import Timer
from textual.widget import Widget
from textual.widgets import Header, Footer

# ----------------------------------------------------------------------
# 1. The Custom Animation Widget
# ----------------------------------------------------------------------
# This widget represents your entire large grid. It knows its total size
# (grid_width, grid_height) and its current scroll position (scroll_x, scroll_y).
# It is responsible for rendering the visible portion of the grid.
# ----------------------------------------------------------------------
class AsciiAnimation(Widget):
    # These reactive attributes hold the state of the widget.
    # When they change, the widget will automatically re-render because of layout=True.
    scroll_x = reactive(0, layout=True)
    scroll_y = reactive(0, layout=True)
    grid_width = reactive(100)
    grid_height = reactive(50)

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        # The timer will drive the 10Hz update for the animation.
        self.animation_timer: Timer | None = None

    def on_mount(self) -> None:
        """Called when the widget is added to the app."""
        # Set up the timer to call update_animation() 10 times per second.
        self.animation_timer = self.set_interval(1 / 10, self.update_animation)

    def update_animation(self) -> None:
        """This is the function called by the timer to create motion."""
        # For a static checkerboard, we don't need to change anything here.
        # We just refresh the display at 10Hz.
        self.refresh()

    # ----------------------------------------------------------------------
    # Your Procedural Generation Function
    # ----------------------------------------------------------------------
    # This function is called by render() to get the ASCII characters for the
    # currently visible window of the grid.
    # ----------------------------------------------------------------------
    def generate_ascii_frame(self, x: int, y: int, width: int, height: int) -> list[str]:
        frame = []
        for row_idx in range(height):
            row_chars = []
            for col_idx in range(width):
                # Calculate the global coordinates in the large grid.
                global_x = x + col_idx
                global_y = y + row_idx
                # Example: a procedural checkerboard pattern.
                if (global_x // 5 + global_y // 5) % 2 == 0:
                    row_chars.append("#")
                else:
                    row_chars.append(".")
            frame.append("".join(row_chars))
        return frame

    # ----------------------------------------------------------------------
    # THE CRITICAL RENDER METHOD - THIS IS THE ONLY FIX NEEDED
    # ----------------------------------------------------------------------
    def render(self) -> str:
        """Renders the widget's content."""
        # Get the dimensions of the visible window.
        visible_width = self.size.width
        visible_height = self.size.height

        # Generate the ASCII data for the visible portion.
        visible_ascii_data = self.generate_ascii_frame(
            self.scroll_x, self.scroll_y, visible_width, visible_height
        )

        # Return ONLY the joined string of the ASCII art.
        # There are no other return statements or debug prints here.
        return "\n".join(visible_ascii_data)

# ----------------------------------------------------------------------
# 2. The Main Application Class
# ----------------------------------------------------------------------
# This class sets up the UI, including the ScrollView that contains
# your AsciiAnimation widget. It's responsible for linking the ScrollView's
# scroll position to the AsciiAnimation widget's state.
# ----------------------------------------------------------------------
class AsciiApp(App):
    BINDINGS = [
        ("q", "quit", "Quit"),
    ]

    CSS = """
    Screen {
        layout: vertical;
    }
    Header, Footer {
        height: auto;
    }
    #animation-scroll-view {
        height: 1fr; /* Make the ScrollView fill the available space */
    }
    AsciiAnimation {
        /* The content widget's size is determined by its content. */
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
        """Called when the app starts."""
        animation_widget = self.query_one("#ascii-animation", AsciiAnimation)
        scroll_view = self.query_one("#animation-scroll-view", ScrollView)

        # Tell the ScrollView the *total size* of its content.
        scroll_view.virtual_size = Size(animation_widget.grid_width, animation_widget.grid_height)

        # Link the ScrollView's scroll position to our animation widget.
        scroll_view.watch(
            scroll_view, "scroll_x", lambda x: setattr(animation_widget, "scroll_x", x)
        )
        scroll_view.watch(
            scroll_view, "scroll_y", lambda y: setattr(animation_widget, "scroll_y", y)
        )

        # Set the initial scroll position.
        animation_widget.scroll_x = scroll_view.scroll_x
        animation_widget.scroll_y = scroll_view.scroll_y


# ----------------------------------------------------------------------
# 3. Main Execution
# ----------------------------------------------------------------------
if __name__ == "__main__":
    app = AsciiApp()
    app.run()
