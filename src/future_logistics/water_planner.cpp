/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file water_planner.cpp Day-zero shipping: two docks joined by open water, a ship depot and a small fleet. */
#include "../stdafx.h"
#include "water_planner.h"
#include "catalogue.h"
#include "freight_planner.h"
#include "scale.h"
#include "tech_library.h"
#include "../cargotype.h"
#include "../command_func.h"
#include "../company_base.h"
#include "../debug.h"
#include "../direction_func.h"
#include "../engine_base.h"
#include "../industry.h"
#include "../landscape_cmd.h"
#include "../order_base.h"
#include "../order_cmd.h"
#include "../settings_type.h"
#include "../slope_func.h"
#include "../station_base.h"
#include "../station_cmd.h"
#include "../station_func.h"
#include "../station_map.h"
#include "../tilearea_type.h"
#include "../town.h"
#include "../vehicle_base.h"
#include "../vehicle_cmd.h"
#include "../water_cmd.h"
#include "../water_map.h"
#include <functional>
#include <map>
#include <queue>

#include "../safeguards.h"

namespace FutureLogistics {
namespace {
constexpr size_t MAX_DOCK_SITES = 3; ///< Dock sites tried per endpoint.
constexpr size_t MAX_SHIP_PAIRS = 32; ///< Routes tried per service.
constexpr size_t MAX_WATER_SEARCH = 32768; ///< Water tiles searched per route.
constexpr int TOWN_DOCK_RADIUS = 16; ///< How far from a town centre a dock may go.
constexpr std::array<DiagDirection, 4> DIRECTIONS{DiagDirection::NE, DiagDirection::SE, DiagDirection::SW, DiagDirection::NW};

/** One end of a shipping route: a dock to build, or an existing station such as an oil rig. */
struct DockEnd {
	TileIndex land = INVALID_TILE; ///< Sloped land tile of a new dock, or INVALID_TILE for an existing station.
	DiagDirection facing = DiagDirection::NE; ///< Direction from the land tile towards the water.
	const Station *existing = nullptr; ///< Existing station with a dock, used as is.
	StationID join = NEW_STATION; ///< Existing station a new dock joins, such as a transfer station.

	/** @return The tiles ships wait on. */
	std::vector<TileIndex> Approach() const
	{
		if (this->existing == nullptr) return {Step(this->land, this->facing, 2)};
		std::vector<TileIndex> tiles;
		for (TileIndex tile : this->existing->docking_station) {
			if (IsValidTile(tile) && IsDockingTile(tile)) tiles.push_back(tile);
		}
		return tiles;
	}
};

/**
 * Check whether a tile is open water a ship can sail over.
 * @param tile Tile to check.
 * @return Whether it is flat water without a structure.
 */
bool IsOpenWater(TileIndex tile)
{
	return IsValidTile(tile) && IsWaterTile(tile) && IsTileFlat(tile);
}

/**
 * Check whether a dock can go on a tile: clear sloped land facing flat water, with open water beyond.
 * @param tile Land tile.
 * @return The dock, or empty if the tile does not fit.
 */
std::optional<DockEnd> DockAt(TileIndex tile)
{
	if (!IsValidTile(tile) || !(IsTileType(tile, TileType::Clear) || IsTileType(tile, TileType::Trees)) || HasTileWaterGround(tile)) return std::nullopt;
	const DiagDirection uphill = GetInclinedSlopeDirection(GetTileSlope(tile));
	if (uphill == DiagDirection::Invalid) return std::nullopt;
	const DiagDirection facing = ReverseDiagDir(uphill);
	const TileIndex water = Step(tile, facing);
	if (water == INVALID_TILE || !IsOpenWater(water) || !IsOpenWater(Step(tile, facing, 2))) return std::nullopt;
	return DockEnd{tile, facing, nullptr};
}

/**
 * Get a dock's catchment radius as the station rules apply it.
 * @return Radius in tiles.
 */
int DockCatchment()
{
	return _settings_game.station.modified_catchment ? CA_DOCK : CA_UNMODIFIED;
}

/**
 * Find dock sites near a point.
 * @param centre Point to search around.
 * @param radius Search radius in tiles.
 * @param accept Extra check on a site's land and water tiles, such as cargo acceptance.
 * @param limit Most sites to return.
 * @return Up to \a limit sites, nearest first.
 */
std::vector<DockEnd> FindDocks(TileIndex centre, int radius, const std::function<bool(const DockEnd &, const TileArea &)> &accept, size_t limit = MAX_DOCK_SITES)
{
	std::vector<DockEnd> result;
	for (int y = std::max(1, (int)TileY(centre) - radius); y <= std::min((int)Map::SizeY() - 2, (int)TileY(centre) + radius); y++) {
		for (int x = std::max(1, (int)TileX(centre) - radius); x <= std::min((int)Map::SizeX() - 2, (int)TileX(centre) + radius); x++) {
			const auto dock = DockAt(TileXY(x, y));
			if (!dock.has_value()) continue;
			if (accept(*dock, TileArea(dock->land, Step(dock->land, dock->facing)))) result.push_back(*dock);
		}
	}
	std::ranges::stable_sort(result, {}, [centre](const DockEnd &d) { return DistanceManhattan(d.land, centre); });
	if (result.size() > limit) result.resize(limit);
	return result;
}

/**
 * Check that ships can sail from some tiles to others over open water.
 * @param starts Tiles to start from.
 * @param goal_list Tiles to reach.
 * @return Whether a water path exists within the search limit.
 */
bool WaterConnected(const std::vector<TileIndex> &starts, const std::vector<TileIndex> &goal_list)
{
	const std::set<TileIndex> goals(goal_list.begin(), goal_list.end());
	std::set<TileIndex> seen;
	std::queue<TileIndex> queue;
	for (TileIndex tile : starts) {
		if (IsOpenWater(tile) || IsDockingTile(tile)) { seen.insert(tile); queue.push(tile); }
	}
	while (!queue.empty() && seen.size() < MAX_WATER_SEARCH) {
		const TileIndex tile = queue.front();
		queue.pop();
		if (goals.contains(tile)) return true;
		for (DiagDirection direction : DIRECTIONS) {
			const TileIndex next = Step(tile, direction);
			if (next == INVALID_TILE || seen.contains(next) || !(IsOpenWater(next) || goals.contains(next))) continue;
			seen.insert(next);
			queue.push(next);
		}
	}
	return false;
}

/**
 * Check that ships can sail between two docks over open water.
 * @param from Origin.
 * @param to Destination.
 * @return Whether a water path exists within the search limit.
 */
bool Connected(const DockEnd &from, const DockEnd &to)
{
	return WaterConnected(from.Approach(), to.Approach());
}

/**
 * Find a place for a ship depot near a dock: two tiles of open water, with open water at both ends,
 * reachable from the dock and not on top of either dock's waiting tile.
 * @param dock Dock the ships start from.
 * @param avoid Tiles to keep clear.
 * @return Depot tile and axis, or empty if none fits.
 */
std::optional<std::pair<TileIndex, Axis>> FindDepot(const DockEnd &dock, const std::set<TileIndex> &avoid)
{
	const TileIndex start = dock.Approach().front();
	for (int distance = 2; distance <= 6; distance++) {
		for (int dy = -distance; dy <= distance; dy++) {
			for (int dx = -distance; dx <= distance; dx++) {
				if (std::abs(dx) + std::abs(dy) != distance) continue;
				const int x = (int)TileX(start) + dx, y = (int)TileY(start) + dy;
				if (x <= 1 || y <= 1 || x >= (int)Map::SizeX() - 2 || y >= (int)Map::SizeY() - 2) continue;
				const TileIndex tile = TileXY(x, y);
				for (Axis axis : {Axis::X, Axis::Y}) {
					const DiagDirection forward = axis == Axis::X ? DiagDirection::SW : DiagDirection::SE;
					const TileIndex second = Step(tile, forward);
					const TileIndex before = Step(tile, ReverseDiagDir(forward));
					const TileIndex after = Step(tile, forward, 2);
					bool clear = true;
					for (TileIndex t : {tile, second, before, after}) {
						if (t == INVALID_TILE || !IsOpenWater(t) || avoid.contains(t)) { clear = false; break; }
					}
					if (!clear) continue;
					if (WaterConnected({before, after}, dock.Approach())) return std::pair{tile, axis};
				}
			}
		}
	}
	return std::nullopt;
}

/** What a shipping service runs. */
struct ShipJob {
	std::string_view kind; ///< Name for logs: ferry, mail or freight.
	const Engine *ship; ///< Ship to buy.
	CargoType cargo; ///< Cargo to refit it to.
	uint fleet; ///< Number of ships.
	std::optional<VehicleOrderID> full_load_at; ///< Order with a full load, as for freight; the other order does not load.
	std::optional<VehicleOrderID> transfer_at = std::nullopt; ///< Order that transfers and leaves empty, for a port where another vehicle takes over.
};

/**
 * Preflight docks and a depot, then build them and the fleet through normal commands.
 * @param a Origin, near which the depot goes.
 * @param b Destination.
 * @param job Service to build.
 * @param execute Whether to build after a successful preflight; false only checks.
 * @param[out] result_built If given, receives what was built on success, so a caller can join onto it or remove it again.
 * @return Whether preflight rejected, execution failed, or the service was built (or would be, when not executing).
 */
ConstructionResult BuildShipping(const DockEnd &a, const DockEnd &b, const ShipJob &job, bool execute = true, BuiltLeg *result_built = nullptr)
{
	std::set<TileIndex> avoid;
	for (const DockEnd *end : {&a, &b}) {
		const auto tiles = end->Approach();
		avoid.insert(tiles.begin(), tiles.end());
		if (end->existing == nullptr) avoid.insert(Step(end->land, end->facing));
	}
	if (a.existing != nullptr) return ConstructionResult::Rejected;
	const auto depot = FindDepot(a, avoid);
	if (!depot.has_value()) return ConstructionResult::Rejected;
	const auto &[depot_tile, depot_axis] = *depot;

	Money cost = 0;
	for (const DockEnd *end : {&a, &b}) {
		if (end->existing != nullptr) continue;
		auto result = Command<Commands::BuildDock>::Do({}, end->land, end->join, true);
		if (result.Failed()) return ConstructionResult::Rejected;
		cost += result.GetCost();
	}
	auto depot_test = Command<Commands::BuildShipDepot>::Do({}, depot_tile, depot_axis);
	if (depot_test.Failed()) return ConstructionResult::Rejected;
	cost += depot_test.GetCost();
	const Company *company = Company::Get(_current_company);
	if (cost * 2 + job.ship->GetCost() * 2 * job.fleet > company->money) return ConstructionResult::Rejected;
	if ((company->clear_limit >> 16) < 16) return ConstructionResult::Rejected;
	if (!execute) return ConstructionResult::Success;

	BuiltLeg done;
	std::vector<TileIndex> &built = done.tiles;
	std::vector<VehicleID> &vehicles = done.vehicles;
	auto fail = [&](std::string_view phase) {
		Debug(Facility::Misc, Severity::Warning, "FL construction failed company={} phase={}; clearing new infrastructure", _current_company, phase);
		Undo(done);
		return ConstructionResult::Failed;
	};
	for (const DockEnd *end : {&a, &b}) {
		if (end->existing != nullptr) continue;
		if (Command<Commands::BuildDock>::Do(EXEC, end->land, end->join, true).Failed()) return fail("dock");
		built.push_back(end->land);
	}
	if (Command<Commands::BuildShipDepot>::Do(EXEC, depot_tile, depot_axis).Failed()) return fail("depot");
	built.push_back(depot_tile);

	const StationID from = GetStationIndex(a.land);
	const StationID to = b.existing != nullptr ? b.existing->index : GetStationIndex(b.land);
	for (uint n = 0; n < job.fleet; n++) {
		auto result = Command<Commands::BuildVehicle>::Do(EXEC, depot_tile, job.ship->index, false, job.cargo, ClientID::Invalid);
		if (ExtractCommandCost(result).Failed()) return fail("vehicle");
		const VehicleID vehicle = std::get<1>(result);
		vehicles.push_back(vehicle);
		for (VehicleOrderID i = 0; i < 2; i++) {
			Order order;
			order.MakeGoToStation(i == 0 ? from : to);
			/* Non-train orders must use the far end; the default near end is rejected. */
			order.SetStopLocation(OrderStopLocation::FarEnd);
			if (job.full_load_at.has_value()) order.SetLoadType(i == *job.full_load_at ? OrderLoadType::FullLoadAny : OrderLoadType::NoLoad);
			if (job.transfer_at == i) {
				/* Transfer and leave empty: the cargo waits at the station for the next vehicle to take on. */
				order.SetUnloadType(OrderUnloadType::Transfer);
				order.SetLoadType(OrderLoadType::NoLoad);
			}
			if (Command<Commands::InsertOrder>::Do(EXEC, vehicle, i, order).Failed()) return fail("orders");
		}
		if (Command<Commands::StartStopVehicle>::Do(EXEC, vehicle, true).Failed()) return fail("start");
	}
	Debug(Facility::Misc, Severity::Notice, "FL ship company={} kind={} cargo={} vehicles={} vehicle={} from={} to={} depot={} balance={}",
		_current_company, job.kind, CargoSpec::Get(job.cargo)->label.AsString(), job.fleet, vehicles.front(), from, to, depot_tile,
		Company::Get(_current_company)->money);
	if (result_built != nullptr) {
		done.sited = result_built->sited;
		done.from = from;
		done.to = to;
		done.from_tile = a.land;
		done.to_tile = b.existing != nullptr ? b.existing->xy : b.land;
		*result_built = std::move(done);
	}
	return ConstructionResult::Success;
}

/**
 * Pick the best ship for a job.
 * @param library Loaded transport technology.
 * @param spec Family and preferences of the service.
 * @param role Job the ship does.
 * @param cargo Cargo it carries.
 * @return Engine, or nullptr if none is available.
 */
const Engine *ChooseShip(const TechLibrary &library, const ServiceSpec &spec, TechRole role, CargoType cargo)
{
	TechQuery query;
	query.mode = TechMode::Water;
	query.role = role;
	query.cargo = cargo;
	query.company = _current_company;
	query.family = spec.family;
	query.early_adopters = spec.early_adopters;
	const TechRecord *record = library.FindBest(query);
	if (record == nullptr) return nullptr;
	Debug(Facility::Misc, Severity::Notice, "FL tech company={} role={} vehicle={}", _current_company, TechRoleName(role), record->id);
	return Engine::Get(record->engine);
}

/**
 * Try every combination of two endpoints' dock sites until one route is built.
 * @param from Origin docks.
 * @param to Destination docks.
 * @param job Service to build.
 * @return Outcome of the first attempt that got past preflight, or Rejected.
 */
ConstructionResult TryDocks(const std::vector<DockEnd> &from, const std::vector<DockEnd> &to, const ShipJob &job)
{
	for (const DockEnd &a : from) {
		for (const DockEnd &b : to) {
			if (a.land == b.land || !Connected(a, b)) continue;
			const auto result = BuildShipping(a, b, job);
			if (result != ConstructionResult::Rejected) return result;
		}
	}
	return ConstructionResult::Rejected;
}

/**
 * Find docks that serve an industry. An industry with its own dock, such as an oil rig, needs nothing built.
 * @param industry Industry to serve.
 * @param cargo Cargo carried.
 * @param destination Whether the cargo is delivered here, so the dock must accept it.
 * @return Dock sites, or the industry's own dock.
 */
std::vector<DockEnd> IndustryDocks(const Industry *industry, CargoType cargo, bool destination)
{
	if (industry->neutral_station != nullptr && industry->neutral_station->facilities.Test(StationFacility::Dock)) {
		return std::vector<DockEnd>{DockEnd{INVALID_TILE, DiagDirection::NE, industry->neutral_station}};
	}
	const int radius = DockCatchment();
	const int search = std::max<int>(industry->location.w, industry->location.h) / 2 + radius + 2;
	return FindDocks(Centre(industry), search, [&](const DockEnd &, const TileArea &area) {
		TileArea catchment = area;
		catchment.Expand(radius);
		if (!catchment.Intersects(industry->location)) return false;
		return !destination || GetAcceptanceAroundTiles(area.tile, area.w, area.h, radius).first[cargo] >= 8;
	});
}
} // namespace

/**
 * Build a dock in each of two unused towns on the same water and run ferries or mail boats between them.
 * @param library Loaded transport technology.
 * @param spec Family, limits and preferences of the service.
 * @param cargo Passengers or mail.
 * @param[in,out] used_towns Towns already served for this cargo; the chosen towns are added on success.
 * @return Outcome.
 */
ConstructionResult BuildTownFerry(const TechLibrary &library, const ServiceSpec &spec, CargoType cargo, std::set<TownID> &used_towns)
{
	if (!IsValidCargoType(cargo)) return ConstructionResult::Rejected;
	const Engine *ship = ChooseShip(library, spec, TownRole(library, cargo, TechRole::PassengerCommuter), cargo);
	if (ship == nullptr) return ConstructionResult::Rejected;
	struct Pair { uint distance; const Town *a; const Town *b; };
	std::vector<const Town *> towns;
	for (const Town *town : Town::Iterate()) {
		if (towns.size() == 128) break;
		if (!used_towns.contains(town->index)) towns.push_back(town);
	}
	std::vector<Pair> pairs;
	for (const Town *a : towns) {
		for (const Town *b : towns) {
			if (a->index >= b->index) continue;
			const uint distance = DistanceManhattan(a->xy, b->xy);
			if (distance >= 16 && distance <= 128 && TierForTowns(a, b) >= spec.min_tier) pairs.push_back({distance, a, b});
		}
	}
	std::ranges::stable_sort(pairs, {}, &Pair::distance);
	const int radius = DockCatchment();
	std::map<TownID, std::vector<DockEnd>> sites;
	auto town_docks = [&](const Town *town) -> const std::vector<DockEnd> & {
		auto it = sites.find(town->index);
		if (it != sites.end()) return it->second;
		return sites.emplace(town->index, FindDocks(town->xy, TOWN_DOCK_RADIUS, [&](const DockEnd &dock, const TileArea &area) {
			if (ClosestTownFromTile(dock.land, UINT_MAX)->index != town->index) return false;
			return GetAcceptanceAroundTiles(area.tile, area.w, area.h, radius).first[cargo] >= 8 && GetProductionAroundTiles(area.tile, area.w, area.h, radius)[cargo] > 0;
		})).first->second;
	};
	size_t tried = 0;
	for (const Pair &pair : pairs) {
		if (tried == MAX_SHIP_PAIRS) break;
		const auto &from = town_docks(pair.a);
		const auto &to = town_docks(pair.b);
		if (from.empty() || to.empty()) continue;
		tried++;
		const MarketTier tier = TierForTowns(pair.a, pair.b);
		const bool passengers = CargoSpec::Get(cargo)->classes.Test(CargoClass::Passengers);
		const auto result = TryDocks(from, to, {passengers ? "ferry" : "mail", ship, cargo, GetTierLimits(tier).fleet, std::nullopt});
		if (result == ConstructionResult::Rejected) continue;
		if (result == ConstructionResult::Success) {
			used_towns.insert(pair.a->index);
			used_towns.insert(pair.b->index);
			Debug(Facility::Misc, Severity::Notice, "FL towns company={} a={} b={} tier={} population={},{}", _current_company, pair.a->index, pair.b->index,
				TierName(tier), pair.a->cache.population, pair.b->cache.population);
		}
		return result;
	}
	return ConstructionResult::Rejected;
}

/**
 * Find docks for one end of a freight leg.
 * @param place Industry or transfer station; ships do not deliver into towns.
 * @param cargo Cargo carried.
 * @param destination Whether the cargo is delivered here, so the dock must accept it.
 * @return Dock sites, or an industry's own dock.
 */
static std::vector<DockEnd> FreightDocks(const Place &place, CargoType cargo, bool destination)
{
	if (place.town != nullptr) return {};
	if (place.IsHub()) {
		auto docks = FindDocks(place.hub, place.radius, [](const DockEnd &, const TileArea &) { return true; }, SIZE_MAX);
		for (DockEnd &dock : docks) dock.join = place.join;
		KeepUsable(docks, place, [](const DockEnd &dock) { return dock.land; }, MAX_DOCK_SITES);
		return docks;
	}
	return IndustryDocks(place.industry, cargo, destination);
}

/**
 * Build, or check, a shipping leg: ships that full-load at one dock and deliver or transfer at the other,
 * over the same water. An industry with its own dock, such as an oil rig, is used as is.
 * @copydoc LegBuild
 */
ConstructionResult ShipLeg(const TechLibrary &library, const ServiceSpec &spec, const Place &from, const Place &to, const LegJob &job, bool execute, BuiltLeg &built)
{
	const Engine *ship = ChooseShip(library, spec, FreightRole(library, job.cargo), job.cargo);
	if (ship == nullptr) return ConstructionResult::Rejected;
	const auto origins = FreightDocks(from, job.cargo, false);
	if (origins.empty()) return ConstructionResult::Rejected;
	const auto destinations = FreightDocks(to, job.cargo, true);
	if (destinations.empty()) return ConstructionResult::Rejected;
	built.sited = true;
	const uint fleet = GetTierLimits(job.tier).fleet;
	for (const DockEnd &a : origins) {
		for (const DockEnd &b : destinations) {
			if (a.existing != nullptr && b.existing != nullptr) continue;
			if (a.existing == nullptr && b.existing == nullptr && a.land == b.land) continue;
			if (!Connected(a, b)) continue;
			ConstructionResult result;
			if (a.existing == nullptr) {
				const ShipJob ship_job{"freight", ship, job.cargo, fleet, VehicleOrderID{0}, job.transfer ? std::optional<VehicleOrderID>{1} : std::nullopt};
				result = BuildShipping(a, b, ship_job, execute, &built);
			} else {
				/* The depot goes beside a dock the company builds, so with an existing source dock such as an oil rig,
				 * the destination dock comes first in the orders and the full load happens at the second stop. */
				const ShipJob ship_job{"freight", ship, job.cargo, fleet, VehicleOrderID{1}, job.transfer ? std::optional<VehicleOrderID>{0} : std::nullopt};
				result = BuildShipping(b, a, ship_job, execute, &built);
				if (result == ConstructionResult::Success && execute) {
					std::swap(built.from, built.to);
					std::swap(built.from_tile, built.to_tile);
				}
			}
			if (result != ConstructionResult::Rejected) return result;
		}
	}
	return ConstructionResult::Rejected;
}

/**
 * Ships need no road or track; check that each ordered station has a dock the ship may use.
 * @param ship Ship to inspect.
 * @return The ordered stations with a dock that belong to the owner or to no company, such as oil rigs.
 */
std::set<StationID> DocksReachable(const Vehicle *ship)
{
	std::set<StationID> reached;
	for (const Order &order : ship->Orders()) {
		if (!order.IsType(OT_GOTO_STATION)) continue;
		const StationID id = order.GetDestination().ToStationID();
		const Station *station = Station::GetIfValid(id);
		if (station != nullptr && (station->owner == ship->owner || station->owner == OWNER_NONE) && station->facilities.Test(StationFacility::Dock)) reached.insert(id);
	}
	return reached;
}

} // namespace FutureLogistics
