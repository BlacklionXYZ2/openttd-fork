# Future Logistics tech format (v2)

This is the contract between NewGRFs that add technology, industries or rival
companies, and the day-zero code that builds and runs the rivals. A NewGRF that
follows it is picked up automatically: adding a new vehicle, a whole new tech
tree, a new industry or a new rival needs no change to the game's code, as long
as it can be built with one of the existing **mechanics** (see below).

Version 1 tags (`fl_format/1`) keep working.

The format sits on top of the NewGRF specification and the shared badge
classes other NewGRFs use (see "Fitting in with community content"). The `fl_`
badges only add what the specification has no words for; everything the
specification already covers (cargo, track types, roles, power sources) is
written the standard way, so other NewGRFs, AIs and Game Scripts understand
Future Logistics content and Future Logistics understands theirs.

## How it fits together

| Layer | Provided by | What it is |
|---|---|---|
| Content | The NewGRF | Ordinary NewGRF items: vehicles, track and road types, airports, industries, cargo |
| Meaning | The NewGRF | `fl_` badges: OpenTTD's built-in tags, with the names defined here |
| Behaviour | The game's code | Mechanics, recipes and profiles, built from the badges at game start |

Badges are part of OpenTTD itself. A badge is a text label; the part before
the first `/` is its class. Labels are global across all loaded NewGRFs, so
`fl_tech/rocket` means the same thing whichever NewGRF uses it, and other
authors can join a tech family just by using its name. A NewGRF using these
badges is an ordinary NewGRF and still loads in unmodified OpenTTD.

There are two kinds of badge:

- **Tags** are attached to an item (a vehicle, track type, airport or industry)
  with its `badges` property.
- **Declarations** are badges that are not attached to anything. They describe
  things that are not a single item: tech families, rival companies and cargo
  meanings. Listing a declaration in the NewGRF's `badgetable` is enough for
  the game to see it. To give a family or rival a display name, also define
  the badge with `item(FEAT_BADGES, ...)` and a `name` property.

Every label is lower case, with `/` between parts. Names (`<family>`,
`<profile>`, `<site>`, `<market>`, `<kind>`) use letters, digits and `_`.
Cargo labels keep their usual four characters, such as `DATA`.

## Building blocks

### Mechanics

A mechanic is a way of building a service. Mechanics are the only part that is
code, because they decide where and how things are built. Each one is
registered by name; a NewGRF refers to it by that name (see "Linking code and
content" below).

| Mechanic | Builds | Vehicles |
|---|---|---|
| `rail_line` | two terminus stations, a single line, a depot, one train | rail |
| `road_route` | two drive-through stops, an owned road, a depot, a small fleet | road |
| `airport_pair` | two airports and a small fleet | air |
| `dock_pair` | two docks on the same water, a ship depot and a small fleet; an industry with its own dock, such as an oil rig, is used as it is | water |

### Freight routes and transfers

A freight route is a cargo, a source, a destination and one or two **legs**.
Every mechanic that serves the `industry` market can build a leg: vehicles that
full-load at one end and either deliver at the other or **transfer and leave
empty** there. Transfers are OpenTTD's own: cargo left at a station waits until a
vehicle with an order to load it arrives. Nothing is added to the game; a
transfer station is an ordinary station, with the second leg's platforms, stops,
dock or airport joined to the first leg's.

For each freight recipe the planner:

1. lists routes (chains first, then the rival's preferred markets, then shorter
   ones);
2. tries the recipe's mechanic alone, within its distance range;
3. otherwise tries two legs, with the recipe's mechanic as the long leg and each
   of the rival's **other freight mechanics** as the short one. That short leg
   either finishes the route from a transfer station near the destination (ships
   to a port, then a train inland) or starts it with a transfer station near the
   source (a train from an inland field to a port, then ships).

A transfer station is only chosen where the second leg is known to be buildable
from it, and the first leg is removed again if the second still fails. So a
rival can only hand cargo to transport it has adopted for freight. A NewGRF that
adds a family with a freight-carrying mechanic (or a new mechanic with a leg
builder in code) takes part in transfers with no further declarations.

A new kind of construction, such as pads on rooftops, needs a new mechanic in
code.

### Markets

A market is what a service connects.

| Market | Meaning | Mechanics |
|---|---|---|
| `town_pair` | passengers between two towns | all |
| `mail` | mail between two towns; road vehicles use truck stops | all |
| `industry` | freight from a producing industry to an industry (any mechanic) or a town (`road_route`) that accepts it; vehicles full-load at the source | all |
| `feeder` | passengers from a nearby town into one of the company's railway stations | `road_route` |

Freight sources are primary industries that produce on their own, or secondary
industries (factories and so on) that another rival's freight service already
supplies, so services can form cargo chains.

A recipe for a combination not in this table is reported by
`future_logistics_tech validate` and skipped.

### Families and recipes

A **tech family** groups vehicles and infrastructure that belong together, such
as rockets and launchpads. Everything without an `fl_tech` tag belongs to the
built-in family `conventional`.

A **recipe** is one family, one mechanic and one market, for example
`rocket/airport_pair/town_pair`. Recipes are built automatically from the
families. The built-in family `conventional` has a recipe for every market of
every mechanic in the table above.

A recipe only uses vehicles of its own family. It only builds on track types and
airports that are untagged or tagged with its own family, so ordinary railways
never end up on a hyperloop guideway. A vehicle with `fl_needs/<site>` is only
used where the track type or airport has a matching `fl_provides/<site>`.

### Market tiers

Each route is classed by the smaller town it serves, or by the source
industry's production for freight: `village`, `town`, `city` or
`metropolis`. The tier limits what a route may use (see the roadmap's headstart
scaling). A family can raise the minimum with `min_tier`.

## Tags on items

| Tag | On | Meaning | Default |
|---|---|---|---|
| `fl_format/2` | anything tagged | Item follows this version; `fl_format/1` also accepted | required for the other `fl_` tags to count |
| `fl_id/<name>` | any item that can carry badges | A stable name code can find the item by; one per item, and unique within its kind | none |
| `fl_tech/<family>` | vehicle, track type, airport | Family the item belongs to | `conventional` |
| `fl_tier/<0-9>` | vehicle | Progression level | `0` |
| `fl_role/<role>` | vehicle | Jobs it suits; several allowed, and they replace the inferred roles | inferred from cargo |
| `fl_needs/<site>` | vehicle | Only usable where a matching site is provided | none |
| `fl_provides/<site>` | track type, airport | Supplies that site | none |
| `fl_ai/avoid` | vehicle | Rivals never buy it | off |
| `fl_ai/early_adopter` | vehicle | Only used by adoption rules with a delay of 0 | off |
| `fl_industry/<kind>` | industry | What the industry is, for display and validation | none |
| `fl_market/<market>` | industry | Market group, so rivals can prefer it (`fl_profile/.../industries/...`) | none |

Two shared badge classes from the NewGRF specification are also read, from any
NewGRF and without `fl_format`:

| Badge | Meaning here |
|---|---|
| `role/<name>` | The job the NewGRF says the vehicle is for; narrows the inferred roles (table below) |
| `power/<source>` | Power source, such as `power/electric`; shown by `list` |

Roles are decided in this order: `fl_role`; then the `role/` badges, which
narrow the inferred roles to those they name (`role/universal` keeps them all,
and a `role/` badge that contradicts the cargo is ignored); then inference. A
vehicle whose roles come from `fl_role` or a specific `role/` badge is
**declared** for them, and is preferred over one that only might do the job,
however fast. So a train set's `role/freight` engine pulls freight even if its
`role/express` engine is faster.

Roles: `passenger_intercity`, `passenger_commuter`, `passenger_feeder`,
`freight_bulk`, `freight_general`, `parcel`, `express`.

Without `fl_role`, roles are inferred from the vehicle's default cargo:

| Vehicle | Roles |
|---|---|
| Bus or tram carrying passengers | `passenger_feeder`, `passenger_commuter` |
| Train or rail wagon carrying passengers | `passenger_intercity`, `passenger_commuter` |
| Plane carrying passengers | `passenger_intercity` |
| Helicopter or ferry carrying passengers | `passenger_commuter` |
| Anything carrying mail | `express` |
| Bulk or liquid cargo | `freight_bulk` |
| Any other cargo | `freight_general` |
| Locomotive with no cargo space | every rail role except `passenger_feeder` and `parcel` |

A cargo declaration (below) overrides the inference for vehicles that carry
that cargo.

What the shared `role/` names mean here (unknown names are ignored):

| `role/` badge | Roles |
|---|---|
| `passenger` | `passenger_intercity`, `passenger_commuter`, `passenger_feeder` |
| `freight` | `freight_bulk`, `freight_general` |
| `bulk` | `freight_bulk` |
| `mail` | `express`, `parcel` |
| `parcel` | `parcel` |
| `express` | `passenger_intercity`, `express` |
| `intercity`, `very_high_speed`, `high_power_railcar`, `coach` | `passenger_intercity` |
| `metro`, `suburban`, `commuter` | `passenger_commuter` |
| `suburban_or_universal_railcar` | `passenger_commuter`, `passenger_intercity` |
| `bus` | `passenger_commuter`, `passenger_feeder` |
| `metro_freight` | `freight_general` |
| `universal` | all inferred roles |

The names come from the specification's proposal (`role/passenger`,
`role/freight`) and from published sets such as Iron Horse.

## Declarations

### Tech families

| Declaration | Meaning | Default |
|---|---|---|
| `fl_family/<family>` | Declares the family; its badge name is the display name | families used only in `fl_tech` tags exist with defaults, and `validate` notes them |
| `fl_family/<family>/mechanic/<mechanic>` | How it is built; one only, and it must exist in the running game | from its vehicles: rail → `rail_line`, road → `road_route`, air → `airport_pair`, water → `dock_pair` |
| `fl_family/<family>/market/<market>` | What it connects; several allowed | passenger roles → `town_pair`; express and parcel roles → `mail`; freight roles → `industry` |
| `fl_family/<family>/min_tier/<tier>` | Smallest market that justifies it | `town` |

A family is available on a date when at least one of its vehicles has been
introduced by then.

### Rival company profiles

| Declaration | Meaning |
|---|---|
| `fl_profile/<profile>` | Declares a rival; its badge name is the company name |
| `fl_profile/<profile>/adopt/<family>/<mechanic>/<market>/<years>` | An adoption rule. Any of the three names may be `any`; a family of `any` matches every NewGRF family but not `conventional`, which has to be named. `<years>` is how long after the family becomes available the rival starts using it |
| `fl_profile/<profile>/industries/<market>` | Prefers industries tagged `fl_market/<market>` for freight; several allowed, in order |

Rules are read in the order the NewGRF declares them. At day zero, a rival
works through its rules in order. Each rule adds every recipe it matches,
whose family has been available for at least `<years>`, and which is not
already in the list. The rival then tries those recipes in turn. If none
succeeds, it falls back to a `conventional/road_route/town_pair` bus.

NewGRF profiles come after the four built-in rivals. Raise the number of
competitors in the game settings to include them. The built-in rivals are
written in this same format in `src/future_logistics/catalogue.cpp`:

```text
fl_profile/national_rail            "National Rail & Freight"
fl_profile/national_rail/adopt/conventional/rail_line/industry/0
fl_profile/national_rail/adopt/conventional/rail_line/town_pair/0
fl_profile/national_rail/adopt/any/rail_line/any/5
fl_profile/national_rail/adopt/conventional/rail_line/mail/0

fl_profile/aerolink                 "AeroLink"
fl_profile/aerolink/adopt/conventional/airport_pair/town_pair/0
fl_profile/aerolink/adopt/any/airport_pair/any/2
fl_profile/aerolink/adopt/conventional/airport_pair/mail/0

fl_profile/metro_regional           "Metro & Regional"
fl_profile/metro_regional/adopt/conventional/rail_line/town_pair/0
fl_profile/metro_regional/adopt/conventional/road_route/feeder/0
fl_profile/metro_regional/adopt/any/road_route/town_pair/3
fl_profile/metro_regional/adopt/conventional/road_route/mail/0
fl_profile/metro_regional/adopt/conventional/dock_pair/town_pair/0

fl_profile/integrated_logistics     "Integrated Logistics"
fl_profile/integrated_logistics/adopt/conventional/rail_line/industry/0
fl_profile/integrated_logistics/adopt/conventional/road_route/industry/0
fl_profile/integrated_logistics/adopt/any/road_route/industry/2
fl_profile/integrated_logistics/adopt/any/rail_line/industry/5
fl_profile/integrated_logistics/adopt/conventional/dock_pair/industry/0
fl_profile/integrated_logistics/adopt/conventional/airport_pair/industry/0
```

A NewGRF can also add rules to a built-in rival, for example
`fl_profile/aerolink/adopt/rocket/airport_pair/town_pair/2`.

### Cargo, mail and roles

Cargo types cannot carry badges, so their meaning is declared by label.

| Declaration | Meaning |
|---|---|
| `fl_cargo/<LABEL>/role/<role>` | Vehicles whose default cargo is this one get this role, and services carrying it look for vehicles with this role; several allowed, and the first is the one services ask for |

Which role a service asks for:

| Service | Role asked for |
|---|---|
| Passengers (`town_pair`, `feeder`) | `passenger_intercity` by rail and air, `passenger_feeder` by road, `passenger_commuter` by water |
| Mail (`mail`) | `express` |
| Freight (`industry`) | `freight_bulk` for bulk and liquid cargo, otherwise `freight_general` |

A cargo declaration replaces the role asked for, for every service that
carries that cargo. For example, `fl_cargo/MAIL/role/parcel` makes mail
services look for `parcel` vehicles, and `fl_cargo/DATA/role/express` makes a
data centre's `DATA` cargo travel with express vehicles. Vehicles must also be
able to carry or refit to the cargo.

## Fitting in with community content

Future Logistics content follows the NewGRF specification
(newgrf-specs.tt-wiki.net) and the conventions published sets use, and only
adds `fl_` badges on top. [`compatibility.md`](compatibility.md) records the
sets this was checked against.

### Badge labels

- Labels are `class/name`, with deeper paths allowed, as the specification
  says. A label without `/` is a class.
- `fl_` is Future Logistics' own namespace, the way Iron Horse uses `ih_`. The
  shared classes (`role`, `power`, `livery`, `manufacturer`, `operator`, `flag`)
  are never used for `fl_` data, and `fl_` names are never used for things the
  shared classes already describe.
- `fl_` badges are data for the game's code, not for players. Leave the `fl_`
  classes without a `name` and without a sprite; the game then never shows any
  `fl_` badge in the purchase list or its filters. Family and rival names
  (`fl_family/<family>`, `fl_profile/<profile>`) may have a `name`, which the
  console and rival names use; they stay hidden because their class has none.
- Badges with the copy flag (`BADGE_FLAG_COPY_TO_RELATED_ENTITY`) on a track or
  road type are copied to its vehicles, and are read like the vehicle's own. If
  `fl_` tags are copied this way, copy `fl_format/2` too.
- Badges from other NewGRFs, including private ones (by convention starting
  with `_`) and `newgrf/<GRFID>`, are left alone and never reported.

### Vehicles

Every vehicle in Future Logistics content carries, besides its `fl_` tags:

- at least one `role/` badge from the table above (`validate` reports a
  formatted vehicle without one), so other NewGRFs, AIs and the purchase list
  filter can see its job. Use `fl_role` only where a finer role is needed;
- a `power/` badge where it has a power source. Reuse the labels sets already
  use: `power/steam`, `power/diesel`, `power/electric`, `power/ohle`,
  `power/electro_diesel`, `power/dual_voltage`, `power/battery_hybrid`,
  `power/metro`. A new source gets a plain lower-case name, such as
  `power/hydrogen` or `power/rocket`;
- where it helps players, `manufacturer/`, `operator/` and `livery/` badges as
  the specification proposes. These are given names and sprites, unlike `fl_`.

Standard vehicle properties stay the source of truth: speed, power, capacity,
introduction date, track type and refits (by cargo class first, with labels
only for exceptions). Badges never replace them.

### Cargo

- Use the label from the cargo label registry (the specification's
  CargoTypes page) when the meaning matches, for example `O2__` (oxygen,
  liquid) for liquid oxygen. Labels already taken must not be reused for
  something else: `FUEL` means natural gas, so rocket fuel needs a new label.
- A new label must not clash with the registry or with labels in use by the
  big industry sets; FIRS, for one, uses about 45 that the registry does not
  list yet.
- Always give cargo the right classes (passengers, mail, express, armoured,
  bulk, piece goods, liquid, refrigerated, hazardous, covered...). Other sets'
  vehicles refit by class, so classes are what make a new cargo usable by them.
- `fl_cargo/<LABEL>/role/...` only adds to this, for meanings classes cannot
  express.

### Track, road and tram types

- Use the standardized railtype and roadtype label schemes where they fit, and
  give fallbacks (`alternative_railtype_list` and so on) so other sets' vehicles
  and other sets' tracks work together. A wholly new kind of way, such as a
  hyperloop tube, uses a label of its own and has no fallback.
- `fl_tech/<family>` and `fl_provides/<site>` on the type are added on top.

### Industries

- Production may be fixed or given by callbacks; the game's code reads last
  month's production when there is no fixed rate, so both work.
- Accepted and produced cargo use the cargo rules above.

## Linking code and content

Two links connect NewGRF content with the game's code, in both directions.

**Content to code: mechanics by name.** A family names the code that builds it
with `fl_family/<family>/mechanic/<mechanic>`. Mechanics are registered in one
table, `MECHANICS` in `src/future_logistics/mechanics.cpp`; each row gives the
name, the vehicle mode, the markets it serves and the function that builds it.
To add a mechanic, write its planner and add a row. A NewGRF that names a
mechanic the running game does not have loads normally; `validate` reports it
and the family's recipes are skipped.

**Code to content: items by `fl_id`.** When code needs one particular item, such
as "the launchpad airport" or "the data centre industry", the NewGRF gives that
item a stable name with `fl_id/<name>` and the code looks it up by that name,
never by a load-order index or a NewGRF-local ID. The lookups are in
`src/future_logistics/tag_index.h`:

| Function | Returns |
|---|---|
| `FindTagged(id)` | every item with that id, of any kind |
| `FindEngineById(id)` | a vehicle's engine |
| `FindRailTypeById(id)`, `FindRoadTypeById(id)` | a track or road type |
| `FindAirportById(id)` | an airport type |
| `FindIndustryTypeById(id)` | an industry type |
| `FindObjectById(id)`, `FindHouseById(id)` | an object or house type |
| `FindStationSpecById(id)`, `FindRoadStopSpecById(id)` | a NewGRF station or road stop |

The single-item lookups return nothing if no item, or more than one item of
that kind, has the id; `validate` reports duplicates. Example in NML:

```nml
item(FEAT_AIRPORTS, launchpad) {
    property { badges: ["fl_format/2", "fl_id/launchpad_small", "fl_tech/rocket", "fl_provides/launchpad"]; }
}
```

and in code: `const auto launchpad = FutureLogistics::FindAirportById("launchpad_small");`.

## Example: a rocket tech tree and a rival that uses it

```nml
badgetable {
    "fl_format/2",
    "fl_family/rocket", "fl_family/rocket/mechanic/airport_pair",
    "fl_family/rocket/market/town_pair", "fl_family/rocket/min_tier/city",
    "fl_tech/rocket", "fl_tier/3", "fl_role/express",
    "fl_needs/launchpad", "fl_provides/launchpad",
    "role/freight", "power/rocket",
    "fl_profile/spacecorp", "fl_profile/spacecorp/adopt/rocket/any/any/0",
}

item(FEAT_BADGES, rocket_family, 0) { property { label: "fl_family/rocket"; name: string(STR_ROCKET_FREIGHT); } }
item(FEAT_BADGES, spacecorp, 1)     { property { label: "fl_profile/spacecorp"; name: string(STR_SPACECORP); } }

item(FEAT_AIRCRAFT, cargo_rocket) {
    property {
        /* normal properties: introduction date, speed, capacity, cost ... */
        badges: ["fl_format/2", "fl_tech/rocket", "fl_tier/3", "fl_role/express", "fl_needs/launchpad", "role/freight", "power/rocket"];
    }
}
item(FEAT_AIRPORTS, launchpad) {
    property {
        /* layout, size ... */
        badges: ["fl_format/2", "fl_tech/rocket", "fl_provides/launchpad"];
    }
}
```

Badge labels used in properties must be listed in the `badgetable`. Check
the exact syntax against the installed NML version (tested with NML 0.9.0).

## Checking a NewGRF

Load it in a new game, open the console and use:

| Command | Shows |
|---|---|
| `future_logistics_tech badges` | Every `fl_` badge the loaded NewGRFs declare, and every tagged vehicle |
| `future_logistics_tech list` | Every vehicle, track type and airport as the library reads it |
| `future_logistics_tech recipes` | Every recipe, and each rival's adoption plan for today's date |
| `future_logistics_tech validate` | Problems: unknown names and mechanics, bad numbers, needs nobody provides, families with no vehicles, duplicate `fl_id`s, tags without `fl_format`, formatted vehicles without a `role/` badge |
| `future_logistics_tech find <id>` | Every item with that `fl_id` |
| `future_logistics_tech best <mode> <role> [<cargo label>]` | The vehicle a company would pick for a job |

## What the game's code guarantees

- It never hard-codes a NewGRF vehicle, industry, family or profile.
- Normal availability applies: nothing is used before its introduction date or
  if a company could not legally buy it.
- `fl_ai/avoid` is always obeyed.
- Unknown `fl_` labels are reported by `validate` and otherwise ignored.
- Other NewGRFs' badges are never reported; of the shared classes, only `role/`
  and `power/` are read, and only as described above.
- A recipe that cannot be built (no vehicles, no matching site, unsupported
  market) is skipped and the reason is logged; it never stops other recipes.
- The library and catalogue are rebuilt from the loaded NewGRFs and never saved,
  so these badges never change the save game format.
