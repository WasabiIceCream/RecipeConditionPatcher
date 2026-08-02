#pragma once

namespace RPP
{
	// Walks every loaded BGSConstructibleObject (crafting recipe) and, for
	// any recipe that requires a material we have a trigger mapping for,
	// injects the corresponding condition(s) if not already present.
	// Also applies recipeOverrides (force conditions onto - or exclude -
	// one specific recipe): a recipe with a (non-exclude) override is
	// handled EXCLUSIVELY by that override - material-based mapping
	// scanning is skipped entirely for it, so the override always takes
	// priority regardless of what the mappings table would otherwise add.
	// Must be called after kDataLoaded (i.e. once all plugins' records
	// are in memory). The function name is kept from before conditions
	// were generalized beyond HasPerk.
	void ApplyPerkRequirementsToAllRecipes();
}
