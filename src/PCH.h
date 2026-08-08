#pragma once

// MUST be defined before anything that might pull in <windows.h> (which
// both RE/Skyrim.h and extern/SKSEMenuFramework.h do transitively).
// Without this, windows.h's own min(a,b)/max(a,b) macros silently rewrite
// any std::min/std::max call in the entire project into garbage. This bit
// UIMappingsEditor.cpp the moment it used std::min, even though the macro
// pollution had actually been present since UI.cpp first included
// SKSEMenuFramework.h; it just hadn't been triggered by anything yet.
#define NOMINMAX

// CommonLibSSE-NG + bundled SKSE headers (alandtse fork)
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <nlohmann/json.hpp>
#include <spdlog/sinks/basic_file_sink.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std::literals;
