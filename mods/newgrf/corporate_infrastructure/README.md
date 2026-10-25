# Corporate Infrastructure — NewGRF object package (v0.1)

This package converts the **seven unique user-created PNG graphics** into six
provisional OpenTTD **NewObjects** with normal and 2x-in zoom 32-bit sprites.
The large originals are retained in `originals/`, and the generated,
alpha-preserving sprite PNGs live in `gfx/`.

## Status

Brought into the Future Logistics repository on 24 October 2026 from Gareth's
v0.1 handoff zip, unchanged apart from the build script.

- **Compiles** with NML 0.9.0, with no errors or warnings, and the build is
  reproducible (the same source gives the same `.grf`). The compiled
  `CorporateInfrastructure.grf` is committed.
- **Loads and places in OpenTTD**: the `corporate-infrastructure` integration
  scenario loads it alongside the Future Logistics NewGRF, and a test Game Script
  (`tests/future_logistics/gs/CIObjectCheck`) builds all six objects. The suite
  fails if the committed `.grf` no longer matches its source.
- **Not yet checked by eye**: sprite position, depth sorting, transparency and
  the purchase-window preview have not been looked at in a game with graphics.
  Large objects use a single sprite on the northern tile of their 4×4 footprint,
  with concrete ground on the others; a release-ready pack would split each
  building into per-tile sprites.
- The objects are decorative. The working drones, drone ports, rockets,
  spaceports and data centres are in the Future Logistics NewGRF
  (`mods/newgrf/future_logistics/`), which uses base-game graphics for now; this
  art is meant to replace those graphics once converted.

## Build

1. `python3 -m pip install nml==0.9.0`
2. `./build.sh` (set `NMLC` to use a particular `nmlc`).
3. Optional asset check: `python3 tools/check_assets.py` (needs Pillow).

## Use

Put `CorporateInfrastructure.grf` in `~/Documents/OpenTTD/newgrf/`, add it under
NewGRF Settings before starting a game, and find the six items in the
landscaping toolbar under **Objects** > **Corporate Infrastructure**.

## Included 6 decorative game objects

| Object | Tiles | Current gameplay |
|---|---:|---|
| Data Centre | 4×4 | Decorative; no income bonus yet |
| Distribution Centre | 4×4 | Decorative; not a cargo industry/station yet |
| Four-Pad Drone Port | 4×4 | Four visible pads; not functional aircraft stands |
| Spaceport — Empty Pad | 4×4 | Decorative |
| Spaceport — Starship on Pad | 4×4 | Decorative, second state as separate object |
| Parked Cargo Drone | 1×1 | Static decorative object, no flight directions |

`originals/openttd_cargo_drone_transparent.png` preserves the large drone art
for a future 8-direction aircraft sprite sheet. `originals/openttd_cargo_drone_game_scale.png`
was used for the decorative parked version. Both spaceport PNGs are included.
No Hyperloop images were present in the supplied files; Hyperloop is **not** in this pack.

## Future work

- Test the sprite anchor in-game and then produce properly layered per-tile renders.
- Convert the distribution centre into industry and appropriate rail/road stops;
  add mail/goods cargo rules.
- Convert the data centre into an industry and add scripted company bonuses.
- Make the drone port and spaceport functional using aircraft/airport support and,
  where necessary, changes in the custom OpenTTD engine.
- Model rotating or 8-direction moving cargo drone sprites.
- Add Hyperloop after its route, terminal and vehicle sprites are produced.

## Technical reference

- NML compiler: https://github.com/OpenTTD/nml
- NML Objects: https://newgrf-specs.tt-wiki.net/wiki/NML:Objects
- NML alternative sprites: https://newgrf-specs.tt-wiki.net/wiki/NML:Alternative_sprites

