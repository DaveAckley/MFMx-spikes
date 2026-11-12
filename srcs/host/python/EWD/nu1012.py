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

def sign(num):
  if num < 0: return -1
  if num > 0: return 1
  return 0

import sys
argType = None
altType = None
if len(sys.argv) >= 2:
  argType = sys.argv[1]
  if len(sys.argv) >= 3:
    altType = sys.argv[2];
    
ewd = None

def logcb(key,text):
  global ewd
  if ewd:
    ewd.logkt(key,text)
  else:
    print(key,text)

class EWD:
  def __init__(self,config):
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

  GRID_ASPECT_RATIO = (2,1)

  # BINDINGS = [
  #   ("q", "quit", "Quit"),
  #   ("+", "app.changeZoom('zoomin')", "Zoom in"),
  #   ("=", "app.changeZoom('resetzoom')", "Reset zoom"),
  #   ("-", "app.changeZoom('zoomout')", "Zoom out"),
  #   ("up", "app.scrollGrid('up')", "Scroll up"),
  #   ("down", "app.scrollGrid('dn')", "Scroll down"),
  #   (".", "app.scrollGrid('ct')", "Scroll center"),
  #   ("left", "app.scrollGrid('lt')", "Scroll left"),
  #   ("right", "app.scrollGrid('rt')", "Scroll right"),
  #   ("n", "app.nuke(False)", "Small nuke"),
  #   ("N", "app.nuke(True)", "Large nuke"),
  #   ("s", "app.seed(False)", "Add start seed"),
  #   ("S", "app.seed(True)", "Add alt start seed"),
  # ]

  # ZOOM_BUTTONS = {
  #   "zoomin" : ("+",+1),
  #   "resetzoom" : ("=", 0),
  #   "zoomout" : ("-",-1),
  # }

  # SCROLL_BUTTONS = {
  #   "nw" : ("⌜",+1,+1),
  #   "up" : ("↑", 0,+1),
  #   "ne" : ("⌝",-1,+1),
  #   "lt" : ("←",+1, 0),
  #   "ct" : ("·", 0, 0),
  #   "rt" : ("→",-1, 0),
  #   "sw" : ("⌞",+1,-1),
  #   "dn" : ("↓", 0,-1),
  #   "se" : ("⌟",-1,-1),
  # }

  BHMIN = 1
  BHMAX = 1

  def logkt(self,key,text):
    global ewd
    msg = f"{key}:{text}\n"
    if False and ewd and (rlist := ewd.query("#richlog")):
      rlist.first().write(msg)     # write to onscreen log

    with open(f"{self.simDir}misc/nu1011-{key}.txt","a") as file:
      file.write(msg)

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

    self.quietbox = MFMx.QuietBox.get()

    print("QBDONGA",self.quietbox)
    print("CONSTANKS",self.quietbox.getConstants())
    print("RANGSES\n",self.quietbox.getRanges(),"SESGNAR")

    self.quietbox.init()
    print("QB-INI-TED")

    #MFMx.BHLog.setLogCallback(logcb)

    print("UPTOSUPER",super())
    #super().run()

  def release_the_puppies(self):
    print("RELEASE THE PUPPIES")
    cmd = MFMx.PhaserBolt.Cmd.RUN_EVENTS
    args = (0,)
    self.quietbox.shootPHASER(cmd,args)

    self.slowscan_updates = 0
    self.slowscan_tlbis_per_update = 1
    self.slowscan_updates_per_second = .5
    self.slowscan_timer = self.set_interval(1 / self.slowscan_updates_per_second,
                                            self.update_slowscans)
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
      time.sleep(.2)
      bh.runSlowScans(count)

  def doRTMPGraphicsFrame(self,simnanos):
    ewc = self.ewc
    img = ewc.renderGraphicsGridWindowToImage()
    frameNP = img.asNP()
    self.theRTMPFeed.sendGraphicsFrame(frameNP,simnanos,f"{self.scriptName}/{self.mfmxVersion}") 

if __name__ == "__main__":
  c = Config.Config(__file__,"config/nu1012.dtoml")
  app = EWD(c)
  print("GOINDGINKTORUN",app)
  app.run()
  print("HIEMBAK!",app)
