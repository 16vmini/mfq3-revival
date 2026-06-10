#!/usr/bin/env python3
"""Verify sarge2 md3s/tgas/pk3 and render assembled previews."""
import sys, os, math, zipfile
import numpy as np
from PIL import Image
sys.path.insert(0, r'C:\source\mfq3\_incoming')
import render_md3 as R

BASE = r'C:\source\mfq3\_incoming\sarge2'

legs = R.load_md3(os.path.join(BASE, 'sarge_legs.md3'))
torso = R.load_md3(os.path.join(BASE, 'sarge_torso.md3'))
head = R.load_md3(os.path.join(BASE, 'sarge_head.md3'))

print('legs  frames', legs['nFrames'], 'tags', list(legs['tags']),
      'surfs', [(s['name'], s['nVerts'], len(s['tris'])) for s in legs['surfs']])
print('torso frames', torso['nFrames'], 'tags', list(torso['tags']),
      'surfs', [(s['name'], s['nVerts'], len(s['tris'])) for s in torso['surfs']])
print('head  frames', head['nFrames'], 'tags', list(head['tags']),
      'surfs', [(s['name'], s['nVerts'], len(s['tris'])) for s in head['surfs']])

assert legs['nFrames'] == 40 and torso['nFrames'] == 16 and head['nFrames'] == 1
assert all(t is not None for t in legs['tags']['tag_torso'])
assert all(t is not None for t in torso['tags']['tag_head'])
assert all(t is not None for t in torso['tags']['tag_weap'])

# sanity: standing pose extents
v0, _ = R.decode(legs['surfs'][0]['xyz'][34])
print('legs idle z range', v0[:, 2].min().round(2), v0[:, 2].max().round(2))

def compose(o1, A1, o2, A2):
    """child placed at tag2 inside parent placed at tag1 (loader: w = o + v@A)."""
    return o1 + o2 @ A1, A2 @ A1

def assemble(lf, tf, hf=0):
    geoms = []
    lv, lnn, lt = R.gather(legs, lf)
    geoms.append((lv, lnn, lt, (96, 108, 66)))
    to, ta = legs['tags']['tag_torso'][lf]
    tv, tn, tt = R.gather(torso, tf, (to, ta))
    geoms.append((tv, tn, tt, (86, 100, 62)))
    ho_l, ha_l = torso['tags']['tag_head'][tf]
    ho, ha = compose(to, ta, ho_l, ha_l)
    hv, hn, ht = R.gather(head, hf, (ho, ha))
    geoms.append((hv, hn, ht, (200, 165, 128)))
    # mark tag_weap with a small 'rifle' box so we can see where the gun goes
    wo_l, wa_l = torso['tags']['tag_weap'][tf]
    wo, wa = compose(to, ta, wo_l, wa_l)
    gun = np.array([[x, y, z] for x in (-7, 9) for y in (-0.6, 0.6)
                    for z in (-0.7, 0.7)], float)
    gtris = np.array([[0,1,3],[0,3,2],[4,6,7],[4,7,5],[0,2,6],[0,6,4],
                      [1,5,7],[1,7,3],[2,3,7],[2,7,6],[0,4,5],[0,5,1]])
    gn = np.tile(np.array([0, 0, 1.0]), (8, 1))
    gv = wo + gun @ wa
    geoms.append((gv, gn @ wa, gtris, (40, 40, 44)))
    return geoms

def panel(geoms, az, el=12, S=440):
    tmp = os.path.join(BASE, '_tmp_panel.png')
    R.render(geoms, tmp, az=az, el=el, S=S)
    return Image.open(tmp).copy()

def preview(name, lf, tf, hf=0):
    geoms = assemble(lf, tf, hf)
    views = [panel(geoms, az) for az in (180, 90, 135)]  # front, side, 3/4
    out = Image.new('RGB', (sum(v.width for v in views), views[0].height))
    x = 0
    for v in views:
        out.paste(v, (x, 0)); x += v.width
    p = os.path.join(BASE, name)
    out.save(p); print('wrote', p)

preview('preview_idle.png', 34, 12)       # LEGS_IDLE / TORSO_STAND
preview('preview_run.png', 23, 12)        # mid LEGS_RUN
preview('preview_run2.png', 27, 12)       # opposite run phase
preview('preview_crouch.png', 7, 12)      # LEGS_WALKCR
preview('preview_death.png', 3, 3)        # BOTH_DEATH1 falling
preview('preview_dead.png', 4, 4)         # BOTH_DEAD1

# TGA verification
for t in ('sarge_legs.tga', 'sarge_torso.tga', 'sarge_head.tga'):
    im = Image.open(os.path.join(BASE, t)); im.load()
    hdr = open(os.path.join(BASE, t), 'rb').read(18)
    print(t, im.size, im.mode, 'type', hdr[2], 'bpp', hdr[16],
          'desc', hex(hdr[17]))
    assert hdr[2] == 2 and hdr[16] == 24 and hdr[17] == 0x20

# pk3 contents
z = zipfile.ZipFile(os.path.join(BASE, 'zz_lqmfix.pk3'))
names = sorted(z.namelist())
print('pk3:', names)
want = sorted('models/vehicles/lqms/sarge/' + f for f in
              ('animation.cfg', 'sarge_legs.md3', 'sarge_torso.md3',
               'sarge_head.md3', 'sarge_legs.tga', 'sarge_torso.tga',
               'sarge_head.tga'))
assert names == want, names
# re-verify md3s straight out of the zip parse identically
import tempfile
for f in want:
    if f.endswith('.md3'):
        data = z.read(f)
        tmp = os.path.join(BASE, '_tmp.md3')
        open(tmp, 'wb').write(data)
        m = R.load_md3(tmp)
        print(f, 'OK frames', m['nFrames'])
os.remove(os.path.join(BASE, '_tmp.md3'))
os.remove(os.path.join(BASE, '_tmp_panel.png'))
print('ALL CHECKS PASSED')
