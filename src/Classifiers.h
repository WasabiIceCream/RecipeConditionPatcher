#pragma once

#include "ConfigFile.h"
#include "Conditions.h"

namespace RPP
{
	// A boolean predicate evaluated against a recipe's PRODUCED item
	// (BGSConstructibleObject::createdItem) and, for the two "recipe*"
	// kinds, the recipe itself. This is the trigger side of a "classifier"
	// rule (see ClassifierRule below), unlike TriggerEntry (which matches
	// a recipe's REQUIRED materials against an exact identifier) or
	// RecipeOverride (which matches one exact recipe), this exists to
	// classify recipes in bulk by what they actually make: record type,
	// keywords, or substrings in its EditorID/FULL name. This is exactly
	// what's needed to express a keyword/name-based compatibility patch
	// (e.g. for Complete Crafting Overhaul Remastered's own xEdit
	// compatibility script) as data instead of code.
	//
	// Every list-valued kind matches on ANY entry (OR). For AND, wrap
	// several single-entry predicates in "all". This keeps the vocabulary
	// small: composition (all/any/not) does the boolean algebra, each leaf
	// kind does exactly one kind of check.
	struct ClassifierPredicate
	{
		enum class Kind
		{
			kAlways,             // no "match" was given, trivially true
			kSignature,          // produced item's record type (e.g. "ARMO") is one of `values`
			kKeyword,            // produced item has any keyword EditorID in `values`
			kArmorType,          // produced item is an ARMO whose BOD2 Armor Type is one of `values` ("Light", "Heavy", "Clothing")
			kEdidContains,       // produced item's own EditorID contains any of `values` (case-insensitive)
			kFullContains,       // produced item's FULL name contains any of `values` (case-insensitive)
			kRecipeEdidContains, // the RECIPE's own EditorID contains any of `values` (case-insensitive)
			kRecipeHasCondition, // the recipe's PRE-EXISTING conditions already reference any identifier in `values`
			kAll,                // AND of `children`
			kAny,                // OR of `children`
			kNot,                // negation of `children[0]`
		};

		Kind kind = Kind::kAlways;
		std::vector<std::string> values;                // leaf kinds
		std::vector<ClassifierPredicate> children;       // kAll/kAny (any size), kNot (exactly one)
	};

	// One row in a classifier group's `rules` array: "if `match` is true
	// for this recipe, add `conditions` (and stop evaluating this group
	// for this recipe, first match wins, like an if/elseif chain)."
	struct ClassifierRule
	{
		ClassifierPredicate match;  // defaults to kAlways; a ruleless-match acts as the chain's default/fallback
		std::vector<ConditionSpec> conditions;
		std::string comment;  // purely for humans reading the json, unused at runtime
	};

	// An ordered, first-match-wins chain of rules, optionally restricted
	// to recipes made at a specific crafting bench (BGSConstructibleObject
	// ::benchKeyword, e.g. "CraftingSmithingForge"); leave benchKeywords
	// empty to apply regardless of bench. A recipe can be matched by more
	// than one classifier group (each group is independent, e.g. a
	// "weapon type" chain and a "material" chain both applying to the
	// same forge recipe); it's only exclusive WITHIN one group's own
	// rules.
	struct ClassifierGroup
	{
		std::vector<std::string> benchKeywords;  // empty = any bench
		// ANDed onto every rule's own `match` below, factoring out a guard
		// shared by every rule in the group (e.g. "the produced item is
		// ARMO and not jewelry") instead of repeating it in each one.
		// Defaults to kAlways (no "when" key = no extra restriction).
		// Checked before any rule, so a recipe that fails it skips the
		// whole group in one predicate evaluation rather than one per rule.
		ClassifierPredicate baseMatch;
		std::vector<ClassifierRule> rules;
		std::string comment;  // purely for humans reading the json, unused at runtime
	};

	// Loads the "classifiers" array from the shared config file, plus any
	// Data/SKSE/Plugins/*_RCP.json files (see ConfigFile::FindExternalConfigPaths).
	// Returns an empty list if none of them have any. This feature is
	// entirely opt-in. This is the RUNTIME loader, used by the patch pass
	// (RecipePatcher.cpp). The in-game Classifiers tab (UIClassifiersEditor.cpp)
	// does NOT go through this or ClassifierGroup/ClassifierRule/
	// ClassifierPredicate: it needs editable char-buffer fields (not
	// resolved runtime state), a way to keep an over-complex predicate's
	// original JSON around read-only instead of forcing it through the
	// block/row editor, and a reverse JSON serializer this type doesn't
	// have, so it has its own parallel editable model in
	// ClassifierGroupEditor.h/ClassifierPredicateEditor.h. It still reads
	// the "setGlobal" shorthand below on load (expanding it into ordinary
	// conditions), it just never writes it back out.
	//
	// Two shorthands beyond the plain schema described above, both purely
	// authoring convenience (identical to spelling them out longhand):
	//   - a group's "when" is a predicate ANDed onto every rule's "match".
	//   - a rule's "setGlobal": "SomeGlobal" (or an array of them) expands
	//     to one GetGlobalValue(SomeGlobal) == 1 condition per name,
	//     appended after anything already in "conditions".
	std::vector<ClassifierGroup> LoadClassifierGroups(
		const nlohmann::ordered_json& a_mainConfig,
		const std::vector<ConfigFile::ExternalConfig>& a_externalConfigs = {});

	// Does a_group apply to a_recipe at all (its benchKeywords, if any,
	// must include a_recipeBenchKeywordEditorID), and if so, which rule
	// (first match wins) fires for a_producedItem? Returns the winning
	// rule's `conditions`, or nullptr if the group doesn't apply to this
	// bench or no rule matched. a_resolveCache memoizes identifier ->
	// TESForm* lookups for kRecipeHasCondition across the whole patch
	// pass (see RecipePatcher.cpp), the same handful of identifiers
	// (e.g. "CCO_MODSupported") get checked against thousands of recipes.
	const std::vector<ConditionSpec>* SelectClassifierConditions(
		const ClassifierGroup& a_group,
		std::string_view a_recipeBenchKeywordEditorID,
		RE::TESForm* a_producedItem,
		RE::BGSConstructibleObject* a_recipe,
		std::unordered_map<std::string, RE::TESForm*>& a_resolveCache);
}
