#!/usr/bin/env python
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
command = ['ffmpeg', '-y', '-re', '-f', 'rawvideo', '-vcodec', 'rawvideo', '-pix_fmt', 'bgr24', '-s', "{}x{}".format(width, height), '-r', str(fps), '-vsync', '2', '-i', '-', '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-preset', 'ultrafast', '-f', 'flv', rtmp_url]

print(f"SIZE {width} x {height}")
print(f"RUN {command}")

#using subprocess and pipe to fetch frame data
p = subprocess.Popen(command, stdin=subprocess.PIPE)

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
      timg = cv2.putText(img=np.copy(img),text=snow, org=(200,200), fontFace=3, fontScale=1, color=(0,200,200), thickness=3)
      # write to pipe
      p.stdin.write(timg.tobytes())
