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
