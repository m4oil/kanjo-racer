#!/usr/bin/env python3
"""
Generates ALL placeholder art for the Kanjo Loop starter (original, no ripped assets):
  graphics/civic.bmp  + civic.json   - 32-angle top-down sprite sheet (16x16 frames), white EF-style hatch
  graphics/rival.bmp  + rival.json   - same shape, blue
  graphics/track.bmp  + track.json   - 512x512 elliptical expressway loop at night
  graphics/title.bmp  + title.json   - 256x256 title screen: night skyline, road, bear logo

Requires Pillow:  pip install pillow
Run from the project root:  python tools/make_assets.py

Replace these with hand-drawn pixel art whenever you like. Keep the same file
names, sizes and palette rules and the code won't need to change.
"""
import json
import math
import os

from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
GFX = os.path.join(ROOT, "graphics")
os.makedirs(GFX, exist_ok=True)

# ---------------------------------------------------------------- shared track geometry
# MUST match the constants at the top of src/main.cpp
CX, CY = 256, 256
A_O, B_O = 212, 180   # outer edge ellipse radii
A_I, B_I = 156, 124   # inner edge ellipse radii
A_C, B_C = 184, 152   # center line ellipse radii


def save_bmp(img, path, palette_rgb):
    """Save a P-mode image as BMP with a palette padded to exactly 16 colors (4bpp-friendly)."""
    pal = list(palette_rgb)
    assert len(pal) <= 16, "more than 16 colors"
    while len(pal) < 16:
        pal.append((0, 0, 0))
    flat = []
    for r, g, b in pal:
        flat += [r, g, b]
    img.putpalette(flat)
    img.save(path, format="BMP")


# ---------------------------------------------------------------- car sprites
S = 8            # supersample factor
FRAMES = 32
FR = 16          # frame size in pixels


def car_palette(body, shade):
    return [
        (255, 0, 255),    # 0 transparent (must be first)
        body,             # 1 body
        shade,            # 2 body shade
        (22, 26, 40),     # 3 glass
        (235, 30, 40),    # 4 tail lights
        (255, 236, 140),  # 5 head lights
        (10, 10, 12),     # 6 tires
    ]


def draw_car_hires(pal):
    """EF9-ish hatch seen from above, nose pointing UP. Canvas is FR*S square."""
    size = FR * S
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    body, shade, glass, tail, head, tire = (pal[1], pal[2], pal[3], pal[4], pal[5], pal[6])

    # tires poking out of the sides
    for y0, y1 in ((20, 38), (86, 104)):
        d.rectangle((25, y0, 33, y1), fill=tire)
        d.rectangle((95, y0, 103, y1), fill=tire)
    # body (shade outline, lighter inside)
    d.rounded_rectangle((31, 5, 97, 123), radius=18, fill=shade)
    d.rounded_rectangle((37, 8, 91, 120), radius=15, fill=body)
    # windshield, short boxy hood in front of it
    d.polygon([(43, 42), (85, 42), (81, 58), (47, 58)], fill=glass)
    # hatch rear glass, steeper and shorter
    d.polygon([(47, 84), (81, 84), (84, 98), (44, 98)], fill=glass)
    # roof pillars/side glass hint
    d.rectangle((41, 58, 45, 84), fill=glass)
    d.rectangle((83, 58, 87, 84), fill=glass)
    # hatch crease / small spoiler line
    d.rectangle((42, 101, 86, 105), fill=shade)
    # head lights and tail lights
    d.rectangle((36, 7, 48, 14), fill=head)
    d.rectangle((80, 7, 92, 14), fill=head)
    d.rectangle((35, 113, 49, 120), fill=tail)
    d.rectangle((79, 113, 93, 120), fill=tail)
    return im


def make_car_sheet(name, body, shade):
    pal = car_palette(body, shade)
    hires = draw_car_hires(pal)
    sheet = Image.new("P", (FR, FR * FRAMES), 0)
    sp = sheet.load()
    for i in range(FRAMES):
        ang = i * 360.0 / FRAMES                      # clockwise from "up"
        rot = hires.rotate(-ang, resample=Image.NEAREST, center=(FR * S // 2, FR * S // 2))
        small = rot.resize((FR, FR), Image.BOX)       # premultiplied resize, smooth edges
        px = small.load()
        for y in range(FR):
            for x in range(FR):
                r, g, b, a = px[x, y]
                if a < 128:
                    idx = 0
                else:
                    idx = min(range(1, len(pal)),
                              key=lambda k: (pal[k][0] - r) ** 2 + (pal[k][1] - g) ** 2 + (pal[k][2] - b) ** 2)
                sp[x, i * FR + y] = idx
    save_bmp(sheet, os.path.join(GFX, name + ".bmp"), pal)
    with open(os.path.join(GFX, name + ".json"), "w") as f:
        json.dump({"type": "sprite", "height": FR}, f)
    return sheet


# ---------------------------------------------------------------- track
def make_track():
    pal = [
        (255, 0, 255),    # 0 unused (transparent slot)
        (12, 14, 26),     # 1 night ground
        (46, 48, 58),     # 2 asphalt
        (150, 156, 172),  # 3 guardrail line
        (240, 196, 80),   # 4 center dashes (sodium yellow)
        (255, 160, 50),   # 5 street lamps
        (235, 235, 235),  # 6 start line white
    ]
    W = 512
    im = Image.new("P", (W, W), 1)
    px = im.load()

    def e(dx, dy, a, b):
        return math.sqrt((dx / a) ** 2 + (dy / b) ** 2)

    for y in range(W):
        for x in range(W):
            dx, dy = x + 0.5 - CX, y + 0.5 - CY
            do_, di = e(dx, dy, A_O, B_O), e(dx, dy, A_I, B_I)
            if do_ <= 1.0 and di >= 1.0:
                c = 2
                if (1.0 - do_) * 195 < 2.0 or (di - 1.0) * 140 < 2.0:
                    c = 3
                else:
                    dc = e(dx, dy, A_C, B_C)
                    if abs(dc - 1.0) < 0.009:
                        ang = math.atan2(dx, -dy) % (2 * math.pi)
                        if int(ang / (2 * math.pi) * 48) % 2 == 0:
                            c = 4
                px[x, y] = c

    # street lamps just outside both edges
    n = 28
    for k in range(n):
        t = 2 * math.pi * (k + 0.5) / n
        for a, b in ((A_O + 8, B_O + 8), (A_I - 8, B_I - 8)):
            lx, ly = int(CX + a * math.sin(t)), int(CY - b * math.cos(t))
            for ox in range(-1, 2):
                for oy in range(-1, 2):
                    if 0 <= lx + ox < W and 0 <= ly + oy < W:
                        px[lx + ox, ly + oy] = 5

    # checkered start line at the top, just right of center (cars cross it left -> right)
    for y in range(CY - B_O + 3, CY - B_I - 2):
        for x in range(CX, CX + 8):
            if ((x - CX) // 4 + (y // 4)) % 2 == 0:
                px[x, y] = 6

    save_bmp(im, os.path.join(GFX, "track.bmp"), pal)
    with open(os.path.join(GFX, "track.json"), "w") as f:
        json.dump({"type": "regular_bg"}, f)


# ---------------------------------------------------------------- title screen
def make_title():
    """Layout is for the 240x160 screen, which shows image x 8..248, y 48..208.
    Screen y 4-36: bear logo | 74-104: skyline | 100-160: road.
    The title text, dedication and the driving car are drawn by main.cpp on top."""
    import random
    pal = [
        (255, 0, 255),    # 0 unused
        (10, 12, 30),     # 1 sky top
        (24, 22, 56),     # 2 sky mid
        (70, 32, 84),     # 3 sky horizon glow
        (8, 8, 18),       # 4 skyline silhouette
        (255, 200, 90),   # 5 lit windows
        (40, 42, 52),     # 6 road
        (240, 196, 80),   # 7 center dashes
        (255, 160, 50),   # 8 lamps
        (150, 95, 50),    # 9 bear fur
        (95, 58, 30),     # 10 bear dark fur
        (225, 185, 130),  # 11 bear snout
        (10, 10, 12),     # 12 outline / eyes
        (240, 240, 240),  # 13 white
        (150, 156, 172),  # 14 guardrail
    ]
    W = 256
    im = Image.new("P", (W, W), 1)
    d = ImageDraw.Draw(im)

    # sky bands (flat bands keep the tile count low)
    d.rectangle((0, 0, W, 90), fill=1)
    d.rectangle((0, 91, W, 118), fill=2)
    d.rectangle((0, 119, W, 150), fill=3)

    # skyline
    rnd = random.Random(1989)
    x = 0
    while x < W:
        bw = rnd.choice((12, 16, 20, 24))
        bh = rnd.choice((18, 26, 34, 44))
        top = 150 - bh
        d.rectangle((x, top, x + bw - 2, 150), fill=4)
        for wy in range(top + 4, 146, 8):
            for wx in range(x + 3, x + bw - 4, 6):
                if rnd.random() < 0.35:
                    d.rectangle((wx, wy, wx + 1, wy + 2), fill=5)
        x += bw

    # road and guardrail
    d.rectangle((0, 150, W, W), fill=6)
    d.rectangle((0, 150, W, 151), fill=14)
    for x0 in range(0, W, 32):
        d.rectangle((x0 + 4, 203, x0 + 19, 204), fill=7)

    # street lamps along the rail
    for x0 in range(20, W, 56):
        d.rectangle((x0, 134, x0, 150), fill=4)
        d.rectangle((x0 - 1, 131, x0 + 2, 133), fill=8)

    # bear logo, centered at image (128, 68) -> screen (120, 20)
    bx, by = 128, 68
    for ex in (-13, 13):
        d.ellipse((bx + ex - 6, by - 20, bx + ex + 6, by - 8), fill=12)
        d.ellipse((bx + ex - 4, by - 18, bx + ex + 4, by - 10), fill=10)
    d.ellipse((bx - 17, by - 16, bx + 17, by + 16), fill=12)
    d.ellipse((bx - 15, by - 14, bx + 15, by + 14), fill=9)
    d.ellipse((bx - 8, by - 1, bx + 8, by + 12), fill=11)
    d.rectangle((bx - 3, by + 1, bx + 3, by + 4), fill=12)           # nose
    d.rectangle((bx - 9, by - 7, bx - 6, by - 4), fill=12)           # eyes
    d.rectangle((bx + 6, by - 7, bx + 9, by - 4), fill=12)
    d.point((bx - 8, by - 7), fill=13)
    d.point((bx + 7, by - 7), fill=13)

    save_bmp(im, os.path.join(GFX, "title.bmp"), pal)
    with open(os.path.join(GFX, "title.json"), "w") as f:
        json.dump({"type": "regular_bg"}, f)


if __name__ == "__main__":
    make_car_sheet("civic", (240, 240, 244), (160, 166, 184))
    make_car_sheet("rival", (50, 100, 230), (24, 48, 130))
    make_track()
    make_title()
    print("assets written to", os.path.abspath(GFX))
