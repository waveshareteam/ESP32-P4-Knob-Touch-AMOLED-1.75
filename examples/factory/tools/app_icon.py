#!/usr/bin/env python3
"""Draw launcher icons that match the bundled Brookesia app icons.

The bundled icons are 112 x 112 ARGB8888 rounded squares (corner radius about 30 px)
with a light-to-dark vertical gradient and a white glyph. This script draws new icons
in the same style and writes both the PNG and the LVGL C array.

    python3 tools/app_icon.py btmusic components/BtMusic/assets/img_app_btmusic
"""

import argparse
import math
import sys

from PIL import Image, ImageDraw, ImageFilter

SIZE = 112
RADIUS = 30
SCALE = 4  # Draw at 4x and downsample for smooth edges
S = SIZE * SCALE


def gradient_square(top, bottom):
    img = Image.new("RGBA", (S, S))
    draw = ImageDraw.Draw(img)
    for y in range(S):
        t = y / (S - 1)
        color = tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,)
        draw.line([(0, y), (S, y)], fill=color)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], RADIUS * SCALE, fill=255)
    img.putalpha(mask)
    return img


def add_glyph(base, glyph, shadow_color):
    """Composites a white glyph mask with a soft shadow, like the bundled icons."""
    shadow = Image.new("RGBA", (S, S), shadow_color + (0,))
    shadow.putalpha(glyph.point(lambda a: a * 90 // 255))
    shadow = shadow.filter(ImageFilter.GaussianBlur(3 * SCALE))
    offset = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    offset.paste(shadow, (0, 2 * SCALE))
    out = Image.alpha_composite(base, offset)
    # The bundled glyphs are lit from above: white at the top, slightly cool at the bottom
    white = Image.new("RGBA", (S, S))
    shade = ImageDraw.Draw(white)
    for y in range(S):
        t = y / (S - 1)
        shade.line([(0, y), (S, y)], fill=(round(255 - 30 * t), round(255 - 20 * t), 255, 255))
    white.putalpha(glyph)
    out = Image.alpha_composite(out, white)
    # Keep the rounded corners transparent after compositing
    out.putalpha(Image.composite(out.getchannel("A"), Image.new("L", (S, S), 0), base.getchannel("A")))
    return out


def glyph_btmusic():
    """Beamed eighth notes with a Bluetooth rune beside them."""
    g = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(g)
    u = SCALE

    def note_head(cx, cy):
        head = Image.new("L", (S, S), 0)
        ImageDraw.Draw(head).ellipse([cx - 12 * u, cy - 9 * u, cx + 12 * u, cy + 9 * u], fill=255)
        head = head.rotate(20, center=(cx, cy), resample=Image.BICUBIC)
        g.paste(255, (0, 0), head)

    # Notes, shifted left to leave room for the rune
    x1, x2 = 30 * u, 62 * u
    y_head1, y_head2 = 80 * u, 72 * u
    note_head(x1, y_head1)
    note_head(x2, y_head2)
    stem = 5 * u
    d.rectangle([x1 + 6 * u, 34 * u, x1 + 6 * u + stem, y_head1 - 2 * u], fill=255)
    d.rectangle([x2 + 6 * u, 26 * u, x2 + 6 * u + stem, y_head2 - 2 * u], fill=255)
    d.polygon([(x1 + 6 * u, 34 * u), (x2 + 6 * u + stem, 26 * u),
               (x2 + 6 * u + stem, 38 * u), (x1 + 6 * u, 46 * u)], fill=255)

    # Bluetooth rune on the right
    cx, top, bottom = 89 * u, 30 * u, 82 * u
    w = 5 * u
    q = (bottom - top) // 4
    pts = [(cx - 12 * u, top + q), (cx + 10 * u, bottom - q), (cx, bottom), (cx, top),
           (cx + 10 * u, top + q), (cx - 12 * u, bottom - q)]
    d.line(pts, fill=255, width=w, joint="curve")
    return g


def glyph_pomodoro():
    """A tomato with clock hands cut out of it."""
    g = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(g)
    u = SCALE
    cx, cy = 56 * u, 64 * u
    d.ellipse([cx - 34 * u, cy - 30 * u, cx + 34 * u, cy + 32 * u], fill=255)

    # Calyx: a five-point star on top, separated from the body by a thin gap
    top = 36 * u
    star = []
    for k in range(10):
        r = (17 if k % 2 == 0 else 7) * u
        a = -math.pi / 2 + k * math.pi / 5
        star.append((cx + r * math.cos(a), top + r * math.sin(a) * 0.7))
    gap = Image.new("L", (S, S), 0)
    ImageDraw.Draw(gap).polygon(star, fill=255)
    gap = gap.filter(ImageFilter.MaxFilter(3 * u + 1))
    g.paste(0, (0, 0), gap)
    d.polygon(star, fill=255)
    d.rounded_rectangle([cx - 2 * u, top - 15 * u, cx + 2 * u, top - 2 * u], 2 * u, fill=255)

    # Clock hands and hub cut out of the body
    w = 5 * u
    hub_y = cy + 6 * u
    d.line([(cx, hub_y), (cx, hub_y - 18 * u)], fill=0, width=w)
    d.line([(cx, hub_y), (cx + 15 * u, hub_y)], fill=0, width=w)
    d.ellipse([cx - 5 * u, hub_y - 5 * u, cx + 5 * u, hub_y + 5 * u], fill=0)
    return g


def glyph_gallery():
    """A framed landscape: sun over two mountains."""
    g = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(g)
    u = SCALE
    left, top, right, bottom = 20 * u, 28 * u, 92 * u, 84 * u
    d.rounded_rectangle([left, top, right, bottom], 10 * u, outline=255, width=6 * u)

    # Landscape clipped to the inside of the frame
    scene = Image.new("L", (S, S), 0)
    s = ImageDraw.Draw(scene)
    s.ellipse([66 * u, 40 * u, 78 * u, 52 * u], fill=255)
    s.polygon([(left, bottom), (44 * u, 56 * u), (60 * u, 72 * u), (70 * u, 62 * u), (right, bottom)], fill=255)
    inside = Image.new("L", (S, S), 0)
    ImageDraw.Draw(inside).rounded_rectangle([left + 9 * u, top + 9 * u, right - 9 * u, bottom - 9 * u], 4 * u,
                                             fill=255)
    g.paste(255, (0, 0), Image.composite(scene, Image.new("L", (S, S), 0), inside))
    return g


def glyph_reader():
    """An open book with lines of text on both pages."""
    g = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(g)
    u = SCALE
    cx = 56 * u
    top, bottom = 32 * u, 80 * u
    for side in (-1, 1):
        inner, outer = cx + side * 3 * u, cx + side * 40 * u
        # Each page rises a little towards the spine, like a book lying open
        d.polygon([(inner, top + 4 * u), (cx + side * 20 * u, top - 2 * u), (outer, top), (outer, bottom),
                   (cx + side * 20 * u, bottom - 2 * u), (inner, bottom + 4 * u)], fill=255)
        for i in range(4):
            y = top + (10 + i * 9) * u
            x0, x1 = sorted((cx + side * 10 * u, cx + side * 33 * u))
            d.line([(x0, y), (x1, y)], fill=0, width=3 * u)
    # The cover showing below the pages
    d.line([(14 * u, 84 * u), (cx, 91 * u), (98 * u, 84 * u)], fill=255, width=5 * u, joint="curve")
    return g


def glyph_motion():
    """A bubble level seen from above: a ring, a crosshair and the bubble just off centre."""
    g = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(g)
    u = SCALE
    cx = cy = 56 * u
    d.ellipse([cx - 38 * u, cy - 38 * u, cx + 38 * u, cy + 38 * u], outline=255, width=6 * u)
    d.ellipse([cx - 14 * u, cy - 14 * u, cx + 14 * u, cy + 14 * u], outline=255, width=3 * u)
    # Crosshair ticks between the two rings
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        d.line([(cx + dx * 20 * u, cy + dy * 20 * u), (cx + dx * 31 * u, cy + dy * 31 * u)], fill=255, width=3 * u)
    # The bubble, nearly level
    bx, by = cx + 8 * u, cy - 6 * u
    d.ellipse([bx - 11 * u, by - 11 * u, bx + 11 * u, by + 11 * u], fill=255)
    return g


def glyph_usbscreen():
    """A monitor on its stand with the USB trident on the screen."""
    g = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(g)
    u = SCALE
    d.rounded_rectangle([16 * u, 22 * u, 96 * u, 76 * u], 9 * u, outline=255, width=6 * u)
    d.polygon([(48 * u, 76 * u), (64 * u, 76 * u), (68 * u, 88 * u), (44 * u, 88 * u)], fill=255)
    d.rounded_rectangle([34 * u, 86 * u, 78 * u, 92 * u], 3 * u, fill=255)

    # The trident: a stem with an arrow head, a round and a square end on the two branches
    cx, w = 56 * u, 4 * u
    d.line([(cx, 36 * u), (cx, 66 * u)], fill=255, width=w)
    d.polygon([(cx, 30 * u), (cx - 6 * u, 39 * u), (cx + 6 * u, 39 * u)], fill=255)
    d.ellipse([cx - 5 * u, 61 * u, cx + 5 * u, 71 * u], fill=255)
    d.line([(cx, 58 * u), (cx - 12 * u, 50 * u), (cx - 12 * u, 46 * u)], fill=255, width=w, joint="curve")
    d.ellipse([cx - 16 * u, 39 * u, cx - 8 * u, 47 * u], fill=255)
    d.line([(cx, 54 * u), (cx + 12 * u, 46 * u), (cx + 12 * u, 43 * u)], fill=255, width=w, joint="curve")
    d.rectangle([cx + 8 * u, 36 * u, cx + 16 * u, 44 * u], fill=255)
    return g


def glyph_xiaozhi():
    """A chat bubble with the assistant's face: two eyes and a smile."""
    g = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(g)
    u = SCALE
    d.rounded_rectangle([18 * u, 22 * u, 94 * u, 80 * u], 22 * u, fill=255)
    d.polygon([(30 * u, 72 * u), (52 * u, 78 * u), (26 * u, 94 * u)], fill=255)
    # Cut the face out of the bubble
    d.rounded_rectangle([38 * u, 38 * u, 46 * u, 54 * u], 4 * u, fill=0)
    d.rounded_rectangle([66 * u, 38 * u, 74 * u, 54 * u], 4 * u, fill=0)
    d.arc([44 * u, 48 * u, 68 * u, 68 * u], 30, 150, fill=0, width=4 * u)
    return g


GLYPHS = {
    "btmusic": (glyph_btmusic, (64, 196, 255), (24, 86, 232), (10, 40, 120)),
    "gallery": (glyph_gallery, (196, 150, 255), (118, 76, 232), (50, 20, 120)),
    "pomodoro": (glyph_pomodoro, (118, 214, 120), (26, 150, 84), (10, 70, 40)),
    "reader": (glyph_reader, (255, 202, 112), (236, 126, 38), (120, 56, 10)),
    "xiaozhi": (glyph_xiaozhi, (88, 236, 214), (32, 150, 220), (8, 60, 110)),
    "motion": (glyph_motion, (255, 128, 176), (214, 52, 120), (110, 16, 56)),
    "usbscreen": (glyph_usbscreen, (96, 228, 214), (16, 146, 160), (6, 66, 74)),
}


def to_lvgl_c(img, name):
    """LVGL 9 ARGB8888 image source: bytes are B, G, R, A per pixel."""
    rgba = img.tobytes()
    data = []
    for i in range(0, len(rgba), 4):
        r, g, b, a = rgba[i:i + 4]
        data += [b, g, r, a]
    lines = []
    row = SIZE * 4
    for i in range(0, len(data), row):
        lines.append("  " + ", ".join("0x%02x" % v for v in data[i:i + row]) + ",")
    upper = name.upper()
    return f"""#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif


#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMAGE_{upper}
#define LV_ATTRIBUTE_IMAGE_{upper}
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMAGE_{upper} uint8_t {name}_map[] = {{
{chr(10).join(lines)}
}};

const lv_image_dsc_t {name} = {{
  .header.cf = LV_COLOR_FORMAT_ARGB8888,
  .header.magic = LV_IMAGE_HEADER_MAGIC,
  .header.w = {SIZE},
  .header.h = {SIZE},
  .data_size = {SIZE * SIZE} * 4,
  .data = {name}_map,
}};
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("glyph", choices=sorted(GLYPHS))
    parser.add_argument("output", help="Output path without extension; the basename is the C symbol")
    args = parser.parse_args()

    draw_glyph, top, bottom, shadow = GLYPHS[args.glyph]
    icon = add_glyph(gradient_square(top, bottom), draw_glyph(), shadow)
    icon = icon.resize((SIZE, SIZE), Image.LANCZOS)
    name = args.output.rstrip("/").split("/")[-1]
    icon.save(args.output + ".png")
    with open(args.output + ".c", "w") as f:
        f.write(to_lvgl_c(icon, name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
