#pragma once

#include "ConfigFile.h"
#include "Conditions.h"
#include "Settings.h"

namespace RPP
{
	// A targeted override for one specific recipe, identified the same way
	// materials are (EditorID or FormID~PluginName). Two independent
	// things this can do, layered on top of (not instead of) the normal
	// material-based scanning:
	//   - conditions: force these specific conditions onto this specific
	//     recipe, regardless of whether any of its materials trigger them
	//     via the mappings table. A recipe with a non-empty conditions
	//     list is handled EXCLUSIVELY by it. Material-based mapping
	//     scanning is skipped entirely for that recipe (see
	//     RecipePatcher.cpp), so these are the only conditions this
	//     plugin adds for it.
	//   - exclude: skip this recipe entirely, no material-based
	//     scanning, no forced conditions, nothing.
	//
	// When more than one source (the main config, and/or one or more
	// external *_RCP.json files) has an entry for the SAME recipe, they
	// chain together in a strict processing order: main config first,
	// then external files in alphabetical order by filename (so
	// "a_RCP.json" is processed, and superseded, before "b_RCP.json").
	// Each entry's own mode decides how it combines with whatever came
	// before it in that chain for this recipe:
	//   - mode = Add to Existing: appends this entry's conditions onto
	//     whatever earlier entries in the chain already established.
	//   - mode = Replace Existing: discards everything the chain
	//     established so far for this recipe (including whether an
	//     earlier entry wanted to preserve the recipe's own vanilla/
	//     other-mod conditions) and starts fresh from this entry alone.
	//   - exclude: also discards everything so far and marks the recipe
	//     excluded, but a LATER entry in the chain with real conditions
	//     still un-excludes it and starts fresh, since later always wins.
	// Whichever behavior wins by the end of the chain is what actually
	// gets applied. See RecipePatcher.cpp's ResolveRecipeOverrides.
	struct RecipeOverride
	{
		std::string recipeID;
		std::vector<ConditionSpec> conditions;
		bool exclude = false;
		int mode = ExistingPerkMode::kAdd;
		std::string comment;  // purely for humans reading the json, unused at runtime

		// Which file this entry was read from (the main config's path, or
		// an external *_RCP.json file's path); not read from or written
		// to the JSON itself, just tracked in memory for log messages
		// when the chaining behavior above actually does something
		// worth explaining.
		std::string sourceFile;
	};

	// Loads the "recipeOverrides" array from the shared config file, plus
	// any Data/SKSE/Plugins/*_RCP.json files (see
	// ConfigFile::FindExternalConfigPaths). Returns an empty list if none
	// of them have any. This feature is entirely opt-in.
	std::vector<RecipeOverride> LoadRecipeOverrides();

	// Same as above, but works from already-parsed config(s) instead of
	// reading files itself. Use this when a caller (like
	// RecipePatcher.cpp) also needs LoadTriggerEntries() in the same
	// pass, so the shared config file is only read/parsed once instead of
	// once per loader. a_externalConfigs are additional mod-author config
	// files; only their "recipeOverrides" arrays are used.
	std::vector<RecipeOverride> LoadRecipeOverrides(
		const nlohmann::ordered_json& a_mainConfig,
		const std::vector<ConfigFile::ExternalConfig>& a_externalConfigs = {});

	// Overwrites the "recipeOverrides" array in a_path with the given
	// list (read-modify-write: every other top-level key, and any "//"
	// comment keys, are left untouched). a_path may be the main config or
	// any external *_RCP.json; the in-game editor can target either.
	void SaveRecipeOverrides(const std::vector<RecipeOverride>& a_overrides, const std::string& a_path);
}
