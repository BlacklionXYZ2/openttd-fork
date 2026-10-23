/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file air_planner.cpp Day-zero airlines: an airport in each of two towns and a passenger aircraft between them. */
#include "../stdafx.h"
#include "air_planner.h"
#include "scale.h"
#include "tech_library.h"
#include "../airport.h"
#include "../cargotype.h"
#include "../command_func.h"
#include "../company_base.h"
#include "../core/math_func.hpp"
#include "../debug.h"
#include "../engine_base.h"
#include "../landscape_cmd.h"
#include "../newgrf_airport.h"
#include "../order_base.h"
#include "../order_cmd.h"
#include "../settings_type.h"
#include "../station_base.h"
#include "../station_cmd.h"
#include "../station_func.h"
#include "../town.h"
#include "../vehicle_base.h"
#include "../vehicle_cmd.h"
#include <map>

#include "../safeguards.h"

namespace FutureLogistics {
namespace {
constexpr size_t MAX_AIRPORT_SITES = 2; ///< Airport sites tried per town.
constexpr size_t MAX_AIR_PAIRS = 32; ///< Town pairs tried per airport type.
constexpr int AIRPORT_SEARCH_RADIUS = 20; ///< How far from the town centre an airport may go.
constexpr uint MIN_AIR_DISTANCE = 48; ///< Shorter hops are left to road and rail.
constexpr uint MAX_AIR_DISTANCE = 256; ///< Longest route considered, in Manhattan tiles.

/**
 * Get an airport type's catchment radius as the station rules apply it.
 * @param spec Airport type.
 * @return Radius in tiles.
 */
int AirportCatchment(const AirportSpec *spec)
{
	return _settings_game.station.modified_catchment ? spec->catchment : CA_UNMODIFIED;
}

/**
 * Find places for an airport near a town: flat, clear, at one height, and where passengers are accepted.
 * @param town Town to serve.
 * @param spec Airport type.
 * @param cargo Passenger cargo type.
 * @return Up to MAX_AIRPORT_SITES top corners, nearest to the town centre first.
 */
std::vector<TileIndex> FindAirportSites(const Town *town, const AirportSpec *spec, CargoType cargo)
{
	std::vector<TileIndex> result;
	const int radius = AirportCatchment(spec);
	const int cx = TileX(town->xy), cy = TileY(town->xy);
	for (int y = std::max(1, cy - AIRPORT_SEARCH_RADIUS); y <= std::min((int)Map::SizeY() - 1 - spec->size_y, cy + AIRPORT_SEARCH_RADIUS); y++) {
		for (int x = std::max(1, cx - AIRPORT_SEARCH_RADIUS); x <= std::min((int)Map::SizeX() - 1 - spec->size_x, cx + AIRPORT_SEARCH_RADIUS); x++) {
			const TileIndex corner = TileXY(x, y);
			const TileArea area(corner, spec->size_x, spec->size_y);
			const int z = GetTileZ(corner);
			bool usable = true;
			for (TileIndex tile : area) {
				if (!Buildable(tile) || GetTileZ(tile) != z) { usable = false; break; }
			}
			if (!usable || ClosestTownFromTile(corner, UINT_MAX)->index != town->index) continue;
			if (GetAcceptanceAroundTiles(corner, spec->size_x, spec->size_y, radius).first[cargo] < 8) continue;
			result.push_back(corner);
		}
	}
	std::ranges::stable_sort(result, {}, [&](TileIndex t) { return DistanceManhattan(t, town->xy); });
	if (result.size() > MAX_AIRPORT_SITES) result.resize(MAX_AIRPORT_SITES);
	return result;
}

/**
 * Preflight two airports and an aircraft, then build them through normal commands.
 * @param type Airport type.
 * @param a Top corner of the first airport, which gets the aircraft.
 * @param b Top corner of the second airport.
 * @param aircraft Aircraft to buy.
 * @param cargo Cargo to refit it to.
 * @param fleet Number of aircraft.
 * @return Whether preflight rejected, execution failed, or the service was built.
 */
ConstructionResult BuildAirline(uint8_t type, TileIndex a, TileIndex b, const Engine *aircraft, CargoType cargo, uint fleet)
{
	Money cost = 0;
	for (TileIndex corner : {a, b}) {
		auto result = Command<Commands::BuildAirport>::Do(CONSTRUCT, corner, type, 0, NEW_STATION, true);
		if (result.Failed()) {
			Debug(Facility::Misc, Severity::Debug1, "FL airport preflight rejected tile={} error={}", corner, result.GetErrorMessage());
			return ConstructionResult::Rejected;
		}
		cost += result.GetCost();
	}
	const Company *company = Company::Get(_current_company);
	if (cost * 2 + aircraft->GetCost() * 2 * fleet > company->money) return ConstructionResult::Rejected;
	const AirportSpec *spec = AirportSpec::Get(type);
	if ((company->clear_limit >> 16) < static_cast<uint>(spec->size_x * spec->size_y) * 2 + 8) return ConstructionResult::Rejected;

	std::vector<TileIndex> built;
	std::vector<VehicleID> vehicles;
	auto rollback = [&]() {
		for (VehicleID vehicle : vehicles) {
			if (Command<Commands::SellVehicle>::Do(EXEC, vehicle, true, false, ClientID::Invalid).Failed()) {
				Debug(Facility::Misc, Severity::Error, "FL rollback failed selling vehicle={}; infrastructure retained", vehicle);
				return;
			}
		}
		/* Clearing any airport tile removes the whole airport. */
		for (auto it = built.rbegin(); it != built.rend(); ++it) {
			auto result = Command<Commands::LandscapeClear>::Do(EXEC, *it);
			if (result.Failed()) Debug(Facility::Misc, Severity::Error, "FL rollback failed tile={} error={}", *it, result.GetErrorMessage());
		}
	};
	auto fail = [&](std::string_view phase) {
		Debug(Facility::Misc, Severity::Warning, "FL construction failed company={} phase={}; clearing new infrastructure", _current_company, phase);
		rollback();
		return ConstructionResult::Failed;
	};
	for (TileIndex corner : {a, b}) {
		if (Command<Commands::BuildAirport>::Do(EXEC | CONSTRUCT, corner, type, 0, NEW_STATION, true).Failed()) return fail("airport");
		built.push_back(corner);
	}
	const Station *origin = Station::GetByTile(a);
	if (origin->airport.GetNumHangars() == 0) return fail("hangar");
	for (uint n = 0; n < fleet; n++) {
		auto result = Command<Commands::BuildVehicle>::Do(EXEC, origin->airport.GetHangarTile(0), aircraft->index, false, cargo, ClientID::Invalid);
		if (ExtractCommandCost(result).Failed()) return fail("vehicle");
		const VehicleID vehicle = std::get<1>(result);
		vehicles.push_back(vehicle);
		for (VehicleOrderID i = 0; i < 2; i++) {
			Order order;
			order.MakeGoToStation(GetStationIndex(i == 0 ? a : b));
			/* Non-train orders must use the far end; the default near end is rejected. */
			order.SetStopLocation(OrderStopLocation::FarEnd);
			auto inserted = Command<Commands::InsertOrder>::Do(EXEC, vehicle, i, order);
			if (inserted.Failed()) {
				Debug(Facility::Misc, Severity::Warning, "FL order rejected error={}", inserted.GetErrorMessage());
				return fail("orders");
			}
		}
		if (Command<Commands::StartStopVehicle>::Do(EXEC, vehicle, true).Failed()) return fail("start");
	}
	Debug(Facility::Misc, Severity::Notice, "FL air company={} airport={} vehicles={} vehicle={} from={} to={} distance={} balance={}",
		_current_company, type, fleet, vehicles.front(), a, b, DistanceManhattan(a, b), Company::Get(_current_company)->money);
	return ConstructionResult::Success;
}
} // namespace

/**
 * Build an airport in each of two unused towns and fly a passenger aircraft between them.
 * Larger airport types are tried first, since they allow larger and faster aircraft, but the
 * smaller town's market tier caps the airport class: no airline for villages, small airports
 * for towns, large ones for cities and hubs only for metropolises. NewGRF airport classes
 * beyond hub are never used.
 * @param library Loaded transport technology, including the airport types.
 * @param[in,out] used_towns Towns already served by rivals; the chosen towns are added on success.
 * @return Outcome.
 */
ConstructionResult BuildPassengerAirline(const TechLibrary &library, const ServiceSpec &spec, std::set<TownID> &used_towns)
{
	const CargoType cargo = GetCargoTypeByLabel(CT_PASSENGERS);
	if (!IsValidCargoType(cargo)) return ConstructionResult::Rejected;

	std::vector<const InfraRecord *> airports;
	for (const InfraRecord &record : library.Infrastructure()) {
		if (record.kind != InfraKind::Airport || !spec.AllowsInfrastructure(record.tech)) continue;
		const AirportSpec *airport_spec = AirportSpec::Get(record.index);
		if (airport_spec->IsAvailable() && airport_spec->fsm != nullptr && GetAirport(record.index)->flags.Test(AirportFTAClass::Flag::Airplanes)) airports.push_back(&record);
	}
	std::ranges::stable_sort(airports, std::greater{}, [](const InfraRecord *r) { return r->size_x * r->size_y; });

	struct Pair { uint distance; const Town *a; const Town *b; };
	std::vector<const Town *> towns;
	for (const Town *town : Town::Iterate()) {
		if (towns.size() == 128) break;
		if (!used_towns.contains(town->index)) towns.push_back(town);
	}
	std::vector<Pair> pairs;
	for (const Town *a : towns) {
		for (const Town *b : towns) {
			if (a->index >= b->index) continue;
			const uint distance = DistanceManhattan(a->xy, b->xy);
			const MarketTier tier = TierForTowns(a, b);
			if (distance < MIN_AIR_DISTANCE || distance > MAX_AIR_DISTANCE || !GetTierLimits(tier).air_passenger || tier < spec.min_tier) continue;
			pairs.push_back({distance, a, b});
		}
	}
	std::ranges::stable_sort(pairs, {}, &Pair::distance);

	for (const InfraRecord *airport : airports) {
		const uint8_t type = static_cast<uint8_t>(airport->index);
		const AirportSpec *airport_spec = AirportSpec::Get(type);
		const bool short_strip = GetAirport(type)->flags.Test(AirportFTAClass::Flag::ShortStrip);
		std::map<TownID, std::vector<TileIndex>> sites;
		auto town_sites = [&](const Town *town) -> const std::vector<TileIndex> & {
			auto it = sites.find(town->index);
			if (it != sites.end()) return it->second;
			return sites.emplace(town->index, FindAirportSites(town, airport_spec, cargo)).first->second;
		};
		for (size_t i = 0; i < std::min(MAX_AIR_PAIRS, pairs.size()); i++) {
			const Pair &pair = pairs[i];
			const MarketTier tier = TierForTowns(pair.a, pair.b);
			const TierLimits &limits = GetTierLimits(tier);
			if (!limits.air_passenger || airport_spec->class_index.base() > limits.max_airport_class) continue;
			const uint range_needed = static_cast<uint>(std::ceil(std::sqrt(static_cast<double>(DistanceSquare(pair.a->xy, pair.b->xy)))));
			TechQuery query;
			query.mode = TechMode::Air;
			query.role = TechRole::PassengerIntercity;
			query.cargo = cargo;
			query.company = _current_company;
			query.short_strip = short_strip;
			query.distance = range_needed;
			query.family = spec.family;
			query.provides = airport->provides;
			query.early_adopters = spec.early_adopters;
			const TechRecord *record = library.FindBest(query);
			if (record == nullptr) continue;
			for (TileIndex a : town_sites(pair.a)) {
				for (TileIndex b : town_sites(pair.b)) {
					const auto result = BuildAirline(type, a, b, Engine::Get(record->engine), cargo, limits.fleet);
					if (result == ConstructionResult::Rejected) continue;
					if (result == ConstructionResult::Success) {
						used_towns.insert(pair.a->index);
						used_towns.insert(pair.b->index);
						Debug(Facility::Misc, Severity::Notice, "FL towns company={} a={} b={} tier={} population={},{} aircraft={}", _current_company,
							pair.a->index, pair.b->index, TierName(tier), pair.a->cache.population, pair.b->cache.population, record->id);
					}
					return result;
				}
			}
		}
	}
	return ConstructionResult::Rejected;
}

/**
 * Aircraft need no ground connection; check that each ordered station is one of the owner's airports.
 * @param aircraft Aircraft to inspect.
 * @return The owner's stations with an airport that the aircraft's orders use.
 */
std::set<StationID> AirportsReachable(const Vehicle *aircraft)
{
	std::set<StationID> reached;
	for (const Order &order : aircraft->Orders()) {
		if (!order.IsType(OT_GOTO_STATION)) continue;
		const StationID id = order.GetDestination().ToStationID();
		const Station *station = Station::GetIfValid(id);
		if (station != nullptr && station->owner == aircraft->owner && station->facilities.Test(StationFacility::Airport)) reached.insert(id);
	}
	return reached;
}

} // namespace FutureLogistics
