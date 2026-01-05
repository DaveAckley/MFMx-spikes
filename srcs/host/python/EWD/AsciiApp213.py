import time
from textual.app import App, ComposeResult
from textual.geometry import Size
from textual.reactive import reactive
from textual.scroll_view import ScrollView
from textual.timer import Timer
from textual.widgets import Header, Footer, Static  # <-- Import Static

# ----------------------------------------------------------------------
# 1. The Custom Animation Widget (CORRECTED BASE CLASS)
# ----------------------------------------------------------------------
# By inheriting from `Static` instead of `Widget`, we tell Textual that this
# widget's content is the string returned by its `render` method.
# ----------------------------------------------------------------------
class AsciiAnimation(Static): # <-- THE KEY FIX: Inherit from Static
    scroll_x = reactive(0, layout=True)
    scroll_y = reactive(0, layout=True)
    grid_width = reactive(100)
    grid_height = reactive(50)

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.animation_timer: Timer | None = None

    def on_mount(self) -> None:
        self.animation_timer = self.set_interval(1 / 10, self.update_animation)

    def update_animation(self) -> None:
        # When the timer calls this, self.refresh() will now correctly
        # update the displayed content of the Static widget.
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


# ----------------------------------------------------------------------
# 2. The Main Application Class (This part was correct)
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

        scroll_view.virtual_size = Size(animation_widget.grid_width, animation_widget.grid_height)

        scroll_view.watch(
            scroll_view, "scroll_x", lambda x: setattr(animation_widget, "scroll_x", x)
        )
        scroll_view.watch(
            scroll_view, "scroll_y", lambda y: setattr(animation_widget, "scroll_y", y)
        )

        animation_widget.scroll_x = scroll_view.scroll_x
        animation_widget.scroll_y = scroll_view.scroll_y


if __name__ == "__main__":
    app = AsciiApp()
    app.run()
