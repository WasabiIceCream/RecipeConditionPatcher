# RecipeConditionPatcher

[![License: MIT](https://img.shields.io/github/license/WasabiIceCream/RecipeConditionPatcher)](LICENSE)

Automatically patches every crafting recipe (Constructible Object record)
in your game with whatever conditions you want. The included default
configuration adds matching perk requirements based on a recipe's
materials. See [Advanced use](#advanced-use) for everything else it can
do (quest progress, item counts, and hundreds of other things Skyrim can
check).

## Requirements

- Skyrim Special Edition, Anniversary Edition, or Skyrim VR
- [Skyrim Script Extender (SKSE64)](https://www.nexusmods.com/skyrimspecialedition/mods/30379) (or SKSE VR for the VR version)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
  (the SE/AE version, or the VR Address Library if you're on Skyrim VR)
- *Optional:* [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352),
  for creating and editing config files with the in-game settings menu (described below).

## Installation

Drop these into your `Data` folder (or install using your favorite mod manager):

```
Data/
  SKSE/
    Plugins/
      zz_RecipeConditionPatcher.dll
      RecipeConditionPatcher.json
```

## What it does by default

The included configuration adds the matching perk requirement to every crafting recipe
that uses one of the mapped materials (e.g. Ebony Ingot needs Ebony Smithing).
See [the full default list](#default-mappings) for specifics.

I basically got annoyed by many mod-added weapons and armors not having the appropriate crafting requirements.
So, at the very least, if they require a material like Dwarven Ingots, this mod can add the Dwarven Smithing requirement to it as well.
However, it won't fix anything if it doesn't already require the material.
For example, if a mod-added armor is named "Daedric Cuirass OMG", and has 100 Armor Rating, or something,
this mod won't do anything to fix it if the crafting recipe is 3 leather strips.
I don't think there's any 100% reliable way to catch stuff like that, other than hounding mod authors to fix their recipes...
We can still use this mod to edit specific recipes, and add the appropriate conditions at the very least.

## Using the in-game menu

If you have SKSE Menu Framework installed, open it in-game with its
hotkey (default `Page Up`) and look for the **Recipe Condition Patcher**
section: a **Settings** tab and a **Mappings** tab. See
[In-game menu, in detail](#in-game-menu-in-detail) for what each one does.

Don't have SKSE Menu Framework? You can either edit the included config file, or create your own from scratch.
See the [Configuring by hand](#configuring-by-hand) section below.

## Advanced use

Two things you can do beyond the built-in, perk-based defaults:

- **Target one specific recipe.** Force a condition onto one specific recipe,
  or exclude a recipe from being touched by this mod at all.
  Use the Mappings tab's Recipe Overrides section in the editor,
  or check the [Recipe overrides](#recipe-overrides) section for the JSON.
- **Use any condition function.** Item counts, quest state,
  global variables, and hundreds of other things Skyrim can check are all
  supported, not just perks. A curated list of recommended functions covers
  the common cases. See the [Condition functions](#condition-functions) section.

---

# Technical details

Everything below is for people modifying, building, or deeply configuring this mod.

## Configuring by hand

Edit `Data/SKSE/Plugins/RecipeConditionPatcher.json`:

```json
{
  "mappings": [
    { "material": "IngotOrichalcum", "perk": "OrcishSmithing", "comment": "optional" },
    { "material": "0x5AD99~Skyrim.esm", "function": "HasPerk", "param1": "0xCB401~Skyrim.esm", "comment": "same mapping as above, fully spelled out" }
  ]
}
```

### Adding your own config as a separate mod (`*_RCP.json`)

If you're a mod author who wants to ship your own `mappings`/`recipeOverrides` alongside your own mod, don't edit the main `RecipeConditionPatcher.json` (any changes would just completely overwrite each other).

Instead, drop a file ending in `_RCP.json` anywhere in `Data/SKSE/Plugins/`
(e.g. `Data/SKSE/Plugins/MyCoolMod_RCP.json`). Every file matching that
pattern is picked up automatically and merged in alongside the main
config:

```json
{
  "mappings": [
    { "material": "MyModsCoolIngot", "perk": "SteelSmithing" }
  ],
  "recipeOverrides": [
    { "recipe": "MyModsSpecialRecipe", "conditions": [{ "function": "HasPerk", "param1": "MyModsPerk" }] }
  ]
}
```

Only `mappings` and `recipeOverrides` are read from these files.
`enabled`/`existingPerkMode`/`logLevel` keys are ignored; only the main config's copies of these settings take effect.

You don't have to write these by hand: the in-game **Mappings** tab has an
**Editing** dropdown listing the main config plus every `*_RCP.json` it
found, so you can open and edit any of them directly, and a **Create New**
button that creates a new one (type `MyCoolMod`, get `Data/SKSE/Plugins/MyCoolMod_RCP.json`).

**Order and priority when the same recipe is targeted more than once**:
`mappings` from every source (the main config and every external file) always combine together.
`recipeOverrides` target one specific recipe directly, and multiple sources can conflict over it.

There's a deterministic processing order: the main config's entries
first, then each external `*_RCP.json` file's entries in alphabetical
order by filename (so `a_RCP.json` is processed, and can be superseded,
before `b_RCP.json`). When more than one entry targets the *same* recipe,
each entry's own `mode` decides how it combines with whatever came before
it for that recipe in this order:
- **Add to Existing.** Appends this entry's conditions onto whatever
  earlier entries already established for this recipe.
- **Replace Existing.** Discards everything established so far for this
  recipe (including whether an earlier entry wanted to preserve the
  recipe's own vanilla/other-mod conditions) and starts fresh from this
  entry alone.
- **`exclude: true`.** Also discards everything so far and excludes the
  recipe, but a *later* entry in the order with real conditions still
  un-excludes it and starts fresh, since later always wins.

Each mapping row has:
- **`material`.** The material that triggers this condition. Accepts an
  EditorID or a `FormID~PluginName` pair (see the identifier format note
  below either way).
- **`function`.** The condition function name (e.g. `"HasPerk"`,
  `"GetItemCount"`, `"GetGlobalValue"`) or a raw numeric FunctionID as a
  string (e.g. `"448"`). Defaults to `"HasPerk"` if omitted.
- **`param1`** / **`param2`.** Each is an identifier, a plain number, or
  omitted, depending on what the function expects (see
  [Condition functions](#condition-functions)). `"perk"` is accepted as a
  shorthand alias for `param1` for the common `HasPerk` case. One
  exception: `GetActorValue`'s `param1` also accepts an Actor Value name
  (e.g. `"Smithing"`) instead of just its numeric ID. See its row in
  [Condition functions](#condition-functions).
- **`operator`.** `"=="`, `"!="`, `">"`, `">="`, `"<"`, or `"<="`. Defaults
  to `"=="`.
- **`value`.** The number to compare the function's result against
  (defaults to `"1"`). Also accepts `"true"`/`"false"` directly (either
  case), or an identifier for a Global variable.
- **`runOn`.** Advanced, not exposed in the in-game editor. Defaults to
  `0` (Subject, since crafting is always done by the player).
  Only change this in the JSON if you're doing something interesting...
- **`logic`.** `"AND"` or `"OR"` against whichever condition immediately
  follows it on the same recipe. Defaults to `"AND"`.

Identifier format, for `material`/`param1`/`param2`/`value`-as-Global:

- A Creation Kit **EditorID** (e.g. `"IngotEbony"`).
- A **`FormID~PluginName`** pair (e.g. `"0x5AD99~Skyrim.esm"`).
  **Leading zeros aren't needed.** `"0x5AD99"` and `"0x0005AD99"` are the same.
- Unresolvable entries (mod not installed, typo'd ID, unrecognized function
  name) are skipped with a warning in the log, and they will never crash the game.

## Condition functions

Skyrim has roughly 750 CTDA condition functions, but only a handful of
them actually make sense on a crafting recipe.
`src/RecommendedConditionFunctions.h` is a curated subset of
the ones that do, and it's what drives the Function field's autocomplete
in the in-game editor (also shown as a reference list right above the
Mappings section there). Typing an exact function name outside this list still
works. It just won't be suggested.

| Function | What it needs |
|---|---|
| `GetActorValue` | `param1` = an Actor Value ID (0-163) or name (e.g. `Smithing`; the in-game editor's autocomplete offers the full list, sourced from [ck.uesp.net](https://ck.uesp.net/wiki/ActorValueInfo_Script#Actor_Value_IDs)), returns that Actor Value as a float |
| `GetCurrentTime` | No params, returns the current in-game time as a decimal (e.g. 4:30am = `4.5`, 7:45pm = `19.75`) |
| `GetFactionRank` | `param1` = faction (EditorID or FormID), `value` = rank number |
| `GetGlobalValue` | `param1` = a Global variable (EditorID or FormID), returns its value (typically a float), `value` = number or another Global to compare against |
| `GetItemCount` | `param1` = a MISC/ARMO/WEAP/INGR/ALCH/BOOK/KEYM record (EditorID or FormID), `value` = quantity in the player's inventory to compare against |
| `GetLevel` | No params, returns the player character's level |
| `GetPCInFaction` | `param1` = faction (EditorID or FormID), `value` = true or false |
| `GetPCIsRace` | `param1` = race (EditorID or FormID), `value` = true or false |
| `GetPCIsSex` | `param1` = 0 for Male, 1 for Female, `value` = true or false |
| `GetQuestRunning` | `param1` = quest (EditorID or FormID), `value` = true or false |
| `GetRandomPercent` | No params, returns a whole number 0-99. For an N% chance, use `<` with `value` = N (e.g. `value` = 1 gives a 1% chance) |
| `GetStage` | `param1` = quest (EditorID or FormID), `value` = stage number |
| `GetStageDone` | `param1` = quest (EditorID or FormID), `param2` = stage number, `value` = true or false |
| `HasPerk` | `param1` = perk (EditorID or FormID), `value` = true or false |
| `HasSpell` | `param1` = spell (EditorID or FormID), `value` = true or false |

For any function not on this list, cross-reference [ck.uesp.net](https://ck.uesp.net/wiki/Condition_Functions),
or inspect a real record using that function in SSEEdit.
Getting any parameter wrong doesn't crash the game.
It produces a condition that silently evaluates to something other than what you intended.

## Default mappings

The `dist_data`/shipped `RecipeConditionPatcher.json` comes with a default
set of mappings, all using `HasPerk` and identified by EditorID.

The full default set (all use the `HasPerk` function):

| Material (EditorID) | Perk (EditorID) | Content |
|---|---|---|
| `IngotSteel` | `SteelSmithing` | Base game |
| `ingotSilver` | `SteelSmithing` | Base game |
| `IngotDwarven` | `DwarvenSmithing` | Base game |
| `IngotQuicksilver` | `ElvenSmithing` | Base game |
| `IngotIMoonstone` | `ElvenSmithing` | Base game |
| `IngotOrichalcum` | `OrcishSmithing` | Base game |
| `IngotMalachite` | `GlassSmithing` | Base game |
| `IngotEbony` | `EbonySmithing` | Base game |
| `DaedraHeart` | `DaedricSmithing` | Base game |
| `IngotCorundum` | `AdvancedArmors` | Base game |
| `DragonBone` | `DragonArmor` | Base game |
| `DragonScales` | `DragonArmor` | Base game |
| `BoneMeal` | `SteelSmithing` | Dragonborn (Bonemold armor) |
| `DLC2ChitinPlate` | `ElvenSmithing` | Dragonborn (Chitin armor) |
| `DLC2NetchLeather` | `ElvenSmithing` | Dragonborn |
| `DLC2OreStalhrim` | `EbonySmithing` | Dragonborn |
| `ccBGSSSE025_IngotAmber` | `GlassSmithing` | Creation Club ("Saints & Seducers") |
| `ccBGSSSE025_IngotMadness` | `EbonySmithing` | Creation Club ("Saints & Seducers") |

## Recipe overrides

The material-based scanning above applies to every recipe using a mapped material,
but you may want to control exactly one recipe directly.
Add entries under `recipeOverrides` in any settings JSON,
or just use the in-game **Mappings** tab's Recipe Overrides section:

```json
{
  "recipeOverrides": [
    {
      "recipe": "0xDB8BF~Skyrim.esm",
      "conditions": [ { "function": "HasPerk", "param1": "DragonArmor" } ],
      "mode": 1,
      "comment": "RecipeWeaponDaedricBattleAxe"
    },
    { "recipe": "SomeModdedRecipeEditorID", "exclude": true }
  ]
}
```

Each entry does one of two things:
- **`conditions`.** An array of condition objects (same function/param1/param2/operator/value/runOn/logic fields as `mappings` rows).
- **`mode`.** Can be either `0` (Add to Existing, the default), or `1` (Replace Existing).
- **`exclude: true`.** Skip this one recipe completely: no material-based
  scanning, no forced conditions, nothing. Overrides `mode` and the global
  `existingPerkMode` setting for this entry. Note that a *later* entry in
  the processing order (see above) can still un-exclude the recipe. This
  isn't an unconditional veto; it's this entry's link in the chain.

## In-game menu, in detail

The mod has a live settings menu built with
[SKSE Menu Framework](https://github.com/QTR-Modding/SKSE-Menu-Framework-3),
an ImGui-based menu framework opened in-game via its own hotkey (default
`Page Up`, configurable in the framework's own settings).
It's a **soft dependency**: `extern/SKSEMenuFramework.h` is a single
vendored header that resolves every call via `GetProcAddress` against
`Data/SKSE/Plugins/SKSEMenuFramework.dll` at runtime. There's no build-time
link to it and no submodule for it. If the player doesn't have SKSE Menu
Framework installed, `SKSEMenuFramework::IsInstalled()` returns false, menu
registration is skipped, and the rest of the mod works exactly the same off
of whatever configuration files exist for it.

The section (registered as "Recipe Condition Patcher") has two tabs.

**Settings tab:**
- **Enable Patcher.** Master on/off switch.
- **Existing Conditions.** A 3-way mode for recipes that already have at least one condition:
  - **Add to Existing** (default). Layer our conditions on top of whatever's already there.
  - **Replace Existing.** Strip *all* of a recipe's current conditions (if any),
    then re-add conditions based only on this plugin's configured mappings.
    If a recipe's original condition doesn't correspond to any material this plugin tracks,
    the recipe can end up **"weaker"** than before (or with no condition at all).
    This mode fully trusts the mapping tables. It doesn't merge with what was already there.
  - **Skip Recipe.** Leave it completely untouched.
- A **Log Verbosity** dropdown
- An **Apply Now** button

**Mappings tab.** An add/edit/delete editor for `mappings` and
`recipeOverrides`, so you don't have to hand-edit JSON for easy edits.
- If any [`*_RCP.json` external config files](#adding-your-own-config-as-a-separate-mod-_rcpjson)
  are present, they're selectable from the **Editing** dropdown at the top,
  alongside the main config. This tab edits whichever one is selected.
  **Save As...** copies the currently selected file to a new `*_RCP.json`.
  **Create New** creates a new `*_RCP.json` from scratch.
  Switching files discards any unsaved edits, so be sure to save your changes.
- A collapsible **"Which Function should I use?"** reference at the top,
  listing every function from [Condition functions](#condition-functions)
  with its usage hint, so you don't have to leave the menu to check what a
  function's fields mean.
- Autocomplete on Material/Recipe/Param/Function fields. Start typing and
  matching candidates from your actual game/mod data (or, for Function,
  the curated list above) appear in a small list below the field; click
  one to fill it in. A few fields get extra help beyond plain autocomplete:
  Param 1 suggests only the record type the selected function actually
  wants: perks for `HasPerk`, factions for `GetPCInFaction`, etc.
  This only narrows what's *suggested*. You can still enter whatever you want.
- A live green **OK** / red **not found** indicator next to Material/Recipe
  and Param 1 fields updates as you type, showing whether that identifier resolves to a real form.
- Add multiple rows with the same Material (or the same Recipe, in
  Overrides) to give it more than one condition.
- Recipe Overrides rows also have an **Exclude** checkbox, a **Mode**
  dropdown (Add to Existing / Replace Existing, independent of the
  Settings tab's global Existing Conditions setting), and a **Comment**.
  All three are really one value per recipe, not per condition, so
  changing any of them on one row updates every other row for that same
  recipe automatically.
- **+ Add Mapping** / **+ Add Override** buttons, and a **Remove** button per entry.
- **Save & Apply.** Writes both sections back into the file, and immediately re-runs the patcher.
  **Reload From Disk.** Discards any unsaved edits and re-reads the file
  (if you hand-edited it while the game is running, or wanted to undo unsaved edits).

### ⚠️ "Apply Now" and turning things off mid-session

`BGSConstructibleObject` condition lists live in memory once the game's
data is loaded. **Replace Existing** mode genuinely does remove
conditions live (it's a real `delete`, not a no-op), so pressing
**Apply Now** after switching to Replace mode will actually strip and
rebuild a recipe's conditions on the spot.

The thing that *can't* happen live is **`Add to Existing` mode
retroactively un-adding something**: a condition it added under one config
stays there even if you then edit the JSON to remove that material's mapping and press Apply Now.
Apply Now only re-scans and adds/replaces based on your *current* config, it doesn't
remember or undo what a *previous* pass did. Restarting Skyrim (which
reloads all game data from scratch and re-runs the patcher cleanly with
whatever config is active at that point) is the only way to get a fully
clean result after changing the mapping file mid-session.

## How it works

1. On the SKSE `kDataLoaded` message (fired once, after every plugin's
   records are in memory), it loads `Data/SKSE/Plugins/RecipeConditionPatcher.json`
   (and any `*_RCP.json` files; see below) for its material-to-condition
   mappings.
2. Each mapping's material identifier is resolved to a live form via
   `TESForm::LookupByEditorID` or `TESDataHandler::LookupForm`.
3. It iterates `TESDataHandler::GetFormArray<RE::BGSConstructibleObject>()`,
   and for each recipe, walks its `requiredItems` (a `TESContainer`).
4. If a required item matches a mapped material, and the recipe doesn't
   already have that exact condition, it prepends a new `TESConditionItem`
   (built from the mapping's `function`/`param1`/`param2`/`operator`/
   `value`/`runOn`) to the recipe's condition list. Existing conditions are
   left untouched and ANDed with the new one by default, so previous
   requirements still apply (see "Existing Conditions" mode for other
   options).

It's built directly against [`alandtse/CommonLibSSE-NG`](https://github.com/alandtse/CommonLibSSE-NG)
(the `ng` branch), which compiles to a single DLL that works across SE, AE, and VR,
detecting which one it's running under at load time rather than needing separate
builds. Materials, perks, recipes, and any other form a condition references are
identified by either Creation Kit EditorID (via `TESForm::LookupByEditorID<T>()`)
or a load-order-safe `FormID~PluginName` pair (via `TESDataHandler::LookupForm<T>()`).

## Building

Two supported paths: a normal build on Windows with Visual Studio, or a
**cross-compile from Linux** using clang-cl targeting the real MSVC ABI.

### Option A: Windows + Visual Studio 2022

**Prerequisites**
- Visual Studio 2022 with the "Desktop development with C++" workload
- [vcpkg](https://github.com/microsoft/vcpkg), with the `VCPKG_ROOT`
  environment variable set to your vcpkg checkout
- CMake ≥ 3.21 (bundled with recent VS, or install separately)
- Git

**Steps**
```bat
git clone <this project> RecipeConditionPatcher
cd RecipeConditionPatcher

:: configure + build
:: (CommonLibSSE-NG is cloned automatically into extern/CommonLibSSE on first
:: configure if it isn't already there - no manual submodule step needed.
:: extern/SKSEMenuFramework.h is already vendored directly in this repo.
:: vcpkg also pulls in DirectXTK here (a CommonLibSSE-NG dependency); on a
:: real Windows build with the Windows SDK installed, its shader compiler
:: build step just works with no extra setup - the workaround described in
:: the Linux section below is only needed when cross-compiling.)
cmake --preset vs2022
cmake --build --preset vs2022-release
```

The compiled DLL will be under
`build/vs2022/Release/zz_RecipeConditionPatcher.dll`. See "Load order"
below for why the filename has that prefix.

To auto-copy the DLL and default config straight into your mod manager's
Data folder on every build, reconfigure with:

```bat
cmake --preset vs2022 -DMOD_OUTPUT_FOLDER="C:/path/to/YourMod/Data"
```

### Option B: cross-compile from Linux

This uses `clang-cl` (Clang's MSVC-compatible driver mode) and `lld-link`
targeting `x86_64-pc-windows-msvc` directly, with the real Windows SDK/CRT
obtained via [`xwin`](https://github.com/Jake-Shadle/xwin). This is *not*
the same as MinGW-w64. MinGW uses the Itanium C++ ABI (same as native Linux
GCC/Clang), which is binary-incompatible with the real game executable's
MSVC-compiled classes and will build a plugin that crashes instantly.
clang-cl/lld-link genuinely target the MSVC ABI, which is what makes this
work at all.

**One-time environment setup** (package manager commands below are for
Arch/pacman; substitute your distro's equivalent for `clang`, `lld`,
`llvm`, `cmake`, `ninja`, `rust`, and `git`)

```bash
# Toolchain
sudo pacman -S clang lld llvm cmake ninja rust git

# Wine + llvm-mingw: CommonLibSSE-NG depends on DirectXTK, and DirectXTK's
# own build compiles its shaders via a Windows batch script that needs a
# real fxc.exe. There's no Linux-native HLSL compiler that produces the
# same classic DXBC bytecode, so this project vendors a patched build of
# Mozilla's fxc2 (extern/fxc2/) that loads the real d3dcompiler_47.dll
# directly and runs under Wine instead. llvm-mingw provides the
# <getopt.h> header fxc2.cpp needs (not part of the MSVC/clang-cl
# sysroot); CMake builds fxc2.exe from it automatically on first
# configure. None of this applies on a native Windows build (Option A
# above), which already has a real fxc.exe via the Windows SDK.
sudo pacman -S wine llvm-mingw

# xwin - if the current release needs a newer Rust edition than your
# distro's rustc provides, pin an older version:
cargo install xwin --version 0.6.5 --locked
echo 'export PATH="$HOME/.cargo/bin:$PATH"' >> ~/.bashrc
export PATH="$HOME/.cargo/bin:$PATH"

# Download the Windows SDK + MSVC CRT. Both flags below are REQUIRED:
#  - --use-winsysroot-style: without it, /winsysroot can't find anything
#    (xwin's default output layout doesn't match what it expects)
#  - --preserve-ms-arch-notation: without it, folders are named "x86_64"
#    instead of "x64", which lld-link's auto-search won't find either
mkdir -p ~/xwin-cache ~/xwin-out
xwin --accept-license --cache-dir ~/xwin-cache splat \
  --output ~/xwin-out --include-debug-libs \
  --use-winsysroot-style --preserve-ms-arch-notation

# vcpkg
git clone https://github.com/microsoft/vcpkg $HOME/.local/share/vcpkg
echo 'export VCPKG_ROOT="$HOME/.local/share/vcpkg"' >> ~/.bashrc
export VCPKG_ROOT="$HOME/.local/share/vcpkg"

# Fix a gap in xwin's generated case-variant symlinks: CommonLibSSE-NG's own
# build links a few system libs (Advapi32.lib, Dbghelp.lib, Ole32.lib,
# Version.lib) with a specific casing xwin doesn't generate by default.
# Only needed if the link step complains about one of these four with
# "No such file or directory".
XWIN_SYSROOT=~/xwin-out ./scripts/fix-xwin-lib-casing.sh
```

If your `~/xwin-out` path or SDK version differs, edit
`cmake/toolchain-clangcl-windows.cmake` (the `XWIN_SYSROOT` line) and
`scripts/fix-xwin-lib-casing.sh` to match.

**Build**

```bash
cd RecipeConditionPatcher
cmake --preset linux-clangcl
cmake --build --preset linux-clangcl-release
```

No `git init`/submodule step needed even if you got this project as a zip
rather than `git clone`-ing it. CommonLibSSE-NG (including its own OpenVR
submodule) is cloned into `extern/CommonLibSSE` automatically by the first
`cmake --preset` call if it isn't already there, and `extern/fxc2/fxc2.exe`
gets built the same way (see `CMakeLists.txt`).

The compiled DLL will be at `build/linux-clangcl/zz_RecipeConditionPatcher.dll`
(see "Load order" below for why the filename has that prefix). Sanity-check
it's a real Windows binary:
```bash
file build/linux-clangcl/zz_RecipeConditionPatcher.dll
# expect: PE32+ executable (DLL) ... x86-64, for MS Windows
```

## Load order

SKSE has no dependency/ordering API for native plugins.
Plugin execution order (specifically, the order each plugin's
`kDataLoaded` handler runs in) follows alphabetical DLL filename within
`Data/SKSE/Plugins`.

This matters specifically for mods like [SkyPatcher](https://www.nexusmods.com/skyrimspecialedition/mods/106659), and [Crafting Recipe Distributor](https://www.nexusmods.com/skyrimspecialedition/mods/52276) (CRD).

SkyPatcher's COBJ patcher can add, remove, or change a recipe's required materials
(though, as of now, not its conditions, which is partially why this mod exists).

Crafting Recipe Distributor *generates* brand-new smelting and tempering recipes at
runtime rather than editing existing ones. Those generated recipes are
real COBJ records like any other, so this plugin will pick them up and condition them normally.

Running before either of these mods would mean seeing the pre-generated and pre-patched recipes.
`CMakeLists.txt`'s `OUTPUT_FILENAME_PREFIX` (default `zz_`)
pushes the compiled filename late enough to run after both SkyPatcher
and Crafting Recipe Distributor, and basically any other SKSE plugin.
This is a filename convention, not a real guarantee. Override with
`-DOUTPUT_FILENAME_PREFIX=""` at configure time to disable it, or set it
to something else if you need different ordering.

## Notes / things worth knowing

- **SE / AE / VR**: `ENABLE_SKYRIM_SE`, `ENABLE_SKYRIM_AE`, and `ENABLE_SKYRIM_VR`
  (all on by default in `CMakeLists.txt`) build a single DLL that detects which
  runtime it's running under and works on all three, rather than needing a
  separate build per runtime. Make sure you have the matching Address Library
  (SE/AE, or the VR one) installed alongside SKSE itself.
- **Performance**: this only runs once, at game data load, and does a single
  pass over all recipes. There's no per-frame or per-craft cost.
  Testing on a load order with ~80,000 COBJ records took only 0.451 seconds (max).
- **Compatibility**: in the default "Add to Existing" mode, this only
  *adds* conditions and never removes existing ones, so it should coexist
  fine with other recipe-editing mods. "Replace Existing" mode is a
  deliberate exception to that.
- **Log detail**: at the default "Info" log level, every actual change is
  logged individually: the recipe's EditorID, FormID, and the plugin it
  originally came from, plus exactly which condition was added, removed, or excluded, and why.
  Recipes created at runtime rather than loaded from a plugin (as [Crafting Recipe Distributor](https://www.nexusmods.com/skyrimspecialedition/mods/52276)
  does) have no EditorID or source file, and are logged as
  `(no EditorID) (0xFF0…) [dynamic]`. That's expected, not an error: they're
  patched like any other recipe, they just can't be named.
  Set Log Verbosity to "Warnings Only" or "Errors Only" in the Settings tab if you
  only want the final summary line and don't need the full per-recipe detail.
