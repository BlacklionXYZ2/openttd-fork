/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file script_futurelogistics.cpp Implementation of ScriptFutureLogistics. */

#include "../../stdafx.h"
#include "script_futurelogistics.hpp"
#include "script_companymode.hpp"
#include "script_error.hpp"
#include "../../future_logistics/growth.h"
#include "../../vehicle_base.h"

#include "../../safeguards.h"

/* static */ bool ScriptFutureLogistics::IsAvailable()
{
	return FutureLogistics::CanGrow();
}

/* static */ SQInteger ScriptFutureLogistics::GetPlanSize(SQInteger profile)
{
	if (profile < 1) return 0;
	return FutureLogistics::PlanKeys(static_cast<uint>(profile)).size();
}

/* static */ std::optional<std::string> ScriptFutureLogistics::GetPlanRecipe(SQInteger profile, SQInteger index)
{
	if (profile < 1 || index < 0) return std::nullopt;
	const auto keys = FutureLogistics::PlanKeys(static_cast<uint>(profile));
	if (static_cast<size_t>(index) >= keys.size()) return std::nullopt;
	return keys[index];
}

/* static */ bool ScriptFutureLogistics::BuildService(SQInteger profile, const std::string &recipe)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, IsAvailable());
	EnforcePrecondition(false, profile >= 1);
	/* Test mode only estimates; the planners cannot build without executing. */
	if (ScriptObject::GetDoCommandMode() != nullptr && !ScriptObject::GetDoCommandMode()()) return false;
	return FutureLogistics::GrowService(static_cast<uint>(profile), recipe) == FutureLogistics::ConstructionResult::Success;
}

/* static */ EngineID ScriptFutureLogistics::GetReplacement(VehicleID vehicle_id)
{
	EnforcePrecondition(EngineID::Invalid(), ScriptVehicle::IsPrimaryVehicle(vehicle_id));
	return FutureLogistics::ReplacementFor(vehicle_id);
}

/* static */ TileIndex ScriptFutureLogistics::GetRouteDepot(VehicleID vehicle_id)
{
	EnforcePrecondition(INVALID_TILE, ScriptVehicle::IsPrimaryVehicle(vehicle_id));
	return FutureLogistics::RouteDepot(vehicle_id);
}
