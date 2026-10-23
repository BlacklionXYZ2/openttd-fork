/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file future_logistics_tech.cpp Test the Future Logistics Tech Library rules that need no loaded game. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

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
