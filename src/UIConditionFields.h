#pragma once

#include "Conditions.h"

namespace RPP::UI
{
	// Shared by ToSpec(), OpIndexFromString(), and the Operator combo in
	// RenderConditionFields, previously duplicated 3 times.
	inline constexpr const char* kOperatorSymbols[] = { "==", "!=", ">", ">=", "<", "<=" };
	inline constexpr int kOperatorCount = 6;

	// Fixed-size text buffers for ImGui::InputText, which needs a stable
	// char* that persists across frames (unlike a fresh std::string each
	// frame).
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

	int OpIndexFromString(const std::string& a_op);

	EditableCondition FromSpec(const ConditionSpec& a_spec);

	// Is a_c's param1 either filled in, or genuinely not needed? Used to
	// decide whether a row is "complete" enough to save. Blank param1
	// isn't automatically incomplete: GetCurrentTime/GetLevel/
	// GetRandomPercent are curated, autocomplete-offered functions that
	// take no param1 at all (the Param 1 field for them is shown
	// disabled with a "not used by this function" hint. See
	// RenderConditionFields), so requiring it unconditionally would
	// silently discard a correctly-filled-in row on save.
	bool HasUsableParam1(const EditableCondition& a_c);

	// One-line summary of a condition for a collapsed header, in the same
	// shape the log uses (e.g. "HasPerk(SteelSmithing) == True"), so an
	// entry can be read at a glance without expanding it.
	std::string SummariseCondition(const EditableCondition& a_c);

	// Renders the function/param1/param2/operator/value/logic fields for
	// one condition, in a compact vertical block. Does NOT render a
	// "Run On" control. Crafting is always done by the player, so it
	// always needs to be Subject in practice; every other option has no
	// sensible meaning for a recipe condition. EditableCondition::runOn
	// stays defaulted to RunOn::kSubject and is never changed by this
	// editor. The underlying field/JSON key is still there for
	// hand-editing an unusual case, just not surfaced here.
	void RenderConditionFields(EditableCondition& a_c);

	// A reference list of the curated, crafting-relevant functions and
	// what their fields mean. The Function field's autocomplete only
	// suggests from this same list, so this is "what autocomplete will
	// offer you", spelled out with the usage hints autocomplete itself
	// can't show inline.
	void RenderFunctionReference();
}
