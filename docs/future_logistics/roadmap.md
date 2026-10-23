# Day-zero roadmap

**Vision:** you start as a newcomer in a world where established transport
companies already run real networks. Over the decades new technology arrives
from the Future Logistics NewGRF (drones, then hyperloop, then rockets), and the
rivals adopt it too, so progression is a race rather than an unopposed build.

**Status (October 2026):** phases 0–5 are complete. The four rivals have
distinct profiles, and the starting market has an airline, passenger and
freight trains, feeder buses into railway stations, and trucks that continue
rail cargo chains, all scaled to the size of the world. NewGRFs can add whole
tech families, rivals and cargo meanings through badges, with no code change.
See `validation.md` for the recorded test results.

The key design rules for everything below: **rivals ask the Tech Library for
"the best tech for this job", never for a specific vehicle**, and **what rivals
build comes from recipes and adoption rules, never from code that names a
technology.** The shared format is described in
[`tech_format.md`](tech_format.md).

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

- Profiles list the services each rival tries, in order (since phase 5, as
  adoption rules in `src/future_logistics/catalogue.cpp`). A rival that builds
  nothing gets a bus as a fallback.

  | Rival | Services |
  |---|---|
  | National Rail & Freight | freight railway, passenger railway |
  | AeroLink | passenger airline (phase 3) |
  | Metro & Regional | passenger railway, feeder bus (phase 4) |
  | Integrated Logistics | freight railway, trucks (phase 4) |

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

## Headstart scaling (done, applies to every phase)

The headstart stays in proportion to the world. The thresholds are one table in
`src/future_logistics/scale.cpp`, ready for balancing.

- **Capital:** each rival's grant is the world's total population × the
  `bootstrap_capital_per_head` setting (default 500), shared between the
  rivals and capped by `bootstrap_capital` (default 25,000,000).
- **Services:** each route is classed by its smaller town's population, or for
  freight by the source industry's production rate:

  | Tier | Smallest town | Allows | Vehicles per road or air service |
  |---|---|---|---|
  | Village | under 500 | buses only (freight trains on 2-tile platforms) | 1 |
  | Town | 500 | short trains on the first usable track type, small airports | 2 |
  | City | 1,500 | 3-tile trains, first two usable track types, large airports | 3 |
  | Metropolis | 4,000 | 4-tile trains, any track type, hub airports | 4 |

- **Route count:** each rival works through its profile's list once, plus once
  more per 50,000 inhabitants, up to three times. Every round reserves its own
  towns and industries.

- Results in testing: a 48-town world of 34,000 people gives about 4.25
  million per rival; two towns of about 1,000 people each give about 250,000
  and at most a bus route. In 2026 a pair of cities gets a large airport
  rather than an intercontinental one, and town railways use electrified
  track rather than maglev.

## Phase 4: trucks and multimodal (A3) (done)

- Road planner (`src/future_logistics/road_planner.cpp`) now builds buses,
  feeder buses and trucks with one shared road builder, and a fleet sized by
  the market tier.
- **Feeder buses:** the first bus stop is built next to one of the company's
  railway stations and joins it, so the two form one interchange. The other
  end is an unused town 16–64 tiles away.
- **Trucks:** from a producing industry to an industry or a town that accepts
  the cargo, 12–96 tiles away, full-loading at the source. Secondary
  industries (factories and so on) are only used as sources when another
  freight service already supplies them, and those cargo chains are preferred.
  In testing: grain by rail to a factory, then goods by truck into a town.
- Larger worlds get more routes (see headstart scaling above).
- Towns and industries reserved by each service stay off-limits to later
  rivals; with four rivals and one round, most of a normal map stays free for
  the player.

Known limits: road vehicles share a single-lane dead-end road with turning at
the stops, which limits fleet size; no trams.

## Phase 5: modular tech trees (done)

Goal: a NewGRF can add an entirely new tech tree, new industries and new rivals,
and the day-zero rivals adopt them correctly, without code changes. This
replaced the original "read GRF tags" phase and goes further.

- **Format v2** ([`tech_format.md`](tech_format.md)): tags on items
  (`fl_tech`, `fl_role`, `fl_needs`, `fl_provides`, `fl_industry`, `fl_market`
  and others) plus declarations (`fl_family`, `fl_profile`, `fl_cargo`). Every
  part is an OpenTTD badge, so the content is an ordinary NewGRF.
- **Tech Library** reads the tags on vehicles, track types, road types and
  airports, and cargo meanings. Queries now filter by family and by the sites
  a vehicle needs.
- **Catalogue** (`src/future_logistics/catalogue.cpp`) reads families and
  profiles from declarations, fills in defaults from the loaded vehicles, and
  builds **recipes** (family, mechanic, market).
- **Mechanics:** the existing rail, road and air planners, now told which
  family to use. They only build on infrastructure that is untagged or tagged
  with that family, and pick vehicles whose needs the site provides.
- **Profiles:** the four rivals are now written in the same declaration format,
  as adoption rules, and parsed by the same code. NewGRF rivals come after them
  (raise the competitor count to include them), and a NewGRF can add rules to
  the built-in rivals. Freight services prefer the rival's preferred industry
  markets.
- **Console:** `future_logistics_tech badges`, `recipes` and `validate`.
- **Proof:** `tests/future_logistics/grf/fl_test.nml` adds a "testjet" family,
  a TestCorp rival, a rule for AeroLink, a cargo meaning and six deliberate
  mistakes. The integration test checks that TestCorp builds with the new
  family, AeroLink adopts it, the cargo meaning applies, and `validate` reports
  exactly the six mistakes.

Completed afterwards in the same phase:

- **Mail** market for rail, road (mail trucks at truck stops), air and water.
- **Freight by air** (`airport_pair/industry`), with airports placed so their
  catchment reaches the industries.
- **Ships** (`dock_pair` mechanic): docks on sloped coast facing open water, a
  check that both docks share the same water, a ship depot, and a fleet.
  Industries with their own dock, such as oil rigs, are used as they are.
- **Mechanic registry** (`src/future_logistics/mechanics.cpp`): NewGRFs name
  mechanics; code adds one with a planner and one table row.
- **`fl_id` and lookups** (`src/future_logistics/tag_index.h`): code finds a
  particular NewGRF item by a stable name; `future_logistics_tech find`.
- Built-in rivals gained rules for mail trains, mail trucks, air mail,
  ferries, freight ships and air freight, after their existing rules.
- **One freight planner with transfers** (`src/future_logistics/freight_planner.cpp`):
  each freight mechanic now builds a *leg* (deliver, or transfer and leave empty),
  and the planner picks the cargo, source, destination and legs. A route goes
  direct where one mechanic reaches; otherwise it hands over at a joined station
  to another freight mechanic the rival has adopted. Seen working: ship then
  train (oil rig → port → refinery), train then ship (oil field → port →
  refinery), ship then truck. Secondary industries are only used as sources
  once supplied, for every mode, which also stopped a railway waiting forever
  at an unsupplied steel mill.

Limits:

- Any new kind of construction (rooftop pads, launch sequences) needs a new
  mechanic in code.
- In the base game there are no mail or freight aircraft, so air mail and air
  freight only happen with NewGRF aircraft.
- Freight ships need industries near usable coast; in the test maps none was,
  so direct freight shipping has not yet been seen to succeed. Ferries and
  routes with a ship leg have.
- Routes have at most two legs, one transfer.
- Cargo types cannot carry badges, so their meaning is declared by label.

## Phase 6: rivals that keep up

Goal: after day zero, rivals renew and grow modestly, and adopt new tech.

- Recommended design (option A): give scripts access to the Tech Library and
  catalogue through a script API, so `CorporateAI` asks the same recipes and
  adoption rules that day zero uses. This makes `CorporateAI` fork-only, which
  is fine.
- `CorporateAI`: replace old vehicles, add vehicles to busy routes, open a small
  number of new routes per year, and adopt new families as their adoption
  rules allow.
- Aggression and expansion settings (low/medium/high).

## Phase 7: future tech arrives

Each step needs the matching graphics milestone first.

Details of each tech and industry will be decided when it is developed. With
the phase 5 format, most of each one is NewGRF content plus declarations; code
is only needed where a new mechanic or market is.

| Tech | Year | GRF provides | Code needed |
|---|---|---|---|
| Parcels | TBD | Parcel cargo, sorting hub, delivery van, `fl_cargo` meaning | None expected (road and rail freight recipes) |
| Drones | 2022 | Drone aircraft (`fl_needs/drone_pad`), drone pad airport | None expected (airport pair mechanic, mail or industry market) |
| Hyperloop | 2035 | Hyperloop rail type and pods, tagged as a family | None expected (rail line mechanic) |
| Rockets | 2050 | Rocket aircraft (`fl_needs/launchpad`), launchpad airport | None expected; a launch sequence would need a new mechanic |
| Data centres | TBD | Industry, cargo types, `fl_industry` and `fl_market` tags | Depends on what carries their cargo |

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

1. Make the vehicles, infrastructure and industries in the NewGRF with correct
   normal properties.
2. Tag them and declare the family (and any new rivals or cargo meanings) per
   `tech_format.md`.
3. Load the NewGRF in a new game and run `future_logistics_tech validate`, then
   `future_logistics_tech recipes` to see each rival's plan.
4. Code only changes if the tech needs a mechanic or market that does not exist
   yet; otherwise rivals adopt it on their own once its introduction date and
   their adoption delay have passed.
