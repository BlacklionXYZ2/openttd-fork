/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file script_futurelogistics.hpp Future Logistics: the day-zero planners and Tech Library, for a company's AI. */

#ifndef SCRIPT_FUTURELOGISTICS_HPP
#define SCRIPT_FUTURELOGISTICS_HPP

#include "script_object.hpp"
#include "script_engine.hpp"
#include "script_vehicle.hpp"

/**
 * Future Logistics (this fork only). Gives an AI the same choices the day-zero bootstrap makes: a rival
 * profile's recipes and adoption rules, the planners that build a service of a recipe, and the Tech
 * Library's choice of vehicle for a job. New technology from NewGRFs reaches the AI through these with
 * no change to the AI. See docs/future_logistics/tech_format.md.
 * @note Building services is only possible in single-player games; IsAvailable says so.
 * @api ai
 */
class ScriptFutureLogistics : public ScriptObject {
public:
	/**
	 * Check whether services can be built with BuildService in this game.
	 * @return False in multiplayer games, where the planners cannot run.
	 */
	static bool IsAvailable();

	/**
	 * Get the number of recipes a rival profile may use today.
	 * @param profile Profile number from 1, as in CorporateAI's "profile" setting.
	 * @return Number of recipes; 0 for an unknown profile.
	 */
	static SQInteger GetPlanSize(SQInteger profile);

	/**
	 * Get one of a rival profile's recipes for today. Recipes come in the order the profile adopts them,
	 * and new ones appear when a NewGRF tech family becomes available and the profile's rules allow it.
	 * @param profile Profile number from 1.
	 * @param index Recipe index from 0 to GetPlanSize(profile) - 1.
	 * @return Recipe key such as "conventional/rail_line/industry", or null if out of range.
	 */
	static std::optional<std::string> GetPlanRecipe(SQInteger profile, SQInteger index);

	/**
	 * Build one new service of a recipe for this company, now, with the day-zero planners: the route,
	 * stations, depot and vehicles. Markets the company already serves, and industries other companies
	 * serve, are left alone. In test mode nothing is built and false is returned.
	 * @param profile Profile number from 1.
	 * @param recipe A recipe key from GetPlanRecipe.
	 * @pre IsAvailable().
	 * @game @pre ScriptCompanyMode::IsValid().
	 * @return Whether a service was built.
	 */
	static bool BuildService(SQInteger profile, const std::string &recipe);

	/**
	 * Get what a vehicle should be replaced with: the vehicle the Tech Library would pick today for the
	 * same job, cargo, track and tech family. For a train this is its locomotive. Use with
	 * ScriptGroup::SetAutoReplace.
	 * @param vehicle_id A vehicle of this company.
	 * @pre ScriptVehicle::IsPrimaryVehicle(vehicle_id).
	 * @return The engine to use; the vehicle's own engine when nothing better is available.
	 */
	static EngineID GetReplacement(VehicleID vehicle_id);

	/**
	 * Get a depot where a copy of a vehicle can start and still reach the vehicle's route: a depot on the
	 * same track or road, found by following it from the vehicle; the hangar of one of its airports; or
	 * the nearest of the company's ship depots. Use with ScriptVehicle::CloneVehicle.
	 * @param vehicle_id A vehicle of this company.
	 * @pre ScriptVehicle::IsPrimaryVehicle(vehicle_id).
	 * @return Depot tile, or an invalid tile if none was found.
	 */
	static TileIndex GetRouteDepot(VehicleID vehicle_id);
};

#endif /* SCRIPT_FUTURELOGISTICS_HPP */
