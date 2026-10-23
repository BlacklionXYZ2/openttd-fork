/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file bootstrap.cpp Bounded, deterministic town-to-town bus network bootstrap. */
#include "../stdafx.h"
#include "bootstrap.h"
#include "../ai/ai.hpp"
#include "../cargotype.h"
#include "../command_func.h"
#include "../company_base.h"
#include "../core/backup_type.hpp"
#include "../debug.h"
#include "../engine_base.h"
#include "../engine_func.h"
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
#include "../station_cmd.h"
#include "../station_func.h"
#include "../station_map.h"
#include "../town.h"
#include "../vehicle_cmd.h"
#include <queue>
#include <map>
#include <set>

#include "../safeguards.h"

extern Company *DoStartupNewCompany(bool is_ai, CompanyID company);

namespace {
constexpr DoCommandFlags EXEC{DoCommandFlag::Execute};
constexpr DoCommandFlags CONSTRUCT{DoCommandFlag::Auto, DoCommandFlag::NoWater};
constexpr size_t MAX_SEARCH = 8192;
constexpr size_t MAX_PAIRS = 128;

/** No demolition of houses, shared roads, water, bridges or slopes in this first version. */
bool Buildable(TileIndex tile)
{
	return IsValidTile(tile) && (IsTileType(tile, TileType::Clear) || IsTileType(tile, TileType::Trees)) && IsTileFlat(tile);
}

DiagDirection RouteDirection(TileIndex from, TileIndex to)
{
	if (TileX(to) > TileX(from)) return DiagDirection::SW;
	if (TileX(to) < TileX(from)) return DiagDirection::NE;
	if (TileY(to) > TileY(from)) return DiagDirection::SE;
	return DiagDirection::NW;
}

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

std::vector<TileIndex> Neighbours(TileIndex tile)
{
	std::vector<TileIndex> result;
	const int x = TileX(tile), y = TileY(tile);
	/* Fixed tie-breaking order; never use InteractiveRandom or wall time. */
	for (const auto &[dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
		if (x + dx > 0 && y + dy > 0 && x + dx < (int)Map::SizeX() - 1 && y + dy < (int)Map::SizeY() - 1) {
			result.push_back(TileXY(x + dx, y + dy));
		}
	}
	return result;
}

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

struct Route {
	std::vector<TileIndex> path;
	TileIndex depot = INVALID_TILE;
	size_t junction = 0;
};

/** Multi-source BFS, with a hard exploration limit for each town pair. */
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

CommandCost Stop(TileIndex tile, DiagDirection direction, RoadType roadtype, DoCommandFlags flags)
{
	return Command<Commands::BuildRoadStop>::Do(flags | CONSTRUCT, tile, 1, 1, RoadStopType::Bus, true, direction,
		roadtype, ROADSTOP_CLASS_DFLT, 0, NEW_STATION, true);
}

enum class ConstructionResult { Rejected, Failed, Success };

/** Preflight the entire layout, then execute through the engine's accounting command interface. */
ConstructionResult Build(const Route &route, const Engine *engine)
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
	auto result = Command<Commands::BuildVehicle>::Do(EXEC, route.depot, engine->index, false, engine->GetDefaultCargoType(), ClientID::Invalid);
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

bool Network(CargoType cargo, std::set<TownID> &used_towns)
{
	const Engine *bus = nullptr;
	for (const Engine *engine : Engine::IterateType(VehicleType::Road)) {
		if (engine->GetDefaultCargoType() == cargo && engine->CanCarryCargo() && engine->VehInfo<RoadVehicleInfo>().roadtype == ROADTYPE_ROAD && IsEngineBuildable(engine->index, VehicleType::Road, _current_company)) {
			bus = engine;
			break;
		}
	}
	if (bus == nullptr) return false;
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
		auto result = Build(route, bus);
		if (result == ConstructionResult::Failed) return false;
		if (result == ConstructionResult::Success) {
			used_towns.insert(pair.a->index);
			used_towns.insert(pair.b->index);
			Debug(Facility::Misc, Severity::Notice, "FL towns company={} a={} b={}", _current_company, pair.a->index, pair.b->index);
			return true;
		}
	}
	return false;
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
	const CargoType cargo = GetCargoTypeByLabel(CT_PASSENGERS);
	uint operational = 0;
	std::set<TownID> used_towns;
	for (uint i = 1; i <= count; i++) {
		Company *company = DoStartupNewCompany(true, CompanyID{static_cast<uint8_t>(i)});
		if (company == nullptr) { Debug(Facility::Misc, Severity::Warning, "FL company allocation failed slot={}", i); break; }
		/* StartNew initializes a VM; no AI GameLoop runs in this synchronous callback. */
		AI::Pause(company->index);
		{
			AutoRestoreBackup owner(_current_company, OWNER_DEITY);
			auto result = Command<Commands::ChangeBankBalance>::Do(EXEC, TileIndex{0}, (Money)_settings_game.game_creation.bootstrap_capital, company->index, ExpensesType::Other);
			if (result.Failed()) {
				Debug(Facility::Misc, Severity::Error, "FL funding failed company={}", company->index);
				AI::Unpause(company->index);
				continue;
			}
		}
		Debug(Facility::Misc, Severity::Notice, "FL funded company={} grant={} balance={} loan={}", company->index,
			_settings_game.game_creation.bootstrap_capital, company->money, company->current_loan);
		{
			AutoRestoreBackup owner(_current_company, company->index);
			if (cargo != INVALID_CARGO && Network(cargo, used_towns)) {
				operational++;
			} else {
				Debug(Facility::Misc, Severity::Warning, "FL no usable bus route company={} (terrain, towns, funds or vehicle availability); funded company retained", company->index);
			}
		}
		AI::Unpause(company->index);
	}
	Debug(Facility::Misc, Severity::Notice, "FL complete companies={} operational={}", Company::GetNumItems(), operational);
}

bool InspectFutureLogistics()
{
	bool valid = Company::IsValidID(CompanyID{0}) && !Company::Get(CompanyID{0})->is_ai && _local_company == CompanyID{0};
	uint checked = 0;
	for (const Company *company : Company::Iterate()) {
		Debug(Facility::Misc, Severity::Notice, "FL check funds company={} balance={} loan={} road_bits={} station_tiles={} other_expense={} ai={}", company->index, company->money, company->current_loan, company->infrastructure.GetRoadTotal(), company->infrastructure.station, company->yearly_expenses[0][ExpensesType::Other], company->is_ai);
	}
	for (const Vehicle *vehicle : Vehicle::Iterate()) {
		if (vehicle->type != VehicleType::Road || !vehicle->IsPrimaryVehicle() || vehicle->owner == CompanyID{0}) continue;
		bool connected = Company::IsValidAiID(vehicle->owner) && vehicle->GetNumOrders() == 2 && !vehicle->vehstatus.Test(VehState::Stopped);
		std::set<StationID> destinations;
		for (const Order &order : vehicle->Orders()) {
			if (!order.IsType(OT_GOTO_STATION)) { connected = false; continue; }
			destinations.insert(order.GetDestination().ToStationID());
		}
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
		connected &= destinations.size() == 2 && std::ranges::includes(reached, destinations);
		valid &= connected;
		checked++;
		uint delivered = 0;
		CargoType passenger = GetCargoTypeByLabel(CT_PASSENGERS);
		if (passenger != INVALID_CARGO) {
			const Company *company = Company::Get(vehicle->owner);
			delivered = company->cur_economy.delivered_cargo[passenger];
			for (const auto &quarter : company->old_economy) delivered += quarter.delivered_cargo[passenger];
		}
		Debug(Facility::Misc, Severity::Notice, "FL check company={} vehicle={} connected={} served={} tile={} profit={} delivered={}",
			vehicle->owner, vehicle->index, connected, destinations.contains(vehicle->last_station_visited), vehicle->tile, vehicle->profit_this_year, delivered);
	}
	Debug(Facility::Misc, Severity::Notice, "FL check result={} companies={} vehicles={} human={}", valid, Company::GetNumItems(), checked, _local_company);
	return valid;
}
