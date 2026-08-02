#pragma once

namespace RPP
{
	// Resolves a material/perk/recipe identifier string, which may be
	// either:
	//   - a Creation Kit EditorID (e.g. "IngotEbony"), or
	//   - a "FormID~PluginName" pair (e.g. "0x5AD99~Skyrim.esm"), for
	//     cases where a mod's EditorIDs aren't guaranteed stable across
	//     its own updates but its FormIDs are. The FormID half is the
	//     record's raw local FormID exactly as shown in SSEEdit/xEdit for
	//     that specific plugin file (no leading zeros needed) - the top
	//     byte (load index) is replaced automatically based on the
	//     current load order, same convention used by tools like SPID.
	//
	// Deliberately silent (no logging) - it's reused for the in-game
	// editor's live per-keystroke validation, where logging every failed
	// lookup while someone's mid-way through typing an ID would spam the
	// log. Callers on the runtime patching path log their own warnings
	// when this returns nullptr.
	template <class T>
	T* ResolveIdentifier(const std::string& a_id)
	{
		const auto tilde = a_id.find('~');
		if (tilde == std::string::npos) {
			return RE::TESForm::LookupByEditorID<T>(a_id);
		}

		const auto formIDStr = a_id.substr(0, tilde);
		const auto pluginName = a_id.substr(tilde + 1);

		RE::FormID rawFormID;
		try {
			rawFormID = static_cast<RE::FormID>(std::stoul(formIDStr, nullptr, 16));
		} catch (const std::exception&) {
			return nullptr;
		}

		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		return dataHandler ? dataHandler->LookupForm<T>(rawFormID, pluginName) : nullptr;
	}
}
