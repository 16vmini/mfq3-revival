#!/usr/bin/env python3
# Auto-generate a MFQ3 vehicle shadow: a 64x64 top-down silhouette TGA (nose up),
# soft alpha, matching gfx/shadows/<model>Shadow.tga. Usage: gen_shadow.py body.md3 out.tga
import sys, numpy as np
sys.path.insert(0,"C:/source/mfq3/_incoming")
from render_md3 import load_md3, gather
from PIL import Image, ImageDraw
md3, out = sys.argv[1], sys.argv[2]
m=load_md3(md3); v,n,t=gather(m,0)
X=v[:,0]; Y=v[:,1]                          # X=forward(nose), Y=left
SS=512
mnx,mxx=X.min(),X.max(); mny,mxy=Y.min(),Y.max()
span=max(mxx-mnx,mxy-mny)*1.12; cx=(mxx+mnx)/2; cy=(mxy+mny)/2
def px(vx,vy):
    sx=(vy-cy)/span*SS + SS/2               # model Y  -> screen x
    sy=SS/2 - (vx-cx)/span*SS               # model X(nose) -> screen up
    return (sx,sy)
msk=Image.new('L',(SS,SS),0); d=ImageDraw.Draw(msk)
for a,b,c in t:
    try: d.polygon([px(X[a],Y[a]),px(X[b],Y[b]),px(X[c],Y[c])],fill=255)
    except: pass
ma=np.array(msk.resize((64,64),Image.LANCZOS))     # anti-aliased downscale
rgba=np.zeros((64,64,4),np.uint8)
# blendFunc GL_ZERO GL_ONE_MINUS_SRC_COLOR darkens ground by (1-src): bright shape = strong shadow
v=(ma.astype(np.float32)*0.82).astype(np.uint8)    # silhouette bright on black bg
rgba[...,0]=rgba[...,1]=rgba[...,2]=v; rgba[...,3]=255
Image.fromarray(rgba,'RGBA').save(out)
print("wrote %s  (%d%% covered)"%(out, int((ma>20).mean()*100)))
