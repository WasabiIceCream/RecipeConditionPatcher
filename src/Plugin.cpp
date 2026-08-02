#include "RecipePatcher.h"
#include "UI.h"
#include "Version.h"

namespace
{
	void InitializeLogging()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			return;
		}

		*path /= "RecipeConditionPatcher.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global", std::move(sink));

		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S] [%l] %v"s);
	}

	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == SKSE::MessagingInterface::kPostLoad) {
			// All SKSE plugin DLLs (including SKSE Menu Framework, if
			// installed) are loaded by this point, so GetModuleHandle-based
			// detection in SKSEMenuFramework::IsInstalled() is reliable here.
			RPP::UI::Register();
		} else if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
			// All plugins' records (including any that add new smithing
			// materials/perks/quests/etc. this plugin's conditions might
			// reference) are guaranteed to be loaded by this point.
			RPP::ApplyPerkRequirementsToAllRecipes();
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	InitializeLogging();
	SKSE::log::info("RecipeConditionPatcher loading...");

	SKSE::Init(a_skse);

	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
		SKSE::log::error("failed to register SKSE messaging listener");
		return false;
	}

	return true;
}

SKSEPluginVersion = []() noexcept {
	SKSE::PluginVersionData v{};
	v.PluginVersion({ RPP_VERSION_MAJOR, RPP_VERSION_MINOR, RPP_VERSION_PATCH });
	v.PluginName("RecipeConditionPatcher");
	v.AuthorName("you");
	v.UsesAddressLibrary();
	v.UsesUpdatedStructs();
	// Deliberately NOT calling v.CompatibleVersions(...): pinning it to
	// SKSE::RUNTIME_SSE_LATEST bakes in whatever the newest Steam version
	// happened to be when this was built against a given CommonLibSSE
	// commit, and would need a rebuild every time Bethesda ships another
	// game patch. Leaving it unset, combined with UsesAddressLibrary() +
	// UsesUpdatedStructs() (which together mean this plugin resolves
	// everything through Address Library rather than hardcoded per-version
	// offsets), tells SKSE this plugin doesn't care which exact version is
	// running - the standard approach for staying compatible with future
	// game updates without reissuing builds.
	return v;
}();
