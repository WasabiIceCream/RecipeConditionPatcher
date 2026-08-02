#pragma once

namespace RPP::UI
{
	// Registers our menu section with SKSE Menu Framework, if it's
	// installed. Safe to call even if it isn't - IsInstalled() short-circuits.
	void Register();
}
