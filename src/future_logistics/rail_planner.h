/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file rail_planner.h Day-zero passenger and freight railways. */
#ifndef FUTURE_LOGISTICS_RAIL_PLANNER_H
#define FUTURE_LOGISTICS_RAIL_PLANNER_H

#include "planner_common.h"
#include "../industry_type.h"
#include "../station_type.h"
#include "../town_type.h"
#include "../vehicle_type.h"

class TechLibrary;

namespace FutureLogistics {

ConstructionResult BuildPassengerRail(const TechLibrary &library, std::set<TownID> &used_towns);
ConstructionResult BuildFreightRail(const TechLibrary &library, std::set<IndustryID> &used_industries);
std::set<StationID> RailStationsReachable(const Vehicle *train);

} // namespace FutureLogistics

#endif /* FUTURE_LOGISTICS_RAIL_PLANNER_H */
