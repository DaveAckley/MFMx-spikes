from unidecode import unidecode
#from PIL import ImageFont, ImageDraw, Image
import time
import datetime
import subprocess
import numpy as np
import cv2

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
      def __init__(self,stream='test'):
            self.logo_path = "logo/logotype-lcf-chop-16-yellow-on-transparent-shadow.png"
            logo = cv2.imread(self.logo_path,-1) # neg arg to keep alpha
            lw,lh = logo.shape[0:2]
            pctsize=1.48
            self.slw,self.slh = int(pctsize*lw/100),int(pctsize*lh/100)
            self.smlogo = cv2.resize(logo,(self.slw,self.slh),interpolation=cv2.INTER_AREA)
            print("smlogo",self.smlogo.shape)
            self.rtmp_url = "rtmp://100.111.186.67:1935/live/"+stream

            h,w = 1080,1920
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

            #gather video info to ffmpeg
            self.fps = int(10)
            self.height = self.img.shape[0]
            self.width = self.img.shape[1]
            self.goplen = 10*self.fps # max ten seconds to I?
            self.ffmpegCommand = [
                  'ffmpeg', '-y',
                  '-re',
                  '-f', 'rawvideo', '-vcodec', 'rawvideo', '-pix_fmt', 'bgr24',
                  '-s', "{}x{}".format(self.width, self.height), '-r', str(self.fps), '-vsync', '2', '-i', '-',
                  '-c:v', 'libx264', '-g', str(self.goplen), '-x264-params', 'no-scenecut=1',
                  '-pix_fmt', 'yuv420p', '-preset', 'ultrafast', '-f', 'flv', '-flvflags', 'no_duration_filesize',
                  self.rtmp_url
            ]

            print(f"SIZE {self.width} x {self.height}")
            print(f"RUN {self.ffmpegCommand}")

            #using subprocess and pipe to fetch frame data
            self.subproc = subprocess.Popen(self.ffmpegCommand, stdin=subprocess.PIPE)
            print(f"PROC IS {self.subproc}")

      def sendTextFrame(self,text):
            now = datetime.datetime.now(datetime.timezone.utc)
            snowd = now.strftime("%Y%m%d")
            #snowt = now.strftime("%H%M%S%Z")
            snowt = now.strftime("%H:%M:%S")
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
            atext = unidecode(text,'replace',replace_str=".")
            lines = 0
            cols = 0
            for i in range(0,len(atext)):
                  ch = atext[i]
                  if ch == '\n':
                        lines = lines+1
                        cols = 0
                  else:
                        if ch != ' ':
                              timg = cv2.putText(timg,text=ch,org=(cols*10,12+lines*20), fontFace=2, fontScale=.5,color=(0,200,200),thickness=1)
                        cols = cols+1

            xup = 4
            timg = shadowText(timg,snowd, (self.slw+6,self.height-12-24-xup), (0,180,180),fontscale=.75)
            timg = shadowText(timg,snowt, (self.slw+6,self.height-10-xup), (0,180,180),fontscale=.85)
            timg = overlay_transparent(timg, self.smlogo, 4, self.height-self.slh-8)
            #print(chopt.shape,timg.shape)
            # write to pipe
            self.subproc.stdin.write(timg.tobytes())

