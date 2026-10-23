/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file catalogue.cpp Read tech families, recipes and rival profiles from fl_ badge declarations. */
#include "../stdafx.h"
#include "catalogue.h"
#include "../industry.h"
#include "../industrytype.h"
#include "../newgrf_badge.h"
#include "../strings_func.h"

#include "../safeguards.h"

namespace FutureLogistics {
namespace {
constexpr std::array<std::string_view, 4> TIER_LABELS{"village", "town", "city", "metropolis"};

/**
 * The recipes of the built-in conventional family: every market of every registered mechanic.
 * @return Mechanic and market pairs in registry order.
 */
std::vector<std::pair<std::string, Market>> ConventionalRecipes()
{
	std::vector<std::pair<std::string, Market>> recipes;
	for (const MechanicInfo &mechanic : GetMechanics()) {
		for (Market market : mechanic.markets) recipes.emplace_back(std::string{mechanic.name}, market);
	}
	return recipes;
}

/**
 * The four built-in rivals, written in the same declaration format a NewGRF uses.
 * See docs/future_logistics/tech_format.md.
 */
const std::vector<BadgeInfo> BUILTIN_DECLARATIONS{
	{"fl_profile/national_rail", "National Rail & Freight"},
	{"fl_profile/national_rail/adopt/conventional/rail_line/industry/0", ""},
	{"fl_profile/national_rail/adopt/conventional/rail_line/town_pair/0", ""},
	{"fl_profile/national_rail/adopt/any/rail_line/any/5", ""},
	{"fl_profile/national_rail/adopt/conventional/rail_line/mail/0", ""},

	{"fl_profile/aerolink", "AeroLink"},
	{"fl_profile/aerolink/adopt/conventional/airport_pair/town_pair/0", ""},
	{"fl_profile/aerolink/adopt/any/airport_pair/any/2", ""},
	{"fl_profile/aerolink/adopt/conventional/airport_pair/mail/0", ""},

	{"fl_profile/metro_regional", "Metro & Regional"},
	{"fl_profile/metro_regional/adopt/conventional/rail_line/town_pair/0", ""},
	{"fl_profile/metro_regional/adopt/conventional/road_route/feeder/0", ""},
	{"fl_profile/metro_regional/adopt/any/road_route/town_pair/3", ""},
	{"fl_profile/metro_regional/adopt/conventional/road_route/mail/0", ""},
	{"fl_profile/metro_regional/adopt/conventional/dock_pair/town_pair/0", ""},

	{"fl_profile/integrated_logistics", "Integrated Logistics"},
	{"fl_profile/integrated_logistics/adopt/conventional/rail_line/industry/0", ""},
	{"fl_profile/integrated_logistics/adopt/conventional/road_route/industry/0", ""},
	{"fl_profile/integrated_logistics/adopt/any/road_route/industry/2", ""},
	{"fl_profile/integrated_logistics/adopt/any/rail_line/industry/5", ""},
	{"fl_profile/integrated_logistics/adopt/conventional/dock_pair/industry/0", ""},
	{"fl_profile/integrated_logistics/adopt/conventional/airport_pair/industry/0", ""},
};

/**
 * Check a name used inside a label: lower-case letters, digits and underscores.
 * @param name Name to check.
 * @return Whether it is valid.
 */
bool IsValidName(std::string_view name)
{
	return !name.empty() && std::ranges::all_of(name, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

/**
 * Parse a number of years.
 * @param text Digits.
 * @return Years from 0 to 100, or empty if invalid.
 */
std::optional<int> ParseYears(std::string_view text)
{
	if (text.empty() || text.size() > 3 || !std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; })) return std::nullopt;
	int years = 0;
	for (char c : text) years = years * 10 + (c - '0');
	if (years > 100) return std::nullopt;
	return years;
}

/** Tag classes, which the catalogue leaves to the Tech Library. */
constexpr std::array<std::string_view, 10> TAG_CLASSES{"fl_format", "fl_id", "fl_tech", "fl_tier", "fl_role", "fl_needs", "fl_provides", "fl_ai", "fl_industry", "fl_market"};
} // namespace

std::optional<MarketTier> ParseTier(std::string_view name)
{
	auto it = std::ranges::find(TIER_LABELS, name);
	if (it == TIER_LABELS.end()) return std::nullopt;
	return static_cast<MarketTier>(std::distance(TIER_LABELS.begin(), it));
}

/**
 * Get a recipe's name for logs and the console.
 * @return Name such as "rocket/airport_pair/town_pair".
 */
std::string Recipe::Key() const
{
	return fmt::format("{}/{}/{}", this->family, this->mechanic, MarketName(this->market));
}

/**
 * The built-in declarations, for display and tests.
 * @return The four built-in rivals.
 */
/* static */ std::span<const BadgeInfo> Catalogue::BuiltinDeclarations()
{
	return BUILTIN_DECLARATIONS;
}

/**
 * Find or create a family.
 * @param key Family name.
 * @return The family.
 */
Family &Catalogue::FamilyFor(std::string_view key)
{
	auto it = std::ranges::find(this->families, key, &Family::key);
	if (it != this->families.end()) return *it;
	Family &family = this->families.emplace_back();
	family.key = std::string{key};
	family.name = family.key;
	return family;
}

/**
 * Read declarations. Families are not complete until Resolve() has seen the loaded vehicles.
 * @param badges Declarations in order; the built-in ones should come first.
 * @return Catalogue with any problems recorded.
 */
/* static */ Catalogue Catalogue::Parse(std::span<const BadgeInfo> badges)
{
	Catalogue catalogue;
	Family &conventional = catalogue.FamilyFor(CONVENTIONAL);
	conventional.declared = true;
	conventional.name = "Conventional";
	conventional.min_tier = MarketTier::Village;
	conventional.recipes = ConventionalRecipes();

	auto profile_for = [&](std::string_view key) -> Profile & {
		auto it = std::ranges::find(catalogue.profiles, key, &Profile::key);
		if (it != catalogue.profiles.end()) return *it;
		Profile &profile = catalogue.profiles.emplace_back();
		profile.id = static_cast<uint8_t>(catalogue.profiles.size());
		profile.key = std::string{key};
		profile.name = profile.key;
		return profile;
	};

	for (const BadgeInfo &badge : badges) {
		if (!badge.label.starts_with("fl_")) continue;
		const auto parts = SplitLabel(badge.label);
		if (parts.size() < 2 || std::ranges::find(TAG_CLASSES, parts[0]) != TAG_CLASSES.end() || parts[0] == "fl_cargo") continue;
		auto bad = [&](std::string_view why) { catalogue.problems.push_back(fmt::format("'{}': {}", badge.label, why)); };

		if (parts[0] == "fl_family") {
			if (!IsValidName(parts[1])) { bad("invalid family name"); continue; }
			if (parts[1] == CONVENTIONAL) { bad("the conventional family is built in and cannot be changed"); continue; }
			Family &family = catalogue.FamilyFor(parts[1]);
			if (parts.size() == 2) {
				family.declared = true;
				if (!badge.name.empty()) family.name = badge.name;
			} else if (parts.size() == 4 && parts[2] == "mechanic") {
				if (!IsValidName(parts[3])) { bad("invalid mechanic name"); continue; }
				if (FindMechanic(parts[3]) == nullptr) { bad("this build of the game has no such mechanic"); continue; }
				if (family.mechanic.has_value() && *family.mechanic != parts[3]) { bad("a family has one mechanic; this one is ignored"); continue; }
				family.mechanic = std::string{parts[3]};
			} else if (parts.size() == 4 && parts[2] == "market") {
				const auto market = ParseMarket(parts[3]);
				if (!market.has_value()) { bad("unknown market"); continue; }
				if (std::ranges::find(family.markets, *market) == family.markets.end()) family.markets.push_back(*market);
			} else if (parts.size() == 4 && parts[2] == "min_tier") {
				const auto tier = ParseTier(parts[3]);
				if (!tier.has_value()) { bad("unknown tier"); continue; }
				family.min_tier = *tier;
			} else {
				bad("unknown family declaration");
			}
		} else if (parts[0] == "fl_profile") {
			if (!IsValidName(parts[1])) { bad("invalid profile name"); continue; }
			if (parts.size() == 2) {
				Profile &profile = profile_for(parts[1]);
				if (!badge.name.empty()) profile.name = badge.name;
			} else if (parts.size() == 7 && parts[2] == "adopt") {
				AdoptRule rule;
				if (parts[3] != "any") {
					if (!IsValidName(parts[3])) { bad("invalid family name"); continue; }
					rule.family = std::string{parts[3]};
				}
				if (parts[4] != "any") {
					if (FindMechanic(parts[4]) == nullptr) { bad("this build of the game has no such mechanic"); continue; }
					rule.mechanic = std::string{parts[4]};
				}
				if (parts[5] != "any") {
					rule.market = ParseMarket(parts[5]);
					if (!rule.market.has_value()) { bad("unknown market"); continue; }
				}
				const auto years = ParseYears(parts[6]);
				if (!years.has_value()) { bad("years must be a whole number from 0 to 100"); continue; }
				rule.years = *years;
				profile_for(parts[1]).rules.push_back(std::move(rule));
			} else if (parts.size() == 4 && parts[2] == "industries" && IsValidName(parts[3])) {
				profile_for(parts[1]).industry_markets.emplace_back(parts[3]);
			} else {
				bad("unknown profile declaration");
			}
		} else {
			bad("unknown fl_ declaration");
		}
	}
	/* Families that declare both their mechanic and markets are complete without seeing any vehicles. */
	for (Family &family : catalogue.families) {
		if (family.key == CONVENTIONAL || !family.mechanic.has_value()) continue;
		for (Market market : family.markets) {
			if (IsSupported(*family.mechanic, market)) family.recipes.emplace_back(*family.mechanic, market);
		}
	}
	return catalogue;
}

/**
 * Complete families from the loaded vehicles and check everything fits together.
 * @param library Loaded transport technology.
 */
void Catalogue::Resolve(const TechLibrary &library)
{
	std::set<std::string> undeclared;
	for (const TechRecord &record : library.Vehicles()) {
		if (record.tech == CONVENTIONAL) continue;
		const Family &family = this->FamilyFor(record.tech);
		if (!family.declared && undeclared.insert(record.tech).second) {
			this->problems.push_back(fmt::format("family '{}' is used by vehicles but not declared with fl_family/{}; defaults apply", record.tech, record.tech));
		}
	}

	for (Family &family : this->families) {
		if (family.key == CONVENTIONAL) continue;
		std::set<TechMode> modes;
		TechRoles roles{};
		for (const TechRecord &record : library.Vehicles()) {
			if (record.tech != family.key || record.wagon) continue;
			modes.insert(record.mode);
			roles.Set(record.roles);
		}
		if (modes.empty()) {
			this->problems.push_back(fmt::format("family '{}' has no vehicles in the loaded NewGRFs", family.key));
			continue;
		}
		if (!family.mechanic.has_value()) {
			if (modes.size() > 1) {
				this->problems.push_back(fmt::format("family '{}' has vehicles of several modes; declare fl_family/{}/mechanic/<mechanic>", family.key, family.key));
				continue;
			}
			const MechanicInfo *mechanic = DefaultMechanic(*modes.begin());
			if (mechanic == nullptr) {
				this->problems.push_back(fmt::format("family '{}' has vehicles of a mode no mechanic builds yet", family.key));
				continue;
			}
			family.mechanic = std::string{mechanic->name};
		}
		if (family.markets.empty()) {
			if (roles.Any({TechRole::PassengerIntercity, TechRole::PassengerCommuter, TechRole::PassengerFeeder})) family.markets.push_back(Market::TownPair);
			if (roles.Any({TechRole::Express, TechRole::Parcel})) family.markets.push_back(Market::Mail);
			if (roles.Any({TechRole::FreightBulk, TechRole::FreightGeneral})) family.markets.push_back(Market::Industry);
		}
		family.recipes.clear();
		for (Market market : family.markets) {
			if (IsSupported(*family.mechanic, market)) {
				family.recipes.emplace_back(*family.mechanic, market);
			} else {
				this->problems.push_back(fmt::format("family '{}': {} cannot serve the {} market yet", family.key, *family.mechanic, MarketName(market)));
			}
		}
	}

	/* Every site a vehicle needs should be provided by some track type or airport. */
	std::set<std::string> provided;
	for (const InfraRecord &infra : library.Infrastructure()) provided.insert(infra.provides.begin(), infra.provides.end());
	std::set<std::string> reported;
	for (const TechRecord &record : library.Vehicles()) {
		for (const std::string &need : record.needs) {
			if (!provided.contains(need) && reported.insert(need).second) {
				this->problems.push_back(fmt::format("vehicles need site '{}' but no loaded track type or airport has fl_provides/{}", need, need));
			}
		}
	}

	for (const Profile &profile : this->profiles) {
		if (profile.rules.empty()) this->problems.push_back(fmt::format("profile '{}' has no adopt rules, so it will only get the fallback bus", profile.key));
		for (const AdoptRule &rule : profile.rules) {
			if (rule.family.has_value() && this->GetFamily(*rule.family) == nullptr) {
				this->problems.push_back(fmt::format("profile '{}' adopts unknown family '{}'", profile.key, *rule.family));
			}
		}
	}
}

/**
 * Read the built-in declarations and every loaded badge, then resolve them against the library.
 * @param library Loaded transport technology.
 * @return Complete catalogue.
 */
/* static */ Catalogue Catalogue::Build(const TechLibrary &library)
{
	std::vector<BadgeInfo> badges(BUILTIN_DECLARATIONS.begin(), BUILTIN_DECLARATIONS.end());
	for (const Badge &badge : GetBadges()) {
		badges.push_back({badge.label, badge.name == StringID{} ? std::string{} : GetString(badge.name)});
	}
	Catalogue catalogue = Parse(badges);
	for (Profile &profile : catalogue.profiles) {
		const std::string label = "fl_profile/" + profile.key;
		profile.builtin = std::ranges::any_of(BUILTIN_DECLARATIONS, [&](const BadgeInfo &badge) { return badge.label == label; });
	}
	catalogue.Resolve(library);
	return catalogue;
}

/**
 * Find a family.
 * @param key Family name.
 * @return The family, or nullptr.
 */
const Family *Catalogue::GetFamily(std::string_view key) const
{
	auto it = std::ranges::find(this->families, key, &Family::key);
	return it == this->families.end() ? nullptr : &*it;
}

/**
 * Work out which recipes a rival tries today, in order.
 * @param profile The rival.
 * @param today Current date.
 * @param available_since When each family first had a vehicle, or empty if never.
 * @return Recipes in rule order, without duplicates.
 */
std::vector<Recipe> Catalogue::Plan(const Profile &profile, TimerGameCalendar::Date today,
	const std::function<std::optional<TimerGameCalendar::Date>(const std::string &family)> &available_since) const
{
	std::vector<Recipe> plan;
	for (const AdoptRule &rule : profile.rules) {
		for (const Family &family : this->families) {
			/* "any" means any NewGRF family; the conventional family has to be named. */
			if (rule.family.has_value() ? *rule.family != family.key : family.key == CONVENTIONAL) continue;
			if (family.key != CONVENTIONAL) {
				const auto since = available_since(family.key);
				if (!since.has_value() || *since + CalendarTime::DAYS_IN_YEAR * rule.years > today) continue;
			}
			for (const auto &[mechanic, market] : family.recipes) {
				if (rule.mechanic.has_value() && *rule.mechanic != mechanic) continue;
				if (rule.market.has_value() && *rule.market != market) continue;
				Recipe recipe{family.key, mechanic, market, family.min_tier, rule.years == 0};
				if (std::ranges::find(plan, recipe) == plan.end()) plan.push_back(std::move(recipe));
			}
		}
	}
	return plan;
}

/**
 * Work out which recipes a rival tries today, using the loaded vehicles' introduction dates.
 * @param profile The rival.
 * @param library Loaded transport technology.
 * @param today Current date.
 * @return Recipes in rule order, without duplicates.
 */
std::vector<Recipe> Catalogue::Plan(const Profile &profile, const TechLibrary &library, TimerGameCalendar::Date today) const
{
	return this->Plan(profile, today, [&](const std::string &family) { return FamilyAvailableSince(library, family); });
}

/**
 * Find when a family first had a vehicle.
 * @param library Loaded transport technology.
 * @param family Family name.
 * @return Earliest introduction date of its usable vehicles, or empty if it has none.
 */
std::optional<TimerGameCalendar::Date> FamilyAvailableSince(const TechLibrary &library, std::string_view family)
{
	std::optional<TimerGameCalendar::Date> since;
	for (const TechRecord &record : library.Vehicles()) {
		if (record.tech != family || record.wagon || record.ai_avoid) continue;
		if (!since.has_value() || record.intro_date < *since) since = record.intro_date;
	}
	return since;
}

/**
 * Get an industry's market groups.
 * @param industry Industry.
 * @return Its fl_market tags.
 */
std::vector<std::string> IndustryMarkets(const Industry *industry)
{
	const ItemTags tags = ReadItemTags(GetIndustrySpec(industry->type)->badges);
	return tags.formatted ? tags.markets : std::vector<std::string>{};
}

/**
 * Rank an industry by a rival's preferred markets.
 * @param industry Industry.
 * @param preferred Preferred fl_market groups, most preferred first.
 * @return Position of its best market in the list, or the list size if it has none of them.
 */
size_t IndustryPreference(const Industry *industry, std::span<const std::string> preferred)
{
	if (preferred.empty()) return 0;
	size_t best = preferred.size();
	for (const std::string &market : IndustryMarkets(industry)) {
		auto it = std::ranges::find(preferred, market);
		if (it != preferred.end()) best = std::min<size_t>(best, std::distance(preferred.begin(), it));
	}
	return best;
}

/**
 * Choose the role a freight vehicle needs for a cargo.
 * @param library Loaded transport technology, with any fl_cargo declarations.
 * @param cargo Cargo to carry.
 * @return The cargo's first declared role, or bulk or general freight from its class.
 */
TechRole FreightRole(const TechLibrary &library, CargoType cargo)
{
	const TechRoles declared = library.CargoRoles(cargo);
	for (uint8_t i = 0; i < to_underlying(TechRole::End); i++) {
		if (declared.Test(static_cast<TechRole>(i))) return static_cast<TechRole>(i);
	}
	return CargoSpec::Get(cargo)->classes.Any({CargoClass::Bulk, CargoClass::Liquid}) ? TechRole::FreightBulk : TechRole::FreightGeneral;
}

/**
 * Choose the role a vehicle needs to carry a town cargo.
 * @param library Loaded transport technology, with any fl_cargo declarations.
 * @param cargo Passengers or mail.
 * @param passenger_role Role for passengers, which depends on the mechanic.
 * @return The cargo's first declared role, else the passenger role for passengers and express for anything else.
 */
TechRole TownRole(const TechLibrary &library, CargoType cargo, TechRole passenger_role)
{
	const TechRoles declared = library.CargoRoles(cargo);
	for (uint8_t i = 0; i < to_underlying(TechRole::End); i++) {
		if (declared.Test(static_cast<TechRole>(i))) return static_cast<TechRole>(i);
	}
	return CargoSpec::Get(cargo)->classes.Test(CargoClass::Passengers) ? passenger_role : TechRole::Express;
}

} // namespace FutureLogistics
