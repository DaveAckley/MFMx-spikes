#from unidecode import unidecode
import random
import FontCache
from PIL import ImageFont, ImageDraw, Image
import time
import datetime
import subprocess
import numpy as np
import cv2

import socket
import errno

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

class RTMPFeed:
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
        self.rtmp_url = f"rtmp://vidsrv:1935/live/{stream}?user=rosesbh&pass=wah-wah-ditty-d0"

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
        self.fontCodeLive = self.fontCache.getFontCode(f"{basedir}/fonts/NK57Monospace/NK57 Monospace Cd Rg.otf",
                                                       self.fontCodeLiveSize,
                                                       (0xef,0x2f,0x04), (0x12,0x14,0x12), preload=False)

        #gather video info to ffmpeg
        #self.fps = int(10)
        self.fps = int(5)
        self.height = self.img.shape[0]
        self.width = self.img.shape[1]
        self.secsPerI = 2
        self.goplen = int(self.secsPerI*self.fps)
        self.ffmpegCommand = [
            'ffmpeg', '-y',
            #'-re',
            '-f', 'rawvideo', '-vcodec', 'rawvideo', '-pix_fmt', 'bgr24',
            '-s', "{}x{}".format(self.width, self.height),
            '-framerate', str(self.fps),   # 'input option' ?
            '-vsync', '2', '-i', '-',
            '-r', str(self.fps),           # 'output option' ?
            '-c:v', 'libx264', '-g', str(self.goplen), '-x264-params', 'no-scenecut=1',
            '-pix_fmt', 'yuv420p',
            '-preset', 'ultrafast',
            #'-preset', 'veryfast',
            #'-maxrate', '4M',
            #'-bufsize', '1M',
            '-f', 'flv', '-flvflags', 'no_duration_filesize',
            self.rtmp_url
        ]

        self.ewd.logkt(self.key,f"SIZE {self.width} x {self.height}")
        self.ewd.logkt(self.key,f"RUN {self.ffmpegCommand}")

        self.ewd.logkt(self.key,f"GORMO")
        self.subproc_started = 0
        self.restartSubProc();
        self.ewd.logkt(self.key,f"SLORG")
        self.ewd.logkt(self.key,f"MADEFONTS({self.fontCodeTime},{self.fontCodeDate})")

        self.ewd.logkt(self.key,f"XX {self.subproc_started}")
        self.bytesWritten = 0

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
        #using subprocess and pipe to fetch frame data
        self.ewd.logkt(self.key,f"STARTING {self.ffmpegCommand}")
        self.subproc = subprocess.Popen(self.ffmpegCommand,
                                        stdin=subprocess.PIPE,
                                        stdout=subprocess.DEVNULL, 
                                        stderr=subprocess.STDOUT)
        self.subproc_started += 1
        self.ewd.logkt(self.key,f"SUBPROC #{self.subproc_started} IS {self.subproc}")
        self.ewd.logkt(self.key,"CLAMS!")

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


    def sendGraphicsFrame(self,bgrimage,simnanos,info): # fully rendered BGR image as numpy array
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

        if simmsec%13579 > 1234:
            self.magicNum = random.randint(0,50)
            self.drawTextOnImage(bgrimage,f"LIVE {el}",
                                 (dickWidth,self.height-richardBaseheight-self.fontCodeLiveSize-2),
                                 self.fontCache.getCachedFont(self.fontCodeLive))
        elif self.magicNum < 4:
            mantras = ("living == trying",
                       "same picture",
                       "trying == living",
                       "means to try")
            man = mantras[self.magicNum]
            self.drawTextOnImage(bgrimage,man,
                                 (dickWidth,self.height-richardBaseheight-self.fontCodeLiveSize-2),
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


        self.framesSentThisSubproc += 1
        try:
            #self.ewd.logkt(self.key,f"TRYWRITE({timg.shape})")
            towrite = bgrimage
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
            
