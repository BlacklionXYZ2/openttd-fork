/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file profiles.h Day-zero company profiles: who the rivals are and what each one runs. */
#ifndef FUTURE_LOGISTICS_PROFILES_H
#define FUTURE_LOGISTICS_PROFILES_H

namespace FutureLogistics {

/** A kind of starter service a planner can build. */
enum class ServiceType : uint8_t {
	Bus, ///< Town-to-town bus.
	RailPassenger, ///< Town-to-town passenger train.
	RailFreight, ///< Industry-to-industry freight train.
};

/** One rival company's character. */
struct CompanyProfile {
	uint8_t id; ///< Stored in the CorporateAI "profile" setting; 0 means none.
	std::string_view key; ///< Name used in logs.
	std::string_view name; ///< Company name shown in game.
	std::vector<ServiceType> services; ///< Services to try, each independently. A bus is the fallback if none succeed.
};

std::span<const CompanyProfile> GetCompanyProfiles();
std::string_view ServiceName(ServiceType type);

} // namespace FutureLogistics

#endif /* FUTURE_LOGISTICS_PROFILES_H */
