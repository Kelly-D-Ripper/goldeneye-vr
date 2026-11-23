"""Bake BloodyCombat's fluid animations and decal masks for Tactical's sprite renderer."""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageOps
import UnityPy
from UnityPy.helpers.MeshHelper import MeshHandler


SIZE = 128
FRAMES = 8
# Pack material IDs, source frame numbers, and the normalized point where the spray starts.
SPRAYS = {
    "blood-spray": (2901183333194844699, (0, 2, 4, 6, 8, 10, 13, 16), (.46, .384), (.68, .41)),
    "blood-stream": (-6419403842180219581, (2, 4, 5, 6, 8, 10, 12, 15), (.315, .452), (.67, .34)),
}
MASKS = (
    -7576509176096340355, -7369066879606089001, -9096290748714798322,
    110674712910532548, -1960881635277807968, 2674670768114622944,
    966261235370831617, -6248759548296469561,
)


def atlas(frames):
    sheet = Image.new("RGBA", (FRAMES * SIZE, SIZE))
    for column, frame in enumerate(frames):
        sheet.paste(frame, (column * SIZE, 0))
    return sheet


def vertices_for_material(objects, material_id):
    for obj in objects.values():
        if obj.type.name != "MeshRenderer":
            continue
        renderer = obj.read()
        if not any(ref.m_PathID == material_id for ref in renderer.m_Materials):
            continue
        for component in renderer.m_GameObject.read().m_Component:
            if component.component.type.name == "MeshFilter":
                mesh = MeshHandler(component.component.read().m_Mesh.read())
                mesh.process()
                return np.asarray(mesh.m_UV0), np.asarray(mesh.get_triangles()[0])
    raise ValueError(f"No mesh for material {material_id}")


def spray_frames(objects, material_id, frames, origin, extent):
    material = objects[material_id].read().m_SavedProperties
    position = dict(material.m_TexEnvs)["_posTex"].m_Texture.read()
    frame_count = int(dict(material.m_Floats)["_numOfFrames"])
    width, height = position.m_Width, position.m_Height
    if position.m_TextureFormat != 17 or height % frame_count:
        raise ValueError(f"{position.m_Name} is not a row-packed RGBAHalf animation")
    data = np.frombuffer(position.get_image_data(), dtype="<f2", count=width * height * 4)
    positions = data.reshape(frame_count, height // frame_count, width, 4)
    uv, faces = vertices_for_material(objects, material_id)
    u = np.floor(uv[:, 0] * width).astype(int)
    v = np.floor((1 - uv[:, 1]) * height).astype(int)
    # With the nozzle at 22% of the frame, the runtime keeps the first fluid
    # motion anchored to the hit and can rotate the spray in the view plane.
    origin_px = np.array([.22, .5])
    supersample = SIZE * 3
    sun = np.array([.22, .86, -.46])
    yy, xx = np.indices((SIZE, SIZE))
    edge_fade = np.clip(np.minimum.reduce((xx, SIZE - 1 - xx, yy, SIZE - 1 - yy)) / 8, 0, 1)

    for frame in frames:
        triangles = positions[frame, v, u, :3].astype(np.float32)[faces]
        edges = triangles[:, 1:] - triangles[:, :1]
        normal = np.cross(edges[:, 0], edges[:, 1])
        area = np.linalg.norm(normal, axis=1)
        # The unused vertices in a dynamic VAT frame form collapsed triangles.
        # Drop large discontinuities across unused packed rows as well.
        valid = (area > 1e-9) & (np.max(np.linalg.norm(edges, axis=2), axis=1) < .08)
        triangles = triangles[valid]
        lighting = np.abs(normal[valid] @ sun) / area[valid]
        points = triangles[:, :, :2] - origin
        points[:, :, 0] += .08 * (triangles[:, :, 2] - .25)
        points = (points / [extent[0], -extent[1]] + origin_px) * supersample
        img = Image.new("RGBA", (supersample, supersample))
        draw = ImageDraw.Draw(img)
        for i in np.argsort(triangles[:, :, 2].mean(axis=1))[::-1]:
            shade = lighting[i]
            color = (126 + int(88 * shade), 10 + int(22 * shade), 13 + int(21 * shade), 252)
            draw.polygon([tuple(point) for point in points[i]], fill=color)
        img = img.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
        # Keep the fine silhouette legible through the game's texture filtering.
        coverage = img.getchannel("A").filter(ImageFilter.MaxFilter(3))
        dark_volume = Image.new("RGBA", img.size, (138, 16, 18))
        dark_volume.putalpha(coverage.point(lambda value: value * 3 // 4))
        mist = Image.new("RGBA", img.size, (159, 18, 20))
        mist.putalpha(coverage.filter(ImageFilter.GaussianBlur(3.0)).point(lambda value: value * 2 // 3))
        spray = Image.alpha_composite(Image.alpha_composite(mist, dark_volume), img)
        alpha = np.asarray(spray.getchannel("A"))
        spray.putalpha(Image.fromarray((alpha * edge_fade).astype(np.uint8)))
        yield spray


def fit_mask(mask, width, angle=0):
    bounds = mask.point(lambda value: 255 if value > 18 else 0).getbbox()
    cropped = mask.crop(bounds).rotate(angle, Image.Resampling.BICUBIC, expand=True)
    cropped = ImageOps.contain(cropped.crop(cropped.getbbox()), (width, width), Image.Resampling.LANCZOS)
    padded = Image.new("L", (SIZE, SIZE))
    padded.paste(cropped, ((SIZE - cropped.width) // 2, (SIZE - cropped.height) // 2))
    return padded


def decoded_mask(objects, path_id):
    img = objects[path_id].read().image.convert("RGBA")
    # Some source masks have a solid pixel border used by the original shader.
    img = img.crop((5, 5, img.width - 5, img.height - 5))
    alpha = img.getchannel("A")
    if alpha.getextrema()[0] < 255:
        return alpha
    return ImageChops.lighter(img.getchannel("R"), img.getchannel("G"))


def fine_droplets(rng, count, minimum, maximum):
    size = SIZE * 3
    mask = Image.new("L", (size, size))
    draw = ImageDraw.Draw(mask)
    headings = rng.uniform(0, 2 * np.pi, size=4)
    for i in range(count):
        angle = headings[i % len(headings)] + rng.normal(0, .30)
        distance = rng.uniform(minimum, maximum) * size
        x = size * .5 + np.cos(angle) * distance
        y = size * .5 + np.sin(angle) * distance
        radius = size * rng.uniform(.003, .012)
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=int(rng.integers(135, 247)))
    return mask.resize((SIZE, SIZE), Image.Resampling.LANCZOS)


def stain_frames(objects, burst, decals):
    sources = [decoded_mask(objects, path_id) for path_id in MASKS]
    for index, source in enumerate(sources):
        rng = np.random.default_rng(4123 + index)
        trace = fit_mask(source, 115, index * 67 - 12)
        stain = fit_mask(sources[0], 50 + index % 4 * 9, index * 39 - 10)
        if decals and index % 2 == 0:
            stain = fit_mask(decals[index // 2], 63 + index % 3 * 10, index * 29 - 15)
        elif burst is not None and index % 3 == 1:
            stain = fit_mask(burst, 66, index * 46)
        trace = trace.point(lambda value: min(223, value * 2))
        stain = stain.point(lambda value: min(245, value * 5 // 4))
        coverage = ImageChops.lighter(stain, trace)
        coverage = ImageChops.lighter(coverage, fine_droplets(rng, 62, .18, .44))
        layer = ImageOps.colorize(coverage, (91, 9, 14), (119, 15, 21)).convert("RGBA")
        layer.putalpha(coverage)
        yield layer


def noise_field(rng, resolution):
    grid = Image.fromarray(rng.integers(0, 256, size=(resolution, resolution), dtype=np.uint8))
    return np.asarray(grid.resize((SIZE, SIZE), Image.Resampling.BICUBIC), dtype=float) / 255


def mist_frames(burst):
    rng = np.random.default_rng(5104)
    y, x = (np.indices((SIZE, SIZE), dtype=float) + .5) * (2 / SIZE) - 1
    mottled = .56 * noise_field(rng, 9) + .30 * noise_field(rng, 19) + .14 * noise_field(rng, 43)
    plume = np.zeros_like(x)
    for cx, cy, width, weight in ((-.26, -.13, .41, .68), (.28, -.08, .34, .58),
                                  (.02, .2, .33, .59), (.15, -.34, .24, .44), (-.39, .27, .24, .35)):
        plume += weight * np.exp(-((x - cx) ** 2 + (y - cy) ** 2) / width ** 2)
    if burst is not None:
        plume += np.asarray(fit_mask(burst, 114, -17).filter(ImageFilter.GaussianBlur(2.7))) * (.27 / 255)
    radius = np.hypot(x, y)
    edge = np.clip((.98 - radius) / .20, 0, 1)
    for frame in range(FRAMES):
        density = np.clip((plume - .11) * (1.32 - frame * .063) * (.48 + mottled) * edge, 0, 1)
        shade = density * .72 + mottled * .28
        alpha = np.power(density, .8) * 243
        yield Image.fromarray(np.stack((111 + 34 * shade, 12 + 10 * shade, 17 + 8 * shade, alpha), axis=-1)
                              .astype(np.uint8))


def pool_frames():
    y, x = (np.indices((SIZE, SIZE), dtype=float) + .5) * (2 / SIZE) - 1
    for frame in range(FRAMES):
        rng = np.random.default_rng(7130 + frame)
        angle = np.arctan2(y * 1.18, x)
        radius = np.hypot(x, y * 1.18)
        ripple = noise_field(rng, 17)
        rim = .57 + .04 * ripple + rng.uniform(-.025, .025)
        for frequency, width in ((2, .085), (3, .058), (5, .049), (9, .026)):
            rim += width * np.sin(angle * frequency + rng.uniform(0, 2 * np.pi))
        distance = rim - radius
        opacity = np.clip((distance + .015) / .055, 0, 1)
        shine = np.exp(-((distance - .028) * 32) ** 2) * np.maximum(0, .22 - y)
        shade = .42 * ripple + shine * .49 + .09 * noise_field(rng, 57)
        drops = np.asarray(fine_droplets(rng, 29, .215, .42), dtype=float) * .88
        alpha = np.maximum(opacity * (240 + 8 * ripple), drops)
        yield Image.fromarray(np.stack((84 + 43 * shade, 9 + 15 * shade, 13 + 15 * shade, alpha), axis=-1)
                              .astype(np.uint8))


def ribbon_frames():
    # Small moving highlights in the continuous stream. A transparent edge
    # gives it a soft silhouette even against pale floor textures.
    y, x = np.indices((SIZE, SIZE), dtype=float) / (SIZE - 1)
    for frame in range(FRAMES):
        phase = (x * 3 - frame / FRAMES) * (2 * np.pi)
        center = .50 + .037 * np.sin(phase) * x
        thick = (.38 - .30 * x) * (.91 + .09 * np.cos(phase))
        spread = np.clip(1 - np.abs(y - center) / thick, 0, 1)
        edge = np.clip(x * 49, 0, 1) * np.clip((1 - x) * 13, 0, 1)
        texture = .68 + .21 * np.sin(phase) + .11 * np.cos(phase * 2 + y * 10)
        coverage = 245 * np.power(spread, .63) * edge
        depth = 88 + 72 * texture * spread
        yield Image.fromarray(np.stack((depth, 7 + 12 * spread, 12 + 9 * spread, coverage), axis=-1).astype(np.uint8))


def drop_frames():
    y, x = (np.indices((SIZE, SIZE), dtype=float) + .5) * (2 / SIZE) - 1
    for frame in range(FRAMES):
        tilt = (frame - 3.5) * .066
        length = .89 - .024 * frame
        width = np.maximum(.20, .49 + .20 * x)
        outline = np.sqrt((x / length) ** 2 + ((y - x * tilt) / width) ** 2)
        shade = np.clip(1 - outline, 0, 1)
        glint = np.exp(-12 * ((x - .25) ** 2 + (y + .1) ** 2)) * shade
        alpha = np.clip((1 - outline) * 6, 0, 1) * 251
        yield Image.fromarray(np.stack((103 + 61 * shade + 50 * glint, 11 + 20 * shade, 15 + 19 * shade, alpha), axis=-1)
                              .astype(np.uint8))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--burst-mask", type=Path, help="TobiasM's CC0 blood_splat.png from OpenGameArt")
    parser.add_argument("--decal-masks", type=Path, help="OpenGameArt's CC0 blood_splatter_01_base.png")
    args = parser.parse_args()
    objects = {obj.path_id: obj for obj in UnityPy.load(str(args.bundle)).objects}
    burst = Image.open(args.burst_mask).convert("RGBA").getchannel("A") if args.burst_mask else None
    decals = []
    if args.decal_masks:
        sheet = Image.open(args.decal_masks).convert("RGBA").getchannel("A")
        w, h = sheet.width // 2, sheet.height // 2
        decals = [sheet.crop((x, y, x + w, y + h)) for y in (0, h) for x in (0, w)]
    args.output.mkdir(parents=True, exist_ok=True)
    for name, config in SPRAYS.items():
        atlas(spray_frames(objects, *config)).save(args.output / f"{name}.png")
    atlas(stain_frames(objects, burst, decals)).save(args.output / "blood-splat.png")
    atlas(ribbon_frames()).save(args.output / "blood-ribbon.png")
    atlas(mist_frames(burst)).save(args.output / "blood-mist.png")
    atlas(pool_frames()).save(args.output / "blood-pool.png")
    atlas(drop_frames()).save(args.output / "blood-drop.png")


if __name__ == "__main__":
    main()
