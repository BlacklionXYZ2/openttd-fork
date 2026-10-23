/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file road_planner.cpp Day-zero road services: an owned road between two drive-through stops, a depot and a small fleet. */
#include "../stdafx.h"
#include "road_planner.h"
#include "freight_planner.h"
#include "catalogue.h"
#include "scale.h"
#include "tech_library.h"
#include "../cargotype.h"
#include "../command_func.h"
#include "../company_base.h"
#include "../debug.h"
#include "../engine_base.h"
#include "../industry.h"
#include "../landscape_cmd.h"
#include "../order_base.h"
#include "../order_cmd.h"
#include "../road_cmd.h"
#include "../road_func.h"
#include "../road_map.h"
#include "../roadveh.h"
#include "../settings_type.h"
#include "../station_base.h"
#include "../station_cmd.h"
#include "../station_func.h"
#include "../station_map.h"
#include "../tilearea_type.h"
#include "../town.h"
#include "../vehicle_cmd.h"
#include <functional>
#include <map>
#include <queue>

#include "../safeguards.h"

namespace FutureLogistics {
namespace {
constexpr int TOWN_SEARCH_RADIUS = 12; ///< How far from a town centre a stop may go.
constexpr size_t MAX_FEEDER_TOWNS = 16; ///< Towns tried per feeder station.

/**
 * Convert a route direction to its road bit.
 * @param direction Diagonal direction.
 * @return Corresponding road bit.
 */
RoadBits Bit(DiagDirection direction)
{
	switch (direction) {
		case DiagDirection::NE: return RoadBit::NE;
		case DiagDirection::SE: return RoadBit::SE;
		case DiagDirection::SW: return RoadBit::SW;
		case DiagDirection::NW: return RoadBit::NW;
		default: NOT_REACHED();
	}
}

/**
 * Get a road stop's catchment radius as the station rules apply it.
 * @param type Bus or truck.
 * @return Radius in tiles.
 */
int StopCatchment(RoadStopType type)
{
	if (!_settings_game.station.modified_catchment) return CA_UNMODIFIED;
	return type == RoadStopType::Bus ? CA_BUS : CA_TRUCK;
}

/**
 * Find buildable stop tiles near a point.
 * @param centre Point to search around.
 * @param radius Search radius in tiles.
 * @param accept Extra check on a tile, such as cargo acceptance.
 * @return Candidate tiles in scan order.
 */
std::vector<TileIndex> FindStops(TileIndex centre, int radius, const std::function<bool(TileIndex)> &accept)
{
	std::vector<TileIndex> result;
	for (int y = std::max(1, (int)TileY(centre) - radius); y <= std::min((int)Map::SizeY() - 2, (int)TileY(centre) + radius); y++) {
		for (int x = std::max(1, (int)TileX(centre) - radius); x <= std::min((int)Map::SizeX() - 2, (int)TileX(centre) + radius); x++) {
			const TileIndex tile = TileXY(x, y);
			if (Buildable(tile) && accept(tile)) result.push_back(tile);
		}
	}
	return result;
}

/**
 * Get the stop type that serves a cargo: buses for passengers, truck stops for everything else, including mail.
 * @param cargo Cargo type.
 * @return Stop type.
 */
RoadStopType StopFor(CargoType cargo)
{
	return CargoSpec::Get(cargo)->classes.Test(CargoClass::Passengers) ? RoadStopType::Bus : RoadStopType::Truck;
}

/**
 * Find stop tiles in a town that both send and accept a town cargo.
 * @param town Town to serve.
 * @param cargo Passengers or mail.
 * @return Buildable candidate tiles belonging to this town's area.
 */
std::vector<TileIndex> TownStops(const Town *town, CargoType cargo)
{
	const int radius = StopCatchment(StopFor(cargo));
	return FindStops(town->xy, TOWN_SEARCH_RADIUS, [&](TileIndex tile) {
		if (ClosestTownFromTile(tile, UINT_MAX)->index != town->index) return false;
		return GetAcceptanceAroundTiles(tile, 1, 1, radius).first[cargo] >= 8 && GetProductionAroundTiles(tile, 1, 1, radius)[cargo] > 0;
	});
}

/** Proposed owned road corridor and attached depot. */
struct Route {
	std::vector<TileIndex> path; ///< Ordered tiles between the two stops.
	TileIndex depot = INVALID_TILE; ///< Depot tile, invalid if planning failed.
	size_t junction = 0; ///< Index of the road tile adjoining the depot.
};

/**
 * Find a corridor using bounded multi-source breadth-first search.
 * @param starts Candidate stops at the origin.
 * @param ends Candidate stops at the destination.
 * @return Proposed route, with an invalid depot if no suitable corridor exists.
 */
Route FindRoute(const std::vector<TileIndex> &starts, const std::vector<TileIndex> &ends)
{
	std::set<TileIndex> targets(ends.begin(), ends.end());
	std::map<TileIndex, TileIndex> previous;
	std::queue<TileIndex> queue;
	for (TileIndex tile : starts) {
		previous.emplace(tile, INVALID_TILE);
		queue.push(tile);
	}
	while (!queue.empty()) {
		TileIndex tile = queue.front();
		queue.pop();
		if (targets.contains(tile)) {
			Route route;
			for (TileIndex t = tile; t != INVALID_TILE; t = previous.at(t)) route.path.push_back(t);
			std::reverse(route.path.begin(), route.path.end());
			if (route.path.size() < 8 || route.path.size() > 128) continue;
			std::set<TileIndex> occupied(route.path.begin(), route.path.end());
			for (size_t i = 1; i + 1 < route.path.size(); i++) {
				for (TileIndex neighbour : Neighbours(route.path[i])) {
					if (!occupied.contains(neighbour) && Buildable(neighbour) && GetTileZ(neighbour) == GetTileZ(tile)) {
						route.depot = neighbour;
						route.junction = i;
						return route;
					}
				}
			}
		}
		for (TileIndex next : Neighbours(tile)) {
			if (previous.size() >= MAX_SEARCH) break;
			if (previous.contains(next) || !Buildable(next) || GetTileZ(next) != GetTileZ(tile)) continue;
			previous.emplace(next, tile);
			queue.push(next);
		}
	}
	return {};
}

/**
 * Calculate road connections for one corridor tile.
 * @param route Proposed corridor and depot.
 * @param i Tile index within the corridor.
 * @return Road bits connecting neighbours and, if applicable, the depot.
 */
RoadBits Pieces(const Route &route, size_t i)
{
	RoadBits bits;
	if (i > 0) bits |= Bit(RouteDirection(route.path[i], route.path[i - 1]));
	if (i + 1 < route.path.size()) bits |= Bit(RouteDirection(route.path[i], route.path[i + 1]));
	if (i == route.junction) bits |= Bit(RouteDirection(route.path[i], route.depot));
	/* End stops need both road halves, allowing vehicles to turn around at the dead end. */
	if (i == 0 || i + 1 == route.path.size()) {
		const auto dir = RouteDirection(route.path[i], route.path[i == 0 ? 1 : i - 1]);
		bits = (dir == DiagDirection::NE || dir == DiagDirection::SW) ? ROAD_X : ROAD_Y;
	}
	return bits;
}

/** What a road service runs. */
struct RoadJob {
	std::string_view kind; ///< Name for logs: bus, feeder or truck.
	RoadStopType stop; ///< Bus or truck stops.
	const Engine *engine; ///< Vehicle to buy.
	CargoType cargo; ///< Cargo to refit it to.
	uint vehicles; ///< Fleet size.
	bool full_load; ///< Full load at the first stop, as for freight.
	StationID join_first = NEW_STATION; ///< Existing station the first stop joins, such as a railway station.
	StationID join_last = NEW_STATION; ///< Existing station the last stop joins, such as a transfer station.
	bool transfer = false; ///< Transfer and leave empty at the last stop, for another leg to take on.
};

/**
 * Test or build a separate drive-through stop.
 * @param tile Station tile.
 * @param direction Road alignment direction.
 * @param roadtype Compatible road type.
 * @param job Service being built.
 * @param join Station to join, or NEW_STATION.
 * @param flags Command execution flags.
 * @return Command cost or rejection.
 */
CommandCost Stop(TileIndex tile, DiagDirection direction, RoadType roadtype, const RoadJob &job, StationID join, DoCommandFlags flags)
{
	return Command<Commands::BuildRoadStop>::Do(flags | CONSTRUCT, tile, 1, 1, job.stop, true, direction,
		roadtype, ROADSTOP_CLASS_DFLT, 0, join, true);
}

/**
 * Preflight the entire layout, then execute through accounting commands.
 * @param route Corridor and depot proposal.
 * @param job Service to build.
 * @param execute Whether to build after a successful preflight; false only checks.
 * @param[out] result_built If given, receives what was built on success.
 * @return Whether preflight rejected, execution failed, or service was built (or would be, when not executing).
 */
ConstructionResult Build(const Route &route, const RoadJob &job, bool execute = true, BuiltLeg *result_built = nullptr)
{
	const RoadType rt = job.engine->VehInfo<RoadVehicleInfo>().roadtype;
	Money cost = 0;
	for (size_t i = 0; i < route.path.size(); i++) {
		auto result = Command<Commands::BuildRoad>::Do(CONSTRUCT, route.path[i], Pieces(route, i), rt, {}, TownID::Invalid());
		if (result.Failed()) {
			Debug(Facility::Misc, Severity::Debug1, "FL road preflight rejected tile={} error={}", route.path[i], result.GetErrorMessage());
			return ConstructionResult::Rejected;
		}
		cost += result.GetCost();
	}
	const auto first_dir = RouteDirection(route.path[0], route.path[1]);
	const auto last_dir = RouteDirection(route.path.back(), route.path[route.path.size() - 2]);
	for (const auto &[tile, direction, join] : {std::tuple{route.path.front(), first_dir, job.join_first}, {route.path.back(), last_dir, job.join_last}}) {
		auto result = Stop(tile, direction, rt, job, join, {});
		if (result.Failed()) return ConstructionResult::Rejected;
		cost += result.GetCost();
	}
	const auto depot_dir = RouteDirection(route.depot, route.path[route.junction]);
	auto depot_test = Command<Commands::BuildRoadDepot>::Do(CONSTRUCT, route.depot, rt, depot_dir);
	if (depot_test.Failed()) return ConstructionResult::Rejected;
	cost += depot_test.GetCost();
	/* Reserve extra capital for the vehicles and any command-cost changes after construction. */
	const Company *company = Company::Get(_current_company);
	if (cost * 2 + job.engine->GetCost() * 2 * job.vehicles > company->money) return ConstructionResult::Rejected;
	/* Explicit demolition consumes clearing credits. Keep enough for a complete rollback. */
	if ((company->clear_limit >> 16) < route.path.size() * 3 + 8) return ConstructionResult::Rejected;
	if (!execute) return ConstructionResult::Success;

	std::vector<TileIndex> built;
	std::vector<VehicleID> vehicles;
	auto rollback = [&]() {
		for (VehicleID vehicle : vehicles) {
			if (Command<Commands::SellVehicle>::Do(EXEC, vehicle, true, false, ClientID::Invalid).Failed()) {
				Debug(Facility::Misc, Severity::Error, "FL rollback failed selling vehicle={}; infrastructure retained", vehicle);
				return;
			}
		}
		for (auto it = built.rbegin(); it != built.rend(); ++it) {
			auto result = Command<Commands::LandscapeClear>::Do(EXEC, *it);
			if (result.Failed()) {
				Debug(Facility::Misc, Severity::Error, "FL rollback failed tile={} error={}", *it, result.GetErrorMessage());
			}
		}
	};
	auto fail = [&](std::string_view phase) {
		Debug(Facility::Misc, Severity::Warning, "FL construction failed company={} phase={}; clearing new infrastructure", _current_company, phase);
		rollback();
		return ConstructionResult::Failed;
	};
	for (size_t i = 0; i < route.path.size(); i++) {
		if (Command<Commands::BuildRoad>::Do(EXEC | CONSTRUCT, route.path[i], Pieces(route, i), rt, {}, TownID::Invalid()).Failed()) return fail("road");
		built.push_back(route.path[i]);
	}
	if (Stop(route.path.front(), first_dir, rt, job, job.join_first, EXEC).Failed() || Stop(route.path.back(), last_dir, rt, job, job.join_last, EXEC).Failed()) return fail("stops");
	if (Command<Commands::BuildRoadDepot>::Do(EXEC | CONSTRUCT, route.depot, rt, depot_dir).Failed()) return fail("depot");
	built.push_back(route.depot);
	for (uint n = 0; n < job.vehicles; n++) {
		auto result = Command<Commands::BuildVehicle>::Do(EXEC, route.depot, job.engine->index, false, job.cargo, ClientID::Invalid);
		if (ExtractCommandCost(result).Failed()) return fail("vehicle");
		const VehicleID vehicle = std::get<1>(result);
		vehicles.push_back(vehicle);
		for (VehicleOrderID i = 0; i < 2; i++) {
			Order order;
			order.MakeGoToStation(GetStationIndex(i == 0 ? route.path.front() : route.path.back()));
			/* Non-train orders must use the far end; the default near end is rejected. */
			order.SetStopLocation(OrderStopLocation::FarEnd);
			if (job.full_load) order.SetLoadType(i == 0 ? OrderLoadType::FullLoadAny : OrderLoadType::NoLoad);
			/* Transfer and leave empty: the cargo waits at the station for the next leg's vehicles. */
			if (job.transfer && i == 1) order.SetUnloadType(OrderUnloadType::Transfer);
			if (Command<Commands::InsertOrder>::Do(EXEC, vehicle, i, order).Failed()) return fail("orders");
		}
		if (Command<Commands::StartStopVehicle>::Do(EXEC, vehicle, true).Failed()) return fail("start");
	}
	uint64_t signature = 14695981039346656037ULL;
	for (TileIndex tile : route.path) signature = (signature ^ tile.base()) * 1099511628211ULL;
	Debug(Facility::Misc, Severity::Notice, "FL route company={} kind={} cargo={} vehicles={} vehicle={} from={} to={} depot={} tiles={} balance={} signature={}",
		_current_company, job.kind, CargoSpec::Get(job.cargo)->label.AsString(), job.vehicles, vehicles.front(), route.path.front(), route.path.back(),
		route.depot, route.path.size(), Company::Get(_current_company)->money, signature);
	if (result_built != nullptr) {
		result_built->from = GetStationIndex(route.path.front());
		result_built->to = GetStationIndex(route.path.back());
		result_built->from_tile = route.path.front();
		result_built->to_tile = route.path.back();
		/* The stops sit on the road tiles, so clearing the road clears them too. */
		result_built->tiles = std::move(built);
		result_built->vehicles = std::move(vehicles);
	}
	return ConstructionResult::Success;
}

/**
 * Pick the best road vehicle for a job. Every road type the company can build that is untagged or
 * tagged with the service's family is considered; the fastest suitable vehicle wins, and ties go
 * to the road type listed first. The road is then built with that vehicle's road type.
 * @param library Loaded transport technology, including the road types.
 * @param spec Family and preferences of the service.
 * @param role Job the vehicle does.
 * @param cargo Cargo it carries.
 * @return Engine, or nullptr if none is available.
 */
const Engine *ChooseRoadVehicle(const TechLibrary &library, const ServiceSpec &spec, TechRole role, CargoType cargo)
{
	const TechRecord *record = nullptr;
	for (const InfraRecord &road : library.Infrastructure()) {
		if (road.kind != InfraKind::RoadType || !spec.AllowsInfrastructure(road.tech)) continue;
		const RoadType roadtype = static_cast<RoadType>(road.index);
		if (!HasRoadTypeAvail(_current_company, roadtype)) continue;
		TechQuery query;
		query.mode = TechMode::Road;
		query.role = role;
		query.cargo = cargo;
		query.company = _current_company;
		query.roadtype = roadtype;
		query.family = spec.family;
		query.provides = road.provides;
		query.early_adopters = spec.early_adopters;
		const TechRecord *candidate = library.FindBest(query);
		if (candidate != nullptr && (record == nullptr || candidate->speed > record->speed)) record = candidate;
	}
	if (record == nullptr) return nullptr;
	Debug(Facility::Misc, Severity::Notice, "FL tech company={} role={} vehicle={}", _current_company, TechRoleName(role), record->id);
	return Engine::Get(record->engine);
}
} // namespace

/**
 * Build a road route carrying passengers (buses) or mail (mail trucks) between two unused towns.
 * @param library Loaded transport technology.
 * @param spec Family, limits and preferences of the service.
 * @param cargo Passengers or mail.
 * @param[in,out] used_towns Towns already served for this cargo; the chosen towns are added on success.
 * @return Outcome.
 */
ConstructionResult BuildTownRoad(const TechLibrary &library, const ServiceSpec &spec, CargoType cargo, std::set<TownID> &used_towns)
{
	if (!IsValidCargoType(cargo)) return ConstructionResult::Rejected;
	const RoadStopType stop = StopFor(cargo);
	const Engine *bus = ChooseRoadVehicle(library, spec, TownRole(library, cargo, TechRole::PassengerFeeder), cargo);
	if (bus == nullptr) return ConstructionResult::Rejected;
	struct Pair { uint distance; const Town *a; const Town *b; };
	std::vector<Pair> pairs;
	std::vector<const Town *> towns;
	for (const Town *town : Town::Iterate()) {
		if (towns.size() == 128) break;
		towns.push_back(town);
	}
	for (const Town *a : towns) {
		for (const Town *b : towns) {
			if (a->index >= b->index || used_towns.contains(a->index) || used_towns.contains(b->index)) continue;
			uint distance = DistanceManhattan(a->xy, b->xy);
			if (distance >= 16 && distance <= 96 && TierForTowns(a, b) >= spec.min_tier) pairs.push_back({distance, a, b});
		}
	}
	std::stable_sort(pairs.begin(), pairs.end(), [](const Pair &a, const Pair &b) { return a.distance < b.distance; });
	std::map<TownID, std::vector<TileIndex>> candidates;
	for (size_t i = 0; i < std::min(MAX_PAIRS, pairs.size()); i++) {
		const auto &pair = pairs[i];
		for (const Town *town : {pair.a, pair.b}) {
			if (!candidates.contains(town->index)) candidates.emplace(town->index, TownStops(town, cargo));
		}
		auto route = FindRoute(candidates.at(pair.a->index), candidates.at(pair.b->index));
		if (route.depot == INVALID_TILE) continue;
		const MarketTier tier = TierForTowns(pair.a, pair.b);
		auto result = Build(route, {stop == RoadStopType::Bus ? "bus" : "mail", stop, bus, cargo, GetTierLimits(tier).fleet, false});
		if (result == ConstructionResult::Failed) return ConstructionResult::Failed;
		if (result == ConstructionResult::Success) {
			used_towns.insert(pair.a->index);
			used_towns.insert(pair.b->index);
			Debug(Facility::Misc, Severity::Notice, "FL towns company={} a={} b={} tier={} population={},{}", _current_company, pair.a->index, pair.b->index,
				TierName(tier), pair.a->cache.population, pair.b->cache.population);
			return ConstructionResult::Success;
		}
	}
	return ConstructionResult::Rejected;
}

/**
 * Build a bus route that feeds one of the company's railway stations from a nearby unused town.
 * The first bus stop joins the railway station, so the two form one interchange.
 * @param library Loaded transport technology.
 * @param[in,out] used_towns Towns reserved by earlier rivals; the fed town is added on success.
 * @return Outcome; Rejected if the company has no railway station yet.
 */
ConstructionResult BuildFeederBus(const TechLibrary &library, const ServiceSpec &spec, std::set<TownID> &used_towns)
{
	const CargoType cargo = GetCargoTypeByLabel(CT_PASSENGERS);
	if (!IsValidCargoType(cargo)) return ConstructionResult::Rejected;
	const Engine *bus = ChooseRoadVehicle(library, spec, TechRole::PassengerFeeder, cargo);
	if (bus == nullptr) return ConstructionResult::Rejected;
	for (const Station *station : Station::Iterate()) {
		if (station->owner != _current_company || !station->facilities.Test(StationFacility::Train) || station->town == nullptr) continue;
		/* Stops right next to the platforms, so they can join the railway station. */
		TileArea ring = station->train_station;
		ring.Expand(1);
		std::vector<TileIndex> starts;
		for (TileIndex tile : ring) {
			if (!station->train_station.Contains(tile) && Buildable(tile)) starts.push_back(tile);
		}
		if (starts.empty()) continue;
		struct Candidate { uint distance; const Town *town; };
		std::vector<Candidate> towns;
		for (const Town *town : Town::Iterate()) {
			if (town == station->town || used_towns.contains(town->index)) continue;
			const uint distance = DistanceManhattan(town->xy, station->xy);
			if (distance >= 16 && distance <= 64 && TierForTowns(station->town, town) >= spec.min_tier) towns.push_back({distance, town});
		}
		std::ranges::stable_sort(towns, {}, &Candidate::distance);
		if (towns.size() > MAX_FEEDER_TOWNS) towns.resize(MAX_FEEDER_TOWNS);
		for (const Candidate &candidate : towns) {
			auto route = FindRoute(starts, TownStops(candidate.town, cargo));
			if (route.depot == INVALID_TILE) continue;
			const MarketTier tier = TierForTowns(station->town, candidate.town);
			RoadJob job{"feeder", RoadStopType::Bus, bus, cargo, GetTierLimits(tier).fleet, false, station->index};
			const auto result = Build(route, job);
			if (result == ConstructionResult::Rejected) continue;
			if (result == ConstructionResult::Success) {
				used_towns.insert(candidate.town->index);
				Debug(Facility::Misc, Severity::Notice, "FL feeder company={} station={} town={} tier={}", _current_company, station->index, candidate.town->index, TierName(tier));
			}
			return result;
		}
	}
	return ConstructionResult::Rejected;
}

/**
 * Find truck stop tiles for one end of a freight leg.
 * @param place Industry, town or transfer station.
 * @param cargo Cargo carried.
 * @param destination Whether the cargo is delivered here, so the stop must accept it.
 * @return Candidate tiles.
 */
static std::vector<TileIndex> FreightStops(const Place &place, CargoType cargo, bool destination)
{
	const int radius = StopCatchment(RoadStopType::Truck);
	auto accepts = [&](TileIndex tile) { return !destination || GetAcceptanceAroundTiles(tile, 1, 1, radius).first[cargo] >= 8; };
	if (place.IsHub()) {
		auto stops = FindStops(place.hub, place.radius, [](TileIndex) { return true; });
		std::ranges::stable_sort(stops, {}, [&](TileIndex tile) { return DistanceManhattan(tile, place.hub); });
		KeepUsable(stops, place, [](TileIndex tile) { return tile; }, place.usable ? 2 : SIZE_MAX);
		return stops;
	}
	if (place.town != nullptr) {
		return FindStops(place.town->xy, TOWN_SEARCH_RADIUS, [&](TileIndex tile) {
			return ClosestTownFromTile(tile, UINT_MAX) == place.town && accepts(tile);
		});
	}
	const Industry *industry = place.industry;
	const int search = std::max<int>(industry->location.w, industry->location.h) / 2 + radius;
	return FindStops(Centre(industry), search, [&](TileIndex tile) {
		TileArea area(tile, 1, 1);
		area.Expand(radius);
		return area.Intersects(industry->location) && accepts(tile);
	});
}

/**
 * Build, or check, a truck leg: trucks that full-load at one stop and deliver or transfer at the other.
 * Trucks also deliver to towns, so goods and food can be distributed. The fleet follows the market size.
 * @copydoc LegBuild
 */
ConstructionResult RoadLeg(const TechLibrary &library, const ServiceSpec &spec, const Place &from, const Place &to, const LegJob &job, bool execute, BuiltLeg &built)
{
	const Engine *truck = ChooseRoadVehicle(library, spec, FreightRole(library, job.cargo), job.cargo);
	if (truck == nullptr) return ConstructionResult::Rejected;
	const auto origins = FreightStops(from, job.cargo, false);
	if (origins.empty()) return ConstructionResult::Rejected;
	const auto destinations = FreightStops(to, job.cargo, true);
	if (destinations.empty()) return ConstructionResult::Rejected;
	built.sited = true;
	const auto route = FindRoute(origins, destinations);
	if (route.depot == INVALID_TILE) return ConstructionResult::Rejected;
	const RoadJob road_job{"truck", RoadStopType::Truck, truck, job.cargo, GetTierLimits(job.tier).fleet, true, from.join, to.join, job.transfer};
	return Build(route, road_job, execute, &built);
}

/**
 * Follow a road vehicle's owned road from where it is now.
 * @param vehicle Road vehicle to inspect.
 * @return Road stops the vehicle can reach over its owner's connected road.
 */
std::set<StationID> RoadStationsReachable(const Vehicle *vehicle)
{
	std::set<TileIndex> seen{vehicle->tile};
	std::set<StationID> reached;
	std::queue<TileIndex> queue;
	queue.push(vehicle->tile);
	while (!queue.empty() && seen.size() <= MAX_SEARCH) {
		TileIndex tile = queue.front(); queue.pop();
		if (IsStationRoadStopTile(tile) && GetTileOwner(tile) == vehicle->owner) reached.insert(GetStationIndex(tile));
		for (TileIndex next : Neighbours(tile)) {
			if (seen.contains(next) || GetAnyRoadBits(next, RoadTramType::Road).None()) continue;
			if (GetRoadOwner(next, RoadTramType::Road) != vehicle->owner) continue;
			if (!GetAnyRoadBits(tile, RoadTramType::Road).Any(Bit(RouteDirection(tile, next))) || !GetAnyRoadBits(next, RoadTramType::Road).Any(Bit(RouteDirection(next, tile)))) continue;
			seen.insert(next); queue.push(next);
		}
	}
	return reached;
}

} // namespace FutureLogistics
