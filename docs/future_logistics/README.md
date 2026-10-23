# Future Logistics: day-zero bootstrap

Upstream: official [OpenTTD/OpenTTD](https://github.com/OpenTTD/OpenTTD), master
`51cb6dcf5e0d304396bf7c5cb3d724adde243fa8`. The destination's initial README commit
is retained as a parent of the import merge. GPL-2.0 and upstream third-party
notices remain in force. No JGR code is used. To sync a fresh clone later:

```sh
git remote add upstream https://github.com/OpenTTD/OpenTTD.git
git fetch upstream master
```

The working repository already has that upstream remote configured.

## Build on Apple Silicon

Install Xcode command-line tools and Homebrew, then:

```sh
brew install cmake ninja pkg-config xz libpng lzo zstd
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --parallel
ctest --test-dir build --output-on-failure
file build/openttd.app/Contents/MacOS/openttd
```

On an Intel Mac (for example a 2015 MacBook Pro on macOS 12 Monterey), use the
same steps with `-DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0`.
The manual "Mac Intel x86_64 build artifact" workflow builds a packaged app for
that target.

Follow upstream's [build documentation](https://github.com/OpenTTD/OpenTTD/wiki/Compiling-on-macOS)
for optional audio dependencies. Install a compatible OpenGFX baseset before
running the game. The CI workflow checks the host architecture on `macos-15`
(an ARM64 runner per actions/runner-images) and builds natively. Private repository
Actions availability and billing are controlled by the repository owner.

## Configuration and scripts

This experimental fork enables bootstrap by default. Set these in openttd.cfg:

```ini
[game_creation]
bootstrap_enabled = true
bootstrap_capital = 25000000
bootstrap_capital_per_head = 500

[difficulty]
max_no_competitors = 4
```

The fork changes the default competitor limit from zero to four; an existing
configuration's explicit zero still disables rivals. At most four are created,
respecting a lower competitor limit. `bootstrap_enabled=false` restores ordinary
new-game startup. The bootstrap settings are new-game-only, local configuration,
not new savegame fields. Capital is in internal currency units, independent of
the display currency. Each rival's grant is the world's total population times
`bootstrap_capital_per_head`, shared between the rivals, and never more than
`bootstrap_capital`. Each rival receives that one grant **in addition to** its
normal initial loan. `ExpensesType::Other` records the grant; construction and
vehicle purchases reduce the cash normally. The loan is not erased or duplicated.

Copy `mods/ai/CorporateAI` to your personal OpenTTD `ai/` directory and select it
for all four rivals in AI/Game Script settings. Each rival is given a profile
(see [`roadmap.md`](roadmap.md), phase 2), is named after it, and builds that
profile's starter railways and bus routes. CorporateAI reads its `profile`
setting. It preserves the supplied fleet;
expansion and technology adoption are future work. Other selected AIs remain in
control after bootstrap and may replace or sell the starter network. Without an
installed AI, upstream's dummy fallback is used; it is not a strategic competitor.

Optionally copy `mods/gamescripts/DayZeroMonopolies` to personal `game/` and select
it. It logs companies and publishes once-only roadmap news for 2022, 2035 and
2050, including late-start catch-up. It saves announcement state, never grants
money and never claims that planned vehicles have been implemented. API 16
signatures were checked against the pinned source.

## Lifecycle and construction

`MakeNewGameDone` creates human company 0, then calls the isolated C++ module
before `OnStartGame` and the game-start console script. Loaded games and editor
startup use other paths. The multiplayer guard runs before any modifications;
dedicated/headless spectator startup returns before human creation, as upstream
does. Heightmap-generated single-player games use the same callback.

The map generator starts and initially runs a Game Script **before** this callback;
the supporting GS sleeps immediately so its world inspection occurs later. No
normal game ticks run inside the synchronous bootstrap. Company creation initializes
an AI VM but does not call its game loop. Each initialized AI is also explicitly
paused during its own setup, then resumed. Current company is restored after every
scoped funding/construction operation; `OnStartGame` selects human company 0.

The planner considers the first 128 towns, up to 128 nearest town pairs (distance
16–96), and explores at most 8192 tiles per pair in a fixed neighbour order.
Candidate stops must both accept and produce passengers and belong to the selected
town's area. Each completed network reserves its two towns during bootstrap so
initial rivals do not starve one another of passengers at overlapping stops. The bus planner
uses the best available standard-road passenger bus and flat clear/tree tiles at one
height. It builds an owned route, two distinct drive-through stops, an attached
depot and one started bus with two station orders. The rail planner works the
same way for two terminus stations, a single line, a depot and one train; freight
railways reserve their two industries. All engine command executions in the rail
planner flush the track signal update buffer, as the normal command loop does.
The air planner builds two airports of the type the market tier allows and a
small fleet chosen by the Tech Library, for passengers, mail or freight. The
water planner builds two docks on the same water, a ship depot and a fleet, and
uses an oil rig's own dock where there is one. The freight planner picks a
cargo, source and destination and builds the route with one of these, or with
two that meet at a joined station where the first leg's vehicles transfer and
leave empty. The road planner builds buses, feeder
buses that join the company's railway stations, and trucks that may continue a
freight railway's cargo chain. No shared infrastructure is
modified. All changes go through tested engine command interfaces and normal
cost accounting. No map tiles are changed directly. No interactive RNG is used.
Equivalent map seed, game settings, installed scripts and NewGRFs are required for
reproducibility.

Failures are bounded and logged with `FL` prefixes. Before construction, every
road/stop/depot command is tested and the estimated layout plus vehicle reserve
must fit company funds. The planner also reserves demolition credits and funds
for cleanup, and uses the same auto-construction/no-water flags as the normal
construction commands. A bus station order explicitly uses `FarEnd`, as required
by the current non-train order command. If execution fails, the stopped vehicle is sold and newly
built infrastructure is cleared in reverse order. Cleanup failures are logged. The company stops attempting construction after
an execution failure; rejected preflight candidates may still be tried safely.
Cleared trees and incurred construction/demolition costs are not transactionally
restored. A funded company remains if no usable route can be built. This is a
reported bootstrap failure, never counted as an operational network.

## Diagnostics and validation

Run with `-d misc=3,script=4` to record allocations, route identifiers and failures.
The console command `future_logistics_check` checks company 0, local human control,
vehicle orders, started vehicles and ownership/road-bit connectivity from depot
to both stops. It logs whether each bus has visited an ordered station, its tile
and profit. This diagnostic is read-only and can be used after save/reload.

The console command `future_logistics_tech list` prints the Tech Library: every
loaded vehicle, rail type, road type and airport with the roles the planner
sees. `future_logistics_tech best <mode> <role> [<cargo label>]` shows which
vehicle a company would pick for a job. `future_logistics_tech badges`,
`recipes` and `validate` show what loaded NewGRFs declare, each rival's plan,
and any problems with the declarations.

NewGRFs can add tech families, rivals and cargo meanings with `fl_` badges; see
[`tech_format.md`](tech_format.md). NewGRF rivals come after the four built-in
ones, so raise `max_no_competitors` to include them. The test NewGRF in
`tests/future_logistics/grf/` is rebuilt with `nmlc -o fl_test.grf fl_test.nml`
(NML 0.9.0). The format is described in
[`tech_format.md`](tech_format.md) and the plan in [`roadmap.md`](roadmap.md).

See [`compatibility.md`](compatibility.md) for how third-party NewGRFs (FIRS,
Iron Horse, Unsinkable Sam) and the NewGRF specification fit with this format.

See `tests/future_logistics/integration.py` for reproducible full-engine checks.
Pass `--update-snapshots` after an intended change to the Tech Library to
rewrite `tests/future_logistics/tech_snapshots/`.
The tests use a real GUI-capable SDL driver with SDL's dummy display, not a
multiplayer dedicated server. Test artifacts go in `work/`.

## Scope and remaining work

Freight routes have at most two legs (one transfer). No signals,
bridges, tunnels, terrain engineering, strategic AI expansion or in-game AI
vehicle adoption are implemented here. Drones, hyperloop and rockets are
supported as NewGRF tech families, but the Future Logistics NewGRF itself
does not exist yet; only the format test NewGRF does. Difficult maps, disconnected heights, very early start dates,
NewGRFs that remove compatible standard-road buses, and town restrictions may
prevent some or all initial networks. See `validation.md` for actual test results
and limitations; CI configuration alone is not a completed Apple Silicon test.
