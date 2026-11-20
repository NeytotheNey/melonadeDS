"""Builds the desktop app's icon (desktop/src/app.ico) from the Android app's launcher icon.

    python -I tools/desktop-icon/make_icon.py app/src/main/res desktop/src/app.ico

The launcher icon is an adaptive icon (res/mipmap-anydpi-v26/ic_launcher.xml): a flat background color and a vector drawable
foreground (res/drawable/ic_launcher_foreground.xml) on a 108 x 108 canvas, of which a launcher shows the middle. This draws the
foreground's paths (M, L, H, V, Q, C, A and Z, fills and round-capped strokes, groups rotated about a pivot) over a rounded square of the
background color, the middle 68 x 68 of the canvas, at 16 to 256 pixels, each drawn at 8x and scaled down for smooth edges, and writes
them into one .ico. Colors named @color/... come from res/values/colors.xml. Needs Pillow (pip install pillow).
"""
import math
import re
import sys
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw

ANDROID = '{http://schemas.android.com/apk/res/android}'
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
# the part of the 108 x 108 canvas the icon shows, and the corner radius of its square (of the icon's size)
VIEW = (20.0, 20.0, 88.0, 88.0)
CORNER = 0.22
SUPERSAMPLE = 8


def read_colors(res):
    colors = {}
    for item in ET.parse(f'{res}/values/colors.xml').getroot().iter('color'):
        colors[item.get('name')] = item.text.strip()
    return colors


def parse_color(text, colors, alpha=1.0):
    while text.startswith('@color/'):
        text = colors[text[len('@color/'):]]
    text = text.lstrip('#')
    if len(text) == 6:
        a, r, g, b = 255, int(text[0:2], 16), int(text[2:4], 16), int(text[4:6], 16)
    elif len(text) == 8:
        a, r, g, b = int(text[0:2], 16), int(text[2:4], 16), int(text[4:6], 16), int(text[6:8], 16)
    else:
        raise ValueError('color ' + text)
    return (r, g, b, round(a * alpha))


def arc_points(x1, y1, rx, ry, angle, large, sweep, x2, y2, steps=32):
    # SVG's endpoint arc to its center form (SVG 1.1, F.6.5), then points along it
    if rx == 0 or ry == 0:
        return [(x2, y2)]
    phi = math.radians(angle)
    cos, sin = math.cos(phi), math.sin(phi)
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p, y1p = cos * dx + sin * dy, -sin * dx + cos * dy
    rx, ry = abs(rx), abs(ry)
    lam = x1p ** 2 / rx ** 2 + y1p ** 2 / ry ** 2
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx ** 2 * ry ** 2 - rx ** 2 * y1p ** 2 - ry ** 2 * x1p ** 2
    den = rx ** 2 * y1p ** 2 + ry ** 2 * x1p ** 2
    coef = math.sqrt(max(num / den, 0)) * (-1 if large == sweep else 1)
    cxp, cyp = coef * rx * y1p / ry, -coef * ry * x1p / rx
    cx = cos * cxp - sin * cyp + (x1 + x2) / 2
    cy = sin * cxp + cos * cyp + (y1 + y2) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    points = []
    for i in range(1, steps + 1):
        t = t1 + dt * i / steps
        x, y = rx * math.cos(t), ry * math.sin(t)
        points.append((cos * x - sin * y + cx, sin * x + cos * y + cy))
    return points


def parse_path(data):
    """The path's subpaths, each a list of points (curves flattened) and whether it's closed."""
    tokens = re.findall(r'[MmLlHhVvQqCcAaZz]|-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?', data)
    subpaths, current = [], []
    x = y = sx = sy = 0.0
    i, cmd = 0, None

    def num():
        nonlocal i
        v = float(tokens[i])
        i += 1
        return v

    while i < len(tokens):
        if re.match(r'[A-Za-z]', tokens[i]):
            cmd = tokens[i]
            i += 1
        rel = cmd.islower()
        c = cmd.upper()
        if c == 'Z':
            if current:
                subpaths.append((current, True))
            current = []
            x, y = sx, sy
            continue
        if c == 'M':
            if current:
                subpaths.append((current, False))
            nx, ny = num(), num()
            x, y = (x + nx, y + ny) if rel else (nx, ny)
            sx, sy = x, y
            current = [(x, y)]
            cmd = 'l' if rel else 'L'
        elif c == 'L':
            nx, ny = num(), num()
            x, y = (x + nx, y + ny) if rel else (nx, ny)
            current.append((x, y))
        elif c == 'H':
            nx = num()
            x = x + nx if rel else nx
            current.append((x, y))
        elif c == 'V':
            ny = num()
            y = y + ny if rel else ny
            current.append((x, y))
        elif c == 'Q':
            qx, qy, nx, ny = num(), num(), num(), num()
            if rel:
                qx, qy, nx, ny = x + qx, y + qy, x + nx, y + ny
            for k in range(1, 17):
                t = k / 16
                current.append(((1 - t) ** 2 * x + 2 * (1 - t) * t * qx + t ** 2 * nx,
                                (1 - t) ** 2 * y + 2 * (1 - t) * t * qy + t ** 2 * ny))
            x, y = nx, ny
        elif c == 'C':
            ax, ay, bx, by, nx, ny = num(), num(), num(), num(), num(), num()
            if rel:
                ax, ay, bx, by, nx, ny = x + ax, y + ay, x + bx, y + by, x + nx, y + ny
            for k in range(1, 21):
                t = k / 20
                u = 1 - t
                current.append((u ** 3 * x + 3 * u * u * t * ax + 3 * u * t * t * bx + t ** 3 * nx,
                                u ** 3 * y + 3 * u * u * t * ay + 3 * u * t * t * by + t ** 3 * ny))
            x, y = nx, ny
        elif c == 'A':
            rx, ry, angle, large, sweep, nx, ny = num(), num(), num(), num(), num(), num(), num()
            if rel:
                nx, ny = x + nx, y + ny
            current += arc_points(x, y, rx, ry, angle, int(large), int(sweep), nx, ny)
            x, y = nx, ny
        else:
            raise ValueError('path command ' + cmd)
    if current:
        subpaths.append((current, False))
    return subpaths


def collect(node, transforms, colors, out):
    """The paths under [node], in drawing order, with their transforms (rotations about pivots, outermost first)."""
    for child in node:
        tag = child.tag
        if tag == 'group':
            t = (float(child.get(ANDROID + 'rotation', '0')), float(child.get(ANDROID + 'pivotX', '0')),
                 float(child.get(ANDROID + 'pivotY', '0')))
            collect(child, transforms + [t], colors, out)
        elif tag == 'path':
            out.append((child, transforms))


def apply(point, transforms):
    x, y = point
    # the innermost group's rotation first
    for angle, px, py in reversed(transforms):
        a = math.radians(angle)
        dx, dy = x - px, y - py
        x, y = px + dx * math.cos(a) - dy * math.sin(a), py + dx * math.sin(a) + dy * math.cos(a)
    return x, y


def render(size, background, paths, colors):
    big = size * SUPERSAMPLE
    image = Image.new('RGBA', (big, big), (0, 0, 0, 0))
    # the rounded square, the background color
    mask = Image.new('L', (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, big - 1, big - 1), radius=round(big * CORNER), fill=255)
    image.paste(Image.new('RGBA', (big, big), background), (0, 0), mask)
    scale = big / (VIEW[2] - VIEW[0])

    def to_px(p):
        return ((p[0] - VIEW[0]) * scale, (p[1] - VIEW[1]) * scale)

    for node, transforms in paths:
        layer = Image.new('RGBA', (big, big), (0, 0, 0, 0))
        draw = ImageDraw.Draw(layer)
        subpaths = [([to_px(apply(p, transforms)) for p in points], closed) for points, closed in parse_path(node.get(ANDROID + 'pathData'))]
        fill = node.get(ANDROID + 'fillColor')
        if fill:
            color = parse_color(fill, colors, float(node.get(ANDROID + 'fillAlpha', '1')))
            for points, _ in subpaths:
                if len(points) >= 3:
                    draw.polygon(points, fill=color)
        stroke = node.get(ANDROID + 'strokeColor')
        if stroke:
            color = parse_color(stroke, colors, float(node.get(ANDROID + 'strokeAlpha', '1')))
            width = float(node.get(ANDROID + 'strokeWidth', '1')) * scale
            round_cap = node.get(ANDROID + 'strokeLineCap', 'butt') == 'round'
            for points, closed in subpaths:
                line = points + [points[0]] if closed else points
                draw.line(line, fill=color, width=max(1, round(width)), joint='curve')
                if round_cap and not closed:
                    r = width / 2
                    for px, py in (points[0], points[-1]):
                        draw.ellipse((px - r, py - r, px + r, py + r), fill=color)
        # the strokes' alpha is the path's, not added up where it crosses itself: drawn on a layer of its own
        image = Image.alpha_composite(image, layer)
    # cut to the rounded square again (strokes may reach past it), then down to size
    clipped = Image.new('RGBA', (big, big), (0, 0, 0, 0))
    clipped.paste(image, (0, 0), mask)
    return clipped.resize((size, size), Image.LANCZOS)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    res, output = sys.argv[1], sys.argv[2]
    colors = read_colors(res)
    launcher = ET.parse(f'{res}/mipmap-anydpi-v26/ic_launcher.xml').getroot()
    background = parse_color(launcher.find('background').get(ANDROID + 'drawable'), colors)
    foreground_name = launcher.find('foreground').get(ANDROID + 'drawable').split('/')[-1]
    foreground = ET.parse(f'{res}/drawable/{foreground_name}.xml').getroot()
    paths = []
    collect(foreground, [], colors, paths)
    images = [render(size, background, paths, colors) for size in SIZES]
    images[-1].save(output, format='ICO', sizes=[(s, s) for s in SIZES], append_images=images[:-1])
    print(f'{output}: {len(SIZES)} sizes, {len(paths)} paths')
    return 0


if __name__ == '__main__':
    sys.exit(main())
