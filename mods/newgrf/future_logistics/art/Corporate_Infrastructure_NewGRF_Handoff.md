# OpenTTD Corporate Infrastructure — NewGRF & Gameplay Handoff

**Version:** 1.0 (requirements handoff)  
**Prepared:** 24 October 2026  
**Status:** Specification and implementation brief — **not** a compiled NewGRF  
**Project:** Open ttd mod / Corporate Infrastructure  
**Intended reader:** NewGRF artist/developer, NML developer, OpenTTD engine developer, Game Script developer, or coding agent

> **How to use this handoff:** Supply this Markdown file alongside the latest exported transparent PNG artwork. The assets, gameplay and animation requirements below should be implemented as a cohesive OpenTTD mod. Do **not** assume that the first visual NewGRF prototype implements the intended mechanics.
>
> **Evidence and coverage:** Consolidated from the project conversations available in this project context (OpenTTD planning, Intel Mac/Codex build status, NewGRF packaging, company-colour discussion and revised spaceport/rocket design), plus the previously uploaded artwork/source ZIP. This is not a verified export of every message in every historical chat. Requirements marked **CONFIRMED** were explicitly specified or approved in the available discussions; **PROPOSED** means a recommended implementation; **TBD** means a decision or technical check remains outstanding.

---

## 1. Objective

Create a coherent **Corporate Infrastructure** add-on for OpenTTD, combining new graphics with operational buildings and transport assets:

1. **Spaceport** with a reusable **rocket vehicle** capable of visually taking off and landing.
2. **Cargo drone** plus a multi-pad **drone port**.
3. **Distribution centre / cargo hub** with real freight-handling functions.
4. **Data centre** that receives cargo from banks and provides a major economic reward.
5. **Hyperloop** terminal/system, when art and transport designs become available.
6. **Dynamic company colours** for owned assets: one shared art set recoloured to the owning company's primary/secondary colours, not a full sprite set for every colour.

The visuals should be consistent, in the same OpenTTD-style isometric perspective, and aligned with the tile grid. The custom game build should allow players and AI rivals to use the assets as intended.

### Non-goal for the first art build

A placeable decorative object is **not** equivalent to an operating station, industry, or vehicle. A successful graphic-only NewGRF is an initial milestone, not project completion.

---

## 2. Confirmed gameplay requirements at a glance

| Asset | Accepts / receives | Produces / generates | Ships / transport | Site requirements | Confidence |
|---|---|---|---|---|---|
| **Spaceport** | Passengers, Mail, Cargo | Passengers, Mail, Cargo (airport-like demand/generation) | All three via rocket/space service | Footprint concept **4×4**; placeable by companies | **CONFIRMED cargo categories**; physical mechanics to implement |
| **Hyperloop terminal** | Passengers, Mail, Cargo | Passengers, Mail, Cargo (terminal catchment / interchange) | All three | Terminal and network rules **TBD** | **CONFIRMED cargo categories** |
| **Data centre** | Gold delivered from banks (**cargo mapping requires decision**) | Very large **company revenue** benefit, not necessarily new physical cargo | No outbound physical cargo specified | **One data centre per town**; cost about **£2 million**; most expensive facility | **CONFIRMED intent**; exact bonus formula **TBD** |
| **Distribution centre / cargo hub** | Freight, Mail, Cargo | Mail, Cargo | Transfers outgoing Mail and Cargo to other hubs/networks | **One cargo hub per town** and town population approx. **5,000–6,000 minimum** | **CONFIRMED for cargo hub**; name/identity relationship to distribution centre **TBD** |
| **Drone port** | At least drone-compatible cargo, detailed cargo rules **TBD** | Dispatch/receive drone shipments | Cargo drone services | Graphic shows **four rooftop pads**; physical capacity **TBD** | **CONFIRMED appearance**; cargo semantics **TBD** |
| **Cargo drone** | Cargo (exact types/capacity **TBD**) | No cargo production | Flies between valid drone facilities | A separate purchasable/usable vehicle rather than only a decoration | **CONFIRMED concept** |
| **Rocket vehicle** | Should serve spaceport passengers, mail and cargo | No inherent production | Launches and lands at a spaceport | Separate moving sprite(s) and landing-pad states | **CONFIRMED concept and animation behaviour** |

### Important cargo terminology

The conversation uses **Freight**, **Cargo**, **Mail**, **Gold** and **Valuables** as conceptual labels. These are not guaranteed to be distinct built-in cargo types for every OpenTTD climate or industry set. Before coding, map each user-facing term to the appropriate **cargo label** (for example `PASS`, `MAIL`, `GOOD`, `VALU`, or a new cargo definition). In temperate OpenTTD, banks normally work with **Valuables** rather than Gold. Do not silently change the intended game loop: explicitly document the chosen mapping.

For the spaceport and Hyperloop, saying a facility “produces passengers, mail and cargo” describes the desired airport-like passenger/cargo catchment and transport function. A station does not automatically manufacture all those cargoes just because it accepts them. Use town catchment, nearby industries, industry production, or custom game logic as needed.

---

## 3. Required visual language — applies to every asset

**CONFIRMED design direction:**

- Isometric, OpenTTD-compatible art with a coherent viewpoint and scale.
- Transparent PNG source graphics, preferably 32bpp RGBA; no decorative backgrounds baked into the asset.
- Industrial/futuristic, but visually compatible with the rest of OpenTTD.
- Main materials stay neutral: grey concrete, dark glass, steel, white panels and dark mechanical details.
- Company colours belong on **small recognisable areas**: trim, signage, landing-pad rings, loading-bay details, drone/rocket markings, roof stripes and light panels.
- Retain shading, windows, warning lights, solar panels, fuel tanks and other details when recolouring.
- The same company must present the same livery across the facility and its vehicles.
- No need for separate blue/red/yellow/green fully rendered copies of each sprite.

**Implementation:** Use 32bpp sprites with correctly palette-indexed 8bpp recolour mask sprites where supported, wired through NML. The blue versions seen in concept previews are only **visual targets**, not proof that recolour masks already exist. Mask entries must correspond to the game's company-colour remapping rules. The 32bpp RGB image provides lighting/intensity while the indexed mask selects recolour regions.

**Ownership caveat:** Company-owned vehicles, stations and many objects can show owner-based colours. Ordinary **industries** are not automatically owned by the company that supplies them. The data centre's colour/ownership model must be deliberately implemented (for example through a company-owned object plus linked gameplay logic or a custom engine feature).

---

## 4. Spaceport — critical art and behaviour contract

**Priority:** Highest. The earlier images had a continuity defect. That must not reappear.

### 4.1 Static world asset — **one master layout**

Create **exactly one canonical empty-pad master scene** (nominally a 4×4-tile facility). Every occupied, landed, launching or empty variation **must reuse the identical static base**. Required fixed elements:

- Main paved launch pad and central recessed mounting/flame-trench area.
- Same tall launch/service tower in the same place.
- Same two gantry/service arms at the same height and coordinates (animated or separate foreground layers only if required).
- Same rear/left maintenance/support building, roof stripes, doors, windows and lighting.
- Same **two vertical cylindrical tanks plus one horizontal tank**, at identical coordinates.
- Same perimeter fencing, safety posts, lights, pipes, concrete tiles, markings and shadows.
- Same camera angle, projection, scale, footprint, drawing order, weather/lighting treatment and company-colour trim.

**Acceptance rule:** Comparing the empty and occupied variants with the rocket pixels excluded should show **no changes to the static infrastructure**. Do **not** independently re-render the entire occupied-pad scene with generative art and hope that it matches; instead composite a rocket onto the single master base, with intentionally separate front/back occlusion layers if needed.

### 4.2 Rocket must be a **separate vehicle**

**CONFIRMED:** In addition to an image of the rocket on the pad, there must be a separately controllable/animated rocket vehicle, following the approach used for the drone. Do not hard-code the moving rocket into a building PNG.

Provide these source states as a minimum:

| ID | Asset | Description |
|---|---|---|
| `spaceport_pad_empty` | Static background / base | Full facility with no rocket or exhaust |
| `rocket_idle` | Separate vehicle sprite | Upright rocket, no flame |
| `rocket_engine_low` | Vehicle/exhaust state | Same rocket, modest exhaust for landing or engine start |
| `rocket_engine_full` | Vehicle/exhaust state | Same rocket, strong flame for launch/climb |
| `spaceport_pad_occupied` | Optional **composite preview/state** | Same exact static base with the **same** rocket aligned to its stand |
| `rocket_exhaust_frames` | Animation frames | Optional multiple flame silhouettes/intensities without body redesign |

The rocket body must have the **same shape, shading and registration point** in all states. Only exhaust and explicitly animated components may change. It uses a light/white lower body, charcoal/black upper body and nose, restrained blue company-colour markings, and dark lower supports/fins.

### 4.3 Takeoff, flight and landing visual state machine

**Recommended intended sequence:**

1. **Parked:** Spaceport is occupied, rocket visible and stationary. No exhaust. Gantry arms may be attached.
2. **Prelaunch:** Service arms retract or become visually detached (if supported). Engine starts with short low-intensity effect.
3. **Launch:** Powered rocket sprite becomes the moving vehicle with bright exhaust. The pad is rendered **without** the parked rocket. Never show two rockets.
4. **In flight:** Actual vehicle is tracked by game transport logic. If direction/flight animation is required, create suitable orientation sprites while retaining the same design.
5. **Approach/landing:** Rocket descends toward the destination's **empty** pad. Exhaust returns during descent and changes intensity appropriately.
6. **Touchdown:** The moving vehicle is positioned precisely over the pad mount. Flame turns off. The presentation transitions to the parked/occupied state with **no jump, blink or double rendering**.
7. **Next launch:** The reverse transition occurs.

**Preferred rendering architecture:** Pad is a static layered world asset; rocket is a separate vehicle rendered between background and foreground structures (so gantry components can correctly cover it). An occupied composite may be used as an optimisation or parked-state graphic only if the underlying vehicle is hidden and the displayed rocket is aligned identically. Avoid maintaining two independently authored scenes.

**Technical limitation:** Traditional NewGRF can supply aircraft graphics and airport layouts, but bespoke vertically animated rocket operations, precise touchdown presentation, gantry interaction and an entirely new launch/landing state machine may require changes to the **custom OpenTTD engine**. Do not label this achievable solely through NML until the target build has been tested.

### 4.4 Functional spaceport

**CONFIRMED:** Functions like an airport: accepts, generates/collects and transports **Passengers, Mail and Cargo**. Model the desired cargo flows using catchment and explicit cargo mapping. The visual pad should behave as an actual transport facility, not just a NewObject. Cost, throughput, rocket purchase price, speed, capacity, range, running cost, introduction year, service intervals and multiplayer limits remain **TBD**.

---

## 5. Drone port and cargo drone

### 5.1 Four-pad drone port

- **Visual target:** Large industrial multi-storey building with **four visible landing circles on its roof**, a central operations/service module, equipment and doors below.
- **Provisional art footprint:** **4×4 tiles**.
- Four pad rings and some signage use dynamic company colour. Safety lighting remains appropriate fixed warning colours.
- Drones should visually approach, land on a pad, load/unload and leave.
- Ideally, independent pads can handle separate drone movements/queues.
- A standard OpenTTD helistation has **three** helipads, while this art has four. An airport overlay alone is **not proof** that four pads are functional. Assess whether the target game supports custom airport movement graphs; otherwise implement a custom airport/heliport state machine in the modified game or accept a temporary lower-capacity prototype.

### 5.2 Cargo drone vehicle

- **CONFIRMED:** Quadcopter-style cargo drone carrying a central cargo box, with four rotor units.
- Light grey/dark body, blue-company trim on rotor rings and box panels; eventually recoloured to owning company.
- Have a **parked/idle sprite** and moving sprites. A static 1×1 parked drone NewObject is acceptable for preview only.
- **PROPOSED** for a real aircraft: 8 directional sprites (or the set required by the chosen engine renderer), rotor animation frames, takeoff/hover/approach states and alignment with landing pads.
- Cargo types, carrying capacity, speed, range, operating cost, purchase price, drone maintenance and whether mail is also supported are **TBD**.
- A helicopter-type aircraft can be a useful vanilla-compatible prototype; bespoke quadcopter behaviour or four independent pad operations may require engine extensions.

---

## 6. Distribution centre / cargo hub

### Appearance

- **Provisional footprint:** **4×4 tiles**.
- Low, wide industrial building with **four truck/loading bays**, road apron, dark roller doors, rooftop solar arrays, vents and signage.
- Recolour selected door-frame trim, roof-line accents and signage to owner colours; leave roof solar panels and safety bollards unaffected.
- Vehicle approaches/parking and functional loading bays should visually correspond to actual cargo handling.

### Gameplay — **CONFIRMED for the cargo hub concept**

- **Accepts:** Freight, Mail and Cargo.
- **Produces/outbound:** Mail and Cargo, transported onward to other distribution/cargo hubs.
- **Restriction:** **Only one cargo hub per town**.
- **Population gate:** Town must be large — threshold described as **5,000–6,000 people**.
- **User-facing function:** Transfer/interchange hub rather than purely decorative scenery.

### Important mapping question

The earlier chat used **“cargo hub”**, while the image is called **“distribution centre.”** These may be the same intended facility, but this identity was **not explicitly resolved**. Implement under a working label `distribution_centre_cargo_hub` and put one decision on the sign-off list: *Are they the same building or two different assets?*

### Recommended mechanics

- Use appropriate cargo-accepting industry/station definitions and **NewGRF road stops** for functioning loading bays.
- Treat inbound/outbound cargo as real cargo packets so it can travel onward and payment/routing rules remain sensible.
- Prevent limitless automatic duplication of cargo simply by transferring it through a hub.
- Define whether the hub genuinely **produces** Mail/Cargo, or only **receives and forwards** shipments; the earlier wording could mean either.
- Enforce population eligibility and one-per-town rule through a mechanism that can reliably inspect the town and placements (Game Script and/or custom build commands). Check what vanilla NewGRF placement callbacks can enforce before choosing an engine patch.

---

## 7. Data centre

### Appearance

- **Provisional footprint:** **4×4 tiles**.
- Tall dark-glass tower, rooftop cooling/server equipment, adjacent support/service buildings, fencing and external lighting.
- Dynamic company-colour bands on entrance signage, tower light strip and selected trim; not the full façade.

### Gameplay — **CONFIRMED intent**

- **Receives:** Gold delivered from banks (map to real cargo label(s) as agreed).
- **Reward:** Substantially raises the owner's company income, described as **“10× revenue or something”** and “makes tons of money.”
- **Construction price target:** Roughly **£2 million** in the player's displayed currency/economic context.
- **Relative cost:** Intended to be **the most expensive item/building in the game**.
- **Restriction:** **Only one data centre per town**.

### Critical accounting decisions — **TBD**

Do not implement an unrestricted global 10× multiplier without defining its scope. Proposed options for stakeholder approval:

1. Multiplier on the **payment for qualifying bank cargo** delivered to this data centre.
2. A periodic **company income bonus** related to qualifying deliveries (safer to balance and explain).
3. Multiplier on **all company revenue** while the centre is operating (strongest and potentially very unbalanced).

Specify the trigger, frequency, duration, cap, ownership, bankruptcy effects, demolition effects and how repeated deliveries interact. Avoid bonus-on-bonus compounding and duplicate payouts. Balance multiplayer and AI use, and show the financial gain in the UI.

**Technical ownership warning:** A standard industry is normally not company property, so construction/funding and owner income require a deliberate design. A Game Script has access to company balance changes, but must be able to identify who owns/qualifies for the site and shipments. Engine-side ownership or company-owned object + linked industry may be necessary for a fully reliable implementation.

---

## 8. Hyperloop — future asset

**CONFIRMED:** The Hyperloop facility accepts, generates/collects and ships **Passengers, Mail and Cargo**, just like the spaceport.

**PROPOSED technical path:** Custom railtype and vehicles plus bespoke station graphics can provide high-speed point-to-point transport while using existing routing and orders. Use modified engine functionality only for capabilities that cannot be expressed through stations, railtypes or vehicle sets.

**TBD:** Final artwork, tube/guideway design, terminal footprint, vehicle sprite set, speeds, capacity, costs, route lengths and whether mixed cargo trains are supported.

**Status:** No Hyperloop source PNG was present in the earlier uploaded NewGRF source package; do not claim it is already implemented or artistically complete.

---

## 9. NewGRF versus Game Script versus custom engine — responsibilities

| Feature | Proposed home | Notes |
|---|---|---|
| Building/vehicle sprites, 32bpp images, masks, zoom variations | **NewGRF/NML** | Graphics and livery |
| Decorative placeable 4×4 objects | **NewGRF/NML** | Useful as an early visual test |
| New industries and basic cargo acceptance/production | **NewGRF/NML** | Must map cargo labels accurately |
| Station/road-stop layout and real loading | **NewGRF/NML** plus vanilla facilities | Verify tile footprints and road access |
| Company-coloured vehicle/station/object trims | **NewGRF/NML** | Requires recolour mask mapping, not merely blue PNG pixels |
| Financial bonus/payouts for data centre | **Game Script or engine** | Game Script bank-balance API exists; ownership/event triggers need validation |
| One-per-town and town-population placement rules | **Game Script and/or engine**, backed by callbacks where possible | Test pre-build enforcement vs after-build correction |
| Cargo-drone aircraft movement (prototype) | **NewGRF aircraft** and existing airports | Exact pad behaviour depends on actual game engine |
| Four fully independent drone pads | Likely **custom engine** unless supported by selected airport implementation | Four visible circles ≠ four aircraft stands |
| Rocket launch/landing custom movement and state handover | Likely **custom engine**, with NewGRF sprites | Preserve single source of truth for occupancy |
| AI opponents choosing and operating new facilities | **CorporateAI script** plus compatible APIs / game changes | Separate from NewGRF; may need AI code updates |
| Hyperloop network (first pass) | **NewGRF railtype/vehicles/stations** | Advanced physical features optional |

### Distinguish three deliverables

1. **`CorporateInfrastructure.grf`** — installable graphics/assets and NewGRF-supported mechanics.
2. **Game Script** (name TBD) — economy bonuses, placement policies and scenario goals where supported.
3. **Custom OpenTTD build** — any missing transport state machines, special ownership, UI or placement enforcement. **CorporateAI** AI package is a fourth, separately installed component if rivals must use the assets.

Not everything can truthfully be packaged inside one `.grf`.

---

## 10. Asset inventory / filenames

The user is exporting a fresh PNG bundle. **Treat those user-exported files as authoritative**, provided they are the latest approved designs. The names below are **known previous uploads**, not mandatory final paths.

| Component | Known previous artwork filename(s) | Intended role |
|---|---|---|
| Latest spaceport **empty master** | `isometric_pixel_art_spaceport_launch_pad.png` | Static empty-pad basis |
| Latest spaceport **occupied preview** | `isometric_rocket_launch_facility.png` | Same pad + rocket preview; must be composited/checked |
| Separate idle rocket | `pixel_art_rocket_on_transparent_background.png` | Vehicle body without exhaust |
| Separate powered rocket | `pixel_art_rocket_launch_sprite.png` | Vehicle body with visible exhaust |
| Data centre (colour-accented revision) | `isometric_data_centre_complex.png` | Tall tower + service buildings |
| Distribution centre (colour-accented revision) | `isometric_warehouse_with_solar_panels.png` | Four-bay warehouse |
| Drone port (colour-accented revision) | `isometric_futuristic_droneport_facility.png` | Four-pad facility |
| Cargo drone (colour-accented revision) | `isometric_blue_cargo_drone_sprite.png` | Single drone artwork |
| Older empty-pad source | `spaceport_empty_pad_transparent.png` | Historical source/reference only |
| Older occupied-pad source | `spaceport_starship_on_pad_transparent.png` | Historical source/reference only |
| Older cargo drone | `openttd_cargo_drone_transparent.png` | Historical source/reference |
| Smaller parked cargo drone | `openttd_cargo_drone_game_scale.png` | Historical game-scale test sprite |
| Older data centre | `openttd_data_centre_transparent.png` | Historical source/reference |
| Older distribution centre | `openttd_distribution_centre_4x4_transparent.png` | Historical source/reference |

Other historical uploads: `openttd_spaceport_transparent_pngs.zip`, `openttd_drone_port_assets.zip`, and `corporate_infrastructure_newgrf_source_v0_1.zip`.

### Asset quality warning

The most recent **spaceport illustrations** were generated as *related images*, but were **not formally validated as pixel-identical static backgrounds**. Likewise the separate idle and powered rocket concept images have not been checked for pixel-perfect identical body outlines/anchors. This handoff **requires** correcting that using true layer compositing and sprite registration before release; do not treat the concept previews as already meeting the continuity requirement.

### Earlier v0.1 source package status

An earlier ZIP named `corporate_infrastructure_newgrf_source_v0_1.zip` contained:

- `CorporateInfrastructure.nml`, `README.md`, `build.sh`, language strings and a manifest.
- Original art, 1× and 2× graphic versions, and a six-object contact sheet.
- Six **decorative** object definitions: data centre, distribution centre, drone port, empty spaceport, occupied spaceport, parked drone.
- **No** standalone moving rocket, usable Hyperloop, full colour-mask implementation, live cargo production, operational four-pad airport, or revenue mechanic.
- It was **source-only**: **not NML-compiler-validated or tested in the game** at the time it was delivered.
- Larger images were provisionally attached to a single tile in a 4×4 object; proper per-tile depth sorting/layers must be implemented.

The new export and this handoff should supersede that proof of concept.

---

## 11. Sprite production contract for the new export

For each source asset:

1. Retain the original high-resolution transparent RGBA source.
2. Remove transparent padding only in derived sprites, **while preserving the original centre/anchor** and exact relative offsets.
3. Set a consistent OpenTTD 2:1 isometric projection and define each asset's actual in-game tile footprint.
4. Generate normal and zoomed sprite variants as supported by the target game. Preserve alpha; use nearest-neighbour/resampled artwork according to the selected art style, and inspect at real game scale.
5. Produce a separate **indexed 8bpp company-colour mask** for every sprite/layer that must recolour. Ensure identical dimensions, origin and transparency registration to the colour image.
6. Keep warning lights, flame effects, windows, steel and concrete as **non-company-colour** pixels unless intentionally livery-specific.
7. Split multi-tile buildings into appropriately aligned **per-tile/layered sprites** with correct `xextent`, `yextent`, `zextent`, `xrel`, `yrel` / sprite bounding boxes for OpenTTD drawing order and clipping.
8. Maintain a machine-readable manifest mapping sprite IDs, paths, footprint, anchor, z-order, source master and mask.
9. Produce at least the standard front/isometric view used by game. For vehicles, produce additional directions/frames as required by the actual motion/renderer.
10. Compile and validate the `.grf` and verify in OpenTTD. A successful PNG export alone is not sufficient.

### Recommended source tree (proposal)

```text
corporate-infrastructure/
├── README.md
├── HANDOFF.md                     # This specification
├── CorporateInfrastructure.nml
├── lang/
│   └── english.lng
├── originals/                     # untouched latest user-approved PNGs
│   ├── spaceport/
│   │   ├── pad_empty_master.png
│   │   ├── rocket_idle_master.png
│   │   └── rocket_exhaust_master.png
│   ├── drone/
│   ├── drone_port/
│   ├── distribution_centre/
│   └── data_centre/
├── gfx/
│   ├── 1x/                        # game-aligned sprites
│   ├── 2x/
│   ├── masks/                     # indexed 8bpp company-colour masks
│   └── animations/
├── metadata/
│   ├── assets.json                # footprints, origins, sprite IDs
│   └── cargo_mapping.json        # physical cargo and user-facing names
├── scripts/                       # Game Script and/or test harness, if used
├── engine/                        # engine-patch notes (not a GRF folder)
├── tests/
│   ├── sprite_alignment/
│   ├── static_pad_diff/
│   ├── palette_mask/
│   └── gameplay/
└── build.sh
```

**Spaceport technical art model:** `pad_background` + `rocket_vehicle` + `pad_foreground_arms`. All are rendered against a fixed master anchor. The optional composed parked sprite is a *derived render*, never a separate independently edited building.

---

## 12. Proposed in-game interaction and ownership

- All company-owned facilities should show the owning company's livery as they do in standard OpenTTD.
- Multicolour operators and primary/secondary company colours should be supported where the engine/rendering feature allows.
- Company's rocket and drone vehicles should inherit that company's livery.
- When a facility changes hands, the visible owner livery should update without replacing the original PNG manually.
- Avoid confusing decorative ownership with cargo income ownership; if an industry is neutral, the gameplay system must explicitly assign reward rights.
- Town-level caps must handle demolition, company insolvency/acquisition, competing companies, town boundary changes and loading saved games.
- New objects must not bypass the placement rules through scenario editor/script/AI routes, unless explicitly allowed.

---

## 13. Integration with existing custom OpenTTD project

**Context from another project conversation:**

- A custom OpenTTD build has been under development via Codex/GitHub.
- Source repository previously referenced: `https://github.com/gaznet-droid/openttd`.
- A previous Intel-only macOS build for a **2015 Intel MacBook Pro on macOS Monterey 12.7.6** was reported ready and linked via GitHub Actions. **Real-device launch was not yet verified in that report.**
- A separate `CorporateAI` AI script was intended to be installed and selected in **four rival AI slots**.

**Developer instruction:** Keep NewGRF art and rules version-controlled and separable from the compiled game executable. Only patch the engine where necessary, and keep the AI/Game Script code in distinct modules. The custom game should load `CorporateInfrastructure.grf` with a compatible Game Script and AI packages so rival companies can interact with these facilities.

### Installation/test assumptions

- On the Mac, NewGRFs normally live in `~/Documents/OpenTTD/newgrf/`.
- Enable the GRF from the **main menu** before starting a new test game; changing NewGRFs mid-save can cause compatibility problems.
- NML compilation produces the `.grf`, e.g. `nmlc --grf CorporateInfrastructure.grf CorporateInfrastructure.nml`.
- NML currently recommends a modern **Python 3.10+** toolchain; provide a suitable venv or CI runner rather than assuming the system Python on old macOS supports it.
- A macOS Intel executable is a separate build product from the `.grf`; the `.grf` is loaded by OpenTTD.

---

## 14. Acceptance tests — **must pass before calling the job complete**

| Test | Pass condition |
|---|---|
| Load | GRF compiles cleanly and loads on target OpenTTD without warnings/fatal errors |
| Graphics | All current assets display at intended isometric perspective and fit the tile grid |
| Depth/sorting | No building disappears behind wrong tiles, clips trucks or draws over foreground incorrectly |
| Zoom | 1×/2× sprites and masks align without shimmer/jumps; no opaque background boxes |
| Company colours | Two different companies build/operate the same asset; trim changes colour while windows, concrete and shadows remain stable |
| **Spaceport continuity** | Empty vs occupied base is pixel-identical once the rocket-only layer is omitted |
| **Rocket identity** | Idle, launching and landing rocket body is the same sprite geometry at the same registration point |
| **Launch transition** | Rocket leaves its parked state without duplication; moving sprite/exhaust appears; pad becomes empty |
| **Landing transition** | Rocket approaches empty pad, visibly fires engines on landing, aligns, then settles to occupied idle graphic without pop/flicker |
| Drone | Drone leaves/arrives at valid port, aligns to landing pad, and is not just a static object |
| Four pads | Either four physical landing slots operate independently, **or** temporary visual-only limitation is prominently disclosed |
| Distribution | Trucks/other transport can genuinely load/unload cargo through working facilities |
| Cargo routing | Mail/cargo transfer through hub correctly; no free cargo duplication or cargo loss |
| Town cap | Second cargo hub/data centre in same town is rejected or handled according to approved rules |
| Population gate | Cargo hub cannot be built below chosen minimum town population; works for player and AI |
| Data centre | Required cargo delivery and selected revenue bonus work once per intended trigger, with correct company credit and no compounding exploit |
| Save/load | Animated/occupied state, company owners, revenue entitlements and construction limits survive save/load |
| AI | CorporateAI (four rival slots) does not crash and can interact with supported new assets |
| Target Mac | Compatible build tested on actual Intel Monterey Mac, not merely compiled |

### Useful test scene

Start a new game with two competing companies of visibly different colours. Place two identical drone ports, two data centres and two distribution centres (in different eligible towns). Place a spaceport for each company. Observe two drones and one rocket each completing trips. Record before/after screenshots of the empty and occupied pad at exactly the same camera location/zoom.

---

## 15. Build plan / priority

### Phase 1 — asset integrity and graphics (highest priority)

- Import the user's final PNG export, assign canonical filenames and confirm duplicates/versions.
- Rebuild spaceport from **one** exact static master with separate rocket body and flame layers.
- Prepare transparent normal/zoom sprites and true company-colour masks.
- Compile a working NewGRF with correct multi-tile graphics, purchase previews and company colouring.
- Test on the target game/build.

### Phase 2 — functional facilities and core vehicles

- Make distribution hub operational (cargo acceptance/transfer and truck loading).
- Make drone port and drone usable; verify actual pad handling.
- Make spaceport/rocket operate as a transport pair, initially using the simplest technically valid mechanism.
- Implement landing/launch graphical handover cleanly.

### Phase 3 — economy and restrictions

- Implement data centre as a working receiving facility plus company reward.
- Enforce town limits and the selected population threshold.
- Add balancing, costs, UI messaging, compatibility checks and AI access.

### Phase 4 — advanced custom mechanics

- Complete physically distinct four-pad drone operations, custom rocket state machine, richer exhaust/gantry animations, and Hyperloop once its artwork/track/terminal design is ready.
- Run multiplayer and savegame regression tests.

---

## 16. Decisions required before final functional release

These are **genuine unresolved items**, not reasons to block Phase 1 sprite preparation:

1. **Cargo hub vs distribution centre:** One building or two? If one, standardise the name throughout code/UI.
2. **Cargo dictionary:** Does “Cargo” mean Goods (`GOOD`), generic freight, or a custom cargo? Does “Freight” differ? How do Gold and Valuables map by climate?
3. **Cargo production semantics:** Do spaceport, Hyperloop and hub actually generate cargo, or only collect/forward production from the catchment?
4. **Data centre bonus:** 10× what, when, for how long, and to which owner? £2m cost in which base price/currency and game year?
5. **Population gate:** Exact cargo-hub threshold: 5,000, 6,000, or a setting in that range?
6. **Drone port functionality:** Must **all four visible pads** be independently usable in v1, or is a three-pad/simpler functional prototype acceptable?
7. **Vehicle figures:** Rocket and drone capacity, cargo classes, speed, range, purchase cost and operational expense.
8. **Rocket handling:** Should the custom build support strictly vertical launch/landing graphics even if the vehicle internally follows aircraft routing? How should gantry arms animate?
9. **Company ownership:** How does a player become the owner/beneficiary of a data centre or industry used by rival carriers?
10. **Hyperloop:** Tube sprites, route type, terminal design and release milestone still need a dedicated art/technical brief.

Suggested implementation policy: put values in named constants/config where possible; never bake an unapproved assumption into an irreversible savegame format or a permanent API.

---

## 17. Definition of done / handoff to an implementer

An implementer should supply:

- [ ] `CorporateInfrastructure.grf` **successfully compiled and tested** with all approved visual assets.
- [ ] NML sources, language file, build instructions and reproducible build setup.
- [ ] RGBA original art, per-zoom game sprites and **indexed company-colour masks**.
- [ ] Shared spaceport master, separate rocket idle/engine sprites and launch/landing visual logic.
- [ ] Sprite/pad layout and coordinate manifests, plus evidence of pixel-consistent empty/occupied pad.
- [ ] Source for Game Script/engine/AI changes where required, clearly separate from the `.grf`.
- [ ] Written list of implemented gameplay versus placeholders/TBD.
- [ ] Screenshots or short in-game captures at target zoom levels and company colours.
- [ ] Gameplay test results for cargo, population/site limits, income, AI, save/load and the actual Intel Mac.

**Do not mark “all buildings complete” merely because six sprites appear in the Objects menu.** The desired end result is a functional and visually consistent transport/economic extension.

---

## 18. Technical references

These are public documentation links for the implementer, **not** claims that every proposed feature is achievable without game code:

- OpenTTD NewGRF overview: https://wiki.openttd.org/en/Manual/NewGRF
- Official NML project and compiler: https://github.com/OpenTTD/nml
- NML objects: https://newgrf-specs.tt-wiki.net/wiki/NML:Objects
- NML industries: https://newgrf-specs.tt-wiki.net/wiki/NML:Industries
- NML airports/airport tiles: https://newgrf-specs.tt-wiki.net/wiki/NML:Airports
- NML 32bpp alternative sprites and masks: https://newgrf-specs.tt-wiki.net/wiki/NML:Alternative_sprites
- NML real sprite mask format: https://newgrf-specs.tt-wiki.net/wiki/NML:Realsprites
- NML recolour sprites: https://newgrf-specs.tt-wiki.net/wiki/NML:Recolour_sprites
- OpenTTD Game Script API, `GSCompany::ChangeBankBalance`: https://docs.openttd.org/gs-api/classGSCompany
- OpenTTD standard airport details: https://wiki.openttd.org/en/Manual/Airports

---

## 19. Short instruction to paste into a coding agent

> **Build the OpenTTD Corporate Infrastructure mod from the attached PNGs and this `Corporate_Infrastructure_NewGRF_Handoff.md` specification. First inventory and match the artwork; preserve the latest designs. Make the spaceport's occupied and empty states use the same pixel-identical static master and separate the rocket into idle and powered vehicle sprites with correct anchors. Implement dynamic company colours using actual 32bpp/8bpp recolour masks. Produce a working, compilable and game-tested NewGRF with correct multi-tile layering. Then implement functional cargo hubs, drone/spaceport transport and data-centre economics using NML, a Game Script and/or custom OpenTTD code according to the technical responsibility table. Record unresolved decisions instead of inventing user requirements. Do not claim complete until compiler, in-game and target-Mac checks pass.**
