# ascii_grid_app.py

from textual.app import App, ComposeResult
from textual.geometry import Size
from textual.reactive import reactive
from textual.scroll_view import ScrollView
from textual.strip import Strip
from textual.widget import Widget
from textual.widgets import Header, Footer

# ----------------------------------------------------------------------
# 1. The Custom Animation Widget
# ----------------------------------------------------------------------
# This widget uses the standard `render` method to return a list of Strip
# objects, one for each line. This is a stable approach for Textual 6.2.1.
# ----------------------------------------------------------------------
class AsciiAnimation(Widget):
    # These reactive attributes update the widget when they change.
    # `layout=True` ensures a re-render is triggered.
    scroll_x = reactive(0, layout=True)
    scroll_y = reactive(0, layout=True)
    grid_width = reactive(100)
    grid_height = reactive(50)

    def render(self) -> list[Strip]:
        """
        Renders the entire visible content as a list of Strip objects.
        This is called whenever the widget needs to be redrawn.
        """
        strips = []
        visible_height = self.size.height
        visible_width = self.size.width

        # Generate a Strip object for every visible line in the viewport.
        for y in range(visible_height):
            global_y = self.scroll_y + y
            line_text = []

            # Generate the characters for this specific line.
            for x in range(visible_width):
                global_x = self.scroll_x + x
                if 0 <= global_x < self.grid_width and 0 <= global_y < self.grid_height:
                    # Checkerboard logic
                    if (global_x // 5 + global_y // 5) % 2 == 0:
                        line_text.append("#")
                    else:
                        line_text.append(".")
                else:
                    # Add spaces for areas outside the grid.
                    line_text.append(" ")

            full_line = "".join(line_text)
            # Create a Strip for the generated line. The tuple format
            # (text, style, control) is compatible with Textual 6.2.1.
            strips.append(Strip([(full_line, None, False)]))

        return strips

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
    AsciiAnimation {
        /* The widget's size is determined by its virtual size. */
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
        """
        Called when the app is ready. Sets up the virtual size and
        links the scroll position to the animation widget.
        """
        animation_widget = self.query_one("#ascii-animation", AsciiAnimation)
        scroll_view = self.query_one("#animation-scroll-view", ScrollView)

        # Set the total size of the scrollable area.
        scroll_view.virtual_size = Size(animation_widget.grid_width, animation_widget.grid_height)

        # Watch the ScrollView's scroll position and update the widget's
        # reactive attributes, which will trigger a re-render.
        scroll_view.watch(scroll_view, "scroll_x", lambda x: setattr(animation_widget, "scroll_x", x))
        scroll_view.watch(scroll_view, "scroll_y", lambda y: setattr(animation_widget, "scroll_y", y))

# ----------------------------------------------------------------------
# 3. Main Execution
# ----------------------------------------------------------------------
if __name__ == "__main__":
    app = AsciiApp()
    app.run()
