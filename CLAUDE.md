# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Silinium is a PS3 module (PRX/SPRX) for Call of Duty: Black Ops 1 (game version 1.13),
multiplayer and zombies. It is a fork of T5GSCLoader by iMCSx. It replaces game scripts (GSC/CSC)
with files from `/dev_hdd0/tmp/Silinium/{mp,zm}/<mod>`, patches the script
checksums so modded hosts can play with others, removes the dvar protections, adds
custom GSC functions and contains a host menu (MP only). Written in C99 against
the PS3 SDK. There is no test suite; behavior can only be verified on a console.

## Code Style

See @docs/code-style.md for code style and convention rules (applies on every machine).

## Commands

`SCE_PS3_ROOT` must point to the PS3 SDK.

```bash
./build.sh [debug|release|clean]   # build; output bin/<config>/Silinium.sprx
./build.sh release hen             # additionally sign Silinium_hen.sprx with scetool
./build.sh format                  # clang-format -i over src/ (requires version 20, CLANG_FORMAT overrides the command)
make release / make format         # same via the SDK makefiles, if available
```

CI (`.github/workflows/format.yml`) only checks formatting with clang-format 20
over `src/`; it does not build, because the PS3 SDK cannot be redistributed.
Build and run `./build.sh format` locally before pushing. Commits that only
reformat go into `.git-blame-ignore-revs`.

## Architecture

All sources are in `src/`.

- `main.c`: module entry. Starts a launcher thread that waits until the eboot's
  modules are loaded (so import OPDs are resolved), then calls `init_game()`.
- `t5.c` / `t5.h`: `init_game()` detects MP/ZM (`isMultiplayer`), sets the native
  offsets (`init_offsets()`), installs the core hooks (`init_hooks()`), applies
  the instruction patches (`apply_patches()`) and resolves the active mod path.
- `hooks.c`: the core hooks. `Scr_LoadRawFile_FastFile` serves replaced files from
  the mod folder, `Scr_GetChecksum` returns the stock checksums from
  `checksums.c`, `Scr_GetFunction` exposes the custom GSC functions from
  `scrfct.c` (e.g. `setMemory`).
- `host_menu.c`: host menu logic and its hooks (`PadGetData`, `ClientCommand`,
  `VM_Notify`, ...), installed by `host_menu_init()`. `host_menu_ui.c`: drawing
  and the menu options.
- `stats_file.c`: Save/Load Stats files (`/dev_hdd0/tmp/Silinium/stats/<name>.bin`).
  `notify.c`: PS3 notifications through webMAN, over the game's `sys_net` import stubs.
- `offsets.h`: all game addresses (`T5_<Name>` / `T5_<Name>_ZM`).
- `defines.h`: the `t5nd` / `t5nhd` / `t5ni` / `t5nmi` declaration macros and game
  structs.
- `cshook.c`: iMCSx's hook library. `utils.c`: `sys_dbg_process_write`,
  `apply_instruction_patch` and helpers. `printf.c`: own `printf`, the module is
  linked with `-nodefaultlibs`.

`tmp/` holds an example mod for MP and ZM; `docs/` holds this style guide.
`tools/stats-viewer.html` is a standalone browser viewer for the Save Stats files;
it has its own copy of the file header layout from `stats_file.c` and of bo1-re's
`data/mp/stats_ddl.txt`.

## Key conventions

- Every new address needs an MP and a `_ZM` offset in `offsets.h`, or is
  explicitly MP-only (`t5nmi` / `t5nmhi`). Code must not run MP-only natives in
  zombies.
- New hooks are declared with `t5nhd` in the module header, initialized with
  `t5nhi` / `t5nmhi` and installed with `cs_hook_install` in the module's init
  (`init_hooks()` or `host_menu_init()`).
- Code patches always go through `InstructionPatch` / `apply_instruction_patch`,
  which checks the original instruction first.
- Mod folder and file names must be lowercase (the game requests canonical names,
  the PS3 HDD is case-sensitive).
- Changes to the stats file format (`stats_file.c` / `stats_file.h`: header fields,
  `STATS_FILE_*` sizes, flags) or to the stats layout it saves must update
  `tools/stats-viewer.html` in the same change. Module and viewer stay backward
  compatible: both keep reading version 2 files (the first released format: `SLST`,
  0xD4-byte header) and every later version (`STATS_FILE_VERSION_MIN`, `header_size`
  / `HEADER_SIZES`). A format change bumps the file version instead of changing what
  an existing version means, and only appends fields at the end of the header (or data
  after the stats).
- Many offsets and game function names are reverse engineered (the
  [bo1-re](https://github.com/itacentury/bo1-re) repository holds that work). Mark unconfirmed findings in comments
  instead of presenting them as facts.
