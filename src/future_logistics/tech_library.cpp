/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file tech_library.cpp Build and query the Future Logistics Tech Library. */
#include "../stdafx.h"
#include "tech_library.h"
#include "../airport.h"
#include "../articulated_vehicles.h"
#include "../engine_base.h"
#include "../engine_func.h"
#include "../newgrf_airport.h"
#include "../rail.h"
#include "../road.h"
#include "../road_func.h"
#include "../string_func.h"

#include "../safeguards.h"

namespace {
/** Names in the same order as TechMode; they match docs/future_logistics/tech_format.md. */
constexpr std::array<std::string_view, 4> MODE_NAMES{"road", "rail", "air", "water"};
/** Names in the same order as TechRole; they match the fl_role badge names. */
constexpr std::array<std::string_view, to_underlying(TechRole::End)> ROLE_NAMES{
	"passenger_intercity", "passenger_commuter", "passenger_feeder", "freight_bulk", "freight_general", "parcel", "express"};

/**
 * Get the transport mode of a vehicle type.
 * @param type Vehicle type of an engine.
 * @return Matching mode.
 */
TechMode ModeOf(VehicleType type)
{
	switch (type) {
		case VehicleType::Train: return TechMode::Rail;
		case VehicleType::Road: return TechMode::Road;
		case VehicleType::Ship: return TechMode::Water;
		case VehicleType::Aircraft: return TechMode::Air;
		default: NOT_REACHED();
	}
}

/**
 * Describe one engine. Only static facts are recorded; buildability and price are checked per query.
 * @param engine Engine to describe.
 * @return Tech record with inferred roles.
 */
TechRecord Describe(const Engine *engine)
{
	TechRecord record;
	record.engine = engine->index;
	const GrfID grfid = engine->GetGRFID();
	record.mode = ModeOf(engine->type);
	/* Local IDs are numbered per vehicle type, so the mode is part of the identifier. */
	record.id = fmt::format("{}:{}:{}", grfid.Empty() ? "base" : FormatArrayAsHex(grfid), TechModeName(record.mode), engine->grf_prop.local_id);
	record.cargo = engine->GetDefaultCargoType();
	record.refits = GetUnionOfArticulatedRefitMasks(engine->index, true);
	record.capacity = engine->CanCarryCargo() ? engine->GetDisplayDefaultCapacity() : 0;
	record.speed = engine->GetDisplayMaxSpeed();
	record.reliability_max = engine->reliability_max;
	record.life_years = engine->info.lifelength.base();

	switch (engine->type) {
		case VehicleType::Train: {
			const RailVehicleInfo &info = engine->VehInfo<RailVehicleInfo>();
			record.wagon = info.railveh_type == RailVehicleType::Wagon;
			record.traction = !record.wagon && record.capacity == 0;
			for (RailType rt : info.railtypes) {
				record.track = GetRailTypeInfo(rt)->label.AsString();
				break;
			}
			break;
		}
		case VehicleType::Road:
			record.roadtype = engine->VehInfo<RoadVehicleInfo>().roadtype;
			record.track = GetRoadTypeInfo(record.roadtype)->label.AsString();
			break;
		case VehicleType::Aircraft:
			record.helicopter = (engine->VehInfo<AircraftVehicleInfo>().subtype & AIR_CTOL) == 0;
			record.range = engine->GetRange();
			break;
		default:
			break;
	}

	CargoClasses classes{};
	if (IsValidCargoType(record.cargo)) classes = CargoSpec::Get(record.cargo)->classes;
	/* Wagons get the role of their cargo, so the rail planner can match them to a locomotive. */
	record.roles = InferTechRoles(record.mode, classes, record.traction, record.helicopter);
	return record;
}
} // namespace

/**
 * Infer the jobs an untagged vehicle suits, from its mode and default cargo.
 * @param mode Transport mode.
 * @param classes Cargo classes of the default cargo; empty if it carries nothing.
 * @param traction Rail locomotive without its own cargo space.
 * @param helicopter Aircraft that can use a helipad.
 * @return Roles the vehicle can be offered for.
 */
TechRoles InferTechRoles(TechMode mode, CargoClasses classes, bool traction, bool helicopter)
{
	/* A locomotive's job depends on what it pulls; the rail planner pairs it with wagons. */
	if (traction) return {TechRole::PassengerIntercity, TechRole::PassengerCommuter, TechRole::FreightBulk, TechRole::FreightGeneral, TechRole::Express};
	if (classes.Test(CargoClass::Passengers)) {
		switch (mode) {
			case TechMode::Road: return {TechRole::PassengerFeeder, TechRole::PassengerCommuter};
			case TechMode::Rail: return {TechRole::PassengerIntercity, TechRole::PassengerCommuter};
			case TechMode::Air: return helicopter ? TechRoles{TechRole::PassengerCommuter} : TechRoles{TechRole::PassengerIntercity};
			case TechMode::Water: return {TechRole::PassengerCommuter};
		}
	}
	if (classes.Test(CargoClass::Mail)) return {TechRole::Express};
	if (classes.Any({CargoClass::Bulk, CargoClass::Liquid})) return {TechRole::FreightBulk};
	if (classes.Any()) return {TechRole::FreightGeneral};
	return {};
}

/**
 * Score a vehicle: transport work per year of ownership cost. Higher is better.
 * @param capacity Cargo capacity.
 * @param speed Maximum speed.
 * @param reliability_max Best reliability, 0..65535.
 * @param cost Purchase price.
 * @param running_cost Yearly running cost.
 * @param life_years Expected life, used to spread the purchase price.
 * @return Score; only comparable between vehicles in the same query.
 */
double ScoreTech(uint capacity, uint speed, uint16_t reliability_max, Money cost, Money running_cost, int life_years)
{
	const double work = static_cast<double>(capacity) * speed * (reliability_max / 65535.0);
	const double yearly = static_cast<double>(running_cost.base()) + static_cast<double>(cost.base()) / std::max(1, life_years);
	return work / std::max(1.0, yearly);
}

/**
 * Snapshot every loaded engine, rail type, road type and airport.
 * @return The library. It reflects the game as it is now; build a new one after loading a game.
 */
/* static */ TechLibrary TechLibrary::Build()
{
	TechLibrary library;
	for (const Engine *engine : Engine::Iterate()) {
		if (!engine->IsEnabled()) continue;
		library.vehicles.push_back(Describe(engine));
	}
	for (RailType rt : _sorted_railtypes) {
		const RailTypeInfo *rti = GetRailTypeInfo(rt);
		library.infrastructure.push_back({.kind = InfraKind::RailType, .index = rt, .id = rti->label.AsString(), .intro_date = rti->introduction_date, .speed = rti->max_speed, .provides = {}});
	}
	for (RoadType rt : _sorted_roadtypes) {
		const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
		library.infrastructure.push_back({.kind = RoadTypeIsTram(rt) ? InfraKind::TramType : InfraKind::RoadType, .index = rt, .id = rti->label.AsString(), .intro_date = rti->introduction_date, .speed = rti->max_speed, .provides = {}});
	}
	for (uint8_t type = 0; type < NUM_AIRPORTS; type++) {
		const AirportSpec *spec = AirportSpec::Get(type);
		if (!spec->enabled) continue;
		library.infrastructure.push_back({.kind = InfraKind::Airport, .index = type, .id = fmt::format("airport:{}", type), .size_x = spec->size_x, .size_y = spec->size_y, .provides = {}});
	}
	return library;
}

/**
 * Check whether a record answers a query right now.
 * @param record Candidate vehicle.
 * @param query What the planner needs.
 * @return Whether the company could buy it today and use it for this job.
 */
bool TechLibrary::IsUsable(const TechRecord &record, const TechQuery &query) const
{
	if (record.ai_avoid || record.wagon || record.mode != query.mode || !record.roles.Test(query.role)) return false;
	/* Special infrastructure is matched in phase 5; until then never offer such vehicles. */
	if (!record.needs.empty()) return false;
	if (query.roadtype != INVALID_ROADTYPE && record.roadtype != query.roadtype) return false;
	if (IsValidCargoType(query.cargo) && !record.traction && (record.capacity == 0 || !record.refits.Test(query.cargo))) return false;
	const Engine *engine = Engine::Get(record.engine);
	VehicleType type = engine->type;
	if (!IsEngineBuildable(record.engine, type, query.company)) return false;
	return engine->GetCost() <= query.budget;
}

/**
 * Pick the best vehicle for a job. Ties go to the lowest engine index, so the result is reproducible.
 * @param query What the planner needs.
 * @return Best usable record, or nullptr if nothing fits.
 */
const TechRecord *TechLibrary::FindBest(const TechQuery &query) const
{
	const TechRecord *best = nullptr;
	double best_score = -1;
	for (const TechRecord &record : this->vehicles) {
		if (!this->IsUsable(record, query)) continue;
		const Engine *engine = Engine::Get(record.engine);
		const double score = ScoreTech(record.capacity, record.speed, record.reliability_max, engine->GetCost(), engine->GetRunningCost(), record.life_years);
		if (score > best_score) {
			best = &record;
			best_score = score;
		}
	}
	return best;
}

std::string_view TechModeName(TechMode mode)
{
	return MODE_NAMES[to_underlying(mode)];
}

std::string_view TechRoleName(TechRole role)
{
	return ROLE_NAMES[to_underlying(role)];
}

std::optional<TechMode> ParseTechMode(std::string_view name)
{
	auto it = std::ranges::find(MODE_NAMES, name);
	if (it == MODE_NAMES.end()) return std::nullopt;
	return static_cast<TechMode>(std::distance(MODE_NAMES.begin(), it));
}

std::optional<TechRole> ParseTechRole(std::string_view name)
{
	auto it = std::ranges::find(ROLE_NAMES, name);
	if (it == ROLE_NAMES.end()) return std::nullopt;
	return static_cast<TechRole>(std::distance(ROLE_NAMES.begin(), it));
}

/**
 * Format roles as a comma separated list of badge names.
 * @param roles Roles to format.
 * @return For example "passenger_feeder,passenger_commuter", or "-" when empty.
 */
std::string FormatTechRoles(TechRoles roles)
{
	std::string result;
	for (uint8_t i = 0; i < to_underlying(TechRole::End); i++) {
		if (!roles.Test(static_cast<TechRole>(i))) continue;
		if (!result.empty()) result += ',';
		result += ROLE_NAMES[i];
	}
	return result.empty() ? "-" : result;
}
