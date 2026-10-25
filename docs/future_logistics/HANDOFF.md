# Future Logistics: handoff

Last updated 24 October 2026. This page is the starting point for anyone
picking the project up: what it is for, the decisions behind it, where it
stands, and what comes next. The detail lives in the pages it links to.

| Page | What it covers |
|---|---|
| [`README.md`](README.md) | Building, configuration, how day zero runs, diagnostics |
| [`roadmap.md`](roadmap.md) | Every phase, what each delivered, and its limits |
| [`tech_format.md`](tech_format.md) | The `fl_` badge format NewGRFs use to add tech, industries and rivals |
| [`grf_authoring_guide.md`](grf_authoring_guide.md) | How to write a NewGRF package in the format: layout, build, traps, tests, a brief for another model |
| [`compatibility.md`](compatibility.md) | How third-party NewGRFs and the NewGRF specification fit |
| [`validation.md`](validation.md) | Recorded test results for every phase |
| [`mods/newgrf/future_logistics/`](../../mods/newgrf/future_logistics/README.md) | The Future Logistics NewGRF, its placeholder values and the handoff art |
| [`mods/newgrf/corporate_infrastructure/`](../../mods/newgrf/corporate_infrastructure/README.md) | Gareth's Corporate Infrastructure package: six decorative objects from the concept art |
| [`mods/newgrf/starship/`](../../mods/newgrf/starship/README.md) | Starship: a reusable rocket and launch site, a complete package in the tech format |

## Latest work (night of 23–24 October 2026)

Branch `feature/future-logistics-grf`, on top of `feature/rivals-keep-up` (#3).
Not pushed yet.

- The Future Logistics NewGRF exists, with drones (2022), hyperloop (2035),
  rockets and spaceports (2050) and a data centre industry. Values and
  graphics are placeholders (`PLACEHOLDERS.md`). Gareth's Corporate
  Infrastructure package (six decorative objects from the concept art) is in
  `mods/newgrf/corporate_infrastructure/`, compiled and placing in game; its
  art is not yet used for the working vehicles and buildings.
- Starship is a complete NewGRF package of its own (`mods/newgrf/starship/`):
  a reusable rocket (2045) and a launch site in the shared rocket family,
  with sprites converted from the handoff art. On its own, AeroLink opened
  Starship routes on day zero and every Starship served its launch sites;
  with the Future Logistics NewGRF too, both work and players see one city
  airport. It is balanced as the top vehicle: the most expensive to buy and
  run, the largest capacity and the most profit.
- The game now ships with its NewGRFs (`mods/newgrf/CMakeLists.txt`), and new
  games use Future Logistics and Starship by default when the configuration
  has no NewGRF list yet (`src/settings.cpp`).
- In a game started in 2021, the rivals adopted drones on their own in
  2024–25 through CorporateAI: the whole loop from new tech to rival adoption
  works.
- Road routes use existing town roads and drive-through stops on them;
  railways cross town roads with level crossings; rail and road orders are
  non-stop.
- The established-rivals settings are in the settings window (Computer
  players → Established rivals).
- A ten-year regression run with all of the above passed once rivals' vehicles
  left in depots after renewal were sold (`validation.md`).
- Railways and road routes now climb over slopes and bridge rivers, roads
  and railways up to six tiles across; road routes bridge railways in their way (a short bridge rather than a level crossing, which
  fast trains turned out to hit buses on). On mountainous worlds rivals build
  about twice as many railways as before.
- Waiting on decisions: what data centres receive (maps rarely start with
  banks), the cargo hub and parcels, and the data centre bonus (see the
  handoff's open decisions).

## What the project is

Future Logistics is a fork of OpenTTD (upstream OpenTTD, not JGR) with two parts:

1. **The day-zero mod** (in this repository). When a new single-player game
   starts, rival transport companies are created and given an established
   network before the player arrives: railways, airlines, bus and truck routes,
   ferries and freight chains, sized to the world. Afterwards the rivals keep
   running and growing their companies through the CorporateAI script.
2. **The Future Logistics NewGRF** (started, in `mods/newgrf/future_logistics/`). New technology that arrives
   over the decades: parcels, drones, hyperloop, rockets in the style of
   SpaceX's Falcon 9, Falcon Heavy and Starship, data centres and other new
   industries. It is ordinary NewGRF content plus `fl_` badges, so the rivals
   can find and adopt it.

The target machine is a 2015 Intel MacBook Pro on macOS 12 Monterey (x86_64).

## Goals

- **Start as the newcomer.** The player enters a market where rivals are
  already established, and has to compete with them.
- **A head start, not an unbeatable one.** What rivals get scales with the
  world: two small villages get a few buses and little money, not airports and
  millions. Villages, towns, cities and metropolises each allow a bigger class
  of service, fleet and infrastructure.
- **Progression is a race.** As new tech arrives, the rivals adopt it on their
  own schedules, so the player is racing them to it.
- **New tech is content, not code.** Adding a vehicle, a whole tech tree, a new
  industry or a new rival company should need a NewGRF, not a change to the game.
- **Work with everyone else's content.** Third-party vehicle, track and industry
  sets should just work, and Future Logistics content should work with them.

## Decisions and the reasons for them

These were settled with the project owner during development. Changing one
affects much of the rest.

| Decision | Why |
|---|---|
| Build on OpenTTD's own NewGRF system and its badges, not a custom import or hook system | It already exists, other content uses it, and a NewGRF using badges still loads in normal OpenTTD |
| One shared format (`fl_` badges, `tech_format.md`) read by both the day-zero code and the NewGRF | The NewGRF only has to describe its content once, and the code never names a specific vehicle or industry |
| Modular tech trees: families, rival profiles, adoption rules, cargo meanings and industry markets are declared by NewGRFs | So new tech trees (rockets, data centres) can be added as wholes, not just new vehicles in existing trees |
| Code is only needed for a new kind of construction (a "mechanic") or a new kind of market | Everything else is data; the registry in `mechanics.cpp` links names in NewGRFs to the code |
| Follow the community conventions (`role/`, `power/` badges, the cargo label registry, track label schemes) and keep `fl_` only for what they cannot say | Compatibility with community content in both directions |
| Freight transfers use the base game's own transfers; one freight planner chooses cargo, source, destination and legs | Nothing new needs storing, and any mode that can carry freight can hand over to any other |
| Rivals after day zero use the existing CorporateAI, given the day-zero planners through a script API (option A) | No second AI to write and keep in step; NewGRF tech reaches running rivals automatically. CorporateAI only works in this fork, which is fine |
| Build the functionality first, balance later | Balance passes are planned for phase 8, once everything exists |
| Pushes and pull requests only when the owner says so | Owner's working preference |

## Where it stands

| Phase | State | Branch / pull request |
|---|---|---|
| 0: day-zero bus bootstrap | done | on `main` |
| 1: Tech Library | done | merged in [#1](https://github.com/BlacklionXYZ2/openttd-fork/pull/1) (`feature/multimodal-incumbents`) |
| 2: rival profiles and rail | done | merged in #1 |
| 3: airlines | done | merged in #1 |
| Headstart scaling | done | merged in #1 |
| 4: trucks, feeders, fleets | done | merged in #1 |
| 5: modular tech trees, mail, ships, freight transfers, third-party compatibility, community spec | done | [#2](https://github.com/BlacklionXYZ2/openttd-fork/pull/2) (`feature/modular-tech`), open |
| 6: rivals that keep up (script API, CorporateAI 4) | done, single-player | `feature/rivals-keep-up`, built on top of #2 |
| 7: future tech arrives | drones, hyperloop and rockets work with placeholders; the data centre industry exists; parcels and the cargo hub wait on decisions | `feature/future-logistics-grf` |
| NewGRF (workstream B) | started in `mods/newgrf/future_logistics/`: drones, hyperloop, rockets | `feature/future-logistics-grf` |
| 8: quality and release | not started | |

Tests on Linux: the C++ unit tests (2307 assertions) and 38 full-game
integration scenarios pass; ten-year runs with growing rivals are recorded in
`validation.md`. Nothing has yet been built or played on the target Mac.

## Map of the code

| Where | What |
|---|---|
| `src/future_logistics/bootstrap.cpp` | Day zero: creates and funds the rivals, builds their plans, runs `future_logistics_check` |
| `src/future_logistics/tech_library.*` | Every loaded vehicle and infrastructure type, its roles, and "best vehicle for this job" |
| `src/future_logistics/catalogue.*` | Families, rival profiles, adoption rules and recipes, read from badges; the built-in four rivals |
| `src/future_logistics/mechanics.*` | The mechanic registry: names used by NewGRFs, mapped to planners and freight legs |
| `src/future_logistics/freight_planner.*` | Freight routes: pairs of industries, direct or in two legs with a transfer |
| `src/future_logistics/{rail,road,air,water}_planner.*` | Building railways, road routes, airports and docks, with their vehicles |
| `src/future_logistics/scale.*` | Market tiers, fleet sizes, grants |
| `src/future_logistics/growth.*` | Building after day zero, for the script API |
| `src/future_logistics/tag_index.*` | Finding NewGRF items by `fl_id` |
| `src/future_logistics/tech_console.cpp` | The `future_logistics_tech` console command |
| `src/script/api/script_futurelogistics.*` | `AIFutureLogistics`, the script API |
| `mods/ai/CorporateAI/` | The rivals' AI after day zero |
| `mods/gamescripts/DayZeroMonopolies/` | Optional Game Script with milestone news |
| `tests/future_logistics/` | Integration harness, test NewGRF, Tech Library snapshots |
| `src/tests/future_logistics_tech.cpp` | Unit tests |

## Building and testing

- Build: see [`README.md`](README.md) (Apple Silicon and Intel Mac flags; on
  Linux the same CMake steps work). The `mac-artifact-x86_64.yml` workflow
  builds the Intel macOS app.
- Unit tests: `openttd_test` from the build directory.
- Integration: `python3 tests/future_logistics/integration.py --binary <openttd> --baseset <OpenGFX dir>`;
  `--update-snapshots` after an intended Tech Library change.
- The test NewGRF is rebuilt with NML 0.9.0: `nmlc -o fl_test.grf fl_test.nml`.
- Third-party sets used for compatibility checks build from source; see
  `compatibility.md`. Iron Horse needs NML from its GitHub master branch.
- In-game: `future_logistics_tech list | badges | recipes | validate | find <id> | best ...`
  and `future_logistics_check`. Run with `-d misc=3,script=4` to see the `FL` log lines.

## Open questions and next steps

In rough order:

1. **Review and merge #2**, then open the pull request for phase 6.
2. **Continue the NewGRF and phase 7.** Drones, hyperloop and rockets are in (placeholder values in
   `mods/newgrf/future_logistics/PLACEHOLDERS.md`, base-game graphics). The
   Corporate Infrastructure handoff from Gareth (concept art and requirements)
   is reviewed in the NewGRF README, and his v0.1 object package builds and
   places in game (`mods/newgrf/corporate_infrastructure/`); the art still needs
   converting before the working vehicles and buildings can use it. Each technology needs its
   graphics, vehicles and infrastructure, tagged per `tech_format.md`. Points
   already decided:
   - Vehicles carry the shared `role/` and `power/` badges as well as `fl_` ones.
   - Liquid oxygen can use the registered `O2__` cargo. Rocket fuel needs a new
     label, because `FUEL` is taken (it means natural gas). New cargo needs the
     right cargo classes.
   - New track types follow the railtype label scheme where they can.
   - Drones, hyperloop and rockets should need no code (airport pair and rail
     line mechanics). A real launch sequence, rooftop pads or underground
     hyperloop would each need a new mechanic or engine work.
   - The exact details of each technology and industry are still to be decided.
3. **Balance (phase 8).** The player must be able to catch up. Grants, tiers,
   fleet sizes and the rivals' expansion pace are all settings or table values
   ready to tune.
4. **Play-test on the target Mac.**
5. **Known limits worth fixing:**
   - Rivals only build after day zero in single-player. Multiplayer needs the
     planners' commands wrapped in one network command, which means
     accumulating their costs (nested commands do not charge money).
   - Railways and new road climb and descend straight over tiles inclined along
     them, and curve only on flat tiles. Both bridge water, roads, railways and
     dips up to six tiles across, from flat open land to flat open land at the
     same height, and both tunnel up to 16 tiles through hills (road routes
     only when there is no route without bridges or tunnels). There are no
     longer bridges or foundations, so wide rivers, deep valleys and steep or
     uneven ground can still stop a line. Where a railway cannot
     bridge a town road it uses a level crossing, where a fast train can hit a
     road vehicle already on it (an OpenTTD rule: the crossing only closes when
     the train is one tile away).
   - Freight routes have at most two legs.
   - Vehicles whose capacity comes from a NewGRF callback (Iron Horse wagons)
     show a placeholder capacity in the Tech Library.
   - Road routes use existing town and unowned roads, with drive-through
     stops on them. Stations, depots, airports and docks still need clear, flat land.
   - Temperate maps rarely start with banks, so data centres usually have
     nothing to receive: the data centre's cargo needs a decision.
6. **Parked:** Gareth reported that GUI icons looked very small in a recent Mac
   build. It has not been reproduced; it is to be looked at later.
7. **Environment note:** the cloud sessions used for development could not
   reach BaNaNaS (OpenTTD's content service), so third-party NewGRFs were built
   from GitHub sources instead.

## Words used in the code and docs

| Word | Meaning |
|---|---|
| Profile | A rival company's character: name, preferred markets, adoption rules |
| Family | A group of tech that belongs together (`conventional`, `rocket`...) |
| Mechanic | A kind of construction the code knows how to build (`rail_line`, `road_route`, `airport_pair`, `dock_pair`) |
| Market | What a service connects (`town_pair`, `mail`, `industry`, `feeder`) |
| Recipe | Family + mechanic + market: one kind of service a rival builds |
| Tier | Size of a market (village, town, city, metropolis), which limits what is built |
| Leg | One part of a freight route; two legs meet at a transfer station |
| Tech Library | The game's catalogue of every loaded vehicle and infrastructure type |
