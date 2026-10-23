/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file planner_common.h Helpers shared by the Future Logistics route planners. */
#ifndef FUTURE_LOGISTICS_PLANNER_COMMON_H
#define FUTURE_LOGISTICS_PLANNER_COMMON_H

#include "../command_type.h"
#include "../direction_type.h"
#include "../map_func.h"
#include "../industry_type.h"
#include "../tile_map.h"
#include "../town_type.h"

namespace FutureLogistics {

constexpr DoCommandFlags EXEC{DoCommandFlag::Execute}; ///< Execute after command validation.
constexpr DoCommandFlags CONSTRUCT{DoCommandFlag::Auto, DoCommandFlag::NoWater}; ///< Normal construction trait flags.
constexpr size_t MAX_SEARCH = 8192; ///< Maximum visited tiles per town pair.
constexpr size_t MAX_PAIRS = 128; ///< Maximum town pairs considered per company.

/** Markets already taken by earlier rivals, so they do not compete for the same customers. */
struct Reservations {
	std::set<TownID> towns; ///< Towns served by passenger services.
	std::set<IndustryID> industries; ///< Industries served by freight services.
	std::set<IndustryID> supplied; ///< Industries that receive freight, so secondary ones will produce.
};

/** Outcome controlling whether another candidate may safely be tried. */
enum class ConstructionResult {
	Rejected, ///< Preflight rejected the layout without construction.
	Failed, ///< Execution failed and cleanup was attempted; stop this company.
	Success, ///< An operational route and vehicle were constructed.
};

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
