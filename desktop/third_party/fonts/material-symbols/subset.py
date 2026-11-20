"""Cuts Material Symbols Outlined down to the icons the desktop menus use.

    python -I subset.py MaterialSymbolsOutlined[FILL,GRAD,opsz,wght].ttf ../../../src/ui/Icons.h MaterialSymbolsOutlined-subset.ttf

The input is the variable font from google/material-design-icons (variablefont/, the commit in third_party/README.md). It is pinned
to its default instance (FILL 0, GRAD 0, opsz 24, wght 400: the plain outlined look) and cut to the codepoints in Icons.h, without the
ligature tables (the menus use codepoints, not names) or hinting. Needs fontTools (pip install fonttools).
"""
import re
import sys

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 1
    source, icons_header, output = sys.argv[1:]
    with open(icons_header, encoding="utf-8") as f:
        codepoints = sorted({int(m, 16) for m in re.findall(r"=\s*0x([0-9a-fA-F]{4,5})\s*;", f.read())})
    if not codepoints:
        print("no codepoints in", icons_header)
        return 1

    font = TTFont(source)
    if "fvar" in font:
        defaults = {axis.axisTag: axis.defaultValue for axis in font["fvar"].axes}
        font = instancer.instantiateVariableFont(font, defaults)

    options = subset.Options()
    options.layout_features = []
    options.hinting = False
    options.notdef_outline = True
    options.name_IDs = ["*"]
    options.name_languages = ["*"]
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=codepoints)
    subsetter.subset(font)
    font.save(output)

    kept = set(font.getBestCmap().keys())
    missing = [hex(c) for c in codepoints if c not in kept]
    print(f"{output}: {len(codepoints)} icons" + (f", missing {missing}" if missing else ""))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
