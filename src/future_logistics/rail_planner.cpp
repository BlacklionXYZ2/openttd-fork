/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file rail_planner.cpp Day-zero railways: two terminus stations, a single line, a depot and one train. */
#include "../stdafx.h"
#include "rail_planner.h"
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
#include "../settings_type.h"
#include "../signal_func.h"
#include "../station_cmd.h"
#include "../station_func.h"
#include "../station_map.h"
#include "../tilearea_type.h"
#include "../town.h"
#include "../track_func.h"
#include "../train.h"
#include "../train_cmd.h"
#include "../vehicle_cmd.h"
#include <functional>
#include <map>
#include <queue>

#include "../safeguards.h"

namespace FutureLogistics {
namespace {
constexpr int PLATFORM_LENGTH = 3; ///< Platform length in tiles; the train is sized to fit.
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

	TileIndex Exit() const { return Step(this->front, this->exit); }
	TileIndex Platform(int i) const { return Step(this->front, this->exit, -i); }
	TileIndex Depot() const { return Step(this->front, this->exit, -PLATFORM_LENGTH); }
	TileArea Area() const { return TileArea(this->front, this->Platform(PLATFORM_LENGTH - 1)); }

	/**
	 * Every tile the terminus needs, depot first and exit last.
	 * @return Tiles, or an empty list if part of it is off the map.
	 */
	std::vector<TileIndex> Footprint() const
	{
		std::vector<TileIndex> tiles{this->Depot()};
		for (int i = PLATFORM_LENGTH - 1; i >= 0; i--) tiles.push_back(this->Platform(i));
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
 * @param accept Extra check on a site, such as cargo acceptance.
 * @return Up to MAX_ENDPOINTS sites, nearest first.
 */
std::vector<Terminus> FindTermini(TileIndex centre, int radius, const std::function<bool(const Terminus &)> &accept)
{
	std::vector<Terminus> result;
	for (int y = std::max(1, (int)TileY(centre) - radius); y <= std::min((int)Map::SizeY() - 2, (int)TileY(centre) + radius); y++) {
		for (int x = std::max(1, (int)TileX(centre) - radius); x <= std::min((int)Map::SizeX() - 2, (int)TileX(centre) + radius); x++) {
			for (DiagDirection exit : DIRECTIONS) {
				Terminus site{TileXY(x, y), exit};
				const auto tiles = site.Footprint();
				if (tiles.empty()) continue;
				const int z = GetTileZ(site.front);
				if (!std::ranges::all_of(tiles, [z](TileIndex t) { return Buildable(t) && GetTileZ(t) == z; })) continue;
				if (accept(site)) result.push_back(site);
			}
		}
	}
	std::ranges::stable_sort(result, {}, [centre](const Terminus &t) { return DistanceManhattan(t.front, centre); });
	if (result.size() > MAX_ENDPOINTS) result.resize(MAX_ENDPOINTS);
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
 * Find a single-track line between two termini over flat land at one height.
 * Consecutive tiles never turn the same way, so the line has no 90 degree bends.
 * @param a Origin terminus.
 * @param b Destination terminus.
 * @return Tiles from a's exit to b's exit, or empty if no line was found.
 */
std::vector<TileIndex> FindRailPath(const Terminus &a, const Terminus &b)
{
	std::set<TileIndex> blocked;
	for (const Terminus *t : {&a, &b}) {
		auto tiles = t->Footprint();
		blocked.insert(tiles.begin(), tiles.end() - 1);
	}
	const int z = GetTileZ(a.front);
	if (GetTileZ(b.front) != z || blocked.contains(a.Exit()) || blocked.contains(b.Exit())) return {};

	const RailNode start{a.Exit(), a.exit, 0};
	std::map<uint64_t, RailNode> previous;
	previous.emplace(start.Key(), RailNode{INVALID_TILE, a.exit, 0});
	std::queue<RailNode> queue;
	queue.push(start);
	while (!queue.empty()) {
		const RailNode node = queue.front();
		queue.pop();
		if (node.tile == b.Exit()) {
			const uint8_t turn = TurnBetween(node.heading, ReverseDiagDir(b.exit));
			if (turn != 2 && !(turn != 0 && turn == node.turn)) {
				std::vector<TileIndex> path;
				for (RailNode n = node; n.tile != INVALID_TILE; n = previous.at(n.Key())) path.push_back(n.tile);
				std::ranges::reverse(path);
				if (path.size() >= 6 && path.size() <= 160) return path;
			}
			continue;
		}
		for (DiagDirection heading : DIRECTIONS) {
			const uint8_t turn = TurnBetween(node.heading, heading);
			if (turn == 2 || (turn != 0 && turn == node.turn)) continue;
			const TileIndex next = Step(node.tile, heading);
			if (next == INVALID_TILE || blocked.contains(next) || !Buildable(next) || GetTileZ(next) != z) continue;
			const RailNode child{next, heading, turn};
			if (previous.size() >= MAX_RAIL_STATES || !previous.emplace(child.Key(), node).second) continue;
			queue.push(child);
		}
	}
	return {};
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
 * @param flags Command flags.
 * @return Cost or error.
 */
CommandCost BuildTerminusStation(const Terminus &site, DoCommandFlags flags)
{
	const TileArea area = site.Area();
	return Command<Commands::BuildRailStation>::Do(flags | CONSTRUCT, area.tile, RAILTYPE_RAIL, DiagDirToAxis(site.exit), 1, PLATFORM_LENGTH,
		STAT_CLASS_DFLT, 0, NEW_STATION, true);
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
};

/**
 * Preflight the whole railway, then build it and its train through normal commands.
 * @param a Origin terminus, which gets the depot.
 * @param b Destination terminus.
 * @param path Line tiles.
 * @param consist Train to buy.
 * @return Whether preflight rejected, execution failed, or the service was built.
 */
ConstructionResult BuildRailway(const Terminus &a, const Terminus &b, const std::vector<TileIndex> &path, const Consist &consist)
{
	const std::vector<Track> tracks = LineTracks(a, b, path);
	Money cost = 0;
	for (size_t i = 0; i < path.size(); i++) {
		auto result = Command<Commands::BuildRail>::Do(CONSTRUCT, path[i], RAILTYPE_RAIL, tracks[i], false);
		if (result.Failed()) return ConstructionResult::Rejected;
		cost += result.GetCost();
	}
	for (const Terminus *site : {&a, &b}) {
		auto result = BuildTerminusStation(*site, {});
		if (result.Failed()) return ConstructionResult::Rejected;
		cost += result.GetCost();
	}
	auto depot_test = Command<Commands::BuildRailDepot>::Do(CONSTRUCT, a.Depot(), RAILTYPE_RAIL, a.exit);
	if (depot_test.Failed()) return ConstructionResult::Rejected;
	cost += depot_test.GetCost();
	/* Reserve capital for the train and any command-cost changes after construction. */
	const Company *company = Company::Get(_current_company);
	const Money train = consist.locomotive->GetCost() + consist.wagon->GetCost() * MAX_WAGONS;
	if (cost * 2 + train * 2 > company->money) return ConstructionResult::Rejected;
	/* Explicit demolition consumes clearing credits. Keep enough for a complete rollback. */
	if ((company->clear_limit >> 16) < (path.size() + 2 * PLATFORM_LENGTH + 1) * 3 + 8) return ConstructionResult::Rejected;

	std::vector<TileIndex> built;
	std::vector<VehicleID> vehicles;
	auto rollback = [&]() {
		for (VehicleID vehicle : vehicles) {
			if (ExecFailed(Command<Commands::SellVehicle>::Do(EXEC, vehicle, true, false, ClientID::Invalid))) {
				Debug(Facility::Misc, Severity::Error, "FL rollback failed selling vehicle={}; infrastructure retained", vehicle);
				return;
			}
		}
		for (auto it = built.rbegin(); it != built.rend(); ++it) {
			auto result = Command<Commands::LandscapeClear>::Do(EXEC, *it);
			if (ExecFailed(result)) Debug(Facility::Misc, Severity::Error, "FL rollback failed tile={} error={}", *it, result.GetErrorMessage());
		}
	};
	auto fail = [&](std::string_view phase) {
		Debug(Facility::Misc, Severity::Warning, "FL construction failed company={} phase={}; clearing new infrastructure", _current_company, phase);
		rollback();
		return ConstructionResult::Failed;
	};
	for (size_t i = 0; i < path.size(); i++) {
		if (ExecFailed(Command<Commands::BuildRail>::Do(EXEC | CONSTRUCT, path[i], RAILTYPE_RAIL, tracks[i], false))) return fail("rail");
		built.push_back(path[i]);
	}
	for (const Terminus *site : {&a, &b}) {
		if (ExecFailed(BuildTerminusStation(*site, EXEC))) return fail("station");
		for (int i = 0; i < PLATFORM_LENGTH; i++) built.push_back(site->Platform(i));
	}
	if (ExecFailed(Command<Commands::BuildRailDepot>::Do(EXEC | CONSTRUCT, a.Depot(), RAILTYPE_RAIL, a.exit))) return fail("depot");
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
		if (Train::Get(head)->gcache.cached_total_length > PLATFORM_LENGTH * TILE_SIZE) {
			/* One wagon too many for the platform; take it off again. */
			if (ExecFailed(Command<Commands::SellVehicle>::Do(EXEC, id, false, false, ClientID::Invalid))) return fail("consist");
			break;
		}
	}
	if (wagons == 0) return fail("consist");

	for (VehicleOrderID i = 0; i < 2; i++) {
		Order order;
		order.MakeGoToStation(GetStationIndex(i == 0 ? a.front : b.front));
		if (consist.freight) order.SetLoadType(i == 0 ? OrderLoadType::FullLoadAny : OrderLoadType::NoLoad);
		if (ExecFailed(Command<Commands::InsertOrder>::Do(EXEC, head, i, order))) return fail("orders");
	}
	if (ExecFailed(Command<Commands::StartStopVehicle>::Do(EXEC, head, true))) return fail("start");

	uint64_t signature = 14695981039346656037ULL;
	for (TileIndex tile : path) signature = (signature ^ tile.base()) * 1099511628211ULL;
	Debug(Facility::Misc, Severity::Notice, "FL rail company={} kind={} cargo={} vehicle={} wagons={} from={} to={} depot={} tiles={} balance={} signature={}",
		_current_company, consist.freight ? "freight" : "passenger", CargoSpec::Get(consist.cargo)->label.AsString(), head, wagons,
		a.front, b.front, a.Depot(), path.size(), Company::Get(_current_company)->money, signature);
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
			const auto path = FindRailPath(a, b);
			if (path.empty()) continue;
			const auto result = BuildRailway(a, b, path, consist);
			if (result != ConstructionResult::Rejected) return result;
		}
	}
	return ConstructionResult::Rejected;
}

/**
 * Pick a locomotive and wagon for a job on plain rail.
 * @param library Loaded transport technology.
 * @param role Job the train does.
 * @param cargo Cargo the wagons carry.
 * @param freight Whether this is a freight service.
 * @return Consist, with null engines if nothing suitable is available.
 */
Consist ChooseConsist(const TechLibrary &library, TechRole role, CargoType cargo, bool freight)
{
	TechQuery query{.mode = TechMode::Rail, .role = role, .cargo = INVALID_CARGO, .company = _current_company, .railtype = RAILTYPE_RAIL};
	query.part = TechPart::Locomotive;
	const TechRecord *locomotive = library.FindBest(query);
	query.part = TechPart::Wagon;
	query.cargo = cargo;
	const TechRecord *wagon = library.FindBest(query);
	if (locomotive == nullptr || wagon == nullptr) return {nullptr, nullptr, cargo, freight};
	return {Engine::Get(locomotive->engine), Engine::Get(wagon->engine), cargo, freight};
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
 * Build a passenger railway between two unused towns.
 * @param library Loaded transport technology.
 * @param[in,out] used_towns Towns already served by rivals; the chosen towns are added on success.
 * @return Outcome.
 */
ConstructionResult BuildPassengerRail(const TechLibrary &library, std::set<TownID> &used_towns)
{
	const CargoType cargo = GetCargoTypeByLabel(CT_PASSENGERS);
	if (!IsValidCargoType(cargo) || !HasRailTypeAvail(_current_company, RAILTYPE_RAIL)) return ConstructionResult::Rejected;
	const Consist consist = ChooseConsist(library, TechRole::PassengerIntercity, cargo, false);
	if (consist.locomotive == nullptr) return ConstructionResult::Rejected;

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
			if (distance >= 24 && distance <= 120) pairs.push_back({distance, a, b});
		}
	}
	std::ranges::stable_sort(pairs, {}, &Pair::distance);

	const int radius = _settings_game.station.modified_catchment ? CA_TRAIN : CA_UNMODIFIED;
	std::map<TownID, std::vector<Terminus>> sites;
	auto town_sites = [&](const Town *town) -> const std::vector<Terminus> & {
		auto it = sites.find(town->index);
		if (it != sites.end()) return it->second;
		return sites.emplace(town->index, FindTermini(town->xy, 14, [&](const Terminus &site) {
			if (ClosestTownFromTile(site.front, UINT_MAX)->index != town->index) return false;
			const TileArea area = site.Area();
			return GetAcceptanceAroundTiles(area.tile, area.w, area.h, radius).first[cargo] >= 8 && GetProductionAroundTiles(area.tile, area.w, area.h, radius)[cargo] > 0;
		})).first->second;
	};
	for (size_t i = 0; i < std::min(MAX_RAIL_PAIRS, pairs.size()); i++) {
		const Pair &pair = pairs[i];
		const auto result = TrySites(town_sites(pair.a), town_sites(pair.b), consist);
		if (result == ConstructionResult::Rejected) continue;
		if (result == ConstructionResult::Success) {
			used_towns.insert(pair.a->index);
			used_towns.insert(pair.b->index);
			Debug(Facility::Misc, Severity::Notice, "FL towns company={} a={} b={}", _current_company, pair.a->index, pair.b->index);
		}
		return result;
	}
	return ConstructionResult::Rejected;
}

/**
 * Build a freight railway from an unused producing industry to one that accepts its cargo.
 * @param library Loaded transport technology.
 * @param[in,out] used_industries Industries already served by rivals; the chosen pair is added on success.
 * @return Outcome.
 */
ConstructionResult BuildFreightRail(const TechLibrary &library, std::set<IndustryID> &used_industries)
{
	if (!HasRailTypeAvail(_current_company, RAILTYPE_RAIL)) return ConstructionResult::Rejected;
	struct Pair { uint distance; const Industry *source; const Industry *destination; CargoType cargo; };
	std::vector<const Industry *> industries;
	for (const Industry *industry : Industry::Iterate()) {
		if (industries.size() == 256) break;
		if (!used_industries.contains(industry->index)) industries.push_back(industry);
	}
	std::vector<Pair> pairs;
	for (const Industry *source : industries) {
		for (const auto &produced : source->produced) {
			if (!IsValidCargoType(produced.cargo) || CargoSpec::Get(produced.cargo)->classes.Any({CargoClass::Passengers, CargoClass::Mail})) continue;
			for (const Industry *destination : industries) {
				if (destination == source || !destination->IsCargoAccepted(produced.cargo)) continue;
				const uint distance = DistanceManhattan(source->location.tile, destination->location.tile);
				if (distance >= 20 && distance <= 100) pairs.push_back({distance, source, destination, produced.cargo});
			}
		}
	}
	std::ranges::stable_sort(pairs, {}, &Pair::distance);

	auto centre = [](const Industry *industry) { return TileAddXY(industry->location.tile, industry->location.w / 2, industry->location.h / 2); };
	const int radius = _settings_game.station.modified_catchment ? CA_TRAIN : CA_UNMODIFIED;
	for (size_t i = 0; i < std::min(MAX_RAIL_PAIRS, pairs.size()); i++) {
		const Pair &pair = pairs[i];
		const CargoClasses classes = CargoSpec::Get(pair.cargo)->classes;
		const TechRole role = classes.Any({CargoClass::Bulk, CargoClass::Liquid}) ? TechRole::FreightBulk : TechRole::FreightGeneral;
		const Consist consist = ChooseConsist(library, role, pair.cargo, true);
		if (consist.locomotive == nullptr) continue;
		const int search = std::max<int>(pair.source->location.w, pair.source->location.h) / 2 + radius + PLATFORM_LENGTH;
		const auto from = FindTermini(centre(pair.source), search, [&](const Terminus &site) {
			return Catchment(site).Intersects(pair.source->location);
		});
		const int dest_search = std::max<int>(pair.destination->location.w, pair.destination->location.h) / 2 + radius + PLATFORM_LENGTH;
		const auto to = FindTermini(centre(pair.destination), dest_search, [&](const Terminus &site) {
			if (!Catchment(site).Intersects(pair.destination->location)) return false;
			const TileArea area = site.Area();
			return GetAcceptanceAroundTiles(area.tile, area.w, area.h, radius).first[pair.cargo] >= 8;
		});
		const auto result = TrySites(from, to, consist);
		if (result == ConstructionResult::Rejected) continue;
		if (result == ConstructionResult::Success) {
			used_industries.insert(pair.source->index);
			used_industries.insert(pair.destination->index);
			Debug(Facility::Misc, Severity::Notice, "FL industries company={} source={} destination={} cargo={}", _current_company,
				pair.source->index, pair.destination->index, CargoSpec::Get(pair.cargo)->label.AsString());
		}
		return result;
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
	return {};
}

/**
 * Follow a train's owned track from where it is now.
 * @param train Train to inspect.
 * @return Stations the train can reach over its owner's connected track.
 */
std::set<StationID> RailStationsReachable(const Vehicle *train)
{
	std::set<StationID> reached;
	std::set<TileIndex> seen{train->tile};
	std::queue<TileIndex> queue;
	queue.push(train->tile);
	while (!queue.empty() && seen.size() <= MAX_SEARCH) {
		const TileIndex tile = queue.front();
		queue.pop();
		if (IsTileType(tile, TileType::Station) && HasStationRail(tile)) reached.insert(GetStationIndex(tile));
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
