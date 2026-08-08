#pragma once

namespace RPP::UI::ClassifiersEditor
{
	// Third tab under the "Recipe Condition Patcher" section: an in-game
	// add/edit/delete editor for the "classifiers" section of a config
	// file. Operates on exactly one file at a time, same convention as
	// MappingsEditor::Render (its own independent file selection, not
	// shared with the Mappings tab). A predicate too complex for the
	// visual block/row editor (see ClassifierPredicateEditor.h) is shown
	// read-only instead of being risked on a lossy round-trip.
	void __stdcall Render();
}
