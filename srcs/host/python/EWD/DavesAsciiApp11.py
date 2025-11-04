# ascii_grid_app.py

from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical
from textual.geometry import Size
from textual.reactive import reactive
from textual.strip import Strip
from textual.renderables.blank import Blank
from textual.widget import Widget
from textual.widgets import Header, Footer, Button
from random import randrange

class AsciiAnimation(Widget):
    grid_width = reactive(100)
    grid_height = reactive(50)

    def __init__(self,id):
        super().__init__(id=id)
        self.atx = 0
        self.aty = 0

    def render(self) -> str:
        """
        Renders the entire visible content as a single string.
        """
        visible_height = self.size.height
        visible_width = self.size.width

        all_lines = ""
        for y in range(visible_height):
            global_y = self.aty + y
            line_text = []

            # Generate the characters for this specific line.
            for x in range(visible_width):
                global_x = self.atx + x
                if 0 <= global_x < self.grid_width and 0 <= global_y < self.grid_height:
                    # Checkerboard logic
                    if (global_x // 5 + global_y // 5) % 2 == 0:
                        str = "#@*&"
                        line_text.append(str[randrange(0,len(str))])
                    else:
                        line_text.append(".")
                else:
                    # Add spaces for areas outside the grid.
                    line_text.append(" ")

            full_line = "".join(line_text)
            all_lines = all_lines + full_line + "\n"

        return all_lines

# ----------------------------------------------------------------------
# 2. The Main Application Class
# ----------------------------------------------------------------------
class AsciiApp(App):
      
    CSS_PATH = "AsciiApp.tcss"

    BINDINGS = [
        ("q", "quit", "Quit"),
        ("up", "app.scrollGrid('up')", "Scroll up"),
        ("dn", "app.scrollGrid('dn')", "Scroll down"),
        ("lt", "app.scrollGrid('lt')", "Scroll left"),
        ("rt", "app.scrollGrid('rt')", "Scroll right")
    ]

    SCROLL_BUTTONS = {
        "nw" : ("⌜",+1,+1),
        "up" : ("↑", 0,+1),
        "ne" : ("⌝",-1,+1),
        "lf" : ("←",+1, 0),
        "ct" : ("·", 0, 0),
        "rt" : ("→",-1, 0),
        "sw" : ("⌞",+1,-1),
        "dn" : ("↓", 0,-1),
        "se" : ("⌟",-1,-1),
    }

    def compose(self) -> ComposeResult:
        print("COCOM",self.id)
        yield Header()
        with Horizontal(id="horiz"):
            with Vertical(id="leftvert"):
                for id,(label,dx,dy) in AsciiApp.SCROLL_BUTTONS.items():
                    yield Button(id=id,label=label,
                                 compact=True,
                                 action=f"app.scrollGrid('{id}')")
            with Vertical(id="ctrvert"):
                with Horizontal(id="centrhoriz"):
                    pass
                yield AsciiAnimation(id="ascii-animation")
        yield Footer()

    def action_scrollGrid(self,id):
        print("ASG",id)
        anim = self.query_one("#ascii-animation")

        label,dx,dy = AsciiApp.SCROLL_BUTTONS[id];
        if dx == 0 and dy == 0:
            pass # deal with centering
        else:
            anim.atx += dx
            anim.aty += dy
        anim.refresh()

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
