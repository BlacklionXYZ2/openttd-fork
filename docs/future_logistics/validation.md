# Recorded validation

Validated engine/test source: `6bb4179e67295a9421a7e51a29867a6aa426e925`.
Date: 23 October 2026. Environment: Debian 13, x86_64, GCC 14.2.0,
CMake 4.4.4, Ninja 1.13.2, SDL 2.32.4, LZMA 5.8.1, OpenGFX 7.1.

## Actual results

| Check | Result |
| --- | --- |
| Unmodified upstream Debug dedicated build | Passed before engine changes |
| Initial upstream unit suite | 94 passed; four saved-game regressions initially blocked by missing OpenGFX/LZMA dependencies |
| Feature GUI Debug build with SDL and LZMA | Passed, no compiler warnings/errors |
| Final CTest suite | **98/98 passed**, including the four upstream saved-game regression suites |
| Full-engine integration suite | **16 scenarios passed** |
| Default terrain, town density and tree settings, seed 12345 | Four operational starter networks |
| Controlled world, seed 12345, repeated | Identical grants, vehicles, route endpoints, route hashes and balances |
| Native macOS ARM64 | **Passed** on macOS 15 ARM64, AppleClang 17: native Debug build and **98/98 CTest checks** |

In the controlled test, human company 0 remained local and human-controlled.
Rivals 1–4 each received a single 25,000,000-unit grant, recorded as negative
`Other` expenses, while retaining the standard 100,000-unit loan. All four
vehicles had valid orders, started, and reached their connected stations. After
three simulated calendar months, delivered passenger counts were **31, 54, 56,
and 34**, with positive vehicle profit for each rival. Networks and balances
survived immediate save/reload; the operated networks also survived a second
save/reload. All four caretaker controllers resumed, and the Game Script saw the
five initialized companies.

The remaining scenarios verified disabled bootstrap, loading an ordinary game
without bootstrapping it, a two-rival limit and custom grant, one-town and
no-available-bus fallback, zero demolition capacity, forced vehicle-purchase
failure with complete infrastructure cleanup, late-start news catch-up,
announcement persistence without duplicate news, and dedicated multiplayer
exclusion. A forced purchase failure left **zero road bits and station tiles**
for all four rivals. No Squirrel errors or cleanup failures appeared in the final
suite.

## Phases 1 to 5 (Tech Library, profiles, railways, airlines, trucks and feeders, modular tech trees)

Branch `feature/tech-library`, 23 October 2026. Environment: Ubuntu 24.04,
x86_64, GCC 13.3.0, CMake 3.28.3, Ninja, SDL 2.30.0, OpenGFX 7.1. Debug build
with `-g1`, as above. These results are from Linux only; no Mac build or play
test was run for these phases.

| Check | Result |
| --- | --- |
| Feature GUI Debug build | Passed, no compiler warnings/errors |
| CTest suite | **107/107 passed** (98 upstream and phase 0, 9 Tech Library, scaling and format unit tests; 132 Future Logistics assertions) |
| Full-engine integration suite | **28 scenarios passed** (16 updated phase 0 scenarios, 4 per-climate Tech Library snapshots, 2 for a 2026 start, 2 for a coastal world, 2 for a two-leg freight route, 1 tiny world, 1 NewGRF tech tree) |
| Mail, ships, air freight (1950) | Mail trains (National Rail) and mail trucks (Metro & Regional) built and checked; air mail and air freight correctly rejected (no base-game mail or freight aircraft) |
| Coastal world, three seeds | A ferry was built in each; in the tested seed it reached both docks within three months. Freight ships were rejected because no industry had usable coast nearby |
| Two-leg freight routes, 512×512 coastal world, six seeds | Integrated Logistics' ship recipe built a two-leg route in all six, choosing the legs itself: ship then train twice, train then ship three times, ship then truck once. Twelve simulated months on three of them: seed 3, oil rig → port → train to a refinery, the train loaded what the ships left (80 aboard) and ended at +124,296; seed 2, oil field → train → port → ships, a ship had delivered and earned 3,015,456 and the other had 220 aboard; seed 5, ships → dock → trucks, both trucks carried oil on and earned about 236,000 each |
| Third-party NewGRFs: FIRS, Iron Horse, Unsinkable Sam (512×512 coastal, seed 3, 1950) | Before the fix: no freight at all, because FIRS industries produce through a callback and have no fixed rate. After: 11 services in every mode with Iron Horse trains and wagons, Sam ships, FIRS cargos; every vehicle served after six months; `validate` reported no problems with Iron Horse's 1,502 badges. See `compatibility.md` |
| Single freight planner, controlled world | 1950: the same nine services as before. 2026: the railway that had picked an unsupplied steel mill now carries oil, and Integrated Logistics runs iron ore by rail and the steel it makes onward by truck |
| `fl_id` and the mechanic registry | The test NewGRF's `fl_id` lookups found the tagged vehicle, both items sharing a duplicate id, and `validate` reported the duplicate and a mechanic the game does not have |
| NewGRF tech tree (`tests/future_logistics/grf/fl_test.grf`, five rivals) | TestCorp Airways, declared only by the NewGRF, built an airline with the NewGRF's "testjet" family; AeroLink adopted it through a NewGRF rule; mail vehicles took the declared parcel role; `validate` reported exactly the eight deliberate mistakes |
| Without NewGRFs after the phase 5 rewrite | Same seven services and nine vehicles as phase 4; catalogue reports no problems |
| Headstart scaling | Controlled world: 34,011 people, grant 4,251,375 per rival (below the 25 million cap), every railway and airline within its tier. Tiny world (64×64, two towns, no industries): grant about 250,000, no railways or airlines |
| Controlled world with normal industry density, seed 12345, 1950 | All four rivals operational with seven services and nine vehicles: grain and coal freight railways, two passenger railways, an airline with two aircraft, a two-bus feeder into Metro & Regional's railway station, and a goods truck from the factory that the grain railway supplies |
| Same world after three simulated months | Every vehicle visited both of its stations; the goods truck had delivered goods made from rail-delivered grain |
| Same world, 2026 start, then three simulated months | Seven services; electrified and monorail track, a large airport, a steel-to-goods chain; every vehicle visited both of its stations |
| Same world after three simulated months, then save/reload | Every train and bus had visited both ordered stations; every rival had delivered cargo; all four CorporateAI controllers reported their saved profile |
| Default terrain, trees and towns | Same six services built |
| Forced vehicle-purchase failure (vehicle limits 0) | Every rival's rollback left zero road bits, rail bits and station tiles |
| 2026 start (manual run, phase 1 only, before railways and airlines) | Four operational bus networks using the Foster Bus, chosen by the Tech Library |

During phase 3, aircraft orders were rejected until they used the far-end stop
location, as bus orders already did, and railways failed in 2026 until the rail
planner chose the track type. Two engine issues were found and fixed during phase 2: rail commands from
different companies tripped a signal-buffer ownership assertion, and rail
rollback tripped a direction assertion from stale signal-buffer entries. Both
were caused by not flushing the signal update buffer between directly executed
commands.

## Phase 6 (rivals that keep up)

Branch `feature/rivals-keep-up`, 23 October 2026, Linux, same build setup as above.

| Check | Result |
| --- | --- |
| CTest suite | **All passed** (2307 assertions in 104 test cases) |
| Full-engine integration suite | **29 scenarios passed**, including a new 13-month growth run; the service scenarios now require every day-zero vehicle to keep serving its stations and the fleet not to shrink, as the rivals grow |
| Controlled world, 1950, 13 months | Fleet 11 → 24 vehicles. AeroLink opened a new airline, Metro & Regional a mail truck route, Integrated Logistics a goods truck route; mail planes and goods trucks were added to busy routes, never past five per route. National Rail opened nothing: the rail planner found no flat, clear line between unserved towns or industries on the small map |
| Controlled world, 1955, ten years | Every two-yearly check passed (24, 41, 43, 48 and 47 vehicles). 23 new routes, including railways for National Rail, Metro & Regional and Integrated Logistics, ferries and airlines. Locomotives went steam → UU '37' → Floss '47' and aircraft Coleman Count → FFP Dart → Bakewell Luckett through the Tech Library's replacements; at year ten every locomotive was a Floss '47' and every aircraft a Luckett. 16 loss-making vehicles were sold. All four rivals ended with 5.3 to 6.1 million and no loan |

Found and fixed during the runs:

- Day-zero vehicles do not share orders, so the AI counted each as its own
  route and kept adding mail planes; routes are now told apart by cargo and
  stations.
- Copies were placed in the nearest depot, which could be on another route's
  road; `GetRouteDepot` now finds a depot on the vehicle's own network.
- The connectivity check only followed company road, and failed trucks that had
  used a town road to reach a depot; it now follows town and unowned road too.
- With breakdowns off vehicles never visit depots, so autorenew and autoreplace
  never ran; the AI now sends vehicles that are due.

## Phase 7 start: drones (Future Logistics NewGRF)

Branch `feature/future-logistics-grf`, 24 October 2026, Linux.

| Check | Result |
| --- | --- |
| NewGRF build | `future_logistics.grf` builds with NML 0.9.0; `validate` reports no problems |
| Controlled world, 2026, with the NewGRF | AeroLink and Metro & Regional both built drone mail routes on day zero (two drones each) between drone ports; the base-game helistation stays available as its own airport |
| Same world, four simulated months | Every drone reached both drone ports and carried full loads of mail; CorporateAI added drones where mail waited (four → nine). Profit is about break-even at the placeholder values |
| Controlled world, 2041, with the NewGRF | National Rail built a 113-tile hyperloop mail line on day zero (propulsion pod and five mail capsules on `HYPL` track); no city pair was found for a passenger line on the small map |
| Same world, three simulated months | The pod reached both stations with full loads and earned 1.5 million, the most of any of National Rail's trains: the placeholder values are far too generous |
| 512×512 world, 2056, with the NewGRF | AeroLink opened rocket passenger and mail routes between two cities via spaceports on day zero. Rocket freight waits for goods or valuables to carry: rockets are limited to express and armoured cargo, after a first version carried wood |
| Integration suite | **35 scenarios passed**, including two scenarios each for drones, hyperloop and rockets |

Later the same night:

| Check | Result |
| --- | --- |
| 512×512 world, 2056 | Rockets: see above |
| Data centres, 512×512 temperate worlds, four seeds | 4 to 9 data centres generated, one per town; `validate` clean. No world had a bank, so there was nothing to deliver to them: the data centre's cargo needs a decision |
| Road routes over town roads | Across the integration worlds most mail and truck routes now use a few tiles of existing street (up to 6), mostly stops on town roads; the controlled, 2026 and coastal worlds build the same number of services as before |
| Integration suite | **36 scenarios passed**, unit tests passed |
| Tech arriving mid-game: controlled world from 2021, four simulated years, with the NewGRF | No drones exist at the start. In 2024 AeroLink adopted drones through CorporateAI and opened a drone mail route; in 2025 AeroLink and Metro & Regional opened more. Every yearly check passed; fleet 28 → 38 → 42 → 57 vehicles |
| Ten-year regression, controlled world from 1955, with all of the night's changes | 22 new routes, three of them railways with level crossings over town roads. Checks at years 2, 4, 6 and 8 passed (26, 49, 61, 60 vehicles); rivals ended with 5.1 to 6.0 million and no loan. At year ten three loss-making trains sat stopped in depots waiting to be sold and the check failed them; it now reports such vehicles as parked, and passes (60 vehicles) |

The year-ten trains waited because autorenew or autoreplace had renewed them in the depot
(they were zero days old) under new vehicle IDs, so CorporateAI no longer recognised them as
the ones it sent to be sold. It now sells any of its vehicles stopped in a depot, since it never
stops one for another reason. Continuing the year-ten save for three months with the new
CorporateAI, no vehicle was left parked and the check passed (63 vehicles).

Railways and roads over hills (same night, later):

| Check | Result |
| --- | --- |
| Four mountainous 256×256 worlds (terrain "mountainous", no sea), 1950 | Before: 10 railways and 27 services in all, and one world with a single railway. After: 22 railways and 42 services; all but four railways climb over 2 to 17 sloped tiles; every company operational and every check passed |
| Two of those worlds, four simulated months | Every train reached and served both stations |
| Controlled world | Metro & Regional's railway now encloses its station, so its feeder buses reach it over a one-tile road bridge across the line |
| Integration suite | **37 scenarios passed**, including a new mountainous world; unit tests passed |

Found and fixed during the runs:

- The rail search could return a line that ran over the same tile twice (its positions
  include the direction), which preflight passed and building then failed on.
- The rail search allowed level crossings on track that forbids them (hyperloop), so preflight
  rejected the line; it now knows the track type, and National Rail builds a hyperloop
  passenger line in the 2041 world.
- A first version let road routes cross the company's railways with level crossings. Within
  three months a train destroyed a feeder bus in two worlds: a crossing closes only when a
  train is one tile away, too late for a slow bus already on it to clear a fast train. Road
  routes now use a short bridge instead, and only when there is no route without one.
- Undoing the first leg of a two-leg freight route failed to sell its train, which had already
  been started; it is now stopped first.
- CorporateAI now logs crashed vehicles.
- Railways now bridge roads too, and use a level crossing only where no bridged line exists.
  Across the integration suite 11 railways bridge a road and 7 still cross one; in the 2041
  world an electric line bridged a road and its train served both stations. All 37 scenarios
  and the unit tests pass.

Bridges over rivers (same night, later):

| Check | Result |
| --- | --- |
| Four hilly 256×256 worlds with many rivers, 1950 | With one-tile bridges: 33 services, three construction failures, one company not operational. With bridges up to six tiles over water, roads, railways and dips: 40 services, no failures, all companies operational; 13 lines use bridges |
| Two of those worlds, four simulated months | Every vehicle served its stations; no crashes |
| Integration suite | **38 scenarios passed**, including a new world with rivers; unit tests passed |

Found and fixed: building a railway's track before its stations could clear enough trees to
lose the town's permission for the station ("local authority refuses"), after preflight had
passed. Stations are now built first.

Railway tunnels (same night, later): on the four mountainous worlds, four railways now go
through a hill in a tunnel (up to 16 tiles) instead of over it; no construction failed and every
check passed. In four simulated months both trains using tunnels served their stations. The
integration suite (38 scenarios, the mountain scenario now requiring a tunnel) and the unit
tests pass.

Road tunnels: road routes use tunnels (and bridges) only when there is no other route, which
none of the test worlds needed. To exercise them, the mountainous worlds were rerun with
tunnels allowed from the start: three road routes tunnelled, one with three tunnels. This
showed that a depot could be joined to a tile above a tunnel, where no road is built; the
depot junction now avoids tunnels as well as bridges. Afterwards every check passed, and in four
simulated months the vehicle on the three-tunnel route served its stations.

Found and fixed: the first NewGRF airport based on a built-in airport replaces it in OpenTTD,
so the drone port at first replaced the helistation; the NewGRF now defines an unchanged
helistation copy first, and the Tech Library and `fl_id` index read each airport type without
following the replacement redirect, so no airport is listed twice.

## NewGRF packages (24 October 2026)

| Check | Result |
| --- | --- |
| Corporate Infrastructure 0.1 (Gareth's package) | Compiles with NML 0.9.0 without warnings, reproducibly. With the Future Logistics NewGRF, a test Game Script built all six objects |
| Starship package, build | Compiles with NML 0.9.0 without warnings, reproducibly; 36 sprites made from the handoff art by `tools/make_sprites.py` |
| Starship on its own, 512×512 world, 2050 | `validate` reports no problems; `find` finds both `fl_id`s. AeroLink adopted the rocket family by its rule for any new air family and opened a Starship passenger route and a mail route between launch sites on day zero |
| Same world, three simulated months | All six Starships served both of their launch sites (profits far too high at the placeholder values). The NewGRF profiler shows both the idle and the powered sprite sets in use |
| Starship with the Future Logistics NewGRF, 2056 | `validate` reports no problems; AeroLink chose Starship over the Reusable Rocket. Exactly one city airport in either load order (the package leaves out its own copy when the Future Logistics NewGRF is active) |

Starship balance and shipping (same day, later):

| Check | Result |
| --- | --- |
| Rankings, 2056, both NewGRFs (135 vehicles) | Starship has the highest purchase cost (522,949; next the Reusable Rocket, 512,695), running cost (7,171 a year; next the Darwin 300, 7,115) and capacity (500 passengers; next the Dinger 200, 400). The integration suite now checks all three |
| Profit, same world, one simulated year | The two highest-earning vehicles were Starships (25.1 and 17.1 million); the best other vehicle, a Dinger 200, earned 14.3 million. Starships sharing a route earn less, since the first takes most of the waiting passengers. The Starship service scenario now checks that its top earner is a Starship |
| Shipped with the game | The NewGRFs are copied to the build's `newgrf` folder and installed with the game's data. With a configuration that has no NewGRF list (a first run), a new 2050 game loaded Future Logistics and Starship by itself and AeroLink built Starship routes |

Found and fixed: with both NewGRFs loaded, each added an unchanged copy of the city airport,
so players would have seen two. The flame sprite's speed threshold was first given in the wrong
units (for aircraft, `current_speed` is in units of about 12.8 km/h), so it would never have shown.

## Reproduction and evidence

Local build (dependencies were extracted into `work/deps/usr` because this
executor has no privileged package installer):

```sh
cmake -S . -B work/build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DOPTION_DEDICATED=OFF -DCMAKE_CXX_FLAGS_DEBUG=-g1 \
  -DCMAKE_PREFIX_PATH="$PWD/work/deps/usr" \
  -DCMAKE_DISABLE_FIND_PACKAGE_CURL=ON
cmake --build work/build --parallel 4
ctest --test-dir work/build --output-on-failure --parallel 3
python3 tests/future_logistics/integration.py \
  --binary work/build/openttd \
  --baseset work/deps/usr/share/games/openttd/baseset \
  --output work/integration-final
```

`-g1` retains Debug assertions and line information while reducing local debug
artifact size. Optional PNG, audio, font-rendering and HTTP libraries were not
installed here. OpenGFX and LZMA are required for the upstream saved-game tests.
The new-game checks use SDL's dummy display and the actual GUI-capable startup
path. Saved single-player games use the null driver for fast simulation. No
interactive macOS GUI session was available. GitHub's native ARM64 runner
subsequently compiled commit `133554042118cb6662f70cd7f2c03d29308695bc`
(the same engine code plus recorded validation) and passed all 98 CTest checks.
This proves native compilation and test execution, not a manual Mac play session.

- [Configure and build logs](validation/linux-build.txt)
- [Full CTest result](validation/linux-tests.txt)
- [Integration scenario report](validation/integration.txt)
- [Engine and script runtime trace](validation/runtime-trace.txt)
- [Native Apple Silicon CI evidence](validation/macos-arm64.txt)
- [Native macOS/Linux workflow](../../.github/workflows/future-logistics.yml)

## Remaining limits

The first planner supports flat land and available standard-road passenger buses.
It does not construct bridges, tunnels, slopes, freight chains or future vehicles.
It reserves distinct town pairs, construction funds and demolition capacity; maps
or settings without enough suitable pairs may retain funded rivals without a
starter network, with an explicit failure log. Cleanup removes new infrastructure
but does not refund construction/demolition costs or restore cleared trees.

CorporateAI grows the network only in single-player games (see phase 6 in the
roadmap); in multiplayer it only looks after its fleet. Other user-selected AIs may change the supplied
network after startup. The GS milestone messages describe planned content and do
not unlock vehicles. Native compilation and automated tests passed; a manual Mac play test remains
outstanding. The 16 full-game integration scenarios were run on Linux.
