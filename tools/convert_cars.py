import sys,pickle,struct,collections,numpy as np; sys.path.insert(0,'.')
import os,sys; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from config import *
from nj import *
from pvrtex import encode_vq_mip, encode_twiddled
from PIL import Image
D=DRIVE2_EXE
PACK=DRIVE2_TEX
e=Exe(D); trees=pickle.load(open(BUILD+'/drive2_trees.pkl','rb'))
tl=dict((hex(va),l) for va,l in pickle.load(open(BUILD+'/drive2_texlists.pkl','rb')))
CARS=[('porsche',0x48a820,tl['0x4744c0'],('por_h_light',)),('f40',0x49e440,tl['0x48a858'],('mcl_h_light1_on','f40_number'))]
L=np.array([0.3,0.9,0.3]); L/=np.linalg.norm(L)
alltex=[]; texidx={}
def tex_id(name):
    if name not in texidx:
        texidx[name]=len(alltex); alltex.append(name)
    return texidx[name]
models=[]
for cname,root,texl,front_tex in CARS:
    inst=dict(trees)[root] if root in dict(trees) else None
    inst=[t for r,t in trees if r==root][0]
    tris=[]
    for mdl,W in inst:
        P,T,U,mid,M=read_model(e,mdl)
        if not T: continue
        pts,nrm,nbp=e.rd('IIi',mdl)
        N=np.array(e.rd('%df'%(3*nbp),nrm)).reshape(-1,3) if nrm else None
        Pw=(np.c_[P,np.ones(len(P))]@W.T)[:,:3]
        Nw=None
        if N is not None: Nw=N@W[:3,:3].T; Nw/=np.linalg.norm(Nw,axis=1,keepdims=True)+1e-9
        for t,uv,mi in zip(T,U,mid):
            dif,spe,ex,tid,fl=M[mi]; tn=texl[tid&0x3fff] if (fl>>21)&1 else None
            tris.append((Pw[list(t)],Nw[list(t)] if Nw is not None else None,uv,dif,fl,tn))
    # orientation: headlights at +z
    hz=[tr[0][:,2].mean() for tr in tris if tr[5] in front_tex]
    flip=bool(hz) and np.mean(hz)<0
    batches=collections.defaultdict(lambda:{'v':{},'vl':[],'i':[]})
    for P3,N3,uv,dif,fl,tn in tris:
        if flip: P3=P3*np.array([-1,1,-1]); N3=None if N3 is None else N3*np.array([-1,1,-1])
        fn=np.cross(P3[1]-P3[0],P3[2]-P3[0]); fn/=np.linalg.norm(fn)+1e-9
        env=bool(fl&(1<<22)); alpha=bool(fl&(1<<20)); dbl=bool(fl&(1<<23))
        mode=(1 if fl&(1<<16) else 0)|(2 if fl&(1<<15) else 0)|(4 if fl&(1<<18) else 0)|(8 if fl&(1<<17) else 0)|(16 if alpha else 0)|(32 if tn is None else 0)
        if env: mode&=~3
        key=(tex_id(tn) if tn else 0,mode); b=batches[key]
        da=(dif>>24)&255; dr,dg,db=(dif>>16)&255,(dif>>8)&255,dif&255
        ids=[]
        for k in range(3):
            n=N3[k] if N3 is not None else fn
            lit=0.55+0.5*max(0.0,float(n@L))
            if env: dr=dg=db=255
            col=(da<<24)|(min(255,int(dr*lit))<<16)|(min(255,int(dg*lit))<<8)|min(255,int(db*lit))
            if env: u,v=0.5+0.48*n[0],0.5-0.48*n[1]
            elif uv: u,v=uv[k][0]/255.0,uv[k][1]/255.0
            else: u=v=0.0
            vt=(round(float(P3[k][0]),3),round(float(P3[k][1]),3),round(float(P3[k][2]),3),round(float(u),4),round(float(v),4),col)
            if vt not in b['v']: b['v'][vt]=len(b['vl']); b['vl'].append(vt)
            ids.append(b['v'][vt])
        b['i'].extend(ids)
    models.append((cname,batches,flip))
    allv=np.array([v[:3] for b in batches.values() for v in b['vl']])
    print(cname,'tris',len(tris),'lotes',len(batches),'girado',flip,'bbox',allv.min(0).round(2),allv.max(0).round(2))
# textures
ent=[];blob=b''
for n in alltex:
    im=np.array(Image.open(PACK+n+'.pvr.png').convert('RGBA')); h,w=im.shape[:2]
    alpha=0 if im[...,3].min()==255 else (1 if set(np.unique(im[...,3]))<= {0,255} else 2)
    if w==h and w>=8: data=encode_vq_mip(im,alpha); fmt=alpha|12
    else: data=encode_twiddled(im,alpha); fmt=alpha
    blob+=b'\0'*((-len(blob))%32); ent.append((fmt,w,h,len(blob),len(data))); blob+=data
with open(ROMDISK+'/cars.bin','wb') as f:
    f.write(struct.pack('<4sII',b'SCRC',len(ent),len(models)))
    for x in ent: f.write(struct.pack('<IHHII',*x))
    for cname,batches,flip in models:
        B=list(batches.items()); V=[];I=[];hdr=[]
        for (t,mode),b in B: hdr.append((t,mode,len(V),len(b['vl']),len(I),len(b['i']))); V+=b['vl']; I+=b['i']
        f.write(struct.pack('<III',len(hdr),len(V),len(I)))
        for h in hdr: f.write(struct.pack('<HHIIII',*h))
        for v in V: f.write(struct.pack('<5fI',*v))
        f.write(np.array(I,'<u2').tobytes())
        if len(I)%2: f.write(b'\0\0')
    blob_off=f.tell(); f.write(blob)
print('texturas',len(ent),alltex,'KB VRAM',len(blob)//1024)
