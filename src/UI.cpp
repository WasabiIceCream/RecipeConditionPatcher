#include "UI.h"

#include "RecipePatcher.h"
#include "Settings.h"
#include "UIMappingsEditor.h"

#include "SKSEMenuFramework.h"

// In this build of the vendored header, the ImGui wrapper functions
// (Checkbox, TextWrapped, Combo, etc.) live in namespace ImGuiMCP, not a
// bare "ImGui" - this alias lets the rest of this file use the more
// familiar ImGui:: spelling without changing every call site.
#include "UIStyle.h"

namespace ImGui = ImGuiMCP;

namespace RPP::UI
{
	namespace
	{
		// Vertical gap between major sections. A bit more breathing room
		// than a single Spacing()/Separator() gives on its own.
		// NOTE: ImVec2 here is a plain aggregate (just float x, y - no
		// constructor), so it needs brace-init, not ImVec2(0.0f, 12.0f).
		void BigGap()
		{
			ImGui::Dummy(ImGui::ImVec2{ 0.0f, 12.0f });
		}

		void __stdcall Render()
		{
			// Shared palette - keeps this tab consistent with Mappings and
			// makes the Log Verbosity / Existing Conditions dropdown lists
			// opaque instead of letting the page bleed through them.
			Style::Push();

			auto& settings = CurrentSettings();
			bool changed = false;

			ImGui::TextWrapped(
				"Adds matching conditions (perk requirements, item checks, "
				"quest gates, and more) to any recipe that uses a mapped "
				"material - e.g. Ebony Ingot -> requires Ebony Smithing. "
				"See the Mappings tab to configure what.");

			BigGap();
			Style::Heading("General");

			changed |= ImGui::Checkbox("Enable Patcher", &settings.enabled);
			ImGui::Indent();
			Style::Hint("Master switch. When off, no recipes are modified at all.");
			ImGui::Unindent();

			ImGui::Spacing();
			static const char* logLevels[] = { "Info", "Warnings Only", "Errors Only" };
			changed |= ImGui::Combo("Log Verbosity", &settings.logLevel, logLevels, 3);
			ImGui::Indent();
			Style::Hint("Only affects the plugin's own log file, not anything in-game.");
			ImGui::Unindent();

			BigGap();
			Style::Heading("Existing Conditions");

			static const char* existingPerkModes[] = { "Add to Existing", "Replace Existing", "Skip Recipe" };
			changed |= ImGui::Combo("##ExistingPerkMode", &settings.existingPerkMode, existingPerkModes, 3);

			ImGui::Spacing();
			ImGui::Indent();

			ImGui::TextWrapped(
				"How to handle a recipe that already has at least one "
				"condition - from vanilla, another mod, or an earlier run "
				"of this plugin.");

			ImGui::Spacing();
			ImGui::TextWrapped(
				"\"Add to Existing\" (default) layers our conditions "
				"on top of whatever's already there.");

			ImGui::Spacing();
			ImGui::TextWrapped(
				"\"Replace Existing\" removes ALL of the recipe's "
				"current conditions, then re-adds conditions based "
				"only on this plugin's own mappings.");
			Style::WarnHint("If the recipe's original condition doesn't "
				"correspond to any material this plugin tracks, that "
				"recipe can end up WEAKER than before, or with no "
				"condition at all.");

			ImGui::Spacing();
			ImGui::TextWrapped("\"Skip Recipe\" leaves it completely untouched.");

			ImGui::Unindent();

			BigGap();
			Style::Heading("Apply");

			if (ImGui::Button("Apply Now")) {
				ApplyPerkRequirementsToAllRecipes();
			}

			ImGui::Spacing();
			ImGui::Indent();

			ImGui::TextWrapped("Applies your current settings to all recipes immediately.");

			ImGui::Spacing();
			ImGui::TextWrapped("\"Replace Existing\" mode genuinely removes conditions live.");
			ImGui::TextWrapped(
				"\"Add to Existing\" mode, though, only affects what "
				"gets added going forward - it won't un-add a "
				"requirement it already added under different settings "
				"earlier this session.");

			ImGui::Spacing();
			Style::Hint("Restart Skyrim for a fully clean re-apply.");

			ImGui::Unindent();

			if (changed) {
				SaveSettings(settings);
			}

			Style::Pop();
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			SKSE::log::info("SKSE Menu Framework not detected; skipping menu registration");
			return;
		}

		SKSEMenuFramework::SetSection("Recipe Condition Patcher");
		SKSEMenuFramework::AddSectionItem("Settings", Render);
		SKSEMenuFramework::AddSectionItem("Mappings", MappingsEditor::Render);
		SKSE::log::info("registered SKSE Menu Framework menu");
	}
}
