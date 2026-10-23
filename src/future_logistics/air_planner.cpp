/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file air_planner.cpp Day-zero airlines: an airport in each of two towns and a passenger aircraft between them. */
#include "../stdafx.h"
#include "air_planner.h"
#include "freight_planner.h"
#include "catalogue.h"
#include "scale.h"
#include "tech_library.h"
#include "../airport.h"
#include "../cargotype.h"
#include "../command_func.h"
#include "../company_base.h"
#include "../core/math_func.hpp"
#include "../debug.h"
#include "../engine_base.h"
#include "../industry.h"
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
#include <functional>
#include <map>

#include "../safeguards.h"

namespace FutureLogistics {
namespace {
constexpr size_t MAX_AIRPORT_SITES = 2; ///< Airport sites tried per endpoint.
constexpr size_t MAX_AIR_PAIRS = 32; ///< Routes tried per airport type.
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
 * Find places for an airport at one end of a route: flat, clear, at one height, and serving the endpoint.
 * At a town the airport must belong to the town and accept the cargo; at an industry its catchment must
 * reach the industry, and at a destination it must also accept the cargo. A transfer station only has to
 * be near its tile and pass the place's own check.
 * @param end Town, industry or transfer station.
 * @param spec Airport type.
 * @param cargo Cargo carried.
 * @param destination Whether the cargo is delivered here.
 * @return Up to MAX_AIRPORT_SITES top corners, nearest first.
 */
std::vector<TileIndex> FindAirportSites(const Place &end, const AirportSpec *spec, CargoType cargo, bool destination)
{
	std::vector<TileIndex> result;
	const int radius = AirportCatchment(spec);
	const TileIndex centre = end.Centre();
	const int search = end.IsHub() ? end.radius : (end.town != nullptr ? AIRPORT_SEARCH_RADIUS
		: std::max<int>(end.industry->location.w, end.industry->location.h) / 2 + radius + std::max(spec->size_x, spec->size_y));
	const int cx = TileX(centre), cy = TileY(centre);
	for (int y = std::max(1, cy - search); y <= std::min((int)Map::SizeY() - 1 - spec->size_y, cy + search); y++) {
		for (int x = std::max(1, cx - search); x <= std::min((int)Map::SizeX() - 1 - spec->size_x, cx + search); x++) {
			const TileIndex corner = TileXY(x, y);
			const TileArea area(corner, spec->size_x, spec->size_y);
			const int z = GetTileZ(corner);
			bool usable = true;
			for (TileIndex tile : area) {
				if (!Buildable(tile) || GetTileZ(tile) != z) { usable = false; break; }
			}
			if (!usable) continue;
			if (end.IsHub()) {
				result.push_back(corner);
				continue;
			}
			if (end.town != nullptr) {
				if (ClosestTownFromTile(corner, UINT_MAX)->index != end.town->index) continue;
			} else {
				TileArea catchment = area;
				catchment.Expand(radius);
				if (!catchment.Intersects(end.industry->location)) continue;
			}
			if ((end.town != nullptr || destination) && GetAcceptanceAroundTiles(corner, spec->size_x, spec->size_y, radius).first[cargo] < 8) continue;
			result.push_back(corner);
		}
	}
	std::ranges::stable_sort(result, {}, [&](TileIndex t) { return DistanceManhattan(t, centre); });
	KeepUsable(result, end, [](TileIndex tile) { return tile; }, MAX_AIRPORT_SITES);
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
 * @param full_load Full load at the first airport and unload at the second, as for freight.
 * @param join_a Existing station the first airport joins, or NEW_STATION.
 * @param join_b Existing station the second airport joins, or NEW_STATION.
 * @param transfer Whether aircraft transfer and leave empty at the second airport instead of delivering.
 * @param execute Whether to build after a successful preflight; false only checks.
 * @param[out] result_built If given, receives what was built on success.
 * @return Whether preflight rejected, execution failed, or the service was built (or would be, when not executing).
 */
ConstructionResult BuildAirline(uint8_t type, TileIndex a, TileIndex b, const Engine *aircraft, CargoType cargo, uint fleet, bool full_load,
	StationID join_a = NEW_STATION, StationID join_b = NEW_STATION, bool transfer = false, bool execute = true, BuiltLeg *result_built = nullptr)
{
	Money cost = 0;
	for (const auto &[corner, join] : {std::pair{a, join_a}, {b, join_b}}) {
		auto result = Command<Commands::BuildAirport>::Do(CONSTRUCT, corner, type, 0, join, true);
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
	if (!execute) return ConstructionResult::Success;

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
	for (const auto &[corner, join] : {std::pair{a, join_a}, {b, join_b}}) {
		if (Command<Commands::BuildAirport>::Do(EXEC | CONSTRUCT, corner, type, 0, join, true).Failed()) return fail("airport");
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
			if (full_load) order.SetLoadType(i == 0 ? OrderLoadType::FullLoadAny : OrderLoadType::NoLoad);
			/* Transfer and leave empty: the cargo waits at the station for the next leg's vehicles. */
			if (transfer && i == 1) order.SetUnloadType(OrderUnloadType::Transfer);
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
	if (result_built != nullptr) {
		result_built->from = GetStationIndex(a);
		result_built->to = GetStationIndex(b);
		result_built->from_tile = a;
		result_built->to_tile = b;
		result_built->tiles = std::move(built);
		result_built->vehicles = std::move(vehicles);
	}
	return ConstructionResult::Success;
}

/** A candidate route for an airport pair. */
struct AirPair {
	size_t preference; ///< Rival's industry market preference; 0 is best.
	uint distance; ///< Manhattan distance between the endpoints.
	Place a; ///< Origin, which gets the aircraft.
	Place b; ///< Destination.
	MarketTier tier; ///< Market tier of the route.
};

/**
 * Try routes until one airport pair is built. Larger airport types are tried first, since they allow
 * larger and faster aircraft, but the route's market tier caps the airport class: no airports for
 * villages, small airports for towns, large ones for cities and hubs only for metropolises.
 * NewGRF airport classes beyond hub are never used.
 * @param library Loaded transport technology, including the airport types.
 * @param spec Family, limits and preferences of the service.
 * @param cargo Cargo carried.
 * @param role Role the aircraft needs.
 * @param pairs Candidate routes, best first.
 * @param on_success Called with the route and aircraft that were built.
 * @return Outcome.
 */
ConstructionResult TryAirPairs(const TechLibrary &library, const ServiceSpec &spec, CargoType cargo, TechRole role, const std::vector<AirPair> &pairs,
	const std::function<void(const AirPair &, const TechRecord &)> &on_success)
{
	std::vector<const InfraRecord *> airports;
	for (const InfraRecord &record : library.Infrastructure()) {
		if (record.kind != InfraKind::Airport || !spec.AllowsInfrastructure(record.tech)) continue;
		const AirportSpec *airport_spec = AirportSpec::Get(record.index);
		if (airport_spec->IsAvailable() && airport_spec->fsm != nullptr && GetAirport(record.index)->flags.Test(AirportFTAClass::Flag::Airplanes)) airports.push_back(&record);
	}
	std::ranges::stable_sort(airports, std::greater{}, [](const InfraRecord *r) { return r->size_x * r->size_y; });

	for (const InfraRecord *airport : airports) {
		const uint8_t type = static_cast<uint8_t>(airport->index);
		const AirportSpec *airport_spec = AirportSpec::Get(type);
		const bool short_strip = GetAirport(type)->flags.Test(AirportFTAClass::Flag::ShortStrip);
		for (size_t i = 0; i < std::min(MAX_AIR_PAIRS, pairs.size()); i++) {
			const AirPair &pair = pairs[i];
			const TierLimits &limits = GetTierLimits(pair.tier);
			if (!limits.air_passenger || airport_spec->class_index.base() > limits.max_airport_class) continue;
			const uint range_needed = static_cast<uint>(std::ceil(std::sqrt(static_cast<double>(DistanceSquare(pair.a.Centre(), pair.b.Centre())))));
			TechQuery query;
			query.mode = TechMode::Air;
			query.role = role;
			query.cargo = cargo;
			query.company = _current_company;
			query.short_strip = short_strip;
			query.distance = range_needed;
			query.family = spec.family;
			query.provides = airport->provides;
			query.early_adopters = spec.early_adopters;
			const TechRecord *record = library.FindBest(query);
			if (record == nullptr) continue;
			const auto origins = FindAirportSites(pair.a, airport_spec, cargo, false);
			const auto destinations = FindAirportSites(pair.b, airport_spec, cargo, true);
			for (TileIndex a : origins) {
				for (TileIndex b : destinations) {
					const auto result = BuildAirline(type, a, b, Engine::Get(record->engine), cargo, limits.fleet, false);
					if (result == ConstructionResult::Rejected) continue;
					if (result == ConstructionResult::Success) on_success(pair, *record);
					return result;
				}
			}
		}
	}
	return ConstructionResult::Rejected;
}
} // namespace

/**
 * Build an airport in each of two unused towns and fly passengers or mail between them.
 * @param library Loaded transport technology, including the airport types.
 * @param spec Family, limits and preferences of the service.
 * @param cargo Passengers or mail.
 * @param[in,out] used_towns Towns already served for this cargo; the chosen towns are added on success.
 * @return Outcome.
 */
ConstructionResult BuildTownAirline(const TechLibrary &library, const ServiceSpec &spec, CargoType cargo, std::set<TownID> &used_towns)
{
	if (!IsValidCargoType(cargo)) return ConstructionResult::Rejected;
	std::vector<const Town *> towns;
	for (const Town *town : Town::Iterate()) {
		if (towns.size() == 128) break;
		if (!used_towns.contains(town->index)) towns.push_back(town);
	}
	std::vector<AirPair> pairs;
	for (const Town *a : towns) {
		for (const Town *b : towns) {
			if (a->index >= b->index) continue;
			const uint distance = DistanceManhattan(a->xy, b->xy);
			const MarketTier tier = TierForTowns(a, b);
			if (distance < MIN_AIR_DISTANCE || distance > MAX_AIR_DISTANCE || !GetTierLimits(tier).air_passenger || tier < spec.min_tier) continue;
			pairs.push_back({0, distance, Place::Of(a), Place::Of(b), tier});
		}
	}
	std::ranges::stable_sort(pairs, {}, &AirPair::distance);
	return TryAirPairs(library, spec, cargo, TownRole(library, cargo, TechRole::PassengerIntercity), pairs, [&](const AirPair &pair, const TechRecord &record) {
		used_towns.insert(pair.a.town->index);
		used_towns.insert(pair.b.town->index);
		Debug(Facility::Misc, Severity::Notice, "FL towns company={} a={} b={} tier={} population={},{} aircraft={}", _current_company,
			pair.a.town->index, pair.b.town->index, TierName(pair.tier), pair.a.town->cache.population, pair.b.town->cache.population, record.id);
	});
}

/**
 * Build, or check, an air freight leg: aircraft that full-load at one airport and deliver or transfer at
 * the other. Larger airport types are tried first, capped by the market size, as for airlines.
 * Aircraft do not deliver freight into towns.
 * @copydoc LegBuild
 */
ConstructionResult AirLeg(const TechLibrary &library, const ServiceSpec &spec, const Place &from, const Place &to, const LegJob &job, bool execute, BuiltLeg &built)
{
	if (from.town != nullptr || to.town != nullptr) return ConstructionResult::Rejected;
	const TierLimits &limits = GetTierLimits(job.tier);
	if (!limits.air_passenger) return ConstructionResult::Rejected;
	std::vector<const InfraRecord *> airports;
	for (const InfraRecord &record : library.Infrastructure()) {
		if (record.kind != InfraKind::Airport || !spec.AllowsInfrastructure(record.tech)) continue;
		const AirportSpec *airport_spec = AirportSpec::Get(record.index);
		if (airport_spec->IsAvailable() && airport_spec->fsm != nullptr && GetAirport(record.index)->flags.Test(AirportFTAClass::Flag::Airplanes)) airports.push_back(&record);
	}
	std::ranges::stable_sort(airports, std::greater{}, [](const InfraRecord *r) { return r->size_x * r->size_y; });
	const uint range_needed = static_cast<uint>(std::ceil(std::sqrt(static_cast<double>(DistanceSquare(from.Centre(), to.Centre())))));
	for (const InfraRecord *airport : airports) {
		const uint8_t type = static_cast<uint8_t>(airport->index);
		const AirportSpec *airport_spec = AirportSpec::Get(type);
		if (airport_spec->class_index.base() > limits.max_airport_class) continue;
		TechQuery query;
		query.mode = TechMode::Air;
		query.role = FreightRole(library, job.cargo);
		query.cargo = job.cargo;
		query.company = _current_company;
		query.short_strip = GetAirport(type)->flags.Test(AirportFTAClass::Flag::ShortStrip);
		query.distance = range_needed;
		query.family = spec.family;
		query.provides = airport->provides;
		query.early_adopters = spec.early_adopters;
		const TechRecord *record = library.FindBest(query);
		if (record == nullptr) continue;
		const auto origins = FindAirportSites(from, airport_spec, job.cargo, false);
		if (origins.empty()) continue;
		const auto destinations = FindAirportSites(to, airport_spec, job.cargo, true);
		if (destinations.empty()) continue;
		built.sited = true;
		for (TileIndex a : origins) {
			for (TileIndex b : destinations) {
				const auto result = BuildAirline(type, a, b, Engine::Get(record->engine), job.cargo, limits.fleet, true, from.join, to.join, job.transfer, execute, &built);
				if (result != ConstructionResult::Rejected) return result;
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
