/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file mechanics.cpp The mechanic registry. To add a mechanic: write its planner, then add a row to MECHANICS. */
#include "../stdafx.h"
#include "mechanics.h"
#include "air_planner.h"
#include "freight_planner.h"
#include "rail_planner.h"
#include "road_planner.h"
#include "water_planner.h"
#include "../cargotype.h"

#include "../safeguards.h"

namespace FutureLogistics {
namespace {
constexpr std::array<std::string_view, to_underlying(Market::End)> MARKET_NAMES{"town_pair", "mail", "industry", "feeder"};

/**
 * Get the cargo a town market carries.
 * @param market Town pair or mail.
 * @return Passengers or mail, or INVALID_CARGO if the climate has none.
 */
CargoType TownCargo(Market market)
{
	return GetCargoTypeByLabel(market == Market::Mail ? CT_MAIL : CT_PASSENGERS);
}

/* Each build function dispatches a market to its planner. A market not listed for the mechanic never reaches it. */

ConstructionResult BuildRailLine(const TechLibrary &library, const ServiceSpec &spec, Market market, Reservations &reserved)
{
	if (market == Market::Industry) return BuildFreight(library, spec, "rail_line", reserved);
	return BuildTownRail(library, spec, TownCargo(market), market == Market::Mail ? reserved.mail_towns : reserved.towns);
}

ConstructionResult BuildRoadRoute(const TechLibrary &library, const ServiceSpec &spec, Market market, Reservations &reserved)
{
	switch (market) {
		case Market::Industry: return BuildFreight(library, spec, "road_route", reserved);
		case Market::Feeder: return BuildFeederBus(library, spec, reserved.towns);
		default: return BuildTownRoad(library, spec, TownCargo(market), market == Market::Mail ? reserved.mail_towns : reserved.towns);
	}
}

ConstructionResult BuildAirportPair(const TechLibrary &library, const ServiceSpec &spec, Market market, Reservations &reserved)
{
	if (market == Market::Industry) return BuildFreight(library, spec, "airport_pair", reserved);
	return BuildTownAirline(library, spec, TownCargo(market), market == Market::Mail ? reserved.mail_towns : reserved.towns);
}

ConstructionResult BuildDockPair(const TechLibrary &library, const ServiceSpec &spec, Market market, Reservations &reserved)
{
	if (market == Market::Industry) return BuildFreight(library, spec, "dock_pair", reserved);
	return BuildTownFerry(library, spec, TownCargo(market), market == Market::Mail ? reserved.mail_towns : reserved.towns);
}

/**
 * Every mechanic this build of the game knows. The first one listed for a mode is that mode's default.
 * A mechanic with a leg builder carries freight: alone, or handing cargo over to another such mechanic.
 */
const std::vector<MechanicInfo> MECHANICS{
	{"rail_line", TechMode::Rail, {Market::TownPair, Market::Mail, Market::Industry}, &BuildRailLine, &RailLeg, 20, 100},
	{"road_route", TechMode::Road, {Market::TownPair, Market::Mail, Market::Industry, Market::Feeder}, &BuildRoadRoute, &RoadLeg, 12, 96},
	{"airport_pair", TechMode::Air, {Market::TownPair, Market::Mail, Market::Industry}, &BuildAirportPair, &AirLeg, 48, 256},
	{"dock_pair", TechMode::Water, {Market::TownPair, Market::Mail, Market::Industry}, &BuildDockPair, &ShipLeg, 16, 160},
};
} // namespace

std::string_view MarketName(Market market)
{
	return MARKET_NAMES[to_underlying(market)];
}

std::optional<Market> ParseMarket(std::string_view name)
{
	auto it = std::ranges::find(MARKET_NAMES, name);
	if (it == MARKET_NAMES.end()) return std::nullopt;
	return static_cast<Market>(std::distance(MARKET_NAMES.begin(), it));
}

/**
 * Get every registered mechanic.
 * @return Mechanics in registry order.
 */
std::span<const MechanicInfo> GetMechanics()
{
	return MECHANICS;
}

/**
 * Find a mechanic by the name NewGRFs use.
 * @param name Name such as "dock_pair".
 * @return The mechanic, or nullptr if this build of the game does not have it.
 */
const MechanicInfo *FindMechanic(std::string_view name)
{
	auto it = std::ranges::find(MECHANICS, name, &MechanicInfo::name);
	return it == MECHANICS.end() ? nullptr : &*it;
}

/**
 * Find the mechanic a family of one mode uses when it does not declare one.
 * @param mode Vehicle mode.
 * @return The first mechanic listed for the mode, or nullptr.
 */
const MechanicInfo *DefaultMechanic(TechMode mode)
{
	auto it = std::ranges::find(MECHANICS, mode, &MechanicInfo::mode);
	return it == MECHANICS.end() ? nullptr : &*it;
}

/**
 * Check whether a mechanic can serve a market.
 * @param mechanic Mechanic name.
 * @param market Market.
 * @return Whether the mechanic exists and lists the market.
 */
bool IsSupported(std::string_view mechanic, Market market)
{
	const MechanicInfo *info = FindMechanic(mechanic);
	return info != nullptr && std::ranges::find(info->markets, market) != info->markets.end();
}

} // namespace FutureLogistics
