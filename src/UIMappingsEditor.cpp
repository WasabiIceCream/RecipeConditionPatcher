#include "UIMappingsEditor.h"

#include "ActorValueTable.h"
#include "ConfigFile.h"
#include "EditorIDCache.h"
#include "Identifiers.h"
#include "RecipeOverrides.h"
#include "RecipePatcher.h"
#include "RecommendedConditionFunctions.h"
#include "Settings.h"
#include "TriggerConfig.h"

#include "SKSEMenuFramework.h"
#include "UIStyle.h"

#include <spdlog/fmt/fmt.h>

namespace ImGui = ImGuiMCP;

namespace RPP::UI::MappingsEditor
{
	namespace
	{
		// Shared by ToSpec(), OpIndexFromString(), and the Operator combo
		// in RenderConditionFields - previously duplicated 3 times.
		constexpr const char* kOperatorSymbols[] = { "==", "!=", ">", ">=", "<", "<=" };
		constexpr int kOperatorCount = 6;

		// From the shared palette so the indicators match the Settings tab.
		constexpr auto kValidColor = UI::Style::kValidText;
		constexpr auto kInvalidColor = UI::Style::kInvalidText;

		// Fixed-size text buffers for ImGui::InputText, which needs a
		// stable char* that persists across frames (unlike a fresh
		// std::string each frame).
		struct EditableCondition
		{
			std::array<char, 64> function{};
			std::array<char, 128> param1{};
			std::array<char, 128> param2{};
			int opIndex = 0;   // index into kOperatorSymbols
			std::array<char, 64> value{ '1', '\0' };
			int runOn = RunOn::kSubject;
			int logicIndex = 0;  // 0 = AND, 1 = OR

			static ConditionSpec ToSpec(const EditableCondition& a_c)
			{
				ConditionSpec spec{};
				spec.function = a_c.function.data();
				spec.param1 = a_c.param1.data();
				spec.param2 = a_c.param2.data();
				spec.op = kOperatorSymbols[a_c.opIndex];
				spec.value = a_c.value.data();
				spec.runOn = a_c.runOn;
				spec.logic = a_c.logicIndex == 0 ? "AND" : "OR";
				return spec;
			}
		};

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

		template <std::size_t N>
		void CopyIntoBuffer(std::array<char, N>& a_buf, const std::string& a_src)
		{
			const std::size_t n = std::min(a_src.size(), N - 1);
			std::copy_n(a_src.data(), n, a_buf.data());
			a_buf[n] = '\0';
		}

		int OpIndexFromString(const std::string& a_op)
		{
			for (int i = 0; i < kOperatorCount; ++i) {
				if (a_op == kOperatorSymbols[i]) {
					return i;
				}
			}
			return 0;
		}

		// Case-insensitive substring search with zero heap allocations -
		// important here specifically, since this runs against a
		// candidate list that can be 50,000+ entries long (every EditorID
		// in the game) once per frame while a field is focused. Allocating
		// a lowercased copy of every candidate on every frame would be
		// real, visible overhead at that scale; comparing in place via a
		// case-insensitive predicate avoids it entirely.
		bool ContainsCaseInsensitive(std::string_view a_haystack, std::string_view a_needle)
		{
			if (a_needle.empty()) {
				return true;
			}
			return std::search(a_haystack.begin(), a_haystack.end(), a_needle.begin(), a_needle.end(),
				[](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }) != a_haystack.end();
		}

		// Autocomplete suggestions for the Function field are drawn from
		// the small, curated RecommendedConditionFunctions.h list (crafting-
		// relevant functions only), NOT the full 736-entry
		// ConditionFunctionTable.h - most of that full list is about
		// combat, dialogue, AI packages, and other things with no sane
		// meaning on a crafting recipe. This only affects what gets
		// SUGGESTED: typing the exact name of a function outside this
		// list still works fine on Save & Apply, since Conditions.cpp
		// resolves against the full table either way.
		const std::vector<Candidate>& AllFunctionNames()
		{
			static const std::vector<Candidate> all = []() {
				std::vector<Candidate> result;
				result.reserve(RecommendedConditions::kTableSize);
				for (std::size_t i = 0; i < RecommendedConditions::kTableSize; ++i) {
					result.push_back(Candidate{
						RecommendedConditions::kTable[i].name,
						RecommendedConditions::kTable[i].isBooleanValue ? "true/false" : "number" });
				}
				std::sort(result.begin(), result.end(),
					[](const Candidate& a, const Candidate& b) { return a.editorID < b.editorID; });
				return result;
			}();
			return all;
		}

		// Autocomplete suggestions for GetActorValue's param1, drawn from
		// ActorValueTable.h - the confirmed 0-163 Actor Value list from
		// the Creation Kit wiki, so you can type/pick "Smithing" instead
		// of needing to know it's ID 10.
		const std::vector<Candidate>& AllActorValueNames()
		{
			static const std::vector<Candidate> all = []() {
				std::vector<Candidate> result;
				result.reserve(ActorValues::kTableSize);
				for (std::size_t i = 0; i < ActorValues::kTableSize; ++i) {
					result.push_back(Candidate{
						ActorValues::kTable[i].name,
						fmt::format("ID {}", ActorValues::kTable[i].id) });
				}
				std::sort(result.begin(), result.end(),
					[](const Candidate& a, const Candidate& b) { return a.editorID < b.editorID; });
				return result;
			}();
			return all;
		}

		// Drawn between a text field and its suggestion list - the ordering
		// matters, see the note on AutocompleteInputText. Defined further
		// down; forward-declared so the template below can call them.
		void ValidationIndicator(const char* a_id);
		void ActorValueIndicator(const char* a_text);

		enum class Indicator
		{
			None,
			Form,        // green OK / red not found, via TESForm resolution
			ActorValue,  // green OK / red not found, via the Actor Value table
		};

		// Cached match list for whichever field is currently showing
		// suggestions. Only one field shows them at a time, so a single
		// shared instance is enough.
		struct SuggestionState
		{
			const void* owner = nullptr;  // buffer of the field the list belongs to
			std::string query;            // text the cached matches were computed for
			std::vector<const Candidate*> matches;

			// Value most recently chosen from the list. Keeps it closed
			// on that exact text instead of reopening under the cursor.
			std::string justPicked;



			void Close()
			{
				owner = nullptr;
				query.clear();
				matches.clear();
			}
		};

		SuggestionState& Suggestions()
		{
			static SuggestionState state;
			return state;
		}

		// Bumped every time a suggestion is picked, and appended to each
		// text field's ImGui ID (after "##", so it stays invisible).
		//
		// While an InputText is active, ImGui keeps its OWN copy of the
		// text and writes that copy back to the caller's buffer each
		// frame, which can silently undo a value written in from outside -
		// exactly what picking a suggestion does. Changing the widget's ID
		// makes ImGui treat it as a new widget and re-initialise from the
		// buffer, so the picked value survives.
		//
		// This guards a real ImGui behaviour, but it was NOT the reason
		// picking used to fail - that was the close-gate in
		// AutocompleteInputText. Kept because the hazard is genuine and
		// would otherwise surface as an intermittent lost edit.
		int& PickRevision()
		{
			static int revision = 0;
			return revision;
		}

		// Renders an InputTextWithHint, its optional validation indicator,
		// and - while the field has focus and at least 2 characters are
		// typed - a scrollable list of matching candidates below it.
		// Clicking a suggestion fills the buffer. Returns true if the
		// buffer changed this frame, by typing or by picking.
		//
		// The list is an INLINE child window, not a separate floating one.
		// A floating window renders above the page but is hit-tested in
		// window order, so once the main window took focus back the list
		// became visible but completely dead - no hover, no click, no
		// scroll. A child window belongs to this same window and always
		// receives input normally.
		//
		// The indicator is drawn BEFORE the list on purpose. Indicators
		// use SameLine(), and SameLine() after a child window rewinds the
		// layout cursor to that child's top edge, so anything drawn
		// afterwards would overlap the list. This ordering was introduced
		// while chasing a different bug and was never proven to be the
		// cause of anything - but it is the correct order regardless, and
		// costs nothing.
		template <std::size_t N>
		bool AutocompleteInputText(
			const char* a_label,
			const char* a_hint,
			std::array<char, N>& a_buf,
			const std::vector<Candidate>& a_candidates,
			Indicator a_indicator = Indicator::None)
		{
			constexpr std::size_t kMaxSuggestions = 15;
			constexpr std::size_t kMinCharsToSuggest = 2;

			// "label##N" renders as just "label" - the suffix only changes
			// the widget's identity. See PickRevision() for why it must.
			const std::string fieldID = fmt::format("{}##{}", a_label, PickRevision());

			bool changed = ImGui::InputTextWithHint(fieldID.c_str(), a_hint, a_buf.data(), a_buf.size());
			const bool isActive = ImGui::IsItemActive();


			// Before the list - see the note above.
			switch (a_indicator) {
			case Indicator::Form:
				ValidationIndicator(a_buf.data());
				break;
			case Indicator::ActorValue:
				ActorValueIndicator(a_buf.data());
				break;
			case Indicator::None:
				break;
			}

			auto& sugg = Suggestions();

			// Focusing a different field hands the list over to it and
			// invalidates the cached matches, which may have come from a
			// completely different candidate list.
			if (isActive && sugg.owner != a_buf.data()) {
				sugg.Close();
				sugg.owner = a_buf.data();
			}
			// OPENING is focus-driven; CLOSING deliberately is NOT.
			//
			// Every previous attempt gated drawing on some live input
			// state - the field being active, the list being hovered, the
			// mouse being held. Pressing a row falsifies the first, and
			// the other two proved unreliable through the framework's
			// GetProcAddress wrapper. Any of them going false between
			// mouse-press and mouse-RELEASE unsubmits the list, and since
			// a click is only reported on release, the pick could never
			// fire. The log confirmed it: not one pick across many
			// attempts.
			//
			// So once this field owns the list, the list keeps being
			// drawn until something DEFINITE closes it: a pick, the text
			// dropping below the minimum, or another field taking over.
			// None of those can happen mid-click.
			if (isActive && sugg.owner != a_buf.data()) {
				sugg.Close();
				sugg.owner = a_buf.data();
			}
			if (sugg.owner != a_buf.data()) {
				return changed;
			}

			const std::string_view current{ a_buf.data() };

			// Closed after a pick, and stays closed until the text is
			// edited to something else - otherwise the list would spring
			// straight back open on the value just chosen.
			if (!sugg.justPicked.empty()) {
				if (current == sugg.justPicked) {
					sugg.Close();
					return changed;
				}
				sugg.justPicked.clear();
			}

			if (current.size() < kMinCharsToSuggest) {
				sugg.query.clear();
				sugg.matches.clear();
				return changed;
			}

			// Only rescan when the typed text actually changes. The
			// candidate lists can hold 100k+ EditorIDs, and a rare
			// substring means scanning all of them - doing that every
			// frame is what made simply moving the mouse over the list
			// stutter, since the results can't change unless the query does.
			if (sugg.query != current) {
				sugg.query.assign(current);
				sugg.matches.clear();
				for (const auto& candidate : a_candidates) {
					if (ContainsCaseInsensitive(candidate.editorID, current)) {
						sugg.matches.push_back(&candidate);
						if (sugg.matches.size() >= kMaxSuggestions) {
							break;
						}
					}
				}
			}

			if (sugg.matches.empty()) {
				return changed;
			}

			// Drawn INLINE, right here, rather than floated over the page
			// at the end of the frame. An overlapping list looks better -
			// it doesn't shove the fields below it down - but it cannot
			// reliably be clicked: ImGui resolves hover by last-submitted
			// (so highlighting works) while the mouse-DOWN is claimed by
			// whichever widget at that position was submitted FIRST, which
			// is always one of the fields underneath. Occupying real
			// layout space is what makes the click land.
			constexpr float kVisibleRows = 8.0f;
			// Frame height, not text-line height: each row is a Button, and
			// a button is a framed widget (taller than a bare line of
			// text). Sizing on text height alone made the list shorter
			// than kVisibleRows actually needs.
			const float rowHeight = ImGui::GetFrameHeightWithSpacing();
			const float height =
				std::min<float>(static_cast<float>(sugg.matches.size()), kVisibleRows) * rowHeight + 8.0f;

			ImGui::PushID(a_label);

			// Styled to read as a list rather than a stack of chunky
			// buttons: transparent background, left-aligned text. These
			// stay Buttons on purpose - Button is the control proven to
			// work in this menu, and the log confirmed picks only started
			// firing once the close-gate was fixed. Restoring the LOOK of
			// a selectable list without going back to the widget whose
			// behaviour here was never established.
			ImGui::PushStyleColor(ImGui::ImGuiCol_Button, ImGui::ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
			ImGui::PushStyleVar(ImGui::ImGuiStyleVar_ButtonTextAlign, ImGui::ImVec2{ 0.0f, 0.5f });

			const Candidate* picked = nullptr;
			if (ImGui::BeginChild("##suggestions", ImGui::ImVec2{ 0.0f, height }, ImGui::ImGuiChildFlags_Border)) {
				for (const auto* match : sugg.matches) {
					// Button rather than Selectable. Not because Selectable
					// is broken - it was suspected during debugging, but
					// the actual cause of clicks never registering was the
					// close-gate above tearing the list down between mouse
					// press and release. Selectable would most likely work
					// now. Button is kept simply because it is the control
					// verified working here, and it is styled (transparent
					// background, left-aligned text) to read as a list row
					// anyway, so there is nothing to gain by switching back.
					//
					// Width -1 makes each row span the list, so the whole
					// row is the hit target rather than just the text.
					// Everything after "##" is ID-only and not displayed.
					const std::string rowLabel = match->detail.empty() ?
						fmt::format("{}##{}", match->editorID, match->editorID) :
						fmt::format("{:<34}{}##{}", match->editorID, match->detail, match->editorID);

					if (ImGui::Button(rowLabel.c_str(), ImGui::ImVec2{ -1.0f, 0.0f })) {
						picked = match;
						break;
					}
				}
			}
			ImGui::EndChild();

			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
			ImGui::PopID();

			// Applied after EndChild so the match list isn't mutated while
			// it's still being iterated.
			if (picked) {
				// Logged deliberately: after several rounds of failing to
				// reproduce this remotely, the log is the ground truth for
				// whether a click actually reaches this branch.
				SKSE::log::info("autocomplete: picked '{}'", picked->editorID);

				// The candidate's own spelling, so picking "IngotIron"
				// after typing "ingotiron" corrects the case.
				CopyIntoBuffer(a_buf, picked->editorID);
				changed = true;

				// Forces the field to rebuild from the buffer we just
				// wrote - see PickRevision().
				++PickRevision();

				// Without this the list would reopen immediately on the
				// value that was just chosen, right under the cursor.
				sugg.justPicked = picked->editorID;
				sugg.Close();
			}

			return changed;
		}

		EditableCondition FromSpec(const ConditionSpec& a_spec)
		{
			EditableCondition c{};
			CopyIntoBuffer(c.function, a_spec.function);
			CopyIntoBuffer(c.param1, a_spec.param1);
			CopyIntoBuffer(c.param2, a_spec.param2);
			c.opIndex = OpIndexFromString(a_spec.op);
			CopyIntoBuffer(c.value, a_spec.value);
			c.runOn = a_spec.runOn;
			c.logicIndex = (a_spec.logic == "OR" || a_spec.logic == "or") ? 1 : 0;
			return c;
		}

		// One-line summary of a condition for the collapsed headers, in the
		// same shape the log uses (e.g. "HasPerk(SteelSmithing) == True"),
		// so an entry can be read at a glance without expanding it.
		std::string SummariseCondition(const EditableCondition& a_c)
		{
			const char* function = a_c.function.data();
			const char* param1 = a_c.param1.data();
			const char* param2 = a_c.param2.data();

			std::string params = param1;
			if (param2[0] != '\0') {
				params += ", ";
				params += param2;
			}

			// Boolean functions store "1"/"0" but read better as
			// True/False - the same mapping the Value dropdown shows.
			std::string value = a_c.value.data();
			if (RecommendedConditions::IsBooleanFunction(std::string_view{ function })) {
				value = (value == "1") ? "True" : "False";
			}

			std::string summary = fmt::format("{}({}) {} {}",
				function[0] != '\0' ? function : "(unset function)",
				params,
				kOperatorSymbols[a_c.opIndex],
				value);

			// AND is the default and would be noise on every row; OR is
			// the one worth surfacing, since it changes how this row
			// combines with the next one for the same material/recipe.
			if (a_c.logicIndex == 1) {
				summary += "  [OR next]";
			}

			return summary;
		}

		// Process-wide editor state. Loaded from disk on first Render()
		// call (or via the Reload button); edits stay purely in memory
		// until Save & Apply is pressed.
		struct EditorState
		{
			bool loaded = false;
			std::vector<EditableMapping> mappingRows;
			std::vector<EditableOverride> overrideRows;
			std::string lastSaveSummary;

			// Which file is currently open for editing - the main config
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
			// rescanned per frame - FindExternalConfigPaths() walks a
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
			// Deliberately parses just this one file - NOT the no-arg
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
				const std::string param1{ row.condition.param1.data() };
				if (material.empty() || param1.empty()) {
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
					continue;  // an exclude row for this recipe already won - ignore condition rows
				}

				const std::string param1{ row.condition.param1.data() };
				if (param1.empty()) {
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

			// Main config first, then the external files - the same order
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
			// having Save/Reload stranded at the bottom of a long page -
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
				// typed name actually becomes on disk - the name is
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

		// Small green "OK" / red "not found" indicator for a live-typed
		// identifier. Blank fields show a neutral marker rather than a
		// scary red mark on a freshly-added, not-yet-filled-in row.
		void ValidationIndicator(const char* a_id)
		{
			const std::string id{ a_id };
			if (id.empty()) {
				ImGui::SameLine();
				ImGui::TextDisabled("(empty)");
				return;
			}

			// TESForm::As<T>() is a single generic template covering every
			// listed form type via a switch on GetFormType() (verified by
			// reading FormTraits.h directly) - so a plain TESForm lookup
			// works here as a genuine "does this resolve to anything at
			// all" check.
			auto* form = ResolveIdentifier<RE::TESForm>(id);
			ImGui::SameLine();
			if (form) {
				ImGui::TextColored(kValidColor, "OK");
			} else {
				ImGui::TextColored(kInvalidColor, "not found");
			}
		}

		// Same idea for GetActorValue's param1, which is an Actor Value
		// name or a raw ID (0-163) rather than a form, so the form-based
		// check above would be the wrong test entirely.
		void ActorValueIndicator(const char* a_text)
		{
			const std::string text{ a_text };
			ImGui::SameLine();
			if (text.empty()) {
				ImGui::TextDisabled("(empty)");
				return;
			}

			// Name lookup first: it's a cheap table scan, it's the common
			// case (autocomplete offers names, not numbers), and it can't
			// throw. Only fall back to the numeric parse when the text
			// actually starts with a digit - std::stoi signals failure by
			// throwing, and this re-runs every frame the field is on
			// screen, so a name-style value would otherwise throw and
			// catch an exception ~60 times a second. (Actor Value IDs are
			// 0-163, so a leading digit is a safe gate - none are negative.)
			int unusedID = 0;
			bool valid = ActorValues::TryResolve(text, unusedID);
			if (!valid && std::isdigit(static_cast<unsigned char>(text.front())) != 0) {
				try {
					std::size_t consumed = 0;
					const int parsed = std::stoi(text, &consumed);
					valid = (consumed == text.size()) && parsed >= 0 && parsed <= 163;
				} catch (const std::exception&) {
					valid = false;
				}
			}

			if (valid) {
				ImGui::TextColored(kValidColor, "OK");
			} else {
				ImGui::TextColored(kInvalidColor, "not found");
			}
		}

		// Renders the function/param1/param2/operator/value/logic fields
		// for one condition, in a compact vertical block. Does NOT render
		// a "Run On" control - see the comment further down for why.
		void RenderConditionFields(EditableCondition& a_c)
		{
			static const char* logicOptions[] = { "AND", "OR" };

			ImGui::PushItemWidth(220.0f);
			AutocompleteInputText("Function", "e.g. HasPerk", a_c.function, AllFunctionNames());
			ImGui::PopItemWidth();

			// GetPCIsSex's param1 is a Male/Female choice (confirmed: 0 =
			// Male, 1 = Female), not a form or a number someone should have
			// to know off-hand - show a plain dropdown instead of the
			// generic text field for this one specific function. Computed
			// AFTER the Function field above so a function name typed this
			// same frame is reflected immediately, not one frame late.
			// Which candidates to suggest is driven by the curated table
			// (see RecommendedConditionFunctions.h) rather than by
			// comparing function names here, so adding a function to that
			// table automatically teaches this field what to offer.
			using ParamKind = RecommendedConditions::ParamKind;
			const std::string_view currentFunction{ a_c.function.data() };
			const ParamKind kind = RecommendedConditions::Param1KindFor(currentFunction);

			ImGui::PushItemWidth(220.0f);
			switch (kind) {
			case ParamKind::kSex: {
				// A fixed two-way choice, not a form - a dropdown beats
				// making someone remember that 0 is Male.
				static const char* sexOptions[] = { "Male", "Female" };
				int sexIndex = (std::string_view{ a_c.param1.data() } == "1") ? 1 : 0;
				if (ImGui::Combo("Param 1 (Sex)", &sexIndex, sexOptions, 2)) {
					CopyIntoBuffer(a_c.param1, sexIndex == 1 ? "1"s : "0"s);
				}
				break;
			}
			case ParamKind::kActorValue:
				// An Actor Value name or 0-163, not a form, so the
				// form-resolution indicator would be the wrong check.
				AutocompleteInputText("Param 1 (Actor Value)", "name or 0-163",
					a_c.param1, AllActorValueNames(), Indicator::ActorValue);
				break;
			case ParamKind::kNone:
				// Drawn but empty-hinted rather than hidden: the function
				// name is free text, so it may be mid-typing or something
				// outside the curated table, and hiding the field would
				// make an already-filled param1 silently unreachable.
				AutocompleteInputText("Param 1 (unused)", "not used by this function",
					a_c.param1, AllEditorIDs(), Indicator::None);
				break;
			default: {
				// Everything else is a form of one specific record type.
				const auto* candidates = &AllEditorIDs();
				const char* hint = "identifier / number";
				switch (kind) {
				case ParamKind::kCraftableItem:
					candidates = &AllCraftableItemEditorIDs();
					hint = "item EditorID";
					break;
				case ParamKind::kPerk:
					candidates = &CandidatesForFormType(RE::FormType::Perk);
					hint = "perk EditorID";
					break;
				case ParamKind::kSpell:
					candidates = &CandidatesForFormType(RE::FormType::Spell);
					hint = "spell EditorID";
					break;
				case ParamKind::kQuest:
					candidates = &CandidatesForFormType(RE::FormType::Quest);
					hint = "quest EditorID";
					break;
				case ParamKind::kFaction:
					candidates = &CandidatesForFormType(RE::FormType::Faction);
					hint = "faction EditorID";
					break;
				case ParamKind::kGlobal:
					candidates = &CandidatesForFormType(RE::FormType::Global);
					hint = "global EditorID";
					break;
				case ParamKind::kRace:
					candidates = &CandidatesForFormType(RE::FormType::Race);
					hint = "race EditorID";
					break;
				default:
					break;
				}

				// If a type's cache came back empty (nothing of that type
				// loaded), fall back to the full list rather than leaving
				// the field with no suggestions at all.
				if (candidates->empty()) {
					candidates = &AllEditorIDs();
				}

				AutocompleteInputText("Param 1", hint, a_c.param1, *candidates, Indicator::Form);
				break;
			}
			}
			ImGui::PopItemWidth();

			ImGui::PushItemWidth(220.0f);
			AutocompleteInputText("Param 2 (optional)", "identifier / number", a_c.param2, AllEditorIDs());
			ImGui::PopItemWidth();

			ImGui::PushItemWidth(90.0f);
			ImGui::Combo("Operator", &a_c.opIndex, kOperatorSymbols, kOperatorCount);
			ImGui::SameLine();
			// Deliberately True/False only, not a text field that could
			// also take a Global identifier - the underlying engine (see
			// Conditions.cpp) supports comparing any function's result
			// against a Global instead of a literal, but for these
			// boolean-only functions that capability is redundant: anyone
			// wanting a Global-driven comparison already has a direct path
			// via the GetGlobalValue function itself, so there's no need
			// for every boolean function to duplicate that here too.
			if (RecommendedConditions::IsBooleanFunction(std::string_view{ a_c.function.data() })) {
				static const char* boolOptions[] = { "False", "True" };
				int boolIndex = (std::string_view{ a_c.value.data() } == "1") ? 1 : 0;
				if (ImGui::Combo("Value", &boolIndex, boolOptions, 2)) {
					CopyIntoBuffer(a_c.value, boolIndex == 1 ? "1"s : "0"s);
				}
			} else {
				ImGui::InputText("Value", a_c.value.data(), a_c.value.size());
			}
			ImGui::PopItemWidth();

			// No "Run On" control here on purpose - crafting is always
			// done by the player, so it always needs to be Subject (the
			// crafting actor) in practice; every other option (Target,
			// Combat Target, ...) has no sensible meaning for a recipe
			// condition. EditableCondition::runOn stays defaulted to
			// RunOn::kSubject and is never changed by this editor. The
			// underlying field/JSON key is still there for hand-editing
			// an unusual case, just not surfaced here.
			ImGui::PushItemWidth(150.0f);
			ImGui::Combo("Logic (vs. next)", &a_c.logicIndex, logicOptions, 2);
			ImGui::PopItemWidth();
		}

		// A reference list of the curated, crafting-relevant functions and
		// what their fields mean - the Function field's autocomplete only
		// suggests from this same list (see AllFunctionNames() above), so
		// this is "what autocomplete will offer you", spelled out with the
		// usage hints autocomplete itself can't show inline.
		void RenderFunctionReference()
		{
			if (!ImGui::TreeNode("Which Function should I use? (click to expand)")) {
				return;
			}

			ImGui::TextWrapped(
				"The Function field's autocomplete only suggests from this "
				"list - typing an exact name outside it still works, it just "
				"won't be suggested.");
			ImGui::Spacing();

			for (std::size_t i = 0; i < RecommendedConditions::kTableSize; ++i) {
				const auto& entry = RecommendedConditions::kTable[i];

				ImGui::Bullet();
				ImGui::SameLine();
				ImGui::Text("%s", entry.name);
				ImGui::Indent();
				ImGui::TextWrapped("%s", entry.usage);
				ImGui::Unindent();
			}

			ImGui::TreePop();
		}

		void RenderMappingsSection()
		{
			auto& state = State();

			// Scope every widget in this section under its own ID.
			// Without this, this section's PushID(i) and the overrides
			// section's PushID(i) produce the SAME ImGui IDs for the same
			// row index and label (e.g. mapping row 0's "Function" and
			// override row 0's "Function"), and ImGui treats same-ID
			// widgets as literally the same widget - so typing in one
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
					ImGui::InputTextWithHint("Comment", "optional note - not used by the patcher",
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
		// one "exclude", and one "comment" - never one per condition. Each
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

			// Distinct ID scope from the mappings section - see the note
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
						// to Existing, not excluded, and blank - see
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
					if (ImGui::InputTextWithHint("Comment", "optional note - not used by the patcher",
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
		// Shared palette - see UIStyle.h. Also what makes the dropdown
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
