#include "UIMappingsEditor.h"

#include "ConfigFile.h"
#include "RecipeOverrides.h"
#include "RecipePatcher.h"
#include "Settings.h"
#include "TriggerConfig.h"
#include "UIAutocomplete.h"
#include "UIConditionFields.h"

#include "SKSEMenuFramework.h"
#include "UIStyle.h"

#include <spdlog/fmt/fmt.h>

namespace ImGui = ImGuiMCP;

namespace RPP::UI::MappingsEditor
{
	namespace
	{
		struct EditableMapping
		{
			std::array<char, 128> material{};
			EditableCondition condition;
			std::array<char, 256> comment{};
		};

		struct EditableOverride
		{
			std::array<char, 128> recipe{};
			EditableCondition condition;
			bool exclude = false;
			int mode = ExistingPerkMode::kAdd;
			std::array<char, 256> comment{};
		};

		// Process-wide editor state. Loaded from disk on first Render()
		// call (or via the Reload button); edits stay purely in memory
		// until Save & Apply is pressed.
		struct EditorState
		{
			bool loaded = false;
			std::vector<EditableMapping> mappingRows;
			std::vector<EditableOverride> overrideRows;
			std::string lastSaveSummary;

			// Which file is currently open for editing: the main config
			// or one of the external *_RCP.json files. Everything the tab
			// loads and saves goes through this, so only ever one file is
			// touched at a time and another mod's file is never written
			// unless it is the one deliberately selected.
			std::string targetPath{ ConfigFile::kPath };

			// "Save As..." prompt: buffer for the new config's name, and
			// whether the prompt is currently showing.
			bool showSaveAs = false;
			bool saveAsBlank = false;  // true = Create New (empty), false = Save As (copy)
			std::array<char, 128> saveAsName{};

			// Config files the picker can offer. Cached rather than
			// rescanned per frame. FindExternalConfigPaths() walks a
			// directory, and the picker is drawn every frame the tab is
			// open. Refreshed whenever the editor reloads.
			std::vector<std::string> availablePaths;
		};

		EditorState& State()
		{
			static EditorState state;
			return state;
		}

		void ReloadFromDisk()
		{
			auto& state = State();

			state.availablePaths.clear();
			state.availablePaths.emplace_back(ConfigFile::kPath);
			for (auto& path : ConfigFile::FindExternalConfigPaths()) {
				state.availablePaths.push_back(std::move(path));
			}

			const auto config = ConfigFile::ReadFile(state.targetPath);

			state.mappingRows.clear();
			// Deliberately parses just this one file, NOT the no-arg
			// loaders, which merge every *_RCP.json together. The editor
			// shows and saves exactly one file, so that saving can't copy
			// another mod's entries into it.
			for (const auto& e : LoadTriggerEntries(config)) {
				EditableMapping row{};
				CopyIntoBuffer(row.material, e.materialID);
				row.condition = FromSpec(e.condition);
				CopyIntoBuffer(row.comment, e.comment);
				state.mappingRows.push_back(row);
			}

			state.overrideRows.clear();
			// Same single-file reasoning as the mappings above.
			for (const auto& o : LoadRecipeOverrides(config)) {
				if (o.exclude) {
					EditableOverride row{};
					CopyIntoBuffer(row.recipe, o.recipeID);
					row.exclude = true;
					CopyIntoBuffer(row.comment, o.comment);
					state.overrideRows.push_back(row);
					continue;
				}
				// One editor row per condition, same "share the same ID
				// across multiple rows to add multiple conditions"
				// pattern the Mappings table already uses for materials.
				for (const auto& spec : o.conditions) {
					EditableOverride row{};
					CopyIntoBuffer(row.recipe, o.recipeID);
					row.condition = FromSpec(spec);
					row.mode = o.mode;
					CopyIntoBuffer(row.comment, o.comment);
					state.overrideRows.push_back(row);
				}
			}

			state.loaded = true;
			state.lastSaveSummary.clear();
		}

		void SaveToDisk()
		{
			auto& state = State();

			std::vector<TriggerEntry> mappings;
			for (const auto& row : state.mappingRows) {
				const std::string material{ row.material.data() };
				if (material.empty() || !HasUsableParam1(row.condition)) {
					continue;  // silently drop incomplete rows rather than saving garbage
				}
				TriggerEntry e{};
				e.materialID = material;
				e.condition = EditableCondition::ToSpec(row.condition);
				e.comment = row.comment.data();
				mappings.push_back(std::move(e));
			}
			SaveUserTriggerEntries(mappings, state.targetPath);

			// Group override rows back into one RecipeOverride per unique
			// recipe ID (each row is one condition, or one exclude-only
			// row).
			std::vector<RecipeOverride> overrides;
			for (const auto& row : state.overrideRows) {
				const std::string recipe{ row.recipe.data() };
				if (recipe.empty()) {
					continue;
				}

				auto it = std::find_if(overrides.begin(), overrides.end(),
					[&](const RecipeOverride& o) { return o.recipeID == recipe; });
				if (it == overrides.end()) {
					RecipeOverride o{};
					o.recipeID = recipe;
					o.comment = row.comment.data();
					overrides.push_back(std::move(o));
					it = std::prev(overrides.end());
				}

				if (row.exclude) {
					it->exclude = true;
					continue;
				}
				if (it->exclude) {
					continue;  // an exclude row for this recipe already won; ignore condition rows
				}

				if (!HasUsableParam1(row.condition)) {
					continue;
				}
				it->mode = row.mode;
				it->conditions.push_back(EditableCondition::ToSpec(row.condition));
			}
			// Drop any override left with neither exclude nor conditions
			// (e.g. only an incomplete row was ever added for that recipe).
			overrides.erase(
				std::remove_if(overrides.begin(), overrides.end(),
					[](const RecipeOverride& o) { return !o.exclude && o.conditions.empty(); }),
				overrides.end());
			SaveRecipeOverrides(overrides, state.targetPath);

			state.lastSaveSummary = fmt::format("Saved {} mapping(s) and {} override recipe(s). Re-applying...",
				mappings.size(), overrides.size());

			ApplyPerkRequirementsToAllRecipes();
		}

		// Filename portion of a config path, for display.
		std::string FileLabel(const std::string& a_path)
		{
			const auto slash = a_path.find_last_of("/\\");
			return slash == std::string::npos ? a_path : a_path.substr(slash + 1);
		}

		// Lets the tab target any config file rather than only the main
		// one: the main config plus every discovered *_RCP.json, and a
		// "Save As..." prompt for creating a new one. Only the selected
		// file is ever loaded or written, so editing here can't merge one
		// mod's entries into another's file.
		void RenderFilePicker()
		{
			auto& state = State();

			// Main config first, then the external files: the same order
			// they're applied in, so the list reads like the load order.
			// Populated by ReloadFromDisk(), not rescanned here.
			const auto& paths = state.availablePaths;
			if (paths.empty()) {
				return;
			}

			// If the selected file vanished (deleted outside the game),
			// fall back to the main config rather than editing a ghost.
			int current = -1;
			for (int i = 0; i < static_cast<int>(paths.size()); ++i) {
				if (paths[i] == state.targetPath) {
					current = i;
					break;
				}
			}
			if (current < 0) {
				current = 0;
				state.targetPath = paths[0];
				ReloadFromDisk();
			}

			std::vector<std::string> labels;
			labels.reserve(paths.size());
			for (const auto& path : paths) {
				labels.push_back(path == ConfigFile::kPath ?
					FileLabel(path) + "  (main config)" :
					FileLabel(path));
			}
			std::vector<const char*> items;
			items.reserve(labels.size());
			for (const auto& label : labels) {
				items.push_back(label.c_str());
			}

			ImGui::PushItemWidth(360.0f);
			if (ImGui::Combo("Editing", &current, items.data(), static_cast<int>(items.size()))) {
				state.targetPath = paths[current];
				state.lastSaveSummary.clear();
				ReloadFromDisk();
			}
			ImGui::PopItemWidth();

			// All the file actions live together on one row rather than
			// having Save/Reload stranded at the bottom of a long page,
			// they all operate on the file named in the dropdown above, so
			// keeping them adjacent to it makes that relationship obvious.
			ImGui::SameLine();
			if (ImGui::Button("Save & Apply")) {
				SaveToDisk();
			}
			ImGui::SameLine();
			if (ImGui::Button("Reload From Disk")) {
				ReloadFromDisk();
			}
			ImGui::SameLine();
			if (ImGui::Button("Save As...")) {
				state.showSaveAs = true;
				state.saveAsBlank = false;
				state.saveAsName = {};
			}
			ImGui::SameLine();
			if (ImGui::Button("Create New")) {
				state.showSaveAs = true;
				state.saveAsBlank = true;
				state.saveAsName = {};
			}

			if (state.showSaveAs) {
				ImGui::Indent();

				UI::Style::Hint(state.saveAsBlank ?
					"Create a new, empty config file and switch to it." :
					"Copy everything currently on screen into a new config file and switch to it.");

				ImGui::PushItemWidth(280.0f);
				ImGui::InputTextWithHint("##saveasname", "new config name",
					state.saveAsName.data(), state.saveAsName.size());
				ImGui::PopItemWidth();

				// Shown resolved rather than raw, so it's obvious what the
				// typed name actually becomes on disk. The name is
				// sanitised (see MakeExternalConfigPath) and gains the
				// _RCP.json suffix the loader scans for.
				const std::string newPath = ConfigFile::MakeExternalConfigPath(state.saveAsName.data());
				if (newPath.empty()) {
					UI::Style::Hint("Enter a name to continue.");
				} else {
					ImGui::TextColored(UI::Style::kAccentText, "Creates: %s", newPath.c_str());
					if (ImGui::Button(state.saveAsBlank ? "Create empty config" : "Create copy")) {
						state.targetPath = newPath;
						if (state.saveAsBlank) {
							state.mappingRows.clear();
							state.overrideRows.clear();
						}
						SaveToDisk();
						// Re-reads the file just written and refreshes the
						// picker so the new config appears in the list and
						// becomes the selected one.
						ReloadFromDisk();
						state.showSaveAs = false;
						state.saveAsName = {};
					}
					ImGui::SameLine();
				}

				if (ImGui::Button("Cancel")) {
					state.showSaveAs = false;
					state.saveAsName = {};
				}
				ImGui::Unindent();
			}

			UI::Style::Hint(
				"Only the selected file is shown and saved. Switching files discards unsaved edits. "
				"Settings (Enable, Existing Conditions, Log Verbosity) always live in the main config.");
		}

		void RenderMappingsSection()
		{
			auto& state = State();

			// Scope every widget in this section under its own ID.
			// Without this, this section's PushID(i) and the overrides
			// section's PushID(i) produce the SAME ImGui IDs for the same
			// row index and label (e.g. mapping row 0's "Function" and
			// override row 0's "Function"), and ImGui treats same-ID
			// widgets as literally the same widget, so typing in one
			// edits both, and keystrokes get processed twice (backspace
			// appearing to delete two characters).
			ImGui::PushID("mappings");

			UI::Style::Heading("Material -> Condition Mappings");
			ImGui::TextDisabled(
				"Add multiple rows with the same material to give it "
				"more than one condition.");

			ImGui::Spacing();

			int deleteIndex = -1;
			for (int i = 0; i < static_cast<int>(state.mappingRows.size()); ++i) {
				auto& row = state.mappingRows[i];
				ImGui::PushID(i);

				// Collapsed by default and summarised on one line, so a
				// long mapping list stays scannable instead of running to
				// hundreds of rows of open fields. "###" keeps the widget's
				// ID stable while the visible summary text changes as you
				// edit, which is what stops it collapsing mid-edit.
				const char* material = row.material.data();
				const std::string header = fmt::format("{} -> {}###mapping{}",
					material[0] != '\0' ? material : "(unset material)",
					SummariseCondition(row.condition),
					i);

				if (ImGui::CollapsingHeader(header.c_str())) {
					ImGui::Indent();

					ImGui::PushItemWidth(220.0f);
					AutocompleteInputText("Material", "EditorID or FormID~Plugin", row.material, AllCraftableItemEditorIDs(), Indicator::Form);
					ImGui::PopItemWidth();

					RenderConditionFields(row.condition);

					// -120 rather than -1: ImGui draws a widget's label to
					// its right, so a full-width field pushed "Comment"
					// off the panel edge and left this looking like an
					// unlabelled box. Reserving that margin brings the
					// label back, and the hint says what the field is for
					// while it's still empty.
					ImGui::PushItemWidth(-120.0f);
					ImGui::InputTextWithHint("Comment", "optional note, not used by the patcher",
						row.comment.data(), row.comment.size());
					ImGui::PopItemWidth();

					if (ImGui::SmallButton("Remove this mapping")) {
						deleteIndex = i;
					}

					ImGui::Unindent();
					ImGui::Spacing();
				}

				ImGui::PopID();
			}

			if (deleteIndex >= 0) {
				state.mappingRows.erase(state.mappingRows.begin() + deleteIndex);
			}

			ImGui::Spacing();
			if (ImGui::SmallButton("+ Add Mapping")) {
				state.mappingRows.emplace_back();
			}

			ImGui::PopID();
		}

		// Mode, Exclude, and Comment are each really one value per RECIPE,
		// not per condition: SaveToDisk groups every row sharing a Recipe
		// field into a single RecipeOverride, which has exactly one "mode",
		// one "exclude", and one "comment", never one per condition. Each
		// row still keeps its own copy of all three so their controls have
		// something to bind to, so without this sync, sibling rows for the
		// same recipe could disagree on screen even though only one of
		// them can actually end up in the saved JSON (silently: for Mode
		// and Exclude, whichever row SaveToDisk happens to process last
		// wins; for Comment, whichever row it processes first). Applying
		// a_apply to every OTHER row sharing a_state.overrideRows[a_changedIndex]'s
		// Recipe keeps them all in lockstep, so there's nothing left for
		// SaveToDisk to silently pick between.
		template <class Fn>
		void SyncAcrossSiblingOverrideRows(EditorState& a_state, int a_changedIndex, Fn a_apply)
		{
			const std::string_view recipe{ a_state.overrideRows[a_changedIndex].recipe.data() };
			if (recipe.empty()) {
				return;
			}
			for (int j = 0; j < static_cast<int>(a_state.overrideRows.size()); ++j) {
				if (j == a_changedIndex) {
					continue;
				}
				auto& other = a_state.overrideRows[j];
				if (std::string_view{ other.recipe.data() } == recipe) {
					a_apply(other);
				}
			}
		}

		void RenderOverridesSection()
		{
			auto& state = State();

			// Distinct ID scope from the mappings section. See the note
			// on RenderMappingsSection's PushID for why this matters.
			ImGui::PushID("overrides");

			UI::Style::Heading("Recipe Overrides");
			ImGui::TextDisabled(
				"Target one specific recipe directly. Check \"Exclude\" to "
				"skip it entirely (its other fields are then ignored); "
				"otherwise fill in a condition to force onto it. Add "
				"multiple rows with the same Recipe to force more than one "
				"condition. \"Mode\", \"Exclude\", and \"Comment\" are each "
				"really one choice per recipe, not per condition, so "
				"changing any of them on one row updates every other row "
				"for that same recipe too.");

			ImGui::Spacing();

			static const char* overrideModes[] = { "Add to Existing", "Replace Existing" };

			int deleteIndex = -1;
			for (int i = 0; i < static_cast<int>(state.overrideRows.size()); ++i) {
				auto& row = state.overrideRows[i];
				ImGui::PushID(i);

				// Same collapsed-summary treatment as the mappings above.
				const char* recipe = row.recipe.data();
				const char* recipeLabel = recipe[0] != '\0' ? recipe : "(unset recipe)";
				const std::string header = row.exclude ?
				    fmt::format("{} -> EXCLUDED###override{}", recipeLabel, i) :
				    fmt::format("{} -> {}  [{}]###override{}",
				        recipeLabel,
				        SummariseCondition(row.condition),
				        row.mode == ExistingPerkMode::kReplace ? "Replace" : "Add",
				        i);

				if (ImGui::CollapsingHeader(header.c_str())) {
					ImGui::Indent();

					ImGui::PushItemWidth(220.0f);
					const bool recipeChanged = AutocompleteInputText("Recipe", "EditorID or FormID~Plugin", row.recipe, AllRecipeEditorIDs(), Indicator::Form);
					ImGui::PopItemWidth();

					if (recipeChanged) {
						// Retargeting this row onto a recipe another row
						// already covers should adopt that recipe's existing
						// Mode/Exclude/Comment instead of defaulting to Add
						// to Existing, not excluded, and blank. See
						// SyncAcrossSiblingOverrideRows.
						const std::string_view retargetedRecipe{ row.recipe.data() };
						if (!retargetedRecipe.empty()) {
							for (int j = 0; j < static_cast<int>(state.overrideRows.size()); ++j) {
								if (j == i) {
									continue;
								}
								const auto& sibling = state.overrideRows[j];
								if (std::string_view{ sibling.recipe.data() } == retargetedRecipe) {
									row.mode = sibling.mode;
									row.exclude = sibling.exclude;
									CopyIntoBuffer(row.comment, std::string{ sibling.comment.data() });
									break;
								}
							}
						}
					}

					if (ImGui::Checkbox("Exclude (skip this recipe entirely)", &row.exclude)) {
						const bool exclude = row.exclude;
						SyncAcrossSiblingOverrideRows(state, i, [exclude](EditableOverride& o) { o.exclude = exclude; });
					}

					if (!row.exclude) {
						RenderConditionFields(row.condition);

						ImGui::PushItemWidth(160.0f);
						if (ImGui::Combo("Mode", &row.mode, overrideModes, 2)) {
							const int mode = row.mode;
							SyncAcrossSiblingOverrideRows(state, i, [mode](EditableOverride& o) { o.mode = mode; });
						}
						ImGui::PopItemWidth();
					}

					// -120 rather than -1: ImGui draws a widget's label to
					// its right, so a full-width field pushed "Comment"
					// off the panel edge and left this looking like an
					// unlabelled box. Reserving that margin brings the
					// label back, and the hint says what the field is for
					// while it's still empty.
					ImGui::PushItemWidth(-120.0f);
					if (ImGui::InputTextWithHint("Comment", "optional note, not used by the patcher",
							row.comment.data(), row.comment.size())) {
						const std::string comment{ row.comment.data() };
						SyncAcrossSiblingOverrideRows(state, i, [&comment](EditableOverride& o) { CopyIntoBuffer(o.comment, comment); });
					}
					ImGui::PopItemWidth();

					if (ImGui::SmallButton("Remove this override")) {
						deleteIndex = i;
					}

					ImGui::Unindent();
					ImGui::Spacing();
				}

				ImGui::PopID();
			}

			if (deleteIndex >= 0) {
				state.overrideRows.erase(state.overrideRows.begin() + deleteIndex);
			}

			ImGui::Spacing();
			if (ImGui::SmallButton("+ Add Override")) {
				state.overrideRows.emplace_back();
			}

			ImGui::PopID();
		}
	}

	void __stdcall Render()
	{
		// Shared palette. See UIStyle.h. Also what makes the dropdown
		// popups and the autocomplete list opaque.
		UI::Style::Push();

		auto& state = State();
		if (!state.loaded) {
			ReloadFromDisk();
		}

		ImGui::TextWrapped(
			"Add, edit, or remove material->condition mappings and "
			"per-recipe overrides without hand-editing the JSON file. "
			"Changes here stay local to this menu until you press "
			"\"Save & Apply\".");

		ImGui::Spacing();

		RenderFilePicker();

		ImGui::Spacing();

		RenderFunctionReference();

		ImGui::Spacing();

		RenderMappingsSection();

		ImGui::Dummy(ImGui::ImVec2{ 0.0f, 16.0f });

		RenderOverridesSection();

		ImGui::Dummy(ImGui::ImVec2{ 0.0f, 16.0f });
		ImGui::Separator();
		ImGui::Spacing();

		if (!state.lastSaveSummary.empty()) {
			ImGui::Spacing();
			ImGui::TextDisabled("%s", state.lastSaveSummary.c_str());
		}

		UI::Style::Pop();
	}
}
