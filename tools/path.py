import pickle,numpy as np,struct
import os,sys; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from config import *
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
R,_=pickle.load(open(BUILD+'/road.pkl','rb'))
WP=[(175,300),(175,100),(175,-100),(175,-300),(165,-400),(120,-490),(60,-525),(0,-528),(-70,-515),(-120,-482),(-145,-432),(-150,-380),(-150,-320),(-152,-262),(-165,-200),(-200,-160),(-240,-138),(-300,-125),(-370,-90),(-405,0),(-380,80),(-310,125),(-230,125),(-150,125),(-60,150),(-10,200),(0,250),(0,350),(5,440),(40,505),(100,530),(150,510),(172,460),(175,400)]
P=[]
for x,z in WP:
    d=np.hypot(R[:,0]-x,R[:,2]-z); m=d<22
    P.append([R[m,0].mean(),R[m,1].mean(),R[m,2].mean()] if m.sum()>=3 else [x,np.nan,z])
P=np.array(P); n=len(P); ok=~np.isnan(P[:,1]); idx=np.arange(n)
P[~ok,1]=np.interp(idx[~ok],np.r_[idx[ok]-n,idx[ok],idx[ok]+n],np.r_[P[ok,1],P[ok,1],P[ok,1]])
def cr(p0,p1,p2,p3,t): return 0.5*((2*p1)+(-p0+p2)*t+(2*p0-5*p1+4*p2-p3)*t*t+(-p0+3*p1-3*p2+p3)*t**3)
S=[]
for i in range(n):
    for t in np.linspace(0,1,40,endpoint=False): S.append(cr(P[i-1],P[i],P[(i+1)%n],P[(i+2)%n],t))
S=np.array(S); Q=np.vstack([S,S[:1]])
s=np.r_[0,np.cumsum(np.linalg.norm(np.diff(Q,axis=0),axis=1))]; t=np.arange(0,s[-1],1.0)
Z=np.stack([np.interp(t,s,Q[:,i]) for i in range(3)],1)
print('vuelta %.0f unidades, %d puntos, altura %.1f..%.1f'%(s[-1],len(Z),Z[:,1].min(),Z[:,1].max()))
open(ROMDISK+'/path.bin','wb').write(struct.pack('<4sI',b'SCRP',len(Z))+Z.astype('<f4').tobytes())
plt.figure(figsize=(5,6)); plt.plot(R[:,0],R[:,2],',',color='0.6'); plt.plot(Z[:,0],Z[:,2],'r-',lw=1); plt.plot(P[:,0],P[:,2],'b.')
plt.gca().set_aspect('equal'); plt.savefig('/tmp/path.png',dpi=70)
# per-point height from road directly below
Y=Z[:,1].copy()
for i,p in enumerate(Z):
    d=np.hypot(R[:,0]-p[0],R[:,2]-p[2]); m=d<7
    if m.sum()>=2: Y[i]=np.median(R[m,1])
for _ in range(20): Y=(np.roll(Y,1)+2*Y+np.roll(Y,-1))/4
Z[:,1]=Y
print('altura corregida %.1f..%.1f'%(Y.min(),Y.max()))
open(ROMDISK+'/path.bin','wb').write(struct.pack('<4sI',b'SCRP',len(Z))+Z.astype('<f4').tobytes())
