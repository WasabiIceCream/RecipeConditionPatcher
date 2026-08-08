#pragma once

namespace RPP
{
	// Named values for Settings::existingPerkMode: how to handle a recipe
	// that already has at least one condition of any kind, from vanilla,
	// another mod, or an earlier run of this plugin. (The name keeps
	// "PerkMode" for historical reasons from before conditions were
	// generalized beyond HasPerk. The concept itself now applies to any
	// condition this plugin adds.)
	namespace ExistingPerkMode
	{
		constexpr int kAdd = 0;      // add ours alongside whatever's already there (default, original behavior)
		constexpr int kReplace = 1;  // remove ALL existing conditions on the recipe, then add ours
		constexpr int kSkip = 2;     // leave the recipe completely untouched
	}

	struct Settings
	{
		bool enabled = true;

		// See the ExistingPerkMode:: constants above.
		int existingPerkMode = ExistingPerkMode::kAdd;

		// 0 = info, 1 = warn, 2 = error
		int logLevel = 0;
	};

	// Loads settings from Data/SKSE/Plugins/RecipeConditionPatcher.json if
	// present, otherwise returns the struct defaults above. Settings only
	// ever live in this ONE file, under their own top-level keys
	// ("enabled", "existingPerkMode", "logLevel"), unlike "mappings"/
	// "recipeOverrides"/"classifiers", which this same file can also hold
	// but which are just as commonly split out into *_RCP.json files (see
	// TriggerConfig.cpp/RecipeOverrides.cpp/Classifiers.cpp); external
	// config files' copies of these settings keys are ignored on purpose
	// (see ConfigFile::ReadExternalConfigs).
	Settings LoadSettings();

	// Writes the current settings back into
	// Data/SKSE/Plugins/RecipeConditionPatcher.json, preserving whatever
	// "mappings"/"recipeOverrides"/"classifiers" content is already in that
	// file (a read-modify-write, not a blind overwrite; see Settings.cpp).
	// Called automatically whenever a control in the in-game menu changes.
	void SaveSettings(const Settings& a_settings);

	// Process-wide live settings, lazily loaded from disk on first access.
	// Both the in-game menu (UI.cpp) and the patcher (RecipePatcher.cpp)
	// read/write through this single instance, so a menu change is visible
	// immediately to anything that re-reads it (e.g. the "Apply Now" button).
	Settings& CurrentSettings();
}
