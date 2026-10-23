/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file tech_console.cpp Console access to the Future Logistics Tech Library. */
#include "../stdafx.h"
#include "tech_library.h"
#include "catalogue.h"
#include "tag_index.h"
#include "../company_func.h"
#include "../console_func.h"
#include "../debug.h"
#include "../engine_base.h"
#include "../engine_func.h"
#include "../newgrf_badge.h"
#include "../strings_func.h"

#include "table/strings.h"

#include "../safeguards.h"

namespace {
/**
 * Print to the console and the misc debug log, so headless test runs can read it.
 * @param line Text to print.
 */
void Report(const std::string &line)
{
	IConsolePrint(CC_DEFAULT, line);
	Debug(Facility::Misc, Severity::Notice, "{}", line);
}

/**
 * Describe a vehicle record on one line. The name goes last because it contains spaces.
 * @param record Record to describe.
 * @return Formatted line.
 */
std::string FormatVehicle(const TechRecord &record)
{
	const std::string cargo = IsValidCargoType(record.cargo) && record.capacity > 0 ? CargoSpec::Get(record.cargo)->label.AsString() : "-";
	const bool buildable = IsEngineBuildable(record.engine, Engine::Get(record.engine)->type, _local_company);
	std::string power;
	for (const std::string &source : record.power) power += (power.empty() ? "" : ",") + source;
	return fmt::format("id={} mode={} track={} cargo={} capacity={} speed={} roles={}{} power={} tech={} tier={} wagon={} buildable={} name={}",
		record.id, TechModeName(record.mode), record.track.empty() ? "-" : record.track, cargo, record.capacity, record.speed,
		FormatTechRoles(record.roles), record.declared_roles ? "(declared)" : "", power.empty() ? "-" : power, record.tech, record.tier, record.wagon, buildable,
		GetString(STR_ENGINE_NAME, record.engine));
}

/**
 * Describe an infrastructure record on one line.
 * @param record Record to describe.
 * @return Formatted line.
 */
std::string FormatInfra(const InfraRecord &record)
{
	static constexpr std::array<std::string_view, 4> KINDS{"railtype", "roadtype", "tramtype", "airport"};
	return fmt::format("FL tech infra kind={} id={} speed={} size={}x{}", KINDS[to_underlying(record.kind)], record.id, record.speed, record.size_x, record.size_y);
}

/**
 * Find a cargo by its four letter label.
 * @param label Label such as "PASS".
 * @return Cargo type, or INVALID_CARGO.
 */
CargoType ParseCargo(std::string_view label)
{
	for (const CargoSpec *spec : CargoSpec::Iterate()) {
		if (spec->label.AsString() == label) return spec->Index();
	}
	return INVALID_CARGO;
}
} // namespace

/**
 * Run the future_logistics_tech console command.
 * @param argv Arguments after the command name.
 * @return Whether the arguments were valid.
 */
bool FutureLogisticsTechCommand(std::span<std::string_view> argv)
{
	if (argv.size() == 1 && argv[0] == "list") {
		const TechLibrary library = TechLibrary::Build();
		for (const TechRecord &record : library.Vehicles()) Report("FL tech vehicle " + FormatVehicle(record));
		for (const InfraRecord &record : library.Infrastructure()) Report(FormatInfra(record));
		Report(fmt::format("FL tech summary vehicles={} infrastructure={}", library.Vehicles().size(), library.Infrastructure().size()));
		return true;
	}
	if (argv.size() == 1 && argv[0] == "badges") {
		/* Every fl_ badge any loaded NewGRF declared, whether or not an item uses it. */
		for (const Badge &badge : GetBadges()) {
			if (!badge.label.starts_with("fl_")) continue;
			Report(fmt::format("FL tech badge label={} name={}", badge.label, badge.name == StringID{} ? std::string{"-"} : GetString(badge.name)));
		}
		for (const Engine *engine : Engine::Iterate()) {
			std::string labels;
			for (BadgeID id : engine->badges) {
				const Badge *badge = GetBadge(id);
				if (badge == nullptr || !badge->label.starts_with("fl_")) continue;
				if (!labels.empty()) labels += ',';
				labels += badge->label;
			}
			if (!labels.empty()) Report(fmt::format("FL tech tagged engine={} badges={} name={}", engine->index, labels, GetString(STR_ENGINE_NAME, engine->index)));
		}
		return true;
	}
	if (argv.size() == 1 && argv[0] == "recipes") {
		using namespace FutureLogistics;
		const TechLibrary library = TechLibrary::Build();
		const Catalogue catalogue = Catalogue::Build(library);
		for (const Family &family : catalogue.Families()) {
			std::string recipes;
			for (const auto &[mechanic, market] : family.recipes) recipes += fmt::format("{}{}/{}", recipes.empty() ? "" : ",", mechanic, MarketName(market));
			const auto since = family.key == CONVENTIONAL ? std::optional<TimerGameCalendar::Date>{} : FamilyAvailableSince(library, family.key);
			Report(fmt::format("FL tech family key={} declared={} min_tier={} available={} recipes={} name={}", family.key, family.declared, TierName(family.min_tier),
				family.key == CONVENTIONAL ? "always" : (since.has_value() ? fmt::format("{}", TimerGameCalendar::ConvertDateToYMD(*since).year) : "never"),
				recipes.empty() ? "-" : recipes, family.name));
		}
		for (const Profile &profile : catalogue.Profiles()) {
			std::string plan;
			for (const Recipe &recipe : catalogue.Plan(profile, library, TimerGameCalendar::date)) plan += (plan.empty() ? "" : ",") + recipe.Key();
			Report(fmt::format("FL tech profile id={} key={} builtin={} rules={} plan={} name={}", profile.id, profile.key, profile.builtin, profile.rules.size(), plan.empty() ? "-" : plan, profile.name));
		}
		return true;
	}
	if (argv.size() == 1 && argv[0] == "validate") {
		using namespace FutureLogistics;
		const TechLibrary library = TechLibrary::Build();
		const Catalogue catalogue = Catalogue::Build(library);
		std::vector<std::string> problems = library.Problems();
		problems.insert(problems.end(), catalogue.Problems().begin(), catalogue.Problems().end());
		/* Code finds items by fl_id, so one id must not name two items of the same kind. */
		std::map<std::pair<std::string, TaggedKind>, uint> uses;
		for (const TaggedItem &item : CollectTaggedItems()) uses[{item.id, item.kind}]++;
		for (const auto &[key, count] : uses) {
			if (count > 1) problems.push_back(fmt::format("fl_id/{} is used by {} items of kind {}", key.first, count, TaggedKindName(key.second)));
		}
		for (const std::string &problem : problems) Report("FL tech problem " + problem);
		Report(fmt::format("FL tech validate problems={}", problems.size()));
		return true;
	}
	if (argv.size() == 2 && argv[0] == "find") {
		using namespace FutureLogistics;
		const auto items = FindTagged(argv[1]);
		for (const TaggedItem &item : items) Report(fmt::format("FL tech found id={} kind={} index={}", item.id, TaggedKindName(item.kind), item.index));
		Report(fmt::format("FL tech find id={} matches={}", argv[1], items.size()));
		return true;
	}
	if ((argv.size() == 3 || argv.size() == 4) && argv[0] == "best") {
		const auto mode = ParseTechMode(argv[1]);
		const auto role = ParseTechRole(argv[2]);
		const CargoType cargo = argv.size() == 4 ? ParseCargo(argv[3]) : INVALID_CARGO;
		if (!mode.has_value() || !role.has_value() || (argv.size() == 4 && !IsValidCargoType(cargo))) return false;
		const TechLibrary library = TechLibrary::Build();
		TechQuery query;
		query.mode = *mode;
		query.role = *role;
		query.cargo = cargo;
		query.company = _local_company;
		const TechRecord *best = library.FindBest(query);
		Report(best == nullptr ? fmt::format("FL tech best mode={} role={} none", argv[1], argv[2]) : "FL tech best " + FormatVehicle(*best));
		return true;
	}
	return false;
}
