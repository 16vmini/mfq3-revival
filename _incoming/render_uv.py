#!/usr/bin/env python3
# Export a model's UV layout (the "flattened" unwrap) as a PNG template - the
# wireframe of every triangle in texture space. Hand this to an AI/artist to paint
# a new texture that lines up with the model. Optionally filter to one material.
#   render_uv.py model.obj out.png [materialSubstr]
import sys
from PIL import Image, ImageDraw
obj, out = sys.argv[1], sys.argv[2]
only = sys.argv[3] if len(sys.argv) > 3 else None
VT=[]; faces=[]; cur='default'; mats={}
for l in open(obj):
    if l.startswith('vt '): p=l.split(); VT.append((float(p[1]), float(p[2])))
    elif l.startswith('usemtl '): cur=l.split(None,1)[1].strip()
    elif l.startswith('f '):
        toks=l.split()[1:]
        vts=[int(t.split('/')[1])-1 for t in toks if len(t.split('/'))>1 and t.split('/')[1]]
        for i in range(1,len(vts)-1): faces.append((cur, vts[0], vts[i], vts[i+1]))
        mats[cur]=mats.get(cur,0)+1
S=2048; img=Image.new('RGB',(S,S),(18,18,24)); d=ImageDraw.Draw(img)
# light grid
for k in range(0,S+1,S//8): d.line([(k,0),(k,S)],fill=(40,40,52)); d.line([(0,k),(S,k)],fill=(40,40,52))
PAL=[(120,200,255),(255,160,90),(150,255,150),(255,120,200),(230,230,120),(120,255,230)]
mlist=sorted(mats); cmap={m:PAL[i%len(PAL)] for i,m in enumerate(mlist)}
def px(vt):
    u,v=VT[vt]; u-=int(u) if u>1 or u<0 else 0; v-=int(v) if v>1 or v<0 else 0
    return (u*S,(1-v)*S)
drawn=0
for mtl,a,b,c in faces:
    if only and only.lower() not in mtl.lower(): continue
    try:
        pts=[px(a),px(b),px(c)]; d.line(pts+[pts[0]],fill=cmap.get(mtl,(120,200,255)),width=1); drawn+=1
    except: pass
img.save(out)
print("wrote %s  (%d UV tris drawn, %d materials)"%(out,drawn,len(mats)))
big=sorted(mats.items(),key=lambda x:-x[1])[:8]
print("top materials by tri count:"); [print("  %-22s %d"%(m,n)) for m,n in big]
