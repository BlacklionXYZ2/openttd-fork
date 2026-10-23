/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file mechanics.h The registry that links mechanic names used by NewGRFs to the code that builds them. */
#ifndef FUTURE_LOGISTICS_MECHANICS_H
#define FUTURE_LOGISTICS_MECHANICS_H

#include "planner_common.h"
#include "tech_library.h"

namespace FutureLogistics {

/** What a service connects. */
enum class Market : uint8_t {
	TownPair, ///< Passengers between two towns.
	Mail, ///< Mail between two towns.
	Industry, ///< Freight from a producing industry to one (or, by road, a town) that accepts it.
	Feeder, ///< Passengers from a nearby town into one of the company's railway stations.
	End,
};

std::string_view MarketName(Market market);
std::optional<Market> ParseMarket(std::string_view name);

/**
 * Build one service of a mechanic for the current company.
 * @param library Loaded transport technology.
 * @param spec Family, limits and preferences of the service.
 * @param market What to connect; always one the mechanic lists.
 * @param[in,out] reserved Markets already taken; the service adds what it uses.
 * @return Outcome.
 */
using MechanicBuild = ConstructionResult (*)(const TechLibrary &library, const ServiceSpec &spec, Market market, Reservations &reserved);

/** A way of building a service, as named in fl_family/.../mechanic/<name>. */
struct MechanicInfo {
	std::string_view name; ///< Name used in NewGRF declarations.
	TechMode mode; ///< Vehicles it uses; a family of this mode defaults to the first mechanic listed for it.
	std::vector<Market> markets; ///< Markets it can serve.
	MechanicBuild build; ///< The code that builds it.
	LegBuild leg = nullptr; ///< Builds one freight leg, so freight routes can use it alone or transfer to and from it; nullptr if it carries no freight.
	uint min_distance = 0; ///< Shortest freight route it runs alone, in Manhattan tiles.
	uint max_distance = 0; ///< Longest freight route it runs alone, in Manhattan tiles.
};

std::span<const MechanicInfo> GetMechanics();
const MechanicInfo *FindMechanic(std::string_view name);
const MechanicInfo *DefaultMechanic(TechMode mode);
bool IsSupported(std::string_view mechanic, Market market);

} // namespace FutureLogistics

#endif /* FUTURE_LOGISTICS_MECHANICS_H */
