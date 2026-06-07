#!/usr/bin/env python3
from PIL import Image,ImageDraw,ImageFont
import os
base="C:/source/mfq3/_incoming"
cells=[
 ("show_sr71.png","SR-71 Blackbird","Mach-3 recon  -  animated gear (nose + 2 mains)"),
 ("show_f35.png","F-35B Lightning II","VTOL fighter  -  gear / nozzle / lift-fan animated"),
 ("show_c5.png","C-5 Galaxy","Heavy transport  -  ~340u, 4 engines"),
 ("show_reaper.png","MQ-9 Reaper","UAV  -  spinning prop + retract gear"),
]
CW,CH=460,420; PAD=26; TITLE=86; LBL=46
W=CW*2+PAD*3; H=TITLE+CH*2+LBL*2+PAD*3
img=Image.new("RGB",(W,H),(248,250,252)); d=ImageDraw.Draw(img)
def font(sz,bold=False):
    for nm in (("arialbd.ttf" if bold else "arial.ttf"),"DejaVuSans.ttf"):
        try: return ImageFont.truetype(nm,sz)
        except: pass
    return ImageFont.load_default()
def ctext(x,y,t,f,fill):
    w=d.textlength(t,font=f); d.text((x-w/2,y),t,font=f,fill=fill)
d.rectangle([0,0,W,TITLE],fill=(28,33,44))
ctext(W/2,16,"MFQ3 Revival  -  New Aircraft",font(40,True),(240,243,248))
ctext(W/2,60,"reviving a 2003 Quake-3 mod  -  models converted & animated with Claude",font(18),(150,170,200))
for i,(fn,name,sub) in enumerate(cells):
    cx=PAD+(i%2)*(CW+PAD); cy=TITLE+PAD+(i//2)*(CH+LBL+PAD)
    d.rectangle([cx,cy,cx+CW,cy+CH+LBL],fill=(255,255,255),outline=(210,215,222))
    im=Image.open(os.path.join(base,fn)).resize((CH,CH))
    img.paste(im,(cx+(CW-CH)//2,cy))
    ctext(cx+CW/2,cy+CH+4,name,font(24,True),(25,30,40))
    ctext(cx+CW/2,cy+CH+30,sub,font(14),(90,100,115))
out="C:/source/mfq3/_incoming/mfq3_aircraft_showcase.png"
img.save(out); print("wrote",out,img.size)
