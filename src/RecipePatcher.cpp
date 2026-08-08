#include "RecipePatcher.h"

#include "Classifiers.h"
#include "ConfigFile.h"
#include "Conditions.h"
#include "EditorIDCache.h"
#include "Identifiers.h"
#include "RecipeOverrides.h"
#include "Settings.h"
#include "TriggerConfig.h"

#include <spdlog/fmt/fmt.h>

namespace RPP
{
	namespace
	{
		// A human-readable identifier for a recipe in log output, e.g.
		//   RecipeArmorEbonyCuirass (0x0001393E) [Skyrim.esm]
		// or, for a recipe with no EditorID:
		//   (no EditorID) (0x00801F3A) [SomeMod.esp]
		//
		// All three parts earn their place: the EditorID is what you'd
		// search for in the config or SSEEdit, the FormID disambiguates
		// when two plugins use similar EditorIDs, and the plugin name is
		// needed to interpret the FormID at all (they're load-order
		// relative).
		std::string DescribeRecipe(RE::BGSConstructibleObject* a_recipe)
		{
			// NOT a_recipe->GetFormEditorID(): that returns "" for
			// BGSConstructibleObject (see EditorIDCache.cpp for why), which
			// is exactly why recipes used to log as a bare FormID.
			const std::string_view editorID = LookupRecipeEditorID(a_recipe);

			// GetFile() with its default index returns the LAST plugin to
			// touch the record, i.e. whichever mod overrode it last, not
			// where it came from. GetFile(0) is the original source, which
			// is what actually identifies the record.
			auto* file = a_recipe->GetFile(0);

			// A form with no source file is normally a red flag, but not
			// here: dynamic forms (FormID >= 0xFF000000) are created at
			// runtime rather than loaded from a plugin, so they genuinely
			// have neither an EditorID nor an owning file. Crafting Recipe
			// Distributor generates recipes this way. Labelling them
			// "dynamic" rather than "unknown plugin" keeps that from
			// reading like an error in the log.
			const std::string_view source = file ? file->GetFilename() :
				(a_recipe->IsDynamicForm() ? std::string_view{ "dynamic" } :
				                             std::string_view{ "unknown plugin" });

			return fmt::format("{} (0x{:08X}) [{}]",
				editorID.empty() ? std::string_view{ "(no EditorID)" } : editorID,
				a_recipe->GetFormID(),
				source);
		}

		// A human-readable rendering of a condition spec for log output,
		// e.g. "HasPerk(EbonySmithing) == true" or
		// "GetStageDone(MQ101, 30) == true".
		std::string DescribeSpec(const ConditionSpec& a_spec)
		{
			std::string result = a_spec.function;
			result += '(';
			result += a_spec.param1;
			if (!a_spec.param2.empty()) {
				result += ", ";
				result += a_spec.param2;
			}
			result += ") ";
			result += a_spec.op;
			result += ' ';
			result += a_spec.value;
			return result;
		}

		// Is a_conditions' list non-empty at all? Used to decide how to
		// apply Settings::existingPerkMode (or a per-recipe override's
		// own mode): Add (default), Replace, or Skip. Deliberately not
		// restricted to any particular condition function anymore, now
		// that this plugin can add conditions beyond HasPerk: the
		// original intent of "does this recipe already have some gating
		// condition" generalizes the same way.
		bool HasAnyCondition(const RE::TESCondition& a_conditions)
		{
			return a_conditions.head != nullptr;
		}

		// Removes every condition from a_conditions' linked list. Safe
		// even for condition items the game itself created when loading
		// the recipe: TESConditionItem overrides operator new/delete via
		// TES_HEAP_REDEFINE_NEW() to route through the game's own memory
		// heap, so deletion here goes through the exact same allocator
		// regardless of which code originally created the node.
		std::size_t RemoveAllConditions(RE::TESCondition& a_conditions)
		{
			std::size_t removed = 0;
			RE::TESConditionItem* item = a_conditions.head;
			while (item) {
				RE::TESConditionItem* next = item->next;
				delete item;
				++removed;
				item = next;
			}
			a_conditions.head = nullptr;
			return removed;
		}

		// Resolves each trigger row's material identifier against
		// currently loaded forms, grouping by material since more than
		// one row can share the same material (to add more than one
		// condition for it). Dropping (with a log message) anything that
		// doesn't resolve.
		std::unordered_map<RE::TESBoundObject*, std::vector<ConditionSpec>> ResolveTriggers(
			const std::vector<TriggerEntry>& a_entries)
		{
			std::unordered_map<RE::TESBoundObject*, std::vector<ConditionSpec>> resolved;

			std::size_t resolvedCount = 0;
			for (const auto& entry : a_entries) {
				auto* material = ResolveIdentifier<RE::TESBoundObject>(entry.materialID);
				if (!material) {
					SKSE::log::warn("could not resolve material '{}' (mod not installed, or ID is wrong)", entry.materialID);
					continue;
				}
				resolved[material].push_back(entry.condition);
				++resolvedCount;
			}

			SKSE::log::info("resolved {}/{} trigger rows", resolvedCount, a_entries.size());
			return resolved;
		}

		struct ResolvedOverrides
		{
			std::unordered_set<RE::BGSConstructibleObject*> excludedRecipes;
			std::unordered_map<RE::BGSConstructibleObject*, std::vector<ConditionSpec>> forcedConditions;
			// Per-recipe Add/Replace/Skip choice, present only for recipes
			// that have a (non-exclude) override entry. Takes priority
			// over Settings::existingPerkMode for that specific recipe.
			std::unordered_map<RE::BGSConstructibleObject*, int> perRecipeMode;
		};

		// Resolves each override's recipe identifier against currently
		// loaded forms, then chains entries together for any recipe
		// targeted by more than one source. a_overrides is already in the
		// right processing order (main config's entries first, then each
		// external *_RCP.json file's entries in alphabetical order by
		// filename; see LoadRecipeOverrides), so a straightforward
		// left-to-right scan is enough: for each recipe, track a running
		// "chain state" that an Add-mode entry appends to and a
		// Replace-mode entry (or exclude) resets, so whichever behavior
		// the LAST entry in a recipe's chain establishes is what wins
		// overall; see RecipeOverrides.h's RecipeOverride comment for
		// the full reasoning. Unresolvable recipes are logged and
		// skipped. Each surviving entry's own conditions are resolved
		// later, individually, when actually applied
		// (AddConditionIfMissing does that resolution internally).
		ResolvedOverrides ResolveRecipeOverrides(const std::vector<RecipeOverride>& a_overrides)
		{
			ResolvedOverrides resolved;

			struct ChainState
			{
				bool excluded = false;
				bool wipeVanilla = false;  // true once any Replace-mode entry has occurred in this recipe's chain
				std::vector<ConditionSpec> conditions;
			};
			std::unordered_map<RE::BGSConstructibleObject*, ChainState> chains;

			for (const auto& o : a_overrides) {
				auto* recipe = ResolveIdentifier<RE::BGSConstructibleObject>(o.recipeID);
				if (!recipe) {
					SKSE::log::warn("recipeOverrides: could not resolve recipe '{}' (mod not installed, or ID is wrong)", o.recipeID);
					continue;
				}

				auto& chain = chains[recipe];  // default-constructed on first reference

				if (o.exclude) {
					if (chain.excluded || !chain.conditions.empty()) {
						SKSE::log::info("recipeOverrides: '{}': entry from {} (exclude) discards what earlier entries for this recipe had established",
							o.recipeID, o.sourceFile);
					}
					chain.excluded = true;
					chain.wipeVanilla = false;
					chain.conditions.clear();
					continue;
				}

				if (chain.excluded) {
					SKSE::log::info("recipeOverrides: '{}': entry from {} un-excludes the recipe (a later entry always wins)",
						o.recipeID, o.sourceFile);
				}
				chain.excluded = false;

				if (o.mode == ExistingPerkMode::kReplace) {
					if (!chain.conditions.empty()) {
						SKSE::log::info("recipeOverrides: '{}': entry from {} (Replace Existing) discards what earlier entries for this recipe had established",
							o.recipeID, o.sourceFile);
					}
					chain.wipeVanilla = true;
					chain.conditions = o.conditions;
				} else {
					// Add to Existing (or Skip, hand-edited only, treated
					// the same way here since it has no coherent meaning
					// as one link in a multi-source chain): append onto
					// whatever the chain has accumulated so far, leaving
					// wipeVanilla exactly as an earlier Replace (if any)
					// already set it.
					chain.conditions.insert(chain.conditions.end(), o.conditions.begin(), o.conditions.end());
				}
			}

			for (const auto& [recipe, chain] : chains) {
				if (chain.excluded) {
					resolved.excludedRecipes.insert(recipe);
					continue;
				}
				resolved.perRecipeMode[recipe] = chain.wipeVanilla ? ExistingPerkMode::kReplace : ExistingPerkMode::kAdd;
				resolved.forcedConditions[recipe] = chain.conditions;
			}

			if (!resolved.excludedRecipes.empty() || !resolved.forcedConditions.empty()) {
				SKSE::log::info("recipeOverrides: {} recipe(s) excluded, {} recipe(s) with forced conditions",
					resolved.excludedRecipes.size(), resolved.forcedConditions.size());
			}

			return resolved;
		}

		// Logs how long the enclosing scope took, in seconds, when it goes
		// out of scope, including every early-return path (disabled via
		// settings, no triggers resolved, etc.), not just the "happy path"
		// at the bottom of the function, since this fires from the
		// destructor rather than needing a log call at each return site.
		class ScopedTimer
		{
		public:
			explicit ScopedTimer(std::string_view a_label) :
				_label(a_label),
				_start(std::chrono::steady_clock::now())
			{}

			~ScopedTimer()
			{
				const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - _start).count();
				SKSE::log::info("{} took {:.3f} seconds", _label, elapsed);
			}

		private:
			std::string_view _label;
			std::chrono::steady_clock::time_point _start;
		};
	}

	void ApplyPerkRequirementsToAllRecipes()
	{
		ScopedTimer timer{ "RecipeConditionPatcher patch pass" };

		const auto& settings = CurrentSettings();

		spdlog::default_logger()->set_level(
			settings.logLevel <= 0 ? spdlog::level::info :
			settings.logLevel == 1 ? spdlog::level::warn :
			                         spdlog::level::err);

		// SKSE::log::info(...) is a function-style call, so its arguments
		// are fully evaluated BEFORE spdlog gets a chance to discard the
		// message for being below the active level. The per-recipe log
		// lines below build strings (DescribeRecipe/DescribeSpec, several
		// heap allocations each) and fire once per condition added across
		// thousands of recipes, so without this guard, someone setting
		// Log Verbosity to "Warnings Only"/"Errors Only" specifically to
		// cut logging overhead would still pay all of that cost and then
		// throw the result away. Mirrors the level mapping directly above.
		const bool logInfo = settings.logLevel <= 0;

		if (!settings.enabled) {
			SKSE::log::info("RecipeConditionPatcher is disabled via the in-game menu; skipping");
			return;
		}

		const auto config = ConfigFile::Read();
		const auto externalConfigs = ConfigFile::ReadExternalConfigs();

		const auto entries = LoadTriggerEntries(config, externalConfigs);
		const auto materialToConditions = ResolveTriggers(entries);

		if (materialToConditions.empty()) {
			SKSE::log::warn("no trigger rows resolved; material-based patching will do nothing this pass "
				"(recipeOverrides, if any, still apply)");
		}

		const auto rawOverrides = LoadRecipeOverrides(config, externalConfigs);
		const auto overrides = ResolveRecipeOverrides(rawOverrides);

		const auto classifierGroups = LoadClassifierGroups(config, externalConfigs);
		// Memoizes identifier -> TESForm* lookups for classifiers' kRecipeHasCondition
		// predicate across this entire pass (see Classifiers.h). The same
		// handful of identifiers get checked against every recipe a
		// matching classifier group applies to.
		std::unordered_map<std::string, RE::TESForm*> classifierResolveCache;

		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			SKSE::log::error("TESDataHandler unavailable; cannot patch recipes");
			return;
		}

		const auto& recipes = dataHandler->GetFormArray<RE::BGSConstructibleObject>();

		std::size_t recipesInspected = 0;
		std::size_t recipesPatched = 0;
		std::size_t recipesSkippedExistingCondition = 0;
		std::size_t recipesReplaced = 0;
		std::size_t recipesExcludedByOverride = 0;
		std::size_t conditionsAdded = 0;
		std::size_t conditionsRemoved = 0;
		std::size_t conditionsAddedFromOverrides = 0;
		std::size_t conditionsAddedFromClassifiers = 0;
		std::size_t conditionResolutionFailures = 0;

		for (auto* cobj : recipes) {
			if (!cobj) {
				continue;
			}
			++recipesInspected;

			// DescribeRecipe() does several allocations (EditorID lookup,
			// owning-plugin lookup, format) and a recipe can be logged
			// about once per condition added to it, so build it at most
			// once per recipe, and only if something actually logs.
			std::string recipeDesc;
			const auto RecipeDesc = [&]() -> const std::string& {
				if (recipeDesc.empty()) {
					recipeDesc = DescribeRecipe(cobj);
				}
				return recipeDesc;
			};

			// A recipeOverrides "exclude" entry takes priority over
			// everything else, including the global existingPerkMode;
			// it's a deliberate, targeted "don't touch this one" opt-out.
			if (overrides.excludedRecipes.contains(cobj)) {
				++recipesExcludedByOverride;
				if (logInfo) {
					SKSE::log::info("[{}] excluded via recipeOverrides", RecipeDesc());
				}
				continue;
			}

			if (HasAnyCondition(cobj->conditions)) {
				// A recipeOverrides entry's own "mode" takes priority over
				// the global existingPerkMode setting for this specific
				// recipe, if one is configured for it.
				int effectiveMode = settings.existingPerkMode;
				if (const auto it = overrides.perRecipeMode.find(cobj); it != overrides.perRecipeMode.end()) {
					effectiveMode = it->second;
				}

				if (effectiveMode == ExistingPerkMode::kSkip) {
					++recipesSkippedExistingCondition;
					continue;
				}
				if (effectiveMode == ExistingPerkMode::kReplace) {
					const auto removedCount = RemoveAllConditions(cobj->conditions);
					conditionsRemoved += removedCount;
					++recipesReplaced;
					if (logInfo) {
						SKSE::log::info("[{}] replaced {} existing condition(s)", RecipeDesc(), removedCount);
					}
				}
				// kAdd: fall through and layer our conditions on top of
				// whatever's already there, same as a recipe with no
				// existing conditions at all.
			}

			bool patchedThisRecipe = false;

			// A recipe with its own (non-exclude) recipeOverrides entry is
			// handled EXCLUSIVELY by that override. Material-based
			// scanning is skipped entirely for it. Without this check, a
			// recipe's override conditions would just be layered
			// alongside whatever the mappings table also adds for that
			// same recipe, which defeats the point of an "override": it
			// stops being authoritative for that recipe and becomes just
			// another set of additions.
			const auto overrideIt = overrides.forcedConditions.find(cobj);
			const bool hasOverride = overrideIt != overrides.forcedConditions.end();

			if (!hasOverride) {
				// Collect the distinct conditions this recipe's required
				// materials call for, then add any that aren't already
				// required. (After a Replace above, none are; everything
				// gets freshly added.)
				cobj->requiredItems.ForEachContainerObject([&](RE::ContainerObject& a_entry) {
					if (!a_entry.obj) {
						return RE::BSContainer::ForEachResult::kContinue;
					}

					const auto it = materialToConditions.find(a_entry.obj);
					if (it == materialToConditions.end()) {
						return RE::BSContainer::ForEachResult::kContinue;
					}

					// Iterated in reverse: AddConditionIfMissing prepends onto
					// the recipe's condition list, so adding this material's
					// specs back-to-front leaves them in the same order they
					// were authored in the mappings array, which is what
					// each spec's "logic" (AND/OR against whichever
					// condition follows it) needs to actually mean what was
					// configured, rather than ending up reversed.
					for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
						const auto& spec = *rit;
						bool alreadyPresent = false;
						std::string failReason;
						if (!AddConditionIfMissing(cobj->conditions, spec, alreadyPresent, failReason)) {
							++conditionResolutionFailures;
							SKSE::log::warn("[{}] skipping condition for material trigger: {}", RecipeDesc(), failReason);
						} else if (!alreadyPresent) {
							patchedThisRecipe = true;
							++conditionsAdded;
							if (logInfo) {
								SKSE::log::info("[{}] added condition: {}", RecipeDesc(), DescribeSpec(spec));
							}
						}
					}

					return RE::BSContainer::ForEachResult::kContinue;
				});

				// Classifier groups: bulk rules that key off what this
				// recipe PRODUCES (record type/keywords/name substrings)
				// rather than what it requires. See Classifiers.h. Each
				// group is independent (a recipe can pick up conditions
				// from more than one group), but within one group only
				// the first matching rule fires.
				if (!classifierGroups.empty()) {
					const std::string_view benchEdid = cobj->benchKeyword ? cobj->benchKeyword->GetFormEditorID() : std::string_view{};
					RE::TESForm* producedItem = cobj->createdItem;

					for (const auto& group : classifierGroups) {
						const auto* conditions = SelectClassifierConditions(
							group, benchEdid, producedItem, cobj, classifierResolveCache);
						if (!conditions) {
							continue;
						}

						// Same reverse-iteration reasoning as the material
						// loop above: AddConditionIfMissing prepends, so
						// this preserves the order the rule's own
						// "conditions" array was authored in.
						for (auto rit = conditions->rbegin(); rit != conditions->rend(); ++rit) {
							const auto& spec = *rit;
							bool alreadyPresent = false;
							std::string failReason;
							if (!AddConditionIfMissing(cobj->conditions, spec, alreadyPresent, failReason)) {
								++conditionResolutionFailures;
								SKSE::log::warn("[{}] skipping classifier condition: {}", RecipeDesc(), failReason);
							} else if (!alreadyPresent) {
								patchedThisRecipe = true;
								++conditionsAddedFromClassifiers;
								if (logInfo) {
									SKSE::log::info("[{}] added condition (via classifiers): {}", RecipeDesc(), DescribeSpec(spec));
								}
							}
						}
					}
				}
			}

			// A recipeOverrides "conditions" entry for this specific
			// recipe: force these on, exclusively (see above), regardless
			// of what materials say. Runs even if this recipe had no
			// mapped materials at all.
			if (hasOverride) {
				// Iterated in reverse for the same reason as the material
				// loop above: AddConditionIfMissing prepends, so this
				// preserves the order the override's own "conditions" array
				// was authored in.
				for (auto rit = overrideIt->second.rbegin(); rit != overrideIt->second.rend(); ++rit) {
					const auto& spec = *rit;
					bool alreadyPresent = false;
					std::string failReason;
					if (!AddConditionIfMissing(cobj->conditions, spec, alreadyPresent, failReason)) {
						++conditionResolutionFailures;
						SKSE::log::warn("[{}] skipping recipeOverrides condition: {}", RecipeDesc(), failReason);
					} else if (!alreadyPresent) {
						patchedThisRecipe = true;
						++conditionsAddedFromOverrides;
						if (logInfo) {
							SKSE::log::info("[{}] added condition (via recipeOverrides): {}", RecipeDesc(), DescribeSpec(spec));
						}
					}
				}
			}

			if (patchedThisRecipe) {
				++recipesPatched;
			}
		}

		// "conditions total" is a real grand total across all three
		// sources. It used to just be conditionsAdded (material
		// mappings), a leftover from before recipeOverrides/classifiers
		// existed, which made the line read as if overrides/classifiers
		// weren't counted at all.
		const std::size_t totalConditionsAdded = conditionsAdded + conditionsAddedFromClassifiers + conditionsAddedFromOverrides;

		SKSE::log::info(
			"inspected {} recipes, patched {} of them, added {} conditions total "
			"({} from material mappings, {} from classifiers, {} from recipeOverrides); "
			"{} recipes had all existing conditions replaced, removing {} conditions; "
			"{} recipes skipped for already having a condition; "
			"{} recipes fully excluded via recipeOverrides; "
			"{} condition(s) failed to resolve and were skipped",
			recipesInspected, recipesPatched, totalConditionsAdded,
			conditionsAdded, conditionsAddedFromClassifiers, conditionsAddedFromOverrides,
			recipesReplaced, conditionsRemoved, recipesSkippedExistingCondition,
			recipesExcludedByOverride,
			conditionResolutionFailures);
	}
}
