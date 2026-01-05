# ascii_grid_static.py

from textual.app import App, ComposeResult
from textual.geometry import Size
from textual.reactive import reactive
from textual.scroll_view import ScrollView
from textual.widgets import Static, Header, Footer

# ----------------------------------------------------------------------
# 1. Helper Function to Generate the Visible ASCII Frame
# ----------------------------------------------------------------------
def generate_ascii_frame(scroll_x: int, scroll_y: int, width: int, height: int, grid_width: int, grid_height: int) -> str:
    """
    Generates a string of ASCII characters for the currently visible portion
    of the grid. This function is simple, pure Python and easy to debug.
    """
    lines = []
    for y in range(height):
        global_y = scroll_y + y
        line_chars = []
        for x in range(width):
            global_x = scroll_x + x

            # Check if the current cell is within the bounds of our large grid
            if 0 <= global_x < grid_width and 0 <= global_y < grid_height:
                if (global_x // 5 + global_y // 5) % 2 == 0:
                    line_chars.append("#")
                else:
                    line_chars.append(".")
            else:
                line_chars.append(" ")

        lines.append("".join(line_chars))

    return "\n".join(lines)

# ----------------------------------------------------------------------
# 2. The Main Application Class
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
        height: 1fr; /* Takes up all available vertical space */
    }
    #ascii-display {
        /* This is the key: make the Static widget fill the ScrollView's viewport */
        width: 100%;
        height: 100%;
    }
    """

    def compose(self) -> ComposeResult:
        yield Header()
        with ScrollView(id="animation-scroll-view"):
            # We use a simple Static widget to display the generated text.
            yield Static(id="ascii-display")
        yield Footer()

    def on_mount(self) -> None:
        """
        Called when the app is ready. Sets up the virtual size and
        a timer to update the display.
        """
        static_widget = self.query_one("#ascii-display", Static)
        scroll_view = self.query_one("#animation-scroll-view", ScrollView)

        # Set the total size of the scrollable area.
        scroll_view.virtual_size = Size(100, 50)

        # Set an initial content string
        static_widget.update(generate_ascii_frame(
            scroll_x=0, scroll_y=0,
            width=static_widget.size.width, height=static_widget.size.height,
            grid_width=100, grid_height=50
        ))

        # Set a timer to update the display at 10Hz
        self.set_interval(0.1, self.update_display, pause=True)

    def on_resize(self) -> None:
        """
        Called when the app or a widget is resized. We use this to
        start our timer once the widgets have a proper size.
        """
        static_widget = self.query_one("#ascii-display", Static)
        if static_widget.size.width > 0 and static_widget.size.height > 0:
            # Start the timer if it's paused
            self.set_interval(0.1, self.update_display, pause=False)

    def update_display(self) -> None:
        """
        This function is called by the timer. It gets the current scroll
        position and updates the Static widget with the new ASCII frame.
        """
        static_widget = self.query_one("#ascii-display", Static)
        scroll_view = self.query_one("#animation-scroll-view", ScrollView)

        # Generate the ASCII for the current viewport and update the widget.
        ascii_frame = generate_ascii_frame(
            scroll_x=scroll_view.scroll_x,
            scroll_y=scroll_view.scroll_y,
            width=static_widget.size.width,
            height=static_widget.size.height,
            grid_width=100,
            grid_height=50
        )
        static_widget.update(ascii_frame)

# ----------------------------------------------------------------------
# 3. Main Execution
# ----------------------------------------------------------------------
if __name__ == "__main__":
    app = AsciiApp()
    app.run()
