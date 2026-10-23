/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file bootstrap.cpp Bounded, deterministic town-to-town bus network bootstrap. */
#include "../stdafx.h"
#include "bootstrap.h"
#include "air_planner.h"
#include "planner_common.h"
#include "profiles.h"
#include "rail_planner.h"
#include "scale.h"
#include "tech_library.h"
#include "../ai/ai.hpp"
#include "../ai/ai_config.hpp"
#include "../company_cmd.h"
#include "../cargotype.h"
#include "../command_func.h"
#include "../company_base.h"
#include "../core/backup_type.hpp"
#include "../debug.h"
#include "../engine_base.h"
#include "../engine_func.h"
#include "../industry.h"
#include "../landscape_cmd.h"
#include "../misc_cmd.h"
#include "../network/network.h"
#include "../order_base.h"
#include "../order_cmd.h"
#include "../road_cmd.h"
#include "../road_map.h"
#include "../roadveh.h"
#include "../direction_func.h"
#include "../settings_type.h"
#include "../signal_func.h"
#include "../station_cmd.h"
#include "../station_func.h"
#include "../station_map.h"
#include "../town.h"
#include "../vehicle_cmd.h"
#include <queue>
#include <map>
#include <set>

#include "../safeguards.h"

using namespace FutureLogistics;

extern Company *DoStartupNewCompany(bool is_ai, CompanyID company);

namespace {
/**
 * Convert a route direction to its road bit.
 * @param direction Diagonal direction.
 * @return Corresponding road bit.
 */
RoadBits Bit(DiagDirection direction)
{
	switch (direction) {
		case DiagDirection::NE: return RoadBit::NE;
		case DiagDirection::SE: return RoadBit::SE;
		case DiagDirection::SW: return RoadBit::SW;
		case DiagDirection::NW: return RoadBit::NW;
		default: NOT_REACHED();
	}
}

/**
 * Find station candidates that can generate and accept the selected cargo.
 * @param town Town to serve.
 * @param cargo Passenger cargo type.
 * @return Buildable candidate tiles belonging to this town's area.
 */
std::vector<TileIndex> Stops(const Town *town, CargoType cargo)
{
	std::vector<TileIndex> result;
	const int radius = _settings_game.station.modified_catchment ? CA_BUS : CA_UNMODIFIED;
	for (int y = std::max(1, (int)TileY(town->xy) - 12); y <= std::min((int)Map::SizeY() - 2, (int)TileY(town->xy) + 12); y++) {
		for (int x = std::max(1, (int)TileX(town->xy) - 12); x <= std::min((int)Map::SizeX() - 2, (int)TileX(town->xy) + 12); x++) {
			TileIndex tile = TileXY(x, y);
			if (!Buildable(tile) || ClosestTownFromTile(tile, UINT_MAX)->index != town->index) continue;
			if (GetAcceptanceAroundTiles(tile, 1, 1, radius).first[cargo] < 8 || GetProductionAroundTiles(tile, 1, 1, radius)[cargo] == 0) continue;
			result.push_back(tile);
		}
	}
	return result;
}

/** Proposed owned road corridor and attached depot. */
struct Route {
	std::vector<TileIndex> path; ///< Ordered tiles between the two stops.
	TileIndex depot = INVALID_TILE; ///< Depot tile, invalid if planning failed.
	size_t junction = 0; ///< Index of the road tile adjoining the depot.
};

/**
 * Find a corridor using bounded multi-source breadth-first search.
 * @param starts Candidate stops in the origin town.
 * @param ends Candidate stops in the destination town.
 * @return Proposed route, with an invalid depot if no suitable corridor exists.
 */
Route FindRoute(const std::vector<TileIndex> &starts, const std::vector<TileIndex> &ends)
{
	std::set<TileIndex> targets(ends.begin(), ends.end());
	std::map<TileIndex, TileIndex> previous;
	std::queue<TileIndex> queue;
	for (TileIndex tile : starts) {
		previous.emplace(tile, INVALID_TILE);
		queue.push(tile);
	}
	while (!queue.empty()) {
		TileIndex tile = queue.front();
		queue.pop();
		if (targets.contains(tile)) {
			Route route;
			for (TileIndex t = tile; t != INVALID_TILE; t = previous.at(t)) route.path.push_back(t);
			std::reverse(route.path.begin(), route.path.end());
			if (route.path.size() < 8 || route.path.size() > 128) continue;
			std::set<TileIndex> occupied(route.path.begin(), route.path.end());
			for (size_t i = 1; i + 1 < route.path.size(); i++) {
				for (TileIndex neighbour : Neighbours(route.path[i])) {
					if (!occupied.contains(neighbour) && Buildable(neighbour) && GetTileZ(neighbour) == GetTileZ(tile)) {
						route.depot = neighbour;
						route.junction = i;
						return route;
					}
				}
			}
		}
		for (TileIndex next : Neighbours(tile)) {
			if (previous.size() >= MAX_SEARCH) break;
			if (previous.contains(next) || !Buildable(next) || GetTileZ(next) != GetTileZ(tile)) continue;
			previous.emplace(next, tile);
			queue.push(next);
		}
	}
	return {};
}

/**
 * Calculate road connections for one corridor tile.
 * @param route Proposed corridor and depot.
 * @param i Tile index within the corridor.
 * @return Road bits connecting neighbours and, if applicable, the depot.
 */
RoadBits Pieces(const Route &route, size_t i)
{
	RoadBits bits;
	if (i > 0) bits |= Bit(RouteDirection(route.path[i], route.path[i - 1]));
	if (i + 1 < route.path.size()) bits |= Bit(RouteDirection(route.path[i], route.path[i + 1]));
	if (i == route.junction) bits |= Bit(RouteDirection(route.path[i], route.depot));
	/* End stops need both road halves, allowing buses to turn around at the dead end. */
	if (i == 0 || i + 1 == route.path.size()) {
		const auto dir = RouteDirection(route.path[i], route.path[i == 0 ? 1 : i - 1]);
		bits = (dir == DiagDirection::NE || dir == DiagDirection::SW) ? ROAD_X : ROAD_Y;
	}
	return bits;
}

/**
 * Test or build a separate drive-through bus stop.
 * @param tile Station tile.
 * @param direction Road alignment direction.
 * @param roadtype Compatible road type.
 * @param flags Command execution flags.
 * @return Command cost or rejection.
 */
CommandCost Stop(TileIndex tile, DiagDirection direction, RoadType roadtype, DoCommandFlags flags)
{
	return Command<Commands::BuildRoadStop>::Do(flags | CONSTRUCT, tile, 1, 1, RoadStopType::Bus, true, direction,
		roadtype, ROADSTOP_CLASS_DFLT, 0, NEW_STATION, true);
}

/**
 * Preflight the entire layout, then execute through accounting commands.
 * @param route Corridor and depot proposal.
 * @param engine Compatible passenger bus engine.
 * @param cargo Cargo to refit the bus to.
 * @return Whether preflight rejected, execution failed, or service was built.
 */
ConstructionResult Build(const Route &route, const Engine *engine, CargoType cargo)
{
	const RoadType rt = engine->VehInfo<RoadVehicleInfo>().roadtype;
	Money cost = 0;
	for (size_t i = 0; i < route.path.size(); i++) {
		auto result = Command<Commands::BuildRoad>::Do(CONSTRUCT, route.path[i], Pieces(route, i), rt, {}, TownID::Invalid());
		if (result.Failed()) {
			Debug(Facility::Misc, Severity::Debug1, "FL road preflight rejected tile={} error={}", route.path[i], result.GetErrorMessage());
			return ConstructionResult::Rejected;
		}
		cost += result.GetCost();
	}
	const auto first_dir = RouteDirection(route.path[0], route.path[1]);
	const auto last_dir = RouteDirection(route.path.back(), route.path[route.path.size() - 2]);
	for (const auto &[tile, direction] : {std::pair{route.path.front(), first_dir}, {route.path.back(), last_dir}}) {
		auto result = Stop(tile, direction, rt, {});
		if (result.Failed()) return ConstructionResult::Rejected;
		cost += result.GetCost();
	}
	const auto depot_dir = RouteDirection(route.depot, route.path[route.junction]);
	auto depot_test = Command<Commands::BuildRoadDepot>::Do(CONSTRUCT, route.depot, rt, depot_dir);
	if (depot_test.Failed()) return ConstructionResult::Rejected;
	cost += depot_test.GetCost();
	/* Reserve extra capital for the vehicle and any command-cost changes after construction. */
	const Company *company = Company::Get(_current_company);
	if (cost * 2 + engine->GetCost() * 2 > company->money) return ConstructionResult::Rejected;
	/* Explicit demolition consumes clearing credits. Keep enough for a complete rollback. */
	if ((company->clear_limit >> 16) < route.path.size() * 3 + 8) return ConstructionResult::Rejected;

	std::vector<TileIndex> built;
	VehicleID vehicle = VehicleID::Invalid();
	auto rollback = [&]() {
		if (vehicle != VehicleID::Invalid() && Command<Commands::SellVehicle>::Do(EXEC, vehicle, true, false, ClientID::Invalid).Failed()) {
			Debug(Facility::Misc, Severity::Error, "FL rollback failed selling vehicle={}; infrastructure retained", vehicle);
			return;
		}
		for (auto it = built.rbegin(); it != built.rend(); ++it) {
			auto result = Command<Commands::LandscapeClear>::Do(EXEC, *it);
			if (result.Failed()) {
				Debug(Facility::Misc, Severity::Error, "FL rollback failed tile={} error={}", *it, result.GetErrorMessage());
			}
		}
	};
	auto fail = [&](std::string_view phase) {
		Debug(Facility::Misc, Severity::Warning, "FL construction failed company={} phase={}; clearing new infrastructure", _current_company, phase);
		rollback();
		return ConstructionResult::Failed;
	};
	for (size_t i = 0; i < route.path.size(); i++) {
		if (Command<Commands::BuildRoad>::Do(EXEC | CONSTRUCT, route.path[i], Pieces(route, i), rt, {}, TownID::Invalid()).Failed()) return fail("road");
		built.push_back(route.path[i]);
	}
	if (Stop(route.path.front(), first_dir, rt, EXEC).Failed() || Stop(route.path.back(), last_dir, rt, EXEC).Failed()) return fail("stops");
	if (Command<Commands::BuildRoadDepot>::Do(EXEC | CONSTRUCT, route.depot, rt, depot_dir).Failed()) return fail("depot");
	built.push_back(route.depot);
	auto result = Command<Commands::BuildVehicle>::Do(EXEC, route.depot, engine->index, false, cargo, ClientID::Invalid);
	if (ExtractCommandCost(result).Failed()) return fail("vehicle");
	vehicle = std::get<1>(result);
	for (VehicleOrderID i = 0; i < 2; i++) {
		Order order;
		order.MakeGoToStation(GetStationIndex(i == 0 ? route.path.front() : route.path.back()));
		order.SetStopLocation(OrderStopLocation::FarEnd);
		if (Command<Commands::InsertOrder>::Do(EXEC, vehicle, i, order).Failed()) return fail("orders");
	}
	if (Command<Commands::StartStopVehicle>::Do(EXEC, vehicle, true).Failed()) return fail("start");
	uint64_t signature = 14695981039346656037ULL;
	for (TileIndex tile : route.path) signature = (signature ^ tile.base()) * 1099511628211ULL;
	Debug(Facility::Misc, Severity::Notice, "FL route company={} vehicle={} from={} to={} depot={} tiles={} balance={} signature={}",
		_current_company, vehicle, route.path.front(), route.path.back(), route.depot, route.path.size(), Company::Get(_current_company)->money, signature);
	return ConstructionResult::Success;
}

/**
 * Build one starter network in an unused town market.
 * @param library Loaded transport technology.
 * @param cargo Passenger cargo type.
 * @param[in,out] used_towns Towns reserved by successfully completed rivals.
 * @return Outcome.
 */
ConstructionResult Network(const TechLibrary &library, CargoType cargo, std::set<TownID> &used_towns)
{
	/* The planner only builds plain road, so ask for a bus that runs on it. */
	const TechRecord *record = library.FindBest({.mode = TechMode::Road, .role = TechRole::PassengerFeeder, .cargo = cargo, .company = _current_company, .roadtype = ROADTYPE_ROAD});
	if (record == nullptr) return ConstructionResult::Rejected;
	const Engine *bus = Engine::Get(record->engine);
	Debug(Facility::Misc, Severity::Notice, "FL tech company={} role={} vehicle={}", _current_company, TechRoleName(TechRole::PassengerFeeder), record->id);
	struct Pair { uint distance; const Town *a; const Town *b; };
	std::vector<Pair> pairs;
	std::vector<const Town *> towns;
	for (const Town *town : Town::Iterate()) {
		if (towns.size() == 128) break;
		towns.push_back(town);
	}
	for (const Town *a : towns) {
		for (const Town *b : towns) {
			if (a->index >= b->index || used_towns.contains(a->index) || used_towns.contains(b->index)) continue;
			uint distance = DistanceManhattan(a->xy, b->xy);
			if (distance >= 16 && distance <= 96) pairs.push_back({distance, a, b});
		}
	}
	std::stable_sort(pairs.begin(), pairs.end(), [](const Pair &a, const Pair &b) { return a.distance < b.distance; });
	std::map<TownID, std::vector<TileIndex>> candidates;
	for (size_t i = 0; i < std::min(MAX_PAIRS, pairs.size()); i++) {
		const auto &pair = pairs[i];
		for (const Town *town : {pair.a, pair.b}) {
			if (!candidates.contains(town->index)) candidates.emplace(town->index, Stops(town, cargo));
		}
		auto route = FindRoute(candidates.at(pair.a->index), candidates.at(pair.b->index));
		if (route.depot == INVALID_TILE) continue;
		auto result = Build(route, bus, cargo);
		if (result == ConstructionResult::Failed) return ConstructionResult::Failed;
		if (result == ConstructionResult::Success) {
			used_towns.insert(pair.a->index);
			used_towns.insert(pair.b->index);
			Debug(Facility::Misc, Severity::Notice, "FL towns company={} a={} b={}", _current_company, pair.a->index, pair.b->index);
			return ConstructionResult::Success;
		}
	}
	return ConstructionResult::Rejected;
}

/** Towns and industries already taken by earlier rivals, so they do not compete for the same market. */
struct Reservations {
	std::set<TownID> towns;
	std::set<IndustryID> industries;
};

/**
 * Build one starter service for the current company.
 * @param type Service to build.
 * @param library Loaded transport technology.
 * @param[in,out] reserved Markets already taken.
 * @return Outcome.
 */
ConstructionResult BuildService(ServiceType type, const TechLibrary &library, Reservations &reserved)
{
	switch (type) {
		case ServiceType::Bus: {
			const CargoType cargo = GetCargoTypeByLabel(CT_PASSENGERS);
			return IsValidCargoType(cargo) ? Network(library, cargo, reserved.towns) : ConstructionResult::Rejected;
		}
		case ServiceType::RailPassenger: return BuildPassengerRail(library, reserved.towns);
		case ServiceType::RailFreight: return BuildFreightRail(library, reserved.industries);
		case ServiceType::AirPassenger: return BuildPassengerAirline(library, reserved.towns);
	}
	NOT_REACHED();
}

/**
 * Name a new rival after its profile and tell its CorporateAI which profile it has.
 * Both are stored in the save game.
 * @param company New rival.
 * @param profile Its profile.
 */
void ApplyProfile(Company *company, const CompanyProfile &profile)
{
	AutoRestoreBackup owner(_current_company, company->index);
	if (Command<Commands::RenameCompany>::Do(EXEC, std::string{profile.name}).Failed()) {
		Debug(Facility::Misc, Severity::Warning, "FL could not rename company={} to profile name", company->index);
	}
	/* Ignored unless the company runs an AI with a "profile" setting, such as CorporateAI. */
	if (company->ai_config != nullptr) company->ai_config->SetSetting("profile", profile.id);
	Debug(Facility::Misc, Severity::Notice, "FL profile company={} profile={}", company->index, profile.key);
}
} // namespace

void BootstrapFutureLogistics()
{
	if (_networking || !_settings_game.game_creation.bootstrap_enabled) return;
	/* Fail closed for unexpected lifecycle calls, including a second invocation. */
	if (Company::GetNumItems() != 1 || !Company::IsValidID(CompanyID{0}) || Company::Get(CompanyID{0})->is_ai) {
		Debug(Facility::Misc, Severity::Warning, "FL skipped: expected only human company 0");
		return;
	}
	const uint count = std::min<uint>(4, _settings_game.difficulty.max_no_competitors);
	Debug(Facility::Misc, Severity::Notice, "FL begin seed={} rivals={}", _settings_game.game_creation.generation_seed, count);
	uint operational = 0;
	uint services = 0;
	Reservations reserved;
	const TechLibrary library = TechLibrary::Build();
	const auto profiles = GetCompanyProfiles();
	/* The headstart is in proportion to the world: capital follows the total population, capped by the setting. */
	uint64_t population = 0;
	for (const Town *town : Town::Iterate()) population += town->cache.population;
	const Money grant = RivalGrant(population, count, _settings_game.game_creation.bootstrap_capital_per_head, _settings_game.game_creation.bootstrap_capital);
	Debug(Facility::Misc, Severity::Notice, "FL market population={} towns={} industries={} grant={}", population, Town::GetNumItems(), Industry::GetNumItems(), grant);
	for (uint i = 1; i <= count; i++) {
		Company *company = DoStartupNewCompany(true, CompanyID{static_cast<uint8_t>(i)});
		if (company == nullptr) { Debug(Facility::Misc, Severity::Warning, "FL company allocation failed slot={}", i); break; }
		/* StartNew initializes a VM; no AI GameLoop runs in this synchronous callback. */
		AI::Pause(company->index);
		if (grant > 0) {
			AutoRestoreBackup owner(_current_company, OWNER_DEITY);
			auto result = Command<Commands::ChangeBankBalance>::Do(EXEC, TileIndex{0}, grant, company->index, ExpensesType::Other);
			if (result.Failed()) {
				Debug(Facility::Misc, Severity::Error, "FL funding failed company={}", company->index);
				AI::Unpause(company->index);
				continue;
			}
		}
		Debug(Facility::Misc, Severity::Notice, "FL funded company={} grant={} balance={} loan={}", company->index,
			grant, company->money, company->current_loan);
		const CompanyProfile &profile = profiles[(i - 1) % profiles.size()];
		ApplyProfile(company, profile);
		{
			AutoRestoreBackup owner(_current_company, company->index);
			uint built = 0;
			bool stopped = false;
			auto attempt = [&](ServiceType type, bool fallback) {
				const ConstructionResult result = BuildService(type, library, reserved);
				/* Commands run here bypass the normal command loop, so flush track signal updates before another company builds. */
				UpdateSignalsInBuffer();
				Debug(Facility::Misc, Severity::Notice, "FL service company={} type={} fallback={} result={}", company->index, ServiceName(type), fallback,
					result == ConstructionResult::Success ? "success" : (result == ConstructionResult::Failed ? "failed" : "rejected"));
				if (result == ConstructionResult::Success) built++;
				/* After an execution failure the company stops building, as rollback may be incomplete. */
				if (result == ConstructionResult::Failed) stopped = true;
			};
			for (ServiceType type : profile.services) {
				if (!stopped) attempt(type, false);
			}
			if (built == 0 && !stopped && std::ranges::find(profile.services, ServiceType::Bus) == profile.services.end()) attempt(ServiceType::Bus, true);
			services += built;
			if (built > 0) {
				operational++;
			} else {
				Debug(Facility::Misc, Severity::Warning, "FL no usable starter network company={} (terrain, towns, industries, funds or vehicle availability); funded company retained", company->index);
			}
		}
		AI::Unpause(company->index);
	}
	Debug(Facility::Misc, Severity::Notice, "FL complete companies={} operational={} services={}", Company::GetNumItems(), operational, services);
}

/**
 * Follow a road vehicle's owned road from its owner's depot.
 * @param vehicle Road vehicle to inspect.
 * @return Bus stops the vehicle can reach over its owner's connected road.
 */
static std::set<StationID> RoadStationsReachable(const Vehicle *vehicle)
{
	TileIndex depot = INVALID_TILE;
	for (uint t = 0; t < Map::Size(); t++) {
		TileIndex tile{t};
		if (IsRoadDepotTile(tile) && GetTileOwner(tile) == vehicle->owner) { depot = tile; break; }
	}
	std::set<TileIndex> seen;
	std::set<StationID> reached;
	std::queue<TileIndex> queue;
	if (depot != INVALID_TILE) { seen.insert(depot); queue.push(depot); }
	while (!queue.empty() && seen.size() <= MAX_SEARCH) {
		TileIndex tile = queue.front(); queue.pop();
		if (IsStationRoadStopTile(tile) && GetRoadStopType(tile) == RoadStopType::Bus && GetTileOwner(tile) == vehicle->owner) reached.insert(GetStationIndex(tile));
		for (TileIndex next : Neighbours(tile)) {
			if (seen.contains(next) || GetAnyRoadBits(next, RoadTramType::Road).None()) continue;
			if (GetRoadOwner(next, RoadTramType::Road) != vehicle->owner) continue;
			if (!GetAnyRoadBits(tile, RoadTramType::Road).Any(Bit(RouteDirection(tile, next))) || !GetAnyRoadBits(next, RoadTramType::Road).Any(Bit(RouteDirection(next, tile)))) continue;
			seen.insert(next); queue.push(next);
		}
	}
	return reached;
}

bool InspectFutureLogistics()
{
	bool valid = Company::IsValidID(CompanyID{0}) && !Company::Get(CompanyID{0})->is_ai && _local_company == CompanyID{0};
	uint checked = 0;
	for (const Company *company : Company::Iterate()) {
		Debug(Facility::Misc, Severity::Notice, "FL check funds company={} balance={} loan={} road_bits={} station_tiles={} rail_bits={} other_expense={} ai={}", company->index, company->money, company->current_loan, company->infrastructure.GetRoadTotal(), company->infrastructure.station, company->infrastructure.GetRailTotal(), company->yearly_expenses[0][ExpensesType::Other], company->is_ai);
	}
	for (const Vehicle *vehicle : Vehicle::Iterate()) {
		if ((vehicle->type != VehicleType::Road && vehicle->type != VehicleType::Train && vehicle->type != VehicleType::Aircraft) || !vehicle->IsPrimaryVehicle() || vehicle->owner == CompanyID{0}) continue;
		bool connected = Company::IsValidAiID(vehicle->owner) && vehicle->GetNumOrders() == 2 && !vehicle->vehstatus.Test(VehState::Stopped);
		std::set<StationID> destinations;
		for (const Order &order : vehicle->Orders()) {
			if (!order.IsType(OT_GOTO_STATION)) { connected = false; continue; }
			destinations.insert(order.GetDestination().ToStationID());
		}
		std::set<StationID> reached;
		switch (vehicle->type) {
			case VehicleType::Train: reached = RailStationsReachable(vehicle); break;
			case VehicleType::Aircraft: reached = AirportsReachable(vehicle); break;
			default: reached = RoadStationsReachable(vehicle); break;
		}
		connected &= destinations.size() == 2 && std::ranges::includes(reached, destinations);
		valid &= connected;
		checked++;
		/* All cargo, so freight trains count too. */
		const Company *company = Company::Get(vehicle->owner);
		uint delivered = company->cur_economy.delivered_cargo.GetSum<uint>();
		for (const auto &quarter : company->old_economy) delivered += quarter.delivered_cargo.GetSum<uint>();
		Debug(Facility::Misc, Severity::Notice, "FL check company={} vehicle={} type={} connected={} served={} tile={} profit={} delivered={}",
			vehicle->owner, vehicle->index, vehicle->type == VehicleType::Train ? "rail" : (vehicle->type == VehicleType::Aircraft ? "air" : "road"), connected, destinations.contains(vehicle->last_station_visited),
			vehicle->tile, vehicle->profit_this_year, delivered);
	}
	Debug(Facility::Misc, Severity::Notice, "FL check result={} companies={} vehicles={} human={}", valid, Company::GetNumItems(), checked, _local_company);
	return valid;
}
