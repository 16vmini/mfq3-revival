#!/usr/bin/env python3
# Extract per-material baseColor textures from a GLB -> one JPG per material,
# named by the sanitized material name (matching obj2md3 -PerMaterial shaders).
# Solid-color materials (no texture) get a small flat-color JPG. Resizes to <=512
# (Q3 hunk can't take 2048^2 -> Z_Malloc crash). Usage: extract_glb_tex.py in.glb outdir
import sys,os,json,struct,io,re
from PIL import Image
glb,outdir=sys.argv[1],sys.argv[2]
os.makedirs(outdir,exist_ok=True)
with open(glb,'rb') as f: data=f.read()
assert data[:4]==b'glTF'
p=12; js=None; bin_=None
while p<len(data):
    clen,ctype=struct.unpack_from('<II',data,p); p+=8
    chunk=data[p:p+clen]; p+=clen
    if ctype==0x4E4F534A: js=json.loads(chunk)
    elif ctype==0x004E4942: bin_=chunk
g=js
bv=g.get('bufferViews',[]); imgs=g.get('images',[]); texs=g.get('textures',[]); mats=g.get('materials',[])
def img_bytes(im):
    if 'bufferView' in im:
        v=bv[im['bufferView']]; o=v.get('byteOffset',0); return bin_[o:o+v['byteLength']]
    return None
def san(n): return re.sub(r'[^A-Za-z0-9]','_',n)
print("%d materials, %d textures, %d images"%(len(mats),len(texs),len(imgs)))
for mi,m in enumerate(mats):
    name=san(m.get('name') or 'mat%d'%mi)
    pbr=m.get('pbrMetallicRoughness',{})
    out=os.path.join(outdir,name+'.jpg')
    bct=pbr.get('baseColorTexture')
    if bct is not None:
        ti=bct['index']; ii=texs[ti]['source']; b=img_bytes(imgs[ii])
        if b:
            im=Image.open(io.BytesIO(b)).convert('RGB')
            if max(im.size)>512: im=im.resize((min(512,im.width),min(512,im.height)))
            im.save(out,'JPEG',quality=90)
            print("  %-22s tex %dx%d -> %s"%(name,im.width,im.height,name+'.jpg')); continue
    # no texture: solid color from baseColorFactor
    bcf=pbr.get('baseColorFactor',[0.6,0.6,0.6,1])
    col=tuple(int(max(0,min(1,c))*255) for c in bcf[:3])
    Image.new('RGB',(8,8),col).save(out,'JPEG',quality=90)
    print("  %-22s solid %s -> %s"%(name,col,name+'.jpg'))
