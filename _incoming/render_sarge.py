#!/usr/bin/env python3
# Render a preview of the MFQ3 "Sarge" 3-part infantry model (legs+torso+head MD3s
# stacked via Q3 player tags). Extracts lqm_sarge.pk3, assembles the parts, renders
# a front 3/4 view and a side view side-by-side into sarge_preview.png.
import os, sys, math, zipfile, tempfile
import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render_md3 import gather
import struct

def load_md3(path):
    """MD3 loader handling both the standard surface header (64-byte name) and the
    sarge exporter's compact variant (32-byte name, no flags-before-counts shift)."""
    d = open(path, 'rb').read()
    flags, nFrames, nTags, nSurf, nSkin, ofsFrames, ofsTags, ofsSurf, ofsEnd = \
        struct.unpack_from('<9i', d, 72)
    tags = {}
    for fr in range(nFrames):
        for t in range(nTags):
            base = ofsTags + (fr * nTags + t) * 112
            tn = d[base:base+64].split(b'\0')[0].decode('latin1')
            vals = struct.unpack_from('<12f', d, base + 64)
            origin = np.array(vals[0:3]); axis = np.array(vals[3:12]).reshape(3, 3)
            tags.setdefault(tn, [None] * nFrames)[fr] = (origin, axis)
    surfs = []
    so = ofsSurf
    for s in range(nSurf):
        # standard: ident(4) name(64) then 10 ints (flags,frames,shaders,verts,tris,oTris,oShaders,oST,oXyz,end)
        std = struct.unpack_from('<10i', d, so + 68)
        # variant: ident(4) name(32) flags(4) then 9 ints (frames,shaders,verts,tris,oTris,oShaders,oST,oXyz,end)
        var = struct.unpack_from('<9i', d, so + 40)
        def plausible(hdr_size, nF, nV, nT, oTris, oXyz, end):
            return (0 < nV < 10000 and 0 < nT < 20000 and oTris >= hdr_size
                    and so + end <= len(d) and oXyz + nF * nV * 8 <= end)
        if plausible(108, std[1], std[3], std[4], std[5], std[8], std[9]):
            name = d[so+4:so+68].split(b'\0')[0].decode('latin1')
            sFrames, sVerts, sTris, oTris, oXyz, sEnd = std[1], std[3], std[4], std[5], std[8], std[9]
        elif plausible(76, var[0], var[2], var[3], var[4], var[7], var[8]):
            name = d[so+4:so+36].split(b'\0')[0].decode('latin1')
            sFrames, sVerts, sTris, oTris, oXyz, sEnd = var[0], var[2], var[3], var[4], var[7], var[8]
        else:
            raise ValueError(f'unrecognized MD3 surface header at {so} in {path}')
        tris = np.frombuffer(d, dtype='<i4', count=sTris*3, offset=so+oTris).reshape(sTris, 3)
        xyz = np.frombuffer(d, dtype='<i2', count=sFrames*sVerts*4, offset=so+oXyz).reshape(sFrames, sVerts, 4)
        surfs.append({'name': name, 'tris': tris, 'xyz': xyz, 'nFrames': sFrames, 'nVerts': sVerts})
        so += sEnd
    return {'nFrames': nFrames, 'tags': tags, 'surfs': surfs}

PK3 = r'C:\source\mfq3\play\mfdata\lqm_sarge.pk3'
OUT = r'C:\source\mfq3\_incoming\sarge_preview.png'
BASE = 'models/vehicles/lqms/sarge/'

def compose(outer, inner):
    """outer applied after inner: world = Oo + (Oi + v@Ai)@Ao -> (Oo + Oi@Ao, Ai@Ao)"""
    Oo, Ao = outer
    Oi, Ai = inner
    return (Oo + Oi @ Ao, Ai @ Ao)

def render_view(geoms, az, el, H=700):
    """Render one view at height H; returns PIL Image (painter's algorithm)."""
    A = math.radians(az); E = math.radians(el)
    ca, sa, ce, se = math.cos(A), math.sin(A), math.cos(E), math.sin(E)
    fwd = np.array([ca*ce, sa*ce, se]); fwd /= np.linalg.norm(fwd)
    right = np.cross(np.array([0, 0, 1.0]), fwd); right /= np.linalg.norm(right)
    up = np.cross(fwd, right)
    light = np.array([0.3, 0.4, 0.85]); light /= np.linalg.norm(light)
    allv = np.vstack([g[0] for g in geoms])
    pr = np.column_stack([allv @ right, allv @ up])
    mn = pr.min(0); mx = pr.max(0)
    span = mx - mn; c = (mx + mn) / 2
    sc = (H * 0.88) / span[1]                # fit to height (model is tall)
    W = max(int(span[0] * sc / 0.88) + 80, int(H * 0.55))
    img = Image.new('RGB', (W, H), (248, 249, 251))
    dr = ImageDraw.Draw(img)
    tris = []
    for (v, n, t, col) in geoms:
        p = np.column_stack([v @ right, v @ up, v @ fwd])
        sx = (p[:, 0] - c[0]) * sc + W / 2
        sy = H / 2 - (p[:, 1] - c[1]) * sc
        depth = p[:, 2]
        for tri in t:
            a, b, cc = tri
            z = (depth[a] + depth[b] + depth[cc]) / 3
            fn = n[a] + n[b] + n[cc]; fn = fn / (np.linalg.norm(fn) + 1e-9)
            sh = 0.55 + 0.45 * max(0, fn @ light)
            color = tuple(int(min(255, k * sh)) for k in col)
            tris.append((z, [(sx[a], sy[a]), (sx[b], sy[b]), (sx[cc], sy[cc])], color))
    tris.sort(key=lambda r: -r[0])           # far first
    for z, pts, color in tris:
        dr.polygon(pts, fill=color)
    return img

def main():
    tmp = tempfile.mkdtemp(prefix='sarge_')
    with zipfile.ZipFile(PK3) as z:
        z.extractall(tmp)
    legs  = load_md3(os.path.join(tmp, BASE, 'sarge_legs.md3'))
    torso = load_md3(os.path.join(tmp, BASE, 'sarge_torso.md3'))
    head  = load_md3(os.path.join(tmp, BASE, 'sarge_head.md3'))

    ident = (np.zeros(3), np.eye(3))
    legs_tag_torso = legs['tags']['tag_torso'][0]
    torso_tag_head = torso['tags']['tag_head'][0]
    head_xf = compose(legs_tag_torso, torso_tag_head)

    parts = [
        ('legs',  legs,  ident,          (140, 155, 190)),   # grey-blue
        ('torso', torso, legs_tag_torso, (170, 175, 170)),   # grey
        ('head',  head,  head_xf,        (225, 180, 140)),   # skin tone
    ]
    geoms = []
    total_v = total_t = 0
    for name, md3, xf, col in parts:
        v, n, t = gather(md3, 0, xf)
        geoms.append((v, n, t, col))
        print(f'{name:5s}: {len(v):4d} verts, {len(t):4d} tris')
        total_v += len(v); total_t += len(t)
    print(f'total: {total_v:4d} verts, {total_t:4d} tris')

    allv = np.vstack([g[0] for g in geoms])
    zmin, zmax = allv[:, 2].min(), allv[:, 2].max()
    print(f'assembled height (Z extent): {zmax - zmin:.1f} units (z {zmin:.1f} .. {zmax:.1f})')

    H = 700
    # Model faces +Y (shoulders along X). Camera looks along 'fwd'; to see the face
    # we look in a -Y-ish direction.
    img1 = render_view(geoms, az=-60, el=12, H=H)   # front 3/4
    img2 = render_view(geoms, az=180, el=0,  H=H)   # side profile
    out = Image.new('RGB', (img1.width + img2.width, H), (248, 249, 251))
    out.paste(img1, (0, 0)); out.paste(img2, (img1.width, 0))
    out.save(OUT)
    print('wrote', OUT, os.path.getsize(OUT), 'bytes')

if __name__ == '__main__':
    main()
