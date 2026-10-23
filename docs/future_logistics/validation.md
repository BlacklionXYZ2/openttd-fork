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

CorporateAI preserves the initial fleet; strategic expansion and technology
adoption are not implemented. Other user-selected AIs may change the supplied
network after startup. The GS milestone messages describe planned content and do
not unlock vehicles. Native compilation and automated tests passed; a manual Mac play test remains
outstanding. The 16 full-game integration scenarios were run on Linux.
