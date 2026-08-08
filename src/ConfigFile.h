#pragma once

namespace RPP::ConfigFile
{
	// The one shared config file: settings, material->condition mappings,
	// and per-recipe overrides all live here under their own top-level keys.
	inline constexpr auto kPath = "Data/SKSE/Plugins/RecipeConditionPatcher.json";

	// The directory kPath lives in, and the filename suffix third-party
	// config files use. See FindExternalConfigPaths(). ("RCP" = Recipe
	// Condition Patcher, same "<ModName>_SUFFIX.json/ini" convention
	// tools like SPID use for their own *_DISTR.ini files.)
	inline constexpr auto kDirectory = "Data/SKSE/Plugins";
	inline constexpr auto kExternalConfigSuffix = "_RCP.json";

	// Reads the whole file as ordered_json (preserves key order, unlike
	// plain json's alphabetical-on-dump behavior) so that later Write()
	// calls don't scramble the file's layout. Returns an empty object if
	// the file doesn't exist or fails to parse. Never throws.
	nlohmann::ordered_json Read();

	// Writes the given json object to disk, replacing the file's entire
	// contents. Callers are expected to Read() first, modify only the
	// keys they own, and Write() the result back. This function itself
	// does no merging.
	void Write(const nlohmann::ordered_json& a_json);

	// Path-explicit versions of the two above. The in-game editor can
	// target any *_RCP.json, not just the main config, so its loaders and
	// savers name a file rather than assuming kPath. Read()/Write() are
	// just these bound to kPath.
	nlohmann::ordered_json ReadFile(const std::string& a_path);
	void WriteFile(const std::string& a_path, const nlohmann::ordered_json& a_json);

	// Builds "<kDirectory>/<name><kExternalConfigSuffix>" from a
	// user-supplied name, stripping anything that could escape that
	// directory (the name comes from a free-text box in-game). Returns an
	// empty string if nothing usable remains.
	std::string MakeExternalConfigPath(std::string_view a_name);

	// Finds every file in kDirectory whose name ends in
	// kExternalConfigSuffix (e.g. "SomeMod_RCP.json"). Lets other mod
	// authors ship their own mappings/recipeOverrides without editing the
	// single shared main config file, avoiding the conflicts that would
	// cause when multiple mods are installed together (each mod author
	// would otherwise be overwriting the same file). Returns full paths,
	// sorted for a deterministic load order. Never throws; returns an
	// empty list if the directory can't be read.
	std::vector<std::string> FindExternalConfigPaths();

	struct ExternalConfig
	{
		std::string path;
		nlohmann::ordered_json content;
	};

	// Reads and parses each path from FindExternalConfigPaths(). A file
	// that fails to parse is logged and skipped (not fatal), same
	// non-fatal-and-move-on pattern as everywhere else in this plugin.
	// These are read-only from the rest of the plugin's perspective (only
	// the main config file, via Write() above, is ever written back to);
	// each mod author's own file stays exactly as they shipped it. Each
	// result carries its own path alongside its content so callers (see
	// RecipeOverrides.cpp) can track which file a given entry came from,
	// needed for the "external files win over the main file for the same
	// recipe" priority rule.
	std::vector<ExternalConfig> ReadExternalConfigs();
}

