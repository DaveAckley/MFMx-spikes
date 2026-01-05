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
                    row_chars.append("#")
                else:
                    row_chars.append(".")
            else:
                # If outside the grid, you can draw a space, a different character, or nothing.
                # Let's draw a space to make the grid's edge obvious.
                row_chars.append(" ")
        frame.append("".join(row_chars))
    return frame

def update_animation_content(self) -> None:
    """Generates the ASCII frame and updates the Static widget."""
    static_widget = self.query_one("#ascii-animation-static", Static)
    scroll_view = self.query_one("#animation-scroll-view", ScrollView)

    scroll_x = int(scroll_view.scroll_x)
    scroll_y = int(scroll_view.scroll_y)

    visible_width = static_widget.size.width
    visible_height = static_widget.size.height

    # Define the total grid size here, so it's consistent with setup_animation
    grid_width = 100
    grid_height = 50

    # Pass the total grid size to the generator function
    visible_ascii_data = self.generate_ascii_frame(
        scroll_x, scroll_y, visible_width, visible_height, grid_width, grid_height
    )

    static_widget.update("\n".join(visible_ascii_data))
