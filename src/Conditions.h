#pragma once

namespace RPP
{
	// Which "object" slot a condition is evaluated against - i.e. whose
	// state HasPerk/GetActorValue/etc. checks. Names match the real
	// RE::CONDITIONITEMOBJECT enum (verified directly against
	// TESCondition.h) minus their "k" prefix, except kSelf which is
	// exposed as "Subject" to match Creation Kit terminology (what every
	// vanilla and community COBJ condition calls it).
	namespace RunOn
	{
		constexpr int kSubject = 0;       // CONDITIONITEMOBJECT::kSelf
		constexpr int kTarget = 1;
		constexpr int kReference = 2;
		constexpr int kCombatTarget = 3;
		constexpr int kLinkedRef = 4;
		constexpr int kQuestAlias = 5;
		constexpr int kPackData = 6;
		constexpr int kEventData = 7;
		constexpr int kCommandTarget = 8;
	}

	// A single, fully generic CTDA condition - deliberately mirrors the
	// game's own condition format almost 1:1, rather than modeling only
	// HasPerk. See RecommendedConditionFunctions.h for the confirmed
	// function list and the README for the full picture:
	//   - function name<->ID resolution is 100% accurate (mechanically
	//     extracted from CommonLibSSE's own FunctionID enum).
	//   - which functions are genuinely meaningful as read-only CTDA
	//     conditions (as opposed to action/setter functions that happen
	//     to share the same underlying ID space) is only confirmed for
	//     the functions in RecommendedConditionFunctions.h.
	//   - each function's parameter TYPES/encoding (does param1 want a
	//     form reference, a raw integer, nothing at all?) is likewise
	//     only confirmed for that same curated list. Cross-reference
	//     https://ck.uesp.net for anything outside it before relying on it.
	struct ConditionSpec
	{
		// Function name (resolved against ConditionFunctionTable.h) OR a
		// raw numeric ID as a string (e.g. "448") for anything not in -
		// or not worth looking up in - that table.
		std::string function;

		// Each is EITHER an identifier (EditorID or FormID~Plugin,
		// resolved the same way as materials/perks/recipes elsewhere in
		// this plugin) OR a plain number, OR empty if this function
		// doesn't use that parameter slot. Numbers are stored as a raw
		// 32-bit integer in the parameter slot. GetActorValue's param1 is
		// a special case with its own resolution path - see
		// ActorValueTable.h and ResolveActorValueParam in Conditions.cpp.
		std::string param1;
		std::string param2;

		// Comparison operator: "==", "!=", ">", ">=", "<", "<=".
		std::string op = "==";

		// The value to compare the function's result against: a literal
		// number (e.g. "1", "30.0"), "true"/"false" (either case, resolve
		// to 1/0), or an identifier for a Global variable to compare
		// against dynamically instead of a fixed literal.
		std::string value = "1";

		// See RunOn:: above. Defaults to Subject - the in-game editor
		// always uses this default and doesn't expose a control to change
		// it, since crafting is always done by the player and every other
		// option has no sensible meaning here. Still hand-editable in the
		// JSON for an unusual case.
		int runOn = RunOn::kSubject;

		// "AND" or "OR" against whichever condition immediately follows
		// this one in the same recipe's chain. Mirrors
		// CONDITION_ITEM_DATA::Flags::isOR.
		std::string logic = "AND";

		std::string comment;  // purely for humans, unused at runtime
	};

	// Adds a_spec to a_conditions, unless an item with the same
	// function+param1+param2 already exists there (operator/value/runOn/
	// logic aren't compared for dedup purposes; this mirrors the original
	// AlreadyRequiresPerk's "same function pointing at the same form"
	// dedup, generalized). Resolves function/params exactly once either
	// way. A separate check-then-add pair used to resolve them twice for
	// every successful add, and that resolution (an EditorID/FormID
	// lookup per param) runs on every recipe a mapped material appears
	// in, so doing it once instead of twice matters at scale.
	//
	// a_outAlreadyPresent reports which case happened: true if the spec
	// was already there (a no-op), false if it was newly added or if
	// resolution failed. Returns false only when resolution fails
	// outright, logged by the caller using the returned reason, never
	// fatal, matching every other resolution failure in this plugin.
	//
	// Prepends new items onto a_conditions' list. A caller adding more
	// than one spec to the same recipe in a single pass must iterate
	// that spec list in reverse, so the final list ends up in the same
	// order the specs were authored in; see RecipePatcher.cpp.
	bool AddConditionIfMissing(RE::TESCondition& a_conditions, const ConditionSpec& a_spec, bool& a_outAlreadyPresent, std::string& a_failureReason);
}
