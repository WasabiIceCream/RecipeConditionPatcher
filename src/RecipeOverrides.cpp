#include "RecipeOverrides.h"

#include "ConfigFile.h"

namespace RPP
{
	namespace
	{
		// Parses one config's "recipeOverrides" array. Shared between the
		// main config and each external *_RCP.json file, since the
		// format is identical either way.
		std::vector<RecipeOverride> ParseRecipeOverridesArray(const nlohmann::ordered_json& a_config, const std::string& a_sourcePath)
		{
			std::vector<RecipeOverride> result;

			if (!a_config.contains("recipeOverrides") || !a_config["recipeOverrides"].is_array()) {
				return result;
			}

			for (auto& entry : a_config["recipeOverrides"]) {
				RecipeOverride o{};
				o.recipeID = entry.value("recipe", "");
				o.exclude = entry.value("exclude", false);
				o.mode = entry.value("mode", ExistingPerkMode::kAdd);
				o.comment = entry.value("comment", "");
				o.sourceFile = a_sourcePath;

				if (entry.contains("conditions") && entry["conditions"].is_array()) {
					for (auto& c : entry["conditions"]) {
						ConditionSpec spec{};
						spec.function = c.value("function", std::string{ "HasPerk" });
						spec.param1 = c.value("param1", "");
						spec.param2 = c.value("param2", "");
						spec.op = c.value("operator", std::string{ "==" });
						spec.value = c.value("value", std::string{ "1" });
						spec.runOn = c.value("runOn", 0);
						spec.logic = c.value("logic", std::string{ "AND" });
						// NOT gated on param1 being non-empty. A
						// recipeOverrides condition can legitimately be a
						// param-less function (GetCurrentTime, GetLevel,
						// GetRandomPercent; see the Condition functions
						// table in the README).
						o.conditions.push_back(std::move(spec));
					}
				} else if (entry.contains("perks") && entry["perks"].is_array()) {
					// Backward compat with the older perks-only schema: one
					// HasPerk condition per listed perk.
					for (auto& perk : entry["perks"]) {
						if (perk.is_string()) {
							ConditionSpec spec{};
							spec.function = "HasPerk";
							spec.param1 = perk.get<std::string>();
							o.conditions.push_back(std::move(spec));
						}
					}
				}

				if (o.recipeID.empty()) {
					SKSE::log::warn("skipping malformed recipeOverrides entry in {} (need \"recipe\")", a_sourcePath);
					continue;
				}
				if (!o.exclude && o.conditions.empty()) {
					SKSE::log::warn(
						"recipeOverrides entry for '{}' in {} has no \"conditions\" and \"exclude\" isn't true; it does nothing, skipping",
						o.recipeID, a_sourcePath);
					continue;
				}

				result.push_back(std::move(o));
			}

			return result;
		}
	}

	std::vector<RecipeOverride> LoadRecipeOverrides(
		const nlohmann::ordered_json& a_mainConfig,
		const std::vector<ConfigFile::ExternalConfig>& a_externalConfigs)
	{
		std::vector<RecipeOverride> result = ParseRecipeOverridesArray(a_mainConfig, ConfigFile::kPath);

		std::size_t externalCount = 0;
		for (const auto& external : a_externalConfigs) {
			auto externalOverrides = ParseRecipeOverridesArray(external.content, external.path);
			externalCount += externalOverrides.size();
			for (auto& o : externalOverrides) {
				result.push_back(std::move(o));
			}
		}

		if (!result.empty()) {
			SKSE::log::info("loaded {} recipe override(s) ({} from external config file(s))", result.size(), externalCount);
		}

		return result;
	}

	std::vector<RecipeOverride> LoadRecipeOverrides()
	{
		return LoadRecipeOverrides(ConfigFile::Read(), ConfigFile::ReadExternalConfigs());
	}

	void SaveRecipeOverrides(const std::vector<RecipeOverride>& a_overrides, const std::string& a_path)
	{
		// Read-modify-write of the TARGET file. See the note in
		// TriggerConfig.cpp's SaveUserTriggerEntries.
		auto j = ConfigFile::ReadFile(a_path);

		nlohmann::ordered_json arr = nlohmann::ordered_json::array();
		for (const auto& o : a_overrides) {
			nlohmann::ordered_json row;
			row["recipe"] = o.recipeID;
			if (o.exclude) {
				row["exclude"] = true;
			} else {
				nlohmann::ordered_json conditions = nlohmann::ordered_json::array();
				for (const auto& spec : o.conditions) {
					nlohmann::ordered_json c;
					c["function"] = spec.function;
					c["param1"] = spec.param1;
					if (!spec.param2.empty()) {
						c["param2"] = spec.param2;
					}
					c["operator"] = spec.op;
					c["value"] = spec.value;
					c["runOn"] = spec.runOn;
					c["logic"] = spec.logic;
					conditions.push_back(std::move(c));
				}
				row["conditions"] = std::move(conditions);
				row["mode"] = o.mode;
			}
			if (!o.comment.empty()) {
				row["comment"] = o.comment;
			}
			arr.push_back(std::move(row));
		}

		j["recipeOverrides"] = std::move(arr);
		ConfigFile::WriteFile(a_path, j);
	}
}
