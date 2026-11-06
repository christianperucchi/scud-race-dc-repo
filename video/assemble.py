import subprocess,numpy as np,re,glob,wave,cv2
import os
# Optional: a frame showing only the X11 cursor (cursor_ref.png) lets us inpaint it away.
CMASK=None
if os.path.exists('cursor_ref.png'):
    _cur=cv2.imread('cursor_ref.png')[:472]
    CMASK=cv2.dilate(((_cur.max(2)>40)).astype(np.uint8)*255,np.ones((7,7),np.uint8))
def clean(img): return img if CMASK is None else cv2.inpaint(np.ascontiguousarray(img),CMASK,3,cv2.INPAINT_TELEA)
W,H=640,480
def frames(fn):
    p=subprocess.Popen(['ffmpeg','-loglevel','error','-i',fn,'-f','rawvideo','-pix_fmt','rgb24','-'],stdout=subprocess.PIPE)
    while True:
        b=p.stdout.read(W*H*3)
        if len(b)<W*H*3: break
        yield np.frombuffer(b,np.uint8).reshape(H,W,3)
def marker(img):
    row=img[477]; bits=[row[8+k*16+7].mean()>128 for k in range(17)]
    if not bits[16]: return None
    return sum(1<<k for k in range(16) if bits[k])
best={}; nimg=0
for seg in sorted(glob.glob('seg_*.mkv'),key=lambda s:int(re.findall(r'\d+',s)[0])):
    for img in frames(seg):
        f=marker(img); nimg+=1
        if f is not None and f not in best: best[f]=clean(img[:472])
keys=sorted(best); print('capturas',nimg,'fotogramas únicos',len(keys),'rango',keys[0],keys[-1])
# video at 30 fps: one output frame per rendered frame (even logic frames)
targets=list(range(keys[0]-keys[0]%2+2,keys[-1]+1,2))
out=subprocess.Popen(['ffmpeg','-loglevel','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s','640x472','-r','30','-i','-',
                      '-c:v','libx264','-preset','medium','-crf','18','-pix_fmt','yuv420p','video_only.mp4'],stdin=subprocess.PIPE)
ki=0; miss=0
for t in targets:
    while ki+1<len(keys) and keys[ki+1]<=t: ki+=1
    if keys[ki]!=t: miss+=1
    out.stdin.write(best[keys[ki]].tobytes())
out.stdin.close(); out.wait()
print('fotogramas de salida',len(targets),'rellenados',miss,'(%.0f%% exactos)'%(100*(1-miss/len(targets))))
open('video_range.txt','w').write('%d %d'%(targets[0],targets[-1]))
