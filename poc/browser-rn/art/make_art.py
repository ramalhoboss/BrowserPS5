# BrowserPS5 RN's icon and home background (2026-11-10), in the PS5 home
# screen's blues: icon0.png 512x512, pic0.png 1920x1080 and pic0.raw (RGB,
# for ../../tests-v1/bc7dds.c -> pic0.dds / pic1.dds).
#   python -I make_art.py <out dir>
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

out = sys.argv[1]
FONT = r'C:\Windows\Fonts\segoeuib.ttf'
FONT_LIGHT = r'C:\Windows\Fonts\segoeuil.ttf'


def gradient(w, h, top, bottom):
    img = Image.new('RGB', (w, h))
    px = img.load()
    for y in range(h):
        t = y / (h - 1)
        row = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(w):
            px[x, y] = row
    return img


def glow(img, cx, cy, r, color, alpha):
    layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=color + (alpha,))
    layer = layer.filter(ImageFilter.GaussianBlur(r / 2))
    return Image.alpha_composite(img.convert('RGBA'), layer)


def streaks(img, n, seed):
    # The PS5 home's soft light bands, drawn as wide blurred arcs.
    w, h = img.size
    layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for i in range(n):
        a = (seed * 7 + i * 13) % 17 / 17
        y0 = int(h * (0.35 + 0.5 * a))
        pts = [(x, y0 - int(h * 0.18 * math.sin(math.pi * x / w + i))) for x in range(0, w + 1, 8)]
        d.line(pts, fill=(120, 170, 255, 26), width=int(h * 0.05))
    layer = layer.filter(ImageFilter.GaussianBlur(h * 0.03))
    return Image.alpha_composite(img.convert('RGBA'), layer)


def globe_on(img, cx, cy, r, width, color):
    # On its own layer: the colour's alpha is kept (ImageDraw on RGB drops it).
    layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
    globe(ImageDraw.Draw(layer), cx, cy, r, width, color)
    return Image.alpha_composite(img.convert('RGBA'), layer)


def globe(d, cx, cy, r, width, color):
    d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=color, width=width)
    for k in (0.45, 0.85):
        d.ellipse((cx - r * k, cy - r, cx + r * k, cy + r), outline=color, width=max(2, width // 2))
    d.line((cx, cy - r, cx, cy + r), fill=color, width=max(2, width // 2))
    for k in (-0.5, 0, 0.5):
        y = cy + r * k
        half = r * math.sqrt(1 - k * k)
        d.line((cx - half, y, cx + half, y), fill=color, width=max(2, width // 2))


# icon0.png
S = 512
icon = gradient(S, S, (18, 52, 128), (3, 10, 32))
icon = glow(icon, S * 0.3, S * 0.25, S * 0.45, (60, 130, 255), 120)
icon = streaks(icon, 3, 1)
icon = globe_on(icon, S * 0.5, S * 0.36, S * 0.2, 11, (255, 255, 255, 240))
d = ImageDraw.Draw(icon)
f = ImageFont.truetype(FONT, 88)
text = 'RN'
d.text(((S - d.textlength(text, font=f)) / 2, S * 0.56), text, font=f, fill=(255, 255, 255, 255))
f2 = ImageFont.truetype(FONT_LIGHT, 44)
t2 = 'BrowserPS5'
d.text(((S - d.textlength(t2, font=f2)) / 2, S * 0.80), t2, font=f2, fill=(190, 210, 255, 255))
icon.convert('RGB').save(os.path.join(out, 'icon0.png'))

# pic0.png and pic0.raw: the home screen's background behind the tile
W, H = 1920, 1080
bg = gradient(W, H, (10, 34, 92), (0, 6, 22))
bg = glow(bg, W * 0.78, H * 0.2, H * 0.55, (40, 110, 255), 90)
bg = glow(bg, W * 0.15, H * 0.95, H * 0.5, (0, 60, 160), 70)
bg = streaks(bg, 5, 3)
bg = globe_on(bg, W * 0.8, H * 0.42, H * 0.2, 6, (255, 255, 255, 46))
bg = bg.convert('RGB')
bg.save(os.path.join(out, 'pic0.png'))
with open(os.path.join(out, 'pic0.raw'), 'wb') as raw:
    raw.write(bg.tobytes())
print('art: icon0.png, pic0.png, pic0.raw in', out)
