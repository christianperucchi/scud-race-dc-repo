import numpy as np,glob,re,wave
SR=22050
T={}
for fn in glob.glob('tele_*.log'):
    for line in open(fn,'rb').read().decode('latin-1').splitlines():
        if line.startswith('T '):
            p=line.split()
            if len(p)==12: T[int(p[1])]=[float(x) for x in p[2:]]
f0,f1=map(int,open('video_range.txt').read().split())
frames=range(f0-1,f1+1)
out=[]; ph1=ph2=lp1=lp2=nlp=nhp=crash=bph=0.0; rng=12345; acc=0.0
for f in frames:
    rf,rpm,thr,slip,wind,cr,airpm,aivol,beep,cam=T.get(f,T[max(k for k in T if k<f)])
    thr/=1000; slip/=1000; wind/=1000; cr/=1000; aivol/=1000
    if cr>crash: crash=cr
    acc+=SR/60.0; n=int(acc); acc-=n
    fr1=rpm/60*3; fr2=airpm/60*3*1.013; a1=0.30+0.22*thr; sk=slip*0.55; wd=wind*0.22
    buf=np.empty(n)
    for i in range(n):
        ph1+=fr1/SR; ph1-=ph1>=1
        ph2+=fr2/SR; ph2-=ph2>=1
        e1=(ph1*2-1)*0.55+(0.25 if ph1<0.5 else -0.25)+(((ph1*0.5+0.25)%1.0)*2-1)*0.3
        e2=(ph2*2-1)*0.6+(0.2 if ph2<0.5 else -0.2)
        lp1+=(e1-lp1)*0.33; lp2+=(e2-lp2)*0.25
        rng=(rng*1664525+1013904223)&0xffffffff
        s=rng-(1<<32) if rng&0x80000000 else rng
        nz=(s>>8)/8388608.0
        nlp+=(nz-nlp)*0.12; nhp+=(nz-nhp)*0.6
        o=lp1*a1+lp2*aivol+(nhp-nlp)*sk+nlp*wd+nz*crash*0.9
        crash*=0.99965
        if beep>0:
            bph+=beep/SR; bph-=bph>=1; o+=0.35 if bph<0.5 else -0.35
        buf[i]=max(-1,min(1,o))
    out.append(buf)
a=(np.concatenate(out)*26000).astype('<i2')
w=wave.open('audio.wav','wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(a.tobytes()); w.close()
print('audio %.1f s'%(len(a)/SR))
