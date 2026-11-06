import numpy as np, struct
def twid_tab(n):
    t=np.zeros(n,np.int64)
    for i in range(n):
        r=0;b=0;v=i
        while v: r|=(v&1)<<(2*b); v>>=1; b+=1
        t[i]=r
    return t
def twiddle_index(w,h):
    """index (y,x) -> linear position in twiddled storage (rect = blocks of min side)."""
    m=min(w,h); T=twid_tab(m)
    ys,xs=np.mgrid[0:h,0:w]
    inner=(T[ys%m]|(T[xs%m]<<1))
    blk=(xs//m) if w>h else (ys//m)
    return inner+blk*m*m
def decode_pvr(path):
    b=open(path,'rb').read(); i=b.find(b'PVRT')
    sz,pf,df,_,w,h=struct.unpack_from('<IBBHHH',b,i+4)
    d=np.frombuffer(b[i+16:i+8+sz],'<u2')
    if df in (1,2):
        top=d[len(d)-w*h:]
        px=top[twiddle_index(w,h)]
    else:
        px=d[:w*h].reshape(h,w)
    px=px.astype(np.uint32)
    if pf==1:   r,g,bl,a=(px>>11&31)*255//31,(px>>5&63)*255//63,(px&31)*255//31,np.full_like(px,255)
    elif pf==0: r,g,bl,a=(px>>10&31)*255//31,(px>>5&31)*255//31,(px&31)*255//31,np.where(px>>15,255,0)
    else:       r,g,bl,a=(px>>8&15)*17,(px>>4&15)*17,(px&15)*17,(px>>12)*17
    return np.stack([r,g,bl,a],-1).astype(np.uint8),pf,df
def pack(rgba,alpha):
    r,g,b,a=[rgba[...,i].astype(np.uint32) for i in range(4)]
    if alpha==0: return ((r>>3)<<11)|((g>>2)<<5)|(b>>3)
    if alpha==1: return ((a>=128).astype(np.uint32)<<15)|((r>>3)<<10)|((g>>3)<<5)|(b>>3)
    return ((a>>4)<<12)|((r>>4)<<8)|((g>>4)<<4)|(b>>4)
def mips(img):
    out=[img.astype(np.float32)]
    while out[-1].shape[0]>1:
        a=out[-1]; out.append((a[0::2,0::2]+a[1::2,0::2]+a[0::2,1::2]+a[1::2,1::2])/4)
    return out[::-1]   # smallest first
def kmeans(X,k=256,it=12,seed=1):
    rng=np.random.default_rng(seed)
    if len(X)<=k: C=np.vstack([X,np.repeat(X[:1],k-len(X),0)]); return C,np.arange(len(X))
    C=X[rng.choice(len(X),k,replace=False)].copy()
    for _ in range(it):
        lab=np.empty(len(X),np.int64)
        for s in range(0,len(X),8192):
            Xs=X[s:s+8192]; d=(Xs**2).sum(1)[:,None]-2*Xs@C.T+(C**2).sum(1)[None]
            lab[s:s+8192]=d.argmin(1)
        for j in range(k):
            m=lab==j
            if m.any(): C[j]=X[m].mean(0)
            else: C[j]=X[rng.integers(len(X))]
    return C,lab
def blocks(img):
    """img HxW x4 -> (h/2*w/2) x 16 vectors in twiddled block order, texel order TL,BL,TR,BR"""
    h,w=img.shape[:2]
    if h==1: v=np.concatenate([img[0,0]]*4)[None]; return v
    q=np.stack([img[0::2,0::2],img[1::2,0::2],img[0::2,1::2],img[1::2,1::2]],2).reshape(h//2,w//2,16)
    idx=twiddle_index(w//2,h//2).ravel(); out=np.empty((h//2*w//2,16),np.float32)
    out[idx]=q.reshape(-1,16); return out
def encode_vq_mip(img,alpha):
    lv=mips(img)
    vecs=[blocks(l) for l in lv]          # level0 (1x1) gets one block (replicated texel)
    allv=np.vstack(vecs)
    wgt=np.array([1,1,1,0.6 if alpha else 0.0]*4,np.float32)
    C,lab=kmeans(allv*wgt)
    C=C/np.where(wgt==0,1,wgt)
    if not alpha: C[:,3::4]=255
    cb=np.clip(C.reshape(256,4,4),0,255).astype(np.uint8)
    cb16=pack(cb,alpha).astype('<u2').tobytes()
    return cb16+lab.astype(np.uint8).tobytes()   # 1x1(1)+2x2(1)+4x4(4)+...
def encode_twiddled(img,alpha):
    h,w=img.shape[:2]; px=pack(img,alpha); out=np.empty(w*h,np.uint16)
    out[twiddle_index(w,h).ravel()]=px.ravel(); return out.astype('<u2').tobytes()
