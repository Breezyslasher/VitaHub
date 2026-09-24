#!/usr/bin/env python3
"""Generate VitaHub's PS Vita LiveArea art from the four service icons.

Writes app/platform/psv/sce_sys/{icon0,pic0}.png and
livearea/contents/{bg,startup}.png. The Vita only accepts 8-bit palette
PNGs there, so every image is quantised before saving.

Needs Pillow:  pip install pillow
"""

import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICONS = [os.path.join(ROOT, "resources", "hub", n + ".png") for n in ("plex", "abs", "suwayomi", "music")]
FONT = os.path.join(ROOT, "resources", "font", "DejaVuSans.ttf")
OUT = os.path.join(ROOT, "app", "platform", "psv", "sce_sys")

BG = (45, 45, 45)
PANEL = (52, 52, 62)
GOLD = (229, 160, 13)
TEXT = (255, 255, 255)
MUTED = (163, 163, 163)


def rounded(img, radius):
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, img.size[0] - 1, img.size[1] - 1], radius, fill=255)
    out = Image.new("RGBA", img.size)
    out.paste(img.convert("RGBA"), (0, 0), mask)
    return out


def icon_grid(size, gap, radius):
    """2x2 grid of the service icons, `size` px square overall."""
    cell = (size - gap) // 2
    grid = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    for i, path in enumerate(ICONS):
        icon = rounded(Image.open(path).convert("RGBA").resize((cell, cell), Image.LANCZOS), radius)
        grid.alpha_composite(icon, ((i % 2) * (cell + gap), (i // 2) * (cell + gap)))
    return grid


def save_indexed(img, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.convert("RGB").quantize(colors=256, method=Image.MEDIANCUT, dither=Image.NONE).save(path, optimize=True)
    print("wrote", os.path.relpath(path, ROOT))


def banner(width, height, grid_size, title_size, subtitle):
    img = Image.new("RGBA", (width, height), BG + (255,))
    draw = ImageDraw.Draw(img)
    grid = icon_grid(grid_size, max(4, grid_size // 20), max(6, grid_size // 12))
    title_font = ImageFont.truetype(FONT, title_size)
    sub_font = ImageFont.truetype(FONT, max(12, title_size * 2 // 5))
    title_w = draw.textlength("VitaHub", font=title_font)
    block_w = grid_size + title_size + max(title_w, draw.textlength(subtitle, font=sub_font))
    x = int((width - block_w) / 2)
    y = (height - grid_size) // 2
    img.alpha_composite(grid, (x, y))
    tx = x + grid_size + title_size
    draw.text((tx, y + grid_size * 0.18), "VitaHub", font=title_font, fill=TEXT)
    draw.text((tx, y + grid_size * 0.18 + title_size * 1.25), subtitle, font=sub_font, fill=MUTED)
    draw.rectangle([tx, y + grid_size * 0.18 + title_size * 1.25 + title_size * 0.75,
                    tx + title_size * 1.5, y + grid_size * 0.18 + title_size * 1.25 + title_size * 0.75 + 4],
                   fill=GOLD)
    return img


def main():
    # icon0: the grid on a dark rounded tile.
    icon = Image.new("RGBA", (128, 128), BG + (255,))
    icon.alpha_composite(icon_grid(112, 6, 12), (8, 8))
    save_indexed(icon, os.path.join(OUT, "icon0.png"))

    save_indexed(banner(960, 544, 220, 64, "Movies · Audiobooks · Manga · Music"),
                 os.path.join(OUT, "pic0.png"))
    save_indexed(banner(840, 500, 200, 60, "Movies, books, manga and music"),
                 os.path.join(OUT, "livearea", "contents", "bg.png"))

    startup = Image.new("RGBA", (280, 158), PANEL + (255,))
    startup.alpha_composite(icon_grid(110, 6, 10), (20, 24))
    d = ImageDraw.Draw(startup)
    d.text((142, 58), "Start", font=ImageFont.truetype(FONT, 30), fill=TEXT)
    save_indexed(startup, os.path.join(OUT, "livearea", "contents", "startup.png"))


if __name__ == "__main__":
    main()
