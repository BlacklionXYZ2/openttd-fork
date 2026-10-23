/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file freight_planner.h Day-zero freight routes: direct, or handed over between two ways of carrying freight. */
#ifndef FUTURE_LOGISTICS_FREIGHT_PLANNER_H
#define FUTURE_LOGISTICS_FREIGHT_PLANNER_H

#include "planner_common.h"

namespace FutureLogistics {

ConstructionResult BuildFreight(const TechLibrary &library, const ServiceSpec &spec, std::string_view mechanic, Reservations &reserved);

/* The freight leg of each mechanic that carries freight; see mechanics.cpp. */
ConstructionResult RailLeg(const TechLibrary &library, const ServiceSpec &spec, const Place &from, const Place &to, const LegJob &job, bool execute, BuiltLeg &built);
ConstructionResult RoadLeg(const TechLibrary &library, const ServiceSpec &spec, const Place &from, const Place &to, const LegJob &job, bool execute, BuiltLeg &built);
ConstructionResult AirLeg(const TechLibrary &library, const ServiceSpec &spec, const Place &from, const Place &to, const LegJob &job, bool execute, BuiltLeg &built);
ConstructionResult ShipLeg(const TechLibrary &library, const ServiceSpec &spec, const Place &from, const Place &to, const LegJob &job, bool execute, BuiltLeg &built);

} // namespace FutureLogistics

#endif /* FUTURE_LOGISTICS_FREIGHT_PLANNER_H */
