"""Clawd desk buddy enclosure for the M5Stack Core2 (54 x 54 x 16.5 mm).
Draft 3.1. Cable powered (window edge chamfered for swipes); the Core2's own cell covers short trips off the cable.

Feature positions come from M5Stack's Core2 dimension drawing (first-angle
projection, so the view right of the front view is the LEFT side):
  left side:  power key, USB-C, Grove port (all about 8.4 mm behind the front)
  right side: speaker grille
  bottom:     TF slot and reset key, about 7.5 mm behind the front
  top:        a 6.6 mm opening near the back (left open)
Axes: X = width, Y = depth (0 = front face, + toward back), Z = height (0 = body underside).
Printing: body face down, cover outer face down. No supports.
"""
import numpy as np, trimesh, os
from trimesh.creation import box as tbox, cylinder
from shapely.geometry import box as sbox

OUT = os.path.dirname(os.path.abspath(__file__)) + '/'
CL = 0.3

# --- Core2 ---
DEV = 54.0
DEV_D = 16.5
# window: shows the 2.0" screen (x 6..48, 11.5..42.6 from the top) and the A/B/C touch strip below it
WIN_X0, WIN_X1 = 4.5, 49.5        # from the device's left edge
WIN_T0, WIN_T1 = 9.5, 51.0        # from the device's top edge
# left side (from the top edge), depth from the device front
PORT_T0, PORT_T1 = 8.0, 46.5      # one opening covers power key, USB-C and Grove
# right side speaker grille (from the top edge)
SPK_T0, SPK_T1 = 9.7, 33.0
SPK_D0, SPK_D1 = 4.7, 10.2
# bottom: TF slot and reset key (from the device's left edge)
TF_X0, TF_X1 = 13.4, 27.4
RST_X = 40.8
BOT_D = 7.6                        # depth of both from the device front
# top: opening 21 mm from the left, 4.9 mm in from the back
TOP_X, TOP_FROM_BACK, TOP_R = 21.0, 4.9, 3.6

# --- body ---
FRONT, FLOOR, TOP, COVER = 2.0, 2.0, 2.4, 2.5
PK = DEV + 2*CL
PK_D = DEV_D + 0.5
BODY_W = 72.0
BODY_H = FLOOR + PK + TOP
BODY_D = FRONT + PK_D + COVER
LEG_W, LEG_H = 6.5, 14.0
LEG_X = (-28.0, -20.0, 20.0, 28.0)   # two pairs, like the terminal Clawd, clear of the TF slot and reset
ARM_W, ARM_H, ARM_D = 7.0, 11.0, 5.0 # nubs; short in depth so the left one sits in front of the port opening
LIP = 0.5

def B(x0,x1,y0,y1,z0,z1):
    b = tbox(extents=[x1-x0, y1-y0, z1-z0]); b.apply_translation([(x0+x1)/2,(y0+y1)/2,(z0+z1)/2]); return b
def cyl_y(x,z,r,y0,y1):
    c = cylinder(radius=r, height=y1-y0, sections=48)
    c.apply_transform(trimesh.transformations.rotation_matrix(np.pi/2,[1,0,0])); c.apply_translation([x,(y0+y1)/2,z]); return c
def cyl_z(x,y,r,z0,z1):
    c = cylinder(radius=r, height=z1-z0, sections=48); c.apply_translation([x,y,(z0+z1)/2]); return c
def rbox_y(x0,x1,z0,z1,y0,y1,r):
    p = sbox(x0,z0,x1,z1).buffer(-r).buffer(r)
    m = trimesh.creation.extrude_polygon(p, y1-y0)
    m.apply_transform(trimesh.transformations.rotation_matrix(-np.pi/2,[1,0,0])); m.apply_translation([0,y0,0])
    # extrude_polygon put (x, z) in (x, y); the rotation maps that y to -z, so flip back
    m.apply_transform(np.diag([1,1,-1,1])); return m
U = lambda *a: trimesh.boolean.union(list(a), engine='manifold')
D = lambda a,*b: trimesh.boolean.difference([a]+list(b), engine='manifold')

# device frame helpers: device left edge x, top edge z, front face y
dx0 = -DEV/2
dz_top = FLOOR + CL + DEV
dy0 = FRONT
X = lambda from_left: dx0 + from_left
Z = lambda from_top: dz_top - from_top
zc = BODY_H/2

shell = rbox_y(-BODY_W/2, BODY_W/2, 0, BODY_H, 0, BODY_D, 4.0)
legs = [B(x-LEG_W/2, x+LEG_W/2, 0, BODY_D-4, -LEG_H, 0.5) for x in LEG_X]
arms = [B(s*BODY_W/2 - (ARM_W if s<0 else 0) - s*0.5, s*BODY_W/2 + (ARM_W if s>0 else 0) - s*0.5, 0, ARM_D, zc-ARM_H/2-4, zc+ARM_H/2-4) for s in (-1,1)]
body = U(shell, *legs, *arms)

cuts = []
cuts.append(rbox_y(X(WIN_X0), X(WIN_X1), Z(WIN_T1), Z(WIN_T0), -1, FRONT+0.01, 3.0))      # front window
# 45 degree chamfer on the window's outer edge, so a swipe doesn't catch on the frame
# (it opens toward the bed when printing face down: a 45 degree overhang, no supports)
CHAMFER = 1.5
for k in range(6):
    t = CHAMFER * (1 - k / 6)
    cuts.append(rbox_y(X(WIN_X0) - t, X(WIN_X1) + t, Z(WIN_T1) - t, Z(WIN_T0) + t,
                       -1, CHAMFER * (k + 1) / 6, 3.0 + t))
cuts.append(B(-PK/2, PK/2, FRONT, BODY_D+1, FLOOR, FLOOR+PK))                                # pocket, open at the back
cuts.append(B(-BODY_W/2-1, -PK/2+0.01, ARM_D+0.8, BODY_D+1, Z(PORT_T1), Z(PORT_T0)))         # left port opening
for k in range(4):                                                                             # right speaker slots
    z0 = Z(SPK_T1) + k*(SPK_T1-SPK_T0)/4
    cuts.append(B(PK/2-0.01, BODY_W/2+1, dy0+SPK_D0, dy0+SPK_D1, z0+1.0, z0+(SPK_T1-SPK_T0)/4-1.0))
cuts.append(B(X(TF_X0)-0.5, X(TF_X1)+0.5, dy0+BOT_D-2.0, dy0+BOT_D+2.0, -LEG_H-1, FLOOR+0.01))  # TF slot
cuts.append(cyl_z(X(RST_X), dy0+BOT_D, 2.2, -LEG_H-1, FLOOR+0.01))                              # reset key (pin)
cuts.append(cyl_z(X(TOP_X), dy0+DEV_D-TOP_FROM_BACK, TOP_R, BODY_H-TOP-0.5, BODY_H+1))         # top opening
SCREW_X = PK/2 + (BODY_W/2 - PK/2)/2
SCREW_Z = (8.0, BODY_H-8.0)
cuts += [cyl_y(s*SCREW_X, z, 1.25, BODY_D-COVER-10, BODY_D+1) for s in (-1,1) for z in SCREW_Z]
cuts.append(B(-BODY_W/2-1, BODY_W/2+1, BODY_D-COVER, BODY_D+1, -0.01, BODY_H+1))               # cover recess
body = D(body, *cuts)
body = U(body, *[B(x-LEG_W/2, x+LEG_W/2, 0, BODY_D-4, -LEG_H, 0) for x in LEG_X])
# the TF slot and reset must stay open through the legs' footprint too
body = D(body, cuts[-7] if False else B(X(TF_X0)-0.5, X(TF_X1)+0.5, dy0+BOT_D-2.0, dy0+BOT_D+2.0, -LEG_H-1, FLOOR+0.01))

# --- back cover: plate, a lip that rests on the Core2's back, screw holes, vents ---
cov = rbox_y(-BODY_W/2, BODY_W/2, 0, BODY_H, BODY_D-COVER, BODY_D, 4.0)
lip = B(-PK/2+CL, PK/2-CL, BODY_D-COVER-LIP, BODY_D-COVER+0.01, FLOOR+CL, FLOOR+PK-CL)
lip_in = B(-PK/2+CL+2, PK/2-CL-2, BODY_D-COVER-LIP-1, BODY_D-COVER+0.01, FLOOR+CL+2, FLOOR+PK-CL-2)
cover = D(U(cov, lip), lip_in,
          *[cyl_y(s*SCREW_X, z, 1.7, BODY_D-COVER-1, BODY_D+1) for s in (-1,1) for z in SCREW_Z],
          *[cyl_y(s*SCREW_X, z, 3.0, BODY_D-1.2, BODY_D+1) for s in (-1,1) for z in SCREW_Z],
          *[B(-15+i*6, -12+i*6, BODY_D-COVER-1, BODY_D+1, zc-9, zc+9) for i in range(6)],
          # the left port opening runs to the back face; notch the cover to match
          B(-BODY_W/2-1, -PK/2+0.01, BODY_D-COVER-1, BODY_D+1, Z(PORT_T1), Z(PORT_T0)))


def on_bed(m, face):
    """Rotate so `face` lies on the print bed: 'front' = the y-min face (the body's
    screen side), 'back' = the y-max face (the cover's outside). Model y becomes print
    z, measured away from that face. Checked: the face must be the largest area on
    the bed, and almost nothing may print flat in mid-air."""
    r = trimesh.transformations.rotation_matrix(np.pi/2 if face == 'front' else -np.pi/2, [1, 0, 0])
    m = m.copy(); m.apply_transform(r); m.apply_translation(-m.bounds[0])
    z0 = m.bounds[0, 2]
    down = m.face_normals[:, 2] < -0.99
    bed = m.area_faces[down & (np.abs(m.triangles_center[:, 2] - z0) < 0.01)].sum()
    air = m.area_faces[down & (m.triangles_center[:, 2] > z0 + 0.3)].sum()
    print(f"  print check ({face} down): {bed:.0f} mm2 on the bed, {air:.0f} mm2 flat in mid-air")
    assert bed > 4 * air, "upside down: more surface would print in mid-air than on the bed"
    return m

for name, m in (('clawd_core2_body', body), ('clawd_core2_back_cover', cover)):
    print(name, 'watertight', m.is_watertight, 'size', m.extents.round(1), 'vol cm3', round(m.volume/1000,1))
on_bed(body, 'front').export(OUT+'clawd_core2_body.stl')
on_bed(cover, 'back').export(OUT+'clawd_core2_back_cover.stl')
ft = trimesh.boolean.intersection([body, B(-60,60,-1,12,-30,80)], engine='manifold')
on_bed(ft, 'front').export(OUT+'fit_test_core2_front_12mm.stl')
# reference parts for the viewer
dev = B(-DEV/2, DEV/2, FRONT, FRONT+DEV_D, FLOOR+CL, FLOOR+CL+DEV)
scr = B(X(6.0), X(48.0), FRONT-0.4, FRONT-0.2, Z(42.6), Z(11.5))
for n,m in (('ref_body',body),('ref_cover',cover),('ref_module',dev),('ref_screen',scr)):
    m.export(OUT+'.'+n+'.stl')
if os.path.exists(OUT+'.ref_battery.stl'): os.remove(OUT+'.ref_battery.stl')
print('body W x H x D', BODY_W, round(BODY_H,1), BODY_D, '+ legs', LEG_H)
