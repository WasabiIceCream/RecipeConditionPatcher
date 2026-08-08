#include "UIConditionFields.h"

#include "ActorValueTable.h"
#include "EditorIDCache.h"
#include "RecommendedConditionFunctions.h"
#include "UIAutocomplete.h"

#include <spdlog/fmt/fmt.h>

namespace RPP::UI
{
	namespace
	{
		// Autocomplete suggestions for the Function field are drawn from
		// the small, curated RecommendedConditionFunctions.h list
		// (crafting-relevant functions only), NOT the full 736-entry
		// ConditionFunctionTable.h. Most of that full list is about
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
		// ActorValueTable.h, the confirmed 0-163 Actor Value list from
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

	bool HasUsableParam1(const EditableCondition& a_c)
	{
		if (a_c.param1[0] != '\0') {
			return true;
		}
		return RecommendedConditions::Param1KindFor(std::string_view{ a_c.function.data() }) ==
		       RecommendedConditions::ParamKind::kNone;
	}

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

		// Boolean functions store "1"/"0" but read better as True/False,
		// the same mapping the Value dropdown shows.
		std::string value = a_c.value.data();
		if (RecommendedConditions::IsBooleanFunction(std::string_view{ function })) {
			value = (value == "1") ? "True" : "False";
		}

		std::string summary = fmt::format("{}({}) {} {}",
			function[0] != '\0' ? function : "(unset function)",
			params,
			kOperatorSymbols[a_c.opIndex],
			value);

		// AND is the default and would be noise on every row; OR is the
		// one worth surfacing, since it changes how this row combines
		// with the next one for the same material/recipe.
		if (a_c.logicIndex == 1) {
			summary += "  [OR next]";
		}

		return summary;
	}

	void RenderConditionFields(EditableCondition& a_c)
	{
		static const char* logicOptions[] = { "AND", "OR" };

		ImGui::PushItemWidth(220.0f);
		AutocompleteInputText("Function", "e.g. HasPerk", a_c.function, AllFunctionNames());
		ImGui::PopItemWidth();

		// GetPCIsSex's param1 is a Male/Female choice (confirmed: 0 =
		// Male, 1 = Female), not a form or a number someone should have
		// to know off-hand. Show a plain dropdown instead of the
		// generic text field for this one specific function. Computed
		// AFTER the Function field above so a function name typed this
		// same frame is reflected immediately, not one frame late. Which
		// candidates to suggest is driven by the curated table (see
		// RecommendedConditionFunctions.h) rather than by comparing
		// function names here, so adding a function to that table
		// automatically teaches this field what to offer.
		using ParamKind = RecommendedConditions::ParamKind;
		const std::string_view currentFunction{ a_c.function.data() };
		const ParamKind kind = RecommendedConditions::Param1KindFor(currentFunction);

		ImGui::PushItemWidth(220.0f);
		switch (kind) {
		case ParamKind::kSex: {
			// A fixed two-way choice, not a form. A dropdown beats
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
		// Deliberately True/False only, not a text field that could also
		// take a Global identifier. The underlying engine (see
		// Conditions.cpp) supports comparing any function's result
		// against a Global instead of a literal, but for these
		// boolean-only functions that capability is redundant: anyone
		// wanting a Global-driven comparison already has a direct path
		// via the GetGlobalValue function itself, so there's no need for
		// every boolean function to duplicate that here too.
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

		ImGui::PushItemWidth(150.0f);
		ImGui::Combo("Logic (vs. next)", &a_c.logicIndex, logicOptions, 2);
		ImGui::PopItemWidth();
	}

	void RenderFunctionReference()
	{
		if (!ImGui::TreeNode("Which Function should I use? (click to expand)")) {
			return;
		}

		ImGui::TextWrapped(
			"The Function field's autocomplete only suggests from this "
			"list. Typing an exact name outside it still works, it just "
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
}
