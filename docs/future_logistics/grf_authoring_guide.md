# Writing Future Logistics NewGRFs: a guide for authors (human or AI)

This is a self-contained brief for whoever writes the next Future Logistics NewGRF package. It explains
what to build, the rules it must follow, how to build and test it, and the traps already found. Read it
with [`tech_format.md`](tech_format.md), the specification itself; this guide does not repeat it, but
points into it.

The working example to copy is [`mods/newgrf/starship/`](../../mods/newgrf/starship/README.md): a complete
package in the format, with graphics converted from art, launch and landing animation, balance, and
integration tests.

## 1. What "plug and play" means here

A NewGRF that follows `tech_format.md` is picked up by the game with **no change to the game's code**:
the day-zero rivals build services with its vehicles and infrastructure, and the rivals that keep
running (CorporateAI) adopt it when it arrives. This was shown with drones, hyperloop, rockets and
Starship: none needed code.

It holds as long as the content can be built by an existing **mechanic** and serves an existing
**market**:

| Mechanic | Builds | Vehicles |
|---|---|---|
| `rail_line` | two terminus stations, a line, a depot, one train | trains (any track type) |
| `road_route` | two stops, a road, a depot, a small fleet | road vehicles |
| `airport_pair` | two airports and a small fleet | aircraft, helicopters |
| `dock_pair` | two docks and a small fleet | ships |

Markets: `town_pair` (passengers), `mail`, `industry` (freight, including two-leg transfers), `feeder`.

If the idea needs a new kind of construction (rooftop pads, an underground line, a launch tower that
is not an airport) or a new kind of market (a revenue bonus, a service with no cargo), it needs code.
Do not try to work around that in the NewGRF: write down what is needed and hand it back.

Rivals only take up a family if one of their **adoption rules** matches. The built-in rules
(`tech_format.md`, "Rival company profiles"):

| Rival | Adopts new families automatically |
|---|---|
| National Rail & Freight | any rail family, 5 years after it arrives |
| AeroLink | any air family, 2 years after it arrives |
| Metro & Regional | any road passenger family, 3 years after |
| Integrated Logistics | any road freight family after 2 years, any rail freight family after 5 |

Anything else (a new ship family, a faster adoption, freight by air) needs a rule in your NewGRF,
for example `fl_profile/integrated_logistics/adopt/rocket/airport_pair/industry/3`. That is a
declaration, not code.

## 2. Package layout

One directory per package under `mods/newgrf/`:

```text
mods/newgrf/<package>/
  <package>.nml          the NewGRF source
  <package>.grf          the compiled NewGRF, committed (the test suite checks it matches the source)
  lang/english.lng       strings
  gfx/                   game-ready sprites (PNG), generated
  tools/make_sprites.py  makes gfx/ from the art, if the art needs converting
  build.sh               "${NMLC:-nmlc}" -c -o <package>.grf <package>.nml
  README.md              what it adds, how it fits the format, graphics, limits, how it was tested
  PLACEHOLDERS.md        every number not yet decided, and every decided one with its reason
  .gitignore             .nmlcache/
```

Concept art from the Corporate Infrastructure handoff is in
`mods/newgrf/future_logistics/art/originals/` (1254×1254 images) and
`mods/newgrf/corporate_infrastructure/originals/` (transparent PNGs). The handoff brief is
`mods/newgrf/future_logistics/art/Corporate_Infrastructure_NewGRF_Handoff.md`.

### Identifiers in use

| NewGRF | GRF ID | Notes |
|---|---|---|
| Future Logistics | `FL\01\01` | drones, hyperloop, rockets, spaceport, data centre |
| Starship | `FL\01\02` | Starship and its launch site |
| Corporate Infrastructure | `CI\01\01` | Gareth's decorative objects |

Use the next free `FL\01\xx` for a new Future Logistics package. Item IDs are per NewGRF, so they may
repeat between packages. `fl_id` names must be unique across all loaded NewGRFs within a kind
(`validate` reports clashes): prefix them with the package's subject (`starship`, `starship_launch_site`).

## 3. The rules (summary of `tech_format.md`)

Every item that takes part carries:

- `fl_format/2`, or none of its `fl_` tags count.
- `fl_tech/<family>`: the family it belongs to. Untagged items are the `conventional` family.
- `fl_id/<name>`: a stable name, so code and the console can find it.
- For vehicles: at least one shared `role/` badge (`role/passenger`, `role/mail`, `role/freight`,
  `role/express`...) and a `power/` badge (`power/electric`, `power/rocket`...). Add `fl_role/<role>`
  only where the inferred roles are wrong or too broad; declared roles beat faster undeclared vehicles.
- `fl_needs/<site>` on a vehicle and `fl_provides/<site>` on the track type or airport that serves it,
  when the vehicle only works there (launch pads, drone pads).

Declared once per NewGRF, in the `badgetable` (not attached to items):

- `fl_family/<family>`, with `.../mechanic/<mechanic>`, `.../market/<market>` (one or more) and
  `.../min_tier/<village|town|city|metropolis>`. Declare the family even if another package also
  declares it, so the package works on its own; the declarations merge.
- Any adoption rules (`fl_profile/<profile>/adopt/<family>/<mechanic>/<market>/<years>`).
- A display name for the family with `item(FEAT_BADGES, ...) { property { label: "fl_family/..."; name: ...; } }`.
  Never give `fl_` classes a name or sprite, so players never see them.

Follow the community conventions: registered cargo labels where they fit (`O2__` for liquid oxygen;
never reuse `FUEL`, which is natural gas), the right cargo classes, the railtype and roadtype label
schemes for track that other sets could share, standard properties for speed, capacity and cost.

## 4. Building

- NML **0.9.0** (`python3 -m pip install nml==0.9.0`); NML from its GitHub master is only needed for
  Iron Horse. `./build.sh` in the package; set `NMLC` to use a particular `nmlc`.
- 32-bit sprites need `BIT_DEPTH_32BPP` in every `spriteset` and `alternative_sprites`, and are
  given at normal zoom with a 2× alternative (`ZOOM_LEVEL_IN_2X`).
- `spriteset` entries are separated by new lines, **not commas**.
- A vehicle's sprite set has **8 sprites**, one per direction.
- Builds are reproducible: the same source gives the same `.grf`. Commit the `.grf` after building.

## 5. Traps already found

Each of these cost a debugging session. They are all NewGRF-side, not code.

**Airports**

- The first NewGRF airport based on a built-in airport (`override: <type>`) **replaces** that airport
  for players. Define an unchanged copy first (`item(FEAT_AIRPORTS, x_unchanged, 0) { property { override: <type>; } }`),
  then your airport. If another loaded package already copies the same type, skip yours with
  `if (!grf_future_status("<GRFID>")) { ... }`, or players see it twice. Current copies: Future
  Logistics copies the city airport (1) and helistation (8); Starship copies the country airport (0).
- An airport's **movement** (where aircraft taxi, park, take off and land) comes from the built-in
  airport it overrides; the layout only sets the tiles and their graphics. To line art up with
  movement, find the positions in `src/table/airport_movement.h` (in sixteenths of a tile from the
  airport's north tile). Helicopter take-off and landing spots: country (44,37)/(44,40) — almost the same
  spot, used by Starship; city (44,63)/(28,74); helistation pads (40,8), (56,8), (56,24).
- Airports that only take helicopters (no planes) give routes of 12–64 tiles and count as the
  smallest class; airports that take planes give 48–256 tiles. The planner needs a **hangar**. Fast
  aircraft are not used at short-strip airports; helicopters are not "fast".
- Airport tile IDs in a `tilelayout` are the built-in `APT_*` numbers from `src/table/airporttile_ids.h`
  (counted from 0), or your own `FEAT_AIRPORTTILES` items.

**Isometric art on tiles**

- On screen a tile's **x axis runs down and to the left**, its **y axis down and to the right**:
  a tile (x, y)'s north corner is `((y - x) * 32, (x + y) * 16)` pixels from the layout's north corner at
  normal zoom. Swapping them mirrors and scatters the pieces (the first launch site had this bug).
- To put one big picture over several tiles, cut it into one piece per tile, assigning each pixel to
  exactly one tile, and draw each piece as that tile's **ground sprite**. Drawing pieces as child sprites
  over a concrete ground lets later tiles' ground draw over earlier tiles' art, leaving seams; rounding
  offsets per piece leaves 1-pixel gaps. Snap the layout's corner to a whole pixel.
  `mods/newgrf/starship/tools/make_sprites.py` does all of this; reuse it.

**Aircraft graphics**

- `flight_state` (var 0xE2) returns the **TTDPatch** movement states, not the game's own: 15 climbing,
  16–21 flying, 23 helicopter taking off at an airport, 25 helicopter landing at an airport,
  26/28 the same at a heliport, 0–12 on the ground. Full list: `TTDPAircraftMovementStates` in
  `src/newgrf_engine.cpp`.
- `current_speed` for aircraft is in units of about 12.8 km/h.
- An aircraft's body sprite is only redrawn when it **moves**. A helicopter's **rotor** is redrawn every
  tick while its engine runs, and is resolved through `livery_override(<self>) { ... }` with sprite
  `first + rotor position` (0 stopped, 1–3 turning). Starship draws its whole lit rocket as the rotor and
  leaves the body blank on the map, so it can appear, animate and vanish exactly with its engines.
- The low byte of `extra_callback_info1` is how the sprite is being drawn: 0 on the map; other values
  for the depot, vehicle windows and purchase list. Use it to keep something visible in windows while
  hiding or changing it on the map.
- A helicopter-type aircraft takes off and lands vertically: the way to get vertical launches without
  engine work.

**Numbers**

- `cost_factor` and `running_cost_factor` are single bytes (maximum 255); `mail_capacity` too. Starship
  sits at both cost maximums, so nothing can out-cost it through its own properties.

## 6. Testing

1. **Validate**: start a game with the NewGRF and run `future_logistics_tech validate` in the console
   (expects `problems=0`), `future_logistics_tech find <fl_id>` for each id, and
   `future_logistics_tech recipes` to see the families and each rival's plan.
2. **Integration scenarios**: add to `tests/future_logistics/integration.py`, after the Starship block:
   - a check that the committed `.grf` matches its source (copy the Starship one);
   - a world in a year after your content arrives, with `grfs=[your_grf]`, requiring `validate`
     to be clean and the right rival's recipe for your family to succeed;
   - a service run from that save (`service=True`), requiring your vehicles to serve their stations
     at the **last** check (`text.split("FL check result")[-2]`: the first check runs before anything moves);
   - a world with your NewGRF and the Future Logistics NewGRF together, if they share families or
     airport types.
   Run the suite: `python3 tests/future_logistics/integration.py --binary <openttd> --baseset <OpenGFX dir>`
   (set `NMLC` to check the `.grf`). All scenarios must pass.
3. **Look at it**: graphics cannot be judged from logs. Run the game with the 32-bit blitter and a dummy
   display (`SDL_VIDEODRIVER=dummy openttd -b 32bpp-anim -v sdl ...`), use
   `future_logistics_tech view <fl_id> [moving|parked|launching]` to centre the view on one of your
   vehicles, then `screenshot normal size 640 400 <name>`. Screenshots are taken on the next frame, so
   take one view per scheduled script (`schedule on-next-calendar-month <script>`), and start the script
   with `script con.txt` to log the console. Check every new sprite in a screenshot before calling it
   done.
4. **Balance**: `future_logistics_tech list` shows every vehicle's capacity, speed, cost and running cost;
   compare yours with what is already there and record decisions in `PLACEHOLDERS.md`. Profits need a
   service run of a simulated year (the `FL check` lines give each vehicle's profit).

Record what you ran and what you saw in [`validation.md`](validation.md).

## 7. Shipping with the game

- Add the `.grf` to `mods/newgrf/CMakeLists.txt` so it is copied to the build's `newgrf` folder and
  installed with the game.
- To have new games use it by default, add its file name to `FUTURE_LOGISTICS_DEFAULT_GRFS` in
  `src/settings.cpp` (this is code: only for packages meant to be on for every player).

## 8. Checklist

- [ ] Content fits an existing mechanic and market; anything that does not is written down, not hacked.
- [ ] Family declared (mechanic, markets, min tier, name); rivals have a matching adoption rule.
- [ ] Every item: `fl_format/2`, `fl_tech`, unique `fl_id`; vehicles: `role/` and `power/` badges.
- [ ] `fl_needs` on vehicles matched by `fl_provides` on infrastructure.
- [ ] Airport copies so no built-in airport disappears or appears twice.
- [ ] Sprites made by a script from the art; checked in screenshots, at normal and 2× zoom.
- [ ] `validate` clean, alone and with the Future Logistics NewGRF.
- [ ] Integration scenarios added and the whole suite passing.
- [ ] README (contents, format fit, graphics, limits, tests) and PLACEHOLDERS written.
- [ ] `.grf` rebuilt and committed; `validation.md` updated.

## 9. A starting brief for another model

> You are writing a NewGRF package for the Future Logistics fork of OpenTTD (repository
> BlacklionXYZ2/openttd-fork). Read `docs/future_logistics/grf_authoring_guide.md` and
> `docs/future_logistics/tech_format.md` first, then study `mods/newgrf/starship/` as the worked
> example. Build <subject> as a new package in `mods/newgrf/<package>/` that follows the format so the
> rival companies adopt it with no change to the game's code. Use the art in
> `mods/newgrf/future_logistics/art/originals/` where it exists. Every number you choose is a placeholder
> unless the brief gives it: record them in PLACEHOLDERS.md. If the content needs a new mechanic or
> market, stop and describe what is needed instead of working around it. Finish with the checklist in
> section 8 of the guide: the integration suite must pass and every sprite must be checked in a
> screenshot.
