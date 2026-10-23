# Day-zero roadmap

**Vision:** you start as a newcomer in a world where established transport
companies already run real networks. Over the decades new technology arrives
from the Future Logistics NewGRF (drones, then hyperloop, then rockets), and the
rivals adopt it too, so progression is a race rather than an unopposed build.

**Status (October 2026):** phases 0–3 are complete. The four rivals have
distinct profiles, and the starting market has an airline, passenger trains,
freight trains and buses. See `validation.md` for the recorded test results.

The key design rule for everything below: **rivals ask the Tech Library for
"the best tech for this job", never for a specific vehicle.** The library and
its shared format are described in [`tech_format.md`](tech_format.md). That is
what lets the GRF add technology without day-zero code changes.

## Phase 0: bus bootstrap (done)

- Four funded rivals, one operating bus route each, built before the player
  starts.
- Rollback on failure, single funding grant, save/load, reproducible seeds.
- `CorporateAI` keeps the starter network running; it does not expand.

## Phase 1: Tech Library core (done)

Goal: the existing bus planner chooses its bus through the library.

- `src/future_logistics/tech_library.{h,cpp}` builds tech records from all
  loaded engines, rail types, road types and airports, and infers roles for
  untagged vehicles as described in the format spec.
- The library is a snapshot built when needed (at bootstrap, or per console
  command), so it always matches the loaded game and is never saved.
- `FindBest` picks the usable vehicle with the most transport work (capacity ×
  speed × reliability) per yearly cost (running cost plus purchase price spread
  over its life). Usable means right mode and role, carries or refits to the
  cargo, and buildable by that company today. Ties go to the lowest engine
  index. Distance and range checks wait for the air planner (phase 3).
- The bus planner now asks for the best `passenger_feeder` on plain road. In a
  1950 start this is the same bus as before, because only one is available; in
  later starts it can now pick a better bus than the first one in the list.
- Console: `future_logistics_tech list` and
  `future_logistics_tech best <mode> <role> [<cargo label>]`.
- Tests: unit tests for role inference and scoring; integration checks that the
  planner and console agree, and a library snapshot per climate in
  `tests/future_logistics/tech_snapshots/`.

## Phase 2: company profiles and rail (A1) (done)

Goal: the four rivals differ, and trains run on day one.

- Profiles (`src/future_logistics/profiles.cpp`) list the services each rival
  tries, in order. A rival that builds nothing gets a bus as a fallback.

  | Rival | Services |
  |---|---|
  | National Rail & Freight | freight railway, passenger railway |
  | AeroLink | passenger airline (phase 3) |
  | Metro & Regional | passenger railway, bus |
  | Integrated Logistics | freight railway, trucks from phase 4 |

- Each rival is renamed after its profile, and its CorporateAI gets a
  `profile` setting. Both are saved with the game, and the AI logs its profile
  after a reload.
- Rail planner (`src/future_logistics/rail_planner.cpp`): two 3-tile terminus
  stations, a single line between them over flat land at one height, a depot
  behind the first station, and one train. The pathfinder never puts two
  same-direction turns next to each other, so lines have no forbidden 90
  degree bends.
- The locomotive (by pulling power) and the wagons come from the Tech Library;
  wagons are added until the train fills the platform. Every track type the
  company can build is considered, and the one with the fastest locomotive
  wins: plain rail in 1950, maglev by 2026 in the base game.
- Passenger railways link two unused towns 24–120 tiles apart. Freight railways
  link an unused producing industry to one 20–100 tiles away that accepts its
  cargo; the train full-loads at the source.
- `future_logistics_check` now checks trains too, following the track from the
  train to both of its stations.

Known limits, for later phases:

- Lines step around obstacles in short zig-zags, which slow trains down.
  Smoother routing would help.
- Single track with one train, so no signals yet. Busier lines need passing
  loops and signals.
- Flat land at one height only, as for buses: no slopes, bridges or tunnels.
- Freight trains make large profits in the first months (tested with a 25
  million starting grant). Balance is a phase 8 task.

## Phase 3: air (A2) (done)

- Air planner (`src/future_logistics/air_planner.cpp`): an airport in each of
  two unused towns 48–256 tiles apart, and one passenger aircraft between them.
- Airport types come from the Tech Library's infrastructure list. The largest
  available type that takes planes is tried first, falling back to smaller
  ones: small airports in 1950, intercontinental airports by 2026.
- Sites are flat, clear land at one height near the town centre, where the
  airport's catchment accepts passengers. Town authority and noise rules are
  enforced by the normal build command during preflight.
- The aircraft comes from the Tech Library. Fast aircraft are excluded from
  airports with a short strip (they could crash), and the aircraft's range
  must cover the route.
- `future_logistics_check` checks aircraft too: both ordered stations must be
  the owner's airports.

Known limits: one aircraft per airline, no helicopters or heliports, and no
mail.

## Phase 4: trucks and multimodal (A3)

- Truck freight between industries and towns, bus feeders into rail stations.
- Spread services so the market visibly contains air, rail passengers, rail
  freight, buses and trucks.
- Scale route counts to map size, and keep towns and industries free for the
  player.

## Phase 5: GRF tags (shared format live)

Goal: the library reads `fl_` badges; a tagged GRF vehicle is treated exactly
like its tags say.

- Read `fl_` badges into tech records; check `fl_format`.
- Pair `fl_needs/*` with `fl_provides/*`.
- `future_logistics_tech validate` reports missing or conflicting tags and
  vehicles that can never be used.
- Add a tiny test GRF under `tests/future_logistics/` (one tagged vehicle) and
  an integration scenario that proves the library picks it up.
- Graphics side: B1 (NML skeleton that compiles and loads) can happen in
  parallel and should carry real badges from the start.

## Phase 6: rivals that keep up

Goal: after day zero, rivals renew and grow modestly, and adopt new tech.

- Add a script API call so scripts can read badges, for example
  `AIEngine.HasBadge(engine, "fl_tech/drone")`. Today the script API cannot see
  badges at all. This makes `CorporateAI` fork-only, which is fine.
- `CorporateAI`: replace old vehicles, add vehicles to busy routes, open a small
  number of new routes per year, all through library queries.
- Adoption rules per profile: how many years after a tech's intro date to
  consider it, and whether `fl_ai/early_adopter` items are allowed.
- Aggression and expansion settings (low/medium/high).

## Phase 7: future tech arrives

Each step needs the matching graphics milestone first.

| Tech | Year | GRF provides | Day-zero work |
|---|---|---|---|
| Parcels | TBD | Parcel cargo, sorting hub, delivery van | Parcel role in profiles; cargo chain planner |
| Drones | 2022 | Drone aircraft (`fl_needs/drone_pad`), drone pad airport | Short-range parcel routes using drone pads |
| Hyperloop | 2035 | Hyperloop rail type, pods, terminals | Rail planner reused on the hyperloop track type |
| Rockets | 2050 | Rocket aircraft (`fl_needs/launchpad`), launchpad airport | Long-haul express routes between launchpads |

Underground hyperloop and true rocket flight behaviour need engine changes and
are tracked separately; the first versions use rail-type and aircraft mechanics.

## Phase 8: quality and release

- Settings for density, start year, rival count and aggression in the game's
  settings window.
- Balance passes: the player must be able to catch up.
- Intel x86_64 macOS 12 build (`mac-artifact-x86_64.yml`) run and play-tested on
  the target MacBook, plus Apple Silicon and Linux CI.
- Screenshots and validation notes updated for each phase.

## Adding new technology later (summary)

1. Make the vehicle in the GRF with correct normal properties.
2. Tag it with `fl_` badges per `tech_format.md`; tag its infrastructure.
3. Run `future_logistics_tech validate` in-game.
4. Only if it needs a new kind of route (a new site type or a new role) does
   day-zero code change; otherwise rivals start using it on their own once its
   intro date and their adoption delay have passed.
