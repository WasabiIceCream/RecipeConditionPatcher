# Cross-compile toolchain for building this project on Linux using clang-cl
# targeting the real MSVC ABI, via a Windows SDK/CRT sysroot obtained with
# `xwin` (https://github.com/Jake-Shadle/xwin). See README.md's "Building on
# Linux" section for full setup steps.
#
# NOTE: assigning a bare command name ("clang-cl") directly to
# CMAKE_C_COMPILER is unreliable here - CMake's cross-compile toolchain-file
# processing doesn't consistently resolve it via PATH the way a normal
# (non-cross) configure does. find_program() sidesteps this: it searches
# PATH itself and hands CMake an already-resolved absolute path before
# EnableLanguage runs, so this stays portable across machines without
# hardcoding install locations.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

find_program(CMAKE_C_COMPILER NAMES clang-cl REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES clang-cl REQUIRED)
find_program(CMAKE_LINKER NAMES lld-link REQUIRED)

# Change this if your xwin sysroot lives somewhere else. Must have been
# generated with: xwin ... splat --use-winsysroot-style --preserve-ms-arch-notation
# (both flags are required - without --use-winsysroot-style, /winsysroot
# won't find anything; without --preserve-ms-arch-notation, lld-link looks
# for "lib/x64" but xwin's default output uses "lib/x86_64" and nothing
# resolves.)
set(XWIN_SYSROOT "$ENV{HOME}/xwin-out")

set(CMAKE_C_FLAGS_INIT   "--target=x86_64-pc-windows-msvc /winsysroot${XWIN_SYSROOT}")
set(CMAKE_CXX_FLAGS_INIT "--target=x86_64-pc-windows-msvc /winsysroot${XWIN_SYSROOT}")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "/winsysroot:${XWIN_SYSROOT}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "/winsysroot:${XWIN_SYSROOT}")
