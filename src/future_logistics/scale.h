/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file scale.h How big a headstart the world justifies: market tiers, what each tier may build, and rival capital. */
#ifndef FUTURE_LOGISTICS_SCALE_H
#define FUTURE_LOGISTICS_SCALE_H

#include "../cargo_type.h"
#include "../economy_type.h"
#include "../industry_type.h"
#include "../town_type.h"

namespace FutureLogistics {

/** Size of the market a service is built for. Ordered, so a larger tier allows at least as much. */
enum class MarketTier : uint8_t {
	Village,
	Town,
	City,
	Metropolis,
};

/** What a starter service may use in a market of a given tier. */
struct TierLimits {
	uint32_t min_population; ///< Smallest town population in this tier.
	uint8_t min_production; ///< Smallest industry production rate in this tier.
	bool rail_passenger; ///< Passenger railways allowed.
	bool air_passenger; ///< Airlines allowed.
	uint8_t max_airport_class; ///< Largest airport class: 0 small, 1 large, 2 hub.
	uint8_t track_rank; ///< How many usable track types may be considered, in the game's list order (plain rail first).
	int platform_length; ///< Station platform length in tiles, which also limits train length.
	uint fleet; ///< Road vehicles or aircraft per service.
};

const TierLimits &GetTierLimits(MarketTier tier);
std::string_view TierName(MarketTier tier);
MarketTier TierForPopulation(uint32_t population);
MarketTier TierForTowns(const Town *a, const Town *b);
uint ProductionRate(const Industry *industry, CargoType cargo);
MarketTier TierForIndustry(const Industry *industry, CargoType cargo);
Money RivalGrant(uint64_t world_population, uint rivals, Money per_head, Money cap);
uint ServiceRounds(uint64_t world_population);

} // namespace FutureLogistics

#endif /* FUTURE_LOGISTICS_SCALE_H */
