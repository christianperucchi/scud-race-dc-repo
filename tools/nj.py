import pefile, struct, numpy as np, sys
class Exe:
    def __init__(s,f):
        s.pe=pefile.PE(f); s.base=s.pe.OPTIONAL_HEADER.ImageBase
        s.img=s.pe.get_memory_mapped_image()
        d=[x for x in s.pe.sections if x.Name.startswith(b'.data')][0]
        s.d0=s.base+d.VirtualAddress; s.d1=s.d0+d.Misc_VirtualSize
    def ok(s,p): return s.d0<=p<s.d1
    def rd(s,fmt,va): return struct.unpack_from('<'+fmt,s.img,va-s.base)
def find_models(e):
    res=[]
    for va in range(e.d0, e.d1-40, 4):
        pts,nrm,nbp,ms,mats,nms,nmat=e.rd('IIiIIHH',va)
        if not(e.ok(pts) and e.ok(ms) and 3<=nbp<20000 and 1<=nms<2000 and nmat<2000): continue
        if nrm and not e.ok(nrm): continue
        if nmat and not e.ok(mats): continue
        cx,cy,cz,r=e.rd('ffff',va+28)
        if not all(abs(v)<1e6 for v in (cx,cy,cz,r)): continue
        res.append(va)
    return res
def read_model(e,va):
    pts,nrm,nbp,ms,mats,nms,nmat=e.rd('IIiIIHH',va)
    P=np.array(e.rd('%df'%(3*nbp),pts)).reshape(-1,3)
    tris=[];uvs=[];matids=[]
    for i in range(nms):
        tm,nb,meshes,attrs,norms,vc,vuv=e.rd('HHIIIII',ms+24*i)
        typ=tm>>14; mid=tm&0x3fff
        if not e.ok(meshes): continue
        p=meshes; k=0
        for _ in range(nb):
            if typ==0: n=3; idx=e.rd('3h',p); p+=6
            elif typ==1: n=4; idx=e.rd('4h',p); p+=8
            else:
                (c,)=e.rd('h',p); p+=2; n=c&0x3fff if typ==3 else c
                idx=e.rd('%dh'%n,p); p+=2*n
            uv=None
            if vuv and e.ok(vuv): uv=[e.rd('hh',vuv+4*(k+j)) for j in range(n)]
            k+=n
            if typ==0: polys=[(0,1,2)]
            elif typ==1: polys=[(0,1,2),(2,1,3)]
            elif typ==2: polys=[(0,j,j+1) for j in range(1,n-1)]
            else: polys=[((j,j+1,j+2) if j%2==0 else (j+1,j,j+2)) for j in range(n-2)]
            for a,b,c2 in polys:
                tris.append((idx[a]&0x7fff,idx[b]&0x7fff,idx[c2]&0x7fff))
                uvs.append((uv[a],uv[b],uv[c2]) if uv else None); matids.append(mid)
    M=[e.rd('IIfII',mats+20*i) for i in range(nmat)] if nmat else []
    return P,tris,uvs,matids,M
import math
def find_objects(e, models):
    ms=set(models); objs={}
    for va in range(e.d0,e.d1-52,4):
        fl,mdl=e.rd('II',va)
        if mdl and mdl not in ms: continue
        child,sib=e.rd('II',va+44)
        if child and not e.ok(child): continue
        if sib and not e.ok(sib): continue
        pos=e.rd('3f',va+8); scl=e.rd('3f',va+32)
        if not all(abs(v)<1e6 for v in pos+scl): continue
        if mdl==0 and child==0: continue
        if not all(0.001<abs(v)<1000 for v in scl): continue
        objs[va]=(fl,mdl,pos,e.rd('3i',va+20),scl,child,sib)
    return objs
def mat(pos,ang,scl):
    def R(a): return a*2*math.pi/65536
    ax,ay,az=map(R,ang)
    cx,sx,cy,sy,cz,sz=math.cos(ax),math.sin(ax),math.cos(ay),math.sin(ay),math.cos(az),math.sin(az)
    Rx=np.array([[1,0,0],[0,cx,-sx],[0,sx,cx]]); Ry=np.array([[cy,0,sy],[0,1,0],[-sy,0,cy]]); Rz=np.array([[cz,-sz,0],[sz,cz,0],[0,0,1]])
    M=np.eye(4); M[:3,:3]=(Rz@Ry@Rx)*np.array(scl); M[:3,3]=pos   # Ninja: XYZ order (Z applied last)
    return M
def walk(objs, root, M=np.eye(4), out=None):
    out=[] if out is None else out; va=root; guard=0
    while va and va in objs and guard<100000:
        fl,mdl,pos,ang,scl,child,sib=objs[va]
        W=M@mat(pos,ang,scl)
        if mdl: out.append((mdl,W))
        if child: walk(objs,child,W,out)
        va=sib; guard+=1
    return out
