#from unidecode import unidecode
import random
import FontCache
import HubGeometry
from PIL import ImageFont, ImageDraw, Image
import os
import time
import datetime
import subprocess
import numpy as np
import cv2

import socket
import errno
import threading

# 'overlay_transparent'
# Source - https://stackoverflow.com/a
# Posted by Cristian Garcia, modified by community. See post 'Timeline' for change history
# Retrieved 2025-11-27, License - CC BY-SA 4.0

def overlay_transparent(background, overlay, x, y):

    background_width = background.shape[1]
    background_height = background.shape[0]

    if x >= background_width or y >= background_height:
        return background

    h, w = overlay.shape[0], overlay.shape[1]

    if x + w > background_width:
        w = background_width - x
        overlay = overlay[:, :w]

    if y + h > background_height:
        h = background_height - y
        overlay = overlay[:h]

    if overlay.shape[2] < 4:
        overlay = np.concatenate(
            [
                overlay,
                np.ones((overlay.shape[0], overlay.shape[1], 1), dtype = overlay.dtype) * 255
            ],
            axis = 2,
        )

    overlay_image = overlay[..., :3]
    mask = overlay[..., 3:] / 255.0

    background[y:y+h, x:x+w] = (1.0 - mask) * background[y:y+h, x:x+w] + mask * overlay_image

    return background

def mix(col1,col2,pct1):
      return (int((pct1*col1[0]+(100-pct1)*col2[0])/100),
              int((pct1*col1[1]+(100-pct1)*col2[1])/100),
              int((pct1*col1[2]+(100-pct1)*col2[2])/100))

def shadowText(img,text,org,col,offset=1,fontscale=.8):
      o = offset
      fs = fontscale
      bri = mix(col,(255,255,255),20)
      dim = mix(col,(0,0,0),20)
      ret = img
      ret = cv2.putText(img=ret,text=text, org=(org[0]+o,org[1]+o), fontFace=2, fontScale=fs, color=dim, thickness=1)
      ret = cv2.putText(img=ret,text=text, org=(org[0]-o,org[1]-o), fontFace=2, fontScale=fs, color=bri, thickness=1)
      ret = cv2.putText(img=ret,text=text, org=(org[0]+0,org[1]+0), fontFace=2, fontScale=fs, color=col, thickness=1)      
      return ret

def load_rtmp_target(stream):
    """Return (url, secret) for the RTMP server, read from a config file that
    is NOT in the git repo.  Search order: $MFMX_RTMP_CONF, then
    ~/.config/mfmx/rtmp.toml.  Keys: user, password (required); host
    (default vidsrv), port (default 1935), app (default live).  No hardcoded
    fallback: a missing file is an error."""
    import tomlikey     # the project's TOML dependency (Config.py uses it); tomllib needs Python 3.11+
    path = os.environ.get("MFMX_RTMP_CONF") or os.path.expanduser("~/.config/mfmx/rtmp.toml")
    try:
        with open(path, "r") as f:
            c = tomlikey.loads(f.read())
        mode = os.stat(path).st_mode & 0o077
    except FileNotFoundError:
        raise RuntimeError(
            f"RTMP settings file not found: {path}\n"
            "Create it (outside any git repo), readable only by you (chmod 600), containing:\n"
            '    user = "..."\n    password = "..."\n'
            '    # optional: host = "vidsrv"   port = 1935   app = "live"\n'
            "or point MFMX_RTMP_CONF at it.") from None
    if mode:
        print(f"WARNING: {path} is accessible by group/others; chmod 600 it", flush=True)
    try:
        user, pw = c["user"], c["password"]
    except KeyError as e:
        raise RuntimeError(f"{path}: missing key {e}") from None
    host, port, app = c.get("host", "vidsrv"), c.get("port", 1935), c.get("app", "live")
    return f"rtmp://{host}:{port}/{app}/{stream}?user={user}&pass={pw}", pw


class RTMPFeed:
    def redact(self, obj):
        """str(obj) with the stream password masked (for log files)."""
        return str(obj).replace(self.rtmp_secret, "****") if self.rtmp_secret else str(obj)

    def __init__(self,ewd,stream='test'):
        self.magicNum = 0
        self.ewd = ewd
        self.key = "RT10";
        basedir = self.ewd.scriptDir
        self.logo_path = f"{basedir}/logo/logotype-lcf-chop-16-yellow-on-transparent-shadow.png"
        logo = cv2.imread(self.logo_path,-1) # neg arg to keep alpha
        lw,lh = logo.shape[0:2]
        pctsize=1.48
        self.slw,self.slh = int(pctsize*lw/100),int(pctsize*lh/100)
        self.smlogo = cv2.resize(logo,(self.slw,self.slh),interpolation=cv2.INTER_AREA)
        print("smlogo",self.smlogo.shape)
        self.rtmp_url, self.rtmp_secret = load_rtmp_target(stream)

        h,w = 1080,1920
        #h,w = 2160,3840
        b,g,r = 0x3e, 0x88, 0x35 # orange
        b,g,r = 20,20,20 # dark greyf
        self.img = np.zeros((h,w,3), np.uint8)
        self.img[:,:,0] = b
        self.img[:,:,1] = g
        self.img[:,:,2] = r

        self.rgbimg = cv2.cvtColor(self.img, cv2.COLOR_BGR2RGB)
        #self.pil_img = Image.fromarray(self.rgbimg)

        #self.renderFont = ImageFont.truetype("fonts/Inconsolata.ttf",16)
        #self.renderFont = ImageFont.truetype("fonts/JetBrainsMono/ttf/JetBrainsMono-ExtraLight.ttf",18)

        self.fontCache = FontCache.FontCache(self.ewd)
        self.fontCodeBody = self.fontCache.getFontCode(f"{basedir}/fonts/JetBrainsMono/ttf/JetBrainsMono-Light.ttf",23,
                                                       (230,230,0), (0x12,0x14,0x12))
        # self.fontCodeDate = self.fontCache.getFontCode("fonts/JetBrainsMono/ttf/JetBrainsMono-Bold.ttf",30,
        #                                                (0xef,0xbf,0x04), (0x12,0x14,0x12), preload=False)
        # self.fontCodeTime = self.fontCache.getFontCode("fonts/JetBrainsMono/ttf/JetBrainsMono-Bold.ttf",27,
        #                                                (0xef,0xbf,0x04), (0x12,0x14,0x12), preload=False)

        # self.fontCodeDate = self.fontCache.getFontCode("fonts/alma-mono-final/AlmaMono-Bold.ttf",30,
        #                                                (0xef,0xbf,0x04), (0x12,0x14,0x12), preload=False)
        # self.fontCodeTime = self.fontCache.getFontCode("fonts/alma-mono-final/AlmaMono-Bold.ttf",27,
        #                                                (0xef,0xbf,0x04), (0x12,0x14,0x12), preload=False)

        # self.fontCodeDate = self.fontCache.getFontCode("fonts/SpaceMono-Regular.ttf",30,
        #                                                (0xef,0xbf,0x04), (0x12,0x14,0x12), preload=False)
        # self.fontCodeTime = self.fontCache.getFontCode("fonts/SpaceMono-Regular.ttf",27,
        #                                                (0xef,0xbf,0x04), (0x12,0x14,0x12), preload=False)

        self.fontCodeDateSize = 25
        self.fontCodeTimeSize = 22
        self.fontCodeLiveSize = 16
        self.fontCodeDate = self.fontCache.getFontCode(f"{basedir}/fonts/NK57Monospace/NK57 Monospace Cd Rg.otf",
                                                       self.fontCodeDateSize,
                                                       (0xef,0xbf,0x04), (0x12,0x14,0x12), preload=False)
        self.fontCodeTime = self.fontCache.getFontCode(f"{basedir}/fonts/NK57Monospace/NK57 Monospace Cd Rg.otf",
                                                       self.fontCodeTimeSize,
                                                       (0xef,0xbf,0x04), (0x12,0x14,0x12), preload=False)
        # logo block stats (LIVE, AER, run name): pink, brighter and less
        # saturated than the NO DATA red (Liv 2026-11-27; was 0xef,0x2f,0x04)
        self.fontCodeLive = self.fontCache.getFontCode(f"{basedir}/fonts/NK57Monospace/NK57 Monospace Cd Rg.otf",
                                                       self.fontCodeLiveSize,
                                                       (0xff,0x8c,0xa8), (0x12,0x14,0x12), preload=False)

        # NO DATA banner: big, centred; font as 11 chars over 3 grid tiles.  A tile of the
        # rendered grid is a hub base size in px (the cross marks in EWControl.cpp).
        self.staleTilePx = (HubGeometry.T6GRID_BASE_WIDTH, HubGeometry.T6GRID_BASE_HEIGHT)
        self.staleTiles = 3
        self.staleText = "NO DATA"
        self.staleSizeChars = 11       # font sized as for "NO NEW DATA"
        staleFontPath = f"{basedir}/fonts/NK57Monospace/NK57 Monospace Cd Rg.otf"
        self.fontCodeStaleSize = self.fontSizeForTextWidth(
            staleFontPath, self.staleSizeChars,
            self.staleTiles*self.staleTilePx[0])
        self.fontCodeStale = self.fontCache.getFontCode(staleFontPath,
                                                        self.fontCodeStaleSize,
                                                        (0xef,0x2f,0x04), (0x12,0x14,0x12), preload=False)

        #gather video info to ffmpeg
        #self.fps = int(10)
        self.fps = int(5)
        #self.fps = int(4)
        self.height = self.img.shape[0]
        self.width = self.img.shape[1]
        self.secsPerI = 2
#        self.secsPerI = 4
#        self.secsPerI = 1
        self.goplen = int(self.secsPerI*self.fps)
        self.localStreamPath = "/data/ackley/AV/MFMX-STREAMS"
        self.ffmpegCommandBaseParms = [
            'ffmpeg', '-y',
            '-hide_banner', '-nostats', '-loglevel', 'warning',
            #'-re',
            '-f', 'rawvideo', '-vcodec', 'rawvideo', '-pix_fmt', 'bgr24',
            '-s', "{}x{}".format(self.width, self.height),
            '-framerate', str(self.fps),   # 'input option' ?
            '-vsync', '2', '-i', '-',
            '-r', str(self.fps),           # 'output option' ?
            '-map', '0', # AI CLAIMS 'tee muxer requires explicit -map'
            '-flags', '+global_header',    # AI CLAIMS: makes encoder emit SPS/PPS as extradata
            '-c:v', 'libx264', '-g', str(self.goplen), '-x264-params', 'no-scenecut=1',
            '-tag:v', '7',                 # AI CLAIMS: forces correct H.264 codec tag for FLV
            '-pix_fmt', 'yuv420p',
            '-preset', 'ultrafast', '-tune', 'zerolatency',
            #'-preset', 'veryfast',
            #'-maxrate', '4M',
            #'-bufsize', '1M',
            '-f', 'tee',
#            '[f=flv]{}|[f=flv]{}'.format(self.rtmp_url, local_file), # tee to rtmp stream and local disk
        ]

        self.ewd.logkt(self.key,f"SIZE {self.width} x {self.height}")
        self.ewd.logkt(self.key,f"RUN {self.ffmpegCommandBaseParms}")

        self.ewd.logkt(self.key,f"GORMO")
        # Frame pacing.  ffmpeg stamps raw input frames by COUNT at
        # -framerate, not by arrival time, so a stall in the (single
        # threaded) shell makes the stream run dry and the viewer
        # rebuffers.  A pacer thread is the only writer: exactly fps frames
        # per second, the newest real one or a repeat (see pacer()).
        # Stats go to the FEEDSTAT log.
        self.writeLock = threading.Lock()
        self.lastBytes = None          #< last frame written, raw BGR bytes
        self.lastBase = None           #< last real image BEFORE overlays
        self.lastSimNanos = 0          #< its sim time
        self.lastInfo = ""             #< its info line
        self.lastExtra = ""            #< its live-AER line (own line, may be empty)
        self.scyFn = None              #< callable -> supercycle number (or None) for the LIVE line
        self.lastRealMono = 0.0        #< monotonic time it arrived
        self.overlayLock = threading.Lock()
        self.lastWriteT = 0.0          #< monotonic time of last write
        self.lastRealT = 0.0           #< ... of last real frame
        self.keepThread = None         #< the pacer thread
        self.pendLock = threading.Lock()
        self.pendBytes = None          #< newest real frame not yet written
        self.statDrops = 0             #< real frames superseded unwritten
        self.statResyncs = 0           #< stalls too long to catch up on
        self.statMaxOwed = 0.0         #< most frames owed (behind) seen
        self.statFrames = 0
        self.statDups = 0
        self.statMaxGap = 0.0          #< longest wait between real frames
        self.statMaxWrite = 0.0        #< slowest pipe write
        self.statNextReport = time.monotonic() + 30.0
        self.subproc_started = 0
        self.restartSubProc();
        self.ewd.logkt(self.key,f"SLORG")
        self.ewd.logkt(self.key,f"MADEFONTS({self.fontCodeTime},{self.fontCodeDate})")

        self.ewd.logkt(self.key,f"XX {self.subproc_started}")
        self.bytesWritten = 0

    def idle_image(self):
        """A fresh dark placeholder frame (sendGraphicsFrame draws on its argument)."""
        return self.img.copy()

    def restartSubProcDEBUG(self):
        self.framesSentThisSubproc = 0
        if False and self.subproc_started > 10:
            print("TOO MONEY RESTRATS",self.subproc_started)
            sys.exit(1)
        #using subprocess and pipe to fetch frame data
        self.ewd.logkt(self.key,f"STARTING {self.ffmpegCommand}")
        with open(f"subproc{len(str(self.subproc_started))}{self.subproc_started}.out",'w') as f:
            self.subproc = subprocess.Popen(self.ffmpegCommand,
                                            stdin=subprocess.PIPE,
                                            stdout=f,
                                            stderr=subprocess.STDOUT)
        self.subproc_started += 1
        self.ewd.logkt(self.key,f"SUBPROC #{self.subproc_started} IS {self.subproc}")
        self.ewd.logkt(self.key,"CLAMS!")

    def restartSubProc(self):
        self.framesSentThisSubproc = 0
    
        now = datetime.datetime.now(datetime.timezone.utc)
        localDir = now.strftime(f"{self.localStreamPath}/%Y-%m-%d")
        os.makedirs(localDir, exist_ok=True);
        localFile = now.strftime(f"{self.localStreamPath}/%Y-%m-%d/%Y-%m-%d+%H%M%S%Z.flv")
#        outputOptions = ":onfail=ignore:use_fifo=1:fifo_options=drop_pkts_on_overflow=1\\:attempt_recovery=1\\:recovery_wait_time=1\\:restart_with_keyframe=1"
#        outputOptions = ":onfail=ignore:use_fifo=1"
#        outputOptions = ":onfail=ignore:use_fifo=1:fifo_options=drop_pkts_on_overflow=1"
        outputOptions = ":onfail=ignore:use_fifo=1:fifo_options=drop_pkts_on_overflow=1\\\\:attempt_recovery=1\\\\:recovery_wait_time=1\\\\:restart_with_keyframe=1"
        ffmpegCmd = [*self.ffmpegCommandBaseParms[:],
                     f"[f=flv{outputOptions}]{self.rtmp_url}|[f=flv{outputOptions}]{localFile}"]
        
        self.ewd.logkt(self.key,f"STARTING {self.redact(ffmpegCmd)}")
        #print(f"SLOTRHOP STARTING {ffmpegCmd}")
        self.subproc = subprocess.Popen(ffmpegCmd,
                                        stdin=subprocess.PIPE,
                                        stdout=subprocess.DEVNULL,
                                        stderr=subprocess.PIPE)
        threading.Thread(target=self.drainStderr,
                         args=(self.subproc, self.subproc_started + 1),
                         daemon=True).start()
        self.subproc_started += 1
        self.ewd.logkt(self.key,f"SUBPROC #{self.subproc_started} IS {self.subproc}")
        self.ewd.logkt(self.key,f"CLAMS! @{localFile}")

    def drainStderr(self, proc, num):
        """Copy ffmpeg's stderr (password masked) into the FFMPEG log."""
        buf = b""
        try:
            while True:
                data = proc.stderr.read1(4096)
                if not data:
                    break
                buf += data
                parts = buf.replace(b"\r", b"\n").split(b"\n")
                buf = parts.pop()
                for ln in parts:
                    if ln.strip():
                        self.logFF(num, ln.decode("utf-8", "replace"))
        except Exception as e:
            self.logFF(num, f"stderr reader died: {e}")
        if buf.strip():
            self.logFF(num, buf.decode("utf-8", "replace"))
        self.logFF(num, f"ffmpeg exited, returncode {proc.wait()}")

    def logFF(self, num, text):
        self.ewd.logkt("FFMPEG", f"#{num} {self.redact(text)}")

    def writeFrame(self, data, real):
        """Write one raw frame (bytes) to ffmpeg.  Also used by the
        pacer thread; the lock is a leftover guard."""
        with self.writeLock:
            t0 = time.monotonic()
            try:
                mv = memoryview(data)
                blockSize = 1 << 20
                for i in range(0, len(mv), blockSize):
                    chunk = mv[i : i + blockSize]
                    self.subproc.stdin.write(chunk)
                    self.bytesWritten += len(chunk)
            except (BrokenPipeError, socket.error) as e:
                if e.errno != errno.EPIPE:
                    raise
                self.ewd.logkt(
                    self.key, f"BORKEN PIPE {e} AFTER {self.bytesWritten}")
                self.restartSubProc()
                return
            t1 = time.monotonic()
            self.lastWriteT = t1
            self.statMaxWrite = max(self.statMaxWrite, t1 - t0)
            if real:
                if self.lastRealT:
                    self.statMaxGap = max(
                        self.statMaxGap, t1 - self.lastRealT)
                self.lastRealT = t1
                self.statFrames += 1
            else:
                self.statDups += 1
            if t1 >= self.statNextReport:
                self.statNextReport = t1 + 30.0
                self.ewd.logkt(
                    "FEEDSTAT",
                    f"30s: real {self.statFrames} dup {self.statDups} "
                    f"maxgap {self.statMaxGap:.2f}s "
                    f"maxwrite {self.statMaxWrite:.2f}s "
                    f"drop {self.statDrops} resync {self.statResyncs} "
                    f"owed {self.statMaxOwed:.1f}")
                self.statDrops = 0
                self.statResyncs = 0
                self.statMaxOwed = 0.0
                self.statFrames = 0
                self.statDups = 0
                self.statMaxGap = 0.0
                self.statMaxWrite = 0.0

    MAX_OWED = 5.0         #< longest stall (s) the pacer will pay back
    SLEW_GAIN = 0.05       #< rate change per owed frame (fraction of fps)
    MAX_SLEW = 0.25        #< most the rate may change: fps*(1 +- 0.25)

    def pacer(self):
        """The ONLY writer of frames to ffmpeg, holding the LONG-TERM
        rate at exactly fps, like mains frequency.

        ffmpeg stamps raw frames by COUNT at -framerate, not by arrival
        time.  Frames written faster than fps on average make the stream's
        timeline run slow against the wall clock, and a player that plays
        it faithfully falls further and further behind (minutes after a
        few hours; a reload jumps back to the live edge).  Frames written
        slower starve the player.

        So every frame time is fractional.  owed = (wall time * fps) -
        frames written: positive when we are behind.  The next interval is
        1/(fps * (1 + slew)) with slew = SLEW_GAIN * owed, limited to
        +-MAX_SLEW: a little fast while behind, a little slow while ahead,
        exactly fps when owed is 0 (an exponential correction, time
        constant 1/(fps*SLEW_GAIN) s).  Stretching the correction out,
        instead of writing the owed frames in a burst, gives real frames
        time to arrive and displace repeats.  A stall that leaves more
        than MAX_OWED seconds owed gives up and resyncs (statResyncs).

        Each tick writes the newest real frame if one arrived since the
        last tick (an older unwritten one is dropped, see statDrops), else
        repeats the last frame as is.  The repeat is NOT re-rendered while
        the data is merely a little late; once it is stale enough for the
        NO DATA banner it is re-rendered, at most once a second, so the
        banner's seconds and the clocks keep moving."""
        fps = float(self.fps)
        period = 1.0 / fps
        minStep = period / (1.0 + self.MAX_SLEW)
        t0 = time.monotonic()           #< when the frame count was zero
        sent = 0                        #< frames written since t0
        due = t0
        bannerT = 0.0                   #< when we last re-rendered a repeat
        while True:
            now = time.monotonic()
            if now < due:
                time.sleep(min(due - now, 0.05))
                continue
            owed = (now - t0) * fps - sent
            if owed > self.MAX_OWED * fps:
                t0 += owed / fps        # hopeless: forgive the debt
                owed = 0.0
                self.statResyncs += 1
            self.statMaxOwed = max(self.statMaxOwed, owed)
            slew = min(self.MAX_SLEW,
                       max(-self.MAX_SLEW, self.SLEW_GAIN * owed))
            due = max(due + period / (1.0 + slew), now + minStep)
            try:
                with self.pendLock:
                    data = self.pendBytes
                    self.pendBytes = None
                real = data is not None
                if not real:
                    base = self.lastBase
                    if base is None:
                        t0 = now        # nothing to send yet: no debt
                        sent = 0
                        continue
                    data = self.lastBytes
                    realMono = self.lastRealMono
                    stale = now - realMono
                    if stale >= 1.5 and now - bannerT >= 1.0:
                        bannerT = now
                        img = base.copy()
                        sim = self.lastSimNanos + int(stale * 1e9)
                        self.renderOverlay(img, sim, self.lastInfo,
                                           stale, self.lastExtra)
                        data = np.ascontiguousarray(img).tobytes()
                        if self.lastRealMono == realMono:  # no new real one
                            self.lastBytes = data
                self.writeFrame(data, real)
                sent += 1
            except Exception as e:
                self.ewd.logkt("FEEDSTAT", f"pacer: {e}")
                time.sleep(1.0)

    def drawTextOnImage(self,img,atext,xy,cachedfont):
        bfbox = cachedfont.renderBox
        cols = 0
        lines = 0
        for i in range(0,len(atext)):
            ch = "" + atext[i]
            cachedfont.drawCodepoint(ch,img,int(cols*bfbox[1]+xy[0]),int(lines*bfbox[0]+xy[1]))
            cols = cols+1
        
    def sendTextFrame(self,text):
        self.framesSentThisSubproc += 1
        if False and self.framesSentThisSubproc > 1000:
            self.ewd.logkt(self.key,f"CREFRESHING {self.subproc} WITH PREJUDICE")
            self.subproc.terminate()
        now = datetime.datetime.now(datetime.timezone.utc)
        snowd = now.strftime("%Y-%m-%d")
        #snowt = now.strftime("%H%M%S%Z")
        snowt = now.strftime("%H:%M:%SUTC")
        #self.ewd.logkt(self.key,f"sendTextFrame #{snowt}\n")
        #status, img = camera.read()
        #self.pil_img.paste((10,10,10), (0,0, self.pil_img.size[0], self.pil_img.size[1]))
        #image.paste( (200,200,200), (0, 0, image.size[0], image.size[1]))
            
        #draw = ImageDraw.Draw(self.pil_img)
        #draw.multiline_text((0,0), text, font=self.renderFont, fill=(200,200,0))
        #lines = 0
        #for l in text.split("\n"):
        #      timg = draw.text((10,10+lines*20), l, font=self.renderFont)
        #      lines = lines + 1
        
        #timg = np.asarray(self.pil_img)
        #timg = cv2.cvtColor(timg,cv2.COLOR_RGB2BGR)

        timg = np.copy(self.img)
        bodyFont = self.fontCache.getCachedFont(self.fontCodeBody)
        bfbox = bodyFont.renderBox
        #atext = unidecode(text,'replace',replace_str=".")
        atext = text
        lines = 0
        cols = 0
        for i in range(0,len(atext)):
            ch = "" + atext[i]
            if ch == "\n":
                lines = lines+1
                cols = 0
            else:
                if ch != " ":
                    #timg = cv2.putText(timg,text=ch,org=(cols*10,12+lines*20), fontFace=2, fontScale=.5,color=(0,200,200),thickness=1)
                    #self.ewd.logkt(self.key,f"DRAW({ch},{cols*bfbox[1]},{lines*bfbox[0]},{timg.shape})")
                    bodyFont.drawCodepoint(ch,timg,int(cols*bfbox[1]*1),int(lines*bfbox[0]*1))
                cols = cols+1

        xup = 4
        #self.ewd.logkt(self.key,f"dtoi(D {snowd})")
        self.drawTextOnImage(timg, snowt,
                             (self.slw+8,self.height-self.fontCodeTimeSize-xup),
                             self.fontCache.getCachedFont(self.fontCodeTime))
        self.drawTextOnImage(timg, snowd,
                             (self.slw+6,self.height-self.fontCodeTimeSize-self.fontCodeDateSize30-2*xup),
                             self.fontCache.getCachedFont(self.fontCodeDate))

        #self.ewd.logkt(self.key,f"dtoi(T {snowt})")
        #self.ewd.logkt(self.key,f"dtoi()")
        #timg = shadowText(timg,snowd, (self.slw+6,self.height-12-24-xup), (0,180,180),fontscale=.75)
        #timg = shadowText(timg,snowt, (self.slw+6,self.height-10-xup), (0,180,180),fontscale=.85)
        timg = overlay_transparent(timg, self.smlogo, 4, self.height-self.slh-8)
        #print(chopt.shape,timg.shape)
        # write to pipe
        try:
            #self.ewd.logkt(self.key,f"TRYWRITE({timg.shape})")
            towrite = timg.tobytes()
            blockSize = 1<<20
            for i in range(0, len(towrite), blockSize):
                chunk = towrite[i : i + blockSize]
                self.subproc.stdin.write(chunk)
                self.bytesWritten += len(chunk)
            #self.subproc.stdin.flush()
        except socket.error as e:
            if e.errno != errno.EPIPE:
                raise
            self.ewd.logkt(self.key,f"BORKEN PIPE {e} AFTER {self.bytesWritten}")
            self.restartSubProc()


    @staticmethod
    def fontSizeForTextWidth(path,nchars,pxwidth,aspect=.5):
        """The font size whose cached glyphs (FontCache.CachedFont: cell
        width = aspect * (ascent+descent)) make nchars characters about
        pxwidth wide."""
        best,bestdiff = 12,None
        for size in range(12,400):
            asc,desc = ImageFont.truetype(path,size).getmetrics()
            diff = abs(nchars*round(aspect*(asc+desc)) - pxwidth)
            if bestdiff is None or diff < bestdiff:
                best,bestdiff = size,diff
            elif diff > bestdiff + 60:
                break
        return best

    def drawStaleBanner(self,bgrimage,stale):
        """NO DATA over the middle of the frame, with the seconds since
        the last real frame under it, in a dark box with a red border."""
        font = self.fontCache.getCachedFont(self.fontCodeStale)
        ch,cw = font.renderBox
        lines = [self.staleText,f"{int(stale)}s"]
        boxw = max(len(t) for t in lines)*cw
        boxh = len(lines)*ch
        pad,bord = 18,4
        x0 = (self.width-boxw)//2
        y0 = (self.height-boxh)//2
        xa,ya = max(0,x0-pad),max(0,y0-pad)
        xb,yb = min(self.width,x0+boxw+pad),min(self.height,y0+boxh+pad)
        bgrimage[ya:yb,xa:xb] = (0x12,0x14,0x12)
        red = (0x04,0x2f,0xef)                  # BGR
        bgrimage[ya:ya+bord,xa:xb] = red
        bgrimage[yb-bord:yb,xa:xb] = red
        bgrimage[ya:yb,xa:xa+bord] = red
        bgrimage[ya:yb,xb-bord:xb] = red
        for i,t in enumerate(lines):
            tx = x0 + (boxw-len(t)*cw)//2       # each line centred
            self.drawTextOnImage(bgrimage,t,(tx,y0+i*ch),font)

    def renderOverlay(self,bgrimage,simnanos,info,stale=None,extra=""):
        """Burn the overlays (info, LIVE timer, UTC clock, logo) into
        bgrimage.  stale is None for a real frame, else the seconds since
        the last real one (a repeated frame): then a NO DATA line is
        added.  extra, if not empty, is a line of its own (the live AER)
        between the info line below and the LIVE line above."""
        with self.overlayLock:
            self.renderOverlayLocked(bgrimage,simnanos,info,stale,extra)

    def renderOverlayLocked(self,bgrimage,simnanos,info,stale,extra=""):
        now = datetime.datetime.now(datetime.timezone.utc)
        snowd = now.strftime("%Y-%m-%d")
        snowt = now.strftime("%H:%M:%SUTC")

        simmsec = simnanos//1_000_000
        simsec = simmsec//1_000
        simmin = simsec//60
        simhr = simmin//60
        simday = simhr//24
        simweek = simday//7

        elmsec = simmsec%1_000
        elsec = simsec%60
        elmin = simmin%60
        elhr = simhr%24
        elday = simday%7
        elweek = simweek

        el = f"{elsec:d}s"
        if simmin > 0:
            el = f"{elmin:d}m "+el
        if simhr > 0:
            el = f"{elhr:d}h "+el
        if simday > 0:
            el = f"{elday:d}d "+el
        if simweek > 0:
            el = f"{elweek:d}w "+el
        
        richardBaseheight = 94
        dickWidth = 6
        self.drawTextOnImage(bgrimage,info,
                             (dickWidth,self.height-richardBaseheight),
                             self.fontCache.getCachedFont(self.fontCodeLive))
        lineh = self.fontCodeLiveSize+4  # was +2: bracket tops of [140] were clipped
        liveY = self.height-richardBaseheight-lineh
        if extra:
            self.drawTextOnImage(bgrimage,extra,(dickWidth,liveY),
                                 self.fontCache.getCachedFont(self.fontCodeLive))
            liveY -= lineh

        if simmsec%13579 > 1234:
            self.magicNum = random.randint(0,50)
            scy = ""
            if self.scyFn is not None:
                try:
                    n = self.scyFn()
                    if n:
                        scy = f"  scy {n}"
                except Exception:
                    pass            # the overlay must never cost the stream
            self.drawTextOnImage(bgrimage,f"LIVE {el}{scy}",
                                 (dickWidth,liveY),
                                 self.fontCache.getCachedFont(self.fontCodeLive))
        elif self.magicNum < 4:
            mantras = ("living == trying",
                       "same picture",
                       "trying == living",
                       "means to try")
            man = mantras[self.magicNum]
            self.drawTextOnImage(bgrimage,man,
                                 (dickWidth,liveY),
                                 self.fontCache.getCachedFont(self.fontCodeLive))
            

        overlay_transparent(bgrimage, self.smlogo, 4, self.height-self.slh-8)

        xup = 5
        self.drawTextOnImage(bgrimage, snowd,
                             (self.slw+8,int(self.height-1.5*self.fontCodeTimeSize-self.fontCodeDateSize-2*xup)),
                             self.fontCache.getCachedFont(self.fontCodeDate))
        self.drawTextOnImage(bgrimage, snowt,
                             #(1920//2,1080//2),
                             (self.slw+12,int(self.height-1.5*self.fontCodeTimeSize-xup)),
                             self.fontCache.getCachedFont(self.fontCodeTime))

        if False:
            self.drawTextOnImage(bgrimage, " living==trying ",
                                 (self.slw+1,self.height-self.fontCodeTimeSize-xup-2),
                                 self.fontCache.getCachedFont(self.fontCodeTime))


        if stale is not None and stale >= 1.5:
            self.drawStaleBanner(bgrimage,stale)

    def sendGraphicsFrame(self,bgrimage,simnanos,info,extra=""): # fully rendered BGR image as numpy array
        # keep the bare image so the pacer can redraw a NO DATA overlay
        self.lastBase = bgrimage.copy()
        self.lastSimNanos = simnanos
        self.lastInfo = info
        self.lastExtra = extra
        self.lastRealMono = time.monotonic()
        self.renderOverlay(bgrimage, simnanos, info, None, extra)
        self.framesSentThisSubproc += 1
        data = np.ascontiguousarray(bgrimage).tobytes()
        self.lastBytes = data
        with self.pendLock:
            if self.pendBytes is not None:
                self.statDrops += 1     # superseded before its tick
            self.pendBytes = data
        if self.keepThread is None:
            self.keepThread = threading.Thread(target=self.pacer,
                                               daemon=True)
            self.keepThread.start()
            
