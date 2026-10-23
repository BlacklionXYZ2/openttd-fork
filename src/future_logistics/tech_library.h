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
#include "../newgrf_badge_type.h"
#include "../rail_type.h"
#include "../road_type.h"
#include "../timer/timer_game_calendar.h"
#include <map>

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
	bool fast = false; ///< Aircraft at risk of crashing on airports with a short strip.
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

	/* Fields below come from fl_ badges; see docs/future_logistics/tech_format.md. */
	std::string tech = "conventional"; ///< Technology family (fl_tech).
	uint8_t tier = 0; ///< Progression level (fl_tier).
	TechRoles roles{}; ///< Jobs this vehicle suits (fl_role, the shared role/ badges, or inferred).
	bool declared_roles = false; ///< The roles come from fl_role or a specific role/ badge rather than a guess, so it is preferred for them.
	std::vector<std::string> power; ///< Power sources from the shared power/ badges, such as "electric".
	std::vector<std::string> needs; ///< Special infrastructure required (fl_needs).
	bool ai_avoid = false; ///< Rival companies must not use it (fl_ai/avoid).
	bool early_adopter = false; ///< Only adoption rules with no delay may use it (fl_ai/early_adopter).
	bool tagged = false; ///< Carries fl_format/1 or fl_format/2.
	TimerGameCalendar::Date intro_date{}; ///< When the engine is introduced.
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
	std::string tech = "conventional"; ///< Technology family (fl_tech).
	std::vector<std::string> provides; ///< Special sites supplied (fl_provides).
};

/** The fl_ tags read from one item's badges. */
struct ItemTags {
	bool any = false; ///< Has at least one fl_ badge.
	bool formatted = false; ///< Has fl_format/1 or fl_format/2; without it, the other fl_ tags are ignored.
	std::optional<std::string> id; ///< fl_id: a stable name code can find the item by.
	std::optional<std::string> tech; ///< fl_tech
	std::optional<uint8_t> tier; ///< fl_tier
	TechRoles roles{}; ///< fl_role, all of them.
	std::vector<std::string> needs; ///< fl_needs
	std::vector<std::string> provides; ///< fl_provides
	std::optional<std::string> industry; ///< fl_industry
	std::vector<std::string> markets; ///< fl_market
	bool avoid = false; ///< fl_ai/avoid
	bool early_adopter = false; ///< fl_ai/early_adopter
	std::vector<std::string> problems; ///< Labels that could not be understood.

	/* Shared badge classes from the NewGRF specification, which any NewGRF may use; read without fl_format. */
	TechRoles community_roles{}; ///< Roles from known role/ badges, such as role/freight.
	bool community_universal = false; ///< Has role/universal: suits every job.
	bool community_role = false; ///< Has at least one role/ badge, known or not.
	std::vector<std::string> power; ///< power/<source> badges.
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
	bool short_strip = false; ///< The airports have a short strip, so fast aircraft are unsafe.
	uint distance = 0; ///< Route length in tiles that an aircraft's range must cover; 0 to ignore.
	std::string family; ///< Tech family the vehicle must belong to, or empty for any.
	std::vector<std::string> provides; ///< Sites available, which must cover the vehicle's fl_needs.
	bool early_adopters = true; ///< Whether fl_ai/early_adopter vehicles may be used.
};

/** Snapshot of all loaded transport technology. Derived data: build it when needed, never save it. */
class TechLibrary {
public:
	static TechLibrary Build();

	const TechRecord *FindBest(const TechQuery &query) const;
	bool IsUsable(const TechRecord &record, const TechQuery &query) const;

	const std::vector<TechRecord> &Vehicles() const { return this->vehicles; }
	const std::vector<InfraRecord> &Infrastructure() const { return this->infrastructure; }
	const std::vector<std::string> &Problems() const { return this->problems; }
	TechRoles CargoRoles(CargoType cargo) const;

private:
	std::vector<TechRecord> vehicles;
	std::vector<InfraRecord> infrastructure;
	std::map<CargoType, TechRoles> cargo_roles; ///< From fl_cargo declarations.
	std::vector<std::string> problems; ///< Tag problems, for future_logistics_tech validate.
};

TechRoles InferTechRoles(TechMode mode, CargoClasses classes, bool traction, bool helicopter);
double ScoreTech(uint capacity, uint speed, uint16_t reliability_max, Money cost, Money running_cost, int life_years);
std::string_view TechModeName(TechMode mode);
std::string TechId(const Engine *engine);
std::string_view TechRoleName(TechRole role);
std::optional<TechMode> ParseTechMode(std::string_view name);
std::optional<TechRole> ParseTechRole(std::string_view name);
std::string FormatTechRoles(TechRoles roles);
std::vector<std::string_view> SplitLabel(std::string_view label);
ItemTags ParseItemTags(std::span<const std::string> labels);
std::optional<TechRoles> CommunityRoleMeaning(std::string_view name);
ItemTags ReadItemTags(std::span<const BadgeID> badges);
std::map<std::string, TechRoles> ParseCargoRoles(std::span<const std::string> labels, std::vector<std::string> &problems);

/** Console command: future_logistics_tech list | badges | recipes | validate | best <mode> <role> [cargo label]. */
bool FutureLogisticsTechCommand(std::span<std::string_view> argv);

#endif /* FUTURE_LOGISTICS_TECH_LIBRARY_H */
