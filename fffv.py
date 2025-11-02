from textual.app import App, ComposeResult
from textual.widget import Widget
from textual.widgets import Label, RichLog
from textual.containers import Horizontal
from textual.reactive import reactive
from textual.message import Message
import time

# --- Custom RichLog to manage its own selected MySource ---
class SelectedRichLog(RichLog):
    selected_source: reactive["MySource | None"] = reactive(None)

    def __init__(self, id: str | None = None):
            super().__init__(id=id)

    def watch_selected_source(self, old_source: "MySource | None", new_source: "MySource | None") -> None:
        """
        When the selected_source for *this* log changes, update the UI.
        """
        self.log(f"GOTSELSOU {self} old {old_source} new {new_source}")
        # Deselect old source visually
        if old_source:
            old_source.remove_class("selected")
            old_source.add_class("unselected")

        # Select new source visually and update log content
        if new_source:
            new_source.remove_class("unselected")
            self.clear()
            self.write(new_source.buffer)
            new_source.add_class("selected")
        else:
            self.clear() # Clear if no source is selected

# --- MySource with target_log_id ---
class MySource(Label):
    buffer = reactive("")
    target_log_id: str

    def __init__(self, content: str, target_log_id: str, id: str | None = None):
        super().__init__(content, id=id)
        self.buffer = content
        self.target_log_id = target_log_id
        self.add_class("unselected")

    class Selected(Message):
        """A custom message to send when a MySource is selected."""
        def __init__(self, buffer_content: str, target_log_id: str) -> None:
            super().__init__()
            self.buffer_content = buffer_content
            self.target_log_id = target_log_id
            self.sender: Widget

    def appendToBuffer(self,new_text:str) -> None:
        self.buffer = self.buffer + new_text

    def on_click(self) -> None:
        message = self.Selected(self.buffer, self.target_log_id)
        message.sender = self
        # Post the message, including the target_log_id
        self.post_message(message)

    def watch_buffer(self, old_buffer: str, new_buffer: str) -> None:
        """
        Watch for changes in the buffer.
        Only post a message if this MySource is currently selected for its target log.
        """
        self.update(self.content) # Always update the MySource's own label content
        self.buffer = new_buffer
        
        # Check if this MySource is the currently selected source for its target log
        try:
            # Query the App to find the target SelectedRichLog
            # The App is the common ancestor that knows about all logs
            app = self.app
            target_log = app.query_one(f"#{self.target_log_id}", SelectedRichLog)

            if target_log.selected_source is self:
                # If this MySource is indeed the selected one, then post the message
                # This will trigger the on_my_source_selected in the App,
                # which will then update the target_log's content.
                message = self.Selected(new_buffer, self.target_log_id)
                message.sender = self
                self.log(f"posting clam {message} from watch_buffer")
                self.post_message(message)
        except Exception:
            # Handle cases where the target log might not be found yet (e.g., during startup)
            self.log(f"FAILED from watch_buffer")


# --- Main App ---
class MyApp(App[None]):
    CSS_PATH = "styles.tcss"

    def compose(self) -> ComposeResult:
        yield Horizontal(
            MySource("Source 1 for Log 1", target_log_id="richlog1", id="mysource1a"),
            MySource("Source 2 for Log 1", target_log_id="richlog1", id="mysource1b"),
            MySource("Source 3 for Log 2", target_log_id="richlog2", id="mysource2a"),
            MySource("Source 4 for Log 2", target_log_id="richlog2", id="mysource2b"),
        )
        yield SelectedRichLog(id="richlog1")
        yield SelectedRichLog(id="richlog2")

    def on_my_source_selected(self, message: MySource.Selected) -> None:
        """
        Handle the custom message from MySource.
        This method acts as a central router to the correct SelectedRichLog.
        """
        try:
            target_log = self.query_one(f"#{message.target_log_id}", SelectedRichLog)

            # Check if the selected source is changing

            if target_log.selected_source is not message.sender:
                # If a new source is selected, update the reactive attribute.
                # This will trigger watch_selected_source for visual updates.
                target_log.selected_source = message.sender
            else:
                # If the same source is already selected, its buffer changed.
                # We need to explicitly update the RichLog content.
                pass # The watcher will handle visual, but we need to update content here

            # Always update the RichLog content with the latest buffer from the message
            target_log.clear()
            target_log.write(message.buffer_content)
            self.log(f"updating zong {target_log} {target_log.selected_source} {message.sender}")

        except Exception as e:
            self.log(f"Error finding target log {message.target_log_id}: {e}")

    async def on_mount(self) -> None:
        # Example of changing buffers after a delay
        mysource1b = self.query_one("#mysource1b", MySource)
        mysource2a = self.query_one("#mysource2a", MySource)
        self.set_interval(7, lambda: self.update_source_buffer(mysource1b, "Log 1"),
                          repeat=0)
        self.set_interval(9, lambda: self.update_source_buffer(mysource2a, "Log 2"),
                          repeat=0)

    def update_source_buffer(self, source: MySource, log_name: str) -> None:
        self.log(f"GOING TO APPEND TO {source}")
        source.appendToBuffer(f"\nUpdated content for {log_name} at {time.strftime('%H:%M:%S')}")
        self.log(f"APPENDED TO {source}")

if __name__ == "__main__":
    app = MyApp()
    app.run()
    
