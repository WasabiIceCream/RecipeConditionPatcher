#pragma once

#include "SKSEMenuFramework.h"

namespace RPP::UI::Style
{
	namespace ImGui = ImGuiMCP;

	// One palette shared by every tab, so the mod reads as a single UI
	// rather than two independently-styled pages.
	//
	// The opaque background matters more than it looks: the framework's
	// default popup/child backgrounds are translucent, so dropdown lists
	// and the autocomplete suggestions let the page behind bleed through
	// and become genuinely hard to read.
	inline constexpr ImGui::ImVec4 kOpaqueBg{ 0.10f, 0.10f, 0.12f, 1.0f };

	// Slightly brighter than the framework defaults - against this dark
	// background the stock body and "disabled" greys are dim enough to be
	// a real readability problem, especially for the explanatory lines
	// that sit under most controls.
	inline constexpr ImGui::ImVec4 kBodyText{ 0.88f, 0.88f, 0.90f, 1.0f };
	inline constexpr ImGui::ImVec4 kMutedText{ 0.66f, 0.68f, 0.74f, 1.0f };

	// Section headings and inline emphasis.
	inline constexpr ImGui::ImVec4 kAccentText{ 0.55f, 0.78f, 1.00f, 1.0f };
	// Used for the "this is destructive / read this" lines.
	inline constexpr ImGui::ImVec4 kWarnText{ 0.95f, 0.78f, 0.35f, 1.0f };

	inline constexpr ImGui::ImVec4 kValidText{ 0.40f, 0.85f, 0.40f, 1.0f };
	inline constexpr ImGui::ImVec4 kInvalidText{ 0.90f, 0.35f, 0.35f, 1.0f };

	// Call once at the top of a tab's Render(), and Pop() at the very end.
	inline void Push()
	{
		ImGui::PushStyleColor(ImGui::ImGuiCol_PopupBg, kOpaqueBg);
		ImGui::PushStyleColor(ImGui::ImGuiCol_ChildBg, kOpaqueBg);
		ImGui::PushStyleColor(ImGui::ImGuiCol_Text, kBodyText);
		ImGui::PushStyleColor(ImGui::ImGuiCol_TextDisabled, kMutedText);
	}

	inline void Pop()
	{
		ImGui::PopStyleColor(4);
	}

	// A wrapped, muted explanatory line - the standard "what does this
	// control do" text under a widget.
	inline void Hint(const char* a_text)
	{
		ImGui::PushStyleColor(ImGui::ImGuiCol_Text, kMutedText);
		ImGui::TextWrapped("%s", a_text);
		ImGui::PopStyleColor();
	}

	// Same, but for a line the reader really should not skim past.
	inline void WarnHint(const char* a_text)
	{
		ImGui::PushStyleColor(ImGui::ImGuiCol_Text, kWarnText);
		ImGui::TextWrapped("%s", a_text);
		ImGui::PopStyleColor();
	}

	// A section heading, accented so the eye can find the structure of a
	// long page quickly.
	inline void Heading(const char* a_text)
	{
		ImGui::PushStyleColor(ImGui::ImGuiCol_Text, kAccentText);
		ImGui::SeparatorText(a_text);
		ImGui::PopStyleColor();
	}
}
