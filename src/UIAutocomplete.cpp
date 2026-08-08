#include "UIAutocomplete.h"

#include "ActorValueTable.h"
#include "Identifiers.h"
#include "UIStyle.h"

namespace RPP::UI
{
	namespace
	{
		// From the shared palette so the indicators match every tab.
		constexpr auto kValidColor = Style::kValidText;
		constexpr auto kInvalidColor = Style::kInvalidText;
	}

	SuggestionState& Suggestions()
	{
		static SuggestionState state;
		return state;
	}

	int& PickRevision()
	{
		static int revision = 0;
		return revision;
	}

	void ValidationIndicator(const char* a_id)
	{
		const std::string id{ a_id };
		if (id.empty()) {
			ImGui::SameLine();
			ImGui::TextDisabled("(empty)");
			return;
		}

		// TESForm::As<T>() is a single generic template covering every
		// listed form type via a switch on GetFormType() (verified by
		// reading FormTraits.h directly), so a plain TESForm lookup
		// works here as a genuine "does this resolve to anything at
		// all" check.
		auto* form = ResolveIdentifier<RE::TESForm>(id);
		ImGui::SameLine();
		if (form) {
			ImGui::TextColored(kValidColor, "OK");
		} else {
			ImGui::TextColored(kInvalidColor, "not found");
		}
	}

	void ActorValueIndicator(const char* a_text)
	{
		const std::string text{ a_text };
		ImGui::SameLine();
		if (text.empty()) {
			ImGui::TextDisabled("(empty)");
			return;
		}

		// Name lookup first: it's a cheap table scan, it's the common
		// case (autocomplete offers names, not numbers), and it can't
		// throw. Only fall back to the numeric parse when the text
		// actually starts with a digit. std::stoi signals failure by
		// throwing, and this re-runs every frame the field is on
		// screen, so a name-style value would otherwise throw and catch
		// an exception ~60 times a second. (Actor Value IDs are 0-163,
		// so a leading digit is a safe gate, none are negative.)
		int unusedID = 0;
		bool valid = ActorValues::TryResolve(text, unusedID);
		if (!valid && std::isdigit(static_cast<unsigned char>(text.front())) != 0) {
			try {
				std::size_t consumed = 0;
				const int parsed = std::stoi(text, &consumed);
				valid = (consumed == text.size()) && parsed >= 0 && parsed <= 163;
			} catch (const std::exception&) {
				valid = false;
			}
		}

		if (valid) {
			ImGui::TextColored(kValidColor, "OK");
		} else {
			ImGui::TextColored(kInvalidColor, "not found");
		}
	}
}
