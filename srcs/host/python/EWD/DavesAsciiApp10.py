# ascii_grid_app.py

from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical
from textual.geometry import Size
from textual.reactive import reactive
from textual.scroll_view import ScrollView
from textual.strip import Strip
from textual.renderables.blank import Blank
from textual.widget import Widget
from textual.widgets import Header, Footer, Button
from rich.console import Group  # Required to wrap the list of Strips
from random import randrange

# ----------------------------------------------------------------------
# 1. The Custom Animation Widget
# ----------------------------------------------------------------------
# This widget uses the standard `render` method to return a single Rich
# `Group` object containing a list of Strip objects. This is a stable
# approach for Textual 6.2.1, which expects a single renderable.
# ----------------------------------------------------------------------
class AsciiAnimation(Widget):
    # These reactive attributes update the widget when they change.
    # `layout=True` ensures a re-render is triggered.
    scroll_x = reactive(0, layout=True)
    scroll_y = reactive(0, layout=True)
    grid_width = reactive(100)
    grid_height = reactive(50)

    def render(self) -> Group:
        """
        Renders the entire visible content as a single Rich `Group` object.
        This is compatible with Textual 6.2.1, which expects a single
        renderable from the `render` method.
        """
        strips = []
        visible_height = self.size.height
        visible_width = self.size.width

        all_lines = ""
        for y in range(visible_height):
            global_y = self.scroll_y + y
            line_text = []

            # Generate the characters for this specific line.
            for x in range(visible_width):
                global_x = self.scroll_x + x
                if 0 <= global_x < self.grid_width and 0 <= global_y < self.grid_height:
                    # Checkerboard logic
                    if (global_x // 5 + global_y // 5) % 2 == 0:
                        str = "#@*&"
                        line_text.append(str[randrange(0,len(str))])
                    else:
                        line_text.append(".")
                else:
                    # Add spaces for areas outside the grid.
                    line_text.append("-")

            full_line = "".join(line_text)
            all_lines = all_lines + full_line + "\n"

        # **THE FIX:** Wrap the list of Strip objects in a Rich `Group`.
        # A Group is a single renderable that contains other renderables.
        #print("ZOOONG",strips)
        #return Group(*strips)
        #return Blank(color='red')
        return all_lines

# ----------------------------------------------------------------------
# 2. The Main Application Class
# ----------------------------------------------------------------------
class AsciiApp(App):

    CSS_PATH = "AsciiApp.tcss"

    BINDINGS = [
        ("q", "quit", "Quit"),
    ]

    def compose(self) -> ComposeResult:
        yield Header()
        with Horizontal(id="horiz"):
            with Vertical(id="leftvert"):
                yield Button(id="up")
                yield Button(id="down")
            with Vertical(id="ctrvert"):
                with Horizontal(id="centrhoriz"):
                    yield Button(id="left")
                    yield Button(id="right")
                yield AsciiAnimation(id="ascii-animation")
        yield Footer()

    def update_animation_content(self):
        animation_widget = self.query_one("#ascii-animation", AsciiAnimation)
        animation_widget.refresh()
        
    def on_mount(self) -> None:
        """
        Called when the app is ready. Sets up the virtual size and
        links the scroll position to the animation widget.
        """
        animation_widget = self.query_one("#ascii-animation", AsciiAnimation)
        #scroll_view = self.query_one("#animation-scroll-view", ScrollView)

        # Set the total size of the scrollable area.
        #scroll_view.virtual_size = Size(animation_widget.grid_width, animation_widget.grid_height)

        self.animation_timer = self.set_interval(1 / 10, self.update_animation_content, pause=False)

        # Watch the ScrollView's scroll position and update the widget's
        # reactive attributes, which will trigger a re-render.
        #scroll_view.watch(scroll_view, "scroll_x", lambda x: setattr(animation_widget, "scroll_x", x))
        #scroll_view.watch(scroll_view, "scroll_y", lambda y: setattr(animation_widget, "scroll_y", y))

# ----------------------------------------------------------------------
# 3. Main Execution
# ----------------------------------------------------------------------
if __name__ == "__main__":
    app = AsciiApp()
    app.run()
