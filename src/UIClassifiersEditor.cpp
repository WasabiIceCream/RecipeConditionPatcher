#include "UIClassifiersEditor.h"

#include "ClassifierGroupEditor.h"
#include "ClassifierPredicateEditor.h"
#include "ConfigFile.h"
#include "RecipePatcher.h"
#include "UIAutocomplete.h"
#include "UIConditionFields.h"

#include "SKSEMenuFramework.h"
#include "UIStyle.h"

#include <spdlog/fmt/fmt.h>

namespace ImGui = ImGuiMCP;

namespace RPP::UI::ClassifiersEditor
{
	using RPP::ClassifierEditor::EditableClassifierGroup;
	using RPP::ClassifierEditor::EditableClassifierRule;
	using RPP::ClassifierEditor::EditableMatch;
	using RPP::ClassifierEditor::MatchBlock;
	using RPP::ClassifierEditor::MatchRow;
	using RPP::ClassifierEditor::RowKind;

	namespace
	{
		// ------------------------------------------------------------------
		// Row-kind metadata: the fixed set the "Field" dropdown offers, and
		// what to suggest/validate for each one's value list.
		// ------------------------------------------------------------------

		constexpr RowKind kAllRowKinds[] = {
			RowKind::kSignature,
			RowKind::kKeyword,
			RowKind::kArmorType,
			RowKind::kEdidContains,
			RowKind::kFullContains,
			RowKind::kRecipeEdidContains,
			RowKind::kRecipeHasCondition,
		};
		constexpr int kRowKindCount = static_cast<int>(std::size(kAllRowKinds));

		int RowKindIndex(RowKind a_kind)
		{
			for (int i = 0; i < kRowKindCount; ++i) {
				if (kAllRowKinds[i] == a_kind) {
					return i;
				}
			}
			return 0;
		}

		const std::array<const char*, kRowKindCount>& RowKindLabels()
		{
			static const std::array<const char*, kRowKindCount> labels = []() {
				std::array<const char*, kRowKindCount> l{};
				for (int i = 0; i < kRowKindCount; ++i) {
					l[static_cast<std::size_t>(i)] = ClassifierEditor::RowKindLabel(kAllRowKinds[i]).data();
				}
				return l;
			}();
			return labels;
		}

		// Fixed, small enumerations, suggested via the same autocomplete
		// widget as everything else, but since the real set is this short,
		// typing anything outside it is still accepted (same "suggestion,
		// not restriction" philosophy as the Mappings tab's Function field).
		const std::vector<Candidate>& SignatureCandidates()
		{
			static const std::vector<Candidate> c = {
				{ "ARMO", "Armor" }, { "WEAP", "Weapon" }, { "AMMO", "Ammunition" }, { "MISC", "Misc item" },
				{ "KEYM", "Key" }, { "BOOK", "Book" }, { "INGR", "Ingredient" }, { "ALCH", "Potion" },
			};
			return c;
		}

		const std::vector<Candidate>& ArmorTypeCandidates()
		{
			static const std::vector<Candidate> c = { { "Light", "" }, { "Heavy", "" }, { "Clothing", "" } };
			return c;
		}

		const std::vector<Candidate>& EmptyCandidates()
		{
			static const std::vector<Candidate> c;
			return c;
		}

		struct RowCandidates
		{
			const std::vector<Candidate>* list;
			Indicator indicator;
			const char* hint;
		};

		// edidContains/fullContains/recipeEdidContains are substring
		// matches, not identifier lookups; no candidate list makes sense
		// for them (there's nothing to "resolve"), so they get a plain
		// free-text field with no suggestions and no OK/not-found
		// indicator, unlike every other row kind here.
		RowCandidates CandidatesForRowKind(RowKind a_kind)
		{
			switch (a_kind) {
			case RowKind::kSignature:
				return { &SignatureCandidates(), Indicator::None, "e.g. ARMO" };
			case RowKind::kArmorType:
				return { &ArmorTypeCandidates(), Indicator::None, "Light / Heavy / Clothing" };
			case RowKind::kKeyword: {
				const auto& kw = CandidatesForFormType(RE::FormType::Keyword);
				return { kw.empty() ? &AllEditorIDs() : &kw, Indicator::Form, "keyword EditorID" };
			}
			case RowKind::kRecipeHasCondition:
				return { &AllEditorIDs(), Indicator::Form, "Global/Perk/... EditorID" };
			case RowKind::kEdidContains:
			case RowKind::kFullContains:
			case RowKind::kRecipeEdidContains:
			default:
				return { &EmptyCandidates(), Indicator::None, "substring (case-insensitive)" };
			}
		}

		// ------------------------------------------------------------------
		// Add/remove list of autocompleted text fields, the same widget
		// shape used for benchKeywords and every row's value
		// list, just with a different buffer size/candidate list/hint.
		// ------------------------------------------------------------------

		template <std::size_t N>
		void RenderValueList(std::vector<std::array<char, N>>& a_list, const std::vector<Candidate>& a_candidates, Indicator a_indicator, const char* a_hint)
		{
			int deleteIndex = -1;
			for (int i = 0; i < static_cast<int>(a_list.size()); ++i) {
				ImGui::PushID(i);
				ImGui::PushItemWidth(240.0f);
				AutocompleteInputText("##v", a_hint, a_list[i], a_candidates, a_indicator);
				ImGui::PopItemWidth();
				ImGui::SameLine();
				if (ImGui::SmallButton("x")) {
					deleteIndex = i;
				}
				ImGui::PopID();
			}
			if (deleteIndex >= 0) {
				a_list.erase(a_list.begin() + deleteIndex);
			}
			if (ImGui::SmallButton("+")) {
				a_list.emplace_back();
			}
		}

		// Same widget as RenderValueList, but packs entries several to a
		// line instead of one per line: bench keyword lists are usually
		// short strings (CraftingSmithingForge, ...) where one-per-line
		// wastes a lot of vertical space for very little content. Only
		// used for benchKeywords, not the match-row value lists above
		// (those can hold long free-text substrings/EditorIDs where the
		// full row width actually helps).
		//
		// Deliberately falls back to one-per-line whenever the item just
		// drawn owns the suggestion popup (Suggestions().owner == its
		// buffer): AutocompleteInputText's own comment on that popup
		// explains that SameLine() after the popup's child window rewinds
		// the cursor to the popup's TOP edge, so packing the NEXT field in
		// beside a field whose popup is currently open would sit it right
		// next to that field's input box while the popup is still open
		// below (fine visually, but untested in-game), so this only takes
		// that layout while nothing is actively suggesting.
		template <std::size_t N>
		void RenderBenchKeywordList(std::vector<std::array<char, N>>& a_list, const std::vector<Candidate>& a_candidates, Indicator a_indicator, const char* a_hint)
		{
			constexpr float kItemWidth = 240.0f;
			// Rough allowance for the NEXT item's own trailing OK/not-found
			// indicator + "x" button, so the wrap check is sized against
			// what that item will actually occupy, not just its input box.
			constexpr float kTrailingControlsWidth = 90.0f;
			const float windowRightEdge = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;

			int deleteIndex = -1;
			for (int i = 0; i < static_cast<int>(a_list.size()); ++i) {
				ImGui::PushID(i);
				ImGui::PushItemWidth(kItemWidth);
				AutocompleteInputText("##v", a_hint, a_list[i], a_candidates, a_indicator);
				ImGui::PopItemWidth();
				ImGui::SameLine();
				if (ImGui::SmallButton("x")) {
					deleteIndex = i;
				}
				const bool popupOpen = Suggestions().owner == static_cast<const void*>(a_list[i].data());
				ImGui::PopID();

				const bool hasNext = (i + 1) < static_cast<int>(a_list.size());
				if (hasNext && !popupOpen) {
					const float nextRightEdge = ImGui::GetItemRectMax().x + ImGui::GetStyle()->ItemSpacing.x + kItemWidth + kTrailingControlsWidth;
					if (nextRightEdge < windowRightEdge) {
						ImGui::SameLine();
					}
				}
			}
			if (deleteIndex >= 0) {
				a_list.erase(a_list.begin() + deleteIndex);
			}
			if (ImGui::SmallButton("+")) {
				a_list.emplace_back();
			}
		}

		// ------------------------------------------------------------------
		// Match predicate editor: OR of blocks, each block an AND of rows.
		// See ClassifierPredicateEditor.h for why this bounded two-level
		// shape (not arbitrary all/any/not nesting) covers essentially
		// every real-world predicate, with a read-only fallback for the
		// rare one that doesn't fit.
		// ------------------------------------------------------------------

		void RenderMatchRow(MatchRow& a_row, bool& a_outDelete)
		{
			ImGui::PushItemWidth(240.0f);
			int kindIndex = RowKindIndex(a_row.kind);
			if (ImGui::Combo("Field", &kindIndex, RowKindLabels().data(), kRowKindCount)) {
				a_row.kind = kAllRowKinds[kindIndex];
			}
			ImGui::PopItemWidth();

			ImGui::SameLine();
			ImGui::Checkbox("NOT", &a_row.negate);

			ImGui::SameLine();
			if (ImGui::SmallButton("Remove condition")) {
				a_outDelete = true;
			}

			const RowCandidates rc = CandidatesForRowKind(a_row.kind);
			ImGui::Indent();
			RenderValueList(a_row.values, *rc.list, rc.indicator, rc.hint);
			ImGui::Unindent();
		}

		void RenderMatchBlock(MatchBlock& a_block, bool& a_outDelete)
		{
			ImGui::TextDisabled("ALL of:");
			int deleteRow = -1;
			for (int i = 0; i < static_cast<int>(a_block.rows.size()); ++i) {
				ImGui::PushID(i);
				bool del = false;
				RenderMatchRow(a_block.rows[i], del);
				if (del) {
					deleteRow = i;
				}
				ImGui::Separator();
				ImGui::PopID();
			}
			if (deleteRow >= 0) {
				a_block.rows.erase(a_block.rows.begin() + deleteRow);
			}

			if (ImGui::SmallButton("+ Add condition (AND)")) {
				a_block.rows.emplace_back();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove this OR-alternative")) {
				a_outDelete = true;
			}
		}

		// a_label doubles as both the heading text and the ImGui ID scope
		// for this whole editor. Callers pass something unique within
		// their own PushID scope (e.g. "When" vs "Match").
		void RenderMatchEditor(const char* a_label, EditableMatch& a_match)
		{
			ImGui::PushID(a_label);
			UI::Style::Heading(a_label);

			if (a_match.isFallback) {
				UI::Style::WarnHint(
					"This predicate is too complex for the visual editor (nesting or a boolean "
					"shape the block editor can't represent), shown read-only below. Edit the "
					"file directly to change it; saving from this menu leaves it untouched.");
				const std::string raw = a_match.fallbackRaw.dump(2);
				if (ImGui::BeginChild("##rawjson", ImGui::ImVec2{ 0.0f, 150.0f }, ImGui::ImGuiChildFlags_Border)) {
					ImGui::TextUnformatted(raw.c_str());
				}
				ImGui::EndChild();
				ImGui::PopID();
				return;
			}

			if (a_match.blocks.empty()) {
				ImGui::TextDisabled("(always matches, no restriction)");
			} else {
				UI::Style::Hint(
					"Each condition matches if it's true for ANY of its listed values. Conditions "
					"in one \"ALL of\" box are ANDed; separate OR-alternatives are ORed.");
			}

			int deleteBlock = -1;
			for (int i = 0; i < static_cast<int>(a_match.blocks.size()); ++i) {
				ImGui::PushID(i);
				if (i > 0) {
					ImGui::TextDisabled("-- OR --");
				}
				bool del = false;
				RenderMatchBlock(a_match.blocks[i], del);
				if (del) {
					deleteBlock = i;
				}
				ImGui::PopID();
			}
			if (deleteBlock >= 0) {
				a_match.blocks.erase(a_match.blocks.begin() + deleteBlock);
			}

			if (ImGui::SmallButton("+ Add OR-alternative")) {
				a_match.blocks.emplace_back();
				a_match.blocks.back().rows.emplace_back();
			}

			ImGui::PopID();
		}

		// ------------------------------------------------------------------
		// Rule conditions: one uniform list reusing RenderConditionFields
		// verbatim (same widget the Mappings tab uses). A plain Global
		// check is just a condition here too (Function = GetGlobalValue,
		// Param 1 = the global's EditorID, Value = True); there's no
		// separate "Set Global" shortcut in the UI, one condition list is
		// simpler than two ways to say the same thing. (The JSON
		// `setGlobal` shorthand still exists for hand-editing (see
		// Classifiers.h); the editor just always saves the expanded form.)
		// ------------------------------------------------------------------

		void RenderRuleConditions(EditableClassifierRule& a_rule)
		{
			UI::Style::Heading("Conditions");
			UI::Style::Hint("What to add when this rule's Match fires. For a plain global check, use Function = GetGlobalValue, Param 1 = the global's EditorID.");

			int deleteIndex = -1;
			for (int i = 0; i < static_cast<int>(a_rule.conditions.size()); ++i) {
				ImGui::PushID(i);
				ImGui::Separator();
				RenderConditionFields(a_rule.conditions[i]);
				if (ImGui::SmallButton("Remove this condition")) {
					deleteIndex = i;
				}
				ImGui::PopID();
			}
			if (deleteIndex >= 0) {
				a_rule.conditions.erase(a_rule.conditions.begin() + deleteIndex);
			}
			if (!a_rule.conditions.empty()) {
				ImGui::Separator();
			}
			if (ImGui::SmallButton("+ Add condition")) {
				a_rule.conditions.emplace_back();
			}
		}

		// One-line summary for a rule's collapsed header.
		std::string SummariseRule(const EditableClassifierRule& a_rule)
		{
			if (a_rule.conditions.empty()) {
				return "(empty, add a condition)";
			}
			if (a_rule.conditions.size() == 1) {
				return SummariseCondition(a_rule.conditions.front());
			}
			return fmt::format("{}, +{} more", SummariseCondition(a_rule.conditions.front()), a_rule.conditions.size() - 1);
		}

		void RenderRule(EditableClassifierRule& a_rule, int a_index, bool& a_outDelete)
		{
			const std::string header = fmt::format("Rule {}: {}###rule{}", a_index + 1, SummariseRule(a_rule), a_index);
			if (ImGui::CollapsingHeader(header.c_str())) {
				ImGui::Indent();

				RenderMatchEditor("Match", a_rule.match);
				ImGui::Spacing();
				RenderRuleConditions(a_rule);

				ImGui::Spacing();
				ImGui::PushItemWidth(-120.0f);
				ImGui::InputTextWithHint("Comment", "optional note, not used by the patcher",
					a_rule.comment.data(), a_rule.comment.size());
				ImGui::PopItemWidth();

				if (ImGui::SmallButton("Remove this rule")) {
					a_outDelete = true;
				}

				ImGui::Unindent();
				ImGui::Spacing();
			}
		}

		// ------------------------------------------------------------------
		// Group: benchKeyword restriction, shared "when" guard, and its
		// first-match-wins rule chain.
		// ------------------------------------------------------------------

		void RenderGroup(EditableClassifierGroup& a_group, int a_index, bool& a_outDelete)
		{
			const std::string comment{ a_group.comment.data() };
			const std::string header = fmt::format("Group {}: {}###group{}",
				a_index + 1, comment.empty() ? "(no comment)" : comment, a_index);

			if (ImGui::CollapsingHeader(header.c_str())) {
				ImGui::Indent();

				UI::Style::Heading("Crafting bench(es)");
				UI::Style::Hint(
					"Restrict this group to recipes made at a specific bench (e.g. "
					"CraftingSmithingForge, CraftingSmelter, CraftingTanningRack). Leave empty "
					"for any bench.");
				RenderBenchKeywordList(a_group.benchKeywords, CandidatesForFormType(RE::FormType::Keyword), Indicator::Form, "bench keyword EditorID");

				ImGui::Spacing();
				RenderMatchEditor("When (applies to every rule below)", a_group.when);

				ImGui::Spacing();
				UI::Style::Heading("Rules (first match wins)");
				UI::Style::Hint("For a given recipe, the first rule below whose Match is true is the one that applies.");

				int deleteRule = -1;
				for (int i = 0; i < static_cast<int>(a_group.rules.size()); ++i) {
					ImGui::PushID(i);
					bool del = false;
					RenderRule(a_group.rules[i], i, del);
					if (del) {
						deleteRule = i;
					}
					ImGui::PopID();
				}
				if (deleteRule >= 0) {
					a_group.rules.erase(a_group.rules.begin() + deleteRule);
				}
				if (ImGui::SmallButton("+ Add Rule")) {
					a_group.rules.emplace_back();
				}

				ImGui::Spacing();
				ImGui::PushItemWidth(-120.0f);
				ImGui::InputTextWithHint("Comment", "optional note, not used by the patcher",
					a_group.comment.data(), a_group.comment.size());
				ImGui::PopItemWidth();

				ImGui::Spacing();
				ImGui::Separator();
				if (ImGui::SmallButton("Remove this entire group")) {
					a_outDelete = true;
				}

				ImGui::Unindent();
				ImGui::Spacing();
			}
		}

		// ------------------------------------------------------------------
		// File picker + load/save, same architecture as MappingsEditor's
		// (own independent EditorState/file selection, not shared with the
		// Mappings tab).
		// ------------------------------------------------------------------

		struct EditorState
		{
			bool loaded = false;
			std::vector<EditableClassifierGroup> groups;
			std::string lastSaveSummary;
			std::string targetPath{ ConfigFile::kPath };

			bool showSaveAs = false;
			bool saveAsBlank = false;
			std::array<char, 128> saveAsName{};

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
			state.groups = ClassifierEditor::ParseClassifierGroups(config);

			state.loaded = true;
			state.lastSaveSummary.clear();
		}

		void SaveToDisk()
		{
			auto& state = State();

			// Read-modify-write: only the "classifiers" key changes,
			// same convention as SaveUserTriggerEntries/SaveRecipeOverrides,
			// so settings/mappings/recipeOverrides already in this file
			// (and any "//" comment keys) survive untouched.
			auto j = ConfigFile::ReadFile(state.targetPath);
			j["classifiers"] = ClassifierEditor::SerializeClassifierGroups(state.groups);
			ConfigFile::WriteFile(state.targetPath, j);

			state.lastSaveSummary = fmt::format("Saved {} classifier group(s). Re-applying...", state.groups.size());

			ApplyPerkRequirementsToAllRecipes();
		}

		std::string FileLabel(const std::string& a_path)
		{
			const auto slash = a_path.find_last_of("/\\");
			return slash == std::string::npos ? a_path : a_path.substr(slash + 1);
		}

		void RenderFilePicker()
		{
			auto& state = State();

			const auto& paths = state.availablePaths;
			if (paths.empty()) {
				return;
			}

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

				const std::string newPath = ConfigFile::MakeExternalConfigPath(state.saveAsName.data());
				if (newPath.empty()) {
					UI::Style::Hint("Enter a name to continue.");
				} else {
					ImGui::TextColored(UI::Style::kAccentText, "Creates: %s", newPath.c_str());
					if (ImGui::Button(state.saveAsBlank ? "Create empty config" : "Create copy")) {
						state.targetPath = newPath;
						if (state.saveAsBlank) {
							state.groups.clear();
						}
						SaveToDisk();
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
				"This tab's file selection is independent from the Mappings tab's.");
		}
	}

	void __stdcall Render()
	{
		UI::Style::Push();

		auto& state = State();
		if (!state.loaded) {
			ReloadFromDisk();
		}

		ImGui::TextWrapped(
			"Classify recipes by what they PRODUCE (record type, keywords, name) instead of "
			"what they require. See the README's \"Classifiers\" section for the full JSON "
			"schema this edits. Changes here stay local to this menu until you press "
			"\"Save & Apply\".");

		ImGui::Spacing();
		RenderFilePicker();
		ImGui::Spacing();

		int deleteGroup = -1;
		for (int i = 0; i < static_cast<int>(state.groups.size()); ++i) {
			ImGui::PushID(i);
			bool del = false;
			RenderGroup(state.groups[i], i, del);
			if (del) {
				deleteGroup = i;
			}
			ImGui::PopID();
		}
		if (deleteGroup >= 0) {
			state.groups.erase(state.groups.begin() + deleteGroup);
		}

		ImGui::Spacing();
		if (ImGui::SmallButton("+ Add Group")) {
			state.groups.emplace_back();
			state.groups.back().rules.emplace_back();
		}

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
