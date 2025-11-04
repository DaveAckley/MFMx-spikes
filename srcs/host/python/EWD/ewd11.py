#!/data/ackley/PART4/code/D/blackholeSpikes/venv/bin/python
from mfmx import MFMx
from time import sleep

from textual import on, work
from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical
from textual.geometry import Size
from textual.reactive import reactive
from textual.widget import Widget
from textual.widgets import Header, Footer, Button, Label
from textual.widgets import Checkbox
from textual.widgets import RadioSet, RadioButton

def sign(num):
  if num < 0: return -1
  if num > 0: return 1
  return 0

def logcb(key,text):
  # print(f"{key}:<{text}>",end='')
  pass

def clearcb():
  MFMx.BHLog.clearLogCallback()

import atexit
atexit.register(clearcb)

import sys
argType = None
if len(sys.argv) == 2:
  argType = sys.argv[1]

class AsciiAnimation(Widget):
    grid_width = reactive(1000)
    grid_height = reactive(500)

    def __init__(self,id):
        super().__init__(id=id)
        self.atx = 0
        self.aty = 0
        self.ewc = MFMx.EWControl.getEWControl()
        self.gridsize = self.ewc.getGridSize()
        grid_width = self.gridsize.x
        grid_height = self.gridsize.y

    def render(self) -> str:
        """
        Renders the entire visible content as a single string.
        """
        siz = MFMx.S32C(self.size.width,self.size.height)
        pos = MFMx.S32C(int((2*self.atx-siz.x)/2),int((self.aty*2-siz.y)/2))
        ret = self.ewc.renderGridWindow(pos,siz)
        self.label.content = f"{pos} + {siz}";
        print("GOTSRENDER")
        return ret

#class EWD(App[None]):
class EWD(App):
  CSS_PATH = "styles.tcss"

  GRID_ASPECT_RATIO = (2,1)

  BINDINGS = [
    ("q", "quit", "Quit"),
    ("up", "app.scrollGrid('up')", "Scroll up"),
    ("down", "app.scrollGrid('dn')", "Scroll down"),
    (".", "app.scrollGrid('ct')", "Scroll center"),
    ("left", "app.scrollGrid('lt')", "Scroll left"),
    ("right", "app.scrollGrid('rt')", "Scroll right")
  ]

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

  BHMIN = 3
  BHMAX = 3

  def run(self):
    self.bhs = [ MFMx.BlackHole(i) for i in range(self.BHMIN,self.BHMAX+1) ]
    self.ewc = MFMx.EWControl.getEWControl()
    print(self.ewc)
    MFMx.BHLog.setLogCallback(logcb)
    #print("CALMMSSLS")
    super().run()

  def compose(self) -> ComposeResult:
    yield Header()
    with Horizontal(id="horiz"):
      with Vertical(id="leftvert"):
        with Vertical(id="runbuttons"):
          yield Checkbox(id="runcheck",label="run")
          yield Button(id="stepbutton",label="step",compact=True)
        with Vertical(id="scrollbuttons"):
          for id,(label,dx,dy) in EWD.SCROLL_BUTTONS.items():
            b = Button(id=id,label=label,
                       compact=True,
                       action=f"app.scrollGrid('{id}')")
            b.active_effect_duration=0.1
            yield b
      with Vertical(id="ctrvert"):
        with Horizontal(id="centrhoriz"):
          yield Label("()",id="renderpos")
        yield AsciiAnimation(id="ascii-animation")
    yield Footer()

  def action_scrollGrid(self,id):
    anim = self.query_one("#ascii-animation")
    label,dx,dy = EWD.SCROLL_BUTTONS[id];
    if dx == 0 and dy == 0:
      anim.atx -= sign(anim.atx)
      anim.aty -= sign(anim.aty)
    else:
      anim.atx += dx*EWD.GRID_ASPECT_RATIO[0]
      anim.aty += dy*EWD.GRID_ASPECT_RATIO[1]
    anim.refresh()

  def update_animation_content(self):
    animation_widget = self.query_one("#ascii-animation", AsciiAnimation)
    animation_widget.refresh()
        
  async def on_mount(self) -> None:
    """
      Called when the app is ready. Sets up the virtual size and
      links the scroll position to the animation widget.
    """
    animation_widget = self.query_one("#ascii-animation", AsciiAnimation)
    animation_widget.label = self.query_one("#renderpos", Label)

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

