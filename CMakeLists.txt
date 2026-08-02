cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# fxc2 (patched Mozilla fxc2, MPL 2.0) - HLSL shader compiler stand-in,
# Linux-host builds only.
#
# CommonLibSSE-NG (below) pulls in DirectXTK unconditionally, and
# DirectXTK's own build compiles its shaders via Src/Shaders/CompileShaders.cmd,
# which needs a real fxc.exe. On a native Windows build that's already on
# the machine via the Windows SDK/Visual Studio, so this whole block is
# skipped there. Gated on CMAKE_HOST_UNIX (reliable this early) rather than
# CMAKE_CROSSCOMPILING (not set until project() runs, and this has to
# finish before project() triggers vcpkg's manifest install, which is what
# actually builds DirectXTK). See extern/fxc2/fxc2.cpp's header comment and
# custom-ports/directxtk/ for the rest of how this gets used.
# ---------------------------------------------------------------------------
if(CMAKE_HOST_UNIX)
	set(FXC2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/fxc2")
	set(FXC2_EXE "${FXC2_DIR}/fxc2.exe")
	if(NOT EXISTS "${FXC2_EXE}")
		find_program(MINGW_CLANGXX
			NAMES x86_64-w64-mingw32-clang++
			PATHS /opt/llvm-mingw/bin
		)
		if(NOT MINGW_CLANGXX)
			message(FATAL_ERROR
				"Building against CommonLibSSE-NG on Linux needs a mingw-w64 "
				"clang++ to compile extern/fxc2/fxc2.cpp (a stand-in fxc.exe "
				"run under Wine - DirectXTK's own build needs a real HLSL "
				"shader compiler, which a plain clang-cl/xwin cross-compile "
				"has no equivalent for). Install llvm-mingw (e.g. 'sudo "
				"pacman -S llvm-mingw' on Arch) and reconfigure.")
		endif()
		message(STATUS "Building extern/fxc2/fxc2.exe with ${MINGW_CLANGXX}...")
		execute_process(
			COMMAND "${MINGW_CLANGXX}" -static fxc2.cpp -o fxc2.exe
			WORKING_DIRECTORY "${FXC2_DIR}"
			RESULT_VARIABLE _fxc2_build_result
		)
		if(NOT _fxc2_build_result EQUAL 0)
			message(FATAL_ERROR "Failed to build extern/fxc2/fxc2.exe (see output above).")
		endif()
	endif()
endif()

# ---------------------------------------------------------------------------
# Project
# ---------------------------------------------------------------------------
project(RecipeConditionPatcher VERSION 2.0.0 LANGUAGES CXX)

option(ENABLE_SKYRIM_SE "Build against the Skyrim SE runtime" ON)
option(ENABLE_SKYRIM_AE "Build against the Skyrim AE runtime" ON)
option(ENABLE_SKYRIM_VR "Build against the Skyrim VR runtime" ON)

# ---------------------------------------------------------------------------
# Version header
#
# Generates src/Version.h.in -> <build dir>/generated/Version.h, exposing
# the VERSION number from project() above as RPP_VERSION_MAJOR/MINOR/PATCH
# macros - see Plugin.cpp's PluginVersionData() call. This means the
# version only needs to be set in ONE place (the project() call above);
# bumping it there updates the compiled plugin's version automatically,
# no need to also edit Plugin.cpp by hand.
#
# NOTE: this does NOT reach vcpkg.json's own "version-string" field -
# vcpkg parses that file directly, independent of CMake, so there's no way
# for a CMake variable to flow into it. That field is really just vcpkg's
# own package-manifest bookkeeping (not something anything reads back out
# of at runtime), so it's fine for it to drift from the plugin's actual
# version; update it by hand on major releases if you want it to stay
# roughly in sync.
# ---------------------------------------------------------------------------
configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/src/Version.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h"
	@ONLY
)

# ---------------------------------------------------------------------------
# CommonLibSSE-NG (alandtse fork, ng branch)
#
# Swapped from powerof3/CommonLibSSE to get single-binary SE+AE+VR support
# (see ENABLE_SKYRIM_* above). See extern/fxc2/ and custom-ports/directxtk/
# for the DirectXTK shader-compiler workaround this pulls in as a
# consequence (NG depends on DirectXTK unconditionally).
#
# Fetched automatically into extern/CommonLibSSE if not already present.
# (Deliberately not a git submodule for the OUTER checkout: submodule
# linkage lives in git's index, which a plain zip/tarball distribution of
# this project can't carry, so a submodule-based setup silently does
# nothing on a fresh extraction instead of fetching anything. A plain
# clone-if-missing step has no such gap. --recurse-submodules IS needed
# on the clone itself though, since NG vendors OpenVR - extern/openvr -
# as a real git submodule internally.)
# ---------------------------------------------------------------------------
set(COMMONLIBSSE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE")
if(NOT EXISTS "${COMMONLIBSSE_DIR}/CMakeLists.txt")
	find_package(Git REQUIRED)
	message(STATUS "extern/CommonLibSSE not found - cloning alandtse/CommonLibSSE-NG (ng branch)...")
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" clone --branch ng --depth 1 --recurse-submodules --shallow-submodules
			https://github.com/alandtse/CommonLibSSE-NG "${COMMONLIBSSE_DIR}"
		RESULT_VARIABLE _clsse_clone_result
	)
	if(NOT _clsse_clone_result EQUAL 0)
		message(FATAL_ERROR
			"Failed to clone CommonLibSSE-NG automatically (check your network "
			"connection). You can also clone it manually:\n"
			"  git clone --branch ng --recurse-submodules https://github.com/alandtse/CommonLibSSE-NG ${COMMONLIBSSE_DIR}")
	endif()
endif()

# We only want the library, not NG's own unit test suite (which pulls in
# Catch2, a dependency we have no other use for and haven't declared).
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory("${COMMONLIBSSE_DIR}" CommonLibSSE)

# ---------------------------------------------------------------------------
# Output filename / load-order note
#
# SKSE has no first-class "load after X" dependency API for native plugins -
# confirmed by inspecting SKSE::PluginVersionData directly: it has version-
# independence flags and a compatible-versions list, but no dependency/
# ordering field of any kind. In practice, the order plugins load in (and
# therefore the order their kDataLoaded handlers run in, since listeners
# fire in registration order) is governed by alphabetical DLL filename
# within Data/SKSE/Plugins - the same mechanism SkyPatcher's own
# documentation describes for its own internal load-order tie-breaking.
#
# This matters because SkyPatcher's COBJ patcher can add/remove/change a
# recipe's required materials (though not its conditions) - exactly what
# ApplyPerkRequirementsToAllRecipes() scans to decide which conditions to
# add. If we ran before SkyPatcher, we'd see the pre-patch material list
# and potentially miss or misjudge recipes SkyPatcher was about to change.
#
# "RecipeConditionPatcher.dll" alphabetically precedes "SkyPatcher.dll"
# (R < S), so by default we'd load - and run - BEFORE it: the wrong order. The
# prefix below nudges the actual compiled filename late enough to run
# after SkyPatcher and, generally, after any other COBJ-editing patcher
# whose name doesn't also start this late in the alphabet. This is a
# filename convention, not a real dependency guarantee (there isn't one to
# have) - override with -DOUTPUT_FILENAME_PREFIX="" to disable, or set it
# to something else if you need different ordering.
# ---------------------------------------------------------------------------
set(OUTPUT_FILENAME_PREFIX "zz_" CACHE STRING
	"Prefix added to the compiled DLL's filename to influence SKSE's alphabetical plugin load order. Empty string to disable.")

# ---------------------------------------------------------------------------
# Dependencies from vcpkg (see vcpkg.json)
# ---------------------------------------------------------------------------
find_package(nlohmann_json CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Plugin sources
# ---------------------------------------------------------------------------
set(SOURCES
	src/PCH.h
	src/Plugin.cpp
	src/Version.h.in
	src/ConditionFunctionTable.h
	src/ActorValueTable.h
	src/RecommendedConditionFunctions.h
	src/Conditions.h
	src/Conditions.cpp
	src/ConfigFile.h
	src/ConfigFile.cpp
	src/EditorIDCache.h
	src/EditorIDCache.cpp
	src/Identifiers.h
	src/TriggerConfig.h
	src/TriggerConfig.cpp
	src/RecipeOverrides.h
	src/RecipeOverrides.cpp
	src/Settings.h
	src/Settings.cpp
	src/UI.h
	src/UIStyle.h
	src/UI.cpp
	src/UIMappingsEditor.h
	src/UIMappingsEditor.cpp
	src/RecipePatcher.h
	src/RecipePatcher.cpp
	extern/SKSEMenuFramework.h
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCES})

add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE src extern "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

if (MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

target_link_libraries(${PROJECT_NAME}
	PRIVATE
		CommonLibSSE::CommonLibSSE
		nlohmann_json::nlohmann_json
)

# Output as a .dll with no "lib" prefix, matching SKSE plugin conventions.
# See the OUTPUT_FILENAME_PREFIX note above the vcpkg find_package() calls -
# the internal plugin name (shown in logs) stays "RecipeConditionPatcher"
# regardless; only the file on disk gets the load-order prefix.
set_target_properties(${PROJECT_NAME} PROPERTIES
	PREFIX ""
	OUTPUT_NAME "${OUTPUT_FILENAME_PREFIX}${PROJECT_NAME}"
)

# ---------------------------------------------------------------------------
# Optional: copy the built DLL + default config straight into your MO2/game
# Data folder. Pass -DMOD_OUTPUT_FOLDER="C:/path/to/Data" to enable.
# ---------------------------------------------------------------------------
if (DEFINED MOD_OUTPUT_FOLDER)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${MOD_OUTPUT_FOLDER}/SKSE/Plugins"
		COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${PROJECT_NAME}>" "${MOD_OUTPUT_FOLDER}/SKSE/Plugins/"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/dist_data"
			"${MOD_OUTPUT_FOLDER}"
	)
endif()
