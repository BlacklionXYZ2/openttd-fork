/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/**
 * @file growth.cpp Building services after day zero.
 *
 * A company's AI asks for the same recipes, adoption rules and planners that built its day-zero network,
 * through the script API (ScriptFutureLogistics), so NewGRF tech reaches running rivals with no AI changes.
 * The planners run their commands directly, as on day zero, so this is only offered in single-player games.
 */
#include "../stdafx.h"
#include "growth.h"
#include "mechanics.h"
#include "rail_planner.h"
#include "road_planner.h"
#include "../aircraft.h"
#include "../cargotype.h"
#include "../company_base.h"
#include "../company_func.h"
#include "../debug.h"
#include "../engine_base.h"
#include "../industry.h"
#include "../network/network.h"
#include "../order_base.h"
#include "../roadveh.h"
#include "../signal_func.h"
#include "../station_base.h"
#include "../timer/timer_game_calendar.h"
#include "../timer/timer_game_economy.h"
#include "../town.h"
#include "../train.h"
#include "../water_map.h"

#include "../safeguards.h"

namespace FutureLogistics {

/**
 * Build one service for the current company with the mechanic its recipe names.
 * @param recipe Family, mechanic and market.
 * @param profile The rival, for its preferences.
 * @param plan All of the rival's recipes; its other freight recipes are the ways a freight route may hand cargo over to.
 * @param library Loaded transport technology.
 * @param[in,out] reserved Markets already taken.
 * @return Outcome; Rejected for a mechanic or market this build does not support.
 */
ConstructionResult BuildService(const Recipe &recipe, const Profile &profile, std::span<const Recipe> plan, const TechLibrary &library, Reservations &reserved)
{
	if (!IsSupported(recipe.mechanic, recipe.market)) return ConstructionResult::Rejected;
	/* Every planner rejects a service it could not demolish again; without even the smallest such amount
	 * of clearing, reject now rather than after searching the whole map for routes. */
	if ((Company::Get(_current_company)->clear_limit >> 16) < MIN_ROLLBACK_CLEARANCE) return ConstructionResult::Rejected;
	ServiceSpec spec{recipe.family, recipe.min_tier, recipe.early_adopters, profile.industry_markets, {}};
	if (recipe.market == Market::Industry) {
		for (const Recipe &other : plan) {
			if (other.market != Market::Industry || other.mechanic == recipe.mechanic) continue;
			if (std::ranges::find(spec.partners, other.mechanic, &Partner::mechanic) != spec.partners.end()) continue;
			spec.partners.push_back({other.mechanic, other.family, other.min_tier, other.early_adopters});
		}
	}
	return FindMechanic(recipe.mechanic)->build(library, spec, recipe.market, reserved);
}

/**
 * Check whether services may be built after day zero. The planners execute commands directly, which
 * would desynchronise a multiplayer game, so this is single-player only, like the day-zero bootstrap.
 * @return Whether GrowService may be used.
 */
bool CanGrow()
{
	return !_networking;
}

namespace {
/**
 * Find a profile by its number.
 * @param catalogue Catalogue built from the loaded NewGRFs.
 * @param profile_id Number from 1, as stored in CorporateAI's "profile" setting.
 * @return The profile, or nullptr.
 */
const Profile *FindProfile(const Catalogue &catalogue, uint profile_id)
{
	for (const Profile &profile : catalogue.Profiles()) {
		if (profile.id == profile_id) return &profile;
	}
	return nullptr;
}
} // namespace

/**
 * Get a rival's recipes for today, in the order it adopts them.
 * @param profile_id Profile number.
 * @return Recipe keys such as "conventional/rail_line/industry"; empty for an unknown profile.
 */
std::vector<std::string> PlanKeys(uint profile_id)
{
	const TechLibrary library = TechLibrary::Build();
	const Catalogue catalogue = Catalogue::Build(library);
	const Profile *profile = FindProfile(catalogue, profile_id);
	if (profile == nullptr) return {};
	std::vector<std::string> keys;
	for (const Recipe &recipe : catalogue.Plan(*profile, library, TimerGameCalendar::date)) keys.push_back(recipe.Key());
	return keys;
}

/**
 * Work out which markets are already taken in a running game, so a new service neither duplicates the
 * company's own nor crowds another company's industries. Towns count as served when the company has a
 * station there; industries when any company's station serves them. Industries that accepted cargo in
 * the last two months are supplied, so a secondary one can be a source.
 * @param company Company that will build.
 * @return Reservations for the planners.
 */
Reservations WorldReservations(CompanyID company)
{
	Reservations reserved;
	for (const Station *station : Station::Iterate()) {
		if (station->owner != company || station->town == nullptr) continue;
		reserved.towns.insert(station->town->index);
		reserved.mail_towns.insert(station->town->index);
	}
	for (const Industry *industry : Industry::Iterate()) {
		if (!industry->stations_near.empty()) reserved.industries.insert(industry->index);
		for (const auto &accepted : industry->accepted) {
			if (accepted.last_accepted != TimerGameEconomy::Date{} && TimerGameEconomy::date - accepted.last_accepted < 60) {
				reserved.supplied.insert(industry->index);
				break;
			}
		}
	}
	return reserved;
}

/**
 * Build one more service of a recipe for the current company, now.
 * @param profile_id The company's profile number.
 * @param recipe_key A key from PlanKeys, such as "conventional/rail_line/industry".
 * @return Outcome; Rejected for an unknown profile or a recipe the profile may not use today.
 */
ConstructionResult GrowService(uint profile_id, std::string_view recipe_key)
{
	if (!CanGrow()) return ConstructionResult::Rejected;
	const TechLibrary library = TechLibrary::Build();
	const Catalogue catalogue = Catalogue::Build(library);
	const Profile *profile = FindProfile(catalogue, profile_id);
	if (profile == nullptr) return ConstructionResult::Rejected;
	const std::vector<Recipe> plan = catalogue.Plan(*profile, library, TimerGameCalendar::date);
	auto recipe = std::ranges::find(plan, recipe_key, &Recipe::Key);
	if (recipe == plan.end()) return ConstructionResult::Rejected;
	Reservations reserved = WorldReservations(_current_company);
	const ConstructionResult result = BuildService(*recipe, *profile, plan, library, reserved);
	/* Commands run here bypass the normal command loop, so flush track signal updates. */
	UpdateSignalsInBuffer();
	Debug(Facility::Misc, Severity::Notice, "FL grow company={} profile={} recipe={} result={}", _current_company, profile->key, recipe_key,
		result == ConstructionResult::Success ? "success" : (result == ConstructionResult::Failed ? "failed" : "rejected"));
	return result;
}

/**
 * Pick what a vehicle should be replaced with: the best vehicle the Tech Library would choose today for
 * the same job, family, cargo and track. For a train this is the locomotive.
 * @param vehicle A primary vehicle of the current company.
 * @return The engine to replace its (front) engine with; its own engine if there is nothing better.
 */
EngineID ReplacementFor(VehicleID vehicle_id)
{
	const Vehicle *vehicle = Vehicle::GetIfValid(vehicle_id);
	if (vehicle == nullptr || !vehicle->IsPrimaryVehicle()) return EngineID::Invalid();
	const TechLibrary library = TechLibrary::Build();
	auto current = std::ranges::find(library.Vehicles(), vehicle->engine_type, &TechRecord::engine);
	if (current == library.Vehicles().end()) return vehicle->engine_type;

	/* The cargo it carries decides the job, as when the service was built. */
	CargoType cargo = INVALID_CARGO;
	for (const Vehicle *part = vehicle; part != nullptr; part = part->Next()) {
		if (part->cargo_cap > 0) {
			cargo = part->cargo_type;
			break;
		}
	}
	if (!IsValidCargoType(cargo)) return vehicle->engine_type;
	TechQuery query;
	query.mode = current->mode;
	query.company = vehicle->owner;
	query.family = current->tech;
	query.provides = current->needs;
	query.early_adopters = true;
	const CargoSpec *spec = CargoSpec::Get(cargo);
	if (spec->classes.Test(CargoClass::Passengers)) {
		switch (vehicle->type) {
			case VehicleType::Road: query.role = TechRole::PassengerFeeder; break;
			case VehicleType::Ship: query.role = TechRole::PassengerCommuter; break;
			default: query.role = TechRole::PassengerIntercity; break;
		}
		query.role = TownRole(library, cargo, query.role);
	} else if (spec->classes.Test(CargoClass::Mail)) {
		query.role = TownRole(library, cargo, TechRole::Express);
	} else {
		query.role = FreightRole(library, cargo);
	}
	switch (vehicle->type) {
		case VehicleType::Train:
			query.part = TechPart::Locomotive;
			for (RailType rt : Train::From(vehicle)->railtypes) {
				query.railtype = rt;
				break;
			}
			break;
		case VehicleType::Road:
			query.roadtype = RoadVehicle::From(vehicle)->roadtype;
			query.cargo = cargo;
			break;
		case VehicleType::Aircraft: {
			query.cargo = cargo;
			/* The new aircraft must reach both airports of its route. */
			std::vector<TileIndex> stops;
			for (const Order &order : vehicle->Orders()) {
				if (order.IsType(OT_GOTO_STATION)) stops.push_back(Station::Get(order.GetDestination().ToStationID())->xy);
			}
			if (stops.size() >= 2) query.distance = DistanceManhattan(stops[0], stops[1]);
			break;
		}
		default:
			query.cargo = cargo;
			break;
	}
	const TechRecord *best = library.FindBest(query);
	return best != nullptr ? best->engine : vehicle->engine_type;
}

/**
 * Find a depot where a copy of a vehicle can start and reach the vehicle's route: one on the same track or
 * road, found by following it from where the vehicle is; an airport's hangar; or the nearest ship depot.
 * @param vehicle_id A primary vehicle of the current company.
 * @return Depot tile, or INVALID_TILE if none was found.
 */
TileIndex RouteDepot(VehicleID vehicle_id)
{
	const Vehicle *vehicle = Vehicle::GetIfValid(vehicle_id);
	if (vehicle == nullptr || !vehicle->IsPrimaryVehicle()) return INVALID_TILE;
	TileIndex depot = INVALID_TILE;
	switch (vehicle->type) {
		case VehicleType::Train:
			RailStationsReachable(vehicle, &depot);
			break;
		case VehicleType::Road:
			RoadStationsReachable(vehicle, &depot);
			break;
		case VehicleType::Aircraft:
			for (const Order &order : vehicle->Orders()) {
				if (!order.IsType(OT_GOTO_STATION)) continue;
				const Station *station = Station::GetIfValid(order.GetDestination().ToStationID());
				if (station != nullptr && station->owner == vehicle->owner && station->airport.GetNumHangars() > 0) return station->airport.GetHangarTile(0);
			}
			break;
		case VehicleType::Ship: {
			/* Ships cross open water, so the nearest ship depot of the company serves. */
			uint best = UINT_MAX;
			for (const Order &order : vehicle->Orders()) {
				if (!order.IsType(OT_GOTO_STATION)) continue;
				const Station *station = Station::GetIfValid(order.GetDestination().ToStationID());
				if (station == nullptr) continue;
				for (TileIndex tile : TileArea(station->xy, 1, 1).Expand(16)) {
					if (!IsValidTile(tile) || !IsShipDepotTile(tile) || GetTileOwner(tile) != vehicle->owner || GetShipDepotPart(tile) != DepotPart::North) continue;
					const uint distance = DistanceManhattan(tile, station->xy);
					if (distance < best) {
						best = distance;
						depot = tile;
					}
				}
			}
			break;
		}
		default:
			break;
	}
	return depot;
}

} // namespace FutureLogistics
