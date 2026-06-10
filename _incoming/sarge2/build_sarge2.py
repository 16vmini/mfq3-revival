#!/usr/bin/env python3
"""
Build an improved, ANIMATED low-poly soldier (LQM 'sarge') for MFQ3.
Outputs zz_lqmfix.pk3 with legs/torso/head MD3s (+TGAs +animation.cfg).

Q3 conventions: +X forward, +Y left, +Z up. Feet at z=0, tag_torso at z=25,
tag_head at torso-local z=22, head ~10-11 tall -> total ~57-58 units.

Frame layout (Q3-style global cfg numbering, parsed by CG_ParseLQMAnimationFile
which subtracts skip = LEGS_WALKCR.first - TORSO_GESTURE.first from LEGS_* lines):

  torso.md3 (16 frames):       legs.md3 (40 frames):
    0- 4 BOTH_DEATH1             0- 4 BOTH_DEATH1
    5- 6 TORSO_GESTURE           5-12 LEGS_WALKCR   (cfg 16-23)
    7- 9 TORSO_ATTACK           13-20 LEGS_WALK     (cfg 24-31)
   10    TORSO_DROP             21-28 LEGS_RUN      (cfg 32-39)
   11    TORSO_RAISE            29-31 LEGS_JUMP     (cfg 40-42)
   12-15 TORSO_STAND            32-33 LEGS_LAND     (cfg 43-44)
                                34-37 LEGS_IDLE     (cfg 45-48)
                                38-39 LEGS_IDLECR   (cfg 49-50)
"""
import math, os, struct, sys, zipfile
import numpy as np
from PIL import Image

OUT = r'C:\source\mfq3\_incoming\sarge2'
MODELDIR = 'models/vehicles/lqms/sarge'
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(1234)

# ---------------------------------------------------------------- math helpers
def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
def rot_y(a):  # pitch: a>0 tips +X (nose) down / moves a hanging limb backward
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
I3 = np.eye(3)
def xf_ident():            return (I3.copy(), np.zeros(3))
def xf_trans(t):           return (I3.copy(), np.asarray(t, float))
def xf_about(R, pivot):    p = np.asarray(pivot, float); return (R, p - R @ p)
def xf_mul(A, B):          # apply B first, then A
    Ra, ta = A; Rb, tb = B
    return (Ra @ Rb, Ra @ tb + ta)
def xf_apply(X, v):        R, t = X; return v @ R.T + t
D = math.radians

# ---------------------------------------------------------------- mesh builder
class Mesh:
    """Boxes with per-face UV rects; every vertex tagged with a segment name so
    frames can pose segments rigidly."""
    def __init__(self, texsize):
        self.tw, self.th = texsize
        self.v = []; self.n = []; self.uv = []; self.tris = []; self.seg = []

    def add_box(self, seg, bot, top, uvmap):
        """bot/top = (cx, cy, z, sx, sy) center & full sizes of bottom/top rect.
        uvmap: dict face->(u0,v0,u1,v1) px rects; faces '+x','-x','+y','-y','+z','-z','all'."""
        bcx, bcy, bz, bsx, bsy = bot
        tcx, tcy, tz, tsx, tsy = top
        def corner(level, sx_sign, sy_sign):
            if level == 0:
                return np.array([bcx + sx_sign*bsx/2, bcy + sy_sign*bsy/2, bz])
            return np.array([tcx + sx_sign*tsx/2, tcy + sy_sign*tsy/2, tz])
        # corners: b/t, +-x, +-y
        c = {}
        for lev, ln in ((0, 'b'), (1, 't')):
            for xs in (1, -1):
                for ys in (1, -1):
                    c[(ln, xs, ys)] = corner(lev, xs, ys)
        # faces as 4 corners CCW seen from outside (Q3 CW front-face culling is
        # handled by the engine; we just keep consistent winding + outward normals)
        faces = {
            '+x': [c[('t',1,1)], c[('t',1,-1)], c[('b',1,-1)], c[('b',1,1)]],
            '-x': [c[('t',-1,-1)], c[('t',-1,1)], c[('b',-1,1)], c[('b',-1,-1)]],
            '+y': [c[('t',-1,1)], c[('t',1,1)], c[('b',1,1)], c[('b',-1,1)]],
            '-y': [c[('t',1,-1)], c[('t',-1,-1)], c[('b',-1,-1)], c[('b',1,-1)]],
            '+z': [c[('t',-1,1)], c[('t',-1,-1)], c[('t',1,-1)], c[('t',1,1)]],
            '-z': [c[('b',1,1)], c[('b',1,-1)], c[('b',-1,-1)], c[('b',-1,1)]],
        }
        for fname, quad in faces.items():
            rect = uvmap.get(fname, uvmap.get('all'))
            if rect is None:
                continue
            u0, v0, u1, v1 = rect
            uvq = [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]
            nrm = np.cross(quad[1]-quad[0], quad[3]-quad[0])
            ln = np.linalg.norm(nrm); nrm = nrm/ln if ln > 1e-9 else np.array([0,0,1.0])
            base = len(self.v)
            for p, (uu, vv) in zip(quad, uvq):
                self.v.append(np.array(p, float)); self.n.append(nrm)
                self.uv.append((uu/self.tw, vv/self.th)); self.seg.append(seg)
            self.tris += [[base, base+1, base+2], [base, base+2, base+3]]

    def bake(self, segs, X):
        """Apply transform X permanently to bind verts of given segments."""
        R, t = X
        for i, s in enumerate(self.seg):
            if s in segs:
                self.v[i] = R @ self.v[i] + t
                self.n[i] = R @ self.n[i]

    def arrays(self):
        return (np.array(self.v), np.array(self.n), np.array(self.uv),
                np.array(self.tris, np.int32), list(self.seg))

# ---------------------------------------------------------------- md3 writer
def enc_normal(n):
    x, y, z = n
    if z > 0.999:  return 0
    if z < -0.999: return 128  # lng=128 -> pi
    lng = int(round(math.acos(max(-1, min(1, z))) * 255 / (2*math.pi))) & 255
    lat = int(round(math.atan2(y, x) * 255 / (2*math.pi))) & 255
    return (lat << 8) | lng

def write_md3(path, model_name, surf_name, shader, verts_frames, normals_frames,
              uv, tris, tags, frame_name='frame'):
    """verts_frames: list[nFrames] of (N,3); tags: dict name->list[nFrames] of (origin, R)
    where R maps local->model (axis rows written as R columns^T, i.e. R.T)."""
    nFrames = len(verts_frames)
    nVerts = len(uv); nTris = len(tris)
    tag_names = sorted(tags.keys())
    nTags = len(tag_names)

    # frames
    frames_bin = b''
    for f in range(nFrames):
        v = verts_frames[f]
        mn = v.min(0); mx = v.max(0)
        radius = float(np.linalg.norm(np.maximum(np.abs(mn), np.abs(mx))))
        frames_bin += struct.pack('<3f3f3ff16s', *mn, *mx, 0, 0, 0, radius,
                                  ('%s%03d' % (frame_name, f)).encode())
    # tags, frame-major
    tags_bin = b''
    for f in range(nFrames):
        for tn in tag_names:
            origin, R = tags[tn][f]
            axis = R.T  # rows = transformed basis vectors
            tags_bin += struct.pack('<64s3f9f', tn.encode(), *origin,
                                    *axis.reshape(-1))
    # surface
    shader_bin = struct.pack('<64si', shader.encode(), 0)
    tris_bin = np.asarray(tris, '<i4').tobytes()
    st_bin = np.asarray(uv, '<f4').tobytes()
    xyz_bin = b''
    for f in range(nFrames):
        v = verts_frames[f]; n = normals_frames[f]
        rec = np.zeros((nVerts, 4), '<i2')
        rec[:, :3] = np.clip(np.round(v * 64), -32768, 32767).astype('<i2')
        for i in range(nVerts):
            rec[i, 3] = np.int16(np.uint16(enc_normal(n[i])).view(np.int16)) \
                if enc_normal(n[i]) > 32767 else enc_normal(n[i])
        xyz_bin += rec.tobytes()

    SURF_HDR = 4 + 64 + 10*4
    ofsTris = SURF_HDR
    ofsShaders = ofsTris + len(tris_bin)
    ofsST = ofsShaders + len(shader_bin)
    ofsXyz = ofsST + len(st_bin)
    ofsEndS = ofsXyz + len(xyz_bin)
    surf = struct.pack('<4s64s10i', b'IDP3', surf_name.encode(), 0,
                       nFrames, 1, nVerts, nTris,
                       ofsTris, ofsShaders, ofsST, ofsXyz, ofsEndS)
    surf += tris_bin + shader_bin + st_bin + xyz_bin

    HDR = 4 + 4 + 64 + 4*9
    ofsFrames = HDR
    ofsTags = ofsFrames + len(frames_bin)
    ofsSurfs = ofsTags + len(tags_bin)
    ofsEnd = ofsSurfs + len(surf)
    hdr = struct.pack('<4si64s9i', b'IDP3', 15, model_name.encode(), 0,
                      nFrames, nTags, 1, 0, ofsFrames, ofsTags, ofsSurfs, ofsEnd)
    with open(path, 'wb') as fh:
        fh.write(hdr + frames_bin + tags_bin + surf)

# ---------------------------------------------------------------- tga writer
def write_tga(path, img):
    """24-bit uncompressed, top-left origin (descriptor 0x20)."""
    arr = np.asarray(img.convert('RGB'), np.uint8)
    h, w = arr.shape[:2]
    bgr = arr[:, :, ::-1]  # rows already top-to-bottom
    hdr = struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, w, h, 24, 0x20)
    with open(path, 'wb') as f:
        f.write(hdr + bgr.tobytes())

# ---------------------------------------------------------------- textures
OLIVE   = (96, 108, 66);  OLIVE_D = (76, 87, 52);  OLIVE_L = (112, 124, 80)
GREEN_D = (62, 74, 46)
TAN     = (148, 116, 74); TAN_D   = (118, 90, 56)
SKIN    = (208, 168, 130); SKIN_D = (180, 140, 105)
HAIR    = (58, 46, 34)
BLACK   = (28, 28, 28);   GRAY    = (70, 70, 70)
WEBBING = (70, 80, 50)

def noisy(img, x0, y0, x1, y1, base, amp=7, blotch=None, nb=0, seed_off=0):
    """fill rect with base color + per-pixel noise + optional camo blotches."""
    r = np.random.default_rng(99 + seed_off)
    w, h = x1-x0, y1-y0
    block = np.zeros((h, w, 3), np.int16) + np.array(base, np.int16)
    block += r.integers(-amp, amp+1, (h, w, 1), np.int16)
    if blotch is not None:
        for _ in range(nb):
            bx, by = r.integers(0, w), r.integers(0, h)
            bw, bh = r.integers(2, max(3, w//3)), r.integers(2, max(3, h//3))
            block[max(0,by-bh//2):by+bh//2+1, max(0,bx-bw//2):bx+bw//2+1] = \
                np.array(blotch, np.int16) + r.integers(-5, 6)
    px = np.asarray(img, np.uint8).copy()
    px[y0:y1, x0:x1] = np.clip(block, 0, 255).astype(np.uint8)
    return Image.fromarray(px)

def rect(img, box, color):
    px = np.asarray(img, np.uint8).copy()
    x0, y0, x1, y1 = box
    px[y0:y1, x0:x1] = color
    return Image.fromarray(px)

def make_legs_tga():
    img = Image.new('RGB', (64, 64), OLIVE)
    # pelvis (0,0,32,16): olive + belt
    img = noisy(img, 0, 0, 32, 16, OLIVE, 6, GREEN_D, 3, 1)
    img = rect(img, (0, 10, 32, 15), (50, 44, 32))          # belt
    img = rect(img, (13, 10, 19, 15), (140, 130, 90))       # buckle
    # pelvis top spare (32,0,64,16) camo
    img = noisy(img, 32, 0, 64, 16, OLIVE_D, 6, GREEN_D, 4, 2)
    # thigh (0,16,32,40): olive + cargo pocket
    img = noisy(img, 0, 16, 32, 40, OLIVE, 7, GREEN_D, 4, 3)
    img = rect(img, (6, 24, 16, 34), OLIVE_D)               # cargo pocket
    img = rect(img, (6, 24, 16, 26), GREEN_D)               # pocket flap
    img = rect(img, (10, 26, 12, 30), GREEN_D)              # button strip
    # shin (0,40,32,64): olive, cuff tucked into boot
    img = noisy(img, 0, 40, 32, 64, OLIVE, 7, GREEN_D, 3, 4)
    img = rect(img, (0, 58, 32, 64), OLIVE_D)               # blousing
    # boot sides (32,16,64,44): tan with laces
    img = noisy(img, 32, 16, 64, 44, TAN, 6, TAN_D, 3, 5)
    for yy in range(18, 32, 3):
        img = rect(img, (44, yy, 54, yy+1), (66, 50, 34))   # lace lines
    img = rect(img, (32, 40, 64, 44), (54, 42, 30))         # welt
    # boot toe/top (32,44,64,52)
    img = noisy(img, 32, 44, 64, 52, TAN_D, 5, None, 0, 6)
    # sole (32,52,64,64)
    img = rect(img, (32, 52, 64, 64), (45, 42, 40))
    return img

def make_torso_tga():
    img = Image.new('RGB', (128, 128), OLIVE)
    # chest front (0,0,64,64)
    img = noisy(img, 0, 0, 64, 64, OLIVE, 7, GREEN_D, 6, 10)
    img = rect(img, (10, 0, 18, 64), WEBBING)               # vest strap L
    img = rect(img, (46, 0, 54, 64), WEBBING)               # vest strap R
    img = rect(img, (18, 34, 30, 50), OLIVE_D)              # pocket L
    img = rect(img, (34, 34, 46, 50), OLIVE_D)              # pocket R
    img = rect(img, (18, 34, 30, 38), GREEN_D)
    img = rect(img, (34, 34, 46, 38), GREEN_D)
    img = rect(img, (31, 2, 33, 32), (60, 66, 44))          # zip line
    img = rect(img, (12, 8, 16, 16), (110, 104, 70))        # mag pouch hi-lite
    img = rect(img, (48, 8, 52, 16), (110, 104, 70))
    # chest back (64,0,128,64)
    img = noisy(img, 64, 0, 128, 64, OLIVE, 7, GREEN_D, 7, 11)
    img = rect(img, (74, 0, 82, 64), WEBBING)
    img = rect(img, (110, 0, 118, 64), WEBBING)
    # upper sleeve (0,64,32,96) with patch
    img = noisy(img, 0, 64, 32, 96, OLIVE, 7, GREEN_D, 3, 12)
    img = rect(img, (10, 70, 22, 80), (120, 96, 40))        # shoulder patch
    img = rect(img, (12, 72, 20, 78), (60, 80, 50))
    # forearm sleeve (32,64,64,96) with cuff
    img = noisy(img, 32, 64, 64, 96, OLIVE, 7, GREEN_D, 3, 13)
    img = rect(img, (32, 88, 64, 96), OLIVE_D)              # cuff
    # hands skin (64,64,88,88)
    img = noisy(img, 64, 64, 88, 88, SKIN, 6, SKIN_D, 2, 14)
    # neck skin (88,64,112,88)
    img = noisy(img, 88, 64, 112, 88, SKIN, 5, None, 0, 15)
    # shoulders top + misc camo
    img = noisy(img, 0, 96, 128, 128, OLIVE_D, 7, GREEN_D, 10, 16)
    img = noisy(img, 112, 64, 128, 96, OLIVE_D, 6, GREEN_D, 2, 17)
    return img

def make_head_tga():
    img = Image.new('RGB', (64, 64), SKIN)
    # helmet (0,0,64,22) + rim (0,22,64,28)
    img = noisy(img, 0, 0, 64, 22, (84, 94, 60), 5, (70, 80, 50), 4, 20)
    img = rect(img, (0, 16, 64, 22), (62, 70, 44))          # helmet band
    img = rect(img, (26, 17, 38, 21), (130, 124, 86))       # band patch
    img = noisy(img, 0, 22, 64, 28, (70, 78, 50), 4, None, 0, 21)
    # face front (16,28,48,58)
    img = noisy(img, 16, 28, 48, 58, SKIN, 4, None, 0, 22)
    img = rect(img, (21, 34, 28, 36), HAIR)                 # brow L
    img = rect(img, (36, 34, 43, 36), HAIR)                 # brow R
    img = rect(img, (22, 37, 27, 40), (240, 238, 230))      # eye white L
    img = rect(img, (37, 37, 42, 40), (240, 238, 230))      # eye white R
    img = rect(img, (24, 37, 26, 40), (40, 30, 22))         # pupil L
    img = rect(img, (38, 37, 40, 40), (40, 30, 22))         # pupil R
    img = rect(img, (30, 41, 34, 47), SKIN_D)               # nose shading
    img = rect(img, (26, 51, 38, 53), (150, 100, 90))       # mouth
    img = rect(img, (16, 28, 48, 31), SKIN_D)               # helmet shadow
    # sides/back (0,28,16,58) & (48,28,64,58): skin + sideburn/ear
    img = noisy(img, 0, 28, 16, 58, SKIN, 5, None, 0, 23)
    img = noisy(img, 48, 28, 64, 58, SKIN, 5, None, 0, 24)
    img = rect(img, (4, 38, 9, 46), SKIN_D)                 # ear L
    img = rect(img, (55, 38, 60, 46), SKIN_D)               # ear R
    img = rect(img, (0, 28, 16, 33), HAIR)                  # hair fringe
    img = rect(img, (48, 28, 64, 33), HAIR)
    # under-chin (16,58,48,64)
    img = noisy(img, 16, 58, 48, 64, SKIN_D, 4, None, 0, 25)
    return img

# ---------------------------------------------------------------- LEGS model
LM = Mesh((64, 64))
HIPY = 2.1
HIP_P  = lambda s: np.array([0.0, s*HIPY, 23.5])
KNEE_P = lambda s: np.array([0.0, s*HIPY, 13.0])
ANK_P  = lambda s: np.array([0.0, s*HIPY, 3.2])

# pelvis
LM.add_box('pelvis', (0, 0, 20.5, 5.2, 6.4), (0, 0, 26.0, 5.6, 7.0), {
    '+x': (0, 0, 32, 16), '-x': (0, 0, 32, 16),
    '+y': (0, 0, 32, 16), '-y': (0, 0, 32, 16),
    '+z': (32, 0, 64, 16), '-z': (32, 0, 64, 16)})
for s, side in ((1, 'L'), (-1, 'R')):
    th, sh, bt = f'thigh{side}', f'shin{side}', f'boot{side}'
    LM.add_box(th, (0, s*HIPY, 13.0, 2.8, 2.6), (0.2, s*HIPY, 21.5, 3.6, 3.4),
               {'all': (0, 16, 32, 40)})
    LM.add_box(sh, (0, s*HIPY, 3.0, 2.2, 2.0), (0, s*HIPY, 13.5, 2.9, 2.7),
               {'all': (0, 40, 32, 64)})
    LM.add_box(sh, (1.55, s*HIPY, 12.0, 1.3, 1.7), (1.55, s*HIPY, 14.4, 1.3, 1.7),
               {'all': (0, 16, 32, 40)})                       # knee cap
    LM.add_box(bt, (0.2, s*HIPY, 0.0, 4.8, 2.7), (0.2, s*HIPY, 3.6, 4.4, 2.5), {
        '+x': (32, 44, 64, 52), '-x': (32, 44, 64, 52),
        '+y': (32, 16, 64, 44), '-y': (32, 16, 64, 44),
        '+z': (32, 44, 64, 52), '-z': (32, 52, 64, 64)})
    LM.add_box(bt, (3.6, s*HIPY, 0.0, 2.4, 2.5), (3.4, s*HIPY, 2.3, 1.7, 2.1), {
        '+x': (32, 44, 64, 52), '-x': (32, 44, 64, 52),
        '+y': (32, 44, 64, 52), '-y': (32, 44, 64, 52),
        '+z': (32, 44, 64, 52), '-z': (32, 52, 64, 64)})       # toe

LV, LN, LUV, LTRIS, LSEG = LM.arrays()

def legs_pose(dz=0.0, root_pitch=0.0, root_pivot=(0, 0, 2.0),
              hipL=0, kneeL=0, ankL=0, hipR=0, kneeR=0, ankR=0,
              torso_pitch=0.0, torso_dz=0.0):
    """angles in degrees; returns (verts, normals, tag_torso (origin,R))"""
    root = xf_mul(xf_trans((0, 0, dz)), xf_about(rot_y(D(root_pitch)), root_pivot))
    X = {'pelvis': root}
    for s, side, (h, k, a) in ((1, 'L', (hipL, kneeL, ankL)),
                               (-1, 'R', (hipR, kneeR, ankR))):
        hip = xf_mul(root, xf_about(rot_y(D(h)), HIP_P(s)))
        knee = xf_mul(hip, xf_about(rot_y(D(k)), KNEE_P(s)))
        ank = xf_mul(knee, xf_about(rot_y(D(a)), ANK_P(s)))
        X[f'thigh{side}'] = hip; X[f'shin{side}'] = knee; X[f'boot{side}'] = ank
    v = np.empty_like(LV); n = np.empty_like(LN)
    for i, sg in enumerate(LSEG):
        R, t = X[sg]
        v[i] = R @ LV[i] + t; n[i] = R @ LN[i]
    Rr, tr = root
    tagR = Rr @ rot_y(D(torso_pitch))
    tag_o = Rr @ np.array([0, 0, 25.0 + torso_dz]) + tr
    return v, n, (tag_o, tagR)

legs_frames = []   # (verts, normals)
legs_tags = {'tag_torso': []}
def add_legs(**kw):
    v, n, (tag_o, tag_R) = legs_pose(**kw)
    lift = max(0.0, -float(v[:, 2].min()))  # raise-only auto-grounding
    if lift > 0.0:
        v = v + np.array([0, 0, lift])
        tag_o = tag_o + np.array([0, 0, lift])
    legs_frames.append((v, n)); legs_tags['tag_torso'].append((tag_o, tag_R))

# 0-4 BOTH_DEATH1: fall backward, knees buckling
for rp, dz, kb, tp in [(0, 0, 5, -4), (-18, -1.5, 20, -8), (-48, -2.5, 35, -8),
                       (-78, -1.5, 24, -2), (-86, -0.5, 14, 5)]:
    add_legs(root_pitch=rp, dz=dz, root_pivot=(-2.0, 0, 1.5),
             hipL=8+kb*0.3, kneeL=kb, hipR=4+kb*0.2, kneeR=kb*0.8,
             ankL=-6, ankR=-6, torso_pitch=tp)
# 5-12 LEGS_WALKCR: crouched walk
for i in range(8):
    t = 2*math.pi*i/8
    sw = 18*math.sin(t)
    kl = 52 + 18*max(0.0, math.sin(t + 2.4))
    kr = 52 + 18*max(0.0, math.sin(t + math.pi + 2.4))
    add_legs(dz=-6.0 - 0.5*abs(math.sin(t)), hipL=-38 - sw, kneeL=kl,
             hipR=-38 + sw, kneeR=kr, ankL=-12, ankR=-12, torso_pitch=10)
# 13-20 LEGS_WALK
for i in range(8):
    t = 2*math.pi*i/8
    sw = 27*math.sin(t)
    kl = 8 + 38*max(0.0, math.sin(t + 2.6))
    kr = 8 + 38*max(0.0, math.sin(t + math.pi + 2.6))
    add_legs(dz=-0.6 - 0.5*abs(math.cos(t)), hipL=-sw, kneeL=kl,
             hipR=sw, kneeR=kr, ankL=-kl*0.25, ankR=-kr*0.25, torso_pitch=2)
# 21-28 LEGS_RUN
for i in range(8):
    t = 2*math.pi*i/8
    sw = 42*math.sin(t)
    kl = 14 + 60*max(0.0, math.sin(t + 2.6))
    kr = 14 + 60*max(0.0, math.sin(t + math.pi + 2.6))
    add_legs(dz=-0.8 + 1.0*abs(math.sin(t)), hipL=-sw, kneeL=kl,
             hipR=sw, kneeR=kr, ankL=-kl*0.3, ankR=-kr*0.3, torso_pitch=7)
# 29-31 LEGS_JUMP: coil, extend, trail
add_legs(dz=-5.0, hipL=-35, kneeL=55, hipR=-35, kneeR=55, ankL=-15, ankR=-15,
         torso_pitch=6)
add_legs(dz=0.5, hipL=-8, kneeL=8, hipR=4, kneeR=4, ankL=18, ankR=14,
         torso_pitch=2)
add_legs(dz=0.0, hipL=14, kneeL=36, hipR=-12, kneeR=22, ankL=10, ankR=4,
         torso_pitch=-2)
# 32-33 LEGS_LAND
add_legs(dz=-5.5, hipL=-32, kneeL=52, hipR=-32, kneeR=52, ankL=-14, ankR=-14,
         torso_pitch=8)
add_legs(dz=-2.0, hipL=-12, kneeL=20, hipR=-12, kneeR=20, ankL=-6, ankR=-6,
         torso_pitch=3)
# 34-37 LEGS_IDLE: subtle sway
for i in range(4):
    t = 2*math.pi*i/4
    add_legs(dz=-0.25 + 0.25*math.cos(t), hipL=-2 + math.sin(t), kneeL=4,
             hipR=-2 - math.sin(t), kneeR=4, ankL=-1, ankR=-1,
             torso_dz=0.15*math.sin(t))
# 38-39 LEGS_IDLECR: crouch hold
for i in range(2):
    add_legs(dz=-6.2 + 0.2*i, hipL=-40, kneeL=58, hipR=-34, kneeR=52,
             ankL=-14, ankR=-14, torso_pitch=10)

assert len(legs_frames) == 40

# ---------------------------------------------------------------- TORSO model
TM = Mesh((128, 128))
SH_P = lambda s: np.array([0.0, s*5.2, 15.5])   # shoulder pivot (torso local)
EL_P = lambda s: np.array([0.0, s*6.1, 9.0])    # elbow pivot

TM.add_box('chest', (0, 0, -1.0, 5.0, 6.6), (0, 0, 5.0, 5.0, 6.8), {
    '+x': (0, 44, 64, 64), '-x': (64, 44, 128, 64),
    '+y': (64, 96, 96, 128), '-y': (64, 96, 96, 128),
    '-z': (96, 96, 128, 128)})                                # waist
TM.add_box('chest', (0, 0, 5.0, 5.0, 6.8), (0.3, 0, 17.0, 5.6, 9.6), {
    '+x': (0, 0, 64, 44), '-x': (64, 0, 128, 44),
    '+y': (96, 96, 128, 128), '-y': (96, 96, 128, 128),
    '+z': (0, 96, 64, 128)})                                  # chest
TM.add_box('chest', (-2.8, 0, 7.0, 2.6, 6.0), (-2.8, 0, 15.0, 2.4, 5.6), {
    'all': (64, 96, 128, 128)})                               # back pack
TM.add_box('chest', (0.3, 5.2, 14.8, 4.0, 3.4), (0.3, 5.0, 17.8, 3.4, 2.8),
           {'all': (0, 96, 64, 128)})                         # shoulder pad L
TM.add_box('chest', (0.3, -5.2, 14.8, 4.0, 3.4), (0.3, -5.0, 17.8, 3.4, 2.8),
           {'all': (0, 96, 64, 128)})
TM.add_box('neck', (0, 0, 16.8, 2.7, 2.7), (0, 0, 22.6, 2.5, 2.5),
           {'all': (88, 64, 112, 88)})

for s, side in ((1, 'L'), (-1, 'R')):
    ua, fa, ha = f'uparm{side}', f'forearm{side}', f'hand{side}'
    TM.add_box(ua, (0, s*6.0, 8.6, 2.6, 2.4), (0.2, s*5.9, 16.2, 3.2, 3.0),
               {'all': (0, 64, 32, 96)})
    TM.add_box(fa, (0, s*6.0, 3.6, 2.2, 2.0), (0, s*6.0, 9.6, 2.5, 2.3),
               {'all': (32, 64, 64, 96)})
    TM.add_box(ha, (0.2, s*6.0, 1.3, 2.2, 1.9), (0, s*6.0, 3.8, 2.1, 1.9),
               {'all': (64, 64, 88, 88)})

# bake rifle-hold pose: right arm forward, left arm crosses toward weapon
def bake_arm(side_sign, side, sh_pitch, el_pitch, sh_yaw=0.0, sh_roll=0.0):
    sh = xf_about(rot_y(D(sh_pitch)) @ rot_z(D(sh_yaw)) @ rot_x(D(sh_roll)),
                  SH_P(side_sign))
    TM.bake([f'uparm{side}'], sh)
    el = xf_mul(sh, xf_about(rot_y(D(el_pitch)), EL_P(side_sign)))
    TM.bake([f'forearm{side}', f'hand{side}'], el)
    return sh, el
shR, elR = bake_arm(-1, 'R', -22, -68, sh_yaw=8, sh_roll=12)
shL, elL = bake_arm(1, 'L', -38, -52, sh_yaw=32, sh_roll=-14)

TV, TN, TUV, TTRIS, TSEG = TM.arrays()
HAND_R = xf_apply(elR, np.array([0.1, -6.0, 2.5]))  # right palm center (bind)

def torso_pose(chest_pitch=0.0, dz=0.0, armR_pitch=0.0, armL_pitch=0.0,
               armR_roll=0.0, armL_roll=0.0, foreR_pitch=0.0, head_dz=0.0):
    root = xf_mul(xf_trans((0, 0, dz)),
                  xf_about(rot_y(D(chest_pitch)), (0, 0, 2.0)))
    XR = xf_mul(root, xf_about(rot_y(D(armR_pitch)) @ rot_x(D(armR_roll)),
                               SH_P(-1)))
    XRf = xf_mul(XR, xf_about(rot_y(D(foreR_pitch)),
                              xf_apply(elR, EL_P(-1))))
    XL = xf_mul(root, xf_about(rot_y(D(armL_pitch)) @ rot_x(D(armL_roll)),
                               SH_P(1)))
    X = {'chest': root, 'neck': root,
         'uparmR': XR, 'forearmR': XRf, 'handR': XRf,
         'uparmL': XL, 'forearmL': XL, 'handL': XL}
    v = np.empty_like(TV); n = np.empty_like(TN)
    for i, sg in enumerate(TSEG):
        R, t = X[sg]
        v[i] = R @ TV[i] + t; n[i] = R @ TN[i]
    Rr, tr = root
    tag_head = (Rr @ np.array([0, 0, 22.0 + head_dz]) + tr, Rr)
    Rw, tw = XRf
    tag_weap = (Rw @ (HAND_R + np.array([1.2, 0, 0.4])) + tw, Rw)
    return v, n, tag_head, tag_weap

torso_frames = []; torso_tags = {'tag_head': [], 'tag_weap': []}
def add_torso(**kw):
    v, n, th, tw = torso_pose(**kw)
    torso_frames.append((v, n))
    torso_tags['tag_head'].append(th); torso_tags['tag_weap'].append(tw)

# 0-4 BOTH_DEATH1: arms fly up/outward while body (via legs tag) falls
for i, (ar, ap) in enumerate([(0, 0), (20, -10), (45, -25), (65, -38), (75, -42)]):
    add_torso(chest_pitch=-2*i, armR_roll=-ar, armL_roll=ar,
              armR_pitch=ap, armL_pitch=ap)
# 5-6 TORSO_GESTURE: raise right forearm
add_torso(foreR_pitch=-18, armR_pitch=-6)
add_torso(foreR_pitch=-30, armR_pitch=-10)
# 7-9 TORSO_ATTACK: recoil back then recover
add_torso(chest_pitch=-4, armR_pitch=5, armL_pitch=5, dz=-0.2)
add_torso(chest_pitch=-2, armR_pitch=2, armL_pitch=2)
add_torso()
# 10 TORSO_DROP / 11 TORSO_RAISE
add_torso(armR_pitch=22, armL_pitch=22, foreR_pitch=12, chest_pitch=3)
add_torso(armR_pitch=10, armL_pitch=10, foreR_pitch=5)
# 12-15 TORSO_STAND: breathing
for i in range(4):
    t = 2*math.pi*i/4
    add_torso(dz=0.18*math.sin(t), chest_pitch=0.7*math.sin(t),
              armR_pitch=0.8*math.sin(t), armL_pitch=0.8*math.sin(t),
              head_dz=0.1*math.sin(t))
assert len(torso_frames) == 16

# ---------------------------------------------------------------- HEAD model
HM = Mesh((64, 64))
HM.add_box('head', (0.1, 0, 0.4, 5.0, 4.8), (0.1, 0, 8.0, 5.8, 5.6), {
    '+x': (16, 28, 48, 58), '-x': (0, 28, 16, 58),
    '+y': (48, 28, 64, 58), '-y': (0, 28, 16, 58),
    '-z': (16, 58, 48, 64)})
HM.add_box('head', (2.9, 0, 3.0, 1.0, 1.3), (2.9, 0, 4.7, 0.9, 1.2),
           {'all': (30, 41, 34, 47)})                          # nose
HM.add_box('head', (0.1, 0, 6.2, 7.4, 7.2), (0.0, 0, 11.2, 5.6, 5.4), {
    '+x': (0, 0, 64, 22), '-x': (0, 0, 64, 22),
    '+y': (0, 0, 64, 22), '-y': (0, 0, 64, 22),
    '+z': (0, 0, 64, 22), '-z': (0, 22, 64, 28)})              # helmet
HM.add_box('head', (3.6, 0, 6.2, 1.8, 6.8), (3.3, 0, 7.3, 1.5, 6.4),
           {'all': (0, 22, 64, 28)})                           # brim
HV, HN, HUV, HTRIS, HSEG = HM.arrays()

# ---------------------------------------------------------------- write files
def seq(frames):  # split list of (v,n)
    return [f[0] for f in frames], [f[1] for f in frames]

base = os.path.join(OUT)
lv, ln = seq(legs_frames)
write_md3(os.path.join(base, 'sarge_legs.md3'), 'sarge_legs', 'legs_skin',
          f'{MODELDIR}/sarge_legs',
          lv, ln, LUV, LTRIS, legs_tags, 'legs')
tv, tn = seq(torso_frames)
write_md3(os.path.join(base, 'sarge_torso.md3'), 'sarge_torso', 'torso_skin',
          f'{MODELDIR}/sarge_torso',
          tv, tn, TUV, TTRIS, torso_tags, 'torso')
write_md3(os.path.join(base, 'sarge_head.md3'), 'sarge_head', 'head_skin',
          f'{MODELDIR}/sarge_head',
          [HV], [HN], HUV, HTRIS, {}, 'head')

write_tga(os.path.join(base, 'sarge_legs.tga'), make_legs_tga())
write_tga(os.path.join(base, 'sarge_torso.tga'), make_torso_tga())
write_tga(os.path.join(base, 'sarge_head.tga'), make_head_tga())

ANIM_CFG = """\
// MFQ3 LQM sarge - animated soldier (built by build_sarge2.py)
// torso.md3: 16 frames, legs.md3: 40 frames
// firstFrame numFrames loopFrames fps
0	5	0	15	// BOTH_DEATH1
4	1	0	15	// BOTH_DEAD1
0	5	0	15	// BOTH_DEATH2
4	1	0	15	// BOTH_DEAD2
0	5	0	15	// BOTH_DEATH3
4	1	0	15	// BOTH_DEAD3
5	2	0	8	// TORSO_GESTURE
7	3	0	15	// TORSO_ATTACK
7	3	0	15	// TORSO_ATTACK2
10	1	0	15	// TORSO_DROP
11	1	0	15	// TORSO_RAISE
12	4	4	8	// TORSO_STAND
12	4	4	8	// TORSO_STAND2
16	8	8	15	// LEGS_WALKCR (file frame 5)
24	8	8	12	// LEGS_WALK   (file frame 13)
32	8	8	15	// LEGS_RUN    (file frame 21)
32	8	8	15	// LEGS_BACK
32	8	8	10	// LEGS_SWIM
40	3	0	15	// LEGS_JUMP   (file frame 29)
43	2	0	15	// LEGS_LAND   (file frame 32)
40	3	0	15	// LEGS_JUMPB
43	2	0	15	// LEGS_LANDB
45	4	4	8	// LEGS_IDLE   (file frame 34)
49	2	2	8	// LEGS_IDLECR (file frame 38)
45	4	4	15	// LEGS_TURN
"""
with open(os.path.join(base, 'animation.cfg'), 'w', newline='\n') as f:
    f.write(ANIM_CFG)

# pk3
pk3 = os.path.join(base, 'zz_lqmfix.pk3')
with zipfile.ZipFile(pk3, 'w', zipfile.ZIP_DEFLATED) as z:
    for fn in ('animation.cfg', 'sarge_legs.md3', 'sarge_torso.md3',
               'sarge_head.md3', 'sarge_legs.tga', 'sarge_torso.tga',
               'sarge_head.tga'):
        z.write(os.path.join(base, fn), f'{MODELDIR}/{fn}')

print('legs tris', len(LTRIS), 'verts', len(LUV), 'frames', len(legs_frames))
print('torso tris', len(TTRIS), 'verts', len(TUV), 'frames', len(torso_frames))
print('head tris', len(HTRIS), 'verts', len(HUV), 'frames 1')
print('total tris', len(LTRIS)+len(TTRIS)+len(HTRIS))
print('tag_weap frame0', torso_tags['tag_weap'][0][0].round(2))
print('wrote', pk3)
