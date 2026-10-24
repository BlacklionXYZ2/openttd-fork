/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file rail_planner.cpp Day-zero railways: two terminus stations, a single line, a depot and one train. */
#include "../stdafx.h"
#include "rail_planner.h"
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
#include "../newgrf_station.h"
#include "../order_base.h"
#include "../order_cmd.h"
#include "../rail.h"
#include "../rail_cmd.h"
#include "../rail_map.h"
#include "../road_map.h"
#include "../settings_type.h"
#include "../signal_func.h"
#include "../station_cmd.h"
#include "../station_func.h"
#include "../station_map.h"
#include "../tilearea_type.h"
#include "../town.h"
#include "../track_func.h"
#include "../train.h"
#include "../tunnelbridge_map.h"
#include "../tunnelbridge_cmd.h"
#include "../bridge.h"
#include "../train_cmd.h"
#include "../vehicle_cmd.h"
#include <functional>
#include <map>
#include <queue>

#include "../safeguards.h"

namespace FutureLogistics {
namespace {
constexpr uint MAX_WAGONS = 6; ///< Upper bound on wagons, before the platform length check.
constexpr size_t MAX_ENDPOINTS = 2; ///< Station sites tried per town or industry.
constexpr size_t MAX_RAIL_PAIRS = 32; ///< Town or industry pairs tried per service.
constexpr size_t MAX_RAIL_STATES = 32768; ///< Pathfinder states per attempt.
constexpr std::array<DiagDirection, 4> DIRECTIONS{DiagDirection::NE, DiagDirection::SE, DiagDirection::SW, DiagDirection::NW};

/**
 * A terminus station: platforms behind the front tile, a depot behind the platforms,
 * and the line leaving from the exit tile ahead of the front.
 */
struct Terminus {
	TileIndex front = INVALID_TILE; ///< Platform tile next to the line.
	DiagDirection exit = DiagDirection::NE; ///< Direction from the platforms towards the line.
	int length = 2; ///< Platform length in tiles; the train is sized to fit.
	StationID join = NEW_STATION; ///< Existing station the platforms join, such as a port where cargo is transferred.

	TileIndex Exit() const { return Step(this->front, this->exit); }
	TileIndex Platform(int i) const { return Step(this->front, this->exit, -i); }
	TileIndex Depot() const { return Step(this->front, this->exit, -this->length); }
	TileArea Area() const { return TileArea(this->front, this->Platform(this->length - 1)); }

	/**
	 * Every tile the terminus needs, depot first and exit last.
	 * @return Tiles, or an empty list if part of it is off the map.
	 */
	std::vector<TileIndex> Footprint() const
	{
		std::vector<TileIndex> tiles{this->Depot()};
		for (int i = this->length - 1; i >= 0; i--) tiles.push_back(this->Platform(i));
		tiles.push_back(this->Exit());
		if (std::ranges::find(tiles, INVALID_TILE) != tiles.end()) return {};
		return tiles;
	}
};

/**
 * Get the track piece that joins two sides of a tile.
 * @param a One side.
 * @param b The other side; must differ from \a a.
 * @return The track.
 */
Track TrackBetween(DiagDirection a, DiagDirection b)
{
	auto has = [&](DiagDirection x, DiagDirection y) { return (a == x && b == y) || (a == y && b == x); };
	if (has(DiagDirection::NE, DiagDirection::SW)) return Track::X;
	if (has(DiagDirection::SE, DiagDirection::NW)) return Track::Y;
	if (has(DiagDirection::NE, DiagDirection::NW)) return Track::Upper;
	if (has(DiagDirection::SE, DiagDirection::SW)) return Track::Lower;
	if (has(DiagDirection::SW, DiagDirection::NW)) return Track::Left;
	if (has(DiagDirection::NE, DiagDirection::SE)) return Track::Right;
	NOT_REACHED();
}

/**
 * Get the track pieces that touch one side of a tile.
 * @param side Side of the tile.
 * @return The straight track and the two curves on that side.
 */
TrackBits TracksTouching(DiagDirection side)
{
	switch (side) {
		case DiagDirection::NE: return TRACK_BIT_3WAY_NE;
		case DiagDirection::SE: return TRACK_BIT_3WAY_SE;
		case DiagDirection::SW: return TRACK_BIT_3WAY_SW;
		case DiagDirection::NW: return TRACK_BIT_3WAY_NW;
		default: NOT_REACHED();
	}
}

/**
 * Which way a train turns when it changes direction.
 * @param from Direction before.
 * @param to Direction after.
 * @return 0 straight on, 1 or 3 for the two turning senses, 2 for reversing.
 */
uint8_t TurnBetween(DiagDirection from, DiagDirection to)
{
	return static_cast<uint8_t>((to_underlying(to) - to_underlying(from) + 4) % 4);
}

/**
 * Find terminus sites near a point.
 * @param centre Point to search around.
 * @param radius Search radius in tiles.
 * @param length Platform length in tiles.
 * @param accept Extra check on a site, such as cargo acceptance.
 * @param limit Most sites to return.
 * @return Up to \a limit sites, nearest first.
 */
std::vector<Terminus> FindTermini(TileIndex centre, int radius, int length, const std::function<bool(const Terminus &)> &accept, size_t limit = MAX_ENDPOINTS)
{
	std::vector<Terminus> result;
	for (int y = std::max(1, (int)TileY(centre) - radius); y <= std::min((int)Map::SizeY() - 2, (int)TileY(centre) + radius); y++) {
		for (int x = std::max(1, (int)TileX(centre) - radius); x <= std::min((int)Map::SizeX() - 2, (int)TileX(centre) + radius); x++) {
			for (DiagDirection exit : DIRECTIONS) {
				Terminus site{TileXY(x, y), exit, length};
				const auto tiles = site.Footprint();
				if (tiles.empty()) continue;
				const int z = GetTileZ(site.front);
				if (!std::ranges::all_of(tiles, [z](TileIndex t) { return Buildable(t) && GetTileZ(t) == z; })) continue;
				if (accept(site)) result.push_back(site);
			}
		}
	}
	std::ranges::stable_sort(result, {}, [centre](const Terminus &t) { return DistanceManhattan(t.front, centre); });
	if (result.size() > limit) result.resize(limit);
	return result;
}

/** A pathfinder position: on a tile, heading a direction, remembering the previous tile's turn. */
struct RailNode {
	TileIndex tile;
	DiagDirection heading;
	uint8_t turn; ///< Turn made on the previous tile; two equal turns in a row make a forbidden 90 degree bend.

	uint64_t Key() const { return (static_cast<uint64_t>(this->tile.base()) << 8) | (to_underlying(this->heading) << 4) | this->turn; }
};

/**
 * Check whether a line heading one way may cross a tile's road with a level crossing: a straight,
 * flat piece of town or unowned road, without tram track, at right angles to the line.
 * @param tile Tile to cross.
 * @param heading Direction the line is going.
 * @return Whether a level crossing may go there.
 */
static bool CrossableRoad(TileIndex tile, DiagDirection heading)
{
	if (!IsValidTile(tile) || !IsNormalRoadTile(tile) || !IsTileFlat(tile)) return false;
	if (!HasTileRoadType(tile, RoadTramType::Road) || HasTileRoadType(tile, RoadTramType::Tram)) return false;
	const Owner owner = GetRoadOwner(tile, RoadTramType::Road);
	if (owner != OWNER_TOWN && owner != OWNER_NONE) return false;
	/* The road runs along one axis; the line must run along the other. */
	return GetRoadBits(tile, RoadTramType::Road) == (DiagDirToAxis(heading) == Axis::X ? ROAD_Y : ROAD_X);
}

/** A planned line: its tiles, and the road tiles among them that a bridge spans. */
struct RailPath {
	std::vector<TileIndex> tiles; ///< Tiles from the origin's exit to the destination's exit.
	std::set<TileIndex> bridged; ///< Tiles under a bridge; the line's tiles either side of a run of them are its heads.
	std::set<TileIndex> tunnelled; ///< Tiles over a tunnel; the line's tiles either side of a run of them are its portals.

	bool empty() const { return this->tiles.empty(); }
};

/**
 * Find a single-track line between two termini, crossing town roads at right angles where it has to.
 * The line climbs and descends over tiles inclined along it, and curves only on flat tiles.
 * Consecutive tiles never turn the same way, so the line has no 90 degree bends, and it runs
 * straight over a level crossing.
 * @param a Origin terminus.
 * @param b Destination terminus.
 * @param crossings Whether the line may cross roads with level crossings; it may always bridge them.
 * @param[out] crossable If given, set when the search met a road it could have crossed with a level crossing.
 * @return The line, or an empty one if none was found.
 */
size_t g_dbg_calls = 0, g_dbg_fail = 0, g_dbg_states = 0, g_dbg_full = 0;
static RailPath SearchRailPath2(const Terminus &a, const Terminus &b, bool crossings, bool *crossable, size_t &states);
static RailPath SearchRailPath(const Terminus &a, const Terminus &b, bool crossings, bool *crossable = nullptr)
{
	size_t states = 0;
	auto r = SearchRailPath2(a, b, crossings, crossable, states);
	g_dbg_calls++; g_dbg_states += states; if (r.empty()) g_dbg_fail++; if (states >= MAX_RAIL_STATES - 8) g_dbg_full++;
	if (g_dbg_calls % 50 == 0) Debug(Facility::Misc, Severity::Error, "FLDBG rail searches={} failed={} full={} states={}", g_dbg_calls, g_dbg_fail, g_dbg_full, g_dbg_states);
	return r;
}
static RailPath SearchRailPath2(const Terminus &a, const Terminus &b, bool crossings, bool *crossable, size_t &states_out)
{
	std::set<TileIndex> blocked;
	for (const Terminus *t : {&a, &b}) {
		auto tiles = t->Footprint();
		blocked.insert(tiles.begin(), tiles.end() - 1);
	}
	if (blocked.contains(a.Exit()) || blocked.contains(b.Exit())) return {};
	/* The line must meet each platform end at the platform's height. */
	for (const Terminus *t : {&a, &b}) {
		if (!IsValidTile(t->Exit()) || SideHeight(t->Exit(), ReverseDiagDir(t->exit)) != GetTileZ(t->front)) return {};
	}

	const RailNode start{a.Exit(), a.exit, 0};
	std::map<uint64_t, RailNode> previous;
	struct Guard { std::map<uint64_t, RailNode> &p; size_t &o; ~Guard() { o = p.size(); } } guard{previous, states_out};
	std::set<uint64_t> middles; ///< Positions under a bridge or over a tunnel.
	std::set<uint64_t> tunnel_middles; ///< Positions over a tunnel.
	previous.emplace(start.Key(), RailNode{INVALID_TILE, a.exit, 0});
	std::queue<RailNode> queue;
	queue.push(start);
	while (!queue.empty()) {
		const RailNode node = queue.front();
		queue.pop();
		/* Track over a level crossing or a slope, or on a bridge head, must be straight. */
		const RailNode &came_from = previous.at(node.Key());
		const bool off_bridge = came_from.tile != INVALID_TILE && middles.contains(came_from.Key());
		const bool straight_only = IsNormalRoadTile(node.tile) || !IsTileFlat(node.tile) || off_bridge;
		if (node.tile == b.Exit()) {
			const uint8_t turn = TurnBetween(node.heading, ReverseDiagDir(b.exit));
			if (turn != 2 && !(turn != 0 && turn == node.turn) && !(straight_only && turn != 0)) {
				RailPath path;
				for (RailNode n = node; n.tile != INVALID_TILE; n = previous.at(n.Key())) {
					path.tiles.push_back(n.tile);
					if (tunnel_middles.contains(n.Key())) {
						path.tunnelled.insert(n.tile);
					} else if (middles.contains(n.Key())) {
						path.bridged.insert(n.tile);
					}
				}
				std::ranges::reverse(path.tiles);
				/* Positions include the heading, so a line can loop back over itself; keep looking for one that does not. */
				const std::set<TileIndex> distinct(path.tiles.begin(), path.tiles.end());
				if (path.tiles.size() >= 6 && path.tiles.size() <= 160 && distinct.size() == path.tiles.size()) return path;
			}
			continue;
		}
		/* Having climbed onto a slope, the line may instead go through the hill in a tunnel. */
		if (const BridgeSpan tunnel = FindTunnel(node.tile, node.heading); tunnel.land != INVALID_TILE && !off_bridge) {
			const RailNode child{tunnel.land, node.heading, 0};
			bool clear = !blocked.contains(tunnel.land) && tunnel.land != a.Depot() && !previous.contains(child.Key()) && previous.size() + tunnel.length < MAX_RAIL_STATES;
			for (uint k = 1; k <= tunnel.length && clear; k++) clear = !previous.contains(RailNode{Step(node.tile, node.heading, k), node.heading, 0}.Key());
			if (clear) {
				RailNode last = node;
				for (uint k = 1; k <= tunnel.length; k++) {
					const RailNode middle{Step(node.tile, node.heading, k), node.heading, 0};
					middles.insert(middle.Key());
					tunnel_middles.insert(middle.Key());
					previous.emplace(middle.Key(), last);
					last = middle;
				}
				previous.emplace(child.Key(), last);
				queue.push(child);
			}
		}
		for (DiagDirection heading : DIRECTIONS) {
			const uint8_t turn = TurnBetween(node.heading, heading);
			if (turn == 2 || (turn != 0 && turn == node.turn)) continue;
			if (straight_only && turn != 0) continue;
			const TileIndex next = Step(node.tile, heading);
			if (next == INVALID_TILE || blocked.contains(next)) continue;
			/* A bridge over water, roads, railways or a dip, straight from flat open land to flat open land. */
			if (turn == 0 && !off_bridge && !IsNormalRoadTile(node.tile)) {
				const BridgeSpan bridge = FindBridge(node.tile, heading);
				const RailNode child{bridge.land, heading, 0};
				if (bridge.land != INVALID_TILE && !blocked.contains(bridge.land) && bridge.land != a.Depot() && !previous.contains(child.Key()) && previous.size() + bridge.length < MAX_RAIL_STATES) {
					RailNode last = node;
					/* Nothing is built under a bridge, so it must not pass over the stations or the depot. */
					bool clear = true;
					for (uint k = 1; k <= bridge.length && clear; k++) {
						const TileIndex under = Step(node.tile, heading, k);
						clear = !blocked.contains(under) && under != a.Depot() && under != a.Exit() && under != b.Exit() && !previous.contains(RailNode{under, heading, 0}.Key());
					}
					if (clear) {
						for (uint k = 1; k <= bridge.length; k++) {
							const RailNode middle{Step(node.tile, heading, k), heading, 0};
							middles.insert(middle.Key());
							previous.emplace(middle.Key(), last);
							last = middle;
						}
						previous.emplace(child.Key(), last);
						queue.push(child);
					}
				}
			}
			if (!OpenGround(next)) {
				if (!CrossableRoad(next, heading)) continue;
				if (!crossings) {
					if (crossable != nullptr) *crossable = true;
					continue;
				}
			}
			/* The ground must meet at the shared side; this also keeps the line straight along a slope. */
			const auto leaving = SideHeight(node.tile, heading);
			if (!leaving.has_value() || leaving != SideHeight(next, ReverseDiagDir(heading))) continue;
			const RailNode child{next, heading, turn};
			if (previous.size() >= MAX_RAIL_STATES || !previous.emplace(child.Key(), node).second) continue;
			queue.push(child);
		}
	}
	return {};
}

/**
 * Find a line between two termini that bridges roads, and only if there is none, one with level
 * crossings: a fast train is on a crossing before a road vehicle already on it can clear it.
 * @param a Origin terminus.
 * @param b Destination terminus.
 * @param railtype Track to build; no level crossings on track types that forbid them.
 * @return Tiles from a's exit to b's exit, or empty if no line was found.
 */
RailPath FindRailPath(const Terminus &a, const Terminus &b, RailType railtype)
{
	/* Searching again only helps if the first search met a road it could have crossed. */
	bool crossable = false;
	auto path = SearchRailPath(a, b, false, &crossable);
	if (path.empty() && crossable && !RailNoLevelCrossings(railtype)) path = SearchRailPath(a, b, true);
	return path;
}

/**
 * Work out the track piece on every line tile.
 * @param a Origin terminus.
 * @param b Destination terminus.
 * @param path Tiles from a's exit to b's exit.
 * @return One track per path tile.
 */
std::vector<Track> LineTracks(const Terminus &a, const Terminus &b, const std::vector<TileIndex> &path)
{
	std::vector<Track> tracks;
	DiagDirection entered = a.exit;
	for (size_t i = 0; i < path.size(); i++) {
		const DiagDirection leave = i + 1 < path.size() ? RouteDirection(path[i], path[i + 1]) : ReverseDiagDir(b.exit);
		tracks.push_back(TrackBetween(ReverseDiagDir(entered), leave));
		entered = leave;
	}
	return tracks;
}

/**
 * Test or build a terminus station.
 * @param site Terminus.
 * @param railtype Track type.
 * @param flags Command flags.
 * @return Cost or error.
 */
CommandCost BuildTerminusStation(const Terminus &site, RailType railtype, DoCommandFlags flags)
{
	const TileArea area = site.Area();
	return Command<Commands::BuildRailStation>::Do(flags | CONSTRUCT, area.tile, railtype, DiagDirToAxis(site.exit), 1, site.length,
		STAT_CLASS_DFLT, 0, site.join, true);
}

/**
 * Check an executed command and process the track signal updates it queued, as the normal command loop does.
 * Without this, later commands would see stale entries for tiles that have since changed.
 * @param result Outcome of an executed command.
 * @return Whether it failed.
 */
bool ExecFailed(const CommandCost &result)
{
	UpdateSignalsInBuffer();
	return result.Failed();
}

/** What a railway carries and with what. */
struct Consist {
	const Engine *locomotive; ///< Engine at the front.
	const Engine *wagon; ///< Wagon type, repeated to fill the platform.
	CargoType cargo; ///< Cargo the wagons are refitted to.
	bool freight; ///< Full load at the origin, unload at the destination.
	RailType railtype; ///< Track to build.
};

/**
 * Preflight the whole railway, then build it and its train through normal commands.
 * @param a Origin terminus, which gets the depot.
 * @param b Destination terminus.
 * @param line Line tiles, and the roads it bridges.
 * @param consist Train to buy.
 * @param execute Whether to build after a successful preflight; false only checks.
 * @param transfer Whether a freight train transfers and leaves empty at \a b instead of delivering.
 * @param[out] result_built If given, receives what was built on success, so a caller can join onto it or remove it again.
 * @return Whether preflight rejected, execution failed, or the service was built (or would be, when not executing).
 */
ConstructionResult BuildRailway(const Terminus &a, const Terminus &b, const RailPath &line, const Consist &consist, bool execute = true,
	bool transfer = false, BuiltLeg *result_built = nullptr)
{
	const std::vector<TileIndex> &path = line.tiles;
	const std::vector<Track> tracks = LineTracks(a, b, path);
	/* Bridges are built from head to head; their heads and the tiles under them get no track of their own. */
	const auto bridges_on_line = RouteBridges(path, line.bridged);
	const auto tunnels_on_line = RouteBridges(path, line.tunnelled);
	std::vector<bool> bridge_part(path.size(), false);
	for (const auto *spans : {&bridges_on_line, &tunnels_on_line}) {
		for (const RouteBridge &b : *spans) std::fill(bridge_part.begin() + b.head, bridge_part.begin() + b.tail + 1, true);
	}
	auto build_tunnel = [&](const RouteBridge &t, DoCommandFlags flags) {
		return Command<Commands::BuildTunnel>::Do(flags | CONSTRUCT, path[t.head], TransportType::Rail, consist.railtype, INVALID_ROADTYPE);
	};
	auto build_bridge = [&](const RouteBridge &b, DoCommandFlags flags) {
		const BridgeType type = CheapestBridge(static_cast<uint>(b.tail - b.head - 1));
		if (type == MAX_BRIDGES) return CommandCost(CMD_ERROR);
		return Command<Commands::BuildBridge>::Do(flags | CONSTRUCT, path[b.tail], path[b.head], TransportType::Rail, type, consist.railtype, INVALID_ROADTYPE);
	};
	Money cost = 0;
	for (const RouteBridge &b : bridges_on_line) {
		auto result = build_bridge(b, {});
		if (result.Failed()) return ConstructionResult::Rejected;
		cost += result.GetCost();
	}
	for (const RouteBridge &t : tunnels_on_line) {
		auto result = build_tunnel(t, {});
		if (result.Failed()) return ConstructionResult::Rejected;
		cost += result.GetCost();
	}
	for (size_t i = 0; i < path.size(); i++) {
		if (bridge_part[i]) continue;
		auto result = Command<Commands::BuildRail>::Do(CONSTRUCT, path[i], consist.railtype, tracks[i], false);
		if (result.Failed()) return ConstructionResult::Rejected;
		cost += result.GetCost();
	}
	for (const Terminus *site : {&a, &b}) {
		auto result = BuildTerminusStation(*site, consist.railtype, {});
		if (result.Failed()) return ConstructionResult::Rejected;
		cost += result.GetCost();
	}
	auto depot_test = Command<Commands::BuildRailDepot>::Do(CONSTRUCT, a.Depot(), consist.railtype, a.exit);
	if (depot_test.Failed()) return ConstructionResult::Rejected;
	cost += depot_test.GetCost();
	/* Reserve capital for the train and any command-cost changes after construction. */
	const Company *company = Company::Get(_current_company);
	const Money train = consist.locomotive->GetCost() + consist.wagon->GetCost() * MAX_WAGONS;
	if (cost * 2 + train * 2 > company->money) return ConstructionResult::Rejected;
	/* Explicit demolition consumes clearing credits. Keep enough for a complete rollback. */
	if ((company->clear_limit >> 16) < (path.size() + 2 * a.length + 1) * 3 + 8) return ConstructionResult::Rejected;
	if (!execute) return ConstructionResult::Success;

	std::vector<TileIndex> built;
	std::map<TileIndex, Track> crossings; ///< Level crossings over town roads: only the rail is removed.
	std::vector<VehicleID> vehicles;
	auto rollback = [&]() {
		for (VehicleID vehicle : vehicles) {
			if (ExecFailed(Command<Commands::SellVehicle>::Do(EXEC, vehicle, true, false, ClientID::Invalid))) {
				Debug(Facility::Misc, Severity::Error, "FL rollback failed selling vehicle={}; infrastructure retained", vehicle);
				return;
			}
		}
		for (auto it = built.rbegin(); it != built.rend(); ++it) {
			if (auto crossing = crossings.find(*it); crossing != crossings.end()) {
				auto result = Command<Commands::RemoveRail>::Do(EXEC, *it, crossing->second);
				if (ExecFailed(result)) Debug(Facility::Misc, Severity::Error, "FL rollback failed tile={} error={}", *it, result.GetErrorMessage());
				continue;
			}
			auto result = Command<Commands::LandscapeClear>::Do(EXEC, *it);
			if (ExecFailed(result)) Debug(Facility::Misc, Severity::Error, "FL rollback failed tile={} error={}", *it, result.GetErrorMessage());
		}
	};
	auto fail = [&](std::string_view phase) {
		Debug(Facility::Misc, Severity::Warning, "FL construction failed company={} phase={}; clearing new infrastructure", _current_company, phase);
		rollback();
		return ConstructionResult::Failed;
	};
	/* Stations first: they need the towns' permission, which clearing trees for the line can lose. */
	for (const Terminus *site : {&a, &b}) {
		if (ExecFailed(BuildTerminusStation(*site, consist.railtype, EXEC))) return fail("station");
		for (int i = 0; i < site->length; i++) built.push_back(site->Platform(i));
	}
	for (const RouteBridge &b : bridges_on_line) {
		if (ExecFailed(build_bridge(b, EXEC))) return fail("bridge");
		/* Clearing a bridge head removes the whole bridge and leaves what is under it. */
		built.push_back(path[b.head]);
	}
	for (const RouteBridge &t : tunnels_on_line) {
		if (ExecFailed(build_tunnel(t, EXEC))) return fail("tunnel");
		/* Clearing a portal removes the whole tunnel. */
		built.push_back(path[t.head]);
	}
	for (size_t i = 0; i < path.size(); i++) {
		if (bridge_part[i]) continue;
		if (auto result = Command<Commands::BuildRail>::Do(EXEC | CONSTRUCT, path[i], consist.railtype, tracks[i], false); ExecFailed(result)) {
			Debug(Facility::Misc, Severity::Warning, "FL rail failed tile={} slope={} error={}", path[i], GetTileSlope(path[i]), result.GetErrorMessage());
			return fail("rail");
		}
		if (IsLevelCrossingTile(path[i])) crossings.emplace(path[i], tracks[i]);
		built.push_back(path[i]);
	}
	if (ExecFailed(Command<Commands::BuildRailDepot>::Do(EXEC | CONSTRUCT, a.Depot(), consist.railtype, a.exit))) return fail("depot");
	built.push_back(a.Depot());

	auto loco = Command<Commands::BuildVehicle>::Do(EXEC, a.Depot(), consist.locomotive->index, false, INVALID_CARGO, ClientID::Invalid);
	if (ExecFailed(ExtractCommandCost(loco))) return fail("vehicle");
	const VehicleID head = std::get<1>(loco);
	vehicles.push_back(head);
	uint wagons = 0;
	for (; wagons < MAX_WAGONS; wagons++) {
		auto wagon = Command<Commands::BuildVehicle>::Do(EXEC, a.Depot(), consist.wagon->index, false, consist.cargo, ClientID::Invalid);
		if (ExecFailed(ExtractCommandCost(wagon))) return fail("vehicle");
		const VehicleID id = std::get<1>(wagon);
		vehicles.push_back(id);
		if (ExecFailed(Command<Commands::MoveRailVehicle>::Do(EXEC, id, Train::Get(head)->Last()->index, false))) return fail("consist");
		vehicles.pop_back();
		if (Train::Get(head)->gcache.cached_total_length > static_cast<uint>(a.length) * TILE_SIZE) {
			/* One wagon too many for the platform; take it off again. */
			if (ExecFailed(Command<Commands::SellVehicle>::Do(EXEC, id, false, false, ClientID::Invalid))) return fail("consist");
			break;
		}
	}
	if (wagons == 0) return fail("consist");

	for (VehicleOrderID i = 0; i < 2; i++) {
		Order order;
		order.MakeGoToStation(GetStationIndex(i == 0 ? a.front : b.front));
		/* Non-stop, as a player would: lines can share track and cross other lines, and stopping at those stations adds implicit orders. */
		order.SetNonStopType(OrderNonStopFlag::NonStop);
		if (consist.freight) order.SetLoadType(i == 0 ? OrderLoadType::FullLoadAny : OrderLoadType::NoLoad);
		/* Transfer and leave empty: the cargo waits at the station for the next leg's vehicles. */
		if (transfer && i == 1) order.SetUnloadType(OrderUnloadType::Transfer);
		if (ExecFailed(Command<Commands::InsertOrder>::Do(EXEC, head, i, order))) return fail("orders");
	}
	if (ExecFailed(Command<Commands::StartStopVehicle>::Do(EXEC, head, true))) return fail("start");

	uint64_t signature = 14695981039346656037ULL;
	for (TileIndex tile : path) signature = (signature ^ tile.base()) * 1099511628211ULL;
	Debug(Facility::Misc, Severity::Notice, "FL rail company={} kind={} cargo={} track={} vehicle={} wagons={} from={} to={} depot={} tiles={} balance={} signature={} locomotive={} wagon={} crossings={} bridges={} tunnels={} slopes={}",
		_current_company, consist.freight ? "freight" : "town", CargoSpec::Get(consist.cargo)->label.AsString(), GetRailTypeInfo(consist.railtype)->label.AsString(), head, wagons,
		a.front, b.front, a.Depot(), path.size(), Company::Get(_current_company)->money, signature, TechId(consist.locomotive), TechId(consist.wagon), crossings.size(), bridges_on_line.size(), tunnels_on_line.size(),
		std::ranges::count_if(path, [](TileIndex t) { return !IsTileFlat(t); }));
	if (result_built != nullptr) {
		result_built->from = GetStationIndex(a.front);
		result_built->to = GetStationIndex(b.front);
		result_built->from_tile = a.front;
		result_built->to_tile = b.front;
		/* Undo clears whole tiles, which would take a town's road with a level crossing; leave those. */
		std::erase_if(built, [&](TileIndex tile) { return crossings.contains(tile); });
		result_built->tiles = std::move(built);
		result_built->vehicles = {head};
	}
	return ConstructionResult::Success;
}

/**
 * Try every combination of the given sites until one railway is built.
 * @param from Origin sites.
 * @param to Destination sites.
 * @param consist Train to buy.
 * @return Outcome of the last attempt that got past preflight, or Rejected.
 */
ConstructionResult TrySites(const std::vector<Terminus> &from, const std::vector<Terminus> &to, const Consist &consist)
{
	for (const Terminus &a : from) {
		for (const Terminus &b : to) {
			const auto path = FindRailPath(a, b, consist.railtype);
			if (path.empty()) continue;
			const auto result = BuildRailway(a, b, path, consist);
			if (result != ConstructionResult::Rejected) return result;
		}
	}
	return ConstructionResult::Rejected;
}

/**
 * Pick a track type, locomotive and wagon for a job. Track types are taken in the game's list
 * order (plain rail first) and only those with a usable locomotive and wagon count. The market
 * tier limits how many of those may be considered; among them, the fastest locomotive wins and
 * ties go to the earlier track type. So small markets get plain rail, big ones better track.
 * Only the service's family is used, on track that is untagged or tagged with that family.
 * Wagons come from the same family where it has some, otherwise from conventional wagons.
 * @param library Loaded transport technology, including the track types.
 * @param spec Family and preferences of the service.
 * @param role Job the train does.
 * @param cargo Cargo the wagons carry.
 * @param freight Whether this is a freight service.
 * @param track_rank How many usable track types may be considered.
 * @return Consist, with null engines if nothing suitable is available.
 */
Consist ChooseConsist(const TechLibrary &library, const ServiceSpec &spec, TechRole role, CargoType cargo, bool freight, uint8_t track_rank)
{
	Consist best{nullptr, nullptr, cargo, freight, INVALID_RAILTYPE};
	uint best_speed = 0;
	uint8_t usable = 0;
	for (const InfraRecord &track : library.Infrastructure()) {
		if (usable >= track_rank) break;
		if (track.kind != InfraKind::RailType) continue;
		const RailType railtype = static_cast<RailType>(track.index);
		if (!spec.AllowsInfrastructure(track.tech) || !HasRailTypeAvail(_current_company, railtype)) continue;
		TechQuery query;
		query.mode = TechMode::Rail;
		query.role = role;
		query.company = _current_company;
		query.railtype = railtype;
		query.family = spec.family;
		query.provides = track.provides;
		query.early_adopters = spec.early_adopters;
		query.part = TechPart::Locomotive;
		const TechRecord *locomotive = library.FindBest(query);
		query.part = TechPart::Wagon;
		query.cargo = cargo;
		const TechRecord *wagon = library.FindBest(query);
		if (wagon == nullptr && spec.family != CONVENTIONAL) {
			query.family = std::string{CONVENTIONAL};
			wagon = library.FindBest(query);
		}
		if (locomotive == nullptr || wagon == nullptr) continue;
		usable++;
		if (locomotive->speed <= best_speed) continue;
		best = {Engine::Get(locomotive->engine), Engine::Get(wagon->engine), cargo, freight, railtype};
		best_speed = locomotive->speed;
	}
	return best;
}

/**
 * Get the area a station's catchment must reach to serve an industry.
 * @param site Terminus.
 * @return Station area grown by the rail catchment radius.
 */
TileArea Catchment(const Terminus &site)
{
	TileArea area = site.Area();
	area.Expand(_settings_game.station.modified_catchment ? CA_TRAIN : CA_UNMODIFIED);
	return area;
}
} // namespace

/**
 * Build a railway carrying passengers or mail between two unused towns.
 * @param library Loaded transport technology.
 * @param spec Family, limits and preferences of the service.
 * @param cargo Passengers or mail.
 * @param[in,out] used_towns Towns already served for this cargo; the chosen towns are added on success.
 * @return Outcome.
 */
ConstructionResult BuildTownRail(const TechLibrary &library, const ServiceSpec &spec, CargoType cargo, std::set<TownID> &used_towns)
{
	if (!IsValidCargoType(cargo)) return ConstructionResult::Rejected;
	const TechRole role = TownRole(library, cargo, TechRole::PassengerIntercity);

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
			/* Villages do not get a railway. */
			const MarketTier tier = TierForTowns(a, b);
			if (distance >= 24 && distance <= 120 && GetTierLimits(tier).rail_passenger && tier >= spec.min_tier) pairs.push_back({distance, a, b});
		}
	}
	std::ranges::stable_sort(pairs, {}, &Pair::distance);

	const int radius = _settings_game.station.modified_catchment ? CA_TRAIN : CA_UNMODIFIED;
	std::map<std::pair<TownID, int>, std::vector<Terminus>> sites;
	auto town_sites = [&](const Town *town, int length) -> const std::vector<Terminus> & {
		auto it = sites.find({town->index, length});
		if (it != sites.end()) return it->second;
		return sites.emplace(std::pair{town->index, length}, FindTermini(town->xy, 14, length, [&](const Terminus &site) {
			if (ClosestTownFromTile(site.front, UINT_MAX)->index != town->index) return false;
			const TileArea area = site.Area();
			return GetAcceptanceAroundTiles(area.tile, area.w, area.h, radius).first[cargo] >= 8 && GetProductionAroundTiles(area.tile, area.w, area.h, radius)[cargo] > 0;
		})).first->second;
	};
	for (size_t i = 0; i < std::min(MAX_RAIL_PAIRS, pairs.size()); i++) {
		const Pair &pair = pairs[i];
		const MarketTier tier = TierForTowns(pair.a, pair.b);
		const TierLimits &limits = GetTierLimits(tier);
		const Consist consist = ChooseConsist(library, spec, role, cargo, false, limits.track_rank);
		if (consist.locomotive == nullptr) continue;
		const auto result = TrySites(town_sites(pair.a, limits.platform_length), town_sites(pair.b, limits.platform_length), consist);
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
 * Find terminus sites for one end of a freight leg.
 * @param place Industry or transfer station; railways do not deliver into towns.
 * @param cargo Cargo carried.
 * @param length Platform length.
 * @param destination Whether the cargo is delivered here, so the station must accept it.
 * @return Sites, nearest first.
 */
static std::vector<Terminus> FreightTermini(const Place &place, CargoType cargo, int length, bool destination)
{
	const int radius = _settings_game.station.modified_catchment ? CA_TRAIN : CA_UNMODIFIED;
	if (place.town != nullptr) return {};
	if (place.IsHub()) {
		auto sites = FindTermini(place.hub, place.radius, length, [](const Terminus &) { return true; }, SIZE_MAX);
		for (Terminus &site : sites) site.join = place.join;
		KeepUsable(sites, place, [](const Terminus &site) { return site.front; }, MAX_ENDPOINTS);
		return sites;
	}
	const Industry *industry = place.industry;
	const int search = std::max<int>(industry->location.w, industry->location.h) / 2 + radius + length;
	return FindTermini(Centre(industry), search, length, [&](const Terminus &site) {
		if (!Catchment(site).Intersects(industry->location)) return false;
		if (!destination) return true;
		const TileArea area = site.Area();
		return GetAcceptanceAroundTiles(area.tile, area.w, area.h, radius).first[cargo] >= 8;
	});
}

/**
 * Build, or check, a freight railway leg: a train that full-loads at one terminus and delivers or
 * transfers at the other. The track type and platform length follow the market size.
 * @copydoc LegBuild
 */
ConstructionResult RailLeg(const TechLibrary &library, const ServiceSpec &spec, const Place &from, const Place &to, const LegJob &job, bool execute, BuiltLeg &built)
{
	const TierLimits &limits = GetTierLimits(job.tier);
	const Consist consist = ChooseConsist(library, spec, FreightRole(library, job.cargo), job.cargo, true, limits.track_rank);
	if (consist.locomotive == nullptr) return ConstructionResult::Rejected;
	const auto origins = FreightTermini(from, job.cargo, limits.platform_length, false);
	if (origins.empty()) return ConstructionResult::Rejected;
	const auto destinations = FreightTermini(to, job.cargo, limits.platform_length, true);
	if (destinations.empty()) return ConstructionResult::Rejected;
	built.sited = true;
	for (const Terminus &a : origins) {
		for (const Terminus &b : destinations) {
			const auto path = FindRailPath(a, b, consist.railtype);
			if (path.empty()) continue;
			const auto result = BuildRailway(a, b, path, consist, execute, job.transfer, &built);
			if (result != ConstructionResult::Rejected) return result;
		}
	}
	return ConstructionResult::Rejected;
}

/**
 * Get the sides of a tile that the company's track leaves through.
 * @param tile Tile to inspect.
 * @return Sides with a track touching them.
 */
static std::vector<DiagDirection> RailSides(TileIndex tile)
{
	if (IsTileType(tile, TileType::Railway)) {
		if (IsRailDepotTile(tile)) return {GetRailDepotDirection(tile)};
		const TrackBits bits = GetTrackBits(tile);
		std::vector<DiagDirection> sides;
		for (DiagDirection side : DIRECTIONS) {
			if (bits.Any(TracksTouching(side))) sides.push_back(side);
		}
		return sides;
	}
	if (IsTileType(tile, TileType::Station) && HasStationRail(tile)) {
		if (GetRailStationAxis(tile) == Axis::X) return {DiagDirection::NE, DiagDirection::SW};
		return {DiagDirection::SE, DiagDirection::NW};
	}
	/* A rail bridge head or tunnel portal leads off on its open side; the walk crosses to the other end. */
	if (IsTileType(tile, TileType::TunnelBridge) && GetTunnelBridgeTransportType(tile) == TransportType::Rail) return {ReverseDiagDir(GetTunnelBridgeDirection(tile))};
	/* Towns may later build roads across the line; the crossing's owner is the track's owner. */
	if (IsLevelCrossingTile(tile)) {
		if (GetCrossingRailAxis(tile) == Axis::X) return {DiagDirection::NE, DiagDirection::SW};
		return {DiagDirection::SE, DiagDirection::NW};
	}
	return {};
}

/**
 * Follow a train's owned track from where it is now.
 * @param train Train to inspect.
 * @param[out] depot If given, receives the first of the owner's rail depots found on that track, or INVALID_TILE.
 * @return Stations the train can reach over its owner's connected track.
 */
std::set<StationID> RailStationsReachable(const Vehicle *train, TileIndex *depot)
{
	std::set<StationID> reached;
	std::set<TileIndex> seen{train->tile};
	std::queue<TileIndex> queue;
	queue.push(train->tile);
	while (!queue.empty() && seen.size() <= MAX_SEARCH) {
		const TileIndex tile = queue.front();
		queue.pop();
		if (IsTileType(tile, TileType::Station) && HasStationRail(tile)) reached.insert(GetStationIndex(tile));
		if (depot != nullptr && *depot == INVALID_TILE && IsRailDepotTile(tile) && GetTileOwner(tile) == train->owner) *depot = tile;
		if (IsTileType(tile, TileType::TunnelBridge) && GetTunnelBridgeTransportType(tile) == TransportType::Rail) {
			const TileIndex other = GetOtherTunnelBridgeEnd(tile);
			if (!seen.contains(other)) { seen.insert(other); queue.push(other); }
		}
		for (DiagDirection side : RailSides(tile)) {
			const TileIndex next = Step(tile, side);
			if (next == INVALID_TILE || seen.contains(next)) continue;
			const auto next_sides = RailSides(next);
			if (std::ranges::find(next_sides, ReverseDiagDir(side)) == next_sides.end() || GetTileOwner(next) != train->owner) continue;
			seen.insert(next);
			queue.push(next);
		}
	}
	return reached;
}

} // namespace FutureLogistics
