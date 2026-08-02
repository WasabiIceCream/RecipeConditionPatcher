#pragma once

namespace RPP::UI::MappingsEditor
{
	// Second tab under the "Recipe Condition Patcher" section: an in-game
	// add/edit/delete editor for the "mappings" and "recipeOverrides"
	// sections of a config file. Operates on exactly one file at a time -
	// the main config or any external *_RCP.json - chosen from the tab's
	// own "Editing" dropdown, with a "Save As..." option for creating a
	// new one. Loading and saving both go through that single selection,
	// so entries from one file are never written into another.
	void __stdcall Render();
}
