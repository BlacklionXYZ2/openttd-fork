/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/**
 * @file tag_index.h Find NewGRF items by their fl_id tag.
 *
 * Code that needs one particular item, such as a mechanic that must build "the launchpad", looks it up by the
 * stable name the NewGRF gives it with fl_id/<name>, never by a load-order index or a GRF-local ID.
 */
#ifndef FUTURE_LOGISTICS_TAG_INDEX_H
#define FUTURE_LOGISTICS_TAG_INDEX_H

#include "../engine_type.h"
#include "../house_type.h"
#include "../industry_type.h"
#include "../object_type.h"
#include "../rail_type.h"
#include "../road_type.h"

struct StationSpec;
struct RoadStopSpec;

namespace FutureLogistics {

/** Kinds of item that can carry an fl_id. */
enum class TaggedKind : uint8_t {
	Vehicle,
	RailType,
	RoadType,
	Airport,
	Industry,
	Object,
	House,
	Station,
	RoadStop,
	End,
};

/** One item carrying an fl_id. */
struct TaggedItem {
	TaggedKind kind; ///< What sort of item it is.
	uint index; ///< Engine ID, rail or road type, airport type, industry type, object type or house ID; for stations and road stops, the position in the scan.
	std::string id; ///< Its fl_id name.
	const StationSpec *station = nullptr; ///< The station spec, for TaggedKind::Station.
	const RoadStopSpec *roadstop = nullptr; ///< The road stop spec, for TaggedKind::RoadStop.
};

std::string_view TaggedKindName(TaggedKind kind);
std::vector<TaggedItem> CollectTaggedItems();
std::vector<TaggedItem> FindTagged(std::string_view id);
std::optional<EngineID> FindEngineById(std::string_view id);
std::optional<RailType> FindRailTypeById(std::string_view id);
std::optional<RoadType> FindRoadTypeById(std::string_view id);
std::optional<uint8_t> FindAirportById(std::string_view id);
std::optional<IndustryType> FindIndustryTypeById(std::string_view id);
std::optional<ObjectType> FindObjectById(std::string_view id);
std::optional<HouseID> FindHouseById(std::string_view id);
const StationSpec *FindStationSpecById(std::string_view id);
const RoadStopSpec *FindRoadStopSpecById(std::string_view id);

} // namespace FutureLogistics

#endif /* FUTURE_LOGISTICS_TAG_INDEX_H */
