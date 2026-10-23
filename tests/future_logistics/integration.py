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
            climate="temperate", commands="", industries=4):
        folder = output / name
        folder.mkdir(exist_ok=True)
        for src, dst in [(baseset, folder / "baseset"),
                         (ROOT / "mods/ai", folder / "ai"),
                         (ROOT / "mods/gamescripts", folder / "game")]:
            shutil.copytree(src, dst, dirs_exist_ok=True, ignore_dangling_symlinks=True)
        cfg = folder / "openttd.cfg"
        cfg.write_text(f"""[game_creation]
bootstrap_enabled = {str(enabled).lower()}
bootstrap_capital = {capital}
landscape = {climate}
map_x = 8
map_y = 8
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
[game_scripts]
DayZeroMonopolies =
""")
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
            (scripts / "month1.scr").write_text("schedule on-next-calendar-month scripts/month2.scr\n")
            (scripts / "month2.scr").write_text("schedule on-next-calendar-month scripts/month3.scr\n")
            (scripts / "month3.scr").write_text("future_logistics_check\nsave " + str(folder / "snapshot") + "\nquit\n")
        else:
            startup += "save " + str(folder / "snapshot") + "\nquit\n"
        (scripts / "game_start.scr").write_text(startup)
        driver = "null:ticks=9000" if service else ("null:ticks=1" if dedicated else "sdl")
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
    vehicle_count = lambda text: [int(n) for n in re.findall(r"FL check result=true companies=5 vehicles=(\d+) human=0", text)]
    profiles = re.findall(r"FL profile company=([1-4]) profile=(\w+)", first)
    require(profiles == [("1", "national_rail"), ("2", "aerolink"), ("3", "metro_regional"), ("4", "integrated_logistics")],
            "each rival should get its own profile, in order")
    built = set(re.findall(r"FL service company=\d+ type=(\w+) fallback=\w+ result=success", first))
    require({"rail_passenger", "rail_freight", "bus"} <= built,
            "the starting market should contain passenger rail, freight rail and buses")
    freight = re.findall(r"FL rail company=\d+ kind=freight cargo=(\w+) vehicle=\d+ wagons=([1-9])", first)
    require(freight, "no freight train with wagons was built")
    chosen = re.findall(r"FL tech company=([1-4]) role=passenger_feeder vehicle=(\S+)", first)
    require(chosen and len({vehicle for _, vehicle in chosen}) == 1,
            "every bus should come from the Tech Library")
    best = re.search(r"FL tech best id=(\S+) mode=road", first)
    require(best and best.group(1) == chosen[0][1], "console best-vehicle query disagrees with the planner")
    require("FL complete companies=5 operational=4" in first, "expected four operational rivals; see new-game/process.log")
    services = int(re.search(r"FL complete companies=5 operational=4 services=(\d+)", first).group(1))
    require(services >= 4 and vehicle_count(first) == [services], "initial ownership/connectivity failed")
    grants = re.findall(r"FL funded company=(\d+) grant=(\d+) balance=(\d+) loan=(\d+)", first)
    require(len(grants) == 4, "expected exactly four funding entries")
    require(all(int(grant) == 25000000 and int(balance) == int(loan) + int(grant)
                for _, grant, balance, loan in grants), "funding/loan accounting mismatch")
    ledger = re.findall(r"FL check funds company=([1-4]).*other_expense=-25000000 ai=true", first)
    require(len(ledger) == 4, "grant ledger or AI company type is incorrect")
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
    require(vehicle_count(restored) == [services, services], "initial or three-month saved-game connectivity check failed")
    served = re.findall(r"FL check company=\d+ vehicle=(\d+) type=\w+ connected=true served=true", restored)
    require(len(set(served)) == services, "not every rival vehicle visited an ordered station")
    deliveries = re.findall(r"FL check company=(\d+).*delivered=([1-9]\d*)", restored)
    require(len({company for company, count in deliveries}) == 4, "not every rival delivered cargo")
    funds = lambda text: re.findall(r"FL check funds company=(\d+) balance=(-?\d+) loan=(\d+)", text)[:5]
    require(funds(first) == funds(restored), "funds changed across initial save/reload")
    require(served_save.exists(), "service snapshot missing")
    again, _ = run("served-save-reload", load=served_save)
    require("FL begin" not in again and vehicle_count(again) == [services],
            "served network failed save/reload")

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

    summary = "PASS: four distinct company profiles with passenger rail, freight rail and buses; Tech Library bus choice, console query and per-climate snapshots; company ownership/count; funding/loan accounting; four connected starter networks with controlled and default world settings; reserved cleanup capacity; fixed-seed reproducibility; station visits after simulation; network save/reload; no bootstrap on ordinary save load; disabled setting; competitor limit/custom capital; one-town/no-bus fallback and construction rollback; late-start milestone catch-up and persistence; dedicated multiplayer exclusion.\n"
    (output / "results.txt").write_text(summary)
    print(summary)


if __name__ == "__main__":
    main()
