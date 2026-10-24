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
#include "../bridge.h"
#include "../rail_map.h"
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
#include "../tunnelbridge_cmd.h"
#include "../tunnelbridge_map.h"
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
 * Check whether a tile has a plain public road that a company's vehicles may use and that a route may
 * join: flat, owned by a town or nobody, without one-way rules or tram track, and of a road type the
 * vehicles can drive on.
 * @param tile Tile to check.
 * @param rt Road type of the route's vehicles.
 * @return Whether the route may use the road.
 */
bool PublicRoad(TileIndex tile, RoadType rt)
{
	if (!IsValidTile(tile) || !IsNormalRoadTile(tile) || !IsTileFlat(tile)) return false;
	if (!HasTileRoadType(tile, RoadTramType::Road) || HasTileRoadType(tile, RoadTramType::Tram)) return false;
	const Owner owner = GetRoadOwner(tile, RoadTramType::Road);
	if (owner != OWNER_TOWN && owner != OWNER_NONE) return false;
	return GetDisallowedRoadDirections(tile) == DisallowedRoadDirections{} && HasPowerOnRoad(rt, GetRoadTypeRoad(tile));
}

/**
 * Check whether a public road tile is a straight piece, where a drive-through stop can go.
 * @param tile Road tile.
 * @return Whether it has exactly one straight road.
 */
bool StraightRoad(TileIndex tile)
{
	const RoadBits bits = GetRoadBits(tile, RoadTramType::Road);
	return bits == ROAD_X || bits == ROAD_Y;
}

/**
 * Find stop tiles near a point: clear land for a new stop, or a straight piece of public road for a
 * drive-through stop on it.
 * @param centre Point to search around.
 * @param radius Search radius in tiles.
 * @param rt Road type of the route's vehicles.
 * @param accept Extra check on a tile, such as cargo acceptance.
 * @return Candidate tiles in scan order.
 */
std::vector<TileIndex> FindStops(TileIndex centre, int radius, RoadType rt, const std::function<bool(TileIndex)> &accept)
{
	std::vector<TileIndex> result;
	for (int y = std::max(1, (int)TileY(centre) - radius); y <= std::min((int)Map::SizeY() - 2, (int)TileY(centre) + radius); y++) {
		for (int x = std::max(1, (int)TileX(centre) - radius); x <= std::min((int)Map::SizeX() - 2, (int)TileX(centre) + radius); x++) {
			const TileIndex tile = TileXY(x, y);
			if ((Buildable(tile) || (PublicRoad(tile, rt) && StraightRoad(tile))) && accept(tile)) result.push_back(tile);
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
 * @param rt Road type of the route's vehicles.
 * @return Candidate tiles belonging to this town's area.
 */
std::vector<TileIndex> TownStops(const Town *town, CargoType cargo, RoadType rt)
{
	const int radius = StopCatchment(StopFor(cargo));
	return FindStops(town->xy, TOWN_SEARCH_RADIUS, rt, [&](TileIndex tile) {
		if (ClosestTownFromTile(tile, UINT_MAX)->index != town->index) return false;
		return GetAcceptanceAroundTiles(tile, 1, 1, radius).first[cargo] >= 8 && GetProductionAroundTiles(tile, 1, 1, radius)[cargo] > 0;
	});
}

/** Proposed owned road corridor and attached depot. */
struct Route {
	std::vector<TileIndex> path; ///< Ordered tiles between the two stops.
	TileIndex depot = INVALID_TILE; ///< Depot tile, invalid if planning failed.
	size_t junction = 0; ///< Index of the road tile adjoining the depot.
	std::set<TileIndex> bridged; ///< Tiles under a bridge; the route's tiles either side of a run of them are its heads.
	std::set<TileIndex> tunnelled; ///< Tiles over a tunnel; the route's tiles either side of a run of them are its portals.
};

/**
 * Check whether a route may step from one tile to the next. New road goes on clear land, flat or
 * climbing straight over a tile inclined along it, and the ground must meet where the tiles touch.
 * It may join public road, adding the piece that connects them, but two public road tiles are only
 * followed where they already connect, and a stop on public road is only entered along the road, so
 * no street gains a junction there.
 * @param before Tile the route came from, or INVALID_TILE at a stop.
 * @param off_bridge Whether \a from is the far head of a bridge.
 * @param from Tile the route is on.
 * @param to Adjacent tile.
 * @param rt Road type of the route's vehicles.
 * @param stops Candidate stops at both ends.
 * @return Whether the step is allowed.
 */
bool CanStep(TileIndex before, bool off_bridge, TileIndex from, TileIndex to, RoadType rt, const std::set<TileIndex> &stops)
{
	const DiagDirection dir = RouteDirection(from, to);
	/* Road on a slope, or off a bridge, runs straight. */
	const bool straight = !IsTileFlat(from) || off_bridge;
	if (straight && (before == INVALID_TILE || RouteDirection(before, from) != dir)) return false;
	const auto leaving = SideHeight(from, dir);
	if (!leaving.has_value() || leaving != SideHeight(to, ReverseDiagDir(dir))) return false;
	const bool from_road = PublicRoad(from, rt);
	const bool to_road = PublicRoad(to, rt);
	if (!to_road && !OpenGround(to)) return false;
	if (from_road && to_road) {
		return GetRoadBits(from, RoadTramType::Road).Any(Bit(RouteDirection(from, to))) && GetRoadBits(to, RoadTramType::Road).Any(Bit(RouteDirection(to, from)));
	}
	/* Joining a road adds a piece to it unless it already points this way; never add one to a stop's tile. */
	if (from_road && stops.contains(from) && !GetRoadBits(from, RoadTramType::Road).Any(Bit(RouteDirection(from, to)))) return false;
	if (to_road && stops.contains(to) && !GetRoadBits(to, RoadTramType::Road).Any(Bit(RouteDirection(to, from)))) return false;
	return true;
}

/**
 * Find a corridor using bounded multi-source breadth-first search, over clear land and public roads.
 * @param starts Candidate stops at the origin.
 * @param ends Candidate stops at the destination.
 * @param rt Road type of the route's vehicles.
 * @param bridges Whether the corridor may use bridges and tunnels.
 * @param[out] spannable If given, set when the search passed somewhere a bridge or tunnel could have gone.
 * @return Proposed route, with an invalid depot if no suitable corridor exists.
 */
static Route SearchRoute(const std::vector<TileIndex> &starts, const std::vector<TileIndex> &ends, RoadType rt, bool bridges, bool *spannable = nullptr)
{
	std::set<TileIndex> stops(starts.begin(), starts.end());
	stops.insert(ends.begin(), ends.end());
	std::set<TileIndex> targets(ends.begin(), ends.end());
	std::map<TileIndex, TileIndex> previous;
	std::set<TileIndex> middles; ///< Tiles under a bridge or over a tunnel.
	std::set<TileIndex> tunnel_middles; ///< Tiles over a tunnel.
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
			for (TileIndex t = tile; t != INVALID_TILE; t = previous.at(t)) {
				route.path.push_back(t);
				if (tunnel_middles.contains(t)) {
					route.tunnelled.insert(t);
				} else if (middles.contains(t)) {
					route.bridged.insert(t);
				}
			}
			std::reverse(route.path.begin(), route.path.end());
			if (route.path.size() < 8 || route.path.size() > 128) continue;
			std::set<TileIndex> occupied(route.path.begin(), route.path.end());
			auto bridged = [&](size_t j) { return j < route.path.size() && (route.bridged.contains(route.path[j]) || route.tunnelled.contains(route.path[j])); };
			for (size_t i = 1; i + 1 < route.path.size(); i++) {
				/* Not on a slope, nor at a bridge or tunnel. */
				if (!IsTileFlat(route.path[i]) || bridged(i) || bridged(i - 1) || bridged(i + 1)) continue;
				for (TileIndex neighbour : Neighbours(route.path[i])) {
					if (!occupied.contains(neighbour) && Buildable(neighbour) && GetTileZ(neighbour) == GetTileZ(route.path[i])) {
						route.depot = neighbour;
						route.junction = i;
						return route;
					}
				}
			}
		}
		const TileIndex before = previous.at(tile);
		const bool off_bridge = middles.contains(before);
		/* Having climbed onto a slope, the road may instead go through the hill in a tunnel. */
		if ((bridges || spannable != nullptr) && !off_bridge && before != INVALID_TILE && !stops.contains(tile)) {
			const DiagDirection dir = RouteDirection(before, tile);
			const BridgeSpan tunnel = FindTunnel(tile, dir);
			if (!bridges && tunnel.land != INVALID_TILE) *spannable = true;
			bool clear = bridges && tunnel.land != INVALID_TILE && !previous.contains(tunnel.land) && !stops.contains(tunnel.land);
			for (uint k = 1; k <= tunnel.length && clear; k++) clear = !previous.contains(Step(tile, dir, k));
			if (clear) {
				TileIndex last = tile;
				for (uint k = 1; k <= tunnel.length; k++) {
					const TileIndex middle = Step(tile, dir, k);
					middles.insert(middle);
					tunnel_middles.insert(middle);
					previous.emplace(middle, last);
					last = middle;
				}
				previous.emplace(tunnel.land, last);
				queue.push(tunnel.land);
			}
		}
		for (TileIndex next : Neighbours(tile)) {
			if (previous.size() >= MAX_SEARCH) break;
			if (previous.contains(next)) continue;
			if (CanStep(before, off_bridge, tile, next, rt, stops)) {
				previous.emplace(next, tile);
				queue.push(next);
				continue;
			}
			/* A bridge over water, a railway, a road or a dip, entered straight from flat open land and landing on it. */
			const DiagDirection dir = RouteDirection(tile, next);
			if (off_bridge || before == INVALID_TILE || RouteDirection(before, tile) != dir || stops.contains(tile)) continue;
			if (!bridges && spannable == nullptr) continue;
			const BridgeSpan bridge = FindBridge(tile, dir);
			if (!bridges) {
				if (bridge.land != INVALID_TILE) *spannable = true;
				continue;
			}
			if (bridge.land == INVALID_TILE || previous.contains(bridge.land) || stops.contains(bridge.land)) continue;
			bool clear = true;
			for (uint k = 1; k <= bridge.length; k++) clear &= !previous.contains(Step(tile, dir, k));
			if (!clear) continue;
			TileIndex last = tile;
			for (uint k = 1; k <= bridge.length; k++) {
				const TileIndex middle = Step(tile, dir, k);
				middles.insert(middle);
				previous.emplace(middle, last);
				last = middle;
			}
			previous.emplace(bridge.land, last);
			queue.push(bridge.land);
		}
	}
	return {};
}

/**
 * Find a corridor, using bridges only if there is no other way, since bridges cost more.
 * @param starts Candidate stops at the origin.
 * @param ends Candidate stops at the destination.
 * @param rt Road type of the route's vehicles.
 * @return Proposed route, with an invalid depot if no suitable corridor exists.
 */
Route FindRoute(const std::vector<TileIndex> &starts, const std::vector<TileIndex> &ends, RoadType rt)
{
	/* Searching again only helps if the first search passed somewhere a bridge or tunnel could have gone. */
	bool spannable = false;
	Route route = SearchRoute(starts, ends, rt, false, &spannable);
	return route.depot != INVALID_TILE || !spannable ? route : SearchRoute(starts, ends, rt, true);
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
	/* Public road on the route keeps its owner and road type; it only gains the pieces it lacks. */
	std::vector<bool> on_road;
	for (TileIndex tile : route.path) on_road.push_back(PublicRoad(tile, rt));
	auto missing = [&](size_t i) {
		RoadBits bits = Pieces(route, i);
		if (on_road[i]) bits.Reset(GetRoadBits(route.path[i], RoadTramType::Road));
		return bits;
	};
	auto type_at = [&](size_t i) { return on_road[i] ? GetRoadTypeRoad(route.path[i]) : rt; };
	/* Bridges are built from head to head; their heads and the tiles under them get no road of their own. */
	const auto bridges = RouteBridges(route.path, route.bridged);
	const auto tunnels = RouteBridges(route.path, route.tunnelled);
	std::vector<bool> bridge_part(route.path.size(), false);
	for (const auto *spans : {&bridges, &tunnels}) {
		for (const RouteBridge &b : *spans) std::fill(bridge_part.begin() + b.head, bridge_part.begin() + b.tail + 1, true);
	}
	auto build_tunnel = [&](const RouteBridge &t, DoCommandFlags flags) {
		return Command<Commands::BuildTunnel>::Do(flags | CONSTRUCT, route.path[t.head], TransportType::Road, INVALID_RAILTYPE, rt);
	};
	auto build_bridge = [&](const RouteBridge &b, DoCommandFlags flags) {
		const BridgeType type = CheapestBridge(static_cast<uint>(b.tail - b.head - 1));
		if (type == MAX_BRIDGES) return CommandCost(CMD_ERROR);
		return Command<Commands::BuildBridge>::Do(flags | CONSTRUCT, route.path[b.tail], route.path[b.head], TransportType::Road, type, INVALID_RAILTYPE, rt);
	};
	Money cost = 0;
	for (const RouteBridge &b : bridges) {
		auto result = build_bridge(b, {});
		if (result.Failed()) return ConstructionResult::Rejected;
		cost += result.GetCost();
	}
	for (const RouteBridge &t : tunnels) {
		auto result = build_tunnel(t, {});
		if (result.Failed()) return ConstructionResult::Rejected;
		cost += result.GetCost();
	}
	for (size_t i = 0; i < route.path.size(); i++) {
		if (bridge_part[i] || missing(i).None()) continue;
		auto result = Command<Commands::BuildRoad>::Do(CONSTRUCT, route.path[i], missing(i), type_at(i), {}, TownID::Invalid());
		if (result.Failed()) {
			Debug(Facility::Misc, Severity::Debug1, "FL road preflight rejected tile={} error={}", route.path[i], result.GetErrorMessage());
			return ConstructionResult::Rejected;
		}
		cost += result.GetCost();
	}
	const auto first_dir = RouteDirection(route.path[0], route.path[1]);
	const auto last_dir = RouteDirection(route.path.back(), route.path[route.path.size() - 2]);
	const size_t last = route.path.size() - 1;
	for (const auto &[i, direction, join] : {std::tuple{size_t{0}, first_dir, job.join_first}, {last, last_dir, job.join_last}}) {
		auto result = Stop(route.path[i], direction, type_at(i), job, join, {});
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
	std::vector<TileIndex> road_stops; ///< Stops on public road: removed without the road.
	std::vector<VehicleID> vehicles;
	auto rollback = [&]() {
		for (VehicleID vehicle : vehicles) {
			if (Command<Commands::SellVehicle>::Do(EXEC, vehicle, true, false, ClientID::Invalid).Failed()) {
				Debug(Facility::Misc, Severity::Error, "FL rollback failed selling vehicle={}; infrastructure retained", vehicle);
				return;
			}
		}
		for (TileIndex tile : road_stops) {
			auto result = Command<Commands::RemoveRoadStop>::Do(EXEC, tile, 1, 1, job.stop, false);
			if (result.Failed()) Debug(Facility::Misc, Severity::Error, "FL rollback failed tile={} error={}", tile, result.GetErrorMessage());
		}
		/* Pieces added to public road are left; the road itself is never removed. */
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
	for (const RouteBridge &b : bridges) {
		if (build_bridge(b, EXEC).Failed()) return fail("bridge");
		/* Clearing a bridge head removes the whole bridge and leaves what is under it. */
		built.push_back(route.path[b.head]);
	}
	for (const RouteBridge &t : tunnels) {
		if (build_tunnel(t, EXEC).Failed()) return fail("tunnel");
		/* Clearing a portal removes the whole tunnel. */
		built.push_back(route.path[t.head]);
	}
	for (size_t i = 0; i < route.path.size(); i++) {
		if (bridge_part[i]) continue;
		if (!missing(i).None() && Command<Commands::BuildRoad>::Do(EXEC | CONSTRUCT, route.path[i], missing(i), type_at(i), {}, TownID::Invalid()).Failed()) return fail("road");
		if (!on_road[i]) built.push_back(route.path[i]);
	}
	for (const auto &[i, direction, join] : {std::tuple{size_t{0}, first_dir, job.join_first}, {last, last_dir, job.join_last}}) {
		if (Stop(route.path[i], direction, type_at(i), job, join, EXEC).Failed()) return fail("stops");
		if (on_road[i]) road_stops.push_back(route.path[i]);
	}
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
			/* Non-stop: routes share town streets, where other drive-through stops would otherwise stop the vehicle. */
			order.SetNonStopType(OrderNonStopFlag::NonStop);
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
	Debug(Facility::Misc, Severity::Notice, "FL route company={} kind={} cargo={} vehicles={} vehicle={} from={} to={} depot={} tiles={} balance={} signature={} public_road={} bridges={} tunnels={} slopes={}",
		_current_company, job.kind, CargoSpec::Get(job.cargo)->label.AsString(), job.vehicles, vehicles.front(), route.path.front(), route.path.back(),
		route.depot, route.path.size(), Company::Get(_current_company)->money, signature, std::ranges::count(on_road, true), bridges.size(), tunnels.size(),
		std::ranges::count_if(route.path, [](TileIndex t) { return !IsTileFlat(t); }));
	if (result_built != nullptr) {
		result_built->from = GetStationIndex(route.path.front());
		result_built->to = GetStationIndex(route.path.back());
		result_built->from_tile = route.path.front();
		result_built->to_tile = route.path.back();
		/* The stops sit on the road tiles, so clearing the road clears them too; a bridge goes with its head. */
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
			if (!candidates.contains(town->index)) candidates.emplace(town->index, TownStops(town, cargo, bus->VehInfo<RoadVehicleInfo>().roadtype));
		}
		auto route = FindRoute(candidates.at(pair.a->index), candidates.at(pair.b->index), bus->VehInfo<RoadVehicleInfo>().roadtype);
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
	const RoadType rt = bus->VehInfo<RoadVehicleInfo>().roadtype;
	for (const Station *station : Station::Iterate()) {
		if (station->owner != _current_company || !station->facilities.Test(StationFacility::Train) || station->town == nullptr) continue;
		/* Stops right next to the platforms, so they can join the railway station: on open land or a straight town road, as other stops. */
		TileArea ring = station->train_station;
		ring.Expand(1);
		std::vector<TileIndex> starts;
		for (TileIndex tile : ring) {
			if (!station->train_station.Contains(tile) && (Buildable(tile) || (PublicRoad(tile, rt) && StraightRoad(tile)))) starts.push_back(tile);
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
			auto route = FindRoute(starts, TownStops(candidate.town, cargo, rt), rt);
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
 * @param rt Road type of the route's vehicles.
 * @return Candidate tiles.
 */
static std::vector<TileIndex> FreightStops(const Place &place, CargoType cargo, bool destination, RoadType rt)
{
	const int radius = StopCatchment(RoadStopType::Truck);
	auto accepts = [&](TileIndex tile) { return !destination || GetAcceptanceAroundTiles(tile, 1, 1, radius).first[cargo] >= 8; };
	if (place.IsHub()) {
		auto stops = FindStops(place.hub, place.radius, rt, [](TileIndex) { return true; });
		std::ranges::stable_sort(stops, {}, [&](TileIndex tile) { return DistanceManhattan(tile, place.hub); });
		KeepUsable(stops, place, [](TileIndex tile) { return tile; }, place.usable ? 2 : SIZE_MAX);
		return stops;
	}
	if (place.town != nullptr) {
		return FindStops(place.town->xy, TOWN_SEARCH_RADIUS, rt, [&](TileIndex tile) {
			return ClosestTownFromTile(tile, UINT_MAX) == place.town && accepts(tile);
		});
	}
	const Industry *industry = place.industry;
	const int search = std::max<int>(industry->location.w, industry->location.h) / 2 + radius;
	return FindStops(Centre(industry), search, rt, [&](TileIndex tile) {
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
	const RoadType rt = truck->VehInfo<RoadVehicleInfo>().roadtype;
	const auto origins = FreightStops(from, job.cargo, false, rt);
	if (origins.empty()) return ConstructionResult::Rejected;
	const auto destinations = FreightStops(to, job.cargo, true, rt);
	if (destinations.empty()) return ConstructionResult::Rejected;
	built.sited = true;
	const auto route = FindRoute(origins, destinations, rt);
	if (route.depot == INVALID_TILE) return ConstructionResult::Rejected;
	const RoadJob road_job{"truck", RoadStopType::Truck, truck, job.cargo, GetTierLimits(job.tier).fleet, true, from.join, to.join, job.transfer};
	return Build(route, road_job, execute, &built);
}

/**
 * Follow a road vehicle's owned road from where it is now.
 * @param vehicle Road vehicle to inspect.
 * @param[out] depot If given, receives the first of the owner's road depots found on that road, or INVALID_TILE.
 * @return Road stops of the owner that the vehicle can reach over its owner's, town and unowned roads.
 */
std::set<StationID> RoadStationsReachable(const Vehicle *vehicle, TileIndex *depot)
{
	std::set<TileIndex> seen{vehicle->tile};
	std::set<StationID> reached;
	std::queue<TileIndex> queue;
	queue.push(vehicle->tile);
	while (!queue.empty() && seen.size() <= MAX_SEARCH) {
		TileIndex tile = queue.front(); queue.pop();
		if (IsStationRoadStopTile(tile) && GetTileOwner(tile) == vehicle->owner) reached.insert(GetStationIndex(tile));
		if (depot != nullptr && *depot == INVALID_TILE && IsRoadDepotTile(tile) && GetTileOwner(tile) == vehicle->owner) *depot = tile;
		/* Across a road bridge or through a tunnel to its other end. */
		if (IsTileType(tile, TileType::TunnelBridge) && GetTunnelBridgeTransportType(tile) == TransportType::Road) {
			const TileIndex other = GetOtherTunnelBridgeEnd(tile);
			if (!seen.contains(other)) { seen.insert(other); queue.push(other); }
		}
		for (TileIndex next : Neighbours(tile)) {
			if (seen.contains(next) || GetAnyRoadBits(next, RoadTramType::Road).None()) continue;
			/* Road vehicles may also use town and unowned roads, which towns build onto company roads as they grow. */
			const Owner road_owner = GetRoadOwner(next, RoadTramType::Road);
			if (road_owner != vehicle->owner && road_owner != OWNER_TOWN && road_owner != OWNER_NONE) continue;
			if (!GetAnyRoadBits(tile, RoadTramType::Road).Any(Bit(RouteDirection(tile, next))) || !GetAnyRoadBits(next, RoadTramType::Road).Any(Bit(RouteDirection(next, tile)))) continue;
			seen.insert(next); queue.push(next);
		}
	}
	return reached;
}

} // namespace FutureLogistics
