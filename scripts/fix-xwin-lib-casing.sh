#!/usr/bin/env bash
# One-time fixup for building this project's Linux cross-compile setup.
#
# xwin generates UPPERCASE/lowercase symlink variants for each system lib
# (e.g. KERNEL32.lib, kernel32.lib) but not the specific "Titlecase name,
# lowercase .lib extension" pattern CommonLibSSE's own CMakeLists.txt
# requests for a handful of libraries (Advapi32.lib, Dbghelp.lib, Ole32.lib,
# Version.lib). On Linux's case-sensitive filesystem, lld-link fails to find
# these with "No such file or directory" even though the file exists under
# a different casing.
#
# Run this once after `xwin splat` (and again any time you re-splat/update
# your sysroot). Safe to re-run - it skips names that already exist.
#
# Usage: XWIN_SYSROOT=~/xwin-out ./scripts/fix-xwin-lib-casing.sh
set -euo pipefail

SYSROOT="${XWIN_SYSROOT:-$HOME/xwin-out}"
LIBDIR="$SYSROOT/Windows Kits/10/Lib/10.0.26100/um/x64"

if [ ! -d "$LIBDIR" ]; then
	echo "error: $LIBDIR not found." >&2
	echo "Check XWIN_SYSROOT, and confirm you splat'd with --use-winsysroot-style" >&2
	echo "(the SDK version number in the path, 10.0.26100, may also differ - " >&2
	echo "check 'find \"\$XWIN_SYSROOT/Windows Kits/10/Lib\" -maxdepth 1' if this fails)." >&2
	exit 1
fi

cd "$LIBDIR"

# The known set CommonLibSSE's CMakeLists.txt links with this exact casing.
needed=(Advapi32.lib Dbghelp.lib Ole32.lib Version.lib)

for name in "${needed[@]}"; do
	if [ -e "$name" ]; then
		echo "OK (already exists): $name"
		continue
	fi
	real=$(ls | grep -i "^${name}\$" | head -1 || true)
	if [ -n "$real" ]; then
		ln -s "$real" "$name"
		echo "linked: $name -> $real"
	else
		echo "WARNING: no case-insensitive match found for $name" >&2
	fi
done
