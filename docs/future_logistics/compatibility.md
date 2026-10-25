# Compatibility with third-party NewGRFs

Checked 23 October 2026 against four widely used open-source sets by andythenorth,
built from their GitHub sources, and against the NewGRF specification wiki
(newgrf-specs.tt-wiki.net: Action0/Badges, NML:Badges, CargoTypes, the
standardized railtype and roadtype schemes).

| Set | Content | Built | Badges |
|---|---|---|---|
| FIRS (master, 2026) | 122 cargos, 116 industries | yes, NML 0.9.0 | `LAND`, `SEA` on objects |
| Iron Horse (master, 2026) | trains, wagons, 6 rail types | yes, NML master (needs features newer than 0.9.0) | 1,502: `role/`, `power/`, `livery/`, public `ih_*`, private `_ih_*` |
| Unsinkable Sam (2026) | ships | yes, NML 0.9.0 | none |
| Road Hog (2022) | road vehicles | no: its bundled helper library no longer matches its vehicles; checked from source only | none (predates badges) |

## What the standards are

- **Vehicles, cargos, industries, track and road types** are defined by the
  NewGRF specification. Vehicles declare speed, power, capacity, introduction
  date, track type and refits. Refits are given mostly by **cargo class**
  (bulk, liquid, piece goods, express...), plus allow/deny lists of labels.
- **Cargo labels** are four characters with a shared registry on the wiki
  (148 labels). Sets also invent labels the registry does not list yet: FIRS
  uses 45 of them, and Iron Horse and Unsinkable Sam know the same ones, so
  in practice the shared list is kept in step between sets rather than on the
  wiki. Classes are what make unknown labels work with other sets' vehicles.
- **Rail and road type labels** follow a four-letter scheme (gauge, speed,
  axle load, power: `RAIL`, `ELRL`, `NAAN`...). Iron Horse uses it for narrow
  gauge and adds its own `LGVN`, `LGVE`, `MTRO`.
- **Badges** (OpenTTD 15) are labels of the form `class/name`; deeper paths
  are allowed (`livery/uk/british_rail/blue`), and a label without a `/` is a
  class. The proposed shared classes are `flag`, `role`, `manufacturer`,
  `operator`, `power` and `livery`, and authors are asked to reuse labels other
  sets already use. In practice sets namespace their own public classes
  (`ih_gen`) and start private ones with `_` (`_ih_wagon_subtype`).

## How Future Logistics fits

- The `fl_` labels follow the badge rules: a namespaced public class, then
  `/`-separated parts. Class-only labels and `fl_cargo/<LABEL>` parents are
  accepted. Other sets' badges are ignored: with Iron Horse's 1,502 badges,
  `future_logistics_tech validate` reported no problems.
- Cargo meaning comes from standard labels and classes; `fl_cargo/<LABEL>/role/...`
  only adds to it. Track and road types are chosen by availability and speed,
  not by label, so non-standard labels work.
- Untagged vehicles work. With FIRS, Iron Horse and Unsinkable Sam loaded
  (512×512 coastal temperate world, seed 3, 1950), all four rivals built 11
  services: Iron Horse trains with the right Iron Horse wagon for scrap metal,
  engineering supplies, passengers and mail; livestock trucks; Unsinkable Sam
  ships carrying fish from a FIRS fishing ground (the first direct freight ship
  seen in testing) and a Sam ferry; engineering supplies by air. After six
  simulated months every vehicle had reached its stations, and everything but the two freight
ships had made a profit.

## Problems found and fixed

- **Industries that produce through a callback** (FIRS does this; the spec
  allows it) have no fixed production rate. The planner read that rate, so
  every FIRS industry looked as if it produced nothing and every freight
  service was rejected. Production is now taken from last month's production
  history when there is no fixed rate. The game fills that history in for
  every industry when the world is generated, by running the callback once,
  so it is right on day zero.
- **Level crossings**: `future_logistics_check` stopped following a railway at
  a road that a town later built across it, and reported the train as
  disconnected. It now follows the track through crossings.
- The rail log now names the locomotive and wagon (`locomotive=`, `wagon=`).

## Shared badges

- **`role/` and `power/` are now read** (see `tech_format.md`). Iron Horse marks
  its engines `role/express`, `role/freight`, `role/universal` and so on. Before,
  every rival picked the fastest engine (a `role/express`, `role/universal` steam
  engine) for freight too. Now freight trains get the `role/freight` engine
  (0-6-0 Lamia) and passenger trains keep the universal one; over six months
  both freight trains earned more than with the express engine (0.59M against
  0.22M, and 1.35M against 0.99M). `validate` still reports no problems.

## Still to note

- The Tech Library now counts every articulated part of a train or road vehicle,
  as the purchase list does: Iron Horse's wagons went from 2 to 7–11. Capacities
  that a set sets per cargo through callbacks may still read low.
- Iron Horse's wagons carry no `role/` badges, so a wagon whose default cargo is
  passengers but which is really a vehicle transporter can be picked for
  passenger trains. Rechecked on 24 October: in 1950 the Tech Library reads
  Iron Horse's Passenger Coaches as carrying 6 to 14 passengers, about the same
  as its Vehicle Transporter (7) and Motorail Vans (12), so the cheaper
  transporter wins on score. Whether those coach capacities are right (Iron
  Horse sets them through callbacks) needs checking against the purchase list
  in a game with graphics. Reading more of a set's own badges would help.

## For the Future Logistics NewGRF

These are now rules in `tech_format.md` ("Fitting in with community content").


- Give vehicles the shared badges as well as `fl_` ones: `role/...`,
  `power/...` (for example `power/electric`, `power/hydrogen`).
- Reuse registered cargo labels where they fit. `O2__` (oxygen, liquid) exists
  and suits liquid oxygen for rockets. `FUEL` is taken: it means natural gas, so
  rocket fuel needs a new label with the liquid and hazardous classes.
- Give new track types labels that follow the railtype scheme where possible,
  so other sets can fall back sensibly.

## Reproducing

```sh
git clone --depth 1 https://github.com/andythenorth/firs
git clone --depth 1 https://github.com/andythenorth/iron-horse
git clone --depth 1 https://github.com/andythenorth/unsinkable-sam
pip install chameleon markdown pillow numpy
# FIRS: make grf
# Iron Horse and Unsinkable Sam need a tag on HEAD for their version scripts
# (git tag horse/9.9.9, git tag sam/9.9.9), then run make until the NML is
# generated and compile it directly, for example:
#   nmlc -l generated/lang/iron-horse --palette=DEFAULT --no-palette-validation \
#        --grf=generated/iron-horse.grf generated/iron-horse.nml
# Iron Horse needs NML from its GitHub master branch.
```

Load the three .grf files (FIRS first) and run `future_logistics_tech validate`
and `future_logistics_tech list`, then start a 512×512 coastal game.
