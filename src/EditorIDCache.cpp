#include "EditorIDCache.h"

#include <spdlog/fmt/fmt.h>

namespace RPP
{
	namespace
	{
		// IMPORTANT: do NOT build these from TESForm::GetFormEditorID().
		// That virtual's base implementation just returns "", and only a
		// handful of form types override it (TESGlobal, BGSKeyword,
		// TESRace, ...). The game doesn't retain EditorIDs in memory for
		// most records. None of the types this plugin cares about
		// (BGSConstructibleObject, TESObjectMISC/ARMO/WEAP, IngredientItem,
		// AlchemyItem, TESObjectBOOK, TESKey) override it, so asking a
		// recipe or an ingot for its EditorID always yields an empty
		// string.
		//
		// CommonLibSSE-NG's TESForm::GetAllFormsByEditorID()
		// map IS populated for those records though (it's the same table
		// LookupByEditorID resolves against, which this plugin already
		// relies on elsewhere), so everything here is derived from a
		// single pass over that map instead, bucketed by form type.
		// The game's own FormType -> 4-character signature table ("ARMO",
		// "WEAP", ...). Indexed directly rather than searched, with the
		// entry's own formType verified so a layout change degrades to a
		// blank label instead of a wrong one.
		std::string_view FormTypeCode(RE::FormType a_type)
		{
			const auto table = RE::FORM_ENUM_STRING::GetFormEnumString();
			const auto index = static_cast<std::size_t>(a_type);
			if (index < table.size() && table[index].formType == a_type && table[index].formString) {
				return table[index].formString;
			}
			return {};
		}

		// "ARMO (Skyrim.esm)". GetFile(0) is the ORIGINAL source plugin,
		// not whichever mod overrode the record last (see the same note in
		// RecipePatcher.cpp's DescribeRecipe).
		std::string MakeDetail(const RE::TESForm* a_form)
		{
			const std::string_view code = FormTypeCode(a_form->GetFormType());
			auto* file = a_form->GetFile(0);
			// See DescribeRecipe in RecipePatcher.cpp. Runtime-created
			// forms have no source file and that is expected, not an error.
			const std::string_view plugin = file ? file->GetFilename() :
				(a_form->IsDynamicForm() ? std::string_view{ "dynamic" } : std::string_view{});

			if (code.empty() && plugin.empty()) {
				return {};
			}
			if (plugin.empty()) {
				return std::string{ code };
			}
			if (code.empty()) {
				return std::string{ plugin };
			}
			return fmt::format("{} ({})", code, plugin);
		}

		struct Caches
		{
			std::vector<Candidate> all;
			std::vector<Candidate> recipes;
			std::vector<Candidate> craftableItems;

			// Only the handful of types the curated functions need. A
			// bucket for every type would duplicate all 400k+ EditorIDs
			// for no benefit.
			std::unordered_map<RE::FormType, std::vector<Candidate>> byType;

			// Reverse lookup for recipes only (Form -> EditorID), so log
			// output can name a recipe rather than only its FormID. Kept
			// deliberately narrow: a reverse map covering every form type
			// would duplicate every EditorID string in memory for no
			// current benefit.
			std::unordered_map<const RE::TESForm*, std::string> recipeEditorIDs;

			// Reverse lookup covering EVERY form in the EditorID table,
			// unlike recipeEditorIDs above. The classifier predicates
			// (Classifiers.cpp) need this for arbitrary produced items
			// (ARMO/WEAP/AMMO/MISC/...), not just recipes. Yes, this does
			// duplicate every EditorID string a second time; unavoidable
			// once more than one record type needs reverse lookup, and
			// still bounded by the size of the game's own EditorID table.
			std::unordered_map<const RE::TESForm*, std::string> allEditorIDsByForm;
		};

		const Caches& GetCaches()
		{
			static const Caches caches = []() {
				Caches c;

				const auto& [map, lock] = RE::TESForm::GetAllFormsByEditorID();
				const RE::BSReadLockGuard locker{ lock };
				if (map) {
					c.all.reserve(map->size());
					for (const auto& [editorID, form] : *map) {
						if (!form || editorID.empty()) {
							continue;
						}

						std::string id{ editorID.c_str() };
						c.allEditorIDsByForm.emplace(form, id);
						Candidate candidate{ id, MakeDetail(form) };
						c.all.push_back(candidate);

						const auto formType = form->GetFormType();
						switch (formType) {
						case RE::FormType::Perk:
						case RE::FormType::Spell:
						case RE::FormType::Quest:
						case RE::FormType::Faction:
						case RE::FormType::Global:
						case RE::FormType::Race:
						case RE::FormType::Keyword:
							c.byType[formType].push_back(candidate);
							break;
						default:
							break;
						}

						switch (formType) {
						case RE::FormType::ConstructibleObject:
							c.recipes.push_back(std::move(candidate));
							c.recipeEditorIDs.emplace(form, std::move(id));
							break;
						// The record types that actually make sense as a
						// GetItemCount target. See AllCraftableItemEditorIDs.
						case RE::FormType::Misc:
						case RE::FormType::Armor:
						case RE::FormType::Weapon:
						case RE::FormType::Ingredient:
						case RE::FormType::AlchemyItem:
						case RE::FormType::Book:
						case RE::FormType::KeyMaster:
							c.craftableItems.push_back(std::move(candidate));
							break;
						default:
							break;
						}
					}
				}

				const auto byEditorID = [](const Candidate& a, const Candidate& b) {
					return a.editorID < b.editorID;
				};
				std::sort(c.all.begin(), c.all.end(), byEditorID);
				std::sort(c.recipes.begin(), c.recipes.end(), byEditorID);
				std::sort(c.craftableItems.begin(), c.craftableItems.end(), byEditorID);
				for (auto& [type, list] : c.byType) {
					std::sort(list.begin(), list.end(), byEditorID);
				}

				SKSE::log::info("EditorID caches built: {} total, {} recipes, {} craftable items",
					c.all.size(), c.recipes.size(), c.craftableItems.size());

				// Said once, here, rather than letting every failed lookup
				// downstream imply the user's config is wrong. See
				// EditorIDTableMissingRecords in the header for why the
				// recipe count is the signal.
				if (c.recipes.empty()) {
					SKSE::log::warn(
						"the game's EditorID table has no recipe records, so no material, recipe or perk "
						"can be resolved by EditorID and this plugin will not patch anything this session");
					SKSE::log::warn(
						"this is what powerofthree's Tweaks provides (its \"editorID cache\" feature); "
						"install/update it and it will work again. Any 'could not resolve' lines below "
						"are caused by this, not by your config");
				}
				return c;
			}();
			return caches;
		}
	}

	const std::vector<Candidate>& AllEditorIDs()
	{
		return GetCaches().all;
	}

	const std::vector<Candidate>& AllRecipeEditorIDs()
	{
		return GetCaches().recipes;
	}

	const std::vector<Candidate>& AllCraftableItemEditorIDs()
	{
		return GetCaches().craftableItems;
	}

	const std::vector<Candidate>& CandidatesForFormType(RE::FormType a_type)
	{
		static const std::vector<Candidate> empty;
		const auto& byType = GetCaches().byType;
		const auto it = byType.find(a_type);
		return it != byType.end() ? it->second : empty;
	}

	std::string_view LookupRecipeEditorID(const RE::TESForm* a_recipe)
	{
		if (!a_recipe) {
			return {};
		}
		const auto& byForm = GetCaches().recipeEditorIDs;
		const auto it = byForm.find(a_recipe);
		return it != byForm.end() ? std::string_view{ it->second } : std::string_view{};
	}

	std::string_view LookupEditorID(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return {};
		}
		const auto& byForm = GetCaches().allEditorIDsByForm;
		const auto it = byForm.find(a_form);
		return it != byForm.end() ? std::string_view{ it->second } : std::string_view{};
	}

	bool EditorIDTableMissingRecords()
	{
		return GetCaches().recipes.empty();
	}
}
