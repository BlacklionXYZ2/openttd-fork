# Placeholder values

Starship's cost, running cost, capacity and speed are decided (below). The rest are placeholders
so the content works in game; change them in `starship.nml` during the balance pass (roadmap
phase 8) and record the decision here.

## Decided

Starship is balanced as the top of the transport world (24 October 2026): the most expensive
vehicle to buy and to run, the largest capacity, and the most profit. The integration suite checks
all four against every vehicle loaded with the Future Logistics NewGRF.

| Property | Value | Standing (2056, with the Future Logistics NewGRF) |
|---|---|---|
| Cost factor | 255, the maximum | Most expensive to buy: 522,949, against the Reusable Rocket's 512,695 |
| Running cost factor | 255, the maximum | Highest running cost: 7,171 a year, against the Darwin 300's 7,115 |
| Capacity | 500 passengers + 120 mail | Largest: the next is the Dinger 200 with 400 |
| Speed / loading speed | 2000 km/h / 80 | Fastest vehicle, and loads its 500 quickly |
| Profit | from the above | In a simulated year, the two highest earners in the world were Starships (25.1 and 17.1 million); the best other vehicle, a Dinger 200, earned 14.3 million |

Purchase and running cost factors are single bytes, so Starship cannot be made more expensive
through its own properties; a NewGRF vehicle with the same factors and a dearer base price would
match it.

## Still placeholders

| Item | Property | Placeholder | Notes |
|---|---|---|---|
| Starship | introduction date | 1 Jan 2045 | Before the Future Logistics rocket (2050), as the first of the reusable rockets |
| Starship | range | unlimited | |
| Starship | refits | passengers, mail, express and armoured cargo | Like the Future Logistics rocket |
| Starship | vehicle life / model life | 30 / 50 years | |
| Starship | tier | `fl_tier/4` | One above nothing else yet; only ordering within the family |
| Starship | flame | by flight phase: plume while launching and landing, flame in flight, none on the ground | Checked in game: see the README |
| Starship Launch Site | available from | 2045 | |
| Starship Launch Site | layout | the handoff launch complex over a 5×4 layout, moving like a country airport | Starship launches and lands vertically on the launch mount |
| Starship Launch Site | catchment / noise / maintenance | 6 / 25 / 80 | |
| Rockets family | minimum market tier / markets | city / passengers, mail, freight | The same as the Future Logistics NewGRF declares |
| AeroLink | Starship adoption delay | 2 years | Its built-in rule for any new air family |
