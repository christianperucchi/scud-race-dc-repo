"""Step 5: find the two cars (Porsche, F40) and their texlists inside the SET2 drive2 demo.exe."""
import os,sys,re,pickle; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from config import *
from nj import *
e=Exe(DRIVE2_EXE)
ms=find_models(e); objs=find_objects(e,ms)
refd=set(v[5] for v in objs.values())|set(v[6] for v in objs.values())
trees=sorted([(r,walk(objs,r)) for r in objs if r not in refd],key=lambda t:-len(t[1]))
names={e.base+m.start():m.group().rstrip(b'\0').decode() for m in re.finditer(rb'[a-zA-Z0-9_.]{3,40}\x00',e.img)}
starts=[]
for va in range(e.d0,e.d1-12,4):
    if e.rd('I',va)[0] in names and e.rd('I',va-12)[0] not in names:
        n=0
        while e.rd('I',va+12*n)[0] in names: n+=1
        if n>=3: starts.append((va,[names[e.rd('I',va+12*i)[0]] for i in range(n)]))
pickle.dump(trees,open(BUILD+'/drive2_trees.pkl','wb')); pickle.dump(starts,open(BUILD+'/drive2_texlists.pkl','wb'))
print('trees',[len(t[1]) for t in trees[:3]],'texlists',[(hex(v),len(l)) for v,l in starts])
