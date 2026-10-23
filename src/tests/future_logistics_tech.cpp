/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file future_logistics_tech.cpp Test the Future Logistics Tech Library rules that need no loaded game. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../future_logistics/catalogue.h"
#include "../future_logistics/scale.h"
#include "../future_logistics/tech_library.h"

#include "../safeguards.h"

TEST_CASE("FutureLogisticsTech - role inference")
{
	const CargoClasses passengers{CargoClass::Passengers};
	CHECK(InferTechRoles(TechMode::Road, passengers, false, false) == TechRoles{TechRole::PassengerFeeder, TechRole::PassengerCommuter});
	CHECK(InferTechRoles(TechMode::Rail, passengers, false, false) == TechRoles{TechRole::PassengerIntercity, TechRole::PassengerCommuter});
	CHECK(InferTechRoles(TechMode::Air, passengers, false, false) == TechRoles{TechRole::PassengerIntercity});
	CHECK(InferTechRoles(TechMode::Air, passengers, false, true) == TechRoles{TechRole::PassengerCommuter});
	CHECK(InferTechRoles(TechMode::Water, passengers, false, false) == TechRoles{TechRole::PassengerCommuter});

	CHECK(InferTechRoles(TechMode::Road, CargoClasses{CargoClass::Mail}, false, false) == TechRoles{TechRole::Express});
	CHECK(InferTechRoles(TechMode::Rail, CargoClasses{CargoClass::Bulk}, false, false) == TechRoles{TechRole::FreightBulk});
	CHECK(InferTechRoles(TechMode::Rail, CargoClasses{CargoClass::Liquid}, false, false) == TechRoles{TechRole::FreightBulk});
	CHECK(InferTechRoles(TechMode::Road, CargoClasses{CargoClass::Express}, false, false) == TechRoles{TechRole::FreightGeneral});
	CHECK(InferTechRoles(TechMode::Road, CargoClasses{}, false, false) == TechRoles{});

	/* A locomotive can pull anything, so it is offered for every rail job except parcels and feeders. */
	const TechRoles traction = InferTechRoles(TechMode::Rail, CargoClasses{}, true, false);
	CHECK(traction.Test(TechRole::PassengerIntercity));
	CHECK(traction.Test(TechRole::FreightBulk));
	CHECK_FALSE(traction.Test(TechRole::PassengerFeeder));
	CHECK_FALSE(traction.Test(TechRole::Parcel));
}

TEST_CASE("FutureLogisticsTech - scoring")
{
	/* More capacity, speed or reliability is better; higher costs are worse. */
	const double base = ScoreTech(30, 50, 60000, 10000, 1000, 10);
	CHECK(ScoreTech(40, 50, 60000, 10000, 1000, 10) > base);
	CHECK(ScoreTech(30, 60, 60000, 10000, 1000, 10) > base);
	CHECK(ScoreTech(30, 50, 65000, 10000, 1000, 10) > base);
	CHECK(ScoreTech(30, 50, 60000, 20000, 1000, 10) < base);
	CHECK(ScoreTech(30, 50, 60000, 10000, 2000, 10) < base);
	/* A longer life spreads the purchase price thinner. */
	CHECK(ScoreTech(30, 50, 60000, 10000, 1000, 20) > base);
	/* Free vehicles and zero life must not divide by zero. */
	CHECK(ScoreTech(30, 50, 60000, 0, 0, 0) > 0);
}

TEST_CASE("FutureLogisticsTech - names round trip")
{
	for (uint8_t i = 0; i < to_underlying(TechRole::End); i++) {
		const TechRole role = static_cast<TechRole>(i);
		CHECK(ParseTechRole(TechRoleName(role)) == role);
	}
	for (TechMode mode : {TechMode::Road, TechMode::Rail, TechMode::Air, TechMode::Water}) {
		CHECK(ParseTechMode(TechModeName(mode)) == mode);
	}
	CHECK_FALSE(ParseTechRole("rocket").has_value());
	CHECK(FormatTechRoles({}) == "-");
	CHECK(FormatTechRoles({TechRole::PassengerFeeder, TechRole::PassengerCommuter}) == "passenger_commuter,passenger_feeder");
}

TEST_CASE("FutureLogisticsScale - tiers and grants")
{
	using namespace FutureLogistics;
	CHECK(TierForPopulation(0) == MarketTier::Village);
	CHECK(TierForPopulation(499) == MarketTier::Village);
	CHECK(TierForPopulation(500) == MarketTier::Town);
	CHECK(TierForPopulation(1500) == MarketTier::City);
	CHECK(TierForPopulation(1000000) == MarketTier::Metropolis);

	/* Each tier allows at least as much as the one below it. */
	for (uint8_t i = 1; i <= to_underlying(MarketTier::Metropolis); i++) {
		const TierLimits &lower = GetTierLimits(static_cast<MarketTier>(i - 1));
		const TierLimits &upper = GetTierLimits(static_cast<MarketTier>(i));
		CHECK(upper.min_population > lower.min_population);
		CHECK(upper.platform_length >= lower.platform_length);
		CHECK(upper.track_rank >= lower.track_rank);
		CHECK(upper.max_airport_class >= lower.max_airport_class);
		CHECK((!lower.rail_passenger || upper.rail_passenger));
		CHECK((!lower.air_passenger || upper.air_passenger));
	}
	const TierLimits &village = GetTierLimits(MarketTier::Village);
	CHECK_FALSE(village.rail_passenger);
	CHECK_FALSE(village.air_passenger);

	/* Grants follow population, are shared between rivals and never exceed the cap. */
	CHECK(RivalGrant(2000, 4, 500, 25000000) == 250000);
	CHECK(RivalGrant(1000000, 4, 500, 25000000) == 25000000);
	CHECK(RivalGrant(2000, 0, 500, 25000000) == 0);
	CHECK(RivalGrant(0, 4, 500, 25000000) == 0);
}

TEST_CASE("FutureLogisticsScale - fleets and rounds")
{
	using namespace FutureLogistics;
	CHECK(GetTierLimits(MarketTier::Village).fleet == 1);
	for (uint8_t i = 1; i <= to_underlying(MarketTier::Metropolis); i++) {
		CHECK(GetTierLimits(static_cast<MarketTier>(i)).fleet >= GetTierLimits(static_cast<MarketTier>(i - 1)).fleet);
	}
	CHECK(ServiceRounds(0) == 1);
	CHECK(ServiceRounds(49999) == 1);
	CHECK(ServiceRounds(50000) == 2);
	CHECK(ServiceRounds(10000000) == 3);
}

TEST_CASE("FutureLogisticsFormat - item tags")
{
	const std::vector<std::string> labels{"fl_format/2", "fl_tech/rocket", "fl_tier/3", "fl_role/express", "fl_role/parcel",
		"fl_needs/launchpad", "fl_provides/launchpad", "fl_ai/avoid", "fl_market/tech", "fl_industry/data_centre", "other/label", "fl_tech"};
	const ItemTags tags = ParseItemTags(labels);
	CHECK(tags.formatted);
	CHECK(tags.tech == "rocket");
	CHECK(tags.tier == 3);
	CHECK(tags.roles == TechRoles{TechRole::Express, TechRole::Parcel});
	CHECK(tags.needs == std::vector<std::string>{"launchpad"});
	CHECK(tags.provides == std::vector<std::string>{"launchpad"});
	CHECK(tags.avoid);
	CHECK(tags.markets == std::vector<std::string>{"tech"});
	CHECK(tags.industry == "data_centre");
	CHECK(tags.problems.empty());

	const std::vector<std::string> bad{"fl_format/9", "fl_tier/high", "fl_role/teleport", "fl_family/rocket", "fl_tech/Bad Name"};
	const ItemTags wrong = ParseItemTags(bad);
	CHECK_FALSE(wrong.formatted);
	CHECK(wrong.problems.size() == 5);

	std::vector<std::string> problems;
	const auto roles = ParseCargoRoles(std::vector<std::string>{"fl_cargo/DATA/role/express", "fl_cargo/DATA/role/parcel", "fl_cargo/DATA", "fl_cargo/DATA/colour/red"}, problems);
	CHECK(roles.at("DATA") == TechRoles{TechRole::Express, TechRole::Parcel});
	CHECK(problems.size() == 1);
}

TEST_CASE("FutureLogisticsFormat - shared badge classes")
{
	/* role/ and power/ are read without fl_format, as other NewGRFs use them. */
	const std::vector<std::string> labels{"role/freight", "role/express", "role/lolz", "power/electric", "livery/uk/british_rail/blue", "_ih_private"};
	const ItemTags tags = ParseItemTags(labels);
	CHECK_FALSE(tags.any);
	CHECK(tags.community_role);
	CHECK_FALSE(tags.community_universal);
	CHECK(tags.community_roles == TechRoles{TechRole::FreightBulk, TechRole::FreightGeneral, TechRole::PassengerIntercity, TechRole::Express});
	CHECK(tags.power == std::vector<std::string>{"electric"});
	CHECK(tags.problems.empty());

	CHECK(ParseItemTags(std::vector<std::string>{"role/universal"}).community_universal);
	CHECK_FALSE(CommunityRoleMeaning("universal").has_value());
	CHECK(CommunityRoleMeaning("metro") == TechRoles{TechRole::PassengerCommuter});
}

TEST_CASE("FutureLogisticsFormat - built-in rivals use the declaration format")
{
	using namespace FutureLogistics;
	const Catalogue catalogue = Catalogue::Parse(Catalogue::BuiltinDeclarations());
	CHECK(catalogue.Problems().empty());
	REQUIRE(catalogue.Profiles().size() == 4);
	CHECK(catalogue.Profiles()[0].key == "national_rail");
	CHECK(catalogue.Profiles()[0].name == "National Rail & Freight");
	CHECK(catalogue.Profiles()[0].id == 1);
	CHECK(catalogue.Profiles()[3].id == 4);
	CHECK(catalogue.Profiles()[1].rules.size() == 3);
	REQUIRE(catalogue.Families().size() == 1);
	CHECK(catalogue.Families()[0].key == "conventional");
	/* Every market of every registered mechanic. */
	size_t markets = 0;
	for (const MechanicInfo &mechanic : GetMechanics()) markets += mechanic.markets.size();
	CHECK(catalogue.Families()[0].recipes.size() == markets);
}

TEST_CASE("FutureLogisticsFormat - declarations and plans")
{
	using namespace FutureLogistics;
	std::vector<BadgeInfo> badges(Catalogue::BuiltinDeclarations().begin(), Catalogue::BuiltinDeclarations().end());
	for (const char *label : {"fl_family/rocket/mechanic/airport_pair", "fl_family/rocket/market/town_pair", "fl_family/rocket/min_tier/city",
			"fl_profile/aerolink/adopt/rocket/any/any/2", "fl_profile/spacecorp/adopt/any/airport_pair/any/0",
			"fl_profile/spacecorp/industries/tech", "fl_family/rocket/mechanic/rail_line", "fl_family/conventional/market/feeder",
			"fl_profile/spacecorp/adopt/rocket/any/any/many", "fl_profile/spacecorp/colour/red", "fl_something/else"}) {
		badges.push_back({label, ""});
	}
	badges.push_back({"fl_family/rocket", "Rocket freight"});
	badges.push_back({"fl_profile/spacecorp", "SpaceCorp"});
	const Catalogue catalogue = Catalogue::Parse(badges);

	const Family *rocket = catalogue.GetFamily("rocket");
	REQUIRE(rocket != nullptr);
	CHECK(rocket->name == "Rocket freight");
	CHECK(rocket->mechanic == "airport_pair");
	CHECK(rocket->min_tier == MarketTier::City);
	CHECK(rocket->recipes.size() == 1);

	REQUIRE(catalogue.Profiles().size() == 5);
	const Profile &spacecorp = catalogue.Profiles()[4];
	CHECK(spacecorp.id == 5);
	CHECK(spacecorp.name == "SpaceCorp");
	CHECK(spacecorp.industry_markets == std::vector<std::string>{"tech"});
	/* Second mechanic, changing conventional, a bad number, an unknown profile part and an unknown class. */
	CHECK(catalogue.Problems().size() == 5);

	const TimerGameCalendar::Date launch = TimerGameCalendar::ConvertYMDToDate(TimerGameCalendar::Year{2050}, 0, 1);
	auto available = [&](const std::string &family) -> std::optional<TimerGameCalendar::Date> {
		if (family == "rocket") return launch;
		return std::nullopt;
	};
	const Profile &aerolink = catalogue.Profiles()[1];
	/* Before the rocket's delay has passed, AeroLink only flies conventional aircraft. */
	auto plan = catalogue.Plan(aerolink, launch + CalendarTime::DAYS_IN_YEAR, available);
	REQUIRE(plan.size() == 2);
	CHECK(plan[0].Key() == "conventional/airport_pair/town_pair");
	CHECK(plan[1].Key() == "conventional/airport_pair/mail");
	/* Rules apply in order: AeroLink's own "any air family after 2 years" rule comes before its mail rule. */
	plan = catalogue.Plan(aerolink, launch + CalendarTime::DAYS_IN_YEAR * 3, available);
	REQUIRE(plan.size() == 3);
	CHECK(plan[1].Key() == "rocket/airport_pair/town_pair");
	CHECK(plan[1].min_tier == MarketTier::City);
	CHECK_FALSE(plan[1].early_adopters);
	CHECK(plan[2].Key() == "conventional/airport_pair/mail");

	/* "any" never matches the conventional family, and nothing is available before launch. */
	CHECK(catalogue.Plan(spacecorp, launch - 1, available).empty());
	plan = catalogue.Plan(spacecorp, launch, available);
	REQUIRE(plan.size() == 1);
	CHECK(plan[0].family == "rocket");
	CHECK(plan[0].early_adopters);
}

TEST_CASE("FutureLogisticsFormat - mechanics and ids")
{
	using namespace FutureLogistics;
	CHECK(FindMechanic("dock_pair") != nullptr);
	CHECK(FindMechanic("launch_site") == nullptr);
	CHECK(DefaultMechanic(TechMode::Water)->name == "dock_pair");
	CHECK(DefaultMechanic(TechMode::Rail)->name == "rail_line");
	/* Every mechanic that carries freight can be one leg of a route, so routes can hand cargo between any two of them. */
	for (const MechanicInfo &mechanic : GetMechanics()) {
		CHECK((mechanic.leg != nullptr) == IsSupported(mechanic.name, Market::Industry));
		if (mechanic.leg != nullptr) CHECK(mechanic.min_distance < mechanic.max_distance);
	}
	CHECK(IsSupported("airport_pair", Market::Industry));
	CHECK(IsSupported("road_route", Market::Mail));
	CHECK_FALSE(IsSupported("airport_pair", Market::Feeder));
	CHECK_FALSE(IsSupported("launch_site", Market::TownPair));
	CHECK(ParseMarket("mail") == Market::Mail);

	const ItemTags tagged = ParseItemTags(std::vector<std::string>{"fl_format/2", "fl_id/launchpad_small", "fl_id/second"});
	CHECK(tagged.id == "launchpad_small");
	CHECK(tagged.problems.size() == 1);
}
