#include "Settings.h"

#include "ConfigFile.h"

namespace RPP
{
	namespace
	{
		Settings FromJson(const nlohmann::ordered_json& a_json)
		{
			Settings s{};
			s.enabled = a_json.value("enabled", s.enabled);
			s.existingPerkMode = a_json.value("existingPerkMode", s.existingPerkMode);
			s.logLevel = a_json.value("logLevel", s.logLevel);
			return s;
		}
	}

	Settings LoadSettings()
	{
		return FromJson(ConfigFile::Read());
	}

	void SaveSettings(const Settings& a_settings)
	{
		// Read-modify-write: only touch our own top-level keys. This is
		// what keeps "mappings"/"recipeOverrides" (and any "//" comment
		// keys) intact even though the in-game menu can trigger a save at
		// any time, including after you've hand-edited the mappings in
		// this same file.
		auto j = ConfigFile::Read();

		j["enabled"] = a_settings.enabled;
		j["existingPerkMode"] = a_settings.existingPerkMode;
		j["logLevel"] = a_settings.logLevel;

		ConfigFile::Write(j);
	}

	Settings& CurrentSettings()
	{
		static Settings settings = LoadSettings();
		return settings;
	}
}
