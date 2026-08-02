# Custom vcpkg triplet so dependencies (nlohmann-json, spdlog, fmt,
# rsm-binary-io) also get built with clang-cl targeting the MSVC ABI,
# instead of vcpkg's default x64-windows triplet which assumes real MSVC
# is on PATH.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

# Deliberately NOT setting VCPKG_CMAKE_SYSTEM_NAME to "Windows" here, even
# though that's exactly what we're targeting: vcpkg's own convention
# (scripts/cmake/vcpkg_common_definitions.cmake) is that
# VCPKG_TARGET_IS_WINDOWS is only set ON when VCPKG_CMAKE_SYSTEM_NAME is
# UNDEFINED or EMPTY - "Windows" as a literal value matches none of its
# elseif branches (WindowsStore/Darwin/Linux/MinGW/...), so setting it
# explicitly silently leaves VCPKG_TARGET_IS_WINDOWS unset for this
# triplet. That broke two things before this was found: directxtk's own
# "supports": "windows & ..." expression reading as unsupported (worked
# around with --allow-unsupported, no longer needed once this is fixed),
# and directxmath's portfile conditionally downloading a non-Windows
# fallback sal.h that shadows the real Windows SDK one and silently
# breaks any TU that reaches <execution> (missing concurrencysal.h
# content, e.g. _Guarded_by_). Leaving this unset costs nothing: the
# actual Windows-targeting (PE output, MSVC ABI via clang-cl) is fully
# handled by VCPKG_CHAINLOAD_TOOLCHAIN_FILE below, which sets the real
# CMAKE_SYSTEM_NAME for each port's actual CMake configure/build - this
# variable is purely vcpkg's own triplet-classification bookkeeping.

# NOTE: this path is relative to THIS file's location. It assumes
# custom-triplets/ and cmake/ are sibling folders directly under the
# project root - if you move either folder, update this path to match.
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../cmake/toolchain-clangcl-windows.cmake")
