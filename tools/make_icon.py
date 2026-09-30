#!/usr/bin/env python3
"""Render the port's application icon (original artwork, see assets/icon/DESIGN.md)
and export PNG sizes plus a Windows .ico.

    python tools/make_icon.py            -> assets/icon/sealteam_1024.png, sealteam.ico, sealteam_{16..256}.png
"""
import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'assets', 'icon')
S = 1024  # master size
SS = 4    # supersampling for the vector parts


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def vgrad(w, h, stops):
    """Vertical gradient image from (t, colour) stops."""
    im = Image.new('RGB', (1, h))
    px = im.load()
    for y in range(h):
        t = y / (h - 1)
        for i in range(len(stops) - 1):
            t0, c0 = stops[i]
            t1, c1 = stops[i + 1]
            if t0 <= t <= t1:
                px[0, y] = lerp(c0, c1, (t - t0) / (t1 - t0) if t1 > t0 else 0)
                break
    return im.resize((w, h))


def rounded_mask(size, radius):
    m = Image.new('L', (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    return m


def jungle(size):
    """Backdrop: humid sky glow fading into layered dark foliage."""
    im = vgrad(size, size, [
        (0.00, (150, 176, 150)), (0.22, (108, 138, 108)), (0.45, (46, 74, 46)),
        (0.72, (18, 38, 20)), (1.00, (8, 18, 10)),
    ])
    d = ImageDraw.Draw(im, 'RGBA')
    rnd = random.Random(7)
    # Foliage layers: soft blobs, darker and larger toward the front.
    for layer in range(4):
        depth = layer / 3
        col = lerp((70, 104, 66), (10, 24, 12), depth)
        n = 18 + layer * 10
        for _ in range(n):
            cx = rnd.uniform(-0.1, 1.1) * size
            cy = (0.30 + 0.16 * layer + rnd.uniform(-0.06, 0.10)) * size
            r = (0.05 + 0.06 * depth) * size * rnd.uniform(0.6, 1.4)
            d.ellipse([cx - r, cy - r * 0.7, cx + r, cy + r * 0.7], fill=col + (200,))
    # Palm fronds on the right horizon: thin arcs.
    for k in range(9):
        ang = -0.25 - k * 0.09
        x0, y0 = size * 0.86, size * 0.40
        pts = [(x0 + math.cos(ang - i * 0.05) * i * size * 0.010,
                y0 + math.sin(ang - i * 0.05) * i * size * 0.010) for i in range(28)]
        d.line(pts, fill=(22, 44, 24, 230), width=max(3, size // 170))
    im = im.filter(ImageFilter.GaussianBlur(size * 0.008))
    # Ground band.
    d = ImageDraw.Draw(im, 'RGBA')
    d.rectangle([0, size * 0.90, size, size], fill=(10, 18, 10, 255))
    return im


def soldier(size):
    """Camouflaged figure, left half: helmet with band, face in shadow,
    shoulder mass and a rifle rising across the chest."""
    import numpy as np
    im = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    s = size
    body = (26, 34, 22, 255)
    hx, hy = s * 0.27, s * 0.35
    # Torso / shoulder mass.
    d.polygon([(0, s), (0, s * 0.64), (s * 0.08, s * 0.58), (s * 0.24, s * 0.53),
               (s * 0.42, s * 0.56), (s * 0.50, s * 0.68), (s * 0.48, s)], fill=body)
    # Neck.
    d.rectangle([hx - s * 0.07, hy + s * 0.12, hx + s * 0.07, hy + s * 0.24], fill=(40, 34, 26, 255))
    # Helmet dome and brim.
    d.ellipse([hx - s * 0.18, hy - s * 0.18, hx + s * 0.18, hy + s * 0.14], fill=body)
    d.polygon([(hx - s * 0.23, hy + s * 0.01), (hx + s * 0.21, hy - s * 0.02),
               (hx + s * 0.19, hy + s * 0.05), (hx - s * 0.21, hy + s * 0.07)], fill=body)
    # Camouflage on helmet and torso: a few large torn patches.
    patches = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    pd = ImageDraw.Draw(patches)
    rnd = random.Random(11)
    cols = [(108, 118, 62, 255), (66, 82, 44, 255), (134, 116, 70, 255)]
    for _ in range(26):
        cx, cy = rnd.uniform(0, s * 0.50), rnd.uniform(s * 0.18, s)
        r = rnd.uniform(s * 0.05, s * 0.11)
        pts = [(cx + math.cos(a) * r * rnd.uniform(0.55, 1.25), cy + math.sin(a) * r * rnd.uniform(0.55, 1.25))
               for a in [i * math.pi / 3.5 for i in range(7)]]
        pd.polygon(pts, fill=rnd.choice(cols))
    clip = np.minimum(np.asarray(patches.split()[3]), np.asarray(im.split()[3]))
    patches.putalpha(Image.fromarray(clip))
    im.alpha_composite(patches)
    d = ImageDraw.Draw(im)
    # Helmet band and brim shadow.
    d.polygon([(hx - s * 0.19, hy - s * 0.01), (hx + s * 0.18, hy - s * 0.04),
               (hx + s * 0.18, hy + s * 0.01), (hx - s * 0.19, hy + s * 0.04)], fill=(52, 60, 40, 255))
    # Face in deep shadow with a lit cheek and jaw line.
    face = [(hx - s * 0.13, hy + s * 0.05), (hx + s * 0.12, hy + s * 0.03), (hx + s * 0.11, hy + s * 0.16),
            (hx + s * 0.04, hy + s * 0.24), (hx - s * 0.06, hy + s * 0.24), (hx - s * 0.13, hy + s * 0.16)]
    d.polygon(face, fill=(14, 14, 12, 255))
    d.polygon([(hx + s * 0.02, hy + s * 0.06), (hx + s * 0.11, hy + s * 0.05), (hx + s * 0.10, hy + s * 0.15),
               (hx + s * 0.04, hy + s * 0.21)], fill=(70, 58, 44, 255))
    # Rifle: dark bar with a lighter top edge, magazine and grip.
    gun = (12, 14, 12, 255)
    d.polygon([(s * 0.33, s * 0.92), (s * 0.39, s * 0.92), (s * 0.66, s * 0.42), (s * 0.60, s * 0.41)], fill=gun)
    d.line([(s * 0.36, s * 0.90), (s * 0.63, s * 0.42)], fill=(92, 96, 84, 255), width=max(2, s // 160))
    d.polygon([(s * 0.45, s * 0.70), (s * 0.53, s * 0.73), (s * 0.50, s * 0.84), (s * 0.42, s * 0.81)], fill=gun)
    d.polygon([(s * 0.40, s * 0.80), (s * 0.45, s * 0.81), (s * 0.43, s * 0.90), (s * 0.38, s * 0.89)], fill=gun)
    # Rim light on the helmet crown and shoulder.
    rim = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    rd = ImageDraw.Draw(rim)
    rd.arc([hx - s * 0.18, hy - s * 0.18, hx + s * 0.18, hy + s * 0.14], 195, 330,
           fill=(196, 214, 170, 170), width=max(3, s // 100))
    rd.line([(s * 0.08, s * 0.58), (s * 0.24, s * 0.53), (s * 0.42, s * 0.56)],
            fill=(160, 176, 130, 120), width=max(3, s // 110))
    im.alpha_composite(rim.filter(ImageFilter.GaussianBlur(s * 0.0035)))
    return im


def gold_text(size, text, font, top, right):
    """Beveled gold lettering: dark drop, amber base, bright upper faces."""
    layer = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    tmp = Image.new('L', (size, size), 0)
    td = ImageDraw.Draw(tmp)
    bbox = td.textbbox((0, 0), text, font=font)
    w = bbox[2] - bbox[0]
    x = right - w - bbox[0]
    y = top - bbox[1]
    td.text((x, y), text, font=font, fill=255)
    mask = tmp
    # Drop shadow.
    sh = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    sh.paste((0, 0, 0, 200), mask=mask.filter(ImageFilter.GaussianBlur(size * 0.012)))
    layer.alpha_composite(sh, (int(size * 0.012), int(size * 0.018)))
    # Base gradient: bright top face to burnt amber bottom, per glyph band.
    grad = vgrad(size, size, [(0, (255, 236, 150)), (0.5, (232, 176, 48)), (1, (150, 84, 12))])
    band = Image.new('L', (size, size), 0)
    ImageDraw.Draw(band).rectangle([0, top, size, top + (bbox[3] - bbox[1])], fill=255)
    # Map the gradient to the text's vertical extent.
    g = grad.crop((0, 0, size, size)).resize((size, max(1, bbox[3] - bbox[1])))
    base = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    base.paste(g, (0, top))
    base.putalpha(mask)
    layer.alpha_composite(base)
    # Bevel: highlight offset up-left, shade offset down-right, both clipped to the glyphs.
    edge = max(2, size // 180)
    hi = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    hi.paste((255, 250, 210, 255), mask=mask)
    inner = mask.filter(ImageFilter.MinFilter(edge * 2 + 1))
    hi_only = Image.new('L', (size, size), 0)
    hi_only.paste(mask, (-edge, -edge))
    import numpy as np
    m_arr = np.asarray(mask)
    rim_hi = np.minimum(m_arr, 255 - np.asarray(inner))
    up_left = np.asarray(hi_only)
    hi_mask = Image.fromarray(np.minimum(rim_hi, up_left))
    lo_only = Image.new('L', (size, size), 0)
    lo_only.paste(mask, (edge, edge))
    lo_mask = Image.fromarray(np.minimum(rim_hi, np.asarray(lo_only)))
    hi.putalpha(hi_mask)
    layer.alpha_composite(hi)
    lo = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    lo.paste((96, 48, 6, 255), mask=lo_mask)
    layer.alpha_composite(lo)
    return layer


def render(size):
    im = jungle(size).convert('RGBA')
    im.alpha_composite(soldier(size))
    font_path = None
    for cand in ['C:/Windows/Fonts/impact.ttf', 'C:/Windows/Fonts/arialbd.ttf']:
        if os.path.exists(cand):
            font_path = cand
            break
    font = ImageFont.truetype(font_path, int(size * 0.30)) if font_path else ImageFont.load_default()
    im.alpha_composite(gold_text(size, 'SEAL', font, int(size * 0.10), int(size * 0.96)))
    im.alpha_composite(gold_text(size, 'TEAM', font, int(size * 0.40), int(size * 0.97)))
    # Vignette and rounded frame.
    vig = Image.new('L', (size, size), 0)
    ImageDraw.Draw(vig).ellipse([-size * 0.2, -size * 0.2, size * 1.2, size * 1.2], fill=255)
    vig = vig.filter(ImageFilter.GaussianBlur(size * 0.15))
    dark = Image.new('RGBA', (size, size), (0, 0, 0, 110))
    dark.putalpha(Image.eval(vig, lambda v: 110 - v * 110 // 255))
    im.alpha_composite(dark)
    im.putalpha(rounded_mask(size, int(size * 0.18)))
    return im


def main():
    os.makedirs(OUT, exist_ok=True)
    master = render(S)
    master.save(os.path.join(OUT, 'sealteam_1024.png'))
    sizes = [256, 128, 64, 48, 32, 16]
    icons = []
    for s in sizes:
        im = master.resize((s, s), Image.LANCZOS)
        im.save(os.path.join(OUT, f'sealteam_{s}.png'))
        icons.append(im)
    icons[0].save(os.path.join(OUT, 'sealteam.ico'), format='ICO', sizes=[(s, s) for s in sizes],
                  append_images=icons[1:])
    print('icon written to', os.path.abspath(OUT))


if __name__ == '__main__':
    main()
