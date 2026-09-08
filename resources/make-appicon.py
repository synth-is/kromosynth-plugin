#!/usr/bin/env python3
"""Regenerates resources/AppIcon-1024.png from the Synth.is brand assets.

WHY A DERIVED FILE IS COMMITTED. src/CMakeLists.txt feeds ICON_BIG to
juce_add_plugin, and juceaide reads it at CONFIGURE time. Pointing that at the
branding folder would hard-code an absolute path under one developer's home
directory and break configure on the hub and the Studio, so the icon lives in
the repo and this script records how it got there.

WHY NOT Profile-pic-black.png DIRECTLY, which is what was asked for. It is
934x934 with TRANSPARENT CORNERS: it is a disc, not a square. Two problems.
An iOS app icon must have no alpha channel -- submission rejects one, and the
transparent corners would composite against whatever is behind them. And iOS
applies its OWN rounded mask, so supplying a disc means the mask cuts a circle
out of a circle. Feeding the 934 source to juceaide would also upscale it into
the 1024 slot.

So: the white mark from Icon-white.png (1080x1080, downscaled and therefore
sharp) composited onto an OPAQUE black square, at the same relative scale the
designer used in Profile-pic-black.png. Appearance is identical once iOS masks
the corners; the difference is that this one is valid.
"""
import sys
from pathlib import Path
from PIL import Image

BRAND = Path("/Users/bjornpjo/Documents/synth.is/Synth.is-Branding")
OUT = Path(__file__).parent / "AppIcon-1024.png"
SIZE = 1024

def mark_scale(reference: Path) -> float:
    """The mark's extent as a fraction of the canvas, taken from the reference.

    Measured rather than chosen, so the padding is the designer's and not mine.
    Alpha alone cannot find it -- the disc is opaque too -- so this looks for
    pixels that are light against the black disc.
    """
    im = Image.open(reference).convert("RGBA")
    w, h = im.size
    px = im.load()
    box = None
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a > 128 and (r + g + b) > 3 * 128:
                box = (x, y, x, y) if box is None else (
                    min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y))
    if box is None:
        sys.exit(f"found no light pixels in {reference}")
    extent = max(box[2] - box[0], box[3] - box[1]) + 1
    print(f"reference {reference.name}: canvas {w}x{h}, mark bbox {box}, "
          f"extent {extent}px = {extent / w:.4f} of canvas")
    return extent / w

def build(scale: float) -> Image.Image:
    mark = Image.open(BRAND / "Synth-Logos/PNG/Icon-white.png").convert("RGBA")
    bbox = mark.getbbox()          # alpha bbox: the mark on transparency
    mark = mark.crop(bbox)
    target = round(SIZE * scale)
    ratio = target / max(mark.size)
    mark = mark.resize((max(1, round(mark.width * ratio)),
                        max(1, round(mark.height * ratio))), Image.LANCZOS)
    print(f"mark cropped to {bbox[2]-bbox[0]}x{bbox[3]-bbox[1]}, "
          f"resized to {mark.size} on a {SIZE}px canvas")

    canvas = Image.new("RGB", (SIZE, SIZE), (0, 0, 0))   # RGB: no alpha channel
    canvas.paste(mark, ((SIZE - mark.width) // 2,
                        (SIZE - mark.height) // 2), mark)
    return canvas

if __name__ == "__main__":
    if not BRAND.is_dir():
        sys.exit(f"brand assets not found at {BRAND}\n"
                 "This script only runs where the branding folder is checked out; "
                 "the generated AppIcon-1024.png is committed so builds do not need it.")
    icon = build(mark_scale(BRAND / "annad/Profile-pic-black.png"))
    icon.save(OUT, "PNG", optimize=True)
    print(f"wrote {OUT} mode={icon.mode} size={icon.size}")
