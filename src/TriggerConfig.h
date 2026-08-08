#pragma once

#include "ConfigFile.h"
#include "Conditions.h"

namespace RPP
{
	// One row: "if a recipe requires this material, add this condition to
	// it." materialID identifies a form using EITHER a Creation Kit
	// EditorID (e.g. "IngotEbony") or a "FormID~PluginName" pair (e.g.
	// "0x5AD99~Skyrim.esm"). See ResolveIdentifier() in Identifiers.h for
	// the format detection logic.
	//
	// A single material can appear in multiple TriggerEntry rows (e.g. to
	// add more than one condition for the same material); each row adds
	// its own condition independently.
	struct TriggerEntry
	{
		std::string materialID;
		ConditionSpec condition;
		std::string comment;  // purely for humans reading the json, unused at runtime
	};

	// Loads Data/SKSE/Plugins/RecipeConditionPatcher.json plus any
	// Data/SKSE/Plugins/*_RCP.json files (see
	// ConfigFile::FindExternalConfigPaths), merging their "mappings"
	// arrays together.
	std::vector<TriggerEntry> LoadTriggerEntries();

	// Same as above, but works from already-parsed config(s) instead of
	// reading files itself. Use this when a caller (like
	// RecipePatcher.cpp) also needs LoadRecipeOverrides() in the same
	// pass, so the shared config file is only read/parsed once instead of
	// once per loader. a_externalConfigs are additional mod-author config
	// files (see ConfigFile::ReadExternalConfigs); only their "mappings"
	// arrays are used. Passing just a_mainConfig (leaving
	// a_externalConfigs at its default) reads only the main config's own
	// mappings, which is what the in-game editor tab uses, since it only
	// shows/edits the main config file.
	std::vector<TriggerEntry> LoadTriggerEntries(
		const nlohmann::ordered_json& a_mainConfig,
		const std::vector<ConfigFile::ExternalConfig>& a_externalConfigs = {});

	// Overwrites the "mappings" array in a_path with the given list
	// (read-modify-write: every other top-level key, and any "//" comment
	// keys, are left untouched). a_path may be the main config or any
	// external *_RCP.json; the in-game editor can target either.
	void SaveUserTriggerEntries(const std::vector<TriggerEntry>& a_entries, const std::string& a_path);
}
