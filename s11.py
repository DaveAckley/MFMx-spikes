#!/usr/bin/env python
import time
import datetime
import subprocess
import numpy as np
import cv2

rtmp_url = "rtmp://100.111.186.67:1935/live/test"

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
command = ['ffmpeg', '-y', '-re', '-f', 'rawvideo', '-vcodec', 'rawvideo', '-pix_fmt', 'bgr24', '-s', "{}x{}".format(width, height), '-r', str(fps), '-vsync', '2', '-i', '-', '-c:v', 'libx264', '-g', str(goplen), '-x264-params', 'no-scenecut=1', '-pix_fmt', 'yuv420p', '-preset', 'ultrafast', '-f', 'flv', rtmp_url]

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
      ret = cv2.putText(img=ret,text=text, org=(org[0]-o,org[1]+o), fontFace=2, fontScale=fs, color=dim, thickness=1)
      ret = cv2.putText(img=ret,text=text, org=(org[0]+o,org[1]-o), fontFace=2, fontScale=fs, color=bri, thickness=1)
      ret = cv2.putText(img=ret,text=text, org=(org[0]+0,org[1]+0), fontFace=2, fontScale=fs, color=col, thickness=1)      
      return ret

run = True
while run:
      now = datetime.datetime.now()
      snow = now.strftime("%Y%m%d-%H%M%S")
      #status, img = camera.read()
      if b < 255:
            b = b + 1
      else:
            b = 0
      img[:,:,0] = b            
      timg = shadowText(np.copy(img),snow, (8,height-12), (0,180,180))
      # write to pipe
      p.stdin.write(timg.tobytes())
      time.sleep(1/(fps+5))
