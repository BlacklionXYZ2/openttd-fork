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
	/* min_population, min_production, rail_passenger, air_passenger, max_airport_class, track_rank, platform_length */
	{0, 0, false, false, 0, 1, 2}, // Village: buses only.
	{500, 8, true, true, 0, 1, 2}, // Town: short trains on the cheapest track, small airports.
	{1500, 16, true, true, 1, 2, 3}, // City: longer trains, better track, large airports.
	{4000, 32, true, true, 2, UINT8_MAX, 4}, // Metropolis: any track, hub airports.
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
 * Classify a freight service by how much its source industry produces.
 * @param industry Source industry.
 * @param cargo Cargo carried.
 * @return Market tier.
 */
MarketTier TierForIndustry(const Industry *industry, CargoType cargo)
{
	auto it = industry->GetCargoProduced(cargo);
	const uint8_t rate = it == industry->produced.end() ? 0 : it->rate;
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

} // namespace FutureLogistics
