import time
from textual.app import App, ComposeResult
from textual.geometry import Size
from textual.scroll_view import ScrollView
from textual.timer import Timer
from textual.widgets import Header, Footer, Static

import random

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
        /* Make the Static widget fill the ScrollView's viewport */
        width: 100%;
        height: 100%;
    }
    """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.animation_timer: Timer | None = None
        self._is_setup = False

    def compose(self) -> ComposeResult:
        yield Header()
        with ScrollView(id="animation-scroll-view"):
            yield Static(id="ascii-animation-static")
        yield Footer()

    def on_resize(self) -> None:
        if self._is_setup:
            return
        self._is_setup = True
        self.setup_animation()

    def on_mount(self) -> None:
        static_widget = self.query_one("#ascii-animation-static", Static)
        scroll_view = self.query_one("#animation-scroll-view", ScrollView)

        # Define the total grid size once and use it consistently
        self.grid_width = 400
        self.grid_height = 200
        scroll_view.virtual_size = Size(self.grid_width, self.grid_height)

        self.animation_timer = self.set_interval(1 / 10, self.update_animation_content)

        #scroll_view.watch(scroll_view, "scroll_x", lambda x: self.update_animation_content())
        #scroll_view.watch(scroll_view, "scroll_y", lambda y: self.update_animation_content())

        #self.update_animation_content()

    def generate_ascii_frame(self, x: int, y: int, width: int, height: int, grid_width: int, grid_height: int) -> list[str]:
        """Your procedural generation function, now with boundary checks."""
        frame = []
        for row_idx in range(height):
            row_chars = []
            for col_idx in range(width):
                global_x = x + col_idx
                global_y = y + row_idx

                # Boundary check: only draw if the global coordinates are within the grid
                if 0 <= global_x < grid_width and 0 <= global_y < grid_height:
                    if (global_x // 5 + global_y // 5) % 2 == 0:
                        str = "#*@"
                        row_chars.append(str[random.randrange(0,len(str))])
                    else:
                        row_chars.append(".")
                else:
                    # If outside the grid, draw a space.
                    row_chars.append("-")
            frame.append("".join(row_chars))
        print(f"ZONGO<{frame}>")
        return frame

    # THIS METHOD IS CORRECTLY PLACED INSIDE THE CLASS
    def update_animation_content(self) -> None:
        """Generates the ASCII frame and updates the Static widget."""
        static_widget = self.query_one("#ascii-animation-static", Static)
        scroll_view = self.query_one("#animation-scroll-view", ScrollView)

        scroll_x = int(scroll_view.scroll_x)
        scroll_y = int(scroll_view.scroll_y)

        visible_width = static_widget.size.width
        visible_height = static_widget.size.height

        # Use the instance variables for grid size
        visible_ascii_data = self.generate_ascii_frame(
            scroll_x, scroll_y, visible_width, visible_height, self.grid_width, self.grid_height
        )

        static_widget.update("\n".join(visible_ascii_data))

if __name__ == "__main__":
    app = AsciiApp()
    app.run()
