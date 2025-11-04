# Source - https://stackoverflow.com/a
# Posted by Matt J, modified by community. See post 'Timeline' for change history
# Retrieved 2025-11-24, License - CC BY-SA 4.0

#!/usr/bin/env python
import signal
import sys

def signal_handler(sig, frame):
    print('You WINCHED!')
    sys.exit(0)

signal.signal(signal.SIGWINCH, signal_handler)
signal.pause()
