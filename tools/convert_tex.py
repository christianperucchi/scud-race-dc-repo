import sys,pickle,struct,numpy as np; sys.path.insert(0,'.')
import os,sys; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from config import *
from pvrtex import *
SD=SCUD_DIR
tex=pickle.load(open(BUILD+'/texlist.pkl','rb'))
ent=[];blob=b'';raw=0
for n in tex:
    rgba,pf,df=decode_pvr(SD+n+'.pvr'); h,w=rgba.shape[:2]
    alpha={1:0,0:1,2:2}[pf]
    if w==h and w>=8:
        data=encode_vq_mip(rgba,alpha); fmt=alpha|4|8
    else:
        data=encode_twiddled(rgba,alpha); fmt=alpha
    raw+=w*h*2
    pad=(-len(blob))%32; blob+=b'\0'*pad
    ent.append((fmt,w,h,len(blob),len(data))); blob+=data
with open(ROMDISK+'/tex.bin','wb') as f:
    f.write(struct.pack('<4sI',b'SCRT',len(ent)))
    for x in ent: f.write(struct.pack('<IHHII',*x))
    f.write(blob)
vq=sum(1 for x in ent if x[0]&4)
print('texturas %d (VQ+mip %d, twiddled %d); original %.2f MB -> %.2f MB en VRAM'%(len(ent),vq,len(ent)-vq,raw/1e6,len(blob)/1e6))
