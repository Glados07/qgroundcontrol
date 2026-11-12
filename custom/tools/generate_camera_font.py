"""Generate the checked-in camera control font; not part of the application build.

Input: the pinned Noto Sans SC variable TTF documented beside the output font.
Requires fontTools. The resulting static font retains the upstream OFL notices.
"""
import argparse
import hashlib
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont


def camera_characters():
    points = set(range(0x20, 0x250))  # Latin, numbers and application units.
    points.update(range(0x2000, 0x2070))
    points.update(range(0x3000, 0x3040))
    points.update(range(0xFF00, 0xFFF0))
    for first in range(0xA1, 0xF8):
        for second in range(0xA1, 0xFF):
            try:
                points.add(ord(bytes((first, second)).decode("gb2312")))
            except UnicodeDecodeError:
                pass
    return points


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    font = TTFont(args.source, recalcTimestamp=False)
    instantiateVariableFont(font, {"wght": 600}, inplace=True)
    options = subset.Options()
    options.name_IDs = ["*"]
    options.name_languages = ["*"]
    options.name_legacy = True
    options.notdef_glyph = True
    options.notdef_outline = True
    subsetter = subset.Subsetter(options=options)
    subsetter.populate(unicodes=camera_characters())
    subsetter.subset(font)
    # Give the modified subset a distinct family instead of claiming the full
    # upstream font. Keep copyright, license and attribution name records.
    names = {1: "QGC Camera Sans", 2: "SemiBold", 3: "QGC Camera Sans SemiBold 1.0",
             4: "QGC Camera Sans SemiBold", 6: "QGCCameraSans-SemiBold",
             16: "QGC Camera Sans", 17: "SemiBold"}
    for record in font["name"].names:
        if record.nameID in names:
            record.string = names[record.nameID].encode(record.getEncoding())
    for name_id, value in names.items():
        font["name"].setName(value, name_id, 3, 1, 0x409)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    font.save(args.output)
    payload = args.output.read_bytes()
    print(f"{args.output.name}: {len(payload)} bytes, SHA256 {hashlib.sha256(payload).hexdigest()}")


if __name__ == "__main__":
    main()
