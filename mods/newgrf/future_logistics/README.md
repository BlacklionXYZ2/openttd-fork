# Future Logistics NewGRF

The technology that arrives over the decades in the Future Logistics game. Everything is
described with the shared tech format ([`docs/future_logistics/tech_format.md`](../../../docs/future_logistics/tech_format.md)),
so day-zero rivals and CorporateAI find and adopt it with no code naming it.

## Contents

| Item | What it is | Status |
|---|---|---|
| Drones (`fl_family/drone`) | Tech family, airport pair mechanic, mail market, town tier and up | Working |
| Cargo Drone (`fl_id/cargo_drone`) | Helicopter-type aircraft for mail and express cargo, from 2022 | Working; placeholder graphics (base-game Tricario helicopter) |
| Drone Port (`fl_id/drone_port`) | Airport for drones, from 2022 | Working; helistation layout and graphics, three pads |
| Hyperloop (`fl_family/hyperloop`) | Tech family on its own track type, rail line mechanic, passengers and mail, city tier and up | Working |
| Hyperloop Tube (`HYPL`, `fl_id/hyperloop_track`) | Track type, from 2035, no level crossings, compatible with nothing else | Working; plain rail graphics |
| Propulsion Pod, Passenger and Mail Capsules | The pod pulls capsules, as a locomotive pulls wagons | Working; base-game maglev graphics |
| Rockets (`fl_family/rocket`) | Tech family, airport pair mechanic, passengers, mail and high-value freight, city tier and up | Working |
| Reusable Rocket (`fl_id/rocket`) | Fast aircraft that needs a launch pad, from 2050 | Working; base-game Yate Haugan graphics, flies like an aircraft |
| Spaceport (`fl_id/spaceport`) | Airport with a launch pad, from 2050 | Working; city airport layout and graphics |
| Data Centre (`fl_industry/data_centre`, `fl_market/tech`) | Industry in towns that receives valuables, gold or diamonds; one per town | Generated and accepted; little to receive yet (see below); no revenue bonus |

AeroLink adopts drones two years after they arrive (its rule for any new air family);
Metro & Regional adopts them for mail after one year (a rule in this NewGRF).
National Rail adopts hyperloop five years after it arrives (its rule for any new rail family).
AeroLink adopts rockets two years after they arrive; Integrated Logistics uses them for freight
after three years (a rule in this NewGRF).

All numbers are placeholders until the balance pass; see [`PLACEHOLDERS.md`](PLACEHOLDERS.md).
The art from the Corporate Infrastructure handoff (drone port, cargo drone, distribution centre,
data centre, spaceport, rocket) is not used here yet; see "Art" below. Gareth's v0.1 package
turns six of the images into decorative objects: see
[`../corporate_infrastructure/`](../corporate_infrastructure/README.md). The rocket and empty
launch pad images are converted for [`../starship/`](../starship/README.md), a separate package
in the same rocket family.

## Known limits

- Rivals rarely serve data centres: temperate maps seldom start with banks, the only source of
  what data centres receive. Trucks can reach town-centre industries, using town roads and
  drive-through stops on them, once there is cargo to carry.
- The data centre's company revenue bonus, the distribution centre / cargo hub and parcels
  wait on decisions from the Corporate Infrastructure handoff (what "cargo" means, whether the
  hub creates or forwards cargo, how the bonus works).

## Building

```sh
python3 -m pip install nml==0.9.0
./build.sh                 # or NMLC=/path/to/nmlc ./build.sh
```

The built `future_logistics.grf` is committed, because the integration tests load it; rebuild
and commit it after changing the source. Put it in your OpenTTD `newgrf/` folder and enable it
before starting a new game.

## Notes for adding content

- Tag every item with `fl_format/2` and its `fl_` tags, plus the shared `role/` and `power/`
  badges (tech_format.md, "Fitting in with community content").
- The first NewGRF airport based on a built-in airport replaces it in OpenTTD. This file
  therefore defines unchanged copies of the helistation and the city airport first, and the
  drone port and spaceport after them. Do the same for any other built-in airport type you
  base one on.
- Airports that only take helicopters (heliports, helistations, drone ports) serve routes of
  12 to 64 tiles in the planners; other airports serve 48 to 256.
- OpenTTD airports have at most three helipads, so the four-pad drone port art needs an engine
  change before all four pads can work.

## Art

The handoff art (`Corporate_Infrastructure_NewGRF_Handoff.md`) is concept art at about
1254×1254 pixels. Before use it needs: one exact master image per building, game-scale
sprites at each zoom level, splitting into per-tile pieces, 8-bit company-colour masks, and
for vehicles a sprite for each direction. Known faults: the spaceport's empty and occupied
images differ outside the rocket, and the idle and powered rockets differ in size.
