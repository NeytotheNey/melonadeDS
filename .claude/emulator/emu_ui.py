"""Helpers for emu.sh: shrink screenshots and list on-screen UI nodes from a uiautomator dump."""
import re
import sys
import xml.etree.ElementTree as ET


def half(src, dst, factor):
    from PIL import Image
    img = Image.open(src)
    w, h = img.size
    small = img.resize((max(1, w // factor), max(1, h // factor)), Image.LANCZOS)
    small.save(dst, optimize=True)
    print(f"full {w}x{h} -> saved {small.size[0]}x{small.size[1]} (multiply image coordinates by {factor} to tap)")


def nodes(xml_path, query=None):
    root = ET.parse(xml_path).getroot()
    # A leading "=" asks for an exact label match instead of a substring
    exact = bool(query) and query.startswith("=")
    q = (query[1:] if exact else query).lower() if query else None
    found = 0
    for n in root.iter("node"):
        text = (n.get("text") or "").strip()
        desc = (n.get("content-desc") or "").strip()
        label = text or desc
        clickable = n.get("clickable") == "true"
        if not label and not clickable:
            continue
        m = re.match(r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]", n.get("bounds", ""))
        if not m:
            continue
        x1, y1, x2, y2 = map(int, m.groups())
        if x2 <= x1 or y2 <= y1:
            continue
        if q and (label.lower() != q if exact else q not in label.lower()):
            continue
        cls = (n.get("class") or "").rsplit(".", 1)[-1]
        flags = []
        if clickable:
            flags.append("clickable")
        if n.get("checked") == "true":
            flags.append("checked")
        if n.get("enabled") == "false":
            flags.append("disabled")
        print(f"({(x1 + x2) // 2},{(y1 + y2) // 2}) {cls} '{label}' {' '.join(flags)}".rstrip())
        found += 1
    if found == 0:
        print("no matching nodes" if q else "no labelled or clickable nodes (a GL game screen has none; take a screenshot instead)")
    return found


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "half":
        half(sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 2)
    elif cmd == "nodes":
        sys.exit(0 if nodes(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None) else 1)
