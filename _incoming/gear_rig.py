#!/usr/bin/env python3
# Interactive gear-rig editor (tkinter): load a model + its gear legs, scrub the
# gear up/down, drag each leg's hinge X/Y/Z + fold + roll, see it live, then Save
# (writes obj2md3 -GearFold/-GearRoll/-GearHinge and optionally rebuilds the md3).
# Defaults to the Vulcan. Coords shown are body-local SCALED (== -GearHinge units).
import sys, math, os, subprocess, numpy as np
import tkinter as tk
from PIL import Image, ImageDraw, ImageTk

OBJ   = sys.argv[1] if len(sys.argv)>1 else r"C:\source\mfq3\_incoming\vulcan.obj"
LEGF  = sys.argv[2] if len(sys.argv)>2 else r"C:\source\mfq3\_incoming\vulcan_gearlegs.txt"
YAW,PITCH,ROLL = 270.0,0.0,0.0
TARGET = 150.0
OUTDIR = r"C:\source\mfq3\play\mfdata\models\vehicles\planes\vulcan"
MODEL  = "vulcan"
SHADER = "models/vehicles/planes/vulcan/krilo_i10_FACES"
LEGNAMES = ["Nose","Left main","Right main"]

# ---- load + transform to body-local SCALED (same frame as -GearHinge) ----
V=[]; faces=[]; cur='?'
for l in open(OBJ):
    if l.startswith('v '): p=l.split(); V.append((float(p[1]),float(p[2]),float(p[3])))
    elif l.startswith('g '): cur=l[2:].strip()
    elif l.startswith('f '):
        idx=[int(t.split('/')[0])-1 for t in l.split()[1:]]
        for i in range(1,len(idx)-1): faces.append((cur,idx[0],idx[i],idx[i+1]))
V=np.array(V)
B=np.column_stack([V[:,0],-V[:,2],V[:,1]])
c,s=math.cos(math.radians(YAW)),math.sin(math.radians(YAW)); G=np.column_stack([B[:,0]*c-B[:,1]*s,B[:,0]*s+B[:,1]*c,B[:,2]])
cp,sp=math.cos(math.radians(PITCH)),math.sin(math.radians(PITCH)); G=np.column_stack([G[:,0]*cp+G[:,2]*sp,G[:,1],-G[:,0]*sp+G[:,2]*cp])
cr,sr=math.cos(math.radians(ROLL)),math.sin(math.radians(ROLL)); G=np.column_stack([G[:,0],G[:,1]*cr+G[:,2]*sr,-G[:,1]*sr+G[:,2]*cr])
ctr=(G.max(0)+G.min(0))/2; scale=TARGET/(G.max(0)-G.min(0)).max()
P=(G-ctr)*scale                      # body-local scaled verts

legs=[set() for _ in range(3)]
legstr=open(LEGF).read().strip().split(';')
for li,grp in enumerate(legstr):
    names=set(g.strip() for g in grp.split(',') if g.strip())
    for g,a,b,cc in faces:
        if g in names: legs[li].update((a,b,cc))
gearset=set().union(*legs)
vleg={}
for li in range(3):
    for v in legs[li]: vleg[v]=li
bodyF=[(a,b,cc) for g,a,b,cc in faces if a not in gearset]
gearF=[(vleg[a],a,b,cc) for g,a,b,cc in faces if a in gearset]

def auto_hinge(idxs):
    pts=P[list(idxs)]; mxz=pts[:,2].max(); mnz=pts[:,2].min(); thr=mxz-0.25*(mxz-mnz)
    top=pts[pts[:,2]>=thr]; return np.array([top[:,0].mean(),top[:,1].mean(),mxz])
hinge=[auto_hinge(legs[i]) for i in range(3)]

# ---- state ----
st={'amt':100.0,'az':120.0,'el':14.0}
for i in range(3):
    st[f'fold{i}']=(90.0 if i==0 else -85.0); st[f'roll{i}']=0.0   # nose aft 90, mains fwd
    st[f'hx{i}'],st[f'hy{i}'],st[f'hz{i}']=hinge[i]

def gear_xform(li,a):
    h=np.array([st[f'hx{li}'],st[f'hy{li}'],st[f'hz{li}']])
    fold=math.radians(st[f'fold{li}']*a); roll=math.radians(st[f'roll{li}']*a)
    pr=P[list(legs[li])]-h
    crr,srr=math.cos(roll),math.sin(roll); cs,ss=math.cos(fold),math.sin(fold)
    x1=pr[:,0]*crr-pr[:,1]*srr; y1=pr[:,0]*srr+pr[:,1]*crr; z1=pr[:,2]
    out=np.column_stack([h[0]+x1*cs+z1*ss, h[1]+y1, h[2]-x1*ss+z1*cs])
    return dict(zip(legs[li],out))

S=560
_bc={'key':None,'img':None}                      # body image cache per view
def render():
    a=st['amt']/100.0
    A,E=math.radians(st['az']),math.radians(st['el'])
    fwd=np.array([math.cos(A)*math.cos(E),math.sin(A)*math.cos(E),math.sin(E)]);fwd/=np.linalg.norm(fwd)
    right=np.cross([0,0,1.0],fwd);right/=np.linalg.norm(right);up=np.cross(fwd,right)
    basis=np.column_stack([right,up,fwd])
    prAll=P@basis                                # stable framing from full model
    span=(prAll[:,:2].max(0)-prAll[:,:2].min(0)).max(); cc=(prAll[:,:2].max(0)+prAll[:,:2].min(0))/2; sc=S*0.8/span
    key=(round(st['az'],1),round(st['el'],1))
    if _bc['key']!=key:                          # re-render + cache body only on view change
        bimg=Image.new('RGB',(S,S),(245,247,250)); d=ImageDraw.Draw(bimg)
        sx=(prAll[:,0]-cc[0])*sc+S/2; sy=S/2-(prAll[:,1]-cc[1])*sc; dep=prAll[:,2]
        bt=sorted(bodyF,key=lambda t:-(dep[t[0]]+dep[t[1]]+dep[t[2]]))
        for a2,b2,c2 in bt:
            try: d.polygon([(sx[a2],sy[a2]),(sx[b2],sy[b2]),(sx[c2],sy[c2])],fill=(150,155,165))
            except: pass
        _bc['key']=key; _bc['img']=bimg
    img=_bc['img'].copy(); d=ImageDraw.Draw(img)  # gear drawn live on top of cached body
    pos={}
    for li in range(3): pos.update(gear_xform(li,a))
    vs=list(pos.keys()); garr=np.array([pos[v] for v in vs])@basis; gm={v:i for i,v in enumerate(vs)}
    gsx=(garr[:,0]-cc[0])*sc+S/2; gsy=S/2-(garr[:,1]-cc[1])*sc; gd=garr[:,2]
    gt=sorted(gearF,key=lambda t:-(gd[gm[t[1]]]+gd[gm[t[2]]]+gd[gm[t[3]]]))
    for li,a2,b2,c2 in gt:
        ia,ib,ic=gm[a2],gm[b2],gm[c2]
        try: d.polygon([(gsx[ia],gsy[ia]),(gsx[ib],gsy[ib]),(gsx[ic],gsy[ic])],fill=(205,120,55))
        except: pass
    return img

# ---- UI ----
root=tk.Tk(); root.title("MFQ3 gear rig - %s"%MODEL)
cv=tk.Canvas(root,width=S,height=S,bg='white'); cv.grid(row=0,column=0)
# scrollable control panel (sliders were running off the bottom)
_cc=tk.Canvas(root,width=300,height=S,highlightthickness=0); _cc.grid(row=0,column=1,sticky='ns')
_sb=tk.Scrollbar(root,orient='vertical',command=_cc.yview); _sb.grid(row=0,column=2,sticky='ns')
_cc.configure(yscrollcommand=_sb.set)
ctrl=tk.Frame(_cc); _cc.create_window((0,0),window=ctrl,anchor='nw')
ctrl.bind('<Configure>',lambda e:_cc.configure(scrollregion=_cc.bbox('all')))
_cc.bind_all('<MouseWheel>',lambda e:_cc.yview_scroll(int(-e.delta/120),'units'))
imgref={'i':None}
_pending={'id':None}
def redraw(*_):
    img=render(); imgref['i']=ImageTk.PhotoImage(img); cv.create_image(0,0,anchor='nw',image=imgref['i'])
def schedule(*_):
    if _pending['id']: root.after_cancel(_pending['id'])
    _pending['id']=root.after(40,redraw)
def slider(label,key,frm,to,res=1.0):
    f=tk.Frame(ctrl); f.pack(anchor='w',padx=4)
    tk.Label(f,text=label,width=10,anchor='w').pack(side='left')
    sc=tk.Scale(f,from_=frm,to=to,orient='horizontal',length=200,resolution=res,
                command=lambda v,k=key:(st.__setitem__(k,float(v)),schedule()))
    sc.set(st[key]); sc.pack(side='left')
tk.Label(ctrl,text="VIEW / GEAR",font=('',10,'bold')).pack(anchor='w',pady=(6,0))
slider("Gear up%","amt",0,100); slider("View az","az",0,360); slider("View el","el",-30,80)
for i in range(3):
    tk.Label(ctrl,text=LEGNAMES[i],font=('',10,'bold')).pack(anchor='w',pady=(8,0))
    slider("fold deg",f'fold{i}',-150,150); slider("roll deg",f'roll{i}',-120,120)
    slider("hinge X",f'hx{i}',-80,80); slider("hinge Y",f'hy{i}',-90,90); slider("hinge Z",f'hz{i}',-40,40,0.5)

def params():
    fold=';'.join('%.0f'%st[f'fold{i}'] for i in range(3))
    roll=';'.join('%.0f'%st[f'roll{i}'] for i in range(3))
    hin =';'.join('%.1f,%.1f,%.1f'%(st[f'hx{i}'],st[f'hy{i}'],st[f'hz{i}']) for i in range(3))
    return fold,roll,hin
def save(rebuild):
    fold,roll,hin=params()
    legstr_raw=open(LEGF).read().strip()
    txt=('-GearFold "%s" -GearRoll "%s" -GearHinge "%s"'%(fold,roll,hin))
    open(r"C:\source\mfq3\_incoming\%s_gear_params.txt"%MODEL,'w').write(txt+"\n")
    print("SAVED params:",txt)
    if rebuild:
        cmd=["powershell.exe","-NoProfile","-Command",
             "& 'C:\\source\\mfq3\\_incoming\\obj2md3.ps1' -ObjPath '%s' -OutMd3 '%s\\%s.md3' "
             "-ShaderName '%s' -TargetSize %d -MaxVerts 8000 -Yaw %g -Pitch %g -Roll %g -FlipV:$false -PerMaterial "
             "-GearLegs '%s' -GearOut '%s\\%s_gear.md3' -GearTag tag_gear -GearFrames 48 "
             "-GearFold '%s' -GearRoll '%s' -GearHinge '%s'"%(
             OBJ,OUTDIR,MODEL,SHADER,int(TARGET),YAW,PITCH,ROLL,legstr_raw,OUTDIR,MODEL,fold,roll,hin)]
        status.config(text="rebuilding md3..."); root.update()
        r=subprocess.run(cmd,capture_output=True,text=True)
        status.config(text="rebuilt %s.md3 + gear  (reload in game)"%MODEL if r.returncode==0 else "REBUILD FAILED - see console")
        if r.returncode: print(r.stdout[-1500:],r.stderr[-500:])
    else: status.config(text="params saved to %s_gear_params.txt"%MODEL)
bf=tk.Frame(ctrl); bf.pack(anchor='w',pady=10)
tk.Button(bf,text="Save params",command=lambda:save(False)).pack(side='left',padx=4)
tk.Button(bf,text="Save & rebuild MD3",command=lambda:save(True),bg='#cde').pack(side='left',padx=4)
status=tk.Label(ctrl,text="drag sliders; 0=down 100=up. scroll panel for more.",fg='#555',wraplength=280); status.pack(anchor='w')
redraw(); root.mainloop()
