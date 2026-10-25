#!/usr/bin/env python3
# GPL-2.0-only. Makes the Starship NewGRF's game sprites from the handoff art. Needs Pillow.
#
#   python3 tools/make_sprites.py
#
# Rocket: the opaque core of each rocket image is cropped and scaled to game size. A rocket stands
# upright, so the same sprite serves all eight directions. Besides the parked rocket there are three
# flame frames for flight and three of flame and exhaust plume for launch and landing, drawn as the
# helicopter's rotor so the game animates them while the engine runs.
# Launch pad: the empty spaceport image is scaled to a 4x4 tile footprint and cut into one piece per
# tile of the launch site's layout. Every pixel goes to exactly one tile, and each piece is its tile's
# ground sprite (with concrete where the art does not reach), so nothing is drawn over anything else
# and the pieces rebuild the image whatever order the tiles are drawn in. In OpenTTD's view a tile's x axis runs down and to the left
# and its y axis down and to the right.
import json
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent.parent
ART = HERE.parent / "future_logistics" / "art" / "originals"
GFX = HERE / "gfx"

# Rocket body height at normal zoom, in pixels: standing on the launch mount, its top reaches the upper clamp
# arm of the tower (measured in game).
ROCKET_HEIGHT = 125
TILE_W, TILE_H = 64, 32  # A tile's ground diamond at normal zoom.
PAD_TILES = 4  # The art's footprint, in tiles each way.
# The launch site's layout, in tiles along x and y, and where the art's footprint starts in it. The offset puts
# the art's launch mount (measured in game: 0.6 and 11.9 sixteenths of a tile off the footprint's middle) at
# (44, 38.5) sixteenths, between the country airport's helicopter take-off (44, 37) and landing (44, 40) spots.
# The art's back edge then reaches a quarter of a tile past the layout's north-east side.
LAYOUT_TILES = (5, 4)
ART_OFFSET = (0.71, -0.24)
ROTOR_Z_OFFSET = 5  # A helicopter's rotor is drawn this many pixels above its body (aircraft.h).
# Where the lit rocket stands, relative to its position at the launch site's take-off spot, in pixels at normal
# zoom (measured in game with the airport's ground grid drawn over a screenshot): the clamp opening on top of
# the launch mount is this far right of and above the take-off spot. In flight the offset makes no difference.
MOUNT_OFFSET = (30, -12)


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


def body_width(core):
    """Median width of the solid part of the rocket's upper half, which is all body."""
    alpha = core.getchannel("A")
    widths = []
    for y in range(core.height // 8, core.height // 2):
        row = [x for x in range(core.width) if alpha.getpixel((x, y)) > 200]
        if row:
            widths.append(row[-1] - row[0] + 1)
    return sorted(widths)[len(widths) // 2]


def flame_start(core):
    """Row where the powered rocket's flame starts: the first row from the top that is mostly orange."""
    pixels = core.load()
    for y in range(core.height // 2, core.height):
        warm = sum(1 for x in range(core.width) if pixels[x, y][3] > 200 and pixels[x, y][0] > 200 and pixels[x, y][2] < 120)
        solid = sum(1 for x in range(core.width) if pixels[x, y][3] > 200)
        if solid and warm * 2 > solid:
            return y
    return core.height * 9 // 10


def save_rocket(info, name, sprite, zoom, base):
    """Save one rocket frame; base is the row of the rocket's foot, which sits on the vehicle's position."""
    sprite.save(GFX / f"{name}_{zoom}x.png")
    info[f"{name}_{zoom}x"] = {"width": sprite.width, "height": sprite.height, "xrel": -(sprite.width // 2), "yrel": -base}


def plume(width, height, frame, zoom):
    """An exhaust plume: soft grey puffs spreading below the flame. Different for each frame."""
    image = Image.new("RGBA", (width, height))
    draw = ImageDraw.Draw(image)
    rng = random.Random(frame)
    for _ in range(14):
        y = rng.uniform(0.15, 0.95) * height
        spread = (y / height) * width * 0.45
        x = width / 2 + rng.uniform(-spread, spread)
        r = rng.uniform(1.5, 3.5) * zoom * (ROCKET_HEIGHT / 44) * (0.6 + y / height)
        shade = rng.randint(190, 240)
        draw.ellipse((x - r, y - r * 0.7, x + r, y + r * 0.7), fill=(shade, shade, shade, rng.randint(150, 220)))
    return image.filter(ImageFilter.GaussianBlur(0.6 * zoom * ROCKET_HEIGHT / 44))


def rocket_sprites(info):
    idle = Image.open(ART / "rocket_idle.png").convert("RGBA")
    powered = Image.open(ART / "rocket_powered.png").convert("RGBA")
    idle_core, idle_box = opaque_crop(idle)
    powered_core, powered_box = opaque_crop(powered)
    # The two images are drawn at different scales, so each gets its own: the idle rocket is scaled to
    # ROCKET_HEIGHT, and the powered one so its body is as wide as the idle rocket's.
    scale = ROCKET_HEIGHT / idle_core.height
    powered_scale = scale * body_width(idle_core) / body_width(powered_core)
    for zoom in (1, 2):
        w = max(2, round(idle_core.width * scale * zoom))
        h = max(2, round(idle_core.height * scale * zoom))
        # The body: the rocket's foot is two pixels (at normal zoom) below the vehicle's position.
        body = drop_fringe(idle_core.resize((w, h), Image.LANCZOS))
        save_rocket(info, "rocket_idle", body, zoom, h - 2 * zoom)
        pw = max(2, round(powered_core.width * powered_scale * zoom))
        ph = max(2, round(powered_core.height * powered_scale * zoom))
        lit = drop_fringe(powered_core.resize((pw, ph), Image.LANCZOS))
        flame = lit.crop((0, round(flame_start(powered_core) * powered_scale * zoom), pw, ph))
        # The rocket with its engines lit is drawn as the helicopter's rotor, which the game draws ROTOR_Z_OFFSET
        # above the body and redraws every tick while the engine runs (the body is only redrawn when it moves).
        flames, plumes = [], []
        for frame, stretch in enumerate((0.8, 1.0, 1.25)):
            fh = max(1, round(flame.height * stretch))
            stretched = flame.resize((pw, fh), Image.LANCZOS)
            # Flight: the rocket over its flame, which flickers by changing length.
            flames.append(lit_rocket(body, stretched, None, zoom))
            # Launch and landing: the rocket over its flame and a spreading exhaust plume, which grows with the rocket.
            cloud_w, cloud_h = round(26 * zoom * ROCKET_HEIGHT / 44), round(18 * zoom * ROCKET_HEIGHT / 44)
            plumes.append(lit_rocket(body, stretched, plume(cloud_w, fh + cloud_h, frame, zoom), zoom))
        save_rotor_strip(info, "flame", flames, zoom)
        save_rotor_strip(info, "plume", plumes, zoom)


def lit_rocket(body, flame, cloud, zoom):
    """The rocket body over its flame, and an exhaust cloud under the flame if given, with rotor offsets."""
    width = max(body.width, flame.width, cloud.width if cloud else 0)
    foot = body.height - zoom
    height = max(foot + flame.height, foot + (cloud.height if cloud else 0))
    image = Image.new("RGBA", (width, height))
    if cloud:
        image.alpha_composite(cloud, ((width - cloud.width) // 2, foot))
    image.alpha_composite(flame, ((width - flame.width) // 2, foot))
    image.alpha_composite(body, ((width - body.width) // 2, 0))
    # The body's top is (body height - 2) pixels above the vehicle's position at normal zoom, moved to the launch
    # mount's clamp; the rotor is drawn ROTOR_Z_OFFSET pixels higher than the body, so its sprite starts that
    # much lower relative to it.
    return (image, -(width // 2) + MOUNT_OFFSET[0] * zoom,
            -(body.height - 2 * zoom) + MOUNT_OFFSET[1] * zoom + ROTOR_Z_OFFSET * zoom)


def save_rotor_strip(info, name, frames, zoom):
    """Save rotor frames side by side: a blank sprite for the stopped rotor (position 0), then one per turning position."""
    blank = (Image.new("RGBA", (1, 1)), 0, 0)
    frames = [blank] + frames
    strip = Image.new("RGBA", (sum(f[0].width + 1 for f in frames), max(f[0].height for f in frames)))
    boxes, x = [], 0
    for image, xrel, yrel in frames:
        strip.alpha_composite(image, (x, 0))
        boxes.append([x, 0, image.width, image.height, xrel, yrel])
        x += image.width + 1
    strip.save(GFX / f"{name}_strip_{zoom}x.png")
    info[f"{name}_strip_{zoom}x"] = boxes


CONCRETE = (126, 126, 122, 255)  # Fill for the parts of the layout the art does not reach.


def pad_tiles(info):
    art = Image.open(ART / "spaceport_empty.webp").convert("RGBA")
    art = art.crop(art.getchannel("A").getbbox())
    for zoom in (1, 2):
        tw, th = TILE_W * zoom, TILE_H * zoom
        width = tw * PAD_TILES
        height = round(art.height * width / art.width)
        image = drop_fringe(art.resize((width, height), Image.LANCZOS))
        r, g, b, a = image.split()
        image = Image.merge("RGBA", (r, g, b, a.point(lambda v: 255 if v >= 96 else 0)))
        # The art's footprint: its south corner is the image's bottom centre, so its north corner is width / 2 above.
        # The layout's north corner is ART_OFFSET tiles back from that along x (up and to the right on screen)
        # and y (up and to the left), snapped to whole pixels so every piece's offsets are exact.
        art_x, art_y = width / 2, height - th * PAD_TILES
        north_x = round(art_x + (ART_OFFSET[0] - ART_OFFSET[1]) * tw / 2)
        north_y = round(art_y - (ART_OFFSET[0] + ART_OFFSET[1]) * th / 2)
        # A canvas holding both the art and the layout's whole ground, so uncovered ground can be filled.
        lw, lh = LAYOUT_TILES
        left = min(0, north_x - lh * tw // 2)
        top = min(0, north_y)
        right = max(width, north_x + lw * tw // 2)
        bottom = max(height, north_y + (lw + lh) * th // 2)
        canvas = Image.new("RGBA", (right - left, bottom - top))
        canvas.alpha_composite(image, (-left, -top))
        north_x, north_y = north_x - left, north_y - top
        source = canvas.load()
        tiles = [(x, y) for x in range(lw) for y in range(lh)]
        pieces = {key: Image.new("RGBA", canvas.size) for key in tiles}
        targets = {key: piece.load() for key, piece in pieces.items()}
        for py in range(canvas.height):
            for px in range(canvas.width):
                # Ground position the pixel would have if it lay on the ground, in tiles from the layout's north corner.
                # u runs down and to the right (the y axis), v down and to the left (the x axis).
                sx, sy = (px + 0.5 - north_x) / (tw / 2), (py + 0.5 - north_y) / (th / 2)
                u, v = (sy + sx) / 2, (sy - sx) / 2
                on_ground = 0 <= v < lw and 0 <= u < lh
                if source[px, py][3] == 0 and not on_ground:
                    continue
                tx = min(lw - 1, max(0, int(v // 1)))
                ty = min(lh - 1, max(0, int(u // 1)))
                # Each piece is its tile's ground sprite: the art where there is art, concrete on the rest of the tile.
                targets[(tx, ty)][px, py] = source[px, py] if source[px, py][3] else CONCRETE
        for (x, y), piece in pieces.items():
            box = piece.getbbox()
            name = f"pad_{x}_{y}_{zoom}x"
            piece = piece.crop(box)
            piece.save(GFX / f"{name}.png")
            # Offsets from the tile's own north corner, which is (y - x) * tw / 2 across and (x + y) * th / 2 down
            # from the layout's north corner.
            tile_x = north_x + (y - x) * tw // 2
            tile_y = north_y + (x + y) * th // 2
            info[name] = {"width": piece.width, "height": piece.height, "xrel": box[0] - tile_x, "yrel": box[1] - tile_y}


def main():
    GFX.mkdir(exist_ok=True)
    info = {}
    rocket_sprites(info)
    pad_tiles(info)
    (GFX / "sprites.json").write_text(json.dumps(info, indent=1, sort_keys=True) + "\n")
    print(f"Wrote {len(info)} sprites to {GFX}")


if __name__ == "__main__":
    main()
