/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file catalogue.h Tech families, recipes and rival profiles, read from fl_ badge declarations. See docs/future_logistics/tech_format.md. */
#ifndef FUTURE_LOGISTICS_CATALOGUE_H
#define FUTURE_LOGISTICS_CATALOGUE_H

#include "scale.h"
#include "tech_library.h"
#include "../timer/timer_game_calendar.h"
#include <functional>

namespace FutureLogistics {

/** A way of building a service. Only mechanics are code. */
enum class Mechanic : uint8_t {
	RailLine,
	RoadRoute,
	AirportPair,
	End,
};

/** What a service connects. */
enum class Market : uint8_t {
	TownPair,
	Industry,
	Feeder,
	End,
};

/** The family that covers everything without an fl_tech tag. */
inline constexpr std::string_view CONVENTIONAL = "conventional";

/** One family, one mechanic, one market. */
struct Recipe {
	std::string family;
	Mechanic mechanic = Mechanic::RoadRoute;
	Market market = Market::TownPair;
	MarketTier min_tier = MarketTier::Village; ///< Smallest market this recipe may serve.
	bool early_adopters = true; ///< Whether fl_ai/early_adopter vehicles may be used.

	std::string Key() const;
	bool operator==(const Recipe &other) const { return this->family == other.family && this->mechanic == other.mechanic && this->market == other.market; }
};

/** A group of vehicles and infrastructure that belong together. */
struct Family {
	std::string key;
	std::string name; ///< Display name, from the declaration badge.
	bool declared = false; ///< Declared with fl_family, not just used in an fl_tech tag.
	std::optional<Mechanic> mechanic; ///< Declared mechanic; filled from the vehicles if absent.
	std::vector<Market> markets; ///< Declared markets; filled from the vehicles' roles if absent.
	MarketTier min_tier = MarketTier::Town;
	std::vector<std::pair<Mechanic, Market>> recipes; ///< Resolved mechanic and market combinations.
};

/** One rule in a profile: adopt matching recipes some years after their family becomes available. */
struct AdoptRule {
	std::optional<std::string> family; ///< Family name, or empty for any family except conventional.
	std::optional<Mechanic> mechanic; ///< Mechanic, or empty for any.
	std::optional<Market> market; ///< Market, or empty for any.
	int years = 0; ///< Delay after the family becomes available.
};

/** A rival company's character. */
struct Profile {
	uint8_t id = 0; ///< 1-based; stored in the CorporateAI "profile" setting.
	std::string key;
	std::string name; ///< Company name.
	bool builtin = false;
	std::vector<AdoptRule> rules; ///< In declaration order.
	std::vector<std::string> industry_markets; ///< Preferred fl_market groups for freight, in order.
};

/** A badge as the parser sees it. */
struct BadgeInfo {
	std::string label;
	std::string name; ///< Display name, empty if none.
};

/**
 * Families, profiles and cargo meanings. Derived data: build it after the NewGRFs have loaded, never save it.
 * Parsing is separate from resolving, so the label format can be tested without a loaded game.
 */
class Catalogue {
public:
	static Catalogue Parse(std::span<const BadgeInfo> badges);
	static Catalogue Build(const TechLibrary &library);
	static std::span<const BadgeInfo> BuiltinDeclarations();

	void Resolve(const TechLibrary &library);
	std::vector<Recipe> Plan(const Profile &profile, TimerGameCalendar::Date today,
		const std::function<std::optional<TimerGameCalendar::Date>(const std::string &family)> &available_since) const;
	std::vector<Recipe> Plan(const Profile &profile, const TechLibrary &library, TimerGameCalendar::Date today) const;

	const Family *GetFamily(std::string_view key) const;
	const std::vector<Family> &Families() const { return this->families; }
	const std::vector<Profile> &Profiles() const { return this->profiles; }
	const std::vector<std::string> &Problems() const { return this->problems; }

private:
	Family &FamilyFor(std::string_view key);

	std::vector<Family> families; ///< Conventional first, then in declaration order.
	std::vector<Profile> profiles; ///< Built-in first, then in declaration order.
	std::vector<std::string> problems; ///< Human-readable issues for future_logistics_tech validate.
};

std::string_view MechanicName(Mechanic mechanic);
std::string_view MarketName(Market market);
std::optional<Mechanic> ParseMechanic(std::string_view name);
std::optional<Market> ParseMarket(std::string_view name);
std::optional<MarketTier> ParseTier(std::string_view name);
bool IsSupported(Mechanic mechanic, Market market);
std::optional<TimerGameCalendar::Date> FamilyAvailableSince(const TechLibrary &library, std::string_view family);
std::vector<std::string> IndustryMarkets(const struct Industry *industry);
size_t IndustryPreference(const struct Industry *industry, std::span<const std::string> preferred);
TechRole FreightRole(const TechLibrary &library, CargoType cargo);

} // namespace FutureLogistics

#endif /* FUTURE_LOGISTICS_CATALOGUE_H */
