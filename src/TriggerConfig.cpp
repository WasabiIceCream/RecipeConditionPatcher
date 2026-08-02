#include "TriggerConfig.h"

#include "ConfigFile.h"

namespace RPP
{
	namespace
	{
		// Parses a raw "mappings" json array into entries, warning on and
		// skipping malformed rows. a_sourcePath is only used for those
		// warnings - it must be the file the array actually came from
		// (the main config, or a specific external *_RCP.json), or a
		// malformed third-party entry would send someone digging through
		// the wrong file.
		std::vector<TriggerEntry> ParseMappingsArray(const nlohmann::ordered_json& a_mappings, const std::string& a_sourcePath)
		{
			std::vector<TriggerEntry> result;

			for (auto& entry : a_mappings) {
				TriggerEntry e{};
				e.materialID = entry.value("material", "");
				e.comment = entry.value("comment", "");

				e.condition.function = entry.value("function", std::string{ "HasPerk" });
				// "perk" is accepted as a backward-compatible alias for
				// param1, specifically because it was this plugin's only
				// field before conditions were generalized.
				e.condition.param1 = entry.contains("param1") ? entry.value("param1", "") : entry.value("perk", "");
				e.condition.param2 = entry.value("param2", "");
				e.condition.op = entry.value("operator", std::string{ "==" });
				e.condition.value = entry.value("value", std::string{ "1" });
				e.condition.runOn = entry.value("runOn", 0);
				e.condition.logic = entry.value("logic", std::string{ "AND" });

				if (e.materialID.empty() || e.condition.param1.empty()) {
					SKSE::log::warn("skipping malformed mapping entry in {} (need \"material\" and \"param1\"/\"perk\")", a_sourcePath);
					continue;
				}

				result.push_back(std::move(e));
			}

			return result;
		}
	}

	std::vector<TriggerEntry> LoadTriggerEntries(
		const nlohmann::ordered_json& a_mainConfig,
		const std::vector<ConfigFile::ExternalConfig>& a_externalConfigs)
	{
		std::vector<TriggerEntry> result;

		std::size_t mainCount = 0;
		if (a_mainConfig.contains("mappings") && a_mainConfig["mappings"].is_array()) {
			result = ParseMappingsArray(a_mainConfig["mappings"], ConfigFile::kPath);
			mainCount = result.size();
		}

		std::size_t externalCount = 0;
		for (const auto& external : a_externalConfigs) {
			if (external.content.contains("mappings") && external.content["mappings"].is_array()) {
				auto externalEntries = ParseMappingsArray(external.content["mappings"], external.path);
				externalCount += externalEntries.size();
				for (auto& e : externalEntries) {
					result.push_back(std::move(e));
				}
			}
		}

		SKSE::log::info("loaded {} trigger rows ({} from {}, {} from {} external config file(s))",
			result.size(), mainCount, ConfigFile::kPath, externalCount, a_externalConfigs.size());

		return result;
	}

	std::vector<TriggerEntry> LoadTriggerEntries()
	{
		return LoadTriggerEntries(ConfigFile::Read(), ConfigFile::ReadExternalConfigs());
	}

	void SaveUserTriggerEntries(const std::vector<TriggerEntry>& a_entries, const std::string& a_path)
	{
		// Read-modify-write of the TARGET file, so saving into an
		// external *_RCP.json leaves that file's other keys (and any "//"
		// comments its author wrote) intact.
		auto j = ConfigFile::ReadFile(a_path);

		nlohmann::ordered_json arr = nlohmann::ordered_json::array();
		for (const auto& e : a_entries) {
			nlohmann::ordered_json row;
			row["material"] = e.materialID;
			row["function"] = e.condition.function;
			row["param1"] = e.condition.param1;
			if (!e.condition.param2.empty()) {
				row["param2"] = e.condition.param2;
			}
			row["operator"] = e.condition.op;
			row["value"] = e.condition.value;
			row["runOn"] = e.condition.runOn;
			row["logic"] = e.condition.logic;
			if (!e.comment.empty()) {
				row["comment"] = e.comment;
			}
			arr.push_back(std::move(row));
		}

		j["mappings"] = std::move(arr);
		ConfigFile::WriteFile(a_path, j);
	}
}
