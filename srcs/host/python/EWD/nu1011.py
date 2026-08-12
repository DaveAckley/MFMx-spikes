#!/data/ackley/PART4/code/D/blackholeSpikes/venv/bin/python
import os
import time
from datetime import datetime
from mfmx import MFMx

import random

import dumper

import Config

from RTMPFeed13 import RTMPFeed

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

#import re
#keyre = re.compile(r"^(.)(\d)([0-9a-fA-F])([0-9a-fA-F])$")

def sign(num):
  if num < 0: return -1
  if num > 0: return 1
  return 0

# does this run too late and generate bogus log filenames?
# def clearcb():
#   MFMx.BHLog.clearLogCallback()

# import atexit
# atexit.register(clearcb)

import sys
argType = None
altType = None
if len(sys.argv) >= 2:
  argType = sys.argv[1]
  if len(sys.argv) >= 3:
    altType = sys.argv[2];
    
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
        self.lastChipDisplayed = 0
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
  return # DO NOTHING DAMMIT
  global ewd
  if ewd:
    ewd.logkt(key,text)
  else:
    print(key,text)

#class EWD(App[None]):
class EWD(App):
  def __init__(self,config):
    super().__init__()
    self.scriptDir = os.path.dirname(os.path.abspath(__file__))
    self.baseDir = os.path.abspath(f"{self.scriptDir}/../../../..")
    print("BASEDIR",self.baseDir)
    self.simDir = MFMx.getSimDir()
    print("SIMDIR",self.simDir)
    self.config = config
    print("BONGO",self.config)
    self.imageManager = MFMx.ImageManager()
    self.renderConsole = None
    self.theRTMPFeed = RTMPFeed(self,'ewd')
    self.key = "EWDA"
    self.logkt(self.key,f"FEEED {self.theRTMPFeed}")
    self.configureImageManager()

    # INIT ADVANCED AUTONUKE TECHNOLOGY
    self.fireCount = 0
    self.fireBig = False

  CSS_PATH = "config/styles.tcss"

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
    ("n", "app.nuke(False)", "Small nuke"),
    ("N", "app.nuke(True)", "Large nuke"),
    ("s", "app.seed(False)", "Add start seed"),
    ("S", "app.seed(True)", "Add alt start seed"),
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
  BHMAX = 1


  def logkt(self,key,text):
    global ewd
    #,keyre

#    if m := keyre.match(key):
#      tag = MFMx.BHTag(m.group(1),int(m.group(2)),hex(m.group(3)),hex(m.group(4)))
#    else:
#      tag = MFMx.BHTag(3,4,0xf,0xe);      # 3 == APPDBG
    #text = text.strip("\n")
    msg = f"{key}:{text}\n"
    if False and ewd and (rlist := ewd.query("#richlog")):
      rlist.first().write(msg)     # write to onscreen log

    #MFMx.BHLog.log(msg)  # alt/2nd dest DEADLOCKY
    with open(f"{self.simDir}misc/nu1011-{key}.txt","a") as file:
      file.write(msg)
    #print(msg,file=sys.stderr)                  # FOGIT

  def configureImageManager(self):
    print("START configureImageManager",self)
    self.config.load()
    im = self.imageManager
    modict = self.config.hash.get('module',{})
    for k,v in modict.items():
      hm = im.makeCommsModule(k)
      for b in v.get('blocks',[]):
        hm.requireCommBlockNamed(b)
        print("REBONGO",hm,b)
      print("MODFONG",k,v,hm)

    cdict = self.config.hash['image']
    keys = []
    for k,v in cdict.items():
      imagecode = v['code']
      path = v['binfile']
      #dumper.dump(self.config)
      for bindir in self.config.hash['Config']['bindirs']:
        bd = os.path.abspath(bindir.replace("$script_dir",self.scriptDir,1))
        if not os.path.isdir(bd): continue
        maybebinfile = f"{bd}/{v['binfile']}"
        if os.path.isfile(maybebinfile):
          imagecode = v['code']
          img = im.makeT6Image(k,imagecode,maybebinfile)
          self.logkt("KEYK","KONGMO "+k+" "+str(img))
          self.logkt("KEYK","BINPOS "+k+" "+str(hex(img.getBinWord(5))))
          self.logkt("KEYK","BINP2S "+k+" "+str(hex(img.getBinWord(6))))
          self.logkt("KEYK","BINP3S "+k+" "+str(hex(img.getBinWord(7))))
          keys.append(k)
        else:
          print("NOT FOUND FOR",k,"->",maybebinfile)
    for k in keys:
      print("ZING",k)
      img = im.getT6Image(k)
      print("ZANG",k,img,img.getImageCode(),img.getBinFileSize(),img.getBinFilePath())
    celldict = self.config.hash['cell']
    for k,c in celldict.items():
      size = MFMx.U8C(*c['size'])
      print("cell",k,c,size)
      cell = im.makeCell(k,size)
      print("CELL",cell)
      if c.get('fill'):
        imglabel = c['fill']
        img = im.getT6Image(imglabel)
        print("LAB",imglabel,img,img.getImageCode())
        cell.addImage(MFMx.U8C(255,255), imglabel)
      imgs = c.get('image')
      print("IM",imgs)
      if imgs: 
        for x,v in imgs.items():
          y = next(iter(v.keys()))
          val = v[y]
          x = 255 if x == 'all' else int(x)
          y = 255 if y == 'all' else int(y)
          print("IM",x,y,val)
          print("ZING",k,val)
          img = im.getT6Image(val)
          cell.addImage(MFMx.U8C(x,y),val)
      print("CELLDONE",cell)
      print("NEXT")
    layoutdict = self.config.hash['layout']
    for k,l in layoutdict.items():
      print("LAYS",k,l);
      lay = im.makeLayout(k,l['defaultImage'])
      for bhc in l['bhchips']:
        lay.addBlackholeChip(int(bhc))
      print("LAID",lay);
      defcell = l.get('fill')
      if defcell:
        cell = im.getCell(defcell)
        print("DEFCELL", defcell, cell)
        lay.addCell(255, defcell)
      cells = l.get('cell')
      print("CS",cells)
      if cells: 
        for c,cellname in cells.items():
          c = 255 if c == 'all' else int(c)
          print("CD",c,cellname)
          lay.addCell(c, cellname)

    cfgdict = self.config.hash['Config']
    act = cfgdict['activeLayout']
    modules = layoutdict[act].get('hostModules',[])
    print("MODS",modules,"FOR",act,layoutdict[act])
    im.setActiveLayout(act)

    print("HAVEO activelayout",act,im)
    al = im.getActiveLayout()
    activeChipNums = al.getActiveBHChips();
    self.bhs = [ MFMx.Blackhole(i) for i in activeChipNums ]
    print("LAYTBEEATCHES",al,self.bhs)

    # Get far enough along that we can configure modules..
    for bh in self.bhs:
      bh.setPhase(MFMx.Phase.HAS_ALLOCATED_TLBS)

    for bh in self.bhs:
      print("BHWCOMMSMODULES",modules)
      for hmname in modules:
        hm = im.getCommsModule(hmname) # or bang
        bh.addCommsModule(hm)
        print("BHaddcommsmodule",bh,hm)

      print("BHWCOMMS",bh)

      #bh.setHostMemoryBaseAddress()    # comms module config is done
      # ..but BH::allocateHostRAM() hasn't happened yet
      # ..so let's do setHost.. in allocateHost.. instead
    print("LAYDACT",al)

    print("CDFGIDC",act)
    for k in keys:
      img = im.getT6Image(k)
      self.logkt("ZEYK","KONGMO "+k+" "+str(img))
      self.logkt("ZEYK","BINPOS "+k+" "+str(hex(img.getBinWord(5))))
      self.logkt("ZEYK","BINP2S "+k+" "+str(hex(img.getBinWord(6))))
      self.logkt("ZEYK","BINP3S "+k+" "+str(hex(img.getBinWord(7))))

  def run(self):
    # RESET CHIPS EAAAAARRLY
    import subprocess
    subprocess.run(["/opt/tenstorrent/pipx/bin/tt-smi","-r"])

    self.mfmxVersion = MFMx.getVersion()
    self.im = MFMx.ImageManager()
    al = self.im.getActiveLayout()
    bhchips =  al.getActiveBHChips()
    print("ASKLSQALK",al,bhchips)

    print("DUUUUMPPERCONFIG")
    dumper.dump(self.config)

    #dumper.dump(self)
    self.ewc = MFMx.EWControl.getEWControl()
    print("EWCONGA",self.ewc)
    MFMx.BHLog.setLogCallback(logcb)

    print("UPTOSUPER",super())
    super().run()
    MFMx.BHLog.clearLogCallback()

  def compose(self) -> ComposeResult:
    self.logkt(self.key,f"composestart {self}")
    global ewd
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
    self.logkt(self.key,f"composeend10 {self}")
    ewd = self
    self.logkt(self.key,f"composeend11 {self}")

  def action_changeZoom(self,id):
    anim = self.query_one("#ascii-animation")
    label,dz = EWD.ZOOM_BUTTONS[id];
    if dz == 0:
      anim.display_zoom = 0
    else:
      anim.display_zoom += dz
    anim.refresh()

  def action_nuke(self,big):
    l = "NUKE" if big else "nuke"
    r = self.ewc.doNuke(big)
    self.logkt(l,r)

  def action_seed(self,doalt):
    l = "SEED"
    if not doalt or altType is None:
      r = self.ewc.doSeed(1)   # start type
    else:
      r = self.ewc.doSeed(int(altType))
    self.logkt(l,r)

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
    curtime = time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
    elapsedns = curtime - self.animation_start_time
    if elapsedns <= 1_000_000_000 * self.animation_frame_count / self.animation_frames_per_second:
      return
    self.animation_frame_count += 1
    animation_widget = self.query_one("#ascii-animation", AsciiAnimation)
    #animation_widget.refresh()
    animation_widget.refreshCount += 1
    #self.doRTMPFrame()
    self.doRTMPGraphicsFrame(elapsedns)
    if animation_widget.refreshCount % 10 == 0:
      count = 1
      #self.logkt(self.key,f"{animation_widget.refreshCount}SLOSC{count}")
      self.slowScan(count)
    if False and animation_widget.refreshCount > 5*90*3 and random.randrange(5*60) == 0:
      if random.randrange(3) == 0:
        self.fireCount = random.randint(1,10)
        self.fireBig = True;
      else:
        self.fireCount = random.randint(10,100)
        self.fireBig = False;
    elif self.fireCount > 0:
      self.action_nuke(self.fireBig)
      self.fireCount = self.fireCount - 1
      
  async def on_mount(self) -> None:
    """
      Called when the app is ready. Sets up the virtual size and
      links the scroll position to the animation widget.
    """
    self.scriptName = os.path.basename(__file__).removesuffix(".py")
    self.title = f"version {self.scriptName} + {self.mfmxVersion} started {datetime.now().astimezone().isoformat()}"
    self.logkt(self.key,f"on_mount {self}")
    animation_widget = self.query_one("#ascii-animation", AsciiAnimation)
    animation_widget.poslabel = self.query_one("#renderpos", Label)
    animation_widget.statslabel = self.query_one("#statsline", Label)

    self.animation_frame_count = 0
    self.animation_frames_per_second = 5
    self.animation_start_time = time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
    self.animation_timer = self.set_interval(1 / (2*self.animation_frames_per_second),
                                             self.update_animation_content, pause=False)
    self.runEvents()

  def reset(self):
    print("NOBODILUBME?")
    #exit(91)

    global argType
    for bh in self.bhs:
      #bh.setMFMxDefaultCodePath("../../../../build_cross/bin/ewp.bin")
      #bh.close() can't close now we're already foggen configured doh
      if argType:
        bh.setStartDecayType(int(argType))
      #bh.layoutImages() let c++ do this during genesis

  def start(self):
    for bh in self.bhs:
      bh.startEWProcessing()
      print("ZONG",bh.getChipNumber())

  def stop(self):
    for bh in self.bhs:
      bh.stopEWProcessing()

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
        time.sleep(1)
        #self.slowScan(140);
        self.slowScan(1);
        print("STOPPING EWPROC\n")
    time.sleep(1)
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
    logcb("EWDA",f"ChBoxCh>{id},{event.value},{self.ewc.isActive()}")

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

  def doRTMPGraphicsFrame(self,simnanos):
    ewc = self.ewc
    img = ewc.renderGraphicsGridWindowToImage()
    frameNP = img.asNP()
    self.theRTMPFeed.sendGraphicsFrame(frameNP,simnanos,f"{self.scriptName}/{self.mfmxVersion}") 

if __name__ == "__main__":
  c = Config.Config(__file__,"config/nu1011.dtoml")
  #dumper.dump(c)
  app = EWD(c)
  print("GOINDGINKTORUN",app)
  app.run()
  print("HIEMBAK!",app)
