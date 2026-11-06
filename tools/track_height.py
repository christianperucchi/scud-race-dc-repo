import sys,struct,numpy as np,collections; sys.path.insert(0,'.')
import os,sys; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from config import *
from wload import *
T,tid,tr=load(); names=np.array(tex)[tid].astype(str)
n=np.cross(T[:,1]-T[:,0],T[:,2]-T[:,0]); n/=np.linalg.norm(n,axis=1,keepdims=True)+1e-9
up=(np.abs(n[:,1])>0.85)&~tr&(T[:,:,1].min(1)>-30)&~np.char.startswith(names,'bri_bri1')&(names!='ca_cw_mizu1')
U=T[up]; G=collections.defaultdict(list); CS=8.0
for i,t in enumerate(U):
    x0,x1=t[:,0].min(),t[:,0].max(); z0,z1=t[:,2].min(),t[:,2].max()
    for gx in range(int(x0//CS),int(x1//CS)+1):
        for gz in range(int(z0//CS),int(z1//CS)+1): G[(gx,gz)].append(i)
def heights(x,z):
    out=[]
    for i in G.get((int(x//CS),int(z//CS)),[]):
        a,b,c=U[i][:,[0,2]]; p=np.array([x,z])
        v0,v1,v2=c-a,b-a,p-a; d00,d01,d11,d20,d21=v0@v0,v0@v1,v1@v1,v2@v0,v2@v1
        den=d00*d11-d01*d01
        if abs(den)<1e-9: continue
        v=(d11*d20-d01*d21)/den; w=(d00*d21-d01*d20)/den
        if v>=-1e-4 and w>=-1e-4 and v+w<=1+1e-4:
            out.append(U[i][0,1]*(1-v-w)+U[i][2,1]*v+U[i][1,1]*w)
    return out
b=open(ROMDISK+'/path.bin','rb').read(); N=struct.unpack_from('<I',b,4)[0]
P=np.frombuffer(b,'<f4',offset=8).reshape(N,3).copy()
# seed: a point on plain road (stadium side) where only one level exists
H=[heights(p[0],p[2]) for p in P]
seed=next(i for i,h in enumerate(H) if len(h)==1 and abs(h[0])<3)
Y=np.full(N,np.nan); Y[seed]=H[seed][0]
for direction in (1,-1):
    prev=Y[seed]
    for k in range(1,N):
        i=(seed+direction*k)%N; c=[h for h in H[i] if abs(h-prev)<2.5]
        if c: prev=min(c,key=lambda h:abs(h-prev)); 
        if np.isnan(Y[i]) or direction==1: Y[i]=prev if c else np.nan
        if direction==1 and not c: Y[i]=np.nan
miss=np.isnan(Y); idx=np.arange(N)
Y[miss]=np.interp(idx[miss],idx[~miss],Y[~miss])
for _ in range(8): Y=(np.roll(Y,1)+2*Y+np.roll(Y,-1))/4
print('sin superficie: %d de %d puntos; altura %.1f..%.1f'%(miss.sum(),N,Y.min(),Y.max()))
for i in range(0,N,250): print(i,'xz=(%.0f,%.0f) y=%.1f niveles=%s'%(P[i,0],P[i,2],Y[i],[round(h,1) for h in sorted(H[i])][:5]))
P[:,1]=Y
open(ROMDISK+'/path.bin','wb').write(struct.pack('<4sI',b'SCRP',N)+P.astype('<f4').tobytes())
