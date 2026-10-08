"""Regenerate resources/app_icon.ico from the Fluent handoff logo.

Rounded square with a conic gradient (from 210deg: #0067C0 -> #60CDFF ->
#8BE1B0 -> #60CDFF -> #0067C0) and a white dot in the centre.
"""
import math
from PIL import Image, ImageDraw

STOPS = [(0.0, (0x00, 0x67, 0xC0)), (0.25, (0x60, 0xCD, 0xFF)), (0.5, (0x8B, 0xE1, 0xB0)),
         (0.75, (0x60, 0xCD, 0xFF)), (1.0, (0x00, 0x67, 0xC0))]


def sample(t):
    for (a, ca), (b, cb) in zip(STOPS, STOPS[1:]):
        if t <= b:
            k = (t - a) / (b - a)
            return tuple(round(ca[i] + (cb[i] - ca[i]) * k) for i in range(3))
    return STOPS[-1][1]


def render(size):
    ss = size * 4
    img = Image.new("RGBA", (ss, ss))
    px = img.load()
    c = (ss - 1) / 2
    for y in range(ss):
        for x in range(ss):
            # CSS conic-gradient: 0deg points up, angle grows clockwise.
            a = (math.degrees(math.atan2(x - c, c - y)) - 210) % 360
            px[x, y] = sample(a / 360) + (255,)

    mask = Image.new("L", (ss, ss), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, ss - 1, ss - 1), radius=round(ss * 5 / 18), fill=255)
    img.putalpha(mask)

    r = ss * 7 / 18 / 2
    ImageDraw.Draw(img).ellipse((c - r, c - r, c + r, c + r), fill=(255, 255, 255, 255))
    return img.resize((size, size), Image.LANCZOS)


sizes = [16, 20, 24, 28, 30, 32, 36, 40, 48, 60, 64, 72, 96, 128, 256]
frames = [render(s) for s in sizes]
frames[-1].save("resources/app_icon.ico", format="ICO", sizes=[(s, s) for s in sizes],
                append_images=frames[:-1])
print("wrote resources/app_icon.ico")
