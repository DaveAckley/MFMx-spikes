#!/usr/bin/env python
from textual.demo.demo_app import DemoApp

#from PIL import Image
import time
import datetime
import subprocess
import numpy as np
import cv2

logo_path = "logo/logotype-lcf-chop-16-yellow-on-transparent-shadow.png"
logo = cv2.imread(logo_path,-1) # neg arg to keep alpha
print("origlogo",logo.shape)
lw,lh = logo.shape[0:2]
pctsize=1.48
slw,slh = int(pctsize*lw/100),int(pctsize*lh/100)
smlogo = cv2.resize(logo,(slw,slh),interpolation=cv2.INTER_AREA)
print("smlogo",smlogo.shape)


rtmp_url = "rtmp://100.111.186.67:1935/live/test"

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


h,w = 1080,1920
b,g,r = 0x3e, 0x88, 0x35 # orange
img = np.zeros((h,w,3), np.uint8)
img[:,:,0] = b
img[:,:,1] = g
img[:,:,2] = r

#camera = cv2.VideoCapture(device_id)
#status, img = camera.read()

#gather video info to ffmpeg
fps = int(15)
height = img.shape[0]
width = img.shape[1]

#command and params for ffmpeg
goplen = 10*fps
command = ['ffmpeg', '-y', '-re', '-f', 'rawvideo', '-vcodec', 'rawvideo', '-pix_fmt', 'bgr24', '-s', "{}x{}".format(width, height), '-r', str(fps), '-vsync', '2', '-i', '-', '-c:v', 'libx264', '-g', str(goplen), '-x264-params', 'no-scenecut=1', '-pix_fmt', 'yuv420p', '-preset', 'ultrafast', '-f', 'flv', '-flvflags', 'no_duration_filesize', rtmp_url]

print(f"SIZE {width} x {height}")
print(f"RUN {command}")

#using subprocess and pipe to fetch frame data
p = subprocess.Popen(command, stdin=subprocess.PIPE)

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

run = True
while run:
      now = datetime.datetime.now(datetime.timezone.utc)
      snowd = now.strftime("%Y%m%d")
      #snowt = now.strftime("%H%M%S%Z")
      snowt = now.strftime("%H:%M:%S")
      #status, img = camera.read()
      if b < 255:
            b = b + 1
      else:
            b = 0
      img[:,:,0] = b            
      xup = 4
      timg = shadowText(np.copy(img),snowd, (slw+6,height-12-24-xup), (0,180,180),fontscale=.75)
      timg = shadowText(timg,snowt, (slw+6,height-10-xup), (0,180,180),fontscale=.85)
      timg = overlay_transparent(timg, smlogo, 4, height-slh-8)
      #print(chopt.shape,timg.shape)
      # write to pipe
      p.stdin.write(timg.tobytes())
      time.sleep(1/(fps+5))

def main() -> None:
      app = DemoApp()
      app.run()

if __name__ == "__main__":
      main()
