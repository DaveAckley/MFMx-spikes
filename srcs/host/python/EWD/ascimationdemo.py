from textual.app import App, ComposeResult
from textual.widgets import Header, Footer, Scrollbar
from textual.containers import Container
from textual.widget import Widget
from textual.reactive import reactive
from textual.timer import Timer
import time

# Define your custom animation widget
class AsciiAnimation(Widget):
    # Reactive attributes for scroll position and grid size
    scroll_x = reactive(0)
    scroll_y = reactive(0)
    grid_width = reactive(100)  # Example large grid width
    grid_height = reactive(50)  # Example large grid height

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.animation_timer: Timer | None = None

    def on_mount(self) -> None:
        # Start the animation timer when the widget is mounted
        self.animation_timer = self.set_interval(1 / 10, self.update_animation) # 10Hz update

    def update_animation(self) -> None:
        # This method will be called at 10Hz
        # Get the visible portion coordinates
        visible_width = self.size.width
        visible_height = self.size.height

        # Call your procedural generation function
        visible_ascii_data = self.generate_ascii_frame(
            self.scroll_x, self.scroll_y, visible_width, visible_height
        )

        # Update the widget's display with the new ASCII data
        self.update_display(visible_ascii_data)

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

    def update_display(self, ascii_data: list[str]) -> None:
        """
        This method will render the ASCII data to the widget.
        """
        # Textual widgets have a render method that can be overridden,
        # or you can directly manipulate the content.
        # For simplicity, we'll just print to the console for now,
        # but in a real Textual app, you'd update the widget's content
        # by overriding `render` or using a `Static` widget.
        # For a custom widget, you'd typically override the `render` method
        # and use `self.app.console.print` or similar.
        self.update() # Request a re-render of the widget

    def render(self) -> str:
        """
        Textual calls this method to get the content to display for the widget.
        """
        visible_width = self.size.width
        visible_height = self.size.height

        visible_ascii_data = self.generate_ascii_frame(
            self.scroll_x, self.scroll_y, visible_width, visible_height
        )
        return "\n".join(visible_ascii_data)

    # Handlers for scrollbar changes (to be implemented with actual scrollbars)
    def watch_scroll_x(self, new_x: int) -> None:
        self.refresh() # Re-render when scroll_x changes

    def watch_scroll_y(self, new_y: int) -> None:
        self.refresh() # Re-render when scroll_y changes

class AsciiApp(App):
    BINDINGS = [
        ("q", "quit", "Quit"),
    ]

    CSS = """
    Screen {
        layout: grid;
        grid-size: 2;
        grid-columns: 1fr auto;
        grid-rows: 1fr auto;
    }
    #animation-container {
        grid-column-span: 1;
        grid-row-span: 1;
        border: solid green;
        overflow: hidden; /* Crucial for clipping the animation */
    }
    AsciiAnimation {
        width: auto;
        height: auto;
    }
    #vertical-scrollbar {
        grid-column-start: 2;
        grid-row-start: 1;
    }
    #horizontal-scrollbar {
        grid-column-start: 1;
        grid-row-start: 2;
    }
    """

    def compose(self) -> ComposeResult:
        yield Header()
        with Container(id="animation-container"):
            yield AsciiAnimation(id="ascii-animation")
        yield Scrollbar(id="vertical-scrollbar", orientation="vertical")
        yield Scrollbar(id="horizontal-scrollbar", orientation="horizontal")
        yield Footer()

    def on_mount(self) -> None:
        animation_widget = self.query_one("#ascii-animation", AsciiAnimation)
        vertical_scrollbar = self.query_one("#vertical-scrollbar", Scrollbar)
        horizontal_scrollbar = self.query_one("#horizontal-scrollbar", Scrollbar)

        # Link scrollbars to the animation widget's scroll positions
        # The range of the scrollbar should reflect the difference between
        # the total grid size and the visible window size.

        # For vertical scrollbar
        vertical_scrollbar.set_range(0, animation_widget.grid_height - animation_widget.size.height)
        vertical_scrollbar.set_page_size(animation_widget.size.height)
        vertical_scrollbar.set_value(animation_widget.scroll_y)

        # For horizontal scrollbar
        horizontal_scrollbar.set_range(0, animation_widget.grid_width - animation_widget.size.width)
        horizontal_scrollbar.set_page_size(animation_widget.size.width)
        horizontal_scrollbar.set_value(animation_widget.scroll_x)

    def on_scrollbar_changed(self, event: Scrollbar.Changed) -> None:
        animation_widget = self.query_one("#ascii-animation", AsciiAnimation)
        if event.scrollbar.id == "vertical-scrollbar":
            animation_widget.scroll_y = int(event.value)
        elif event.scrollbar.id == "horizontal-scrollbar":
            animation_widget.scroll_x = int(event.value)

if __name__ == "__main__":
    app = AsciiApp()
    app.run()
