"""Step 1: find the Ninja scene (models, object trees, texlist) inside the SET2 scud.exe."""
import os,sys,re,pickle; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from config import *
from nj import *
e=Exe(SCUD_DIR+'scud.exe')
ms=find_models(e); objs=find_objects(e,ms)
refd=set(v[5] for v in objs.values())|set(v[6] for v in objs.values())
trees=sorted([(r,walk(objs,r)) for r in objs if r not in refd],key=lambda t:-len(t[1]))
names={e.base+m.start():m.group().rstrip(b'\0').decode() for m in re.finditer(rb'[a-zA-Z0-9_]{3,40}(\.pvr)?\x00',e.img)}
best=None
for va in range(e.d0,e.d1-12,4):
    if e.rd('I',va)[0] not in names: continue
    n=0
    while e.rd('I',va+12*n)[0] in names: n+=1
    if n>20 and (best is None or n>best[1]): best=(va,n)
tex=[names[e.rd('I',best[0]+12*i)[0]] for i in range(best[1])]
pickle.dump((ms,trees),open(BUILD+'/scene.pkl','wb')); pickle.dump(tex,open(BUILD+'/texlist.pkl','wb'))
print('models',len(ms),'trees',[len(t[1]) for t in trees[:3]],'textures',len(tex))
