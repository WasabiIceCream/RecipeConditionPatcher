#pragma once

#include "ClassifierPredicateEditor.h"
#include "UIConditionFields.h"

namespace RPP::ClassifierEditor
{
	// One rule: a match predicate (see ClassifierPredicateEditor.h) plus
	// two independently-editable ways to specify what it adds: `setGlobal`
	// (an add/remove list of Global names, the common case: each becomes
	// GetGlobalValue(name) == 1) and `advancedConditions` (the rare case
	// that needs something GetGlobalValue-with-a-plain-1-comparison can't
	// express, e.g. an OR-chained gate or a Global-vs-Global comparison.
	// Reuses UI::EditableCondition/RenderConditionFields verbatim, same
	// widget the Mappings tab already uses). Both can be non-empty at
	// once; on save, `advancedConditions` are written first, `setGlobal`
	// appended after, matching Classifiers.cpp's own parser order.
	struct EditableClassifierRule
	{
		EditableMatch match;
		std::vector<std::array<char, 128>> setGlobals;
		std::vector<UI::EditableCondition> advancedConditions;
		std::array<char, 256> comment{};
	};

	// A first-match-wins chain of rules, optionally restricted to
	// specific crafting benches (`benchKeywords`, empty = any bench) and
	// with an optional shared `when` predicate ANDed onto every rule's
	// own match. Mirrors ClassifierGroup in Classifiers.h, but editable
	// (char buffers, not resolved runtime state) and independent of it
	// (see the design note in the implementation plan for why the editor
	// doesn't reuse the runtime structs directly).
	struct EditableClassifierGroup
	{
		std::array<char, 256> comment{};
		std::vector<std::array<char, 64>> benchKeywords;
		EditableMatch when;
		std::vector<EditableClassifierRule> rules;
	};

	// Parses just the "classifiers" array out of an already-loaded config
	// (the main config or one external *_RCP.json, single-file only,
	// same convention LoadTriggerEntries(config)/LoadRecipeOverrides(config)
	// already use for the Mappings tab, so the editor never merges one
	// file's classifiers into another's).
	std::vector<EditableClassifierGroup> ParseClassifierGroups(const nlohmann::ordered_json& a_config);

	// Builds the "classifiers" JSON array from the given groups. Read-
	// modify-write against the target file (preserving every other
	// top-level key) is the CALLER's job, via ConfigFile::ReadFile/
	// WriteFile, same convention SaveUserTriggerEntries/
	// SaveRecipeOverrides already use.
	nlohmann::ordered_json SerializeClassifierGroups(const std::vector<EditableClassifierGroup>& a_groups);
}
