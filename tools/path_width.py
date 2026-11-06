import sys,struct,numpy as np,collections; sys.path.insert(0,'.')
import os,sys; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from config import *
from wload import *
T,tid,tr=load(); names=np.array(tex)[tid].astype(str)
n=np.cross(T[:,1]-T[:,0],T[:,2]-T[:,0]); n/=np.linalg.norm(n,axis=1,keepdims=True)+1e-9
keep=(np.abs(n[:,1])>0.8)&~tr&(T[:,:,1].min(1)>-30)&(names!='ca_cw_mizu1')
U=T[keep]; CS=6.0; G=collections.defaultdict(list)
for i,t in enumerate(U):
    for gx in range(int(t[:,0].min()//CS),int(t[:,0].max()//CS)+1):
        for gz in range(int(t[:,2].min()//CS),int(t[:,2].max()//CS)+1): G[(gx,gz)].append(i)
A=U[:,0][:,[0,2]]; B=U[:,1][:,[0,2]]; C=U[:,2][:,[0,2]]
def surf(x,z,y):
    for i in G.get((int(x//CS),int(z//CS)),[]):
        a,b,c=A[i],B[i],C[i]; v0,v1,v2=c-a,b-a,np.array([x,z])-a
        d00,d01,d11,d20,d21=v0@v0,v0@v1,v1@v1,v2@v0,v2@v1; den=d00*d11-d01*d01
        if abs(den)<1e-9: continue
        u=(d11*d20-d01*d21)/den; w=(d00*d21-d01*d20)/den
        if u>=-1e-3 and w>=-1e-3 and u+w<=1.001:
            h=U[i][0,1]*(1-u-w)+U[i][2,1]*u+U[i][1,1]*w
            if abs(h-y)<1.2: return True
    return False
b=open(ROMDISK+'/path.bin','rb').read(); N=struct.unpack_from('<I',b,4)[0]
P=np.frombuffer(b,'<f4',offset=8).reshape(N,3).copy()
tg=np.roll(P,-3,0)-np.roll(P,3,0); tg[:,1]=0; tg/=np.linalg.norm(tg,axis=1,keepdims=True)
left=np.stack([-tg[:,2],np.zeros(N),tg[:,0]],1)   # +d = left
WL=np.zeros(N);WR=np.zeros(N)
for i in range(N):
    for side,arr in ((1,WL),(-1,WR)):
        o=0.0
        while o<22 and surf(P[i,0]+left[i,0]*side*(o+0.5),P[i,2]+left[i,2]*side*(o+0.5),P[i,1]): o+=0.5
        arr[i]=o
# robust smoothing (median then mean), clamp
from numpy.lib.stride_tricks import sliding_window_view as sw
def med(a,k=15): ap=np.r_[a[-k:],a,a[:k]]; return np.median(sw(ap,2*k+1),axis=1)
WL=med(WL);WR=med(WR)
for _ in range(10): WL=(np.roll(WL,1)+2*WL+np.roll(WL,-1))/4; WR=(np.roll(WR,1)+2*WR+np.roll(WR,-1))/4
WL=np.clip(WL,3.0,16.0); WR=np.clip(WR,3.0,16.0)
print('ancho izq %.1f..%.1f (media %.1f)  der %.1f..%.1f (media %.1f)'%(WL.min(),WL.max(),WL.mean(),WR.min(),WR.max(),WR.mean()))
out=np.c_[P,WL,WR].astype('<f4')
open(ROMDISK+'/path.bin','wb').write(struct.pack('<4sI',b'SCP2',N)+out.tobytes())
