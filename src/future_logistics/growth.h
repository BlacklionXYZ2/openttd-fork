/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file growth.h Building services after day zero: the same recipes and planners, asked for by a company's AI. */
#ifndef FUTURE_LOGISTICS_GROWTH_H
#define FUTURE_LOGISTICS_GROWTH_H

#include "catalogue.h"
#include "planner_common.h"
#include "../engine_type.h"
#include "../vehicle_type.h"

namespace FutureLogistics {

ConstructionResult BuildService(const Recipe &recipe, const Profile &profile, std::span<const Recipe> plan, const TechLibrary &library, Reservations &reserved);

bool CanGrow();
std::vector<std::string> PlanKeys(uint profile_id);
ConstructionResult GrowService(uint profile_id, std::string_view recipe_key);
EngineID ReplacementFor(VehicleID vehicle);
TileIndex RouteDepot(VehicleID vehicle);
Reservations WorldReservations(CompanyID company);

} // namespace FutureLogistics

#endif /* FUTURE_LOGISTICS_GROWTH_H */
