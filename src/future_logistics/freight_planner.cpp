/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/**
 * @file freight_planner.cpp Day-zero freight routes.
 *
 * A freight route is a cargo, a source, a destination and the legs between them. Each leg is built by
 * one mechanic (a railway, a road, an airport pair, a dock pair). A route is one leg where a mechanic
 * reaches both ends; otherwise it is two legs that meet at a transfer station, where the first leg's
 * vehicles transfer and leave empty and the second leg's vehicles load, exactly as players set up
 * transfers in the base game. Nothing here is specific to a mode of transport.
 */
#include "../stdafx.h"
#include "freight_planner.h"
#include "catalogue.h"
#include "mechanics.h"
#include "scale.h"
#include "../cargotype.h"
#include "../command_func.h"
#include "../company_func.h"
#include "../debug.h"
#include "../industry.h"
#include "../landscape_cmd.h"
#include "../rail_map.h"
#include "../road_map.h"
#include "../settings_type.h"
#include "../signal_func.h"
#include "../station_func.h"
#include "../town.h"
#include "../bridge.h"
#include "../vehicle_base.h"
#include "../vehicle_cmd.h"

#include "../safeguards.h"

namespace FutureLogistics {

TileIndex Centre(const Industry *industry)
{
	return TileAddXY(industry->location.tile, industry->location.w / 2, industry->location.h / 2);
}

TileIndex Place::Centre() const
{
	if (this->industry != nullptr) return FutureLogistics::Centre(this->industry);
	if (this->town != nullptr) return this->town->xy;
	return this->hub;
}

/**
 * Remove a leg: sell its vehicles, then clear what it built in reverse order.
 * @param built What was built.
 */
BridgeSpan FindBridge(TileIndex head, DiagDirection dir)
{
	if (!Buildable(head)) return {};
	const int z = GetTileZ(head);
	TileIndex tile = Step(head, dir);
	for (uint length = 1; length <= MAX_BRIDGE_SPAN; length++) {
		if (tile == INVALID_TILE || GetTileMaxZ(tile) > z) return {};
		const bool spannable = IsTileType(tile, TileType::Water) || IsNormalRoadTile(tile) || IsPlainRailTile(tile)
			|| ((IsTileType(tile, TileType::Clear) || IsTileType(tile, TileType::Trees)) && GetTileMaxZ(tile) < z);
		if (!spannable) return {};
		const TileIndex land = Step(tile, dir);
		if (land != INVALID_TILE && Buildable(land) && GetTileZ(land) == z) return {land, length};
		tile = land;
	}
	return {};
}

BridgeSpan FindTunnel(TileIndex portal, DiagDirection dir)
{
	if (!IsTileType(portal, TileType::Clear) && !IsTileType(portal, TileType::Trees)) return {};
	const auto [slope, z] = GetTileSlopeZ(portal);
	if (!IsInclinedSlope(slope) || GetInclinedSlopeDirection(slope) != dir) return {};
	TileIndex tile = Step(portal, dir);
	for (uint length = 0; length <= MAX_TUNNEL_SPAN && tile != INVALID_TILE; length++) {
		const auto [end_slope, end_z] = GetTileSlopeZ(tile);
		if (end_z == z) {
			/* An exit that needs no terraforming, through at least one tile of hill. */
			if (length == 0 || end_slope != ComplementSlope(slope)) return {};
			if (!IsTileType(tile, TileType::Clear) && !IsTileType(tile, TileType::Trees)) return {};
			return {tile, length};
		}
		if (end_z < z) return {};
		tile = Step(tile, dir);
	}
	return {};
}

BridgeType CheapestBridge(uint length)
{
	BridgeType best = MAX_BRIDGES;
	for (BridgeType type = 0; type < MAX_BRIDGES; type++) {
		if (CheckBridgeAvailability(type, length).Failed()) continue;
		if (best == MAX_BRIDGES || GetBridgeSpec(type)->price < GetBridgeSpec(best)->price) best = type;
	}
	return best;
}

void Undo(const BuiltLeg &built)
{
	for (VehicleID vehicle : built.vehicles) {
		/* The leg's vehicles were started in the depot; only a stopped vehicle can be sold. */
		if (!Vehicle::Get(vehicle)->vehstatus.Test(VehState::Stopped)) Command<Commands::StartStopVehicle>::Do(EXEC, vehicle, true);
		const auto result = Command<Commands::SellVehicle>::Do(EXEC, vehicle, true, false, ClientID::Invalid);
		UpdateSignalsInBuffer();
		if (result.Failed()) {
			Debug(Facility::Misc, Severity::Error, "FL rollback failed selling vehicle={}; infrastructure retained", vehicle);
			return;
		}
	}
	for (auto it = built.tiles.rbegin(); it != built.tiles.rend(); ++it) {
		const auto result = Command<Commands::LandscapeClear>::Do(EXEC, *it);
		UpdateSignalsInBuffer();
		if (result.Failed()) Debug(Facility::Misc, Severity::Error, "FL rollback failed tile={} error={}", *it, result.GetErrorMessage());
	}
}

namespace {
constexpr size_t MAX_FREIGHT_PAIRS = 32; ///< Routes tried per service with a single leg.
constexpr size_t MAX_TRANSFER_PAIRS = 8; ///< Routes tried per service for each way of handing cargo over.
constexpr uint MIN_FREIGHT_DISTANCE = 12; ///< Shortest freight route considered, in Manhattan tiles.
constexpr uint MAX_FREIGHT_DISTANCE = 256; ///< Longest freight route considered, in Manhattan tiles.
constexpr int HUB_RADIUS = 40; ///< How far from the source or destination a transfer station may be.
constexpr int JOIN_RADIUS = 6; ///< How far from a transfer station the next leg's station may be, so it can join it.

/** A freight route candidate: a cargo from a source industry to an industry or town that accepts it. */
struct FreightPair {
	bool chain; ///< The source is a secondary industry that another service supplies.
	size_t preference; ///< Rival's industry market preference; 0 is best.
	uint distance; ///< Manhattan distance between the ends.
	const Industry *source;
	const Industry *destination; ///< Industry that accepts the cargo, or nullptr for a town.
	const Town *town; ///< Town that accepts the cargo, or nullptr for an industry.
	CargoType cargo;
	MarketTier tier; ///< Size of the source's market for the cargo.

	Place From() const { return Place::Of(this->source); }
	Place To() const { return this->destination != nullptr ? Place::Of(this->destination) : Place::Of(this->town); }
};

/**
 * List freight routes, best first. Sources are unused industries that produce by themselves, or
 * secondary industries another freight service already supplies, which makes a cargo chain;
 * destinations are unused industries, or towns, that accept the cargo. Chains come first, then the
 * rival's preferred markets, then shorter routes.
 * @param spec Limits and preferences of the service.
 * @param reserved Markets already taken.
 * @return Candidates in order.
 */
std::vector<FreightPair> FreightPairs(const ServiceSpec &spec, const Reservations &reserved)
{
	const int town_radius = _settings_game.station.modified_catchment ? CA_TRUCK : CA_UNMODIFIED;
	std::vector<FreightPair> pairs;
	for (const Industry *source : Industry::Iterate()) {
		const bool chain = reserved.supplied.contains(source->index);
		if (reserved.industries.contains(source->index) && !chain) continue;
		for (const auto &produced : source->produced) {
			if (!IsValidCargoType(produced.cargo) || CargoSpec::Get(produced.cargo)->classes.Any({CargoClass::Passengers, CargoClass::Mail})) continue;
			/* Secondary industries produce nothing until supplied; vehicles would wait for a full load forever. */
			if (ProductionRate(source, produced.cargo) == 0 && !chain) continue;
			const MarketTier tier = TierForIndustry(source, produced.cargo);
			if (tier < spec.min_tier) continue;
			for (const Industry *destination : Industry::Iterate()) {
				if (destination == source || reserved.industries.contains(destination->index) || !destination->IsCargoAccepted(produced.cargo)) continue;
				const uint distance = DistanceManhattan(source->location.tile, destination->location.tile);
				if (distance < MIN_FREIGHT_DISTANCE || distance > MAX_FREIGHT_DISTANCE) continue;
				const size_t preference = std::min(IndustryPreference(source, spec.industry_markets), IndustryPreference(destination, spec.industry_markets));
				pairs.push_back({chain, preference, distance, source, destination, nullptr, produced.cargo, tier});
			}
			for (const Town *town : Town::Iterate()) {
				const uint distance = DistanceManhattan(source->location.tile, town->xy);
				if (distance < MIN_FREIGHT_DISTANCE || distance > MAX_FREIGHT_DISTANCE) continue;
				if (GetAcceptanceAroundTiles(town->xy, 1, 1, town_radius).first[produced.cargo] < 8) continue;
				pairs.push_back({chain, IndustryPreference(source, spec.industry_markets), distance, source, nullptr, town, produced.cargo, tier});
			}
		}
	}
	std::ranges::stable_sort(pairs, [](const FreightPair &a, const FreightPair &b) {
		if (a.chain != b.chain) return a.chain;
		if (a.preference != b.preference) return a.preference < b.preference;
		return a.distance < b.distance;
	});
	return pairs;
}

/**
 * Record a built route: reserve its industries, mark the destination as supplied so a later service
 * can carry what it makes, and log it.
 * @param pair The route.
 * @param legs Mechanics of the legs, in order, such as "dock_pair>rail_line".
 * @param hub Transfer station, or StationID::Invalid() for a single leg.
 * @param[in,out] reserved Markets already taken.
 */
void Reserve(const FreightPair &pair, std::string_view legs, StationID hub, Reservations &reserved)
{
	reserved.industries.insert(pair.source->index);
	if (pair.destination != nullptr) {
		reserved.industries.insert(pair.destination->index);
		reserved.supplied.insert(pair.destination->index);
	}
	Debug(Facility::Misc, Severity::Notice, "FL industries company={} source={} chain={} destination={}{} cargo={} tier={} legs={} hub={} destination_type={}", _current_company,
		pair.source->index, pair.chain, pair.destination != nullptr ? "industry:" : "town:",
		pair.destination != nullptr ? pair.destination->index.base() : pair.town->index.base(),
		CargoSpec::Get(pair.cargo)->label.AsString(), TierName(pair.tier), legs, hub == StationID::Invalid() ? std::string{"-"} : fmt::format("{}", hub),
		pair.destination != nullptr ? fmt::format("{}", pair.destination->type) : std::string{"town"});
}

/**
 * Build a route in two legs. The main leg is the long one; the other leg links it to the source or
 * the destination through a transfer station. The main leg's transfer station is only chosen where
 * a check shows the other leg can be built from it, and the main leg is removed again if the other
 * leg still fails.
 * @param library Loaded transport technology.
 * @param pair Route.
 * @param main Mechanic of the long leg, with its service.
 * @param spec Service of the main leg.
 * @param other Mechanic of the short leg.
 * @param other_spec Service of the short leg.
 * @param near_destination Whether the transfer station is near the destination (main leg first) or near the source (other leg first).
 * @param[out] hub Transfer station, on success.
 * @param[out] sited Whether both legs found somewhere to go.
 * @return Outcome.
 */
ConstructionResult TryTransfer(const TechLibrary &library, const FreightPair &pair, const MechanicInfo &main, const ServiceSpec &spec,
	const MechanicInfo &other, const ServiceSpec &other_spec, bool near_destination, StationID &hub, bool &sited)
{
	const Place source = pair.From();
	const Place destination = pair.To();
	const LegJob main_job{pair.cargo, pair.tier, near_destination};
	const LegJob other_job{pair.cargo, pair.tier, !near_destination};

	/* The other leg, joined to a transfer station at a given tile. */
	auto build_other = [&](TileIndex tile, StationID join, bool execute, BuiltLeg &built) {
		Place station;
		station.hub = tile;
		station.radius = JOIN_RADIUS;
		station.join = join;
		return near_destination ? other.leg(library, other_spec, station, destination, other_job, execute, built)
			: other.leg(library, other_spec, source, station, other_job, execute, built);
	};
	Place station;
	station.hub = near_destination ? destination.Centre() : source.Centre();
	station.radius = HUB_RADIUS;
	station.usable = [&](TileIndex tile) {
		BuiltLeg check;
		return build_other(tile, NEW_STATION, false, check) == ConstructionResult::Success;
	};

	BuiltLeg main_built;
	ConstructionResult result = near_destination ? main.leg(library, spec, source, station, main_job, true, main_built)
		: main.leg(library, spec, station, destination, main_job, true, main_built);
	sited = main_built.sited;
	if (result != ConstructionResult::Success) return result;

	hub = near_destination ? main_built.to : main_built.from;
	BuiltLeg other_built;
	result = build_other(near_destination ? main_built.to_tile : main_built.from_tile, hub, true, other_built);
	if (result != ConstructionResult::Success) {
		Debug(Facility::Misc, Severity::Warning, "FL transfer leg failed company={} hub={}; removing the first leg", _current_company, hub);
		Undo(main_built);
		return ConstructionResult::Failed;
	}
	return ConstructionResult::Success;
}
} // namespace

/**
 * Build one freight route for the current company, led by one mechanic. Routes that mechanic can run
 * alone come first. If there are none, the route hands its cargo over at a transfer station to or from
 * another way the rival carries freight: near the destination (ships to a port, then a train inland),
 * or near the source (trucks from an inland mine to a railway).
 * @param library Loaded transport technology.
 * @param spec Family, limits and preferences of the service; its partners are the rival's other freight mechanics.
 * @param mechanic Mechanic that leads the route.
 * @param[in,out] reserved Markets already taken; the route's industries are added on success.
 * @return Outcome.
 */
ConstructionResult BuildFreight(const TechLibrary &library, const ServiceSpec &spec, std::string_view mechanic, Reservations &reserved)
{
	const MechanicInfo *main = FindMechanic(mechanic);
	if (main == nullptr || main->leg == nullptr) return ConstructionResult::Rejected;
	const std::vector<FreightPair> pairs = FreightPairs(spec, reserved);
	Debug(Facility::Misc, Severity::Notice, "FL freight company={} lead={} candidates={}", _current_company, main->name, pairs.size());
	size_t preferred = 0;
	for (size_t i = 0; i < pairs.size(); i++) {
		/* The first few, and the first few in the rival's preferred markets. */
		if (i >= 5 && (pairs[i].preference != 0 || preferred++ >= 3)) continue;
		Debug(Facility::Misc, Severity::Debug1, "FL freight candidate company={} rank={} source={} destination={}{} cargo={} distance={} chain={} preference={}", _current_company, i,
			pairs[i].source->index, pairs[i].destination != nullptr ? "industry:" : "town:",
			pairs[i].destination != nullptr ? pairs[i].destination->index.base() : pairs[i].town->index.base(),
			CargoSpec::Get(pairs[i].cargo)->label.AsString(), pairs[i].distance, pairs[i].chain, pairs[i].preference);
	}

	size_t tried = 0;
	for (const FreightPair &pair : pairs) {
		if (tried == MAX_FREIGHT_PAIRS) break;
		if (pair.distance < main->min_distance || pair.distance > main->max_distance) continue;
		BuiltLeg built;
		const auto result = main->leg(library, spec, pair.From(), pair.To(), {pair.cargo, pair.tier, false}, true, built);
		if (built.sited) tried++;
		if (result == ConstructionResult::Rejected) continue;
		if (result == ConstructionResult::Success) Reserve(pair, main->name, StationID::Invalid(), reserved);
		return result;
	}

	for (const Partner &partner : spec.partners) {
		const MechanicInfo *other = FindMechanic(partner.mechanic);
		if (other == nullptr || other->leg == nullptr || other == main) continue;
		const ServiceSpec other_spec{partner.family, partner.min_tier, partner.early_adopters, spec.industry_markets, {}};
		for (bool near_destination : {true, false}) {
			tried = 0;
			for (const FreightPair &pair : pairs) {
				if (tried == MAX_TRANSFER_PAIRS) break;
				if (pair.distance < main->min_distance || pair.distance > main->max_distance + HUB_RADIUS) continue;
				StationID hub = StationID::Invalid();
				bool sited = false;
				const auto result = TryTransfer(library, pair, *main, spec, *other, other_spec, near_destination, hub, sited);
				if (sited) tried++;
				if (result == ConstructionResult::Rejected) continue;
				if (result == ConstructionResult::Success) {
					const std::string legs = near_destination ? fmt::format("{}>{}", main->name, other->name) : fmt::format("{}>{}", other->name, main->name);
					Reserve(pair, legs, hub, reserved);
				}
				return result;
			}
		}
	}
	return ConstructionResult::Rejected;
}

} // namespace FutureLogistics
