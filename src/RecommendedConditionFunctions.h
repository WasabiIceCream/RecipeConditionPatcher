#pragma once

namespace RPP::RecommendedConditions
{
	// A curated subset of Skyrim's ~750 condition functions that actually
	// make sense on a crafting recipe - as opposed to the full
	// ConditionFunctionTable.h list, which includes hundreds of functions
	// about combat, dialogue, AI packages, and other things with no sane
	// meaning on a COBJ record. This list is what drives the Function
	// field's autocomplete suggestions in the in-game editor; it does NOT
	// restrict what you can actually type - Conditions.cpp resolves against
	// the full table regardless, so any function not listed here still
	// works if you type its exact name, autocomplete or not.
	// What param1 expects, so the editor can suggest only sensible
	// candidates instead of every EditorID in the game. Suggestions only -
	// the resolver still accepts anything you hand-write in the JSON.
	enum class ParamKind
	{
		kNone,           // function takes no param1
		kAny,            // unknown - fall back to the full EditorID list
		kCraftableItem,  // MISC/ARMO/WEAP/INGR/ALCH/BOOK/KEYM
		kPerk,           // PERK
		kSpell,          // SPEL
		kQuest,          // QUST
		kFaction,        // FACT
		kGlobal,         // GLOB
		kRace,           // RACE
		kActorValue,     // Actor Value name or 0-163 (not a form)
		kSex,            // 0 = Male, 1 = Female (not a form)
	};

	struct Entry
	{
		const char* name;
		const char* usage;  // plain-language hint: what param1/param2/value mean

		// If true, this function's result is a plain boolean (0 or 1) -
		// the in-game editor shows a True/False dropdown for the Value
		// field instead of a raw number field for these. Functions whose
		// result is a genuine number (a count, a stage, a level, ...) or
		// whose meaning varies (GetGlobalValue can hold anything) leave
		// this false and keep the plain number field.
		bool isBooleanValue = false;

		// Record type param1 wants. param2 is always a plain number in
		// this curated set (only GetStageDone uses it), so it needs no
		// equivalent.
		ParamKind param1Kind = ParamKind::kAny;
	};

	inline constexpr Entry kTable[] = {
		{ "HasPerk", "param1 = perk. value = true or false.", true, ParamKind::kPerk },
		{ "GetItemCount", "param1 = a misc item, armor, weapon, ingredient, potion, book, or key (MISC/ARMO/WEAP/INGR/ALCH/BOOK/KEYM). "
			"value = quantity in the player's inventory to compare against.", false, ParamKind::kCraftableItem },
		{ "GetQuestRunning", "param1 = quest (EditorID or FormID). value = true or false.", true, ParamKind::kQuest },
		{ "GetStage", "param1 = quest (EditorID or FormID), value = stage number to compare against.", false, ParamKind::kQuest },
		{ "GetStageDone", "param1 = quest (EditorID or FormID), param2 = stage number. value = true or false.", true, ParamKind::kQuest },
		{ "GetPCInFaction", "param1 = faction (EditorID or FormID). value = true or false.", true, ParamKind::kFaction },
		{ "GetFactionRank", "param1 = faction (EditorID or FormID), value = rank number to compare against.", false, ParamKind::kFaction },
		{ "GetGlobalValue", "param1 = a Global variable (EditorID or FormID). Returns that Global's value (typically a float). "
			"value = number (or another Global) to compare it against.", false, ParamKind::kGlobal },
		{ "GetPCIsRace", "param1 = race (EditorID or FormID). value = true or false.", true, ParamKind::kRace },
		{ "GetPCIsSex", "param1 = 0 for Male, 1 for Female. value = true or false.", true, ParamKind::kSex },
		{ "GetRandomPercent", "No params. Returns a random whole number 0-99. For an N% chance, use "
			"operator '<' with value N (e.g. value=1 gives a 1% chance - only rolling 0 satisfies it).", false, ParamKind::kNone },
		{ "GetLevel", "No params. Returns the player character's level, compared against value.", false, ParamKind::kNone },
		{ "GetActorValue", "param1 = an Actor Value ID (0-163) or name (e.g. \"Smithing\" - autocomplete offers the full list). "
			"Returns that Actor Value as a float, compared against value.", false, ParamKind::kActorValue },
		{ "GetCurrentTime", "No params. Returns the current in-game time as a decimal (e.g. 4:30am = 4.5, "
			"7:45pm = 19.75), compared against value.", false, ParamKind::kNone },
		{ "HasSpell", "param1 = spell (EditorID or FormID). value = true or false.", true, ParamKind::kSpell },
	};

	inline constexpr std::size_t kTableSize = sizeof(kTable) / sizeof(kTable[0]);

	// Looks up a_function by exact name in kTable and reports whether it's
	// flagged boolean. Unknown/uncurated function names (anything not in
	// this list) return false, which keeps the safe default: a plain
	// number field, not a dropdown that would silently restrict input for
	// a function this table has no actual data about.
	// param1's expected record type for a_function, or kAny for anything
	// not in this curated table.
	constexpr ParamKind Param1KindFor(std::string_view a_function)
	{
		for (std::size_t i = 0; i < kTableSize; ++i) {
			if (a_function == kTable[i].name) {
				return kTable[i].param1Kind;
			}
		}
		return ParamKind::kAny;
	}

	constexpr bool IsBooleanFunction(std::string_view a_function)
	{
		for (std::size_t i = 0; i < kTableSize; ++i) {
			if (a_function == kTable[i].name) {
				return kTable[i].isBooleanValue;
			}
		}
		return false;
	}
}
