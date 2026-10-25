/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file planner_common.h Helpers shared by the Future Logistics route planners. */
#ifndef FUTURE_LOGISTICS_PLANNER_COMMON_H
#define FUTURE_LOGISTICS_PLANNER_COMMON_H

#include <optional>

#include "../bridge_type.h"
#include "../cargo_type.h"
#include "../command_type.h"
#include "../direction_type.h"
#include "../map_func.h"
#include "../slope_func.h"
#include "../industry_type.h"
#include "../station_type.h"
#include "../tile_map.h"
#include "../town_type.h"
#include "../vehicle_type.h"
#include "scale.h"

class TechLibrary;
struct Industry;
struct Town;

namespace FutureLogistics {

constexpr DoCommandFlags EXEC{DoCommandFlag::Execute}; ///< Execute after command validation.
constexpr DoCommandFlags CONSTRUCT{DoCommandFlag::Auto, DoCommandFlag::NoWater}; ///< Normal construction trait flags.
constexpr size_t MAX_SEARCH = 8192; ///< Maximum visited tiles per town pair.
constexpr size_t MAX_PAIRS = 128; ///< Maximum town pairs considered per company.
constexpr uint32_t MIN_ROLLBACK_CLEARANCE = 16; ///< Fewest tiles of clearing any service needs in reserve to be removed again (a dock pair).

/** Markets already taken by earlier rivals, so they do not compete for the same customers. */
struct Reservations {
	std::set<TownID> towns; ///< Towns served by passenger services.
	std::set<TownID> mail_towns; ///< Towns served by mail services.
	std::set<IndustryID> industries; ///< Industries served by freight services.
	std::set<IndustryID> supplied; ///< Industries that receive freight, so secondary ones will produce.
};

/** Another way a rival carries freight, which a freight route may hand its cargo over to. */
struct Partner {
	std::string mechanic; ///< Registered mechanic, such as "rail_line".
	std::string family{"conventional"}; ///< Tech family it uses.
	MarketTier min_tier = MarketTier::Village; ///< Smallest market it may serve.
	bool early_adopters = true; ///< Whether fl_ai/early_adopter vehicles may be used.
};

/** What a planner should build with: the recipe's family and limits, and the rival's preferences. */
struct ServiceSpec {
	std::string family{"conventional"}; ///< Tech family whose vehicles may be used.
	MarketTier min_tier = MarketTier::Village; ///< Smallest market the service may serve.
	bool early_adopters = true; ///< Whether fl_ai/early_adopter vehicles may be used.
	std::vector<std::string> industry_markets; ///< Preferred fl_market groups for freight, in order.
	std::vector<Partner> partners; ///< The rival's other freight mechanics, for routes that transfer cargo between them.

	/**
	 * Check whether a track type or airport may be used: untagged ones always, tagged ones only by their own family.
	 * @param tech The infrastructure's fl_tech family.
	 * @return Whether this service may build on it.
	 */
	bool AllowsInfrastructure(std::string_view tech) const { return tech == "conventional" || tech == this->family; }
};

/** Outcome controlling whether another candidate may safely be tried. */
enum class ConstructionResult {
	Rejected, ///< Preflight rejected the layout without construction.
	Failed, ///< Execution failed and cleanup was attempted; stop this company.
	Success, ///< An operational route and vehicle were constructed.
};

/**
 * One end of a freight leg: an industry, a town that accepts the cargo, or a transfer station.
 * At a transfer station the cargo is left by one leg's vehicles ("transfer and leave empty") and
 * picked up by the next leg's, as in the base game; the second leg's station joins the first's.
 */
struct Place {
	const Industry *industry = nullptr; ///< Industry the cargo comes from or goes to.
	const Town *town = nullptr; ///< Town the cargo goes to; only legs that can reach town centres serve towns.
	TileIndex hub = INVALID_TILE; ///< For a transfer station: a tile it should be near.
	int radius = 0; ///< For a transfer station: how far from \a hub it may be.
	StationID join = NEW_STATION; ///< For a transfer station: the station it joins, once that exists.
	std::function<bool(TileIndex)> usable; ///< For a transfer station: an extra check on a site, such as whether the next leg can start there.

	/** @return The place of an industry. */
	static Place Of(const Industry *industry) { Place place; place.industry = industry; return place; }
	/** @return The place of a town. */
	static Place Of(const Town *town) { Place place; place.town = town; return place; }

	/** @return Whether this is a transfer station rather than an industry or town. */
	bool IsHub() const { return this->industry == nullptr && this->town == nullptr; }
	TileIndex Centre() const;
};

/** What a freight leg carries. */
struct LegJob {
	CargoType cargo; ///< Cargo carried.
	MarketTier tier; ///< Size of the market, which sets the fleet and the class of infrastructure.
	bool transfer = false; ///< Transfer and leave empty at the end, for another leg to take on; otherwise deliver there.
};

/** What a freight leg built, so a route can be joined onto it or removed again. */
struct BuiltLeg {
	bool sited = false; ///< Set when a vehicle and sites at both ends were found, so the attempt counts as a real try.
	StationID from = StationID::Invalid(); ///< Station at the start.
	StationID to = StationID::Invalid(); ///< Station at the end.
	TileIndex from_tile = INVALID_TILE; ///< A tile of the start station.
	TileIndex to_tile = INVALID_TILE; ///< A tile of the end station.
	std::vector<TileIndex> tiles; ///< Infrastructure, in build order.
	std::vector<VehicleID> vehicles; ///< Vehicles, each sold with its whole consist.
};

/**
 * Build, or only check, one freight leg for the current company: vehicles that full-load at \a from and
 * deliver or transfer at \a to. Each mechanic that carries freight provides one.
 * @param library Loaded transport technology.
 * @param spec Family, limits and preferences of the service.
 * @param from Where the cargo is loaded.
 * @param to Where it is delivered or transferred.
 * @param job Cargo, market size and what happens at the end.
 * @param execute Whether to build; false only checks that the leg could be built.
 * @param[out] built What was found and, when executing, built.
 * @return Outcome.
 */
using LegBuild = ConstructionResult (*)(const TechLibrary &library, const ServiceSpec &spec, const Place &from, const Place &to, const LegJob &job, bool execute, BuiltLeg &built);

constexpr size_t MAX_HUB_CHECKS = 6; ///< Transfer station sites checked with Place::usable per leg.

constexpr uint MAX_BRIDGE_SPAN = 6; ///< Most tiles a planned bridge spans between its heads.

/** A bridge or tunnel a route may take, from the tile it starts on to the tile it ends on. */
struct BridgeSpan {
	TileIndex land = INVALID_TILE; ///< Far end, or INVALID_TILE if there is none.
	uint length = 0; ///< Tiles between the ends.
};

/**
 * Find the shortest bridge from a head on flat open land, straight ahead over water, roads,
 * railways or lower ground, to flat open land at the same height.
 * @param head Near bridge head.
 * @param dir Direction of the bridge.
 * @return The bridge, or one with an invalid landing.
 */
BridgeSpan FindBridge(TileIndex head, DiagDirection dir);

constexpr uint MAX_TUNNEL_SPAN = 16; ///< Most tiles a planned tunnel runs under between its portals.

/**
 * Find a tunnel from a portal on open land inclined up the way the route is going, through the hill
 * to the first tile at the portal's height, which must slope the opposite way.
 * @param portal Near portal, where the route has climbed onto the slope.
 * @param dir Direction of the tunnel.
 * @return The tunnel's far portal and length, or one with an invalid landing.
 */
BridgeSpan FindTunnel(TileIndex portal, DiagDirection dir);

/** Where a bridge or tunnel sits on a planned route: the indices of its two ends. */
struct RouteBridge {
	size_t head; ///< Index of the near head.
	size_t tail; ///< Index of the far head.
};

/**
 * List the bridges on a planned route.
 * @param path Route tiles.
 * @param bridged Tiles under a bridge.
 * @return Each run of bridged tiles with the heads either side of it.
 */
inline std::vector<RouteBridge> RouteBridges(const std::vector<TileIndex> &path, const std::set<TileIndex> &bridged)
{
	std::vector<RouteBridge> result;
	for (size_t i = 1; i + 1 < path.size(); i++) {
		if (!bridged.contains(path[i]) || bridged.contains(path[i - 1])) continue;
		size_t j = i;
		while (j < path.size() && bridged.contains(path[j])) j++;
		if (j < path.size()) result.push_back({i - 1, j});
	}
	return result;
}

/**
 * Find the cheapest bridge type available for a span.
 * @param length Tiles between the bridge heads.
 * @return Bridge type, or MAX_BRIDGES if none is available yet.
 */
BridgeType CheapestBridge(uint length);

/**
 * Narrow a list of candidate sites, nearest first, to those a place's extra check allows.
 * @param sites Candidates, nearest first; narrowed in place.
 * @param place End of the leg.
 * @param tile_of Gets a tile of a site.
 * @param limit Most sites to keep.
 */
template <class Site, class TileOf>
void KeepUsable(std::vector<Site> &sites, const Place &place, TileOf tile_of, size_t limit)
{
	if (!place.usable) {
		if (sites.size() > limit) sites.resize(limit);
		return;
	}
	std::vector<Site> kept;
	for (size_t i = 0; i < sites.size() && i < MAX_HUB_CHECKS && kept.size() < limit; i++) {
		if (place.usable(tile_of(sites[i]))) kept.push_back(sites[i]);
	}
	sites = std::move(kept);
}

TileIndex Centre(const Industry *industry);
void Undo(const BuiltLeg &built);

/**
 * Exclude houses, shared roads, water, bridges and slopes.
 * @param tile Candidate construction tile.
 * @return Whether the tile is eligible for command preflight.
 */
inline bool Buildable(TileIndex tile)
{
	return IsValidTile(tile) && (IsTileType(tile, TileType::Clear) || IsTileType(tile, TileType::Trees)) && IsTileFlat(tile);
}

/**
 * Get the height of the ground at one side of a tile, where track along the side's axis would meet it.
 * Track and road go on flat tiles, and straight up or down a tile inclined along their direction, with no foundation.
 * @param tile Tile to look at.
 * @param side Side of the tile.
 * @return Height at that side, or nothing if nothing can reach that side without a foundation.
 */
inline std::optional<int> SideHeight(TileIndex tile, DiagDirection side)
{
	const auto [slope, z] = GetTileSlopeZ(tile);
	if (slope == SLOPE_FLAT) return z;
	if (!IsInclinedSlope(slope)) return std::nullopt;
	const DiagDirection up = GetInclinedSlopeDirection(slope);
	if (side == up) return z + 1;
	if (side == ReverseDiagDir(up)) return z;
	return std::nullopt;
}

/**
 * Check whether open ground can take track or road without a foundation: clear land or trees, flat or inclined.
 * @param tile Tile to check.
 * @return Whether track or road may go there.
 */
inline bool OpenGround(TileIndex tile)
{
	if (!IsValidTile(tile) || !(IsTileType(tile, TileType::Clear) || IsTileType(tile, TileType::Trees))) return false;
	const Slope slope = GetTileSlope(tile);
	return slope == SLOPE_FLAT || IsInclinedSlope(slope);
}

/**
 * Get the direction between adjacent route tiles.
 * @param from Origin tile.
 * @param to Adjacent destination tile.
 * @return Direction from origin to destination.
 */
inline DiagDirection RouteDirection(TileIndex from, TileIndex to)
{
	if (TileX(to) > TileX(from)) return DiagDirection::SW;
	if (TileX(to) < TileX(from)) return DiagDirection::NE;
	if (TileY(to) > TileY(from)) return DiagDirection::SE;
	return DiagDirection::NW;
}

/**
 * Enumerate adjacent tiles in deterministic order, excluding map borders.
 * @param tile Origin tile.
 * @return Adjacent interior tiles.
 */
inline std::vector<TileIndex> Neighbours(TileIndex tile)
{
	std::vector<TileIndex> result;
	const int x = TileX(tile), y = TileY(tile);
	/* Fixed tie-breaking order; never use InteractiveRandom or wall time. */
	for (const auto &[dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
		if (x + dx > 0 && y + dy > 0 && x + dx < (int)Map::SizeX() - 1 && y + dy < (int)Map::SizeY() - 1) {
			result.push_back(TileXY(x + dx, y + dy));
		}
	}
	return result;
}

/**
 * Step a number of tiles in a direction, staying off the map border.
 * @param tile Origin tile.
 * @param direction Direction to step in.
 * @param count Number of tiles; negative steps backwards.
 * @return Destination, or INVALID_TILE if it would leave the map interior.
 */
inline TileIndex Step(TileIndex tile, DiagDirection direction, int count = 1)
{
	const TileIndexDiffC diff = TileIndexDiffCByDiagDir(direction);
	const int x = (int)TileX(tile) + diff.x * count, y = (int)TileY(tile) + diff.y * count;
	if (x <= 0 || y <= 0 || x >= (int)Map::SizeX() - 1 || y >= (int)Map::SizeY() - 1) return INVALID_TILE;
	return TileXY(x, y);
}

} // namespace FutureLogistics

#endif /* FUTURE_LOGISTICS_PLANNER_COMMON_H */
