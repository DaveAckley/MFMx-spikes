from textual.app import App, ComposeResult
from textual.geometry import Size
from textual.reactive import reactive
from textual.scroll_view import ScrollView
from textual.strip import Strip, Segment
from textual.widget import Widget
from textual.widgets import Header, Footer

# ----------------------------------------------------------------------
# 1. The Custom Animation Widget (The Correct Way)
# ----------------------------------------------------------------------
# This widget inherits from the base Widget class and implements render_line.
# The render_line method is called by Textual for each visible line (y-coordinate).
# This is highly efficient as it only generates what is needed for display.
# ----------------------------------------------------------------------
# ----------------------------------------------------------------------
# 1. The Custom Animation Widget (Corrected for Textual 6.2.1)
# ----------------------------------------------------------------------
class AsciiAnimation(Widget):
    scroll_x = reactive(0, layout=True)
    scroll_y = reactive(0, layout=True)
    grid_width = reactive(100)
    grid_height = reactive(50)

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.animation_timer: Timer | None = None
        self._is_setup = False

    def on_resize(self) -> None:
        if self._is_setup:
            return
        self._is_setup = True
        self.setup_animation()

        # Define the total grid size once and use it consistently
        self.grid_width = 400
        self.grid_height = 200
        self.virtual_size = Size(self.grid_width, self.grid_height)

        self.animation_timer = self.set_interval(1 / 10, self.update_animation_content)

        #scroll_view.watch(scroll_view, "scroll_x", lambda x: self.update_animation_content())
        #scroll_view.watch(scroll_view, "scroll_y", lambda y: self.update_animation_content())
        self.update_animation_content()


    def render_line(self, y: int) -> Strip:
        """
        Called by Textual to render a specific line 'y' of the widget.
        This version is compatible with Textual 6.2.1.
        """
        # Calculate the global y-coordinate on our large grid.
        global_y = self.scroll_y + y

        # If the line is outside our grid's bounds, return a blank line.
        # The correct way to create a blank strip is to provide the text and a style.
        if not (0 <= global_y < self.grid_height):
            return Strip([(" " * self.size.width, None, False)])

        # Generate the characters for this visible line.
        line_text = []
        for x in range(self.size.width):
            global_x = self.scroll_x + x

            if 0 <= global_x < self.grid_width:
                if (global_x // 5 + global_y // 5) % 2 == 0:
                    line_text.append("#")
                else:
                    line_text.append(".")
            else:
                line_text.append(" ")

        # **THE FIX FOR TEXTUAL 6.2.1:**
        # Instantiate Strip directly with the text and the widget's style.
        full_line = "".join(line_text)
        return Strip([(full_line,None,False)])

# ----------------------------------------------------------------------
# 2. The Main Application Class (Now much simpler)
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
        height: 1fr;
    }
    AsciiAnimation {
        /* The widget's size is determined by its virtual size, not CSS. */
        width: auto;
        height: auto;
    }
    """

    def compose(self) -> ComposeResult:
        yield Header()
        with ScrollView(id="animation-scroll-view"):
            # We now use our custom widget.
            yield AsciiAnimation(id="ascii-animation")
        yield Footer()

    def on_mount(self) -> None:
        animation_widget = self.query_one("#ascii-animation", AsciiAnimation)
        scroll_view = self.query_one("#animation-scroll-view", ScrollView)

        # Tell the ScrollView the total size of its content.
        #scroll_view.virtual_size = Size(animation_widget.grid_width, animation_widget.grid_height)
        animation_widget.virtual_size = Size(animation_widget.grid_width, animation_widget.grid_height)

        # Link the ScrollView's scroll position to our animation widget's state.
        # When the ScrollView scrolls, it updates the animation_widget's scroll_x/y,
        # which automatically triggers a re-render via render_line.
        scroll_view.watch(scroll_view, "scroll_x", lambda x: setattr(animation_widget, "scroll_x", x))
        scroll_view.watch(scroll_view, "scroll_y", lambda y: setattr(animation_widget, "scroll_y", y))

        # We don't need a timer for a static checkerboard.
        # The rendering is handled automatically when scroll_x/y change.
        # If you wanted to animate the checkerboard, you would start a timer
        # here that calls animation_widget.refresh().

if __name__ == "__main__":
    app = AsciiApp()
    app.run()
