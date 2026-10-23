/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file scale.cpp Market tiers and the limits that keep the day-zero headstart in proportion to the world. */
#include "../stdafx.h"
#include "scale.h"
#include "../industry.h"
#include "../town.h"

#include "../safeguards.h"

namespace FutureLogistics {

/**
 * The tier table. All headstart scaling lives here, so balancing only has to edit these rows.
 * Town tiers use the smaller town of a pair; freight uses the source industry's production rate.
 */
static constexpr std::array<TierLimits, 4> TIERS{{
	/* min_population, min_production, rail_passenger, air_passenger, max_airport_class, track_rank, platform_length, fleet */
	{0, 0, false, false, 0, 1, 2, 1}, // Village: buses only, one vehicle per service.
	{500, 8, true, true, 0, 1, 2, 2}, // Town: short trains on the first usable track type, small airports.
	{1500, 16, true, true, 1, 2, 3, 3}, // City: longer trains, better track, large airports.
	{4000, 32, true, true, 2, UINT8_MAX, 4, 4}, // Metropolis: any track, hub airports.
}};
static constexpr std::array<std::string_view, 4> TIER_NAMES{"village", "town", "city", "metropolis"};

/**
 * Get what a tier may build.
 * @param tier Market tier.
 * @return Its limits.
 */
const TierLimits &GetTierLimits(MarketTier tier)
{
	return TIERS[to_underlying(tier)];
}

/**
 * Get a tier's log name.
 * @param tier Market tier.
 * @return Name such as "city".
 */
std::string_view TierName(MarketTier tier)
{
	return TIER_NAMES[to_underlying(tier)];
}

/**
 * Classify a population.
 * @param population Number of inhabitants.
 * @return The largest tier whose minimum it reaches.
 */
MarketTier TierForPopulation(uint32_t population)
{
	MarketTier tier = MarketTier::Village;
	for (uint8_t i = 0; i < TIERS.size(); i++) {
		if (population >= TIERS[i].min_population) tier = static_cast<MarketTier>(i);
	}
	return tier;
}

/**
 * Classify a town-to-town service by its smaller town.
 * @param a One town.
 * @param b The other town.
 * @return Market tier.
 */
MarketTier TierForTowns(const Town *a, const Town *b)
{
	return TierForPopulation(std::min(a->cache.population, b->cache.population));
}

/**
 * Get how much of a cargo an industry produces, in the units of the fixed production rate.
 * Industries whose NewGRF produces through a callback, such as FIRS, have no fixed rate;
 * their production is taken from last month's history, which the game also fills in for
 * new industries when the world is generated.
 * @param industry Industry.
 * @param cargo Cargo it produces.
 * @return Production per 256 ticks; 0 if it produces none, such as a secondary industry nobody supplies.
 */
uint ProductionRate(const Industry *industry, CargoType cargo)
{
	auto it = industry->GetCargoProduced(cargo);
	if (it == industry->produced.end()) return 0;
	/* A month has about eight production rounds. */
	return std::max<uint>(it->rate, it->history[LAST_MONTH].production / 8);
}

/**
 * Classify a freight service by how much its source industry produces.
 * @param industry Source industry.
 * @param cargo Cargo carried.
 * @return Market tier.
 */
MarketTier TierForIndustry(const Industry *industry, CargoType cargo)
{
	const uint rate = ProductionRate(industry, cargo);
	MarketTier tier = MarketTier::Village;
	for (uint8_t i = 0; i < TIERS.size(); i++) {
		if (rate >= TIERS[i].min_production) tier = static_cast<MarketTier>(i);
	}
	return tier;
}

/**
 * Work out each rival's one-off grant from the size of the world.
 * @param world_population Total population of all towns.
 * @param rivals Number of rivals sharing the market.
 * @param per_head Capital per inhabitant across all rivals.
 * @param cap Largest grant per rival.
 * @return Grant per rival, never more than \a cap.
 */
Money RivalGrant(uint64_t world_population, uint rivals, Money per_head, Money cap)
{
	if (rivals == 0) return 0;
	Money share = per_head * static_cast<int64_t>(world_population);
	share /= static_cast<int>(rivals);
	return std::min(share, cap);
}

/**
 * Work out how many times each rival works through its profile's services, so larger worlds
 * get more routes. Every round still reserves its own towns and industries.
 * @param world_population Total population of all towns.
 * @return One round, plus one per POPULATION_PER_ROUND inhabitants, up to MAX_ROUNDS.
 */
uint ServiceRounds(uint64_t world_population)
{
	static constexpr uint64_t POPULATION_PER_ROUND = 50000;
	static constexpr uint MAX_ROUNDS = 3;
	return static_cast<uint>(std::min<uint64_t>(MAX_ROUNDS, 1 + world_population / POPULATION_PER_ROUND));
}

} // namespace FutureLogistics
