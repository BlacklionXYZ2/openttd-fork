#!/usr/bin/env python3
# GPL-2.0-only. Makes the Starship NewGRF's game sprites from the handoff art. Needs Pillow.
#
#   python3 tools/make_sprites.py
#
# Rocket: the opaque core of each rocket image is cropped and scaled to game size. A rocket stands
# upright, so the same sprite serves all eight directions.
# Launch pad: the empty spaceport image is scaled to a 4x4 tile footprint and cut into one piece per
# tile. Every pixel goes to exactly one tile, so the pieces drawn as ground sprites rebuild the image
# whatever order the tiles are drawn in.
import json
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent.parent
ART = HERE.parent / "future_logistics" / "art" / "originals"
GFX = HERE / "gfx"

ROCKET_HEIGHT = 44  # Rocket body height at normal zoom, in pixels.
TILE_W, TILE_H = 64, 32  # A tile's ground diamond at normal zoom.
PAD_TILES = 4


def opaque_crop(image, threshold=200, alpha_floor=60):
    """Crop to the solid part of an image and drop faint glow below alpha_floor."""
    alpha = image.getchannel("A")
    box = alpha.point(lambda a: 255 if a > threshold else 0).getbbox()
    cropped = image.crop(box)
    r, g, b, a = cropped.split()
    a = a.point(lambda v: 0 if v < alpha_floor else v)
    return Image.merge("RGBA", (r, g, b, a)), box


def drop_fringe(image, floor=24):
    """Clear the near-transparent fringe that resizing leaves around an image."""
    r, g, b, a = image.split()
    return Image.merge("RGBA", (r, g, b, a.point(lambda v: 0 if v < floor else v)))


def rocket_sprites(info):
    idle = Image.open(ART / "rocket_idle.png").convert("RGBA")
    powered = Image.open(ART / "rocket_powered.png").convert("RGBA")
    idle_core, idle_box = opaque_crop(idle)
    powered_core, powered_box = opaque_crop(powered)
    # One scale for both, from the idle rocket's body, so the powered rocket's flame adds length.
    scale = ROCKET_HEIGHT / idle_core.height
    for name, core in (("rocket_idle", idle_core), ("rocket_powered", powered_core)):
        for zoom in (1, 2):
            w = max(2, round(core.width * scale * zoom))
            h = max(2, round(core.height * scale * zoom))
            sprite = drop_fringe(core.resize((w, h), Image.LANCZOS))
            path = GFX / f"{name}_{zoom}x.png"
            sprite.save(path)
            # The rocket's base sits on the vehicle's position.
            info[f"{name}_{zoom}x"] = {"width": w, "height": h, "xrel": -(w // 2), "yrel": -h + 2 * zoom}


def pad_tiles(info):
    art = Image.open(ART / "spaceport_empty.webp").convert("RGBA")
    art = art.crop(art.getchannel("A").getbbox())
    for zoom in (1, 2):
        tw, th = TILE_W * zoom, TILE_H * zoom
        width = tw * PAD_TILES
        height = round(art.height * width / art.width)
        image = drop_fringe(art.resize((width, height), Image.LANCZOS))
        # The footprint's south corner is the image's bottom centre; its north corner is width / 2 above.
        north_x, north_y = width / 2, height - th * PAD_TILES
        pieces = {(x, y): Image.new("RGBA", (width, height)) for x in range(PAD_TILES) for y in range(PAD_TILES)}
        source = image.load()
        targets = {key: piece.load() for key, piece in pieces.items()}
        for py in range(height):
            for px in range(width):
                if source[px, py][3] == 0:
                    continue
                # Ground position the pixel would have if it lay on the ground, in tiles from the north corner.
                sx, sy = (px + 0.5 - north_x) / (tw / 2), (py + 0.5 - north_y) / (th / 2)
                u, v = (sy + sx) / 2, (sy - sx) / 2
                tx = min(PAD_TILES - 1, max(0, int(u // 1)))
                ty = min(PAD_TILES - 1, max(0, int(v // 1)))
                targets[(tx, ty)][px, py] = source[px, py]
        for (x, y), piece in pieces.items():
            box = piece.getbbox()
            name = f"pad_{x}_{y}_{zoom}x"
            if box is None:
                piece, box = Image.new("RGBA", (1, 1)), (0, 0, 1, 1)
            else:
                piece = piece.crop(box)
            piece.save(GFX / f"{name}.png")
            # Offsets from the tile's own north corner, which is (x - y) * tw / 2 across and (x + y) * th / 2 down
            # from the footprint's north corner.
            tile_x = north_x + (x - y) * tw / 2
            tile_y = north_y + (x + y) * th / 2
            info[name] = {"width": piece.width, "height": piece.height,
                          "xrel": round(box[0] - tile_x), "yrel": round(box[1] - tile_y)}


def main():
    GFX.mkdir(exist_ok=True)
    info = {}
    rocket_sprites(info)
    pad_tiles(info)
    (GFX / "sprites.json").write_text(json.dumps(info, indent=1, sort_keys=True) + "\n")
    print(f"Wrote {len(info)} sprites to {GFX}")


if __name__ == "__main__":
    main()
