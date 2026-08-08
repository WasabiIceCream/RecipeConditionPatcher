#include "Classifiers.h"

#include "EditorIDCache.h"
#include "Identifiers.h"

namespace RPP
{
	namespace
	{
		bool ContainsCI(std::string_view a_haystack, std::string_view a_needle)
		{
			if (a_needle.empty()) {
				return false;
			}
			const auto it = std::search(a_haystack.begin(), a_haystack.end(), a_needle.begin(), a_needle.end(),
				[](unsigned char a_a, unsigned char a_b) { return std::tolower(a_a) == std::tolower(a_b); });
			return it != a_haystack.end();
		}

		bool ContainsAnyCI(std::string_view a_haystack, const std::vector<std::string>& a_needles)
		{
			for (const auto& needle : a_needles) {
				if (ContainsCI(a_haystack, needle)) {
					return true;
				}
			}
			return false;
		}

		// The record-type strings this plugin's classifiers support,
		// deliberately just the crafting-relevant subset (same set
		// EditorIDCache::AllCraftableItemEditorIDs uses, plus Ammo since
		// ammunition recipes are common), not all ~100 RE::FormType values.
		bool ResolveSignature(const std::string& a_code, RE::FormType& a_out)
		{
			static const std::unordered_map<std::string, RE::FormType> table{
				{ "ARMO", RE::FormType::Armor },
				{ "WEAP", RE::FormType::Weapon },
				{ "AMMO", RE::FormType::Ammo },
				{ "MISC", RE::FormType::Misc },
				{ "KEYM", RE::FormType::KeyMaster },
				{ "BOOK", RE::FormType::Book },
				{ "INGR", RE::FormType::Ingredient },
				{ "ALCH", RE::FormType::AlchemyItem },
			};
			const auto it = table.find(a_code);
			if (it == table.end()) {
				return false;
			}
			a_out = it->second;
			return true;
		}

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

		ClassifierPredicate ParsePredicate(const nlohmann::ordered_json& a_json, const std::string& a_sourcePath)
		{
			ClassifierPredicate p{};

			if (!a_json.is_object()) {
				return p;  // kAlways
			}

			if (a_json.contains("signature")) {
				p.kind = ClassifierPredicate::Kind::kSignature;
				p.values = ToStringList(a_json["signature"]);
			} else if (a_json.contains("keyword")) {
				p.kind = ClassifierPredicate::Kind::kKeyword;
				p.values = ToStringList(a_json["keyword"]);
			} else if (a_json.contains("armorType")) {
				p.kind = ClassifierPredicate::Kind::kArmorType;
				p.values = ToStringList(a_json["armorType"]);
			} else if (a_json.contains("edidContains")) {
				p.kind = ClassifierPredicate::Kind::kEdidContains;
				p.values = ToStringList(a_json["edidContains"]);
			} else if (a_json.contains("fullContains")) {
				p.kind = ClassifierPredicate::Kind::kFullContains;
				p.values = ToStringList(a_json["fullContains"]);
			} else if (a_json.contains("recipeEdidContains")) {
				p.kind = ClassifierPredicate::Kind::kRecipeEdidContains;
				p.values = ToStringList(a_json["recipeEdidContains"]);
			} else if (a_json.contains("recipeHasCondition")) {
				p.kind = ClassifierPredicate::Kind::kRecipeHasCondition;
				p.values = ToStringList(a_json["recipeHasCondition"]);
			} else if (a_json.contains("all") && a_json["all"].is_array()) {
				p.kind = ClassifierPredicate::Kind::kAll;
				for (auto& child : a_json["all"]) {
					p.children.push_back(ParsePredicate(child, a_sourcePath));
				}
			} else if (a_json.contains("any") && a_json["any"].is_array()) {
				p.kind = ClassifierPredicate::Kind::kAny;
				for (auto& child : a_json["any"]) {
					p.children.push_back(ParsePredicate(child, a_sourcePath));
				}
			} else if (a_json.contains("not")) {
				p.kind = ClassifierPredicate::Kind::kNot;
				p.children.push_back(ParsePredicate(a_json["not"], a_sourcePath));
			} else {
				SKSE::log::warn("classifiers: unrecognized match predicate in {} (no known key); treating as always-true", a_sourcePath);
			}

			return p;
		}

		std::vector<ClassifierGroup> ParseClassifiersArray(const nlohmann::ordered_json& a_config, const std::string& a_sourcePath)
		{
			std::vector<ClassifierGroup> result;

			if (!a_config.contains("classifiers") || !a_config["classifiers"].is_array()) {
				return result;
			}

			for (auto& groupJson : a_config["classifiers"]) {
				ClassifierGroup group{};
				group.comment = groupJson.value("comment", "");
				if (groupJson.contains("benchKeyword")) {
					group.benchKeywords = ToStringList(groupJson["benchKeyword"]);
				}
				if (groupJson.contains("when")) {
					group.baseMatch = ParsePredicate(groupJson["when"], a_sourcePath);
				}

				if (groupJson.contains("rules") && groupJson["rules"].is_array()) {
					for (auto& ruleJson : groupJson["rules"]) {
						ClassifierRule rule{};
						rule.comment = ruleJson.value("comment", "");
						if (ruleJson.contains("match")) {
							rule.match = ParsePredicate(ruleJson["match"], a_sourcePath);
						}

						if (ruleJson.contains("conditions") && ruleJson["conditions"].is_array()) {
							for (auto& c : ruleJson["conditions"]) {
								ConditionSpec spec{};
								spec.function = c.value("function", std::string{ "HasPerk" });
								spec.param1 = c.value("param1", "");
								spec.param2 = c.value("param2", "");
								spec.op = c.value("operator", std::string{ "==" });
								spec.value = c.value("value", std::string{ "1" });
								spec.runOn = c.value("runOn", 0);
								spec.logic = c.value("logic", std::string{ "AND" });
								// NOT gated on param1 being non-empty, unlike
								// mappings/recipeOverrides rows (which always
								// key off a specific identifier), a
								// classifier rule's conditions legitimately
								// include param-less functions like
								// GetCurrentTime (see the CCOR example's
								// daedric-at-night rule).
								rule.conditions.push_back(std::move(spec));
							}
						}

						// "setGlobal": "X" (or ["X", "Y"]) is shorthand for
						// one GetGlobalValue(X) == 1 condition per name,
						// covers the overwhelming majority of a typical
						// classifier's rules (see the CCOR example), which
						// just tag a recipe with a single global.
						if (ruleJson.contains("setGlobal")) {
							for (auto& g : ToStringList(ruleJson["setGlobal"])) {
								ConditionSpec spec{};
								spec.function = "GetGlobalValue";
								spec.param1 = g;
								rule.conditions.push_back(std::move(spec));
							}
						}

						if (rule.conditions.empty()) {
							SKSE::log::warn("classifiers: skipping a rule with no usable \"conditions\" in {}", a_sourcePath);
							continue;
						}

						group.rules.push_back(std::move(rule));
					}
				}

				if (group.rules.empty()) {
					SKSE::log::warn("classifiers: skipping a classifier group with no usable \"rules\" in {}", a_sourcePath);
					continue;
				}

				result.push_back(std::move(group));
			}

			return result;
		}

		RE::TESForm* ResolveCached(const std::string& a_id, std::unordered_map<std::string, RE::TESForm*>& a_cache)
		{
			if (const auto it = a_cache.find(a_id); it != a_cache.end()) {
				return it->second;
			}
			auto* form = ResolveIdentifier<RE::TESForm>(a_id);
			a_cache[a_id] = form;
			return form;
		}

		bool RecipeReferencesForm(const RE::TESCondition& a_conditions, const RE::TESForm* a_form)
		{
			const auto target = static_cast<const void*>(a_form);
			for (auto* item = a_conditions.head; item; item = item->next) {
				const auto& data = item->data;
				if (data.functionData.params[0] == target || data.functionData.params[1] == target) {
					return true;
				}
				if (data.flags.global && static_cast<const void*>(data.comparisonValue.g) == target) {
					return true;
				}
			}
			return false;
		}

		bool EvaluatePredicate(
			const ClassifierPredicate& a_predicate,
			RE::TESForm* a_producedItem,
			RE::BGSConstructibleObject* a_recipe,
			std::unordered_map<std::string, RE::TESForm*>& a_resolveCache)
		{
			using Kind = ClassifierPredicate::Kind;

			switch (a_predicate.kind) {
			case Kind::kAlways:
				return true;

			case Kind::kSignature:
				{
					if (!a_producedItem) {
						return false;
					}
					const auto formType = a_producedItem->GetFormType();
					for (const auto& code : a_predicate.values) {
						RE::FormType wanted{};
						if (ResolveSignature(code, wanted) && formType == wanted) {
							return true;
						}
					}
					return false;
				}

			case Kind::kKeyword:
				{
					auto* keywordForm = a_producedItem ? a_producedItem->As<RE::BGSKeywordForm>() : nullptr;
					if (!keywordForm) {
						return false;
					}
					for (const auto& kw : a_predicate.values) {
						if (keywordForm->HasKeywordString(kw)) {
							return true;
						}
					}
					return false;
				}

			case Kind::kArmorType:
				{
					auto* biped = a_producedItem ? a_producedItem->As<RE::BGSBipedObjectForm>() : nullptr;
					if (!biped) {
						return false;
					}
					const auto armorType = biped->GetArmorType();
					for (const auto& want : a_predicate.values) {
						if ((want == "Light" && armorType == RE::BGSBipedObjectForm::ArmorType::kLightArmor) ||
							(want == "Heavy" && armorType == RE::BGSBipedObjectForm::ArmorType::kHeavyArmor) ||
							(want == "Clothing" && armorType == RE::BGSBipedObjectForm::ArmorType::kClothing)) {
							return true;
						}
					}
					return false;
				}

			case Kind::kEdidContains:
				return a_producedItem && ContainsAnyCI(LookupEditorID(a_producedItem), a_predicate.values);

			case Kind::kFullContains:
				{
					auto* fullName = a_producedItem ? a_producedItem->As<RE::TESFullName>() : nullptr;
					const char* full = fullName ? fullName->GetFullName() : nullptr;
					return full && ContainsAnyCI(full, a_predicate.values);
				}

			case Kind::kRecipeEdidContains:
				return a_recipe && ContainsAnyCI(LookupRecipeEditorID(a_recipe), a_predicate.values);

			case Kind::kRecipeHasCondition:
				{
					if (!a_recipe) {
						return false;
					}
					for (const auto& id : a_predicate.values) {
						if (auto* form = ResolveCached(id, a_resolveCache); form && RecipeReferencesForm(a_recipe->conditions, form)) {
							return true;
						}
					}
					return false;
				}

			case Kind::kAll:
				for (const auto& child : a_predicate.children) {
					if (!EvaluatePredicate(child, a_producedItem, a_recipe, a_resolveCache)) {
						return false;
					}
				}
				return true;

			case Kind::kAny:
				for (const auto& child : a_predicate.children) {
					if (EvaluatePredicate(child, a_producedItem, a_recipe, a_resolveCache)) {
						return true;
					}
				}
				return false;

			case Kind::kNot:
				return !a_predicate.children.empty() &&
				       !EvaluatePredicate(a_predicate.children[0], a_producedItem, a_recipe, a_resolveCache);
			}

			return false;
		}
	}

	std::vector<ClassifierGroup> LoadClassifierGroups(
		const nlohmann::ordered_json& a_mainConfig,
		const std::vector<ConfigFile::ExternalConfig>& a_externalConfigs)
	{
		std::vector<ClassifierGroup> result = ParseClassifiersArray(a_mainConfig, ConfigFile::kPath);

		std::size_t externalCount = 0;
		for (const auto& external : a_externalConfigs) {
			auto externalGroups = ParseClassifiersArray(external.content, external.path);
			externalCount += externalGroups.size();
			for (auto& g : externalGroups) {
				result.push_back(std::move(g));
			}
		}

		if (!result.empty()) {
			SKSE::log::info("loaded {} classifier group(s) ({} from external config file(s))", result.size(), externalCount);
		}

		return result;
	}

	const std::vector<ConditionSpec>* SelectClassifierConditions(
		const ClassifierGroup& a_group,
		std::string_view a_recipeBenchKeywordEditorID,
		RE::TESForm* a_producedItem,
		RE::BGSConstructibleObject* a_recipe,
		std::unordered_map<std::string, RE::TESForm*>& a_resolveCache)
	{
		if (!a_group.benchKeywords.empty()) {
			const bool benchMatches = std::any_of(a_group.benchKeywords.begin(), a_group.benchKeywords.end(),
				[&](const std::string& a_bk) { return a_bk == a_recipeBenchKeywordEditorID; });
			if (!benchMatches) {
				return nullptr;
			}
		}

		// "when" (baseMatch) is ANDed onto every rule below, checked once
		// here rather than once per rule, so a recipe that fails it skips
		// the whole group without evaluating any individual rule's match.
		if (!EvaluatePredicate(a_group.baseMatch, a_producedItem, a_recipe, a_resolveCache)) {
			return nullptr;
		}

		for (const auto& rule : a_group.rules) {
			if (EvaluatePredicate(rule.match, a_producedItem, a_recipe, a_resolveCache)) {
				return &rule.conditions;
			}
		}

		return nullptr;
	}
}
