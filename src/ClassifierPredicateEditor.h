#pragma once

// Deliberately self-contained (explicit includes below, no reliance on
// PCH.h) and free of any RE::/SKSE:: dependency: this file only touches
// JSON and plain strings. That's what lets it be built and unit-tested as
// plain, native C++ outside the Windows cross-compile toolchain (see the
// test harness used during development), and it's also just the right
// separation of concerns: the ImGui-facing editor (UIClassifiersEditor.cpp)
// converts between this and its own char-buffer-based row widgets, but has
// no business owning the actual boolean-algebra logic.
#include <nlohmann/json.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace RPP::ClassifierEditor
{
	// The predicate kinds a row can edit. Mirrors ClassifierPredicate::Kind
	// in Classifiers.h minus the composition kinds (all/any/not); those
	// are what the block/row structure below exists to represent visually
	// instead of exposing as a literal tree.
	enum class RowKind
	{
		kSignature,
		kKeyword,
		kArmorType,
		kEdidContains,
		kFullContains,
		kRecipeEdidContains,
		kRecipeHasCondition,
	};

	// A single value slot, fixed-size so ImGui::InputText (which needs a
	// stable char* buffer across frames while being edited, and uses a
	// widget's buffer ADDRESS as part of its own per-field autocomplete
	// state; see UIAutocomplete.h's SuggestionState) can bind to it
	// directly, with no separate UI-only mirror type needed. Blank
	// (buf[0] == '\0') means "not yet filled in," not a real value,
	// skipped on serialize, same "drop incomplete entries rather than
	// save garbage" rule as the rest of the in-game editor.
	using RowValue = std::array<char, 128>;

	// One AND-term within a block: "<kind> is (not) one of <values>" (no
	// non-blank values means this row is incomplete/unset, not a
	// meaningful restriction; see IsUsable()).
	struct MatchRow
	{
		RowKind kind = RowKind::kSignature;
		bool negate = false;
		std::vector<RowValue> values;

		[[nodiscard]] bool IsUsable() const
		{
			for (const auto& v : values) {
				if (v[0] != '\0') {
					return true;
				}
			}
			return false;
		}
	};

	// One OR-alternative: every row ANDed together.
	struct MatchBlock
	{
		std::vector<MatchRow> rows;
	};

	// An editable match/when predicate: EITHER a bounded "OR of AND-of-rows"
	// (an empty `blocks` list means always-true, the canonical
	// representation for "no restriction," never a block with zero rows),
	// OR a read-only fallback holding the original JSON verbatim, for a
	// predicate too complex for the block model (arbitrary nesting beyond
	// what a fixed two-level structure can express, or one that would
	// blow past the block-count cap). A fallback match is never rebuilt
	// from `blocks` on save. See SerializeMatch.
	struct EditableMatch
	{
		bool isFallback = false;
		nlohmann::ordered_json fallbackRaw;
		std::vector<MatchBlock> blocks;
	};

	// Parses a "match"/"when" JSON node (or a missing/null one, which
	// means always-true, same as ClassifierPredicate's default) into the
	// editable form above. Same-predicate-kind `any`/`not(any(...))`
	// collapses into a single multi-value row rather than triggering
	// block-splitting (that's just the leaf kinds' own built-in OR
	// semantics, not a real disjunction), so only genuine cross-kind
	// disjunction inside an `all` causes real block distribution. See
	// Classifiers.cpp's ParsePredicate for the JSON shape this mirrors.
	// a_maxBlocks caps how far distribution is allowed to grow before
	// giving up and falling back (default matches what real-world testing
	// against examples/CCOR_RCP.json showed is never actually approached
	// by hand-authored predicates).
	EditableMatch ParseMatch(const nlohmann::ordered_json& a_json, std::size_t a_maxBlocks = 8);

	// Inverse of ParseMatch. A non-fallback match serializes to the same
	// predicate JSON shape Classifiers.cpp's own parser reads (an empty
	// `blocks` list serializes to JSON `null`, meaning "omit the match/when
	// key entirely"; see the caller in UIClassifiersEditor.cpp). A
	// fallback match returns `fallbackRaw` completely unmodified.
	nlohmann::ordered_json SerializeMatch(const EditableMatch& a_match);

	// The exact JSON key each row kind reads/writes (e.g. "edidContains"),
	// and a short display label for the row-kind picker widget.
	std::string_view RowKindJsonKey(RowKind a_kind);
	std::string_view RowKindLabel(RowKind a_kind);
}
