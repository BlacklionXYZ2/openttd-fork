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
| Starship Launch Site | airport (5×4) | The launch complex from the handoff art: launch tower and mount, hangar and propellant tanks |

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

The launch site's movement comes from the country airport. The first NewGRF airport based on a
built-in airport replaces it, so the package defines an unchanged copy of the country airport
before the launch site, and players keep the country airport (the Future Logistics NewGRF copies
the city airport and helistation, not this one).

## Graphics and animation

Made from the Corporate Infrastructure handoff art (`../future_logistics/art/originals/`) by
[`tools/make_sprites.py`](tools/make_sprites.py), with normal and 2× zoom 32-bit sprites.

- **Launch and landing.** Starship is a helicopter-type aircraft, so it rises straight up from the
  launch mount and comes straight back down onto it; landing is the launch in reverse. The launch
  site's movement is the country airport's, whose helicopters take off at (44, 37) and land at
  (44, 40) in sixteenths of a tile, and the art is placed so its launch mount is there.
- **Seen only on the pad and in the air.** On the map the rocket is drawn as the helicopter's
  rotor, which the game redraws every tick while the engine runs; the body sprite is left blank,
  since the game only redraws it when the vehicle moves. So Starship appears standing in the
  mount's clamp when its engines light for take-off, climbs away, and on landing comes down onto
  the mount and is gone once its engines stop: it never drifts between the mount, the stands and
  the hangar. Vehicle windows, the depot and the purchase list show the rocket as usual.
- **Size and flame.** The rocket is the solid part of `rocket_idle.png`, 125 pixels tall at normal
  zoom, so standing in the clamp its nose reaches the tower's upper clamp arm (measured in a game,
  with the airport's ground grid drawn over a screenshot). One sprite serves all eight directions.
  Under it is the flame from `rocket_powered.png` (scaled so its rocket is as wide as the idle one;
  the two images are drawn at different scales), over an exhaust plume drawn by the tool while
  launching and landing. Three frames cycle with the rotor; which set depends on the flight phase
  (`flight_state`).
- **Launch site.** `spaceport_empty.webp`, scaled to a 4×4 footprint, placed over the 5×4 layout and
  cut into one piece per tile. Each piece is its tile's ground sprite, with concrete where the art
  does not reach. Every pixel belongs to exactly one piece, so the pieces rebuild the picture
  whatever order the tiles are drawn in, with no seams. The hangar is the country airport's, at
  tile (3, 0), beside the art's hangar building.

Known limits: Starship's take-off and landing spots are 3/16 of a tile apart, so it lands a
little in front of the clamp; the art's back edge reaches a quarter of a tile past the layout's north-east side;
the tower's upper pieces belong to a tile far below them, so they can be clipped when only part of
the screen is redrawn; and the rocket has no separate shadow sprite.

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
