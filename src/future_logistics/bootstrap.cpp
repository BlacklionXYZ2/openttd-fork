/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file bootstrap.cpp Bounded, deterministic town-to-town bus network bootstrap. */
#include "../stdafx.h"
#include "bootstrap.h"
#include "air_planner.h"
#include "planner_common.h"
#include "catalogue.h"
#include "growth.h"
#include "rail_planner.h"
#include "road_planner.h"
#include "water_planner.h"
#include "scale.h"
#include "tech_library.h"
#include "../ai/ai.hpp"
#include "../ai/ai_config.hpp"
#include "../company_cmd.h"
#include "../cargotype.h"
#include "../command_func.h"
#include "../company_base.h"
#include "../core/backup_type.hpp"
#include "../debug.h"
#include "../engine_base.h"
#include "../engine_func.h"
#include "../industry.h"
#include "../landscape_cmd.h"
#include "../misc_cmd.h"
#include "../network/network.h"
#include "../order_base.h"
#include "../order_cmd.h"
#include "../road_cmd.h"
#include "../road_map.h"
#include "../roadveh.h"
#include "../direction_func.h"
#include "../settings_type.h"
#include "../signal_func.h"
#include "../station_cmd.h"
#include "../station_func.h"
#include "../station_map.h"
#include "../town.h"
#include "../vehicle_cmd.h"
#include <queue>
#include <map>
#include <set>

#include "../safeguards.h"

using namespace FutureLogistics;

extern Company *DoStartupNewCompany(bool is_ai, CompanyID company);

namespace {
/**
 * Name a new rival after its profile and tell its CorporateAI which profile it has.
 * Both are stored in the save game.
 * @param company New rival.
 * @param profile Its profile.
 */
void ApplyProfile(Company *company, const Profile &profile)
{
	AutoRestoreBackup owner(_current_company, company->index);
	if (Command<Commands::RenameCompany>::Do(EXEC, profile.name).Failed()) {
		Debug(Facility::Misc, Severity::Warning, "FL could not rename company={} to profile name", company->index);
	}
	/* Ignored unless the company runs an AI with a "profile" setting, such as CorporateAI. */
	if (company->ai_config != nullptr) company->ai_config->SetSetting("profile", profile.id);
	Debug(Facility::Misc, Severity::Notice, "FL profile company={} profile={}", company->index, profile.key);
}
} // namespace

void BootstrapFutureLogistics()
{
	if (_networking || !_settings_game.game_creation.bootstrap_enabled) return;
	/* Fail closed for unexpected lifecycle calls, including a second invocation. */
	if (Company::GetNumItems() != 1 || !Company::IsValidID(CompanyID{0}) || Company::Get(CompanyID{0})->is_ai) {
		Debug(Facility::Misc, Severity::Warning, "FL skipped: expected only human company 0");
		return;
	}
	const TechLibrary library = TechLibrary::Build();
	const Catalogue catalogue = Catalogue::Build(library);
	const auto &profiles = catalogue.Profiles();
	/* One rival per profile: the built-in four, then any declared by NewGRFs, up to the competitor setting. */
	const uint count = std::min<uint>(static_cast<uint>(profiles.size()), _settings_game.difficulty.max_no_competitors);
	Debug(Facility::Misc, Severity::Notice, "FL begin seed={} rivals={}", _settings_game.game_creation.generation_seed, count);
	Debug(Facility::Misc, Severity::Notice, "FL catalogue families={} profiles={} problems={}", catalogue.Families().size(), profiles.size(),
		catalogue.Problems().size() + library.Problems().size());
	uint operational = 0;
	uint services = 0;
	Reservations reserved;
	/* The headstart is in proportion to the world: capital follows the total population, capped by the setting. */
	uint64_t population = 0;
	for (const Town *town : Town::Iterate()) population += town->cache.population;
	const Money grant = RivalGrant(population, count, _settings_game.game_creation.bootstrap_capital_per_head, _settings_game.game_creation.bootstrap_capital);
	const uint rounds = ServiceRounds(population);
	Debug(Facility::Misc, Severity::Notice, "FL market population={} towns={} industries={} grant={} rounds={}", population, Town::GetNumItems(), Industry::GetNumItems(), grant, rounds);
	for (uint i = 1; i <= count; i++) {
		Company *company = DoStartupNewCompany(true, CompanyID{static_cast<uint8_t>(i)});
		if (company == nullptr) { Debug(Facility::Misc, Severity::Warning, "FL company allocation failed slot={}", i); break; }
		/* StartNew initializes a VM; no AI GameLoop runs in this synchronous callback. */
		AI::Pause(company->index);
		if (grant > 0) {
			AutoRestoreBackup owner(_current_company, OWNER_DEITY);
			auto result = Command<Commands::ChangeBankBalance>::Do(EXEC, TileIndex{0}, grant, company->index, ExpensesType::Other);
			if (result.Failed()) {
				Debug(Facility::Misc, Severity::Error, "FL funding failed company={}", company->index);
				AI::Unpause(company->index);
				continue;
			}
		}
		Debug(Facility::Misc, Severity::Notice, "FL funded company={} grant={} balance={} loan={}", company->index,
			grant, company->money, company->current_loan);
		const Profile &profile = profiles[i - 1];
		ApplyProfile(company, profile);
		const std::vector<Recipe> plan = catalogue.Plan(profile, library, TimerGameCalendar::date);
		std::string plan_names;
		for (const Recipe &recipe : plan) plan_names += (plan_names.empty() ? "" : ",") + recipe.Key();
		Debug(Facility::Misc, Severity::Notice, "FL plan company={} recipes={}", company->index, plan_names.empty() ? "-" : plan_names);
		{
			AutoRestoreBackup owner(_current_company, company->index);
			uint built = 0;
			bool stopped = false;
			auto attempt = [&](const Recipe &recipe, bool fallback) {
				const ConstructionResult result = BuildService(recipe, profile, plan, library, reserved);
				/* Commands run here bypass the normal command loop, so flush track signal updates before another company builds. */
				UpdateSignalsInBuffer();
				Debug(Facility::Misc, Severity::Notice, "FL service company={} recipe={} fallback={} result={}", company->index, recipe.Key(), fallback,
					result == ConstructionResult::Success ? "success" : (result == ConstructionResult::Failed ? "failed" : "rejected"));
				if (result == ConstructionResult::Success) built++;
				/* After an execution failure the company stops building, as rollback may be incomplete. */
				if (result == ConstructionResult::Failed) stopped = true;
			};
			for (uint round = 0; round < rounds; round++) {
				for (const Recipe &recipe : plan) {
					if (!stopped) attempt(recipe, false);
				}
			}
			const Recipe bus{std::string{CONVENTIONAL}, "road_route", Market::TownPair};
			if (built == 0 && !stopped && std::ranges::find(plan, bus) == plan.end()) attempt(bus, true);
			services += built;
			if (built > 0) {
				operational++;
			} else {
				Debug(Facility::Misc, Severity::Warning, "FL no usable starter network company={} (terrain, towns, industries, funds or vehicle availability); funded company retained", company->index);
			}
		}
		AI::Unpause(company->index);
	}
	Debug(Facility::Misc, Severity::Notice, "FL complete companies={} operational={} services={}", Company::GetNumItems(), operational, services);
}

bool InspectFutureLogistics()
{
	bool valid = Company::IsValidID(CompanyID{0}) && !Company::Get(CompanyID{0})->is_ai && _local_company == CompanyID{0};
	uint checked = 0;
	for (const Company *company : Company::Iterate()) {
		Debug(Facility::Misc, Severity::Notice, "FL check funds company={} balance={} loan={} road_bits={} station_tiles={} rail_bits={} other_expense={} ai={}", company->index, company->money, company->current_loan, company->infrastructure.GetRoadTotal(), company->infrastructure.station, company->infrastructure.GetRailTotal(), company->yearly_expenses[0][ExpensesType::Other], company->is_ai);
	}
	for (const Vehicle *vehicle : Vehicle::Iterate()) {
		if (vehicle->type == VehicleType::Effect || vehicle->type == VehicleType::Disaster || !vehicle->IsPrimaryVehicle() || vehicle->owner == CompanyID{0}) continue;
		/* A rival's vehicle stopped in a depot is waiting to be sold, not a broken route. */
		if (Company::IsValidAiID(vehicle->owner) && vehicle->IsStoppedInDepot()) {
			Debug(Facility::Misc, Severity::Notice, "FL check parked vehicle={} company={}", vehicle->index, vehicle->owner);
			continue;
		}
		/* Implicit orders record stations a vehicle stopped at on the way; only the vehicle's own orders count. */
		bool connected = Company::IsValidAiID(vehicle->owner) && vehicle->GetNumManualOrders() == 2 && !vehicle->vehstatus.Test(VehState::Stopped);
		std::set<StationID> destinations;
		for (const Order &order : vehicle->Orders()) {
			if (order.IsType(OT_IMPLICIT)) continue;
			if (!order.IsType(OT_GOTO_STATION)) { connected = false; continue; }
			destinations.insert(order.GetDestination().ToStationID());
		}
		std::set<StationID> reached;
		switch (vehicle->type) {
			case VehicleType::Train: reached = RailStationsReachable(vehicle); break;
			case VehicleType::Aircraft: reached = AirportsReachable(vehicle); break;
			case VehicleType::Ship: reached = DocksReachable(vehicle); break;
			default: reached = RoadStationsReachable(vehicle); break;
		}
		connected &= destinations.size() == 2 && std::ranges::includes(reached, destinations);
		if (!connected) {
			Debug(Facility::Misc, Severity::Notice, "FL check detail vehicle={} orders={} stopped={} destinations={} reached={}", vehicle->index, vehicle->GetNumOrders(),
				vehicle->vehstatus.Test(VehState::Stopped), destinations.size(), reached.size());
		}
		valid &= connected;
		checked++;
		/* All cargo, so freight trains count too. */
		const Company *company = Company::Get(vehicle->owner);
		uint delivered = company->cur_economy.delivered_cargo.GetSum<uint>();
		for (const auto &quarter : company->old_economy) delivered += quarter.delivered_cargo.GetSum<uint>();
		uint carrying = 0;
		for (const Vehicle *part = vehicle; part != nullptr; part = part->Next()) carrying += part->cargo.StoredCount();
		Debug(Facility::Misc, Severity::Notice, "FL check company={} vehicle={} type={} connected={} served={} tile={} profit={} delivered={} carrying={} engine={} age={}",
			vehicle->owner, vehicle->index, vehicle->type == VehicleType::Train ? "rail" : (vehicle->type == VehicleType::Aircraft ? "air" : (vehicle->type == VehicleType::Ship ? "water" : "road")), connected, destinations.contains(vehicle->last_station_visited),
			vehicle->tile, vehicle->profit_this_year, delivered, carrying, TechId(Engine::Get(vehicle->engine_type)), vehicle->age.base() / 366);
	}
	Debug(Facility::Misc, Severity::Notice, "FL check result={} companies={} vehicles={} human={}", valid, Company::GetNumItems(), checked, _local_company);
	return valid;
}
