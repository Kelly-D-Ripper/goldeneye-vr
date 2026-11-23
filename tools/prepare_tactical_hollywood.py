"""Bake selected HollywoodFX particle sheets for the Windows comparison build."""
import argparse
from pathlib import Path

import UnityPy
from PIL import Image, ImageChops


ATLAS = {
    "flash": ("Rifle_Main_Jet_1", 1, 8, range(8)),
    "jet": ("Rifle_Forward_Jet_Narrow_1", 1, 8, range(8)),
    "dim-jet": ("Rifle_Forward_Jet_Narrow_Dim_1", 1, 8, range(8)),
    "muzzle-smoke": ("Puff02_8x8", 8, 8, (6, 13, 19, 26, 33, 41, 50, 57)),
    "concrete": ("Puff_Vert01_8x8", 8, 8, (4, 9, 16, 23, 30, 38, 47, 55)),
    "dust": ("Puff_Dusty_01", 8, 8, (3, 8, 14, 21, 28, 36, 45, 54)),
    "metal-sparks": ("Sparks_01", 4, 4, (2, 3, 4, 5, 6, 8, 10, 12)),
    "wood-chips": ("WoodDebris", 4, 4, (1, 2, 4, 5, 9, 10, 13, 14)),
}


def bake(bundle, output):
    needed = {spec[0] for spec in ATLAS.values()}
    atlas = {obj.peek_name(): obj for obj in UnityPy.load(str(bundle)).objects
             if obj.type.name == "Texture2D" and obj.peek_name() in needed}
    if atlas.keys() != needed:
        raise ValueError(f"Missing HollywoodFX layers: {needed - atlas.keys()}")
    output.mkdir(parents=True, exist_ok=True)
    flash_edge = Image.new("L", (120, 120))
    flash_edge.putdata([
        min(255, round(255 * min(x, y, 119 - x, 119 - y) / 15))
        for y in range(120) for x in range(120)
    ])
    for label, (name, columns, rows, frames) in ATLAS.items():
        source = atlas[name].read().image.convert("RGBA")
        frame_w, frame_h = source.width // columns, source.height // rows
        result = Image.new("RGBA", (128 * len(frames), 128))
        for index, frame in enumerate(frames):
            x, y = (frame % columns) * frame_w, (frame // columns) * frame_h
            image = source.crop((x, y, x + frame_w, y + frame_h))
            # Leave a transparent border so the mipmaps don't bleed across cells.
            image = image.resize((120, 120), Image.Resampling.LANCZOS)
            if label in ("flash", "jet", "dim-jet"):
                # The Unity materials soften their margins through additive
                # blending. Feather the baked cell for our translucent pass.
                image.putalpha(ImageChops.multiply(image.getchannel("A"), flash_edge))
            result.paste(image, (index * 128 + 4, 4))
        result.save(output / (label + ".png"), optimize=True)
        print(f"{label}: {name}, {len(frames)} frames")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    bake(args.bundle, args.output)
