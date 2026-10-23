/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file profiles.cpp Day-zero company profiles. */
#include "../stdafx.h"
#include "profiles.h"

#include "../safeguards.h"

namespace FutureLogistics {

/**
 * The rivals, in the order companies are created. Air (AeroLink) arrives in phase 3 and
 * trucks (Integrated Logistics) in phase 4; until then those companies use what exists.
 * @return All profiles; company N uses profile (N - 1) modulo the count.
 */
std::span<const CompanyProfile> GetCompanyProfiles()
{
	static const std::array<CompanyProfile, 4> profiles{{
		{1, "national_rail", "National Rail & Freight", {ServiceType::RailFreight, ServiceType::RailPassenger}},
		{2, "aerolink", "AeroLink", {ServiceType::Bus}},
		{3, "metro_regional", "Metro & Regional", {ServiceType::RailPassenger, ServiceType::Bus}},
		{4, "integrated_logistics", "Integrated Logistics", {ServiceType::RailFreight}},
	}};
	return profiles;
}

/**
 * Get the log name of a service type.
 * @param type Service type.
 * @return Name such as "rail_freight".
 */
std::string_view ServiceName(ServiceType type)
{
	switch (type) {
		case ServiceType::Bus: return "bus";
		case ServiceType::RailPassenger: return "rail_passenger";
		case ServiceType::RailFreight: return "rail_freight";
	}
	NOT_REACHED();
}

} // namespace FutureLogistics
