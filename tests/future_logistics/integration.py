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


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--baseset", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "work/integration")
    args = parser.parse_args()
    binary, baseset, output = args.binary.resolve(), args.baseset.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    def run(name, *, seed=12345, load=None, enabled=True, limit=4, capital=25000000,
            towns=48, year=1950, service=False, dedicated=False):
        folder = output / name
        folder.mkdir(exist_ok=True)
        for src, dst in [(baseset, folder / "baseset"),
                         (ROOT / "mods/ai", folder / "ai"),
                         (ROOT / "mods/gamescripts", folder / "game")]:
            shutil.copytree(src, dst, dirs_exist_ok=True)
        cfg = folder / "openttd.cfg"
        cfg.write_text(f"""[game_creation]
bootstrap_enabled = {str(enabled).lower()}
bootstrap_capital = {capital}
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
industry_density = 0
vehicle_breakdowns = 0
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
        scripts = folder / "scripts"
        scripts.mkdir(exist_ok=True)
        startup = "future_logistics_check\n"
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

    first, snapshot = run("new-game")
    require("FL complete companies=5 operational=4" in first, "expected four operational rivals; see new-game/process.log")
    require("FL check result=true companies=5 vehicles=4 human=0" in first, "initial ownership/connectivity failed")
    grants = re.findall(r"FL funded company=(\d+) grant=(\d+) balance=(\d+) loan=(\d+)", first)
    require(len(grants) == 4, "expected exactly four funding entries")
    require(all(int(grant) == 25000000 and int(balance) == int(loan) + int(grant)
                for _, grant, balance, loan in grants), "funding/loan accounting mismatch")
    ledger = re.findall(r"FL check funds company=([1-4]).*other_expense=-25000000 ai=true", first)
    require(len(ledger) == 4, "grant ledger or AI company type is incorrect")
    require(snapshot.exists(), "new-game snapshot missing")
    second, _ = run("repeat-seed")
    fingerprint = lambda text: re.findall(r"FL (?:funded|route|complete)[^\n]*", text)
    require(fingerprint(first) == fingerprint(second), "fixed-seed setup differs")

    restored, served_save = run("service-and-reload", load=snapshot, service=True)
    require("FL begin" not in restored and "FL funded" not in restored, "bootstrap ran on load")
    checks = re.findall(r"FL check result=true companies=5 vehicles=4 human=0", restored)
    require(len(checks) == 2, "initial or three-month saved-game connectivity check failed")
    visits = re.findall(r"FL check company=(\d+).*connected=true served=true", restored)
    require(len(set(visits)) == 4, "not every rival bus visited an ordered station")
    deliveries = re.findall(r"FL check company=(\d+).*delivered=([1-9]\d*)", restored)
    require(len({company for company, count in deliveries}) == 4, "not every rival delivered passengers")
    funds = lambda text: re.findall(r"FL check funds company=(\d+) balance=(-?\d+) loan=(\d+)", text)[:5]
    require(funds(first) == funds(restored), "funds changed across initial save/reload")
    require(served_save.exists(), "service snapshot missing")
    again, _ = run("served-save-reload", load=served_save)
    require("FL begin" not in again and "FL check result=true companies=5 vehicles=4 human=0" in again,
            "served network failed save/reload")

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
    failed, _ = run("one-town-fallback", towns=1)
    require("FL complete companies=5 operational=0" in failed and failed.count("FL no usable bus route") == 4,
            "unfavourable map did not fail cleanly")
    early, _ = run("no-available-bus", year=1800)
    require("FL complete companies=5 operational=0" in early, "vehicle availability not respected")
    multiplayer, _ = run("dedicated-multiplayer", dedicated=True)
    require("FL begin" not in multiplayer and "FL funded" not in multiplayer,
            "multiplayer executed bootstrap")
    summary = "PASS: company ownership/count; funding/loan accounting; four connected started bus networks; fixed-seed reproducibility; station visits after simulation; network save/reload; no bootstrap on ordinary save load; disabled setting; competitor limit/custom capital; one-town/no-bus fallback; dedicated multiplayer exclusion.\n"
    (output / "results.txt").write_text(summary)
    print(summary)


if __name__ == "__main__":
    main()
