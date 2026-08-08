#include "ClassifierPredicateEditor.h"

#include <algorithm>
#include <array>

namespace RPP::ClassifierEditor
{
	namespace
	{
		// An internal, JSON-shaped boolean tree, mirroring
		// ClassifierPredicate::Kind in Classifiers.h, but this file
		// doesn't reuse that type: Classifiers.h's parser is written for
		// the runtime patch pass (see Classifiers.cpp's ParsePredicate)
		// and has no reverse serializer, and this editor needs to keep
		// working even on a predicate shape it can't fully normalize
		// (the fallback path), which the runtime type was never designed
		// to preserve.
		struct RawNode
		{
			enum class Kind
			{
				kAlways,
				kLeaf,
				kAll,
				kAny,
				kNot,
			};

			Kind kind = Kind::kAlways;
			RowKind leafKind{};             // valid when kind == kLeaf
			std::vector<std::string> leafValues;  // valid when kind == kLeaf
			std::vector<RawNode> children;  // valid for kAll/kAny (any size), kNot (exactly one)
		};

		struct RowKindEntry
		{
			RowKind kind;
			const char* jsonKey;
			const char* label;
		};

		// Single source of truth for every row kind's JSON key and
		// display label. Everything else (parsing, serializing, the
		// row-kind picker widget) drives off this table instead of
		// repeating the mapping.
		constexpr std::array<RowKindEntry, 7> kRowKinds{ {
			{ RowKind::kSignature, "signature", "Item type (signature)" },
			{ RowKind::kKeyword, "keyword", "Keyword" },
			{ RowKind::kArmorType, "armorType", "Armor type" },
			{ RowKind::kEdidContains, "edidContains", "Item EditorID contains" },
			{ RowKind::kFullContains, "fullContains", "Item name (FULL) contains" },
			{ RowKind::kRecipeEdidContains, "recipeEdidContains", "Recipe's own EditorID contains" },
			{ RowKind::kRecipeHasCondition, "recipeHasCondition", "Recipe already has a condition referencing" },
		} };

		std::vector<std::string> ToStringList(const nlohmann::ordered_json& a_value)
		{
			std::vector<std::string> result;
			if (a_value.is_array()) {
				for (auto& v : a_value) {
					if (v.is_string()) {
						result.push_back(v.get<std::string>());
					}
				}
			} else if (a_value.is_string()) {
				result.push_back(a_value.get<std::string>());
			}
			return result;
		}

		RawNode ParseRaw(const nlohmann::ordered_json& a_json)
		{
			RawNode n{};

			if (!a_json.is_object()) {
				return n;  // kAlways: missing/null/malformed "match" or "when"
			}

			for (const auto& entry : kRowKinds) {
				if (a_json.contains(entry.jsonKey)) {
					n.kind = RawNode::Kind::kLeaf;
					n.leafKind = entry.kind;
					n.leafValues = ToStringList(a_json[entry.jsonKey]);
					return n;
				}
			}

			if (a_json.contains("all") && a_json["all"].is_array()) {
				n.kind = RawNode::Kind::kAll;
				for (auto& child : a_json["all"]) {
					n.children.push_back(ParseRaw(child));
				}
			} else if (a_json.contains("any") && a_json["any"].is_array()) {
				n.kind = RawNode::Kind::kAny;
				for (auto& child : a_json["any"]) {
					n.children.push_back(ParseRaw(child));
				}
			} else if (a_json.contains("not")) {
				n.kind = RawNode::Kind::kNot;
				n.children.push_back(ParseRaw(a_json["not"]));
			}
			// Anything else (an object with none of the recognized keys)
			// stays kAlways. ToBlocks/ParseMatch will only reach this
			// case for a genuinely malformed hand-written predicate, and
			// treating it as always-true (rather than failing outright)
			// matches Classifiers.cpp's own parser, which logs a warning
			// and does the same.

			return n;
		}

		bool AllSameLeafKind(const std::vector<RawNode>& a_children, RowKind& a_outKind)
		{
			if (a_children.empty() || a_children.front().kind != RawNode::Kind::kLeaf) {
				return false;
			}
			a_outKind = a_children.front().leafKind;
			for (const auto& c : a_children) {
				if (c.kind != RawNode::Kind::kLeaf || c.leafKind != a_outKind) {
					return false;
				}
			}
			return true;
		}

		std::vector<std::string> UnionValues(const std::vector<RawNode>& a_leaves)
		{
			std::vector<std::string> result;
			for (const auto& leaf : a_leaves) {
				result.insert(result.end(), leaf.leafValues.begin(), leaf.leafValues.end());
			}
			return result;
		}

		// Converts the algorithm's own working representation (plain
		// strings, unbounded) into the fixed-size buffers MatchRow
		// exposes publicly. See RowValue's doc comment for why. Only
		// needed at the point a MatchRow actually gets constructed, not
		// throughout the boolean-algebra logic above.
		std::vector<RowValue> ToRowValues(const std::vector<std::string>& a_values)
		{
			std::vector<RowValue> result;
			result.reserve(a_values.size());
			for (const auto& v : a_values) {
				RowValue buf{};
				const std::size_t n = std::min(v.size(), buf.size() - 1);
				std::copy_n(v.data(), n, buf.data());
				buf[n] = '\0';
				result.push_back(buf);
			}
			return result;
		}

		// Recognizes the shapes that collapse into a SINGLE row: a bare
		// leaf, a negated leaf, an `any` of same-kind leaves (their
		// built-in OR-list semantics, not a real disjunction needing
		// block-splitting), or a negated same-kind `any`. Returns
		// nullopt for anything else. The caller (ToBlocks) then falls
		// through to genuine block-level handling.
		std::optional<MatchRow> TryAsRow(const RawNode& a_node)
		{
			if (a_node.kind == RawNode::Kind::kLeaf) {
				return MatchRow{ a_node.leafKind, false, ToRowValues(a_node.leafValues) };
			}

			if (a_node.kind == RawNode::Kind::kNot) {
				const auto& inner = a_node.children.front();
				if (inner.kind == RawNode::Kind::kLeaf) {
					return MatchRow{ inner.leafKind, true, ToRowValues(inner.leafValues) };
				}
				if (inner.kind == RawNode::Kind::kAny) {
					RowKind kind{};
					if (AllSameLeafKind(inner.children, kind)) {
						return MatchRow{ kind, true, ToRowValues(UnionValues(inner.children)) };
					}
				}
				return std::nullopt;
			}

			if (a_node.kind == RawNode::Kind::kAny) {
				RowKind kind{};
				if (AllSameLeafKind(a_node.children, kind)) {
					return MatchRow{ kind, false, ToRowValues(UnionValues(a_node.children)) };
				}
			}

			return std::nullopt;
		}

		// Core algorithm: converts a_node into "OR of AND-of-rows",
		// distributing AND over OR (cartesian product) only where a row
		// can't already express the shape. kAlways is represented
		// internally as one block with zero rows (the multiplicative
		// identity for the cartesian product below). ParseMatch
		// canonicalizes that back to an empty block LIST for the public
		// EditableMatch. Returns nullopt if a_maxBlocks would be
		// exceeded, or for the one genuinely unrepresentable case
		// (NOT of an always-true predicate, i.e. "always false"; there's
		// no sentinel for that in this model, and no real config has a
		// reason to write one).
		std::optional<std::vector<MatchBlock>> ToBlocks(const RawNode& a_node, std::size_t a_maxBlocks)
		{
			if (a_node.kind == RawNode::Kind::kAlways) {
				return std::vector<MatchBlock>{ MatchBlock{} };
			}

			if (auto row = TryAsRow(a_node)) {
				MatchBlock b;
				b.rows.push_back(std::move(*row));
				return std::vector<MatchBlock>{ std::move(b) };
			}

			if (a_node.kind == RawNode::Kind::kAll) {
				std::vector<MatchBlock> result{ MatchBlock{} };  // identity: 1 block, 0 rows
				for (const auto& child : a_node.children) {
					auto childBlocks = ToBlocks(child, a_maxBlocks);
					if (!childBlocks) {
						return std::nullopt;
					}

					std::vector<MatchBlock> product;
					for (const auto& existing : result) {
						for (const auto& add : *childBlocks) {
							if (product.size() >= a_maxBlocks) {
								return std::nullopt;
							}
							MatchBlock merged = existing;
							merged.rows.insert(merged.rows.end(), add.rows.begin(), add.rows.end());
							product.push_back(std::move(merged));
						}
					}
					result = std::move(product);
				}
				return result;
			}

			if (a_node.kind == RawNode::Kind::kAny) {
				// Group IMMEDIATE leaf children by kind before falling
				// back to one-block-per-child: any(kwA, kwB, edidC,
				// fullD, fullE) is 3 alternatives (kwA-or-kwB, edidC,
				// fullD-or-fullE), not 5, since each kind's own leaves merge
				// into a single row via the same OR-list semantics
				// TryAsRow already exploits for a fully homogeneous any.
				// Without this, a real-world mixed-kind alternative list
				// (e.g. CCOR's "keyword X, OR any of these EDID
				// substrings, OR any of these FULL substrings" style
				// rules) explodes into one block per leaf instead of one
				// block per distinct kind, tripping the block cap for
				// entirely ordinary hand-authored predicates. Only
				// non-leaf children (nested all/any/not) still go
				// through full recursive ToBlocks, since there's no
				// leaf-level kind to group them by.
				std::vector<RowKind> seenKinds;
				std::vector<std::vector<std::string>> seenValues;  // parallel to seenKinds
				std::vector<RawNode> nonLeafChildren;

				for (const auto& child : a_node.children) {
					if (child.kind != RawNode::Kind::kLeaf) {
						nonLeafChildren.push_back(child);
						continue;
					}
					const auto it = std::find(seenKinds.begin(), seenKinds.end(), child.leafKind);
					if (it == seenKinds.end()) {
						seenKinds.push_back(child.leafKind);
						seenValues.push_back(child.leafValues);
					} else {
						auto& values = seenValues[static_cast<std::size_t>(std::distance(seenKinds.begin(), it))];
						values.insert(values.end(), child.leafValues.begin(), child.leafValues.end());
					}
				}

				if (seenKinds.size() + nonLeafChildren.size() > a_maxBlocks) {
					return std::nullopt;
				}

				std::vector<MatchBlock> result;
				for (std::size_t i = 0; i < seenKinds.size(); ++i) {
					MatchBlock b;
					b.rows.push_back(MatchRow{ seenKinds[i], false, ToRowValues(seenValues[i]) });
					result.push_back(std::move(b));
				}
				for (const auto& child : nonLeafChildren) {
					auto childBlocks = ToBlocks(child, a_maxBlocks);
					if (!childBlocks) {
						return std::nullopt;
					}
					for (auto& b : *childBlocks) {
						if (result.size() >= a_maxBlocks) {
							return std::nullopt;
						}
						result.push_back(std::move(b));
					}
				}
				return result;
			}

			if (a_node.kind == RawNode::Kind::kNot) {
				// TryAsRow already tried and failed above. This NOT
				// wraps something more complex than a leaf or a
				// same-kind any. De Morgan's expansion, then retry via
				// the (now differently-shaped) node.
				const auto& inner = a_node.children.front();
				RawNode expanded{};
				if (inner.kind == RawNode::Kind::kAll) {
					expanded.kind = RawNode::Kind::kAny;
					for (const auto& c : inner.children) {
						expanded.children.push_back(RawNode{ RawNode::Kind::kNot, {}, {}, { c } });
					}
				} else if (inner.kind == RawNode::Kind::kAny) {
					expanded.kind = RawNode::Kind::kAll;
					for (const auto& c : inner.children) {
						expanded.children.push_back(RawNode{ RawNode::Kind::kNot, {}, {}, { c } });
					}
				} else if (inner.kind == RawNode::Kind::kNot) {
					expanded = inner.children.front();  // double negation
				} else {
					// NOT(always-true) = always-false: not representable.
					return std::nullopt;
				}
				return ToBlocks(expanded, a_maxBlocks);
			}

			return std::nullopt;
		}

	}

	EditableMatch ParseMatch(const nlohmann::ordered_json& a_json, std::size_t a_maxBlocks)
	{
		EditableMatch m{};

		const RawNode raw = ParseRaw(a_json);
		auto blocks = ToBlocks(raw, a_maxBlocks);
		if (!blocks) {
			m.isFallback = true;
			m.fallbackRaw = a_json;
			return m;
		}

		// Canonicalize "always true" (exactly one block with zero rows)
		// to an empty block list: the one and only representation for
		// "no restriction" in the public model, so the caller never has
		// to treat "0 blocks" and "1 block, 0 rows" as different things.
		if (blocks->size() == 1 && blocks->front().rows.empty()) {
			m.blocks = {};
		} else {
			m.blocks = std::move(*blocks);
		}
		return m;
	}

	namespace
	{
		nlohmann::ordered_json RowToJson(const MatchRow& a_row)
		{
			// Blank slots (buf[0] == '\0') are unfilled-in-progress rows,
			// not real values, skipped here, same "drop incomplete
			// entries rather than save garbage" rule as everywhere else
			// in the editor.
			std::vector<std::string> values;
			for (const auto& v : a_row.values) {
				if (v[0] != '\0') {
					values.emplace_back(v.data());
				}
			}

			nlohmann::ordered_json leaf;
			// A single value serializes as a bare string rather than a
			// 1-element array, matching how the CCOR example (and
			// Classifiers.cpp's parser, which accepts either shape) is
			// written by hand, and keeps a simple row's JSON simple.
			if (values.size() == 1) {
				leaf[std::string{ RowKindJsonKey(a_row.kind) }] = values.front();
			} else {
				nlohmann::ordered_json arr = nlohmann::ordered_json::array();
				for (const auto& v : values) {
					arr.push_back(v);
				}
				leaf[std::string{ RowKindJsonKey(a_row.kind) }] = std::move(arr);
			}

			if (!a_row.negate) {
				return leaf;
			}
			nlohmann::ordered_json notNode;
			notNode["not"] = std::move(leaf);
			return notNode;
		}

		nlohmann::ordered_json BlockToJson(const MatchBlock& a_block)
		{
			if (a_block.rows.size() == 1) {
				return RowToJson(a_block.rows.front());
			}
			nlohmann::ordered_json all = nlohmann::ordered_json::array();
			for (const auto& row : a_block.rows) {
				all.push_back(RowToJson(row));
			}
			nlohmann::ordered_json node;
			node["all"] = std::move(all);
			return node;
		}
	}

	nlohmann::ordered_json SerializeMatch(const EditableMatch& a_match)
	{
		if (a_match.isFallback) {
			return a_match.fallbackRaw;
		}

		if (a_match.blocks.empty()) {
			return nullptr;  // always-true; caller omits the match/when key entirely
		}

		if (a_match.blocks.size() == 1) {
			return BlockToJson(a_match.blocks.front());
		}

		nlohmann::ordered_json any = nlohmann::ordered_json::array();
		for (const auto& block : a_match.blocks) {
			any.push_back(BlockToJson(block));
		}
		nlohmann::ordered_json node;
		node["any"] = std::move(any);
		return node;
	}

	std::string_view RowKindJsonKey(RowKind a_kind)
	{
		for (const auto& entry : kRowKinds) {
			if (entry.kind == a_kind) {
				return entry.jsonKey;
			}
		}
		return {};
	}

	std::string_view RowKindLabel(RowKind a_kind)
	{
		for (const auto& entry : kRowKinds) {
			if (entry.kind == a_kind) {
				return entry.label;
			}
		}
		return {};
	}
}
