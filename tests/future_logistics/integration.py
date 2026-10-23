#!/usr/bin/env python3
"""Full-engine regression checks. Requires an SDL GUI build and OpenGFX.

Each run uses an isolated local configuration and installed supporting scripts.
New games use SDL's dummy display to exercise the normal human-company callback.
Saved games use OpenTTD's null driver to simulate ticks quickly in single-player.
This is runtime integration testing, independent of the C++ unit test suite.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SNAPSHOTS = ROOT / "tests/future_logistics/tech_snapshots"
CLIMATES = ["temperate", "arctic", "tropic", "toyland"]


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--baseset", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "work/integration")
    parser.add_argument("--update-snapshots", action="store_true",
                        help="rewrite the Tech Library snapshots instead of comparing against them")
    args = parser.parse_args()
    binary, baseset, output = args.binary.resolve(), args.baseset.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    def run(name, *, seed=12345, load=None, enabled=True, limit=4, capital=25000000,
            towns=48, year=1950, service=False, dedicated=False, vehicle_limit=500, clear_limit=4096, default_world=False,
            climate="temperate", commands="", industries=4, map_bits=8, grfs=(), coastal=False, months=3):
        folder = output / name
        folder.mkdir(exist_ok=True)
        if grfs:
            (folder / "newgrf").mkdir(exist_ok=True)
            for grf in grfs:
                shutil.copy(grf, folder / "newgrf" / grf.name)
        for src, dst in [(baseset, folder / "baseset"),
                         (ROOT / "mods/ai", folder / "ai"),
                         (ROOT / "mods/gamescripts", folder / "game")]:
            shutil.copytree(src, dst, dirs_exist_ok=True, ignore_dangling_symlinks=True)
        cfg = folder / "openttd.cfg"
        cfg.write_text(f"""[game_creation]
bootstrap_enabled = {str(enabled).lower()}
bootstrap_capital = {capital}
landscape = {climate}
map_x = {map_bits}
map_y = {map_bits}
land_generator = 0
custom_town_number = {towns}
custom_sea_level = 0
tree_placer = 0
[difficulty]
max_no_competitors = {limit}
competitors_interval = 0
number_towns = 4
terrain_type = 0
quantity_sea_lakes = 4
industry_density = {industries}
vehicle_breakdowns = 0
[construction]
clear_frame_burst = {clear_limit}
[vehicle]
max_roadveh = {vehicle_limit}
max_trains = {vehicle_limit}
max_aircraft = {vehicle_limit}
[economy]
town_layout = 1
[gui]
pause_on_newgame = false
threaded_saves = false
autosave_interval = 0
[network]
server_game_type = local
participate_survey = no
[ai_players]
none =
CorporateAI =
CorporateAI =
CorporateAI =
CorporateAI =
CorporateAI =
[game_scripts]
DayZeroMonopolies =
""")
        if grfs:
            cfg.write_text(cfg.read_text() + "[newgrf]\n" + "".join(f"{grf.name} = \n" for grf in grfs))
        if coastal:
            text = cfg.read_text().replace("land_generator = 0", "land_generator = 1").replace("quantity_sea_lakes = 4", "quantity_sea_lakes = 3")
            cfg.write_text(text.replace("terrain_type = 0", "terrain_type = 1").replace("tree_placer = 0", "tree_placer = 2"))
        if default_world:
            text = cfg.read_text().replace("land_generator = 0", "land_generator = 1")
            text = text.replace("tree_placer = 0", "tree_placer = 2").replace("number_towns = 4", "number_towns = 2")
            text = text.replace("terrain_type = 0", "terrain_type = 1").replace("quantity_sea_lakes = 4", "quantity_sea_lakes = 0")
            cfg.write_text(text)
        scripts = folder / "scripts"
        scripts.mkdir(exist_ok=True)
        startup = commands + "future_logistics_check\n"
        if service:
            startup += "schedule on-next-calendar-month scripts/month1.scr\n"
            for month in range(1, months):
                (scripts / f"month{month}.scr").write_text(f"schedule on-next-calendar-month scripts/month{month + 1}.scr\n")
            (scripts / f"month{months}.scr").write_text("future_logistics_check\nsave " + str(folder / "snapshot") + "\nquit\n")
        else:
            startup += "save " + str(folder / "snapshot") + "\nquit\n"
        (scripts / "game_start.scr").write_text(startup)
        driver = f"null:ticks={3000 * months}" if service else ("null:ticks=1" if dedicated else "sdl")
        cmd = [str(binary), "-X", "-x", "-c", str(cfg), "-v", driver,
               "-s", "null", "-m", "null", "-d", "misc=3,script=4", "-G", str(seed), "-t", str(year), "-g"]
        if load:
            cmd.append(str(load))
        if dedicated:
            cmd.extend(["-D", "-v", "null:ticks=1"])
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        print("Running", name, flush=True)
        with (folder / "process.log").open("w") as log:
            proc = subprocess.run(cmd, cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=240)
        text = (folder / "process.log").read_text()
        require(proc.returncode == 0, f"{name}: process exited {proc.returncode}; see {folder}/process.log")
        require("FL rollback failed" not in text, f"{name}: cleanup failed")
        require("Your script made an error" not in text and "is not a valid index" not in text, f"{name}: Squirrel runtime error")
        return text, folder / "snapshot.sav"

    first, snapshot = run("new-game", commands="future_logistics_tech best road passenger_feeder PASS\n")
    # Trains are one vehicle each; road and air services log their fleet size.
    fleet = lambda text: (len(re.findall(r"FL rail company=", text))
                          + sum(int(n) for n in re.findall(r"FL (?:route|air|ship) company=\d+ (?:kind=\w+ cargo=\w+ |airport=\d+ )vehicles=(\d+)", text)))
    vehicle_count = lambda text: [int(n) for n in re.findall(r"FL check result=true companies=5 vehicles=(\d+) human=0", text)]
    profiles = re.findall(r"FL profile company=([1-4]) profile=(\w+)", first)
    require(profiles == [("1", "national_rail"), ("2", "aerolink"), ("3", "metro_regional"), ("4", "integrated_logistics")],
            "each rival should get its own profile, in order")
    built = set(re.findall(r"FL service company=\d+ recipe=(\S+) fallback=\w+ result=success", first))
    require({"conventional/airport_pair/town_pair", "conventional/rail_line/town_pair", "conventional/rail_line/industry",
             "conventional/road_route/feeder", "conventional/road_route/industry"} <= built,
            "the starting market should contain an airline, passenger and freight rail, feeder buses and trucks")
    require("FL catalogue families=1 profiles=4 problems=0" in first, "without NewGRFs the catalogue should hold only the built-in content")
    require(re.search(r"FL feeder company=3 station=\d+ town=\d+", first), "Metro & Regional did not feed its railway station")
    require(re.search(r"FL industries company=\d+ source=\d+ chain=true", first), "no truck service continues a freight railway's cargo chain")
    require(re.search(r"FL air company=2 airport=\d+ vehicles=\d+", first), "AeroLink did not open an airline")
    freight = re.findall(r"FL rail company=\d+ kind=freight cargo=(\w+) track=\w+ vehicle=\d+ wagons=([1-9])", first)
    require(freight, "no freight train with wagons was built")
    chosen = re.findall(r"FL tech company=([1-4]) role=passenger_feeder vehicle=(\S+)", first)
    require(chosen and len({vehicle for _, vehicle in chosen}) == 1,
            "every bus should come from the Tech Library")
    best = re.search(r"FL tech best id=(\S+) mode=road", first)
    require(best and best.group(1) == chosen[0][1], "console best-vehicle query disagrees with the planner")
    require("FL complete companies=5 operational=4" in first, "expected four operational rivals; see new-game/process.log")
    services = int(re.search(r"FL complete companies=5 operational=4 services=(\d+)", first).group(1))
    vehicles = fleet(first)
    require(services >= 4 and vehicles >= services and vehicle_count(first) == [vehicles], "initial ownership/connectivity failed")
    population, market_grant = map(int, re.search(r"FL market population=(\d+) towns=\d+ industries=\d+ grant=(\d+)", first).groups())
    require(market_grant == min(25000000, 500 * population // 4), "grant does not follow world population and the cap")
    require(market_grant < 25000000, "the controlled world is small enough that the population, not the cap, should set the grant")
    grants = re.findall(r"FL funded company=(\d+) grant=(\d+) balance=(\d+) loan=(\d+)", first)
    require(len(grants) == 4, "expected exactly four funding entries")
    require(all(int(grant) == market_grant and int(balance) == int(loan) + int(grant)
                for _, grant, balance, loan in grants), "funding/loan accounting mismatch")
    ledger = re.findall(rf"FL check funds company=([1-4]).*other_expense=-{market_grant} ai=true", first)
    require(len(ledger) == 4, "grant ledger or AI company type is incorrect")
    tiers = re.findall(r"FL towns company=\d+ a=\d+ b=\d+ tier=(\w+) population=(\d+),(\d+)", first)
    limits = {"village": 0, "town": 500, "city": 1500, "metropolis": 4000}
    require(tiers and all(min(int(a), int(b)) >= limits[tier] for tier, a, b in tiers),
            "a railway or airline was built for towns smaller than its tier allows")
    require(snapshot.exists(), "new-game snapshot missing")
    second, _ = run("repeat-seed")
    fingerprint = lambda text: re.findall(r"FL (?:funded|profile|route|rail|service|complete)[^\n]*", text)
    require(fingerprint(first) == fingerprint(second), "fixed-seed setup differs")

    restored, served_save = run("service-and-reload", load=snapshot, service=True)
    require("FL begin" not in restored and "FL funded" not in restored, "bootstrap ran on load")
    require(restored.count("CorporateAI operating the engine-built starter network") == 4,
            "not every AI resumed its controller")
    require(sorted(re.findall(r"CorporateAI profile ([1-4])", restored)) == ["1", "2", "3", "4"],
            "CorporateAI did not see its saved profile")
    require("Companies visible after startup: 5" in restored, "Game Script did not inspect the initialized companies")
    require(vehicle_count(restored) == [vehicles, vehicles], "initial or three-month saved-game connectivity check failed")
    served = re.findall(r"FL check company=\d+ vehicle=(\d+) type=\w+ connected=true served=true", restored)
    require(len(set(served)) == vehicles, "not every rival vehicle visited an ordered station")
    deliveries = re.findall(r"FL check company=(\d+).*delivered=([1-9]\d*)", restored)
    require(len({company for company, count in deliveries}) == 4, "not every rival delivered cargo")
    funds = lambda text: re.findall(r"FL check funds company=(\d+) balance=(-?\d+) loan=(\d+)", text)[:5]
    require(funds(first) == funds(restored), "funds changed across initial save/reload")
    require(served_save.exists(), "service snapshot missing")
    again, _ = run("served-save-reload", load=served_save)
    require("FL begin" not in again and vehicle_count(again) == [vehicles],
            "served network failed save/reload")

    modern, modern_save = run("modern-start", year=2026)
    require("FL complete companies=5 operational=4" in modern, "2026 start did not give four operational rivals")
    modern_built = set(re.findall(r"FL service company=\d+ recipe=(\S+) fallback=false result=success", modern))
    require({"conventional/airport_pair/town_pair", "conventional/rail_line/town_pair", "conventional/rail_line/industry"} <= modern_built,
            "2026 start should still get airlines and railways on modern track types")
    modern_vehicles = fleet(modern)
    modern_run, _ = run("modern-service", load=modern_save, service=True)
    modern_served = re.findall(r"FL check company=\d+ vehicle=(\d+) type=\w+ connected=true served=true", modern_run)
    require(vehicle_count(modern_run)[-1:] == [modern_vehicles] and len(set(modern_served)) == modern_vehicles,
            "not every 2026 vehicle visited an ordered station")

    mail = re.findall(r"FL service company=\d+ recipe=conventional/(rail_line|road_route)/mail fallback=false result=success", first)
    require(set(mail) == {"rail_line", "road_route"}, "mail trains and mail trucks should run from day zero")
    require(re.search(r"FL route company=\d+ kind=mail cargo=MAIL ", first), "mail trucks were not built for mail")

    coast, coast_save = run("coastal-world", coastal=True)
    require(re.search(r"FL ship company=\d+ kind=ferry cargo=PASS vehicles=\d+", coast), "no ferry was built in a coastal world")
    require(re.search(r"FL service company=\d+ recipe=conventional/dock_pair/town_pair fallback=false result=success", coast), "the ferry recipe did not succeed")
    coast_vehicles = fleet(coast)
    coast_run, _ = run("coastal-service", load=coast_save, service=True)
    coast_served = re.findall(r"FL check company=\d+ vehicle=(\d+) type=\w+ connected=true served=true", coast_run)
    require(re.search(r"FL check company=\d+ vehicle=\d+ type=water connected=true served=true", coast_run), "the ferry did not reach its docks")
    require(vehicle_count(coast_run)[-1:] == [coast_vehicles] and len(set(coast_served)) == coast_vehicles,
            "not every vehicle in the coastal world visited an ordered station")

    # Freight that no single mode reaches goes in two legs: here ships to a port, where they transfer and
    # leave empty, then a train from a station joined to the port. The planner picks the legs itself.
    transfer, transfer_save = run("transfer-world", coastal=True, map_bits=9, seed=3)
    leg = re.search(r"FL rail company=(\d+) kind=freight cargo=(\w+) track=\w+ vehicle=(\d+)[^\n]*\n(?:[^\n]*\n){0,2}?[^\n]*"
                    r"FL industries company=\1 source=\d+ chain=\w+ destination=industry:\d+ cargo=\2 tier=\w+ legs=dock_pair>rail_line hub=\d+", transfer)
    require(leg, "no ship-then-train route was built in a large coastal world")
    require(re.search(r"FL service company=\d+ recipe=conventional/dock_pair/industry fallback=false result=success", transfer),
            "the dock pair freight recipe did not succeed")
    transfer_run, _ = run("transfer-service", load=transfer_save, service=True, months=12)
    train = re.findall(rf"FL check company={leg.group(1)} vehicle={leg.group(3)} type=rail connected=true served=true tile=\d+ profit=(-?\d+)", transfer_run)
    require(train and int(train[-1]) > 0, "the transfer train did not earn by delivering what the ships left at the port")

    tiny, _ = run("tiny-world", map_bits=6, towns=2, industries=0)
    tiny_grant = int(re.search(r"FL market population=\d+ towns=2 industries=0 grant=(\d+)", tiny).group(1))
    require(tiny_grant < 500000, "two villages should not fund rivals with millions")
    require(not re.search(r"recipe=\w+/(rail_line|airport_pair)/\w+ fallback=\w+ result=success", tiny),
            "two villages should not get railways or airlines")

    # A NewGRF that adds a tech family, a rival, a cargo meaning, a rule for a built-in rival, and mistakes.
    grf_text, _ = run("newgrf-tech-tree", limit=5, grfs=[ROOT / "tests/future_logistics/grf/fl_test.grf"],
                      commands="future_logistics_tech recipes\nfuture_logistics_tech validate\nfuture_logistics_tech list\n"
                               "future_logistics_tech find test_rocket\nfuture_logistics_tech find test_airliner\n")
    require("FL catalogue families=3 profiles=5" in grf_text, "the NewGRF's families and profile were not read")
    require("FL profile company=5 profile=testcorp" in grf_text, "the NewGRF's rival was not created")
    require(re.search(r"FL service company=5 recipe=testjet/airport_pair/town_pair fallback=false result=success", grf_text),
            "the NewGRF's rival did not build with the NewGRF's tech family")
    require(re.search(r"FL plan company=2 recipes=conventional/airport_pair/town_pair,testjet/airport_pair/town_pair", grf_text),
            "the NewGRF's rule for AeroLink was not added after its own rules")
    require(re.search(r"FL tech family key=testrocket declared=true .* recipes=- ", grf_text), "unsupported recipe was not skipped")
    require(re.search(r"FL tech vehicle id=\S+ mode=road track=ROAD cargo=MAIL .*roles=parcel ", grf_text), "the cargo meaning was not applied")
    for problem in ["cannot understand tag 'fl_tier/high'", "fl_ tags ignored because fl_format/1 or fl_format/2 is missing",
                    "'fl_profile/testcorp/adopt/testjet/teleporter/any/0': this build of the game has no such mechanic",
                    "'fl_family/testrocket/mechanic/launch_site': this build of the game has no such mechanic",
                    "airport_pair cannot serve the feeder market yet",
                    "no loaded track type or airport has fl_provides/testpad", "adopts unknown family 'nosuchfamily'",
                    "fl_id/test_airliner is used by 2 items of kind vehicle"]:
        require(f"FL tech problem" in grf_text and problem in grf_text, f"validate did not report: {problem}")
    require("FL tech validate problems=8" in grf_text, "validate reported unexpected problems")
    require(re.search(r"FL tech vehicle id=\S+:rail:0 mode=rail .*roles=freight_bulk,freight_general\(declared\) power=steam ", grf_text),
            "the shared role/ and power/ badges were not read from a vehicle without fl_ tags")
    require(re.search(r"FL tech found id=test_rocket kind=vehicle index=\d+", grf_text) and "FL tech find id=test_rocket matches=1" in grf_text,
            "fl_id lookup did not find the tagged vehicle")
    require("FL tech find id=test_airliner matches=2" in grf_text, "fl_id lookup did not find both items sharing an id")
    require("FL check result=true companies=6" in grf_text, "a NewGRF rival's network failed the connectivity check")

    default_world, _ = run("default-world", default_world=True)
    require("FL complete companies=5 operational=4" in default_world, "default terrain/tree/town settings failed")
    no_clearance, _ = run("no-cleanup-capacity", clear_limit=0)
    require("FL complete companies=5 operational=0" in no_clearance and "FL construction failed" not in no_clearance,
            "bootstrap constructed a route without rollback capacity")
    disabled, old_save = run("disabled", enabled=False)
    require("FL begin" not in disabled and "FL check result=true companies=1 vehicles=0 human=0" in disabled,
            "disabled bootstrap modified the game")
    old_load, _ = run("ordinary-save-load", load=old_save, enabled=True)
    require("FL begin" not in old_load and "FL check result=true companies=1 vehicles=0 human=0" in old_load,
            "ordinary save was bootstrapped on load")
    limited, _ = run("competitor-limit", limit=2, capital=1000000)
    require("FL complete companies=3" in limited and limited.count("FL funded company=") == 2,
            "competitor limit not respected")
    require("grant=1000000" in limited, "configured grant ignored")
    failed, _ = run("one-town-fallback", towns=1, industries=0)
    require("FL complete companies=5 operational=0" in failed and failed.count("FL no usable starter network") == 4,
            "unfavourable map did not fail cleanly")
    rolled_back, _ = run("vehicle-command-failure", vehicle_limit=0)
    require("FL complete companies=5 operational=0" in rolled_back, "vehicle cap failure was not bounded")
    require("phase=vehicle" in rolled_back, "vehicle construction failure path was not exercised")
    clean = re.findall(r"FL check funds company=([1-4]).*road_bits=0 station_tiles=0 rail_bits=0", rolled_back)
    require(len(clean) == 4, "failed vehicle purchase left orphaned infrastructure")
    early, _ = run("no-available-bus", year=1800)
    require("FL complete companies=5 operational=0" in early, "vehicle availability not respected")
    _, late_save = run("late-start", year=2050, enabled=False, limit=0, towns=1)
    late_news, late_announced = run("late-start-news", load=late_save, enabled=False, service=True)
    require(len(re.findall(r"Published roadmap milestone (2022|2035|2050)", late_news)) == 3,
            "late-start milestone catch-up failed")
    persisted_news, _ = run("milestone-persistence", load=late_announced, enabled=False, service=True)
    require("Published roadmap milestone" not in persisted_news, "milestone news duplicated after reload")
    multiplayer, _ = run("dedicated-multiplayer", dedicated=True)
    require("FL begin" not in multiplayer and "FL funded" not in multiplayer,
            "multiplayer executed bootstrap")
    for climate in CLIMATES:
        listing, _ = run(f"tech-{climate}", enabled=False, limit=0, towns=1, climate=climate,
                         commands="future_logistics_tech list\n")
        lines = re.findall(r"FL tech (?:vehicle|infra|summary)[^\n]*", listing)
        require(lines and lines[-1].startswith("FL tech summary"), f"{climate}: library listing missing")
        vehicles = [line for line in lines if line.startswith("FL tech vehicle")]
        require(not [line for line in vehicles if "roles=-" in line and " capacity=0 " not in line],
                f"{climate}: a vehicle that carries cargo has no inferred role")
        require(any("mode=road" in line and "passenger_feeder" in line and "buildable=true" in line for line in vehicles),
                f"{climate}: no buildable bus in 1950")
        snapshot_file = SNAPSHOTS / f"{climate}.txt"
        text = "\n".join(lines) + "\n"
        if args.update_snapshots:
            SNAPSHOTS.mkdir(parents=True, exist_ok=True)
            snapshot_file.write_text(text)
        else:
            require(snapshot_file.exists() and snapshot_file.read_text() == text,
                    f"{climate}: Tech Library differs from {snapshot_file.relative_to(ROOT)}; "
                    "rerun with --update-snapshots if the change is intended")

    summary = "PASS: mail trains and trucks; ferries in a coastal world; freight routes in two legs with a transfer (ship then train through a port); NewGRF tech family, rival, cargo meaning and validation; headstart scaled to world population and town size; four distinct company profiles with an airline, passenger and freight rail, feeder buses into railway stations, trucks continuing rail cargo chains, and tier-sized fleets; Tech Library bus choice, console query and per-climate snapshots; company ownership/count; funding/loan accounting; four connected starter networks with controlled, default-world and 2026 settings; reserved cleanup capacity; fixed-seed reproducibility; station visits after simulation; network save/reload; no bootstrap on ordinary save load; disabled setting; competitor limit/custom capital; one-town/no-bus fallback and construction rollback; late-start milestone catch-up and persistence; dedicated multiplayer exclusion.\n"
    (output / "results.txt").write_text(summary)
    print(summary)


if __name__ == "__main__":
    main()
