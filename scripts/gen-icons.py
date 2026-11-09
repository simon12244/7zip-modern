#!/usr/bin/env python3
"""
7-Zip Modern - toolbar icon generator.

Replaces the legacy Win9x-style bitmaps (thick black outlines, pure #00FF00
fills, magenta colour keys) with modern flat icons: rounded geometry, soft
tinted fills, 2px strokes and a restrained palette.

Output matches the exact specs the 7-Zip toolbar expects:
    <Name>.bmp    48x36   large buttons
    <Name>2.bmp   24x24   small buttons
Background stays magenta (0xFF00FF) because the image list is created with
ILC_MASK and derives its transparency mask from that colour key.

Usage:  python scripts/gen-icons.py [--preview out.png]
"""

import os
import sys
from PIL import Image, ImageDraw

SS = 8  # supersampling factor

# ---------------------------------------------------------------- palette

BLUE = (59, 130, 246)
GREEN = (16, 185, 129)
INDIGO = (99, 102, 241)
SKY = (14, 165, 233)
AMBER = (245, 158, 11)
RED = (239, 68, 68)
SLATE = (100, 116, 139)

KEY = (255, 0, 255)  # magenta transparency key
INK = (30, 41, 59)   # dark accent used for a few details


def tint(color, alpha=0.16):
    """Soft translucent fill for the given stroke colour."""
    return (color[0], color[1], color[2], int(255 * alpha))


# ---------------------------------------------------------------- drawing
#
# Every painter receives (draw, w, h, stroke) and draws in the *scaled*
# supersampled coordinate space. Coordinates are expressed relative to the
# canvas so the same code renders both the 48x36 and 24x24 variants.


class Canvas:
    def __init__(self, size, pad_ratio=0.10):
        self.size = size
        self.w, self.h = size
        self.s = max(self.w, self.h)
        # A thin icon inside a big canvas: keep a small even margin.
        self.m = self.s * pad_ratio

    def box(self):
        return (self.m, self.m, self.w - self.m, self.h - self.m)

    def x(self, f):
        x0, _, x1, _ = self.box()
        return x0 + (x1 - x0) * f

    def y(self, f):
        _, y0, _, y1 = self.box()
        return y0 + (y1 - y0) * f

    def r(self, f):
        return self.s * f


def draw_add(d, c, stroke):
    d.rounded_rectangle(c.box(), radius=c.r(0.10), fill=tint(BLUE),
                        outline=BLUE, width=stroke)
    d.line([(c.x(0.5), c.y(0.28)), (c.x(0.5), c.y(0.72))],
           fill=BLUE, width=stroke)
    d.line([(c.x(0.28), c.y(0.5)), (c.x(0.72), c.y(0.5))],
           fill=BLUE, width=stroke)


def draw_extract(d, c, stroke):
    # open tray
    d.line([(c.x(0.16), c.y(0.40)), (c.x(0.16), c.y(0.80)),
            (c.x(0.84), c.y(0.80)), (c.x(0.84), c.y(0.40))],
           fill=GREEN, width=stroke, joint="curve")
    # downward arrow into the tray
    d.line([(c.x(0.5), c.y(0.14)), (c.x(0.5), c.y(0.58))],
           fill=GREEN, width=stroke)
    d.line([(c.x(0.34), c.y(0.44)), (c.x(0.5), c.y(0.60)),
            (c.x(0.66), c.y(0.44))],
           fill=GREEN, width=stroke, joint="curve")


def draw_test(d, c, stroke):
    cx, cy, rad = c.x(0.42), c.y(0.42), c.r(0.26)
    d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad],
              fill=tint(INDIGO), outline=INDIGO, width=stroke)
    d.line([(c.x(0.62), c.y(0.62)), (c.x(0.86), c.y(0.86))],
           fill=INDIGO, width=stroke)
    d.line([(c.x(0.30), c.y(0.42)), (c.x(0.39), c.y(0.52)),
            (c.x(0.56), c.y(0.32))],
           fill=INDIGO, width=stroke, joint="curve")


def draw_copy(d, c, stroke):
    # back sheet
    d.rounded_rectangle([c.x(0.12), c.y(0.12), c.x(0.64), c.y(0.72)],
                        radius=c.r(0.08), fill=tint(SKY, 0.10),
                        outline=SKY, width=stroke)
    # front sheet, tinted so it reads as the focused one
    d.rounded_rectangle([c.x(0.36), c.y(0.28), c.x(0.88), c.y(0.88)],
                        radius=c.r(0.08), fill=tint(SKY, 0.24),
                        outline=SKY, width=stroke)


def draw_move(d, c, stroke):
    d.rounded_rectangle([c.x(0.10), c.y(0.16), c.x(0.56), c.y(0.84)],
                        radius=c.r(0.08), fill=tint(AMBER),
                        outline=AMBER, width=stroke)
    d.line([(c.x(0.60), c.y(0.50)), (c.x(0.92), c.y(0.50))],
           fill=AMBER, width=stroke)
    d.line([(c.x(0.78), c.y(0.34)), (c.x(0.94), c.y(0.50)),
            (c.x(0.78), c.y(0.66))],
           fill=AMBER, width=stroke, joint="curve")


def draw_delete(d, c, stroke):
    # lid
    d.line([(c.x(0.14), c.y(0.26)), (c.x(0.86), c.y(0.26))],
           fill=RED, width=stroke)
    # handle
    d.line([(c.x(0.38), c.y(0.26)), (c.x(0.38), c.y(0.16)),
            (c.x(0.62), c.y(0.16)), (c.x(0.62), c.y(0.26))],
           fill=RED, width=stroke, joint="curve")
    # body
    d.line([(c.x(0.24), c.y(0.30)), (c.x(0.28), c.y(0.86)),
            (c.x(0.72), c.y(0.86)), (c.x(0.76), c.y(0.30))],
           fill=RED, width=stroke, joint="curve")
    # ribs
    for fx in (0.40, 0.50, 0.60):
        d.line([(c.x(fx), c.y(0.42)), (c.x(fx), c.y(0.72))],
               fill=RED, width=max(1, stroke - 1))


def draw_info(d, c, stroke):
    d.rounded_rectangle([c.x(0.14), c.y(0.10), c.x(0.86), c.y(0.90)],
                        radius=c.r(0.10), fill=tint(SLATE),
                        outline=SLATE, width=stroke)
    # bullet + text lines
    dot = c.r(0.045)
    d.ellipse([c.x(0.28) - dot, c.y(0.32) - dot,
               c.x(0.28) + dot, c.y(0.32) + dot], fill=SLATE)
    for fy, fx1 in ((0.32, 0.68), (0.52, 0.72), (0.70, 0.60)):
        d.line([(c.x(0.40), c.y(fy)), (c.x(fx1), c.y(fy))],
               fill=SLATE, width=max(1, stroke - 1))


PAINTERS = {
    "Add": draw_add,
    "Extract": draw_extract,
    "Test": draw_test,
    "Copy": draw_copy,
    "Move": draw_move,
    "Delete": draw_delete,
    "Info": draw_info,
}


def render(name, size):
    """Render one icon to an RGB image with a magenta colour key."""
    w, h = size
    big = (w * SS, h * SS)

    # Draw on transparent RGBA, then composite over magenta so the colour
    # key and the antialiased edge blend consistently.
    layer = Image.new("RGBA", big, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    c = Canvas(big)
    stroke = max(2, int(round(max(big) * 0.075)))
    PAINTERS[name](d, c, stroke)

    layer = layer.resize((w, h), Image.LANCZOS)

    out = Image.new("RGB", (w, h), KEY)
    # alpha-weighted composite, then hard-threshold so no purple fringe
    # survives on the transparent side of the mask edge.
    px = layer.load()
    opx = out.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a >= 128:
                # re-composite the remaining transparency onto magenta and
                # normalise so colours stay saturated
                f = a / 255.0
                opx[x, y] = (
                    int(r * f + KEY[0] * (1 - f)),
                    int(g * f + KEY[1] * (1 - f)),
                    int(b * f + KEY[2] * (1 - f)),
                )
    return out


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    outdir = os.path.join(here, "..", "overlay", "CPP", "7zip", "UI", "FileManager")
    outdir = os.path.normpath(outdir)
    os.makedirs(outdir, exist_ok=True)

    written = []
    for name in PAINTERS:
        for suffix, size in (("", (48, 36)), ("2", (24, 24))):
            im = render(name, size)
            path = os.path.join(outdir, f"{name}{suffix}.bmp")
            im.save(path, "BMP")
            written.append(path)
            print(f"  {os.path.basename(path):14s} {size[0]}x{size[1]}")

    # contact sheet for quick visual review
    if "--preview" in sys.argv:
        idx = sys.argv.index("--preview")
        target = sys.argv[idx + 1] if len(sys.argv) > idx + 1 else "icons-preview.png"
        cols = len(PAINTERS)
        sheet = Image.new("RGB", (cols * 72, 2 * 72), (248, 249, 251))
        for i, name in enumerate(PAINTERS):
            for row, size in enumerate(((48, 36), (24, 24))):
                im = render(name, size)
                bg = Image.new("RGB", im.size, (248, 249, 251))
                im = im.convert("RGB")
                px, bx = im.load(), bg.load()
                for y in range(im.size[1]):
                    for x in range(im.size[0]):
                        if px[x, y] != KEY:
                            bx[x, y] = px[x, y]
                bg = bg.resize((im.size[0] * 2, im.size[1] * 2), Image.NEAREST)
                sheet.paste(bg, (i * 72 + 8, row * 72 + 12))
        sheet.save(target)
        print(f"  preview -> {target}")

    print(f"\n{len(written)} icons written to {outdir}")


if __name__ == "__main__":
    main()
