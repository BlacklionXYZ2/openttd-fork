# Future Logistics tech format (v2)

This is the contract between NewGRFs that add technology, industries or rival
companies, and the day-zero code that builds and runs the rivals. A NewGRF that
follows it is picked up automatically: adding a new vehicle, a whole new tech
tree, a new industry or a new rival needs no change to the game's code, as long
as it can be built with one of the existing **mechanics** (see below).

Version 1 tags (`fl_format/1`) keep working.

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
code, because they decide where and how things are built.

| Mechanic | Builds | Vehicles |
|---|---|---|
| `rail_line` | two terminus stations, a single line, a depot, one train | rail |
| `road_route` | two drive-through stops, an owned road, a depot, a small fleet | road |
| `airport_pair` | two airports and a small fleet | air |

Ships (`dock_pair`) are planned. A new kind of construction, such as pads on
rooftops, needs a new mechanic in code.

### Markets

A market is what a service connects.

| Market | Meaning | Mechanics |
|---|---|---|
| `town_pair` | passengers or town cargo between two towns | all |
| `industry` | freight from a producing industry to an industry (any mechanic) or a town (`road_route`) that accepts it | `rail_line`, `road_route` |
| `feeder` | a route from a nearby town into one of the company's railway stations | `road_route` |

A recipe for a combination not in this table is reported by
`future_logistics_tech validate` and skipped.

### Families and recipes

A **tech family** groups vehicles and infrastructure that belong together, such
as rockets and launchpads. Everything without an `fl_tech` tag belongs to the
built-in family `conventional`.

A **recipe** is one family, one mechanic and one market, for example
`rocket/airport_pair/town_pair`. Recipes are built automatically from the
families. The built-in family `conventional` has these recipes:
`rail_line/town_pair`, `rail_line/industry`, `road_route/town_pair`,
`road_route/feeder`, `road_route/industry` and `airport_pair/town_pair`.

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
| `fl_tech/<family>` | vehicle, track type, airport | Family the item belongs to | `conventional` |
| `fl_tier/<0-9>` | vehicle | Progression level | `0` |
| `fl_role/<role>` | vehicle | Jobs it suits; several allowed, and they replace the inferred roles | inferred from cargo |
| `fl_needs/<site>` | vehicle | Only usable where a matching site is provided | none |
| `fl_provides/<site>` | track type, airport | Supplies that site | none |
| `fl_ai/avoid` | vehicle | Rivals never buy it | off |
| `fl_ai/early_adopter` | vehicle | Only used by adoption rules with a delay of 0 | off |
| `fl_industry/<kind>` | industry | What the industry is, for display and validation | none |
| `fl_market/<market>` | industry | Market group, so rivals can prefer it (`fl_profile/.../industries/...`) | none |

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

## Declarations

### Tech families

| Declaration | Meaning | Default |
|---|---|---|
| `fl_family/<family>` | Declares the family; its badge name is the display name | families used only in `fl_tech` tags exist with defaults, and `validate` notes them |
| `fl_family/<family>/mechanic/<mechanic>` | How it is built; one only | from its vehicles: rail → `rail_line`, road → `road_route`, air → `airport_pair` |
| `fl_family/<family>/market/<market>` | What it connects; several allowed | passenger and express roles → `town_pair`; freight roles → `industry` |
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

fl_profile/aerolink                 "AeroLink"
fl_profile/aerolink/adopt/conventional/airport_pair/town_pair/0
fl_profile/aerolink/adopt/any/airport_pair/any/2

fl_profile/metro_regional           "Metro & Regional"
fl_profile/metro_regional/adopt/conventional/rail_line/town_pair/0
fl_profile/metro_regional/adopt/conventional/road_route/feeder/0
fl_profile/metro_regional/adopt/any/road_route/town_pair/3

fl_profile/integrated_logistics     "Integrated Logistics"
fl_profile/integrated_logistics/adopt/conventional/rail_line/industry/0
fl_profile/integrated_logistics/adopt/conventional/road_route/industry/0
fl_profile/integrated_logistics/adopt/any/road_route/industry/2
fl_profile/integrated_logistics/adopt/any/rail_line/industry/5
```

### Cargo meanings

Cargo types cannot carry badges, so their meaning is declared by label.

| Declaration | Meaning |
|---|---|
| `fl_cargo/<LABEL>/role/<role>` | Vehicles carrying this cargo get this role, and freight services for it look for vehicles with this role; several allowed |

## Example: a rocket tech tree and a rival that uses it

```nml
badgetable {
    "fl_format/2",
    "fl_family/rocket", "fl_family/rocket/mechanic/airport_pair",
    "fl_family/rocket/market/town_pair", "fl_family/rocket/min_tier/city",
    "fl_tech/rocket", "fl_tier/3", "fl_role/express",
    "fl_needs/launchpad", "fl_provides/launchpad",
    "fl_profile/spacecorp", "fl_profile/spacecorp/adopt/rocket/any/any/0",
}

item(FEAT_BADGES, rocket_family, 0) { property { label: "fl_family/rocket"; name: string(STR_ROCKET_FREIGHT); } }
item(FEAT_BADGES, spacecorp, 1)     { property { label: "fl_profile/spacecorp"; name: string(STR_SPACECORP); } }

item(FEAT_AIRCRAFT, cargo_rocket) {
    property {
        /* normal properties: introduction date, speed, capacity, cost ... */
        badges: ["fl_format/2", "fl_tech/rocket", "fl_tier/3", "fl_role/express", "fl_needs/launchpad"];
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
| `future_logistics_tech validate` | Problems: unknown names, bad numbers, needs nobody provides, families with no vehicles, tags without `fl_format` |
| `future_logistics_tech best <mode> <role> [<cargo label>]` | The vehicle a company would pick for a job |

## What the game's code guarantees

- It never hard-codes a NewGRF vehicle, industry, family or profile.
- Normal availability applies: nothing is used before its introduction date or
  if a company could not legally buy it.
- `fl_ai/avoid` is always obeyed.
- Unknown `fl_` labels are reported by `validate` and otherwise ignored.
- A recipe that cannot be built (no vehicles, no matching site, unsupported
  market) is skipped and the reason is logged; it never stops other recipes.
- The library and catalogue are rebuilt from the loaded NewGRFs and never saved,
  so these badges never change the save game format.
