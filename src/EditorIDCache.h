#pragma once

namespace RPP
{
	// One selectable entry in the editor's autocomplete. `detail` is the
	// supporting context shown alongside the name (record type and source
	// plugin for forms), so a list of similar EditorIDs can actually be
	// told apart without leaving the menu.
	struct Candidate
	{
		std::string editorID;
		std::string detail;
	};

	// Cached, sorted EditorID lists used to drive the in-game editor's
	// autocomplete, plus a reverse lookup for log output. All of these are
	// built lazily on first use from a single pass over the game's own
	// EditorID table (TESForm::GetAllFormsByEditorID); see the note at the
	// top of the .cpp for why they are NOT built from
	// TESForm::GetFormEditorID(), which returns an empty string for every
	// record type this plugin deals with.

	// Every EditorID currently loaded, regardless of type. Used for the
	// generic identifier fields (Material, Param 1/2), which can legitimately
	// point at almost any kind of form depending on the condition function.
	const std::vector<Candidate>& AllEditorIDs();

	// Only BGSConstructibleObject (recipe) EditorIDs, used for the Recipe
	// field in recipeOverrides, which is always a recipe specifically, so
	// there's no reason to suggest from the full list when a far smaller,
	// more relevant one is available.
	const std::vector<Candidate>& AllRecipeEditorIDs();

	// Only the record types that make sense as a GetItemCount target:
	// MISC/ARMO/WEAP/INGR/ALCH/BOOK/KEYM. GetItemCount will technically
	// accept almost any form as param1, but most of them (quests, NPCs,
	// cells, ...) don't mean anything as "how many of this does the player
	// have." Narrowing suggestions to plausible inventory item types keeps
	// the list actually useful.
	const std::vector<Candidate>& AllCraftableItemEditorIDs();

	// Every EditorID of one specific record type: PERK, SPEL, QUST, FACT,
	// GLOB and RACE are cached, since those are the types the curated
	// condition functions ask for (see RecommendedConditionFunctions.h's
	// ParamKind). Any other type returns an empty list; callers fall back
	// to AllEditorIDs() so a field is never left with no suggestions.
	const std::vector<Candidate>& CandidatesForFormType(RE::FormType a_type);

	// The EditorID for a given recipe, or an empty view if it has none.
	// Only recipes are covered (see the .cpp). This exists so log lines
	// can identify a recipe by name instead of only its FormID.
	std::string_view LookupRecipeEditorID(const RE::TESForm* a_recipe);

	// The EditorID for ANY currently loaded form, or an empty view if it
	// has none (or isn't in the game's EditorID table at all, most
	// runtime-only/dynamic forms). Unlike LookupRecipeEditorID, not
	// restricted to one record type, used by the classifier predicates
	// (see Classifiers.cpp) to substring-match a produced item's own
	// EditorID, which can be an ARMO/WEAP/AMMO/MISC/etc. Backed by the
	// same single cache-building pass as everything else in this file.
	std::string_view LookupEditorID(const RE::TESForm* a_form);
}
