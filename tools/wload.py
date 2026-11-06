import struct,numpy as np,pickle
import os,sys; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from config import *
tex=pickle.load(open(BUILD+'/texlist.pkl','rb'))
def load():
    b=open(ROMDISK+'/world.bin','rb').read()
    _,nc,nb,nv,ni=struct.unpack_from('<4sIIII',b,0)
    o=20+nc*32; B=[struct.unpack_from('<HHIIII',b,o+20*i) for i in range(nb)]; o+=nb*20
    V=np.frombuffer(b,dtype=np.dtype([('p','<f4',3),('uv','<f4',2),('c','<u4')]),count=nv,offset=o); o+=nv*24
    I=np.frombuffer(b,'<u2',count=ni,offset=o)
    tris=[];tid=[];tr=[]
    for t,mode,vo,n,io,cnt in B:
        T=I[io:io+cnt].reshape(-1,3).astype(np.int64)+vo; tris.append(V['p'][T]); tid+= [t]*len(T); tr+=[bool(mode&16)]*len(T)
    return np.concatenate(tris),np.array(tid),np.array(tr)
