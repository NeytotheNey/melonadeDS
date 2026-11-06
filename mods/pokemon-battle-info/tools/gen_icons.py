"""Generates the mod's icon sheet: img/icons.png and pbi/data_icons.lua, which says where each icon is in it. Run once; both are kept in the repo.

  py -I -X utf8 gen_icons.py [--out <mods/pokemon-battle-info>]

One image pixel is one overlay pixel (2 per DS pixel), so the mod draws these with scale = 0.5. Needs Pillow.
"""
import argparse
import os
from PIL import Image

TYPES = ["NOR", "FIR", "WAT", "ELE", "GRA", "ICE", "FIG", "POI", "GRO", "FLY", "PSY", "BUG", "ROC", "GHO", "DRA", "DAR", "STE"]
STATUSES = ["PAR", "SLP", "FRZ", "BRN", "PSN", "TOX", "CNF"]
MARKERS = ["x4", "x2", "x.5", "x.25", "x0"]

TYPE_COLOR = {
    "NOR": 0xFF9A9A82, "FIR": 0xFFE2622C, "WAT": 0xFF3F7FE0, "ELE": 0xFFF0C81E, "GRA": 0xFF5AAE46, "ICE": 0xFF7CD0D0,
    "FIG": 0xFFB03428, "POI": 0xFF9A44A0, "GRO": 0xFFCFA850, "FLY": 0xFF8F7EE6, "PSY": 0xFFEE5C86, "BUG": 0xFF9AAE22,
    "ROC": 0xFFA88F38, "GHO": 0xFF6A5490, "DRA": 0xFF6838F0, "DAR": 0xFF6A5040, "STE": 0xFFA8A8C2,
}
# Which glyph and color each status uses. Paralysis, freeze, burn, poison and confusion borrow a type's glyph.
STATUS_ICON = {
    "PAR": ("ELE", 0xFFF0C81E), "SLP": ("SLP", 0xFF8A8A9A), "FRZ": ("ICE", 0xFF7CD0D0), "BRN": ("FIR", 0xFFE2622C),
    "PSN": ("POI", 0xFF9A44A0), "TOX": ("TOX", 0xFF6E1E86), "CNF": ("PSY", 0xFFEE5C86),
}
HOT, WARM, COOL, OFF = 0xFFFF7A1A, 0xFFFFB347, 0xFF62B6FF, 0xFF9AA2AC
DARK = (16, 20, 24, 255)
ICON = 18  # overlay pixels, so 9 DS pixels
ARROW_W = 12
COLUMNS = 10

GLYPHS = {
    "NOR": """
...######...
..########..
.###....###.
###......###
##........##
##........##
##........##
##........##
###......###
.###....###.
..########..
...######...""",
    "FIR": """
.....##.....
.....###....
....####....
...#####.#..
...#######..
..#########.
..####.####.
..###...###.
..###...###.
...##...##..
....##.##...
.....###....""",
    "WAT": """
.....##.....
....####....
....####....
...######...
..########..
.##########.
.#.########.
.#.########.
.##..######.
..##.#####..
...######...
....####....""",
    "ELE": """
.......###..
......###...
.....###....
....###.....
...#######..
..#######...
.....###....
....###.....
...###......
..###.......
..##........
..#.........""",
    "GRA": """
..........##
.......#####
.....######.
....#######.
...###.####.
..###.####..
..##.####...
..#.####....
...####.....
..#.##......
.#..........
#...........""",
    "ICE": """
.....##.....
..#..##..#..
...#.##.#...
....####....
.#...##...#.
############
############
.#...##...#.
....####....
...#.##.#...
..#..##..#..
.....##.....""",
    "FIG": """
.#.##.##.##.
.##.##.##.#.
.##.##.##.##
.##.##.##.##
.###########
.###########
.#....######
.##########.
.#########..
..########..
..########..
...######...""",
    "POI": """
.......###..
......#####.
......#####.
.##....###..
####........
####...##...
.##...####..
......####..
..###..##...
.#####......
.#####......
..###.......""",
    "GRO": """
.....##.....
....####....
...######...
..########..
............
.##########.
############
............
.###.####.#.
############
.##.####.##.
............""",
    "FLY": """
##..........
####........
#####.......
.#####......
.######.....
..#######...
..#########.
...#########
...#####....
....#####...
......####..
........##..""",
    "PSY": """
...######...
..#......#..
.#..####..#.
.#.#....#.#.
.#.#.##.#.#.
.#.#.#..#.#.
.#.#.####.#.
.#.#......#.
.#..######..
..#.........
...####.....
............""",
    "BUG": """
..#......#..
...#....#...
....####....
...######...
##.##..##.##
..###..###..
.####..####.
#.###..###.#
..###..###..
.#.##..##.#.
#..######..#
....####....""",
    "ROC": """
....#####...
...#######..
..#########.
.###.#######
.##.########
###.###.####
#######.####
######.#####
###########.
.##########.
...#######..
............""",
    "GHO": """
....####....
..########..
.##########.
.##..##..##.
.##..##..##.
.##########.
.##########.
.##########.
.##########.
.##########.
.##.##.##.#.
.#...#...#..""",
    "DRA": """
#..........#
##........##
###......###
.###....###.
.####..####.
..########..
..########..
...######...
...######...
....####....
....####....
.....##.....""",
    "DAR": """
.....####...
...####.....
..####......
.####.......
.####.......
.####.......
.####.......
.#####......
..#####....#
..#########.
....######..
............""",
    "STE": """
.....##.....
..#..##..#..
.##########.
..###..###..
..##....##..
####....####
####....####
..##....##..
..###..###..
.##########.
..#..##..#..
.....##.....""",
    "SLP": """
............
.#########..
.#########..
.......##...
......##....
.....##.....
....##......
...##.......
..##........
.#########..
.#########..
............""",
    "TOX": """
...######...
..########..
.##########.
.##..##..##.
.##..##..##.
.##########.
..###..###..
...######...
...#.##.#...
...#.##.#...
............
............""",
}


def rgba(c):
    return ((c >> 16) & 255, (c >> 8) & 255, c & 255, (c >> 24) & 255)


def luminance(c):
    r, g, b, _ = rgba(c)
    return .299 * r + .587 * g + .114 * b


def make_icon(glyph, color, round_shape=False):
    """A badge with a dark edge and a glyph: rounded squares for types, circles for status"""
    im = Image.new("RGBA", (ICON, ICON), (0, 0, 0, 0))
    px = im.load()
    fill = rgba(color)
    ink = DARK if luminance(color) > 150 else (255, 255, 255, 255)
    for y in range(ICON):
        for x in range(ICON):
            dx, dy = abs(x - 8.5), abs(y - 8.5)
            if round_shape:
                d = dx * dx + dy * dy
                inside, edge = d <= 8.6 ** 2, d <= 9.5 ** 2
            else:
                # Rounded square: corners cut by 2 pixels
                inside = dx <= 7.5 and dy <= 7.5 and dx + dy <= 13
                edge = dx <= 8.5 and dy <= 8.5 and dx + dy <= 15
            if inside:
                px[x, y] = fill
            elif edge:
                px[x, y] = DARK
    for gy, row in enumerate(GLYPHS[glyph].strip("\n").split("\n")):
        for gx, ch in enumerate(row):
            if ch == "#":
                px[3 + gx, 3 + gy] = ink
    return im


def arrow(color, up=True, double=False):
    """One or two triangles with a dark edge"""
    im = Image.new("RGBA", (ARROW_W, ICON), (0, 0, 0, 0))
    px = im.load()
    c = rgba(color)
    shapes = [(1, 8)] if not double else [(0, 6), (6, 6)]
    for top, h in shapes:
        for i in range(h):
            y = top + 3 + i if up else (ICON - 1 - (top + 3 + i))
            for x in range(6 - i - 1, 6 + i + 1):
                if 0 <= x < ARROW_W:
                    px[x, y] = c
    base = im.copy()
    bp = base.load()
    for y in range(ICON):
        for x in range(ARROW_W):
            if bp[x, y][3] == 0:
                for ox, oy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + ox, y + oy
                    if 0 <= nx < ARROW_W and 0 <= ny < ICON and bp[nx, ny][3] and bp[nx, ny] != DARK:
                        px[x, y] = DARK
                        break
    return im


def no_sign():
    """A ring with a bar across it"""
    im = Image.new("RGBA", (ICON, ICON), (0, 0, 0, 0))
    px = im.load()
    c = rgba(OFF)
    for y in range(ICON):
        for x in range(ICON):
            d = ((x - 8.5) ** 2 + (y - 8.5) ** 2) ** .5
            onring = 5.2 <= d <= 7.6
            onbar = abs((x - 8.5) + (y - 8.5)) <= 1.2 and d <= 6
            if onring or onbar:
                px[x, y] = c
            elif 4.2 <= d <= 8.8 or (abs((x - 8.5) + (y - 8.5)) <= 2.4 and d <= 6):
                px[x, y] = DARK
    return im


def outlined(layer):
    """A 1 px dark edge around whatever is drawn"""
    out = layer.copy()
    src, px = layer.load(), out.load()
    for y in range(ICON):
        for x in range(ICON):
            if src[x, y][3]:
                continue
            for ox, oy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + ox, y + oy
                if 0 <= nx < ICON and 0 <= ny < ICON and src[nx, ny][3]:
                    px[x, y] = DARK
                    break
    return out


def sword():
    """Attack's stat changes: a sword pointing up and to the right, no badge"""
    im = Image.new("RGBA", (ICON, ICON), (0, 0, 0, 0))
    px = im.load()
    silver, shine, gold, grip = (214, 222, 232, 255), (255, 255, 255, 255), (240, 196, 70, 255), (150, 96, 52, 255)
    for i in range(11):  # blade, tip top right, 3 px across
        x, y = 16 - i, 1 + i
        px[x, y] = shine
        if i > 0:
            px[x, y - 1] = silver
            px[x - 1, y] = silver
            px[x + 1, y] = silver
            px[x, y + 1] = silver
    for i in range(8):  # guard, across the blade
        for w in (0, 1, 2):
            px[2 + i + w, 7 + i] = gold
    for i in range(3):  # grip
        for w in (0, 1, 2):
            px[3 - i + w, 12 + i] = grip
    for w in (0, 1, 2):
        px[1 + w, 15] = gold
    return outlined(im)


def shield():
    """Defense's stat changes: a shield, no badge"""
    im = Image.new("RGBA", (ICON, ICON), (0, 0, 0, 0))
    px = im.load()
    light, deep, rim = (120, 170, 235, 255), (62, 110, 190, 255), (214, 222, 232, 255)
    half = {1: 7, 2: 7, 3: 7, 4: 7, 5: 7, 6: 7, 7: 7, 8: 7, 9: 6, 10: 6, 11: 5, 12: 4, 13: 3, 14: 2, 15: 1, 16: 0}
    for y, h in half.items():
        for x in range(8 - h, 10 + h):
            on_rim = y in (1, 2) or x <= 8 - h or x >= 9 + h
            px[x, y] = rim if on_rim else (light if x < 9 else deep)
    return outlined(im)


def build_icons():
    icons = [(t, make_icon(t, TYPE_COLOR[t])) for t in TYPES]
    icons += [(s, make_icon(STATUS_ICON[s][0], STATUS_ICON[s][1], True)) for s in STATUSES]
    icons += [
        ("x4", arrow(HOT, double=True)),
        ("x2", arrow(WARM)),
        ("x.5", arrow(COOL, up=False)),
        ("x.25", arrow(COOL, up=False, double=True)),
        ("x0", no_sign()),
        ("ATK", sword()),
        ("DEF", shield()),
    ]
    return icons


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    p = argparse.ArgumentParser()
    p.add_argument("--out", default=os.path.dirname(here))
    args = p.parse_args()

    icons = build_icons()
    rows = (len(icons) + COLUMNS - 1) // COLUMNS
    sheet = Image.new("RGBA", (COLUMNS * ICON, rows * ICON), (0, 0, 0, 0))
    lines = []
    for i, (name, icon) in enumerate(icons):
        x, y = (i % COLUMNS) * ICON, (i // COLUMNS) * ICON
        sheet.paste(icon, (x, y))
        lines.append('  ["%s"] = { %d, %d, %d, %d },' % (name, x, y, icon.width, icon.height))
    os.makedirs(os.path.join(args.out, "img"), exist_ok=True)
    sheet.save(os.path.join(args.out, "img", "icons.png"))
    with open(os.path.join(args.out, "pbi", "data_icons.lua"), "w", encoding="ascii", newline="\n") as f:
        f.write("-- Generated by tools/gen_icons.py. Don't edit by hand.\n")
        f.write("-- Where each icon is in img/icons.png: x, y, width, height in image pixels (2 per DS pixel).\n")
        f.write("return {\n" + "\n".join(lines) + "\n}\n")
    print(len(icons), "icons,", sheet.size)


if __name__ == "__main__":
    main()
