# Future Logistics: Starship

A NewGRF with a fully reusable rocket, **Starship**, and its **Starship Launch Site**, for the
rocket age of Future Logistics. It follows the Future Logistics tech format
([`docs/future_logistics/tech_format.md`](../../../docs/future_logistics/tech_format.md)) and the
community badge conventions, so rival companies find and adopt it with no change to the game.

It works on its own, or alongside the Future Logistics NewGRF
([`../future_logistics/`](../future_logistics/README.md)), whose Reusable Rocket and Spaceport
belong to the same `rocket` family. Starship is balanced as the top of the transport world: the
most expensive vehicle to buy and to run, the largest capacity (500 passengers and 120 mail) and
the most profit; the other numbers are placeholders. See [`PLACEHOLDERS.md`](PLACEHOLDERS.md).

The game ships with this NewGRF and the Future Logistics NewGRF and uses both in new games by
default: they are installed in the game's `newgrf` folder, and added to a new game's NewGRF list
when the configuration does not have one yet (the first run). Players can remove them in the
NewGRF settings.

## Contents

| Item | Kind | What it is |
|---|---|---|
| Starship | aircraft (large) | 500 passengers and 120 mail, or express and armoured cargo, between launch sites at 2000 km/h, from 2045 |
| Starship Launch Site | airport (6×6) | The city airport's runway, taxiways and hangar, with the launch pad art on its northern 4×4 tiles |

## How it fits the tech format

| Badge | On | Why |
|---|---|---|
| `fl_format/2` | both | Makes the `fl_` tags count |
| `fl_tech/rocket` | both | Joins the shared rocket family |
| `fl_family/rocket` and its `mechanic`, `market` and `min_tier` declarations | the GRF | Declares the family, so the package works without the Future Logistics NewGRF: built with `airport_pair`, for passengers, mail and freight, in cities and up |
| `fl_needs/launchpad` / `fl_provides/launchpad` | Starship / Launch Site | Starship only flies from airports that provide a launch pad: this launch site, or the Future Logistics Spaceport |
| `fl_role/passenger_intercity`, `fl_role/express`, `fl_role/freight_general` | Starship | The jobs it is declared for |
| `fl_tier/4` | Starship | Its place in the family |
| `fl_id/starship`, `fl_id/starship_launch_site` | both | Stable names for code and the console (`future_logistics_tech find starship`) |
| `role/passenger`, `role/mail`, `role/freight`, `power/rocket` | Starship | The shared classes other NewGRFs, AIs and the purchase list filter read |

No rival rules are added: AeroLink's built-in rule for any new air family adopts the rocket
family two years after it arrives. Other NewGRFs can add rules with
`fl_profile/<profile>/adopt/rocket/...`.

The first NewGRF airport based on a built-in airport replaces it, so the package defines an
unchanged copy of the city airport before the launch site. The Future Logistics NewGRF does the
same; when it is also active, this package leaves its copy out, so players see one city airport
whichever order the two are loaded in.

## Graphics

Made from the Corporate Infrastructure handoff art
(`../future_logistics/art/originals/`) by [`tools/make_sprites.py`](tools/make_sprites.py), with
normal and 2× zoom 32-bit sprites:

- **Starship**: the solid part of `rocket_idle.png` and `rocket_powered.png`, scaled to 44 pixels
  tall at normal zoom. The rocket stands upright, so one sprite serves all eight directions. The
  powered sprite (with flame) is shown once the rocket moves fast enough to be airborne.
- **Launch pad**: `spaceport_empty.webp`, scaled to a 4×4 footprint and cut into one piece per tile.
  Every pixel goes to exactly one tile and each piece is drawn on concrete, so the pieces rebuild
  the picture whatever order the tiles are drawn in.

The sprites have not yet been checked by eye in a game with graphics. Known limits: tall parts of
the pad art are drawn by the northern tile, so they can be clipped when only part of the screen is
redrawn; the rocket has no separate shadow sprite; and Starship takes off and lands along the
runway like an aircraft, because a vertical launch needs engine work.

## Build

1. `python3 -m pip install nml==0.9.0`
2. `./build.sh` (set `NMLC` to use a particular `nmlc`).
3. To remake the sprites from the art: `python3 tools/make_sprites.py` (needs Pillow), then build.

The compiled `starship.grf` is committed; the integration suite fails if it no longer matches its
source.

## Tested

With the integration harness (`tests/future_logistics/integration.py`):

- On its own, in a 512×512 world in 2050, AeroLink opened Starship passenger and mail routes
  between launch sites on day zero, and every Starship served both of its launch sites over
  three months. `future_logistics_tech validate` reports no problems.
- With the Future Logistics NewGRF, `validate` reports no problems, there is one city airport
  whichever order the two are loaded in, and AeroLink chooses Starship over the Reusable Rocket.

## Use

In this fork it is already installed and on for new games. In other OpenTTD builds, put
`starship.grf` in `~/Documents/OpenTTD/newgrf/` and add it under NewGRF Settings before starting a
game. Starship arrives in 2045.
