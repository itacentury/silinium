# Code Style

Style conventions for this repository. These apply on every machine and do not depend on a global Claude configuration.

## Project-specific rules (non-obvious — read these first)

Formatting is owned by `clang-format` (`.clang-format`, checked in CI with version 20 over `src/`). Run `./build.sh format` or `make format` before committing; layout questions (indentation, column limit, brace placement, pointer alignment, include order) are answered by the tool, not by this file. The rules below are the ones the toolchain does **not** enforce.

**Engineering principles**

- **Do not touch unrelated code.**
- Prefer pure functions where feasible; keep side effects (hooks, patches, game memory writes) in clearly named functions.
- Break code into small, focused, reusable functions or modules.
- Use early returns where they improve readability and reduce nesting.
- **Comments:** explain "why", not "what", used sparingly and only when non-obvious. Only document what is not obvious.

**Braces**

- **Every `if` / `else` / `for` / `while` / `do` body gets braces and its own line**, also for single statements (`if (!ptr) { return; }` split over three lines). Enforced by `InsertBraces: true` and the disabled short-if/short-loop options in `.clang-format`. Short function bodies may stay on one line.

**Naming**

- Own functions are `snake_case`; public ones carry their module prefix (`host_menu_lobby_party`), file-local ones are `static`.
- Game functions and natives keep the game's name (`Dvar_FindVar`, `Party_RemoveMember`). A hook is `<Name>_Hook`, the original it calls is `<Name>_Trampoline`.
- Local variables, parameters and globals are `camelCase` (`clientNum`, `hostMenu`). Own types are `PascalCase` (`HostMenu`, `DisplayName`); game structs may keep their original name (`dvar_s`).
- Macros and constants are `UPPER_SNAKE_CASE` (`HOST_MENU_MAX_CLIENTS`, `PARTY_MEMBER_NAME`). Exception: the `t5n*` declaration macros in `defines.h`.
- Never abbreviate by deleting letters within a word; avoid ambiguous abbreviations.

**Game addresses and MP/ZM**

- Every address lives in `src/offsets.h` as `T5_<Name>` (multiplayer) and `T5_<Name>_ZM` (zombies), not as a magic number in the code. Addresses without a ZM counterpart are explicitly MP-only (the host menu is MP-only).
- Values that differ between MP and ZM are selected with `t5o(<Name>)` or `isMultiplayer`, never duplicated code paths.
- Struct field offsets and strides go behind named accessor macros (`CG_CLIENTNUM`, `PARTY_MEMBER(party, i)`) instead of scattered pointer casts.
- Reverse-engineered facts that are not confirmed are marked as such in the comment ("unverified", "names inferred from usage"), so nobody builds on them as if they were certain.

**Natives, hooks and patches**

- Declare natives with `t5nd` and hooks with `t5nhd` in the module header; initialize them with `t5ni` / `t5nhi`, or `t5nmi` / `t5nmhi` for MP-only ones (`src/defines.h`).
- Instruction patches go through an `InstructionPatch` and `apply_instruction_patch` (`src/utils.c`), which checks the original instruction before writing. Don't write code bytes directly.

**Runtime environment**

- The module links with `-nodefaultlibs`; only what is linked (`-lc -lfs_stub -llv2_stub`) and `src/printf.c` are available. Check before using a libc function that is not used elsewhere yet.
- Log output uses the `T5INFO` / `T5WARNING` / `T5ERROR` prefixes.

## C

- C99 (`-std=c99`): declare variables close to first use and initialize them right away.
- Use `bool` from `<stdbool.h>` and fixed-width types (`uint8_t`, `uint32_t`) wherever the size matters for game memory or stat layouts.
- Use `const` for read-only pointers and lookup tables.
- Use `sizeof(buffer)` / `sizeof(array) / sizeof(*array)` instead of repeating sizes.
- Header guards are `#ifndef <FILE>_H` / `#define <FILE>_H`.
- A function declaration in a header gets a one-line `//` comment when its purpose or return value is not obvious from the name (`// The active party the local player hosts, 0 if there is none.`).
- `// clang-format off` only for tables where manual alignment carries meaning, such as the offset list, and for third-party code.
- Third-party code (`src/printf.c`) stays as it was taken over: it is excluded from clang-format with a `// clang-format off` after its license header, so neither formatting nor `InsertBraces` restyles it.

## GSC

Applies to the example mods under `tmp/`.

- Braces on their own line, like the game's stock scripts.
- Functions and fields are `camelCase` (`onPlayerConnect`, `player.clientId`); stock names stay as they are.
- Folder and file names in a mod are lowercase: the game requests scripts by their canonical name and the PS3 HDD is case-sensitive.
- A script that replaces a stock one starts with a comment naming the replaced script and who calls it.
