# Future Logistics tech format (v1)

This is the shared contract between the **Future Logistics NewGRF** (which adds
technology) and the **day-zero mod** (which gives that technology to rival
companies). Anything that follows this format is picked up automatically: no
day-zero code change is needed to add a new vehicle.

## The idea in one paragraph

At game start and on every load, the day-zero code scans everything the game
has loaded (base vehicles and every active NewGRF) and builds a **Tech Library**:
one *tech record* per vehicle or infrastructure type. Most of each record comes
from normal NewGRF properties the game already understands (dates, speed,
capacity, cargo, cost). The few things the game has no property for, such as
"this is a delivery drone" or "this needs a launchpad", come from **NewGRF
badges** with an `fl_` prefix. Rival companies never ask for a specific vehicle;
they ask the library for "the best available tech for this job".

## Why badges

Badges are part of OpenTTD itself (`src/newgrf_badge.h`). A badge is a text
label of the form `class/name`; the part before the first `/` is its class.
Labels are global across all loaded GRFs, so `fl_tech/drone` means the same
thing whichever GRF uses it. Engines, rail types, road types, airports,
stations, objects and industries can all carry badges. This means:

- The GRF stays an ordinary NewGRF and works in unmodified OpenTTD; the badges
  simply show as extra tags there.
- No sidecar files to keep in sync with the `.grf`.
- Other people's GRFs can opt in by adding the same badges.

## Tech record

| Field | Source | Notes |
|---|---|---|
| `id` | game | GRF ID + local ID; base vehicles use `base:<engine id>` |
| `kind` | game | `vehicle`, `railtype`, `roadtype`, `airport`, `station` |
| `mode` | game | `road`, `rail`, `air`, `water` (from vehicle type) |
| `name`, `intro_date`, `buildable` | game | Normal engine availability rules apply |
| `cargo`, `refits`, `capacity` | game | From cargo and refit properties |
| `speed`, `range`, `cost`, `running_cost`, `reliability` | game | Used to score options |
| `track` | game | Rail/road type label for rail and road vehicles |
| `tech` | badge `fl_tech/*` | Technology family; default `conventional` |
| `tier` | badge `fl_tier/*` | Progression level; default `0` |
| `roles` | badge `fl_role/*` | What jobs it is good for; inferred if absent |
| `needs` | badge `fl_needs/*` | Special infrastructure the vehicle requires |
| `provides` | badge `fl_provides/*` | Special infrastructure a station or airport supplies |
| `ai` | badge `fl_ai/*` | Hints for rival companies |

The library is **derived data**. It is rebuilt from the loaded GRFs and never
stored in save games, so changing the GRF never corrupts a save through this
format. (Changing NewGRFs on an existing save is still risky for other reasons.)

## Badge vocabulary

Every tagged item must carry `fl_format/1`. The library ignores `fl_` badges on
items whose format version it does not understand, and logs a warning.

| Badge | Meaning |
|---|---|
| `fl_format/1` | Item follows this spec, version 1 |
| `fl_tech/conventional` | Ordinary transport (the default) |
| `fl_tech/drone` | Delivery drone family |
| `fl_tech/hyperloop` | Hyperloop family |
| `fl_tech/rocket` | Rocket family |
| `fl_tier/0` … `fl_tier/3` | 0 conventional, 1 drones (2022), 2 hyperloop (2035), 3 rockets (2050) |
| `fl_role/passenger_intercity` | Long-distance passengers |
| `fl_role/passenger_commuter` | Short, frequent passenger services |
| `fl_role/passenger_feeder` | Buses or small vehicles feeding a hub |
| `fl_role/freight_bulk` | Coal, ore, grain and similar |
| `fl_role/freight_general` | Goods and other piece cargo |
| `fl_role/parcel` | Parcels and e-commerce |
| `fl_role/express` | Mail and urgent cargo over long distances |
| `fl_needs/<site>` | Vehicle can only use stations or airports with `fl_provides/<site>` |
| `fl_provides/<site>` | This airport or station supplies `<site>` |
| `fl_ai/avoid` | Rival companies must never buy or build this |
| `fl_ai/early_adopter` | Only adventurous company profiles should pick this up early |

Site names in v1: `drone_pad`, `launchpad`, `hyperloop_terminal`.

New tech families, roles or sites are added by updating this file and bumping
nothing else, as long as existing meanings do not change. A change of meaning
needs `fl_format/2`.

### Linking vehicles to infrastructure

- **Rail and road:** linking is automatic. A hyperloop pod's rail type is its
  hyperloop rail type, and the game already enforces that. Tag the rail type
  itself with `fl_tech/hyperloop` too, so the library can find it and cost it.
- **Air:** aircraft only know "small airport", "large airport" or "helicopter".
  A rocket therefore carries `fl_needs/launchpad`, and the launchpad airport
  carries `fl_provides/launchpad`. The planner will only pair them with each
  other.

## Untagged items (base game and other GRFs)

Untagged vehicles still enter the library with inferred values:

- `tech = conventional`, `tier = 0`, no `needs`.
- `roles` from cargo: passengers by vehicle type and speed (bus becomes
  `passenger_feeder`, fast train becomes `passenger_intercity`, and so on), mail
  becomes `express`, bulk cargo classes become `freight_bulk`, and other cargo
  becomes `freight_general`.

So the day-zero mod works with no GRF at all, and the GRF only has to tag the
things that are actually new.

## What a GRF author does for each new vehicle

1. Set the normal properties properly: introduction date, vehicle life,
   cargo/refits, capacity, speed, cost, running cost, reliability and rail or
   road type. The library uses these, so wrong numbers produce wrong AI choices.
2. Add `fl_format/1`, one `fl_tech/*`, one `fl_tier/*`, at least one
   `fl_role/*`, and any `fl_needs/*`.
3. Tag any matching infrastructure with `fl_provides/*` (and `fl_tech/*` for
   rail or road types).
4. Load it in-game and run `future_logistics_tech validate` (planned, see the
   roadmap) to confirm the library reads it the way you intended.

Illustrative NML (check the exact badge syntax against the installed NML
version before relying on it):

```nml
item(FEAT_AIRCRAFT, cargo_drone) {
    property {
        name: string(STR_NAME_CARGO_DRONE);
        introduction_date: date(2022, 1, 1);
        /* ...normal speed, capacity, cost and refit properties... */
        badges: ["fl_format/1", "fl_tech/drone", "fl_tier/1",
                 "fl_role/parcel", "fl_needs/drone_pad"];
    }
}
```

## What the day-zero side guarantees

- It never hard-codes a GRF vehicle ID or name.
- It respects normal availability: nothing is used before its intro date or
  if a company could not legally buy it.
- `fl_ai/avoid` is always obeyed.
- Unknown badges are ignored, not treated as errors.
- If a needed site (for example `launchpad`) has no matching infrastructure
  loaded, the vehicle is skipped and the reason is logged.
