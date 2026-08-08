#pragma once

#include "EditorIDCache.h"

#include "SKSEMenuFramework.h"

namespace RPP::UI
{
	namespace ImGui = ImGuiMCP;

	template <std::size_t N>
	void CopyIntoBuffer(std::array<char, N>& a_buf, const std::string& a_src)
	{
		const std::size_t n = std::min(a_src.size(), N - 1);
		std::copy_n(a_src.data(), n, a_buf.data());
		a_buf[n] = '\0';
	}

	// Case-insensitive substring search with zero heap allocations,
	// important here specifically, since this runs against a candidate
	// list that can be 50,000+ entries long (every EditorID in the game)
	// once per frame while a field is focused. Allocating a lowercased
	// copy of every candidate on every frame would be real, visible
	// overhead at that scale; comparing in place via a case-insensitive
	// predicate avoids it entirely. Kept inline (header, not a .cpp
	// definition) so it can still be inlined into AutocompleteInputText's
	// hot loop below across translation units.
	inline bool ContainsCaseInsensitive(std::string_view a_haystack, std::string_view a_needle)
	{
		if (a_needle.empty()) {
			return true;
		}
		return std::search(a_haystack.begin(), a_haystack.end(), a_needle.begin(), a_needle.end(),
			[](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }) != a_haystack.end();
	}

	enum class Indicator
	{
		None,
		Form,        // green OK / red not found, via TESForm resolution
		ActorValue,  // green OK / red not found, via the Actor Value table
	};

	// Small green "OK" / red "not found" indicator for a live-typed
	// identifier. Blank fields show a neutral marker rather than a scary
	// red mark on a freshly-added, not-yet-filled-in row.
	void ValidationIndicator(const char* a_id);

	// Same idea for GetActorValue's param1, which is an Actor Value name
	// or a raw ID (0-163) rather than a form, so the form-based check
	// above would be the wrong test entirely.
	void ActorValueIndicator(const char* a_text);

	// Cached match list for whichever field is currently showing
	// suggestions. Only one field shows them at a time, so a single
	// shared instance is enough. Definition here (not hidden in the .cpp)
	// because AutocompleteInputText's inline template body below
	// manipulates its fields directly.
	struct SuggestionState
	{
		const void* owner = nullptr;  // buffer of the field the list belongs to
		std::string query;            // text the cached matches were computed for
		std::vector<const Candidate*> matches;

		// Value most recently chosen from the list. Keeps it closed on
		// that exact text instead of reopening under the cursor.
		std::string justPicked;

		void Close()
		{
			owner = nullptr;
			query.clear();
			matches.clear();
		}
	};

	// Single shared instance, defined once in UIAutocomplete.cpp, shared
	// across every AutocompleteInputText<N> instantiation and every
	// translation unit that includes this header (Mappings editor,
	// Classifiers editor, ...), since only one field across the whole
	// game window can be focused at a time anyway.
	SuggestionState& Suggestions();

	// Bumped every time a suggestion is picked, and appended to each text
	// field's ImGui ID (after "##", so it stays invisible).
	//
	// While an InputText is active, ImGui keeps its OWN copy of the text
	// and writes that copy back to the caller's buffer each frame, which
	// can silently undo a value written in from outside, exactly what
	// picking a suggestion does. Changing the widget's ID makes ImGui
	// treat it as a new widget and re-initialise from the buffer, so the
	// picked value survives.
	int& PickRevision();

	// Renders an InputTextWithHint, its optional validation indicator,
	// and, while the field has focus and at least 2 characters are
	// typed, a scrollable list of matching candidates below it. Clicking
	// a suggestion fills the buffer. Returns true if the buffer changed
	// this frame, by typing or by picking.
	//
	// The list is an INLINE child window, not a separate floating one. A
	// floating window renders above the page but is hit-tested in window
	// order, so once the main window took focus back the list became
	// visible but completely dead: no hover, no click, no scroll. A
	// child window belongs to this same window and always receives input
	// normally.
	//
	// The indicator is drawn BEFORE the list on purpose. Indicators use
	// SameLine(), and SameLine() after a child window rewinds the layout
	// cursor to that child's top edge, so anything drawn afterwards would
	// overlap the list. This ordering was introduced while chasing a
	// different bug and was never proven to be the cause of anything,
	// but it is the correct order regardless, and costs nothing.
	template <std::size_t N>
	bool AutocompleteInputText(
		const char* a_label,
		const char* a_hint,
		std::array<char, N>& a_buf,
		const std::vector<Candidate>& a_candidates,
		Indicator a_indicator = Indicator::None)
	{
		constexpr std::size_t kMaxSuggestions = 15;
		constexpr std::size_t kMinCharsToSuggest = 2;

		// "label##N" renders as just "label"; the suffix only changes
		// the widget's identity. See PickRevision() for why it must.
		const std::string fieldID = fmt::format("{}##{}", a_label, PickRevision());

		bool changed = ImGui::InputTextWithHint(fieldID.c_str(), a_hint, a_buf.data(), a_buf.size());
		const bool isActive = ImGui::IsItemActive();

		// Before the list. See the note above.
		switch (a_indicator) {
		case Indicator::Form:
			ValidationIndicator(a_buf.data());
			break;
		case Indicator::ActorValue:
			ActorValueIndicator(a_buf.data());
			break;
		case Indicator::None:
			break;
		}

		auto& sugg = Suggestions();

		// Focusing a different field hands the list over to it and
		// invalidates the cached matches, which may have come from a
		// completely different candidate list.
		if (isActive && sugg.owner != a_buf.data()) {
			sugg.Close();
			sugg.owner = a_buf.data();
		}
		// OPENING is focus-driven; CLOSING deliberately is NOT.
		//
		// Every previous attempt gated drawing on some live input state:
		// the field being active, the list being hovered, the mouse
		// being held. Pressing a row falsifies the first, and the other
		// two proved unreliable through the framework's GetProcAddress
		// wrapper. Any of them going false between mouse-press and
		// mouse-RELEASE unsubmits the list, and since a click is only
		// reported on release, the pick could never fire. The log
		// confirmed it: not one pick across many attempts.
		//
		// So once this field owns the list, the list keeps being drawn
		// until something DEFINITE closes it: a pick, the text dropping
		// below the minimum, or another field taking over. None of those
		// can happen mid-click.
		if (isActive && sugg.owner != a_buf.data()) {
			sugg.Close();
			sugg.owner = a_buf.data();
		}
		if (sugg.owner != a_buf.data()) {
			return changed;
		}

		const std::string_view current{ a_buf.data() };

		// Closed after a pick, and stays closed until the text is edited
		// to something else, otherwise the list would spring straight
		// back open on the value just chosen.
		if (!sugg.justPicked.empty()) {
			if (current == sugg.justPicked) {
				sugg.Close();
				return changed;
			}
			sugg.justPicked.clear();
		}

		if (current.size() < kMinCharsToSuggest) {
			sugg.query.clear();
			sugg.matches.clear();
			return changed;
		}

		// Only rescan when the typed text actually changes. The
		// candidate lists can hold 100k+ EditorIDs, and a rare substring
		// means scanning all of them; doing that every frame is what
		// made simply moving the mouse over the list stutter, since the
		// results can't change unless the query does.
		if (sugg.query != current) {
			sugg.query.assign(current);
			sugg.matches.clear();
			for (const auto& candidate : a_candidates) {
				if (ContainsCaseInsensitive(candidate.editorID, current)) {
					sugg.matches.push_back(&candidate);
					if (sugg.matches.size() >= kMaxSuggestions) {
						break;
					}
				}
			}
		}

		if (sugg.matches.empty()) {
			return changed;
		}

		// Drawn INLINE, right here, rather than floated over the page at
		// the end of the frame. An overlapping list looks better (it
		// doesn't shove the fields below it down), but it cannot
		// reliably be clicked: ImGui resolves hover by last-submitted
		// (so highlighting works) while the mouse-DOWN is claimed by
		// whichever widget at that position was submitted FIRST, which
		// is always one of the fields underneath. Occupying real layout
		// space is what makes the click land.
		constexpr float kVisibleRows = 8.0f;
		// Frame height, not text-line height: each row is a Button, and
		// a button is a framed widget (taller than a bare line of text).
		// Sizing on text height alone made the list shorter than
		// kVisibleRows actually needs.
		const float rowHeight = ImGui::GetFrameHeightWithSpacing();
		const float height =
			std::min<float>(static_cast<float>(sugg.matches.size()), kVisibleRows) * rowHeight + 8.0f;

		ImGui::PushID(a_label);

		// Styled to read as a list rather than a stack of chunky
		// buttons: transparent background, left-aligned text. These stay
		// Buttons on purpose. Button is the control proven to work in
		// this menu, and the log confirmed picks only started firing
		// once the close-gate was fixed. Restoring the LOOK of a
		// selectable list without going back to the widget whose
		// behaviour here was never established.
		ImGui::PushStyleColor(ImGui::ImGuiCol_Button, ImGui::ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
		ImGui::PushStyleVar(ImGui::ImGuiStyleVar_ButtonTextAlign, ImGui::ImVec2{ 0.0f, 0.5f });

		const Candidate* picked = nullptr;
		if (ImGui::BeginChild("##suggestions", ImGui::ImVec2{ 0.0f, height }, ImGui::ImGuiChildFlags_Border)) {
			for (const auto* match : sugg.matches) {
				// Button rather than Selectable. Not because Selectable
				// is broken. It was suspected during debugging, but the
				// actual cause of clicks never registering was the
				// close-gate above tearing the list down between mouse
				// press and release. Selectable would most likely work
				// now. Button is kept simply because it is the control
				// verified working here, and it is styled (transparent
				// background, left-aligned text) to read as a list row
				// anyway, so there is nothing to gain by switching back.
				//
				// Width -1 makes each row span the list, so the whole
				// row is the hit target rather than just the text.
				// Everything after "##" is ID-only and not displayed.
				const std::string rowLabel = match->detail.empty() ?
					fmt::format("{}##{}", match->editorID, match->editorID) :
					fmt::format("{:<34}{}##{}", match->editorID, match->detail, match->editorID);

				if (ImGui::Button(rowLabel.c_str(), ImGui::ImVec2{ -1.0f, 0.0f })) {
					picked = match;
					break;
				}
			}
		}
		ImGui::EndChild();

		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		ImGui::PopID();

		// Applied after EndChild so the match list isn't mutated while
		// it's still being iterated.
		if (picked) {
			// Logged deliberately: after several rounds of failing to
			// reproduce this remotely, the log is the ground truth for
			// whether a click actually reaches this branch.
			SKSE::log::info("autocomplete: picked '{}'", picked->editorID);

			// The candidate's own spelling, so picking "IngotIron" after
			// typing "ingotiron" corrects the case.
			CopyIntoBuffer(a_buf, picked->editorID);
			changed = true;

			// Forces the field to rebuild from the buffer we just wrote.
			// See PickRevision().
			++PickRevision();

			// Without this the list would reopen immediately on the
			// value that was just chosen, right under the cursor.
			sugg.justPicked = picked->editorID;
			sugg.Close();
		}

		return changed;
	}
}
