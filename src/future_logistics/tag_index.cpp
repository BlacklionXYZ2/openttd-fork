/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file tag_index.cpp Find NewGRF items by their fl_id tag. */
#include "../stdafx.h"
#include "tag_index.h"
#include "tech_library.h"
#include "../engine_base.h"
#include "../house.h"
#include "../industrytype.h"
#include "../newgrf_airport.h"
#include "../newgrf_object.h"
#include "../newgrf_roadstop.h"
#include "../newgrf_station.h"
#include "../rail.h"
#include "../road.h"

#include "../safeguards.h"

namespace FutureLogistics {
namespace {
constexpr std::array<std::string_view, to_underlying(TaggedKind::End)> KIND_NAMES{"vehicle", "railtype", "roadtype", "airport", "industry", "object", "house", "station", "roadstop"};

/**
 * Add an item to the list if its badges include a usable fl_id.
 * @param items List to add to.
 * @param kind Kind of item.
 * @param index Its index.
 * @param badges Its badges.
 * @return The added item, or nullptr if it has no fl_id.
 */
TaggedItem *Add(std::vector<TaggedItem> &items, TaggedKind kind, uint index, std::span<const BadgeID> badges)
{
	const ItemTags tags = ReadItemTags(badges);
	if (!tags.formatted || !tags.id.has_value()) return nullptr;
	return &items.emplace_back(TaggedItem{kind, index, *tags.id});
}

/**
 * Find the single item of one kind with an id.
 * @param id fl_id name.
 * @param kind Kind wanted.
 * @return The item, or nullptr if none or several have it.
 */
std::optional<TaggedItem> FindOne(std::string_view id, TaggedKind kind)
{
	std::optional<TaggedItem> found;
	for (const TaggedItem &item : FindTagged(id)) {
		if (item.kind != kind) continue;
		/* Two items of one kind with one id is a NewGRF mistake, reported by validate; refuse to guess. */
		if (found.has_value()) return std::nullopt;
		found = item;
	}
	return found;
}
} // namespace

std::string_view TaggedKindName(TaggedKind kind)
{
	return KIND_NAMES[to_underlying(kind)];
}

/**
 * Collect every loaded item that carries an fl_id.
 * @return Items in a fixed order: vehicles, track and road types, airports, industries, objects, houses, stations, road stops.
 */
std::vector<TaggedItem> CollectTaggedItems()
{
	std::vector<TaggedItem> items;
	for (const Engine *engine : Engine::Iterate()) {
		if (engine->IsEnabled()) Add(items, TaggedKind::Vehicle, engine->index.base(), engine->badges);
	}
	for (uint i = RAILTYPE_BEGIN; i < RAILTYPE_END; i++) {
		const RailType rt = static_cast<RailType>(i);
		const RailTypeInfo *rti = GetRailTypeInfo(rt);
		if (!rti->label.Empty()) Add(items, TaggedKind::RailType, rt, rti->badges);
	}
	for (uint i = ROADTYPE_BEGIN; i < ROADTYPE_END; i++) {
		const RoadType rt = static_cast<RoadType>(i);
		const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
		if (!rti->label.Empty()) Add(items, TaggedKind::RoadType, rt, rti->badges);
	}
	for (uint8_t type = 0; type < NUM_AIRPORTS; type++) {
		const AirportSpec *spec = AirportSpec::Get(type);
		if (spec->enabled) Add(items, TaggedKind::Airport, type, spec->badges);
	}
	for (IndustryType type = 0; type < NUM_INDUSTRYTYPES; type++) {
		const IndustrySpec *spec = GetIndustrySpec(type);
		if (spec->enabled) Add(items, TaggedKind::Industry, type, spec->badges);
	}
	for (size_t type = 0; type < ObjectSpec::Count(); type++) {
		const ObjectSpec &spec = ObjectSpec::Specs()[type];
		if (spec.IsEnabled()) Add(items, TaggedKind::Object, static_cast<uint>(type), spec.badges);
	}
	for (size_t house = 0; house < HouseSpec::Specs().size(); house++) {
		const HouseSpec &spec = HouseSpec::Specs()[house];
		if (spec.enabled) Add(items, TaggedKind::House, static_cast<uint>(house), spec.badges);
	}
	uint position = 0;
	for (uint c = 0; c < StationClass::GetClassCount(); c++) {
		const StationClass *cls = StationClass::Get(StationClassID(c));
		for (uint i = 0; i < cls->GetSpecCount(); i++) {
			const StationSpec *spec = cls->GetSpec(i);
			if (spec == nullptr) continue;
			if (TaggedItem *item = Add(items, TaggedKind::Station, position, spec->badges); item != nullptr) item->station = spec;
			position++;
		}
	}
	position = 0;
	for (uint c = 0; c < RoadStopClass::GetClassCount(); c++) {
		const RoadStopClass *cls = RoadStopClass::Get(RoadStopClassID(c));
		for (uint i = 0; i < cls->GetSpecCount(); i++) {
			const RoadStopSpec *spec = cls->GetSpec(i);
			if (spec == nullptr) continue;
			if (TaggedItem *item = Add(items, TaggedKind::RoadStop, position, spec->badges); item != nullptr) item->roadstop = spec;
			position++;
		}
	}
	return items;
}

/**
 * Find every loaded item with an fl_id.
 * @param id Name given with fl_id/<name>.
 * @return Matching items of any kind.
 */
std::vector<TaggedItem> FindTagged(std::string_view id)
{
	std::vector<TaggedItem> found;
	for (TaggedItem &item : CollectTaggedItems()) {
		if (item.id == id) found.push_back(std::move(item));
	}
	return found;
}

std::optional<EngineID> FindEngineById(std::string_view id)
{
	const auto item = FindOne(id, TaggedKind::Vehicle);
	return item.has_value() ? std::optional<EngineID>{EngineID(item->index)} : std::nullopt;
}

std::optional<RailType> FindRailTypeById(std::string_view id)
{
	const auto item = FindOne(id, TaggedKind::RailType);
	return item.has_value() ? std::optional<RailType>{static_cast<RailType>(item->index)} : std::nullopt;
}

std::optional<RoadType> FindRoadTypeById(std::string_view id)
{
	const auto item = FindOne(id, TaggedKind::RoadType);
	return item.has_value() ? std::optional<RoadType>{static_cast<RoadType>(item->index)} : std::nullopt;
}

std::optional<uint8_t> FindAirportById(std::string_view id)
{
	const auto item = FindOne(id, TaggedKind::Airport);
	return item.has_value() ? std::optional<uint8_t>{static_cast<uint8_t>(item->index)} : std::nullopt;
}

std::optional<IndustryType> FindIndustryTypeById(std::string_view id)
{
	const auto item = FindOne(id, TaggedKind::Industry);
	return item.has_value() ? std::optional<IndustryType>{static_cast<IndustryType>(item->index)} : std::nullopt;
}

std::optional<ObjectType> FindObjectById(std::string_view id)
{
	const auto item = FindOne(id, TaggedKind::Object);
	return item.has_value() ? std::optional<ObjectType>{static_cast<ObjectType>(item->index)} : std::nullopt;
}

std::optional<HouseID> FindHouseById(std::string_view id)
{
	const auto item = FindOne(id, TaggedKind::House);
	return item.has_value() ? std::optional<HouseID>{static_cast<HouseID>(item->index)} : std::nullopt;
}

const StationSpec *FindStationSpecById(std::string_view id)
{
	const auto item = FindOne(id, TaggedKind::Station);
	return item.has_value() ? item->station : nullptr;
}

const RoadStopSpec *FindRoadStopSpecById(std::string_view id)
{
	const auto item = FindOne(id, TaggedKind::RoadStop);
	return item.has_value() ? item->roadstop : nullptr;
}

} // namespace FutureLogistics
