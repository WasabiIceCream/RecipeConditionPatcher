#pragma once

#include "ClassifierPredicateEditor.h"
#include "UIConditionFields.h"

namespace RPP::ClassifierEditor
{
	// One rule: a match predicate (see ClassifierPredicateEditor.h) plus
	// what it adds, as one uniform list of conditions (reuses
	// UI::EditableCondition/RenderConditionFields verbatim, same widget
	// the Mappings tab already uses). `setGlobal` is still read on load
	// (each name becomes a GetGlobalValue(name) == 1 condition, appended
	// after anything already in `conditions`, matching Classifiers.cpp's
	// own parser order) so existing hand-authored files using the
	// shorthand still load correctly, but it's never written back out:
	// the editor always saves the expanded `conditions` form. The
	// shorthand still exists for hand-editing (see Classifiers.h); the
	// in-game editor just doesn't special-case it, one condition list is
	// simpler than a widget that offers two ways to say the same thing.
	struct EditableClassifierRule
	{
		EditableMatch match;
		std::vector<UI::EditableCondition> conditions;
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
