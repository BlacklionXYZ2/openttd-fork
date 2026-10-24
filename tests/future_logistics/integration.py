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
            climate="temperate", commands="", industries=4, map_bits=8, grfs=(), coastal=False, months=3, mountains=False, rivers=False, game_script=None):
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
        # Always a NewGRF list, even an empty one: without it, new games use the shipped Future Logistics NewGRFs.
        cfg.write_text(cfg.read_text() + "[newgrf]\n" + "".join(f"{grf.name} = \n" for grf in grfs))
        if game_script:
            shutil.copytree(game_script, folder / "game" / game_script.name, dirs_exist_ok=True)
            cfg.write_text(cfg.read_text().replace("DayZeroMonopolies =", f"{game_script.name} ="))
        if coastal:
            text = cfg.read_text().replace("land_generator = 0", "land_generator = 1").replace("quantity_sea_lakes = 4", "quantity_sea_lakes = 3")
            cfg.write_text(text.replace("terrain_type = 0", "terrain_type = 1").replace("tree_placer = 0", "tree_placer = 2"))
        if default_world:
            text = cfg.read_text().replace("land_generator = 0", "land_generator = 1")
            text = text.replace("tree_placer = 0", "tree_placer = 2").replace("number_towns = 4", "number_towns = 2")
            text = text.replace("terrain_type = 0", "terrain_type = 1").replace("quantity_sea_lakes = 4", "quantity_sea_lakes = 0")
            cfg.write_text(text)
        if mountains:
            text = cfg.read_text().replace("land_generator = 0", "land_generator = 1").replace("tree_placer = 0", "tree_placer = 2")
            cfg.write_text(text.replace("terrain_type = 0", "terrain_type = 3").replace("quantity_sea_lakes = 4", "quantity_sea_lakes = 0"))
        if rivers:
            text = cfg.read_text().replace("land_generator = 0", "land_generator = 1").replace("tree_placer = 0", "tree_placer = 2")
            text = text.replace("terrain_type = 0", "terrain_type = 2").replace("quantity_sea_lakes = 4", "quantity_sea_lakes = 0")
            cfg.write_text(text.replace("[game_creation]\n", "[game_creation]\namount_of_rivers = 3\n"))
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
    # Rivals grow after day zero, so later checks need the day-zero vehicles to keep working and the fleet not to shrink.
    vehicle_ids = lambda text: set(re.findall(r"FL check company=\d+ vehicle=(\d+) ", text))
    served_ids = lambda text: set(re.findall(r"FL check company=\d+ vehicle=(\d+) type=\w+ connected=true served=true", text))
    # A train destroys a road vehicle caught on a level crossing, as in any game; CorporateAI reports it.
    crashed_ids = lambda text: set(re.findall(r"Vehicle (\d+) crashed at tile", text))
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
    require(restored.count("CorporateAI profile") == 4, "not every AI resumed its controller")
    require(sorted(re.findall(r"CorporateAI profile ([1-4])", restored)) == ["1", "2", "3", "4"],
            "CorporateAI did not see its saved profile")
    require("Companies visible after startup: 5" in restored, "Game Script did not inspect the initialized companies")
    counts = vehicle_count(restored)
    require(len(counts) == 2 and counts[0] == vehicles and counts[1] >= vehicles, "initial or three-month saved-game connectivity check failed")
    require(vehicle_ids(first) - crashed_ids(restored) <= served_ids(restored), "not every day-zero rival vehicle visited an ordered station")
    deliveries = re.findall(r"FL check company=(\d+).*delivered=([1-9]\d*)", restored)
    require(len({company for company, count in deliveries}) == 4, "not every rival delivered cargo")
    funds = lambda text: re.findall(r"FL check funds company=(\d+) balance=(-?\d+) loan=(\d+)", text)[:5]
    require(funds(first) == funds(restored), "funds changed across initial save/reload")
    require(served_save.exists(), "service snapshot missing")
    again, _ = run("served-save-reload", load=served_save)
    require("FL begin" not in again and vehicle_count(again) == counts[-1:],
            "served network failed save/reload")

    # Phase 6: CorporateAI keeps the rivals going with the day-zero planners through AIFutureLogistics.
    grown, _ = run("rivals-grow", load=snapshot, service=True, months=13)
    require(re.search(r"FL grow company=\d+ profile=\w+ recipe=\S+ result=success", grown), "no rival opened a new route after day zero")
    require(re.search(r"Opened a new \S+ route", grown), "CorporateAI did not report its new route")
    require(re.search(r"Added a vehicle to .* \(\d+ on the route\)", grown), "no rival added a vehicle to a busy route")
    grown_counts = vehicle_count(grown)
    require(len(grown_counts) == 2 and grown_counts[-1] > vehicles, "the rivals' fleets did not grow, or a vehicle failed the check")
    require(vehicle_ids(first) - crashed_ids(grown) <= served_ids(grown), "a day-zero vehicle stopped serving its stations as the rivals grew")
    largest = max(int(n) for n in re.findall(r"\((\d+) on the route\)", grown))
    require(largest <= 5, "a route grew past the medium aggression limit of five vehicles")

    modern, modern_save = run("modern-start", year=2026)
    require("FL complete companies=5 operational=4" in modern, "2026 start did not give four operational rivals")
    modern_built = set(re.findall(r"FL service company=\d+ recipe=(\S+) fallback=false result=success", modern))
    require({"conventional/airport_pair/town_pair", "conventional/rail_line/town_pair", "conventional/rail_line/industry"} <= modern_built,
            "2026 start should still get airlines and railways on modern track types")
    modern_vehicles = fleet(modern)
    modern_run, _ = run("modern-service", load=modern_save, service=True)
    modern_served = re.findall(r"FL check company=\d+ vehicle=(\d+) type=\w+ connected=true served=true", modern_run)
    require(vehicle_count(modern_run)[-1] >= modern_vehicles and vehicle_ids(modern) <= set(modern_served),
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
    require(vehicle_count(coast_run)[-1] >= coast_vehicles and vehicle_ids(coast) <= set(coast_served),
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

    # The Future Logistics NewGRF itself: drones arrive in 2022 and rivals adopt them for mail.
    fl_grf = ROOT / "mods/newgrf/future_logistics/future_logistics.grf"
    # The committed .grf must match its source; checked when an NML compiler is available ($NMLC or nmlc).
    nmlc = os.environ.get("NMLC") or shutil.which("nmlc")
    if nmlc:
        rebuilt = output / "future_logistics_rebuilt.grf"
        subprocess.run([nmlc, "-c", "-o", str(rebuilt), "future_logistics.nml"], cwd=fl_grf.parent, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        require(rebuilt.read_bytes() == fl_grf.read_bytes(), "future_logistics.grf is out of date: run mods/newgrf/future_logistics/build.sh")
    else:
        print("nmlc not found; not checking that future_logistics.grf matches its source", flush=True)
    drones, drone_save = run("drone-world", year=2026, grfs=[fl_grf],
                             commands="future_logistics_tech validate\nfuture_logistics_tech list\n")
    require("FL tech validate problems=0" in drones, "the Future Logistics NewGRF has declaration problems")
    require(re.search(r"FL tech vehicle id=464C0101:air:64 .*roles=parcel,express\(declared\) power=electric tech=drone ", drones),
            "the cargo drone was not read with its family, roles and power source")
    require(len(re.findall(r"FL tech infra kind=airport ", drones)) == len(set(re.findall(r"FL tech infra kind=airport id=(\S+)", drones))),
            "an airport was listed twice")
    for company in ("2", "3"):
        require(re.search(rf"FL service company={company} recipe=drone/airport_pair/mail fallback=false result=success", drones),
                f"company {company} did not adopt drones for mail")
    drone_run, _ = run("drone-service", load=drone_save, service=True, months=4, grfs=[fl_grf])
    flying = re.findall(r"FL check company=\d+ vehicle=\d+ type=air connected=true served=true .*engine=464C0101:air:64 ", drone_run)
    require(len(flying) >= 4, "the drones did not serve their drone ports")

    # Hyperloop arrives in 2035 on its own track type; National Rail adopts it five years later.
    tubes, tube_save = run("hyperloop-world", year=2041, grfs=[fl_grf], commands="future_logistics_tech validate\n")
    require("FL tech validate problems=0" in tubes, "the Future Logistics NewGRF has declaration problems in 2041")
    tube = re.search(r"FL rail company=1 kind=town cargo=\w+ track=HYPL vehicle=(\d+) .*locomotive=464C0101:rail:120 wagon=464C0101:rail:12[12]", tubes)
    require(tube and re.search(r"FL service company=1 recipe=hyperloop/rail_line/\w+ fallback=false result=success", tubes),
            "National Rail did not build a hyperloop line")
    tube_run, _ = run("hyperloop-service", load=tube_save, service=True, grfs=[fl_grf])
    require(re.search(rf"FL check company=1 vehicle={tube.group(1)} type=rail connected=true served=true ", tube_run),
            "the hyperloop pod did not serve its stations")

    # Rockets arrive in 2050 and fly between spaceports; AeroLink adopts them two years later.
    rockets, rocket_save = run("rocket-world", year=2056, map_bits=9, grfs=[fl_grf], commands="future_logistics_tech validate\n")
    require("FL tech validate problems=0" in rockets, "the Future Logistics NewGRF has declaration problems in 2056")
    require(re.search(r"FL towns company=2 a=\d+ b=\d+ tier=city population=\d+,\d+ aircraft=464C0101:air:65", rockets)
            and re.search(r"FL service company=2 recipe=rocket/airport_pair/town_pair fallback=false result=success", rockets),
            "AeroLink did not open a rocket route between cities")
    rocket_run, _ = run("rocket-service", load=rocket_save, service=True, map_bits=9, grfs=[fl_grf])
    require(re.search(r"FL check company=2 vehicle=\d+ type=air connected=true served=true .*engine=464C0101:air:65 ", rocket_run),
            "the rockets did not serve their spaceports")

    # Starship: a package of its own in the shared rocket family, with a launch site; AeroLink adopts it.
    ss_grf = ROOT / "mods/newgrf/starship/starship.grf"
    if nmlc:
        rebuilt = output / "starship_rebuilt.grf"
        subprocess.run([nmlc, "-c", "-o", str(rebuilt), "starship.nml"], cwd=ss_grf.parent, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        require(rebuilt.read_bytes() == ss_grf.read_bytes(), "starship.grf is out of date: run mods/newgrf/starship/build.sh")
    ships, ship_save = run("starship-world", year=2050, map_bits=9, grfs=[ss_grf],
                           commands="future_logistics_tech validate\nfuture_logistics_tech find starship\nfuture_logistics_tech find starship_launch_site\n")
    require("FL tech validate problems=0" in ships, "the Starship NewGRF has declaration problems")
    require(re.search(r"FL tech found id=starship kind=vehicle", ships) and re.search(r"FL tech found id=starship_launch_site kind=airport", ships),
            "the Starship or its launch site has no fl_id")
    require(re.search(r"FL towns company=2 a=\d+ b=\d+ tier=city population=\d+,\d+ aircraft=464C0102:air:64", ships)
            and re.search(r"FL service company=2 recipe=rocket/airport_pair/town_pair fallback=false result=success", ships),
            "AeroLink did not open a Starship route on its own")
    ship_run, _ = run("starship-service", load=ship_save, service=True, map_bits=9, grfs=[ss_grf])
    # Only the last check counts: the first, as the save loads, comes before any flying.
    final_check = ship_run.split("FL check result")[-2]
    starships = re.findall(r"FL check company=2 vehicle=\d+ type=air connected=(\w+) served=(\w+) .*engine=464C0102:air:64 ", final_check)
    require(starships and all(c == "true" and v == "true" for c, v in starships), "not every Starship served its launch sites")
    # Balance: Starship earns the most of any vehicle.
    profits = re.findall(r"FL check company=\d+ vehicle=\d+ type=\w+ .*profit=(-?\d+) .*engine=(\S+) ", final_check)
    require(max(profits, key=lambda p: int(p[0]))[1] == "464C0102:air:64", "a vehicle out-earned every Starship")
    # With the Future Logistics NewGRF: both declare the rocket family, and each built-in airport is listed once.
    both, _ = run("starship-with-future-logistics", year=2056, map_bits=9, grfs=[fl_grf, ss_grf],
                  commands="future_logistics_tech validate\nfuture_logistics_tech list\n")
    require("FL tech validate problems=0" in both, "Starship and Future Logistics together report declaration problems")
    # The launch site copies the country airport (4x3); the country airport itself must still be there, once.
    require(len(re.findall(r"FL tech infra kind=airport id=airport:\d+ speed=0 size=4x3", both)) == 1,
            "loading Starship replaces or duplicates the country airport")
    # Balance: Starship is the most expensive vehicle to buy and to run, and carries the most.
    vehicles = re.findall(r"FL tech vehicle id=(\S+) .*capacity=(\d+) .*cost=(\d+) running_cost=(\d+) name=", both)
    for column, what in ((2, "purchase cost"), (3, "running cost"), (1, "capacity")):
        top = max(vehicles, key=lambda v: int(v[column]))
        require(top[0] == "464C0102:air:64", f"Starship does not have the highest {what}: {top[0]} does")

    # Corporate Infrastructure: Gareth's decorative objects, loaded alongside Future Logistics.
    ci_grf = ROOT / "mods/newgrf/corporate_infrastructure/CorporateInfrastructure.grf"
    if nmlc:
        rebuilt = output / "corporate_infrastructure_rebuilt.grf"
        subprocess.run([nmlc, "--grf", str(rebuilt), "CorporateInfrastructure.nml"], cwd=ci_grf.parent, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        require(rebuilt.read_bytes() == ci_grf.read_bytes(),
                "CorporateInfrastructure.grf is out of date: run mods/newgrf/corporate_infrastructure/build.sh")
    objects, _ = run("corporate-infrastructure", year=2026, enabled=False, service=True, months=1, grfs=[fl_grf, ci_grf],
                     game_script=ROOT / "tests/future_logistics/gs/CIObjectCheck", commands="future_logistics_tech validate\n")
    require("CI objects built=6" in objects and "FL tech validate problems=0" in objects,
            "the Corporate Infrastructure objects could not all be built alongside Future Logistics")

    # Data centres: an industry in the "tech" market, one per town, that receives what banks hold.
    centres, _ = run("data-centre-world", year=2026, map_bits=9, grfs=[fl_grf],
                     commands="future_logistics_tech validate\nfuture_logistics_tech find data_centre\n")
    require("FL tech validate problems=0" in centres, "the data centre declaration has problems")
    require(re.search(r"FL tech found id=data_centre kind=industry index=\d+ built=[1-9]", centres), "no data centre was generated")

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
    # Railways climb and descend over sloped land rather than needing one flat height.
    mountain_world, _ = run("mountain-world", mountains=True, seed=1)
    slopes = [int(n) for n in re.findall(r"FL rail company=\d+ .* slopes=(\d+)", mountain_world)]
    require(len(slopes) >= 3 and sum(1 for n in slopes if n > 0) >= 2, "railways in a mountainous world should climb over slopes")
    require(re.search(r"FL rail company=\d+ .* tunnels=[1-9]", mountain_world), "no railway in a mountainous world went through a hill")
    require("FL check result=true" in mountain_world and "FL construction failed" not in mountain_world, "mountain railways failed to build or connect")
    # Lines bridge rivers, roads and railways in their way.
    river_world, _ = run("river-world", rivers=True, seed=1)
    spans = [int(n) for n in re.findall(r"FL (?:rail|route) company=\d+ .* bridges=(\d+)", river_world)]
    require(sum(spans) >= 3, "railways and roads in a world with rivers should use bridges")
    require("FL check result=true" in river_world and "FL construction failed" not in river_world, "bridged lines failed to build or connect")
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

    summary = "PASS: mail trains and trucks; Starship on its own and with Future Logistics; Corporate Infrastructure objects; railways over hills, through them and over rivers; ferries in a coastal world; freight routes in two legs with a transfer (ship then train through a port); NewGRF tech family, rival, cargo meaning and validation; drones, hyperloop and rockets from the Future Logistics NewGRF adopted by the rivals; data centres generated; headstart scaled to world population and town size; rivals that grow after day zero through AIFutureLogistics; four distinct company profiles with an airline, passenger and freight rail, feeder buses into railway stations, trucks continuing rail cargo chains, and tier-sized fleets; Tech Library bus choice, console query and per-climate snapshots; company ownership/count; funding/loan accounting; four connected starter networks with controlled, default-world and 2026 settings; reserved cleanup capacity; fixed-seed reproducibility; station visits after simulation; network save/reload; no bootstrap on ordinary save load; disabled setting; competitor limit/custom capital; one-town/no-bus fallback and construction rollback; late-start milestone catch-up and persistence; dedicated multiplayer exclusion.\n"
    (output / "results.txt").write_text(summary)
    print(summary)


if __name__ == "__main__":
    main()
