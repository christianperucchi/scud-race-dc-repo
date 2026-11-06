import sys,numpy as np; sys.path.insert(0,'.')
import os,sys; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from config import *
from pvrtex import encode_twiddled
from PIL import Image,ImageDraw,ImageFont
f=ImageFont.load_default()
img=Image.new('RGBA',(128,128),(0,0,0,0)); d=ImageDraw.Draw(img)
for c in range(32,128):
    i=c-32; x=(i%16)*8; y=(i//16)*12
    for dx,dy in ((1,1),(1,0),(0,1)): d.text((x+dx,y+dy),chr(c),font=f,fill=(0,0,0,255))
    d.text((x,y),chr(c),font=f,fill=(255,255,255,255))
a=np.array(img); open(ROMDISK+'/font.bin','wb').write(encode_twiddled(a,2)); img.resize((256,256)).save('/tmp/font.png')
print('ok',len(encode_twiddled(a,2)))
