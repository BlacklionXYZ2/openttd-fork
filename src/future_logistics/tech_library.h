/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file tech_library.h Future Logistics Tech Library: what transport technology is loaded and what it is good for. */
#ifndef FUTURE_LOGISTICS_TECH_LIBRARY_H
#define FUTURE_LOGISTICS_TECH_LIBRARY_H

#include "../cargo_type.h"
#include "../cargotype.h"
#include "../company_type.h"
#include "../core/enum_type.hpp"
#include "../economy_type.h"
#include "../engine_type.h"
#include "../rail_type.h"
#include "../road_type.h"
#include "../timer/timer_game_calendar.h"

/** Transport mode of a tech record. See docs/future_logistics/tech_format.md. */
enum class TechMode : uint8_t {
	Road,
	Rail,
	Air,
	Water,
};

/** Jobs a vehicle is suited to. Names match the fl_role badges. */
enum class TechRole : uint8_t {
	PassengerIntercity, ///< Long-distance passengers.
	PassengerCommuter, ///< Short, frequent passenger services.
	PassengerFeeder, ///< Small vehicles feeding a hub.
	FreightBulk, ///< Coal, ore, grain, liquids and similar.
	FreightGeneral, ///< Goods and other piece cargo.
	Parcel, ///< Parcels and e-commerce.
	Express, ///< Mail and urgent cargo.
	End,
};
using TechRoles = EnumBitSet<TechRole, uint8_t>;

/** Kind of infrastructure record. */
enum class InfraKind : uint8_t {
	RailType,
	RoadType,
	TramType,
	Airport,
};

/** One vehicle in the library. Static facts only; availability is checked per query. */
struct TechRecord {
	EngineID engine = EngineID::Invalid(); ///< Engine this record describes.
	std::string id; ///< Stable identifier: "<base or GRF ID>:<mode>:<local id>".
	TechMode mode = TechMode::Road; ///< Transport mode.
	bool traction = false; ///< Rail locomotive that carries no cargo itself.
	bool wagon = false; ///< Rail wagon, which cannot run on its own.
	bool helicopter = false; ///< Aircraft that only needs a helipad.
	CargoType cargo = INVALID_CARGO; ///< Default cargo.
	CargoTypes refits{}; ///< All cargo the vehicle can be refitted to, including its default.
	uint capacity = 0; ///< Default capacity.
	uint speed = 0; ///< Maximum speed in display units (km-ish/h).
	uint16_t range = 0; ///< Aircraft order range in tiles, 0 for unlimited.
	uint16_t reliability_max = 0; ///< Best reliability, 0..65535.
	int life_years = 0; ///< Expected vehicle life.
	RoadType roadtype = INVALID_ROADTYPE; ///< Road type for road vehicles.
	RailTypes railtypes{}; ///< Rail types for rail vehicles.
	std::string track; ///< Rail or road type label for ground vehicles.

	/* Fields below come from fl_ badges once phase 5 lands; until then they hold the defaults. */
	std::string tech = "conventional"; ///< Technology family (fl_tech).
	uint8_t tier = 0; ///< Progression level (fl_tier).
	TechRoles roles{}; ///< Jobs this vehicle suits (fl_role, or inferred).
	std::vector<std::string> needs; ///< Special infrastructure required (fl_needs).
	bool ai_avoid = false; ///< Rival companies must not use it (fl_ai/avoid).
	bool tagged = false; ///< Carries fl_format/1.
};

/** One rail type, road type or airport in the library. */
struct InfraRecord {
	InfraKind kind = InfraKind::RailType;
	uint index = 0; ///< RailType, RoadType or airport type.
	std::string id; ///< Label for rail/road/tram types, "airport:<type>" for airports.
	TimerGameCalendar::Date intro_date{}; ///< Introduction date, if any.
	uint speed = 0; ///< Speed limit for rail/road types, 0 for none.
	uint8_t size_x = 0; ///< Airport footprint.
	uint8_t size_y = 0; ///< Airport footprint.
	std::vector<std::string> provides; ///< Special sites supplied (fl_provides).
};

/** Which part of a consist a query wants. */
enum class TechPart : uint8_t {
	Vehicle, ///< Anything that runs on its own, i.e. not a wagon.
	Locomotive, ///< Rail locomotive without its own cargo space.
	Wagon, ///< Rail wagon.
};

/** What a planner is looking for. */
struct TechQuery {
	TechMode mode = TechMode::Road;
	TechRole role = TechRole::PassengerFeeder;
	CargoType cargo = INVALID_CARGO; ///< Required cargo, or INVALID_CARGO for any.
	CompanyID company = CompanyID::Invalid(); ///< Company that would buy it.
	RoadType roadtype = INVALID_ROADTYPE; ///< Required road type, or INVALID_ROADTYPE for any.
	Money budget = INT64_MAX; ///< Maximum purchase price.
	TechPart part = TechPart::Vehicle; ///< Which part of a consist.
	RailType railtype = INVALID_RAILTYPE; ///< Track the vehicle must run on, or INVALID_RAILTYPE for any.
};

/** Snapshot of all loaded transport technology. Derived data: build it when needed, never save it. */
class TechLibrary {
public:
	static TechLibrary Build();

	const TechRecord *FindBest(const TechQuery &query) const;
	bool IsUsable(const TechRecord &record, const TechQuery &query) const;

	const std::vector<TechRecord> &Vehicles() const { return this->vehicles; }
	const std::vector<InfraRecord> &Infrastructure() const { return this->infrastructure; }

private:
	std::vector<TechRecord> vehicles;
	std::vector<InfraRecord> infrastructure;
};

TechRoles InferTechRoles(TechMode mode, CargoClasses classes, bool traction, bool helicopter);
double ScoreTech(uint capacity, uint speed, uint16_t reliability_max, Money cost, Money running_cost, int life_years);
std::string_view TechModeName(TechMode mode);
std::string_view TechRoleName(TechRole role);
std::optional<TechMode> ParseTechMode(std::string_view name);
std::optional<TechRole> ParseTechRole(std::string_view name);
std::string FormatTechRoles(TechRoles roles);

/** Console command: future_logistics_tech list | best <mode> <role> [cargo label]. */
bool FutureLogisticsTechCommand(std::span<std::string_view> argv);

#endif /* FUTURE_LOGISTICS_TECH_LIBRARY_H */
