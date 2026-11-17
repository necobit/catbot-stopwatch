"""Render the Catbot GLB into 8 directional sprites (tilted top-down view)
for the M5StopWatch firmware. Output: catbot_sprites.h + preview montage PNG."""
import numpy as np
import trimesh
from PIL import Image

import os
BASE = os.path.dirname(os.path.abspath(__file__))
GLB = os.path.join(BASE, 'catbot.glb')
OUT_H = os.path.join(BASE, 'CatbotWalk', 'catbot_sprites.h')
OUT_PNG = os.path.join(BASE, 'sprites_preview.png')

SS = 2                      # supersampling
W, H = 168, 208             # final sprite size
W2, H2 = W * SS, H * SS

mesh = trimesh.load(GLB, force='mesh', process=False)
verts = np.asarray(mesh.vertices, dtype=np.float64)          # mm, z-up, footprint center origin
faces = np.asarray(mesh.faces, dtype=np.int64)
cols = np.asarray(mesh.visual.vertex_colors, dtype=np.float64)[:, :3] / 255.0
# stored colors are linear-space -> back to sRGB for display
cols = np.where(cols <= 0.0031308, cols * 12.92, 1.055 * np.power(cols, 1 / 2.4) - 0.055)
face_col = cols[faces].mean(axis=1)
# body yellow -> pure yellow (255,255,0), per user's request (STEP color was #E8AC22)
_yellow = np.array([0.910, 0.678, 0.137])
_mask = (np.abs(face_col - _yellow) < 0.05).all(axis=1)
face_col[_mask] = np.array([1.0, 1.0, 0.0])
print('pure-yellow faces:', int(_mask.sum()), '/', len(face_col))

# face normals
v0, v1, v2 = verts[faces[:, 0]], verts[faces[:, 1]], verts[faces[:, 2]]
nrm = np.cross(v1 - v0, v2 - v0)
nrm /= (np.linalg.norm(nrm, axis=1, keepdims=True) + 1e-12)

LIGHT = np.array([0.55, -0.45, 0.75]); LIGHT /= np.linalg.norm(LIGHT)
TARGET = np.array([0.0, 0.0, 25.0])
EL = np.radians(52.0)       # sprite viewpoint elevation: how far we look down on the bot.
# Tune together with VIEW_TILT in the .ino when adjusting for the acrylic dome.
DIST = 128.0
FOCAL = (H2 / 2) / np.tan(np.radians(16.5))   # vertical half-fov 16.5 deg

def render(rot_deg):
    th = np.radians(rot_deg)
    c, s = np.cos(th), np.sin(th)
    Rz = np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
    V = verts @ Rz.T
    N = nrm @ Rz.T
    cam = TARGET + DIST * np.array([0.0, -np.cos(EL), np.sin(EL)])
    fwd = TARGET - cam; fwd /= np.linalg.norm(fwd)
    right = np.cross(fwd, [0, 0, 1.0]); right /= np.linalg.norm(right)
    up = np.cross(right, fwd)
    rel = V - cam
    cx = rel @ right; cy = rel @ up; cz = rel @ fwd
    sx = W2 / 2 + FOCAL * cx / cz
    sy = H2 / 2 - FOCAL * cy / cz
    shade = 0.40 + 0.60 * np.clip(N @ LIGHT, 0, 1)
    fc = face_col * shade[:, None]

    img = np.zeros((H2, W2, 3), dtype=np.float64)
    cov = np.zeros((H2, W2), dtype=np.float64)
    zbuf = np.full((H2, W2), 1e18)
    order = np.argsort(-cz[faces].mean(axis=1))   # far first (zbuf still resolves)
    P = np.stack([sx, sy], axis=1)
    for fi in order:
        tri = faces[fi]
        p = P[tri]; z = cz[tri]
        x0, y0 = p.min(axis=0); x1, y1 = p.max(axis=0)
        ix0, iy0 = max(int(np.floor(x0)), 0), max(int(np.floor(y0)), 0)
        ix1, iy1 = min(int(np.ceil(x1)), W2 - 1), min(int(np.ceil(y1)), H2 - 1)
        if ix0 > ix1 or iy0 > iy1:
            continue
        d = (p[1, 1] - p[2, 1]) * (p[0, 0] - p[2, 0]) + (p[2, 0] - p[1, 0]) * (p[0, 1] - p[2, 1])
        if abs(d) < 1e-9:
            continue
        xs = np.arange(ix0, ix1 + 1) + 0.5
        ys = np.arange(iy0, iy1 + 1) + 0.5
        gx, gy = np.meshgrid(xs, ys)
        w0 = ((p[1, 1] - p[2, 1]) * (gx - p[2, 0]) + (p[2, 0] - p[1, 0]) * (gy - p[2, 1])) / d
        w1 = ((p[2, 1] - p[0, 1]) * (gx - p[2, 0]) + (p[0, 0] - p[2, 0]) * (gy - p[2, 1])) / d
        w2 = 1 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any():
            continue
        zz = w0 * z[0] + w1 * z[1] + w2 * z[2]
        sub = zbuf[iy0:iy1 + 1, ix0:ix1 + 1]
        hit = inside & (zz < sub)
        if not hit.any():
            continue
        sub[hit] = zz[hit]
        img[iy0:iy1 + 1, ix0:ix1 + 1][hit] = fc[fi]
        cov[iy0:iy1 + 1, ix0:ix1 + 1][hit] = 1.0
    # downsample
    img4 = img.reshape(H, SS, W, SS, 3).sum(axis=(1, 3))
    cov4 = cov.reshape(H, SS, W, SS).sum(axis=(1, 3))
    alpha = cov4 / (SS * SS)
    rgb = np.zeros((H, W, 3))
    nz = cov4 > 0
    rgb[nz] = img4[nz] / cov4[nz][:, None]
    return rgb, alpha

# anchor = projection of model origin (footprint center, z=0), same for all rotations
_cam = TARGET + DIST * np.array([0.0, -np.cos(EL), np.sin(EL)])
_fwd = TARGET - _cam; _fwd /= np.linalg.norm(_fwd)
_right = np.cross(_fwd, [0, 0, 1.0]); _right /= np.linalg.norm(_right)
_up = np.cross(_right, _fwd)
_rel = np.array([0.0, 0.0, 0.0]) - _cam
_ax = (W2 / 2 + FOCAL * (_rel @ _right) / (_rel @ _fwd)) / SS
_ay = (H2 / 2 - FOCAL * (_rel @ _up) / (_rel @ _fwd)) / SS
print('anchor: %.1f, %.1f' % (_ax, _ay))

sprites = []
for k in range(8):
    rgb, alpha = render(k * 45.0)
    sprites.append((rgb, alpha))
    ys, xs = np.where(alpha > 0.5)
    print('sprite %d (rot %3d): bbox x %d..%d y %d..%d' % (k, k * 45, xs.min(), xs.max(), ys.min(), ys.max()))

# preview montage on checkerboard-ish sky bg
mont = Image.new('RGB', (W * 8, H), (0x87, 0xce, 0xeb))
for k, (rgb, alpha) in enumerate(sprites):
    im = Image.fromarray((np.dstack([rgb, alpha]) * 255).astype(np.uint8), 'RGBA')
    mont.paste(im, (k * W, 0), im)
mont.save(OUT_PNG)

# ---- C header: RGB565 + alpha, row-major, per sprite ----
def to565(r, g, b):
    return ((int(r * 255) >> 3) << 11) | ((int(g * 255) >> 2) << 5) | (int(b * 255) >> 3)

with open(OUT_H, 'w') as f:
    f.write('// Catbot directional sprites, generated by render_sprites.py from catbot.glb\n')
    f.write('// %d sprites x %dx%d, RGB565 + 8-bit alpha. Sprite k = model rotated k*45 deg CCW (z-up).\n' % (len(sprites), W, H))
    f.write('// Firmware mapping: k = round(((headingDeg + 90) mod 360) / 45) mod 8.\n')
    f.write('#pragma once\n#include <stdint.h>\n\n')
    f.write('static const int CATBOT_SPR_W = %d;\nstatic const int CATBOT_SPR_H = %d;\n' % (W, H))
    f.write('static const int CATBOT_ANCHOR_X = %d;\nstatic const int CATBOT_ANCHOR_Y = %d;\n\n' % (round(_ax), round(_ay)))
    for k, (rgb, alpha) in enumerate(sprites):
        flat565 = []
        flata = []
        for y in range(H):
            for x in range(W):
                flat565.append(to565(rgb[y, x, 0], rgb[y, x, 1], rgb[y, x, 2]))
                flata.append(int(alpha[y, x] * 255))
        f.write('static const uint16_t catbot_spr%d_rgb[%d] PROGMEM = {%s};\n' % (k, W * H, ','.join(str(v) for v in flat565)))
        f.write('static const uint8_t catbot_spr%d_a[%d] PROGMEM = {%s};\n\n' % (k, W * H, ','.join(str(v) for v in flata)))
    f.write('static const uint16_t* const catbot_spr_rgb[8] = {%s};\n' % ','.join('catbot_spr%d_rgb' % k for k in range(8)))
    f.write('static const uint8_t* const catbot_spr_a[8] = {%s};\n' % ','.join('catbot_spr%d_a' % k for k in range(8)))
import os
print('header bytes:', os.path.getsize(OUT_H), 'preview:', OUT_PNG)
