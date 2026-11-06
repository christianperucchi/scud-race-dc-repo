import sys,pickle,struct,collections,numpy as np
import os,sys; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from config import *
sys.path.insert(0,'.')
from nj import *
from pvrtex import decode_pvr
SD=SCUD_DIR
e=Exe(SD+'scud.exe'); ms,trees=pickle.load(open(BUILD+'/scene.pkl','rb'))
tex=pickle.load(open(BUILD+'/texlist.pkl','rb'))
texfmt={t:decode_pvr(SD+t+'.pvr')[1] for t in tex}
L=np.array([0.35,0.85,0.4]); L/=np.linalg.norm(L)
CELL=48.0
batches=collections.defaultdict(lambda:{'v':{},'vl':[],'i':[]})
roadpts=[]
cache={}
for root,inst in trees[:2]:
    for mdl,W in inst:
        if mdl not in cache: cache[mdl]=read_model(e,mdl)
        P,T,U,mid,M=cache[mdl]
        if not T: continue
        pts,nrm,nbp=e.rd('IIi',mdl)
        N=np.array(e.rd('%df'%(3*nbp),nrm)).reshape(-1,3) if nrm else None
        Pw=(np.c_[P,np.ones(len(P))]@W.T)[:,:3]
        if N is not None:
            Nw=N@W[:3,:3].T; Nw/=np.linalg.norm(Nw,axis=1,keepdims=True)+1e-9
        for t,uv,mi in zip(T,U,mid):
            dif,spe,ex,tid,fl=M[mi]; tid&=0x3fff; tn=tex[tid]
            tri=Pw[list(t)]
            fn=np.cross(tri[1]-tri[0],tri[2]-tri[0]); ln=np.linalg.norm(fn)
            if ln<1e-9: continue
            fn/=ln
            alpha=bool(fl&(1<<20)); dbl=bool(fl&(1<<23)); nolight=bool(fl&(1<<25))
            mode=(1 if fl&(1<<16) else 0)|(2 if fl&(1<<15) else 0)|(4 if fl&(1<<18) else 0)|(8 if fl&(1<<17) else 0)|(16 if alpha else 0)
            c=tri.mean(0); key=(int(c[0]//CELL),int(c[2]//CELL),tid,mode)
            b=batches[key]
            if tn.startswith('bri_rd'): roadpts.append(c)
            da=(dif>>24)&255; dr,dg,db=(dif>>16)&255,(dif>>8)&255,dif&255
            idx=[]
            for k in range(3):
                n=Nw[t[k]] if N is not None else fn
                d=n@L
                lit=1.0 if nolight else 0.5+0.55*(abs(d) if dbl else max(0.0,d))
                col=(da<<24)|(min(255,int(dr*lit))<<16)|(min(255,int(dg*lit))<<8)|min(255,int(db*lit))
                u,v=(uv[k][0]/255.0,uv[k][1]/255.0) if uv else (0.0,0.0)
                vert=(round(tri[k][0],3),round(tri[k][1],3),round(tri[k][2],3),round(u,4),round(v,4),col)
                if vert not in b['v']: b['v'][vert]=len(b['vl']); b['vl'].append(vert)
                idx.append(b['v'][vert])
            b['i'].extend(idx)

# ---- open sea: fine 12-unit squares where there is no water and no dry floor below sea level
MIZU=tex.index('ca_cw_mizu1'); SKIP={tex.index(t) for t in ('iwa_00','sango04','sango06','wave_far')}
floor=[];water=[]
for key,bb in list(batches.items()):
    if key[2] in SKIP: continue
    vl=np.array([v[:3] for v in bb['vl']]); I3=np.array(bb['i']).reshape(-1,3)
    tri=vl[I3]
    if key[2]==MIZU: water.append(tri); continue
    nn=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]); nn/=np.linalg.norm(nn,axis=1,keepdims=True)+1e-9
    m=(np.abs(nn[:,1])>0.6)&(tri[:,:,1].max(1)<19.5)&(tri[:,:,1].min(1)>-5)
    floor.append(tri[m])
def build_hash(tris,cs=12.0):
    H=collections.defaultdict(list)
    for i,t in enumerate(tris):
        for gx in range(int(t[:,0].min()//cs),int(t[:,0].max()//cs)+1):
            for gz in range(int(t[:,2].min()//cs),int(t[:,2].max()//cs)+1): H[(gx,gz)].append(i)
    return H
def inside(tris,H,x,z,cs=12.0):
    for i in H.get((int(x//cs),int(z//cs)),[]):
        a,b2,c=tris[i][:,[0,2]]; v0,v1,v2=c-a,b2-a,np.array([x,z])-a
        d00,d01,d11,d20,d21=v0@v0,v0@v1,v1@v1,v2@v0,v2@v1; den=d00*d11-d01*d01
        if abs(den)<1e-9: continue
        u=(d11*d20-d01*d21)/den; w=(d00*d21-d01*d20)/den
        if u>=-0.02 and w>=-0.02 and u+w<=1.02: return True
    return False
FL=np.concatenate(floor); WA=np.concatenate(water); HF=build_hash(FL); HW=build_hash(WA)
SQ=12.0; nsea=0; col=0xFF5A8CA0; y=19.6
def add_quad(x0,z0,x1,z1):
    c=(int(((x0+x1)/2)//CELL),int(((z0+z1)/2)//CELL)); bb=batches[(c[0],c[1],MIZU,0)]
    q=[(x0,y,z0,x0/24,z0/24,col),(x1,y,z0,x1/24,z0/24,col),(x0,y,z1,x0/24,z1/24,col),(x1,y,z1,x1/24,z1/24,col)]
    ids=[]
    for v in q:
        if v not in bb['v']: bb['v'][v]=len(bb['vl']); bb['vl'].append(v)
        ids.append(bb['v'][v])
    bb['i'].extend([ids[0],ids[1],ids[2],ids[2],ids[1],ids[3]])
IX0,IX1,IZ0,IZ1=-768,480,-720,816
for gx in range(int(-1536//CELL),int(1296//CELL)):
    for gz in range(int(-1536//CELL),int(1584//CELL)):
        x0,z0=gx*CELL,gz*CELL
        if x0>=IX0 and x0<IX1 and z0>=IZ0 and z0<IZ1:
            occ=[[not(inside(WA,HW,x0+sx*SQ+SQ/2,z0+sz*SQ+SQ/2) or inside(FL,HF,x0+sx*SQ+SQ/2,z0+sz*SQ+SQ/2)) for sz in range(4)] for sx in range(4)]
            if all(all(r) for r in occ): add_quad(x0,z0,x0+CELL,z0+CELL); nsea+=1; continue
            for sx in range(4):
                sz=0
                while sz<4:
                    if not occ[sx][sz]: sz+=1; continue
                    e2=sz
                    while e2<4 and occ[sx][e2]: e2+=1
                    add_quad(x0+sx*SQ,z0+sz*SQ,x0+(sx+1)*SQ,z0+e2*SQ); nsea+=1; sz=e2
        else:
            add_quad(x0,z0,x0+CELL,z0+CELL); nsea+=1
print('cuadros de mar abierto',nsea)

# ---- seabed: opaque rock floor under the whole water area (y=-36), split per cell
SEA_TID=tex.index('iwa_00')
for gx in range(int(-720//CELL)-1,int(410//CELL)+1):
    for gz in range(int(-650//CELL)-1,int(760//CELL)+1):
        x0,z0=gx*CELL,gz*CELL; x1,z1=x0+CELL,z0+CELL
        key=(gx,gz,SEA_TID,0); b=batches[key]
        col=0xFF2E5868; y=-36.0
        q=[(x0,y,z0,0,0,col),(x1,y,z0,3,0,col),(x0,y,z1,0,3,col),(x1,y,z1,3,3,col)]
        ids=[]
        for v in q:
            if v not in b['v']: b['v'][v]=len(b['vl']); b['vl'].append(v)
            ids.append(b['v'][v])
        b['i'].extend([ids[0],ids[1],ids[2],ids[2],ids[1],ids[3]])

# split batches that exceed 65535 verts (unlikely) and group by cell
cells=collections.defaultdict(list)
for key,b in batches.items(): cells[key[:2]].append((key,b))
V=[];I=[];BT=[];CL=[]
for ck in sorted(cells):
    first=len(BT); mn=np.full(3,1e9); mx=np.full(3,-1e9)
    for key,b in sorted(cells[ck],key=lambda kb:(kb[0][3]>>4,kb[0][2])):
        vl=np.array([v[:3] for v in b['vl']]); mn=np.minimum(mn,vl.min(0)); mx=np.maximum(mx,vl.max(0))
        BT.append((key[2],key[3],len(V),len(b['vl']),len(I),len(b['i'])))
        V.extend(b['vl']); I.extend(b['i'])
    CL.append((*mn,*mx,first,len(BT)-first))
with open(ROMDISK+'/world.bin','wb') as f:
    f.write(struct.pack('<4sIIII',b'SCRW',len(CL),len(BT),len(V),len(I)))
    for c in CL: f.write(struct.pack('<6fII',*c))
    for b in BT: f.write(struct.pack('<HHIIII',*b))
    for v in V: f.write(struct.pack('<5fI',*v))
    f.write(np.array(I,'<u2').tobytes())
    if len(I)%2: f.write(b'\0\0')
tris=len(I)//3
print('celdas',len(CL),'lotes',len(BT),'vértices',len(V),'triángulos',tris,'MB %.2f'%((len(V)*24+len(I)*2)/1e6))
pickle.dump((np.array(roadpts),texfmt),open(BUILD+'/road.pkl','wb'))
