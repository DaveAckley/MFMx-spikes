#!/data/ackley/PART4/code/D/blackholeSpikes/venv/bin/python
from mfmx import MFMx
from time import sleep

from RTMPFeed10 import RTMPFeed

from io import StringIO
from rich.console import Console

from textual import on, work
from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical
from textual.geometry import Size
from textual.reactive import reactive
from textual.widget import Widget
from textual.widgets import Header, Footer, Button, Label
from textual.widgets import Checkbox
from textual.widgets import RadioSet, RadioButton
from textual.widgets import RichLog

def sign(num):
  if num < 0: return -1
  if num > 0: return 1
  return 0

def clearcb():
  MFMx.BHLog.clearLogCallback()

import atexit
atexit.register(clearcb)

import sys
argType = None
if len(sys.argv) == 2:
  argType = sys.argv[1]

# try to debug uncaught exceptions? --
def custom_exception_hook(type,value,tb):
  if False: # WAS: hasattr(sys,'ps1') or not sys.stderr.isatty():
    # For interactive mode or non-tty devices.
    sys.__excepthook__(type, value, tb)
  else:
    import traceback
    import pdb
    traceback.print_exception(type, value, tb)
    print()  # Print a newline for better readability.
    pdb.post_mortem(tb)

sys.excepthook = custom_exception_hook
                   
class AsciiAnimation(Widget):
    grid_width = reactive(1000)
    grid_height = reactive(500)

    def __init__(self,id):
        super().__init__(id=id)
        self.atx = 0
        self.aty = 0
        self.display_zoom = 0
        self.ewc = MFMx.EWControl.getEWControl()
        self.gridsize = self.ewc.getGridSize()
        self.refreshCount = 0
        grid_width = self.gridsize.x
        grid_height = self.gridsize.y

    def render(self) -> str:
        """
        Renders the entire visible content as a single string.
        """
        siz = MFMx.S32C(self.size.width,self.size.height)
        #pos = MFMx.S32C(int((2*self.atx-siz.x)/2),int((self.aty*2-siz.y)/2))
        pos = MFMx.S32C(int(self.atx),int(self.aty))
        zum = self.display_zoom
        ret = self.ewc.renderGridWindow(pos,siz,zum)
        self.poslabel.content = f"{pos} + {siz} @ {zum}";
        self.statslabel.content = self.ewc.statsLine()
        return ret

ewd = None

def logcb(key,text):
  global ewd
  if ewd:
    rid = ewd.query_one("#richlog")
    if rid:
      text = text.strip("\n")
      rid.write(f"{key}:{text}")

#class EWD(App[None]):
class EWD(App):
  def __init__(self):
    super().__init__()
    self.renderConsole = None
    self.theRTMPFeed = RTMPFeed(self,'ewd')

  CSS_PATH = "styles.tcss"

  GRID_ASPECT_RATIO = (2,1)

  BINDINGS = [
    ("q", "quit", "Quit"),
    ("+", "app.changeZoom('zoomin')", "Zoom in"),
    ("=", "app.changeZoom('resetzoom')", "Reset zoom"),
    ("-", "app.changeZoom('zoomout')", "Zoom out"),
    ("up", "app.scrollGrid('up')", "Scroll up"),
    ("down", "app.scrollGrid('dn')", "Scroll down"),
    (".", "app.scrollGrid('ct')", "Scroll center"),
    ("left", "app.scrollGrid('lt')", "Scroll left"),
    ("right", "app.scrollGrid('rt')", "Scroll right"),
  ]

  ZOOM_BUTTONS = {
    "zoomin" : ("+",+1),
    "resetzoom" : ("=", 0),
    "zoomout" : ("-",-1),
  }

  SCROLL_BUTTONS = {
    "nw" : ("⌜",+1,+1),
    "up" : ("↑", 0,+1),
    "ne" : ("⌝",-1,+1),
    "lt" : ("←",+1, 0),
    "ct" : ("·", 0, 0),
    "rt" : ("→",-1, 0),
    "sw" : ("⌞",+1,-1),
    "dn" : ("↓", 0,-1),
    "se" : ("⌟",-1,-1),
  }

  BHMIN = 1
  BHMAX = 3

  def run(self):
    self.bhs = [ MFMx.BlackHole(i) for i in range(self.BHMIN,self.BHMAX+1) ]
    self.ewc = MFMx.EWControl.getEWControl()
    print(self.ewc)
    MFMx.BHLog.setLogCallback(logcb)
    #print("CALMMSSLS")
    super().run()

  def compose(self) -> ComposeResult:
    global ewd
    ewd = self
    yield Header()
    with Horizontal(id="horiz"):
      with Vertical(id="leftvert"):
        with Vertical(id="runbuttons"):
          yield Checkbox(id="runcheck",label="run")
          yield Button(id="stepbutton",label="step",compact=True)
        with Vertical(id="scrollbuttons"):
          for id,(label,arg) in EWD.ZOOM_BUTTONS.items():
            b = Button(id=id,label=label,
                       compact=True,
                       action=f"app.changeZoom('{id}')")
            b.active_effect_duration=0.1
            yield b
          for id,(label,dx,dy) in EWD.SCROLL_BUTTONS.items():
            b = Button(id=id,label=label,
                       compact=True,
                       action=f"app.scrollGrid('{id}')")
            b.active_effect_duration=0.1
            yield b
        yield RichLog(id='richlog',max_lines=10000)
      with Vertical(id="ctrvert"):
        with Horizontal(id="centrhoriz"):
          yield Label("()",id="renderpos")
          yield Label("",id="statsline")
        yield AsciiAnimation(id="ascii-animation")
    yield Footer()

  def action_changeZoom(self,id):
    anim = self.query_one("#ascii-animation")
    label,dz = EWD.ZOOM_BUTTONS[id];
    if dz == 0:
      anim.display_zoom = 0
    else:
      anim.display_zoom += dz
    anim.refresh()

  def action_scrollGrid(self,id):
    anim = self.query_one("#ascii-animation")
    label,dx,dy = EWD.SCROLL_BUTTONS[id];
    inc = max(1,min(abs(anim.atx),abs(anim.aty),1-anim.display_zoom))
    if dx == 0 and dy == 0:
      anim.atx -= sign(anim.atx)*inc
      anim.aty -= sign(anim.aty)*inc
    else:
      anim.atx += dx*EWD.GRID_ASPECT_RATIO[0]*inc
      anim.aty += dy*EWD.GRID_ASPECT_RATIO[1]*inc
    anim.refresh()

  def update_animation_content(self):
    animation_widget = self.query_one("#ascii-animation", AsciiAnimation)
    animation_widget.refresh()
    animation_widget.refreshCount += 1
    self.doRTMPFrame()
    if False and animation_widget.refreshCount % 10 == 0:
      import gc
      gc.collect()
        
  async def on_mount(self) -> None:
    """
      Called when the app is ready. Sets up the virtual size and
      links the scroll position to the animation widget.
    """
    animation_widget = self.query_one("#ascii-animation", AsciiAnimation)
    animation_widget.poslabel = self.query_one("#renderpos", Label)
    animation_widget.statslabel = self.query_one("#statsline", Label)

    self.animation_timer = self.set_interval(1 / 10, self.update_animation_content, pause=False)
    self.runEvents()

  def reset(self):
    global argType
    for bh in self.bhs:
      bh.setMFMxCodePath("../../../../build_cross/bin/crossmain.bin")
      bh.close()
      if argType:
        bh.setStartDecayType(int(argType))
      bh.deployMFMxCode()

  def start(self):
    for bh in self.bhs:
      bh.startMFMxCode()

  def stop(self):
    for bh in self.bhs:
      bh.stopMFMxCode()

  def slowScan(self,count):
    for bh in self.bhs:
      bh.runSlowScans(count)

  @work(exclusive=True)
  async def runEvents(self):
    self.reset()
    self.start()
    print("STARTING EWPROC\n")
    self.ewc.setActive(False)
    if False:
      for i in range(1000):
        sleep(1)
        self.slowScan(140);
        print("STOPPING EWPROC\n")
    sleep(1)
    self.ewc.setActive(False)
    #self.stop()

  @on(Button.Pressed,"#stepbutton")
  def stepbutton_pressed(self,event):
    runch = self.query_one("#runcheck")
    runch.value = False       # stepping ends running
    # trigger step

  @on(Checkbox.Changed,"#runcheck")
  def runcheck_changed(self,event):
    id = event.checkbox.id
    self.ewc.setActive(event.value)
    print("OCC",id,event.value,self.ewc.isActive())

  def getRenderConsole(self):
    w,h = self.size
    if not self.renderConsole or w != self.renderConsole.width or h != self.renderConsole.height:
      self.renderConsole = Console(width = w,
                                   height= h,
                                   file=StringIO(),
                                   force_terminal=True,
                                   color_system="truecolor",
                                   record=True,
                                   legacy_windows=False,
                                   safe_box=False
                                   )
    return self.renderConsole
  
  def doRTMPFrame(self):
    rc = self.getRenderConsole()
    simplify = False
    screen_render = self.screen._compositor.render_update(
      full=True, screen_stack=self.app._background_screens, simplify=simplify
    )
    rc.print(screen_render)
    text = rc.export_text(clear=True,styles=False)
    #with open("/tmp/frame.txt","w",encoding="utf-8") as file:
    #  file.write(rc.export_text(clear=True,styles=False))
    self.theRTMPFeed.sendTextFrame(text)

if __name__ == "__main__":
    app = EWD()
    app.run()

# ctr=ewc.pickEWCenter()
# print(ctr)
# for t in range(0,(1<<9)+2,33): # couple bogus ones
#   a = ewc.makeAtom(t)
#   print(t,hex(t),a,a.isValid())

# for x in range(-8,8+1):
#   for y in range(-8,8+1):
#     c = MFMx.S32C(x,y)
#     a = ewc.getAtom(c)
#     if (a.getType() != 0):
#       print("WOORD",c,a)
# at = ewc.pickEWCenter()
# print("dkkdsout",at, ewc.getAtom(at))
# ewc.setAtom(at,ewc.makeAtom(0))
# print("Nonedkkdsout",at, ewc.getAtom(at))
# print("Noc",ewc.pickEWCenter())

