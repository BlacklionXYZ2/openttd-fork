/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file water_planner.h Day-zero shipping: ferries, mail boats and freight ships. */
#ifndef FUTURE_LOGISTICS_WATER_PLANNER_H
#define FUTURE_LOGISTICS_WATER_PLANNER_H

#include "planner_common.h"
#include "../cargo_type.h"
#include "../station_type.h"
#include "../town_type.h"
#include "../vehicle_type.h"

class TechLibrary;

namespace FutureLogistics {

ConstructionResult BuildTownFerry(const TechLibrary &library, const ServiceSpec &spec, CargoType cargo, std::set<TownID> &used_towns);
std::set<StationID> DocksReachable(const Vehicle *ship);

} // namespace FutureLogistics

#endif /* FUTURE_LOGISTICS_WATER_PLANNER_H */
