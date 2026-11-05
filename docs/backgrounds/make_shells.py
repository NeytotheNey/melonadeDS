"""Generate handheld-shell SVG backgrounds for melonadeDS.

Each SVG has no viewBox and a 100% x 100% root, so the app renders it in real
pixels at whatever size the display is. Positions are percentages (they follow
the screen's shape) and detail sizes are pixels (they stay round and crisp), so
the same file works portrait, landscape, folded and unfolded.

Only features AndroidSVG 1.4 supports are used: gradients, patterns, <use>,
strokes, dashes and opacity. No filters or CSS.

Run: python docs/backgrounds/make_shells.py
"""

import os
import random

# Written straight into the app's assets: these ship as built-in backgrounds.
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, "app", "src", "main", "assets", "backgrounds")

# The molded edge: a groove `GROOVE_IN` px in from the screen edge.
# Ring corners stay hidden as long as radius <= 2.41 * inset.
GROOVE_IN = 30
GROOVE_DARK = 3
GROOVE_LIGHT = 2
GROOVE_RADIUS = 70
PIN_IN = 58
PIN_W = 3
PIN_RADIUS = 135


def ring(stroke, width, radius, opacity=None):
    op = f' stroke-opacity="{opacity}"' if opacity is not None else ""
    return (f'<rect width="100%" height="100%" rx="{radius}" fill="none" '
            f'stroke="{stroke}" stroke-width="{width}"{op}/>')


def base_gradient(c):
    light, mid, dark = c
    return (f'<linearGradient id="base" x1="0" y1="0" x2="0.35" y2="1">'
            f'<stop offset="0" stop-color="{light}"/>'
            f'<stop offset="0.45" stop-color="{mid}"/>'
            f'<stop offset="1" stop-color="{dark}"/></linearGradient>')


def gloss_defs(highlight, sheen):
    return (f'<radialGradient id="hl" cx="0.22" cy="0.1" r="0.8">'
            f'<stop offset="0" stop-color="#fff" stop-opacity="{highlight}"/>'
            f'<stop offset="0.55" stop-color="#fff" stop-opacity="{highlight * 0.25:.3f}"/>'
            f'<stop offset="1" stop-color="#fff" stop-opacity="0"/></radialGradient>'
            f'<linearGradient id="sheen" x1="0" y1="0" x2="1" y2="1">'
            f'<stop offset="0.28" stop-color="#fff" stop-opacity="0"/>'
            f'<stop offset="0.36" stop-color="#fff" stop-opacity="{sheen}"/>'
            f'<stop offset="0.40" stop-color="#fff" stop-opacity="{sheen * 0.4:.3f}"/>'
            f'<stop offset="0.47" stop-color="#fff" stop-opacity="0"/></linearGradient>')


def vignette_def(strength):
    return (f'<radialGradient id="vig" cx="0.5" cy="0.45" r="0.78">'
            f'<stop offset="0.55" stop-color="#000" stop-opacity="0"/>'
            f'<stop offset="1" stop-color="#000" stop-opacity="{strength}"/></radialGradient>')


def pcb_pattern(board, trace):
    t = (f'fill="none" stroke="{trace}" stroke-width="5" '
         f'stroke-linecap="round" stroke-linejoin="round"')
    v = f'fill="{board}" stroke="{trace}" stroke-width="4"'
    return (
        f'<pattern id="pcb" patternUnits="userSpaceOnUse" width="360" height="360">'
        f'<rect width="360" height="360" fill="{board}"/>'
        f'<path {t} d="M0 40 H100 L130 70 H220 L250 40 H360"/>'
        f'<path {t} d="M0 200 H60 L90 230 H360"/>'
        f'<path {t} d="M0 215 H30"/>'
        f'<path {t} d="M300 0 V120 L270 150 V250 L300 280 V360"/>'
        f'<path {t} d="M150 0 V20"/><path {t} d="M150 340 V360"/>'
        f'<path {t} d="M40 120 H90 L120 150 V180"/>'
        f'<path {t} d="M170 290 H230 L250 270 V250"/>'
        f'<path {t} d="M180 110 V160 L200 180 H240"/>'
        f'<circle cx="120" cy="180" r="7" {v}/>'
        f'<circle cx="250" cy="250" r="7" {v}/>'
        f'<circle cx="240" cy="180" r="7" {v}/>'
        f'<circle cx="40" cy="120" r="7" {v}/>'
        f'<circle cx="170" cy="290" r="7" {v}/>'
        f'<rect x="60" y="290" width="16" height="26" rx="2" fill="{trace}"/>'
        f'<rect x="90" y="290" width="16" height="26" rx="2" fill="{trace}"/>'
        f'<rect x="200" y="90" width="26" height="14" rx="2" fill="{trace}"/>'
        f'</pattern>')


def chips(color, pins):
    out = []
    for x, y, w, h in [("34%", "30%", 260, 260), ("12%", "62%", 180, 120),
                       ("62%", "70%", 140, 140), ("70%", "14%", 200, 90)]:
        out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="none" '
                   f'stroke="{pins}" stroke-width="22" stroke-dasharray="6 12"/>')
        out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="{color}"/>')
    return "".join(out)


def brushed_pattern(seed):
    rnd = random.Random(seed)
    lines = []
    for _ in range(26):
        y = rnd.uniform(0, 40)
        x = rnd.uniform(-200, 600)
        length = rnd.uniform(300, 900)
        light = rnd.random() < 0.5
        color = "#fff" if light else "#000"
        op = rnd.uniform(0.03, 0.09)
        w = rnd.choice([1, 1, 1.5, 2])
        for dx in (0, -800, 800):  # wrap horizontally so the tile seams don't show
            lines.append(f'<line x1="{x + dx:.0f}" y1="{y:.1f}" x2="{x + dx + length:.0f}" '
                         f'y2="{y:.1f}" stroke="{color}" stroke-opacity="{op:.3f}" stroke-width="{w}"/>')
    return (f'<pattern id="brush" patternUnits="userSpaceOnUse" width="800" height="40">'
            + "".join(lines) + '</pattern>')


def sparkles(seed, count, colors):
    rnd = random.Random(seed)
    out = []
    for _ in range(count):
        c = rnd.choice(colors)
        out.append(f'<circle cx="{rnd.uniform(0, 100):.2f}%" cy="{rnd.uniform(0, 100):.2f}%" '
                   f'r="{rnd.uniform(0.8, 2.4):.1f}" fill="{c}" fill-opacity="{rnd.uniform(0.25, 0.85):.2f}"/>')
    return "".join(out)


def slot_grille(dark, light):
    # Diagonal speaker slots anchored to the bottom-right corner, kept clear
    # of the edge groove (which sits 30-35 px in).
    g = []
    for i in range(6):
        x = -330 + i * 34
        g.append(f'<line x1="{x}" y1="-100" x2="{x + 70}" y2="-210" stroke="{light}" '
                 f'stroke-width="13" stroke-linecap="round" transform="translate(0 3)"/>')
        g.append(f'<line x1="{x}" y1="-100" x2="{x + 70}" y2="-210" stroke="{dark}" '
                 f'stroke-width="13" stroke-linecap="round"/>')
    return ('<g id="grille">' + "".join(g) + '</g>')


def hole_grille(dark, light):
    g = []
    for row in range(5):
        for col in range(5):
            if (row in (0, 4) and col in (0, 4)):
                continue
            cx = -260 + col * 30
            cy = -250 + row * 30
            g.append(f'<circle cx="{cx}" cy="{cy + 2}" r="8" fill="{light}"/>')
            g.append(f'<circle cx="{cx}" cy="{cy}" r="8" fill="{dark}"/>')
    return '<g id="grille">' + "".join(g) + '</g>'


# --- Gunpla-style panel lines -------------------------------------------------
# Panel shapes are in per-mille (0..1000 on both axes). Fills go in a nested
# <svg> stretched to the screen; edges are drawn as <line>s with % endpoints so
# the engraving keeps a constant pixel width on any screen shape. Bolts, slots
# and decals are fixed pixel size at % positions.

TECH_PANELS = [
    # (points, tone) — tone: "dark", "cool" or "accent"
    ([(0, 170), (85, 170), (125, 215), (125, 540), (85, 585), (0, 585)], "dark"),
    ([(280, 0), (315, 55), (470, 55), (490, 35), (510, 35), (530, 55), (685, 55), (720, 0)], "cool"),
    ([(1000, 90), (890, 90), (850, 135), (850, 330), (905, 385), (1000, 385)], "dark"),
    ([(0, 780), (140, 780), (200, 840), (200, 1000), (0, 1000)], "cool"),
    ([(1000, 640), (930, 640), (880, 690), (880, 860), (820, 920), (820, 1000), (1000, 1000)], "dark"),
    ([(0, 600), (70, 600), (95, 625), (0, 625)], "accent"),
    ([(1000, 400), (925, 400), (950, 425), (1000, 425)], "accent"),
    ([(560, 1000), (575, 965), (640, 965), (655, 1000)], "accent"),
]

TECH_LINES = [
    [(500, 55), (500, 140), (520, 160), (520, 230)],
    [(0, 380), (125, 380)],
    [(850, 230), (1000, 230)],
    [(330, 1000), (360, 950), (540, 950)],
    [(675, 950), (700, 950), (730, 1000)],
    [(200, 900), (260, 900), (290, 930), (290, 1000)],
    [(880, 760), (1000, 760)],
    [(125, 300), (165, 300), (180, 315), (180, 440)],
    [(850, 180), (815, 180), (800, 195), (800, 280)],
    # Large armor-plate divisions across the face.
    [(315, 55), (250, 120), (250, 260), (180, 315)],
    [(685, 55), (750, 120), (750, 195), (800, 195)],
    [(180, 440), (260, 520), (300, 520), (300, 700), (260, 740), (0, 740)],
    [(300, 620), (420, 620), (440, 645), (560, 645), (580, 620), (700, 620)],
    [(700, 620), (700, 480), (740, 440), (1000, 440)],
    [(700, 620), (760, 690), (880, 690)],
    [(300, 700), (360, 760), (360, 950)],
    [(820, 920), (760, 920), (730, 950), (700, 950)],
    [(440, 645), (440, 700)],
    [(560, 645), (560, 700)],
]


def _on_border(a, b):
    return (a[0] == b[0] and a[0] in (0, 1000)) or (a[1] == b[1] and a[1] in (0, 1000))


def _engraved(a, b, dark, light):
    x1, y1, x2, y2 = a[0] / 10, a[1] / 10, b[0] / 10, b[1] / 10
    coords = f'x1="{x1}%" y1="{y1}%" x2="{x2}%" y2="{y2}%"'
    return (f'<line {coords} stroke="{light}" stroke-width="1.6" stroke-linecap="round" transform="translate(1.6 1.6)"/>'
            f'<line {coords} stroke="{dark}" stroke-width="2.4" stroke-linecap="round"/>')


def tech_layer(accent, accent_line, ink="#55585f", lip="#ffffff"):
    defs, body = [], []

    tones = {"dark": 'fill="#000" fill-opacity="0.045"',
             "cool": 'fill="#9fb1bd" fill-opacity="0.16"',
             "accent": f'fill="{accent}"'}
    polys = "".join(
        f'<polygon points="{" ".join(f"{x},{y}" for x, y in pts)}" {tones[tone]}/>'
        for pts, tone in TECH_PANELS)
    body.append('<svg x="0" y="0" width="100%" height="100%" viewBox="0 0 1000 1000" '
                f'preserveAspectRatio="none">{polys}</svg>')

    lines = []
    for pts, _ in TECH_PANELS:
        for a, b in zip(pts, pts[1:] + pts[:1]):
            if not _on_border(a, b):
                lines.append(_engraved(a, b, ink, lip))
    for pts in TECH_LINES:
        for a, b in zip(pts, pts[1:]):
            lines.append(_engraved(a, b, ink, lip))
    body.append('<g stroke-opacity="0.9">' + "".join(lines) + '</g>')

    # An accent line riding just under the top band's lower edge.
    body.append(f'<line x1="31.5%" y1="5.5%" x2="47%" y2="5.5%" stroke="{accent_line}" '
                f'stroke-width="3" transform="translate(0 9)"/>')
    body.append(f'<line x1="53%" y1="5.5%" x2="68.5%" y2="5.5%" stroke="{accent_line}" '
                f'stroke-width="3" transform="translate(0 9)"/>')
    body.append(f'<line x1="4%" y1="74%" x2="25%" y2="74%" stroke="{accent_line}" '
                f'stroke-width="3" transform="translate(0 9)"/>')
    body.append(f'<line x1="74%" y1="44%" x2="96%" y2="44%" stroke="{accent_line}" '
                f'stroke-width="3" transform="translate(0 -9)"/>')

    defs.append(
        '<g id="pillh"><rect width="170" height="22" rx="11" fill="#000" fill-opacity="0.05" '
        f'stroke="{ink}" stroke-opacity="0.6" stroke-width="2"/>'
        f'<rect x="4" y="3" width="162" height="16" rx="8" fill="none" stroke="{lip}" stroke-width="1.2"/></g>')
    defs.append(
        '<g id="pill"><rect width="22" height="170" rx="11" fill="#000" fill-opacity="0.05" '
        f'stroke="{ink}" stroke-opacity="0.6" stroke-width="2"/>'
        f'<rect x="3" y="4" width="16" height="162" rx="8" fill="none" stroke="{lip}" stroke-width="1.2"/></g>')
    defs.append(
        '<g id="bolt"><polygon points="13,0 6.5,11.3 -6.5,11.3 -13,0 -6.5,-11.3 6.5,-11.3" '
        f'fill="#000" fill-opacity="0.07" stroke="{ink}" stroke-opacity="0.65" stroke-width="2"/>'
        '<polygon points="13,0 6.5,11.3 -6.5,11.3 -13,0" transform="translate(1.5 1.5)" fill="none" '
        f'stroke="{lip}" stroke-width="1"/>'
        '<circle r="6.5" fill="#8d9097"/><circle r="6.5" fill="none" stroke="#5c5f66" stroke-width="1.2"/>'
        '<circle cx="-2" cy="-2" r="2.2" fill="#fff" fill-opacity="0.85"/></g>')
    defs.append(
        f'<g id="notch"><line x2="40" stroke="{lip}" stroke-width="1.6" transform="translate(1.6 1.6)"/>'
        f'<line x2="40" stroke="{ink}" stroke-width="2.4" stroke-opacity="0.75"/></g>')
    defs.append(
        f'<g id="ell"><path d="M0 0 H34 V20" fill="none" stroke="{lip}" stroke-width="1.6" transform="translate(1.6 1.6)"/>'
        f'<path d="M0 0 H34 V20" fill="none" stroke="{ink}" stroke-width="2.4" stroke-opacity="0.75"/></g>')
    defs.append(
        f'<g id="vent"><rect width="30" height="5" rx="1" fill="{accent}"/>'
        f'<rect y="10" width="30" height="5" rx="1" fill="{accent}"/>'
        f'<rect y="20" width="30" height="5" rx="1" fill="{accent}"/></g>')
    defs.append(
        f'<g id="decal"><rect width="6" height="22" fill="{accent}"/>'
        f'<text x="14" y="17" font-family="sans-serif" font-size="17" font-weight="bold" '
        f'letter-spacing="2" fill="{ink}" fill-opacity="0.8">MLN-01</text></g>')
    defs.append(
        f'<g id="decal2"><polygon points="0,16 9,0 18,16" fill="none" stroke="{accent_line}" stroke-width="2.5"/>'
        f'<text x="26" y="15" font-family="sans-serif" font-size="13" letter-spacing="1.5" '
        f'fill="{ink}" fill-opacity="0.7">HANDLE WITH CARE</text></g>')

    uses = [
        ("pill", "5%", "27%"), ("pill", "91%", "15%"),
        ("bolt", "10.5%", "53%"), ("bolt", "88%", "32%"), ("bolt", "13%", "87%"),
        ("bolt", "91%", "70%"), ("bolt", "36%", "3%"), ("bolt", "64%", "3%"),
        ("notch", "16%", "21%"), ("notch", "78%", "93%"), ("notch", "40%", "97%"),
        ("ell", "20%", "48%"), ("ell", "74%", "12%"),
        ("vent", "93.5%", "48%"), ("vent", "4.5%", "66%"),
        ("decal", "88.5%", "79%"), ("decal2", "4%", "77%"),
        ("bolt", "27%", "50%"), ("bolt", "72.5%", "46%"), ("bolt", "33%", "73%"),
        ("bolt", "74%", "66%"), ("notch", "45%", "67%"), ("notch", "22%", "17%"),
        ("ell", "62%", "72%"), ("ell", "31%", "23%"), ("pillh", "41.5%", "60%"),
        ("vent", "53%", "88%"),
    ]
    body.append("".join(f'<use xlink:href="#{i}" href="#{i}" x="{x}" y="{y}"/>' for i, x, y in uses))
    return defs, body


# --- Carbon fiber and honeycomb ------------------------------------------------
# Each tile packs its shapes into a handful of multi-part <path>s, so AndroidSVG
# makes a few draw calls per tile instead of one per weave bundle or hex cell.

def _rect_path(x, y, w, h):
    return f"M{x:.1f} {y:.1f}h{w:.1f}v{h:.1f}h{-w:.1f}z"


def carbon_pattern(c=16, n=16, gap=1.4):
    """2/2 twill weave: bundles two cells long, stepping one cell per row."""
    size = c * n
    h_fill, h_shine, v_fill, v_shine, fibers = [], [], [], [], []
    inner = c - 2 * gap
    for j in range(-2, n + 2):
        for i in range(-2, n + 2):
            x, y = i * c, j * c
            if (i + j) % 4 == 0:  # horizontal bundle over cells i, i+1
                w = 2 * c - 2 * gap
                h_fill.append(_rect_path(x + gap, y + gap, w, inner))
                h_shine.append(_rect_path(x + gap, y + gap + inner * 0.12, w, inner * 0.32))
                for f in (0.62, 0.82):
                    fibers.append(f"M{x + gap + 2:.1f} {y + gap + inner * f:.1f}h{w - 4:.1f}")
            if (i + j) % 4 == 2:  # vertical bundle over cells j, j+1
                h = 2 * c - 2 * gap
                v_fill.append(_rect_path(x + gap, y + gap, inner, h))
                v_shine.append(_rect_path(x + gap + inner * 0.12, y + gap, inner * 0.32, h))
                for f in (0.62, 0.82):
                    fibers.append(f"M{x + gap + inner * f:.1f} {y + gap + 2:.1f}v{h - 4:.1f}")
    return (f'<pattern id="carbon" patternUnits="userSpaceOnUse" width="{size}" height="{size}">'
            f'<rect width="{size}" height="{size}" fill="#0b0c0e"/>'
            f'<path d="{"".join(v_fill)}" fill="#191b1f"/>'
            f'<path d="{"".join(v_shine)}" fill="#24272c"/>'
            f'<path d="{"".join(h_fill)}" fill="#2b2e33"/>'
            f'<path d="{"".join(h_shine)}" fill="#3c4046"/>'
            f'<path d="{"".join(fibers)}" fill="none" stroke="#fff" stroke-opacity="0.05" stroke-width="1"/>'
            '</pattern>')


def hex_pattern(seed, accent, accent_strength="subtle", r=30, cols=20, rows=22):
    """Pointy-top honeycomb with engraved seams, a few raised cells and lit accent cells."""
    import math
    w = math.sqrt(3) * r
    tile_w, tile_h = cols * w, rows * 1.5 * r
    rnd = random.Random(seed)
    raised_cells = {(c, rw) for rw in range(rows) for c in range(cols) if rnd.random() < 0.13}

    def hexagon(cx, cy, rad):
        pts = [(cx + rad * math.cos(math.radians(60 * k - 30)),
                cy + rad * math.sin(math.radians(60 * k - 30))) for k in range(6)]
        return "M" + "L".join(f"{x:.1f} {y:.1f}" for x, y in pts) + "z"

    seams, raised = [], []
    for row in range(-1, rows + 1):
        for col in range(-1, cols + 1):
            cx = col * w + (w / 2 if row % 2 else 0)
            cy = row * 1.5 * r
            seams.append(hexagon(cx, cy, r))
            if (col % cols, row % rows) in raised_cells:
                raised.append(hexagon(cx, cy, r - 3))
    seam = "".join(seams)
    pattern = (f'<pattern id="hex" patternUnits="userSpaceOnUse" width="{tile_w:.2f}" height="{tile_h:.2f}">'
               f'<path d="{"".join(raised)}" fill="#fff" fill-opacity="0.045"/>'
               f'<path d="{seam}" fill="none" stroke="#9fb2c4" stroke-opacity="0.16" stroke-width="1.4" '
               f'transform="translate(0 1.6)"/>'
               f'<path d="{seam}" fill="none" stroke="#06080a" stroke-opacity="0.7" stroke-width="2.2"/>'
               '</pattern>')

    # Lit cells sit on the same grid but outside the tile, scattered over a
    # 3200 px square, so they never repeat. Off-screen ones are just clipped.
    rings, lit = [], []
    for row in range(int(3200 / (1.5 * r)) + 1):
        for col in range(int(3200 / w) + 1):
            roll = rnd.random()
            cx = col * w + (w / 2 if row % 2 else 0)
            cy = row * 1.5 * r
            if roll < 0.010:
                lit.append(hexagon(cx, cy, r - 6))
            elif roll < 0.026:
                rings.append(hexagon(cx, cy, r - 7))
    ring_op, lit_op = (0.55, 0.9) if accent_strength == "bold" else (0.22, 0.14)
    cells = (f'<path d="{"".join(rings)}" fill="none" stroke="{accent}" stroke-opacity="{ring_op}" stroke-width="2"/>'
             f'<path d="{"".join(lit)}" fill="{accent}" fill-opacity="{lit_op}"/>')
    return pattern, cells


def build(d):
    defs = [base_gradient(d["colors"]), gloss_defs(d["highlight"], d["sheen"]),
            vignette_def(d["vignette"])]
    body = []

    finish = d["finish"]
    if finish == "translucent":
        defs.append(pcb_pattern(d["board"], d["trace"]))
        body.append('<rect width="100%" height="100%" fill="url(#pcb)"/>')
        body.append(chips(d["chip"], d["trace"]))
        body.append(f'<rect width="100%" height="100%" fill="url(#base)" fill-opacity="{d["tint"]}"/>')
    else:
        body.append('<rect width="100%" height="100%" fill="url(#base)"/>')

    if finish == "brushed":
        defs.append(brushed_pattern(d["name"]))
        body.append('<rect width="100%" height="100%" fill="url(#brush)"/>')
    if finish == "carbon":
        defs.append(carbon_pattern())
        body.append('<rect width="100%" height="100%" fill="url(#carbon)"/>')
        # A broad, soft light falloff so the weave doesn't look printed flat.
        body.append('<rect width="100%" height="100%" fill="url(#base)" fill-opacity="0.35"/>')
    if finish == "hex":
        hex_defs, hex_cells = hex_pattern(d["name"], d["accent"])
        defs.append(hex_defs)
        body.append('<rect width="100%" height="100%" fill="url(#hex)"/>')
        body.append(hex_cells)
    if finish == "flake":
        body.append(sparkles(d["name"], 700, d["flake"]))
    if d.get("pearl"):
        defs.append('<linearGradient id="pearl" x1="0" y1="1" x2="1" y2="0">'
                    '<stop offset="0" stop-color="#f7c6d9" stop-opacity="0.22"/>'
                    '<stop offset="0.5" stop-color="#fff" stop-opacity="0"/>'
                    '<stop offset="1" stop-color="#bfe0f7" stop-opacity="0.25"/></linearGradient>')
        body.append('<rect width="100%" height="100%" fill="url(#pearl)"/>')
    if d.get("tech"):
        tech_defs, tech_body = tech_layer(*d["tech"])
        defs.extend(tech_defs)
        body.extend(tech_body)

    # Edge rings: draw widest first, then cover the outer part with the body
    # color so only a thin line is left at each inset.
    plastic = "url(#base)"
    if finish == "translucent":
        # Thicker plastic at the edge: the internals fade out under it.
        plastic = d["colors"][1]
    if d.get("pinstripe"):
        body.append(ring(d["pinstripe"], 2 * (PIN_IN + PIN_W), PIN_RADIUS))
        body.append(ring(plastic, 2 * PIN_IN, PIN_RADIUS,
                         d["tint"] if finish == "translucent" else None))
    body.append(ring(d["groove_light"], 2 * (GROOVE_IN + GROOVE_DARK + GROOVE_LIGHT), GROOVE_RADIUS))
    body.append(ring(d["groove_dark"], 2 * (GROOVE_IN + GROOVE_DARK), GROOVE_RADIUS))
    body.append(ring(plastic, 2 * GROOVE_IN, GROOVE_RADIUS))

    if d.get("grille"):
        kind, dark, light = d["grille"]
        defs.append(slot_grille(dark, light) if kind == "slots" else hole_grille(dark, light))
        body.append('<use xlink:href="#grille" href="#grille" x="100%" y="100%"/>')

    body.append('<rect width="100%" height="100%" fill="url(#hl)"/>')
    if d["sheen"] > 0:
        body.append('<rect width="100%" height="100%" fill="url(#sheen)"/>')
    body.append('<rect width="100%" height="100%" fill="url(#vig)"/>')

    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            f'<!-- melonadeDS shell background: {d["title"]} -->\n'
            '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
            'width="100%" height="100%">\n'
            '<defs>' + "".join(defs) + '</defs>\n'
            + "\n".join(body) + '\n</svg>\n')


DESIGNS = [
    # All matte: no sheen band, only a soft broad light from the top left.
    dict(name="crimson", title="Crimson (matte red)", colors=("#e2343e", "#bf1821", "#7d0910"),
         finish="matte", highlight=0.12, sheen=0, vignette=0.35,
         groove_dark="#3d0306", groove_light="#ff8c92",
         grille=("slots", "#4a0509", "#ff7a82")),
    dict(name="onyx", title="Onyx (matte black)", colors=("#3b3b40", "#202024", "#0b0b0d"),
         finish="matte", highlight=0.08, sheen=0, vignette=0.45,
         groove_dark="#000", groove_light="#4a4a50"),
    dict(name="pearl", title="Pearl (matte white)", colors=("#fbfaf8", "#eceae6", "#c6c2bb"),
         finish="matte", highlight=0.15, sheen=0, vignette=0.18,
         groove_dark="#9c978e", groove_light="#ffffff",
         grille=("holes", "#8f8a82", "#ffffff")),
    dict(name="pearl-tech", title="Pearl Tech (white, panel lines, lime accents)",
         colors=("#fbfaf8", "#eceae6", "#c6c2bb"),
         finish="matte", highlight=0.15, sheen=0, vignette=0.18,
         groove_dark="#9c978e", groove_light="#ffffff",
         tech=("#a6d70a", "#8cbf00")),
    dict(name="atomic-purple", title="Atomic Purple (frosted translucent)", colors=("#9a6fd6", "#6b3fb0", "#3a1d6e"),
         finish="translucent", tint=0.74, board="#16301f", trace="#5aa36a", chip="#0c0c10",
         highlight=0.12, sheen=0, vignette=0.40,
         groove_dark="#1d0b3d", groove_light="#c9a8ff"),
    dict(name="cobalt", title="Cobalt (matte metallic blue)", colors=("#4677ee", "#1c45c4", "#0a1f6e"),
         finish="flake", flake=("#ffffff", "#bcd4ff", "#7fa6ff"),
         highlight=0.10, sheen=0, vignette=0.40,
         groove_dark="#050f3a", groove_light="#8fb0ff"),
    dict(name="glacier", title="Glacier (frosted clear)", colors=("#f2fbff", "#cfe8f5", "#8fb8cf"),
         finish="translucent", tint=0.76, board="#1d3a2a", trace="#6fbf80", chip="#15181c",
         highlight=0.15, sheen=0, vignette=0.25,
         groove_dark="#5d8297", groove_light="#ffffff"),
    dict(name="kiwi", title="Kiwi (matte green)", colors=("#b9e44a", "#86c21b", "#4a7a09"),
         finish="matte", highlight=0.12, sheen=0, vignette=0.30,
         groove_dark="#2a4703", groove_light="#e8ff9e"),
    dict(name="dandelion", title="Dandelion (matte yellow)", colors=("#ffdd5c", "#f5b800", "#b07c00"),
         finish="matte", highlight=0.12, sheen=0, vignette=0.28,
         groove_dark="#6b4a00", groove_light="#fff3b0",
         grille=("slots", "#6b4800", "#fff0a0")),
    dict(name="gunmetal", title="Gunmetal (brushed metal)", colors=("#9aa0a7", "#5c6168", "#2c3035"),
         finish="brushed", highlight=0.10, sheen=0, vignette=0.40,
         groove_dark="#15171a", groove_light="#c9ced4"),
    dict(name="coral", title="Coral (matte pink)", colors=("#ff9a8b", "#f2604f", "#b03526"),
         finish="matte", highlight=0.12, sheen=0, vignette=0.30,
         groove_dark="#6e1a10", groove_light="#ffc4ba"),
    dict(name="carbon", title="Carbon (twill carbon fiber)", colors=("#5a5e66", "#1c1e22", "#000000"),
         finish="carbon", highlight=0.14, sheen=0, vignette=0.45,
         groove_dark="#000", groove_light="#5c6068"),
    dict(name="hex", title="Hex (slate honeycomb)", colors=("#46525e", "#29313a", "#12161b"),
         finish="hex", accent="#c3d2e0", highlight=0.12, sheen=0, vignette=0.40,
         groove_dark="#05070a", groove_light="#7d8c9b"),
    dict(name="smoke", title="Smoke (frosted translucent black)", colors=("#4a4a52", "#25252b", "#0d0d10"),
         finish="translucent", tint=0.84, board="#1a2e22", trace="#4f8f5e", chip="#050506",
         highlight=0.10, sheen=0, vignette=0.45,
         groove_dark="#000", groove_light="#6a6a74"),
]


def main():
    os.makedirs(OUT, exist_ok=True)
    outputs = [(d["name"], build(d)) for d in DESIGNS]
    for name, svg in outputs:
        path = os.path.join(OUT, f"{name}.svg")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(svg)
        print(f"{path}  {os.path.getsize(path) // 1024} KB")


if __name__ == "__main__":
    main()
