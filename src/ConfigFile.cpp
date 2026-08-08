#include "ConfigFile.h"

namespace RPP::ConfigFile
{
	nlohmann::ordered_json ReadFile(const std::string& a_path)
	{
		std::ifstream file{ a_path };
		if (!file.is_open()) {
			return nlohmann::ordered_json::object();
		}

		try {
			nlohmann::ordered_json j;
			file >> j;
			return j;
		} catch (const std::exception& e) {
			SKSE::log::error("failed to parse {}: {} -- treating as empty", a_path, e.what());
			return nlohmann::ordered_json::object();
		}
	}

	void WriteFile(const std::string& a_path, const nlohmann::ordered_json& a_json)
	{
		std::ofstream file{ a_path };
		if (!file.is_open()) {
			SKSE::log::error("failed to open {} for writing", a_path);
			return;
		}

		file << a_json.dump(2);
	}

	nlohmann::ordered_json Read()
	{
		return ReadFile(kPath);
	}

	void Write(const nlohmann::ordered_json& a_json)
	{
		WriteFile(kPath, a_json);
	}

	std::string MakeExternalConfigPath(std::string_view a_name)
	{
		// Keep only characters that are safe in a filename. This is doing
		// real work, not being fussy: the name comes from a free-text box
		// in-game, and without stripping separators and dots a value like
		// "../../SkyrimPrefs" would write outside Data/SKSE/Plugins
		// entirely.
		std::string cleaned;
		cleaned.reserve(a_name.size());
		for (const char c : a_name) {
			const auto uc = static_cast<unsigned char>(c);
			if (std::isalnum(uc) != 0 || c == '_' || c == '-' || c == ' ') {
				cleaned += c;
			}
		}

		// Trim surrounding spaces so " foo " doesn't become " foo _RCP.json".
		const auto first = cleaned.find_first_not_of(' ');
		const auto last = cleaned.find_last_not_of(' ');
		if (first == std::string::npos) {
			return {};
		}
		cleaned = cleaned.substr(first, last - first + 1);

		// Tolerate the user typing the suffix themselves rather than
		// producing "MyMod_RCP_RCP.json".
		constexpr std::string_view kSuffixNoExt = "_RCP";
		if (cleaned.size() >= kSuffixNoExt.size() &&
			std::string_view{ cleaned }.substr(cleaned.size() - kSuffixNoExt.size()) == kSuffixNoExt) {
			cleaned.erase(cleaned.size() - kSuffixNoExt.size());
		}

		if (cleaned.empty()) {
			return {};
		}

		return std::string{ kDirectory } + "/" + cleaned + kExternalConfigSuffix;
	}

	std::vector<std::string> FindExternalConfigPaths()
	{
		std::vector<std::string> result;

		std::error_code ec;
		if (!std::filesystem::is_directory(kDirectory, ec) || ec) {
			return result;
		}

		for (const auto& entry : std::filesystem::directory_iterator(kDirectory, ec)) {
			if (ec) {
				break;
			}
			if (!entry.is_regular_file()) {
				continue;
			}

			const auto filename = entry.path().filename().string();
			// generic_string(), NOT string(): on Windows the latter yields
			// backslashes, which then never compare equal to kPath or to
			// MakeExternalConfigPath()'s output (both forward-slash). That
			// mismatch silently broke selecting a file in the editor.
			if (filename.ends_with(kExternalConfigSuffix)) {
				result.push_back(entry.path().generic_string());
			}
		}

		std::sort(result.begin(), result.end());
		return result;
	}

	std::vector<ExternalConfig> ReadExternalConfigs()
	{
		std::vector<ExternalConfig> result;

		for (auto& path : FindExternalConfigPaths()) {
			std::ifstream file{ path };
			if (!file.is_open()) {
				SKSE::log::warn("could not open external config '{}'", path);
				continue;
			}

			try {
				nlohmann::ordered_json j;
				file >> j;

				// Settings only make sense coming from the one main file a
				// player actually controls. A third-party file setting
				// these would let any installed mod silently change
				// another mod's settings, which is a bad, easily-abused
				// precedent. Warn rather than silently ignore, so a mod
				// author notices these did nothing instead of assuming
				// they took effect.
				for (const char* key : { "enabled", "existingPerkMode", "logLevel" }) {
					if (j.contains(key)) {
						SKSE::log::warn("external config '{}' sets \"{}\", which is ignored outside the main "
							"{}: only \"mappings\"/\"recipeOverrides\" are read from external config files",
							path, key, kPath);
					}
				}

				SKSE::log::info("loaded external config '{}'", path);
				result.push_back(ExternalConfig{ std::move(path), std::move(j) });
			} catch (const std::exception& e) {
				SKSE::log::error("failed to parse external config '{}': {} -- skipping", path, e.what());
			}
		}

		return result;
	}
}
