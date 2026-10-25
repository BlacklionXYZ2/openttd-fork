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
#include "../newgrf_badge.h"
#include "../rail.h"
#include "../road.h"
#include "../road_func.h"
#include "../string_func.h"

#include <map>

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
 * @param cargo_roles Roles declared for cargo types, which replace the inferred ones.
 * @param[in,out] problems Tag problems found.
 * @return Tech record with its tags applied, or with inferred roles if it has none.
 */
TechRecord Describe(const Engine *engine, const std::map<CargoType, TechRoles> &cargo_roles, std::vector<std::string> &problems)
{
	TechRecord record;
	record.engine = engine->index;
	record.mode = ModeOf(engine->type);
	record.id = TechId(engine);
	record.cargo = engine->GetDefaultCargoType();
	record.refits = GetUnionOfArticulatedRefitMasks(engine->index, true);
	/* As the purchase list does: trains and road vehicles count every articulated part, which some sets use for all of a wagon's capacity. */
	if (!engine->CanCarryCargo() || !IsValidCargoType(record.cargo)) {
		record.capacity = 0;
	} else if (engine->type == VehicleType::Train || engine->type == VehicleType::Road) {
		record.capacity = GetCapacityOfArticulatedParts(engine->index)[record.cargo];
	} else {
		record.capacity = engine->CanCarryCargo() ? engine->GetDisplayDefaultCapacity() : 0;
	}
	record.speed = engine->GetDisplayMaxSpeed();
	record.reliability_max = engine->reliability_max;
	record.life_years = engine->info.lifelength.base();
	record.intro_date = engine->intro_date;

	switch (engine->type) {
		case VehicleType::Train: {
			const RailVehicleInfo &info = engine->VehInfo<RailVehicleInfo>();
			record.wagon = info.railveh_type == RailVehicleType::Wagon;
			record.traction = !record.wagon && record.capacity == 0;
			record.railtypes = info.railtypes;
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
			record.fast = (engine->VehInfo<AircraftVehicleInfo>().subtype & AIR_FAST) != 0;
			record.range = engine->GetRange();
			break;
		default:
			break;
	}

	CargoClasses classes{};
	if (IsValidCargoType(record.cargo)) classes = CargoSpec::Get(record.cargo)->classes;
	/* Wagons get the role of their cargo, so the rail planner can match them to a locomotive. */
	record.roles = InferTechRoles(record.mode, classes, record.traction, record.helicopter);
	if (auto it = cargo_roles.find(record.cargo); it != cargo_roles.end() && !record.traction) record.roles = it->second;

	const ItemTags tags = ReadItemTags(engine->badges);
	for (const std::string &problem : tags.problems) problems.push_back(fmt::format("vehicle {}: {}", record.id, problem));
	if (tags.any && !tags.formatted) problems.push_back(fmt::format("vehicle {}: fl_ tags ignored because fl_format/1 or fl_format/2 is missing", record.id));
	/* The shared role/ badges narrow the guess, unless they contradict what the vehicle can carry; role/universal keeps it. */
	if (tags.community_roles.Any() && !tags.community_universal) {
		const TechRoles narrowed = record.roles & tags.community_roles;
		if (narrowed.Any()) {
			record.roles = narrowed;
			record.declared_roles = true;
		}
	}
	record.power = tags.power;
	if (tags.formatted) {
		record.tagged = true;
		if (tags.tech.has_value()) record.tech = *tags.tech;
		if (tags.tier.has_value()) record.tier = *tags.tier;
		if (tags.roles.Any()) {
			record.roles = tags.roles;
			record.declared_roles = true;
		}
		if (!tags.community_role) problems.push_back(fmt::format("vehicle {}: no role/ badge, so other NewGRFs, AIs and the purchase list filter cannot see its job", record.id));
		record.needs = tags.needs;
		record.ai_avoid = tags.avoid;
		record.early_adopter = tags.early_adopter;
	}
	return record;
}

/**
 * Apply an infrastructure item's tags.
 * @param record Record to update.
 * @param badges The item's badges.
 * @param[in,out] problems Tag problems found.
 */
void ApplyInfraTags(InfraRecord &record, std::span<const BadgeID> badges, std::vector<std::string> &problems)
{
	const ItemTags tags = ReadItemTags(badges);
	for (const std::string &problem : tags.problems) problems.push_back(fmt::format("infrastructure {}: {}", record.id, problem));
	if (tags.any && !tags.formatted) problems.push_back(fmt::format("infrastructure {}: fl_ tags ignored because fl_format/1 or fl_format/2 is missing", record.id));
	if (!tags.formatted) return;
	if (tags.tech.has_value()) record.tech = *tags.tech;
	record.provides = tags.provides;
}
} // namespace

/**
 * Get the identifier the Tech Library and logs use for a vehicle: its NewGRF, mode and local ID, such as "4E4F5602:rail:12".
 * Local IDs are numbered per vehicle type, so the mode is part of the identifier.
 * @param engine Engine.
 * @return Identifier, with "base" for the base game's vehicles.
 */
std::string TechId(const Engine *engine)
{
	const GrfID grfid = engine->GetGRFID();
	return fmt::format("{}:{}:{}", grfid.Empty() ? "base" : FormatArrayAsHex(grfid), TechModeName(ModeOf(engine->type)), engine->grf_prop.local_id);
}

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
	/* Cargo meanings are declarations, so they come from every badge rather than from an item. */
	std::vector<std::string> labels;
	for (const Badge &badge : GetBadges()) labels.push_back(badge.label);
	for (const auto &[label, roles] : ParseCargoRoles(labels, library.problems)) {
		for (const CargoSpec *spec : CargoSpec::Iterate()) {
			if (spec->label.AsString() == label) library.cargo_roles[spec->Index()] = roles;
		}
	}
	for (const Engine *engine : Engine::Iterate()) {
		if (!engine->IsEnabled()) continue;
		library.vehicles.push_back(Describe(engine, library.cargo_roles, library.problems));
	}
	for (RailType rt : _sorted_railtypes) {
		const RailTypeInfo *rti = GetRailTypeInfo(rt);
		InfraRecord &record = library.infrastructure.emplace_back();
		record.kind = InfraKind::RailType;
		record.index = rt;
		record.id = rti->label.AsString();
		record.intro_date = rti->introduction_date;
		record.speed = rti->max_speed;
		ApplyInfraTags(record, rti->badges, library.problems);
	}
	for (RoadType rt : _sorted_roadtypes) {
		const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
		InfraRecord &record = library.infrastructure.emplace_back();
		record.kind = RoadTypeIsTram(rt) ? InfraKind::TramType : InfraKind::RoadType;
		record.index = rt;
		record.id = rti->label.AsString();
		record.intro_date = rti->introduction_date;
		record.speed = rti->max_speed;
		ApplyInfraTags(record, rti->badges, library.problems);
	}
	for (uint8_t type = 0; type < NUM_AIRPORTS; type++) {
		/* A NewGRF airport based on a built-in one replaces it: the built-in one is disabled and Get() redirects to
		 * the replacement, which is listed under its own type. Read each type without the redirect. */
		const AirportSpec *spec = AirportSpec::GetWithoutOverride(type);
		if (!spec->enabled) continue;
		InfraRecord &record = library.infrastructure.emplace_back();
		record.kind = InfraKind::Airport;
		record.index = type;
		record.id = fmt::format("airport:{}", type);
		record.size_x = spec->size_x;
		record.size_y = spec->size_y;
		ApplyInfraTags(record, spec->badges, library.problems);
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
	if (record.ai_avoid || record.mode != query.mode || !record.roles.Test(query.role)) return false;
	if (!query.family.empty() && record.tech != query.family) return false;
	if (record.early_adopter && !query.early_adopters) return false;
	/* Special infrastructure: every site the vehicle needs must be provided. */
	for (const std::string &need : record.needs) {
		if (std::ranges::find(query.provides, need) == query.provides.end()) return false;
	}
	switch (query.part) {
		case TechPart::Vehicle: if (record.wagon) return false; break;
		case TechPart::Locomotive: if (!record.traction) return false; break;
		case TechPart::Wagon: if (!record.wagon) return false; break;
	}
	if (query.railtype != INVALID_RAILTYPE) {
		/* Locomotives need power on the track; wagons only need to fit on it. */
		if (record.mode != TechMode::Rail) return false;
		if (record.wagon ? !IsCompatibleRail(record.railtypes, query.railtype) : !HasPowerOnRail(record.railtypes, query.railtype)) return false;
	}
	if (query.roadtype != INVALID_ROADTYPE && record.roadtype != query.roadtype) return false;
	if (query.short_strip && record.fast) return false;
	if (query.helicopter && !record.helicopter) return false;
	if (query.distance > 0 && record.range > 0 && record.range < query.distance) return false;
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
	bool best_declared = false;
	for (const TechRecord &record : this->vehicles) {
		if (!this->IsUsable(record, query)) continue;
		const Engine *engine = Engine::Get(record.engine);
		/* A locomotive carries nothing, so its pulling power stands in for capacity.
		 * Wagons never break down, and a wagon speed of 0 means no limit. */
		const uint capacity = record.traction ? engine->GetPower() : record.capacity;
		const uint speed = record.wagon && record.speed == 0 ? UINT16_MAX : record.speed;
		const uint16_t reliability = record.wagon ? UINT16_MAX : record.reliability_max;
		const double score = ScoreTech(capacity, speed, reliability, engine->GetCost(), engine->GetRunningCost(), record.life_years);
		/* A vehicle its NewGRF says is made for the job beats one that only might do it. */
		const bool declared = record.declared_roles && record.roles.Test(query.role);
		if (best == nullptr || declared > best_declared || (declared == best_declared && score > best_score)) {
			best = &record;
			best_score = score;
			best_declared = declared;
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

/**
 * Split a badge label into its parts.
 * @param label Label such as "fl_tech/rocket".
 * @return Parts between the slashes.
 */
std::vector<std::string_view> SplitLabel(std::string_view label)
{
	std::vector<std::string_view> parts;
	for (;;) {
		const size_t slash = label.find('/');
		parts.push_back(label.substr(0, slash));
		if (slash == std::string_view::npos) break;
		label.remove_prefix(slash + 1);
	}
	return parts;
}

/**
 * Check a name used inside a label: lower-case letters, digits and underscores.
 * @param name Name to check.
 * @return Whether it is valid.
 */
static bool IsValidName(std::string_view name)
{
	return !name.empty() && std::ranges::all_of(name, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

/**
 * The jobs a role/ badge from the shared badge classes stands for. These are the names the NewGRF
 * specification proposes and that published sets use; unknown ones are ignored.
 */
static const std::map<std::string_view, TechRoles> COMMUNITY_ROLES{
	{"passenger", {TechRole::PassengerIntercity, TechRole::PassengerCommuter, TechRole::PassengerFeeder}},
	{"freight", {TechRole::FreightBulk, TechRole::FreightGeneral}},
	{"mail", {TechRole::Express, TechRole::Parcel}},
	{"express", {TechRole::PassengerIntercity, TechRole::Express}},
	{"very_high_speed", {TechRole::PassengerIntercity}},
	{"high_power_railcar", {TechRole::PassengerIntercity}},
	{"intercity", {TechRole::PassengerIntercity}},
	{"metro", {TechRole::PassengerCommuter}},
	{"suburban", {TechRole::PassengerCommuter}},
	{"commuter", {TechRole::PassengerCommuter}},
	{"suburban_or_universal_railcar", {TechRole::PassengerCommuter, TechRole::PassengerIntercity}},
	{"metro_freight", {TechRole::FreightGeneral}},
	{"bus", {TechRole::PassengerCommuter, TechRole::PassengerFeeder}},
	{"coach", {TechRole::PassengerIntercity}},
	{"bulk", {TechRole::FreightBulk}},
	{"parcel", {TechRole::Parcel}},
};

/**
 * Get the jobs a shared role/ badge stands for.
 * @param name The part after "role/", such as "freight".
 * @return Roles, or empty for role/universal and names this build does not know.
 */
std::optional<TechRoles> CommunityRoleMeaning(std::string_view name)
{
	auto it = COMMUNITY_ROLES.find(name);
	if (it == COMMUNITY_ROLES.end()) return std::nullopt;
	return it->second;
}

/**
 * Read the fl_ tags in an item's badge labels. Declarations and other badges are not tags and are skipped or reported.
 * @param labels Badge labels of one item.
 * @return The tags, with any problems.
 */
ItemTags ParseItemTags(std::span<const std::string> labels)
{
	ItemTags tags;
	for (const std::string &label : labels) {
		if (label.starts_with("role/")) {
			tags.community_role = true;
			const std::string_view name = std::string_view{label}.substr(5);
			if (name == "universal") tags.community_universal = true;
			if (auto roles = CommunityRoleMeaning(name); roles.has_value()) tags.community_roles.Set(*roles);
			continue;
		}
		if (label.starts_with("power/")) {
			tags.power.emplace_back(std::string_view{label}.substr(6));
			continue;
		}
		if (!label.starts_with("fl_")) continue;
		const auto parts = SplitLabel(label);
		/* A label without a slash is a badge class, which OpenTTD creates by itself. */
		if (parts.size() < 2) continue;
		tags.any = true;
		const std::string_view kind = parts[0];
		const bool simple = parts.size() == 2 && IsValidName(parts[1]);
		auto bad = [&]() { tags.problems.push_back(fmt::format("cannot understand tag '{}'", label)); };
		if (kind == "fl_format") {
			if (parts.size() == 2 && (parts[1] == "1" || parts[1] == "2")) {
				tags.formatted = true;
			} else {
				tags.problems.push_back(fmt::format("unsupported format '{}'", label));
			}
		} else if (kind == "fl_id" && simple) {
			if (tags.id.has_value()) tags.problems.push_back(fmt::format("an item has one fl_id; '{}' is ignored", label));
			else tags.id = std::string{parts[1]};
		} else if (kind == "fl_tech" && simple) {
			tags.tech = std::string{parts[1]};
		} else if (kind == "fl_tier" && parts.size() == 2 && parts[1].size() == 1 && parts[1][0] >= '0' && parts[1][0] <= '9') {
			tags.tier = static_cast<uint8_t>(parts[1][0] - '0');
		} else if (kind == "fl_role" && parts.size() == 2 && ParseTechRole(parts[1]).has_value()) {
			tags.roles.Set(*ParseTechRole(parts[1]));
		} else if (kind == "fl_needs" && simple) {
			tags.needs.emplace_back(parts[1]);
		} else if (kind == "fl_provides" && simple) {
			tags.provides.emplace_back(parts[1]);
		} else if (kind == "fl_industry" && simple) {
			tags.industry = std::string{parts[1]};
		} else if (kind == "fl_market" && simple) {
			tags.markets.emplace_back(parts[1]);
		} else if (kind == "fl_ai" && parts.size() == 2 && parts[1] == "avoid") {
			tags.avoid = true;
		} else if (kind == "fl_ai" && parts.size() == 2 && parts[1] == "early_adopter") {
			tags.early_adopter = true;
		} else if (kind == "fl_family" || kind == "fl_profile" || kind == "fl_cargo") {
			tags.problems.push_back(fmt::format("'{}' is a declaration, not a tag; define it with FEAT_BADGES instead of attaching it", label));
		} else {
			bad();
		}
	}
	return tags;
}

/**
 * Read the fl_ tags of a loaded item.
 * @param badges The item's badges.
 * @return The tags, with any problems.
 */
ItemTags ReadItemTags(std::span<const BadgeID> badges)
{
	std::vector<std::string> labels;
	for (BadgeID id : badges) {
		if (const Badge *badge = GetBadge(id); badge != nullptr) labels.push_back(badge->label);
	}
	return ParseItemTags(labels);
}

/**
 * Read cargo meanings: fl_cargo/<LABEL>/role/<role>.
 * @param labels Every badge label loaded.
 * @param[in,out] problems Malformed declarations are reported here.
 * @return Roles per cargo label.
 */
std::map<std::string, TechRoles> ParseCargoRoles(std::span<const std::string> labels, std::vector<std::string> &problems)
{
	std::map<std::string, TechRoles> roles;
	for (const std::string &label : labels) {
		if (!label.starts_with("fl_cargo/")) continue;
		const auto parts = SplitLabel(label);
		/* "fl_cargo/LABEL" alone is the parent of the role declarations; OpenTTD may create it. */
		if (parts.size() == 2) continue;
		if (parts.size() == 4 && !parts[1].empty() && parts[1].size() <= 4 && parts[2] == "role" && ParseTechRole(parts[3]).has_value()) {
			roles[std::string{parts[1]}].Set(*ParseTechRole(parts[3]));
		} else {
			problems.push_back(fmt::format("cannot understand cargo declaration '{}'", label));
		}
	}
	return roles;
}

/**
 * Get the roles declared for a cargo.
 * @param cargo Cargo type.
 * @return Declared roles, or none if the cargo has no declaration.
 */
TechRoles TechLibrary::CargoRoles(CargoType cargo) const
{
	auto it = this->cargo_roles.find(cargo);
	return it == this->cargo_roles.end() ? TechRoles{} : it->second;
}
