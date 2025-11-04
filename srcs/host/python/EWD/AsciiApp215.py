import time
from textual.app import App, ComposeResult
from textual.geometry import Size
from textual.scroll_view import ScrollView
from textual.timer import Timer
from textual.widgets import Header, Footer, Static

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
        height: 1fr;
    }
    #ascii-animation-static {
        width: auto;
        height: auto;
    }
    """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.animation_timer: Timer | None = None
        self._is_setup = False # Flag to ensure setup only runs once

    def compose(self) -> ComposeResult:
        yield Header()
        with ScrollView(id="animation-scroll-view"):
            yield Static(id="ascii-animation-static")
        yield Footer()

    def on_resize(self) -> None:
        """Called when the app is resized. We use this to know widgets are ready."""
        if self._is_setup:
            return

        # The widgets are now laid out and have a size. We can proceed.
        self._is_setup = True
        self.setup_animation()

    def setup_animation(self) -> None:
        """Sets up the timer and scroll watchers."""
        static_widget = self.query_one("#ascii-animation-static", Static)
        scroll_view = self.query_one("#animation-scroll-view", ScrollView)

        grid_width = 100
        grid_height = 50
        scroll_view.virtual_size = Size(grid_width, grid_height)

        # Start the 10Hz animation timer.
        self.animation_timer = self.set_interval(1 / 10, self.update_animation_content)

        # Watch the ScrollView's scroll position. When it changes, update the content.
        scroll_view.watch(scroll_view, "scroll_x", lambda x: self.update_animation_content())
        scroll_view.watch(scroll_view, "scroll_y", lambda y: self.update_animation_content())

        # Initial content update.
        self.update_animation_content()

    def generate_ascii_frame(self, x: int, y: int, width: int, height: int) -> list[str]:
        """Your procedural generation function."""
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

    def update_animation_content(self) -> None:
        """Generates the ASCII frame and updates the Static widget."""
        static_widget = self.query_one("#ascii-animation-static", Static)
        scroll_view = self.query_one("#animation-scroll-view", ScrollView")

        scroll_x = scroll_view.scroll_x
        scroll_y = scroll_view.scroll_y

        visible_width = static_widget.size.width
        visible_height = static_widget.size.height

        # Add a print to verify this method is now being called.
        print(f"Updating content at ({scroll_x}, {scroll_y}) with size ({visible_width}, {visible_height})")

        visible_ascii_data = self.generate_ascii_frame(
            scroll_x, scroll_y, visible_width, visible_height
        )

        static_widget.update("\n".join(visible_ascii_data))

if __name__ == "__main__":
    app = AsciiApp()
    app.run()
