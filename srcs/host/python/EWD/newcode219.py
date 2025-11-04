# ----------------------------------------------------------------------
# 1. The Custom Animation Widget (Corrected for Textual 6.2.1)
# ----------------------------------------------------------------------
class AsciiAnimation(Widget):
    scroll_x = reactive(0, layout=True)
    scroll_y = reactive(0, layout=True)
    grid_width = reactive(100)
    grid_height = reactive(50)

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
            return Strip(" " * self.size.width, style=self.rich_style)

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
        return Strip(full_line, style=self.rich_style)
