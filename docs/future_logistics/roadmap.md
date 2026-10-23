# Day-zero roadmap

**Vision:** you start as a newcomer in a world where established transport
companies already run real networks. Over the decades new technology arrives
from the Future Logistics NewGRF (drones, then hyperloop, then rockets), and the
rivals adopt it too, so progression is a race rather than an unopposed build.

**Status (October 2026):** phase 0 is complete. Each rival runs one bus line.
See `validation.md` for the recorded test results.

The key design rule for everything below: **rivals ask the Tech Library for
"the best tech for this job", never for a specific vehicle.** The library and
its shared format are described in [`tech_format.md`](tech_format.md). That is
what lets the GRF add technology without day-zero code changes.

## Phase 0: bus bootstrap (done)

- Four funded rivals, one operating bus route each, built before the player
  starts.
- Rollback on failure, single funding grant, save/load, reproducible seeds.
- `CorporateAI` keeps the starter network running; it does not expand.

## Phase 1: Tech Library core

Goal: the existing bus planner chooses its bus through the library, with
identical results.

- Add `src/future_logistics/tech_library.{h,cpp}`: build tech records from all
  loaded engines, rail types, road types and airports; infer roles for untagged
  items as described in the format spec.
- Rebuild the library on new game and on load; never save it.
- Query function: best record for mode + role + cargo + date + budget +
  distance, scored on capacity × speed against cost and reliability, filtered by
  normal buildability for that company.
- Replace the planner's own bus search with a library query.
- Console command `future_logistics_tech list` to print the library.
- Tests: library snapshot for each climate with no NewGRFs; the existing 16
  integration scenarios must still pass unchanged.

## Phase 2: company profiles and rail (A1)

Goal: the four rivals differ, and trains run on day one.

- Data-driven profiles: each lists modes, roles, budget share and adoption
  style. Start with the four from the handoff (National Rail & Freight,
  AeroLink, Metro & Regional, Integrated Logistics).
- Store each company's profile with the game, for example as a `CorporateAI`
  script setting chosen during bootstrap (check that per-company AI settings
  persist through save/load).
- Rail planner: station orientation, track, depot, signals for two-way or
  passing loops, then library-chosen engine and wagons.
- One passenger rail service and one freight rail service, with cargo checked at
  both ends.

## Phase 3: air (A2)

- Airport placement against town limits and noise, footprint and terrain checks.
- Aircraft chosen by the library, respecting airport size and order range.
- At least two airports with an operating passenger route.

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
