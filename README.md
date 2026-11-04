# Silinium

Silinium by Century is a PS3 module for Black Ops 1 that allows you to load custom game scripts, call C functions from them and makes it possible to connect to other players by patching client/server checksum. It is a fork of [T5GSCLoader](https://github.com/iMCSx/T5GSCLoader) by iMCSx.

## Build
* Clone the project and open a terminal/cmd on it.
* Type `make release`.
* The module will be created in `bin/release/`.

### Without make / SDK samples
If `make` or `$SCE_PS3_ROOT/samples/mk` is not available, use `build.sh`, which calls the toolchain directly (Git Bash on Windows, bash on Linux):

```
./build.sh release       # or: debug (default), clean, format
./build.sh release hen   # also sign Silinium_hen.sprx for PS3HEN
```

`SCE_PS3_ROOT` must point to the SDK. Outputs are `bin/<config>/Silinium.prx` and `.sprx`.
On Linux the SDK needs a `host-linux` toolchain, or run the Windows tools through Wine: `PS3_HOST_DIR=$SCE_PS3_ROOT/host-win32 PS3_TOOL_WRAPPER=wine ./build.sh release`.

### HEN
The code is the same for CFW and PS3HEN, only the signature differs: HEN does not load fake-signed SELFs. `./build.sh release hen` additionally signs `bin/release/Silinium_hen.sprx` with [scetool](https://github.com/naehrwert/scetool) (retail APP SELF, key revision 4, like the HEN build of InfinityOps). scetool and its keys are not part of the repo. Keep the keys outside the repo and point `PS3` at their folder; `data/` in the repo root (scetool's default) is gitignored as a fallback. Set `SCETOOL` to the binary if it is not on `PATH`. It is not run through `PS3_TOOL_WRAPPER`; for a Windows scetool under Wine use e.g. `SCETOOL="wine scetool.exe"`. Use absolute paths for `SCETOOL` and `PS3`; `build.sh` runs scetool from the repo root.

HEN also needs a signed EBOOT that loads the SPRX. Create one from the CFW SPRX loader EBOOT with [`tools/hen_eboot.py`](https://github.com/itacentury/bo1-re/blob/main/tools/hen_eboot.py) from [bo1-re](https://github.com/itacentury/bo1-re). Run it from a bo1-re clone, because it imports other scripts from the same `tools/` folder. It also signs with scetool and uses the same `SCETOOL` / `PS3` setup as `./build.sh release hen`. In Git Bash set `MSYS_NO_PATHCONV=1`, otherwise the `/dev_hdd0/...` path gets rewritten. For example:

```
python3 tools/hen_eboot.py path/to/cfw/EBOOT_MP.BIN /dev_hdd0/tmp/Silinium.sprx -o EBOOT_MP.BIN
```

Then copy `Silinium_hen.sprx` to the path baked into the EBOOT (here `/dev_hdd0/tmp/Silinium.sprx`).

### Formatting
The sources are formatted with clang-format 20 (`.clang-format`, checked by CI with that version; other versions format differently). Run `./build.sh format` or `make format` before committing; both refuse to run with another clang-format version. Set `CLANG_FORMAT` if version 20 is not the first `clang-format` on `PATH` (e.g. `CLANG_FORMAT=clang-format-20`). To hide the commit that reformatted everything from `git blame`, run `git config blame.ignoreRevsFile .git-blame-ignore-revs` once.

CI (`.github/workflows/format.yml`) only checks the formatting. It does not build, because the PS3 SDK cannot be redistributed, so build locally before pushing.

## Usage
Create a folder in `/dev_hdd0/tmp/Silinium/mp` or `/dev_hdd0/tmp/Silinium/zm`. Beside your created folder create a .mod file that has the same name as your mod to enable it.

Files in the mod folder **replace the game scripts (GSC/CSC) with the same path**, like InfinityOps does with `/dev_hdd0/tmp/raw`. For example `/dev_hdd0/tmp/Silinium/mp/welcome/maps/mp/gametypes/_clientids.gsc` replaces `maps/mp/gametypes/_clientids.gsc`. Only scripts the game loads through `Scr_LoadRawFile_FastFile` are replaced; other rawfiles such as `.cfg` files, string tables or menus are not. The game loads and starts the replaced scripts itself, so there is no `main.gsc` entry point anymore. New files are loaded too when a replaced script includes them.

The game requests scripts by their canonical name (lowercase, with `/` as separator) and the PS3 HDD is case-sensitive, so folder and file names in the mod must be lowercase.

The tmp directory has an example for each mode: MP replaces `maps/mp/gametypes/_clientids.gsc`, ZM replaces `maps/_zombiemode_user.gsc` (an empty stub in the stock game that every zombies map calls; this example is not yet tested on console).

The loader also removes the dvar protections (read only, write protected, cheat) and the out-of-memory error in `Hunk_AllocateTempMemoryHigh`, which is used for the replaced files.

## Host menu
In multiplayer the loader also contains a host menu, a port of "The Shake Package" mod menu from a reconstruction of `TheShakeBO1.sprx` ([src/host_menu.c](src/host_menu.c), [src/host_menu_ui.c](src/host_menu_ui.c)). It is not available in zombies.

Open it with **L1 + R3**. Up/Down scroll, Left/Right change values, ✕ selects and ○ goes back or closes. While the menu is open the game doesn't receive any button presses except Select (the sticks still work).

| Option | Effect |
|---|---|
| Show Controls | Shows the open hint while the menu is closed. |
| Force Host | Sets the party dvars so the lobby starts with you as host (kept applied in the pregame lobby) and patches the host check. |
| Players to Start | `party_minplayers` used by Force Host. |
| Max Players | Appends `party_maxplayers N` to every playlist rule (up to 18 players). Once applied, any player's spawn in team deathmatch also sets `scr_tdm_scorelimit` (3000 below 5 players, 5000 below 7, 7500 below 13, else 10000). |
| Select Map | Opens the map selection in the lobby. |
| Select Gametype | Opens the game mode selection in the lobby. |
| Depatch Bounces | Restores the bounce exploit and disables fall damage. |
| Disable DLC | Hides the DLC maps by clearing `ui_showDLCMaps` and `dlc2`–`dlc5`, and keeps them cleared if the game sets them again. Turning it off restores them. |
| Quick Recovery | Sets level 50 and 100,000,000 COD points and unlocks the pro perks (lobby only). |
| Prestige Unlocks | Removes the prestige requirement from create-a-class slots 6–10, weapon options such as the gold camo, face paints, clan tag features and calling cards (emblem backgrounds and icons), so they can be bought and used at any prestige. Items that only unlock after buying more items of the same group (classified, such as "Purchase 3 more face paints") are unlocked too. Rank requirements and COD point costs stay. It only works while the plugin is running, but what you buy or pick meanwhile is saved in your stats. When joining a lobby the game sets a prestige camo and the clan tag color back to none; while the option is on this is prevented. The server side check (`isItemLocked`) is only patched in matches you host. |
| Prestige | Sets the prestige (0–16, lobby only). |
| Change Name / Reset Name | Changes the local name with the keyboard in the lobby / restores it. |
| End Game | Ends the match with the same `mr … endround` command as End Game in the pause menu (in game only). Works as host, and as client on hosts that don't block the command. |
| Player Menu | Lists the clients (in game) or party members (lobby), each with their clan tag in front (in the lobby colored like in the lobby list, in game without color). ✕ on a player opens a menu with **Kick** and **Ban** (in game, or in the lobby party). |

When you host with Silinium, only you can end the game with `mr … endround`: the command is dropped for other clients and you see "<name> tried to end the game" in the kill feed.

The loader always lifts the restrictions on class names and the clan tag: their keyboards skip the character, profanity and "3arc"/"COD" checks, and the clan tag keeps color codes (`^1`…). The only character dropped from the clan tag is `%`, because other consoles may print the tag as a format string.

Quick Recovery and Prestige write to your online stats, only in the lobby. Quick Recovery also sets the `rank` stat, which the menus show the level from.

## Development
VS Code is used for this project, you can also use Visual Studio if you prefer but you'll need to created a new solution using the VSIX from the SDK and make sure to don't forget to include the required libraries `libc.a libfs_stub.a liblv2_stub.a`.

[`data/mp/stats_ddl.txt`](https://github.com/itacentury/bo1-re/blob/main/data/mp/stats_ddl.txt) in bo1-re is a dump of the MP stats DDL, the layout the stats offsets come from (bo1-re generates `include/bo1_mp/stats.h` from it). Its byte and bit offsets count from `T5_Stats_Base + 0x28`, bits from the lowest one. Arrays with an enum are indexed by the position in that enum, listed at the end of the file.

For a better experience with VS Code and use the tasks:

* Install C/C++ Extension.
* Git Bash or MSYS2 (unix tools, `bash`) on Windows.
* clang-format 20 for the `Format All` task, e.g. from the [LLVM 20 release](https://github.com/llvm/llvm-project/releases) or `pip install "clang-format==20.*"`. Set `CLANG_FORMAT` to its path if another version comes first on `PATH`.

You can now open `Silinium.code-workspace` in VS Code and press `Ctrl+Shift+B` to pick a task: `Build Debug/Release`, `Build Debug/Release (HEN)`, `Clean` or `Format All` (formats all sources in `src/` with clang-format).

The outputs made with a custom `printf` can be seen in Target Manager on "Console Output" menu.

# Reversing
Everything is identical on `XBOX` and `PC`, you can easily adapt for these platforms.

## Opcodes Checksum

The game load and compile scripts each time that a game is started or joined (host or client). Each game scripts will be compiled and will become opcodes, Each opcodes will pass in order through few instructions that will compute a checksum:

```
00596FC4    lwz   r10, 8(r4)    # Load the checksum from struct
00596FCC    slwi  r12, r10, 5   # Shift left by 5 bits (r10 << 5)   
00596FD0    subf  r6, r10, r12  # Substract checksum from the shifted checksum 
00596FD4    add   r5, r6, r21   # Add opcode value to checksum
00596FD8    stw   r5, 8(r4)     # Write checksum to struct
```

The lastest game script loaded is always the map one, right away this one loaded the checksum get copied from the function `Scr_GetChecksum`. On host side the checksum buffer is writed in the GameState that will be sent to clients. On client side, the checksum is read from GameState and compared with the client local value. This is the reason that if you change only a simple character in a GSC you'll not able to join/host with people.

The function signature of `Scr_GetChecksum` on PC is `8B C1 69 C9 38 10 02 00`.

**Note:** The checksum on client scripts (CSC) is computed but never checked on clients.

## Patching Checksum

There is 3 values required for the client check: `checksum`, `programLen`, `substract`.

A struct pointer is passed in `Scr_GetChecksum` and the three values will be copied into it.
I thought about two ways to patch the checksum:

* The first one is to dump the right checksum and just hook `Scr_GetCheckum` to pass the right one after you edited a script, simple and works well. But you'll need to dump each checksum for each map and gamemodes, since the checksum is based on opcodes, a gamemode or a map will also load his own script, so **you'll have to dump ~900 different value** to be able to play on each map/modes in multiplayer.

* The second way is to load custom game scripts, **after the load of the main map script** which is the latest before the copy of the checksum, by this way we can **save the valid checksum** just-in time and replace it in `Scr_GetChecksum_Hook` when our custom game scripts have been loaded.

I used the second one to don't need to worry about dumping checksums per map/gamemodes, **we can auto patch it** like this. Of course this way require additionnal hooks and code.

**Update:** the loader now replaces stock scripts instead of adding new ones. Replaced scripts are compiled before the map script, so the checksum saved after the map script would already be modified. The loader therefore uses the first way with the precomputed tables from InfinityOps ([src/checksums.c](src/checksums.c), 11 ZM and 312 MP entries, keyed by `jenkins_hash(mapname)` and `jenkins_hash(g_gametype)`). A map/gametype combination that is not in the tables keeps the real checksum, so other players can't join it. The tables only hold `checksum` and `programLen`. `substract` still matters: with the value from the modded compile, clients lose the connection while loading the map ("connection to host interrupted"). The loader therefore sends `substract = 0`, the same value InfinityOps sends (it leaves the zeroed buffer of the caller untouched).

## Calling C functions

I made a quick exemple to show how to call C functions from game script.
You just have to make your function, and return his opd pointer from `Scr_GetFunction`, like i did for `setMemory` with hex strings. If you want to make your own function working with an entity like `self` ahead the function you need to hook `Scr_GetMethod` and use the second argument of this function (type) to return the according opd.

Built-in functions:

| Function | Modes | Effect |
|---|---|---|
| `setmemory(address, data)` | MP, ZM | Writes the hex string `data` to the hex `address`. |
| `callkeyboard(title, clientNum)` | MP | Opens the game keyboard on the host console and returns 1, or 0 while another request is still running. The entered text is sent to the player with that client number: `self waittill("keyboard_input", text);`. Characters outside ASCII become `?`, at most 64 characters. |

```
if (callkeyboard("Say something", self getEntityNumber()))
{
    self waittill("keyboard_input", text);
    self iPrintln(text);
}
```

## Why I made this now?

I made it for some people few months ago, to help and having some memories times by doing C and PPC. Then someone release his own with closed source when he heard that i made it.

Recently, by cleaning my desktop i came across that folder, I said to myself it would be a cool to share this, maybe some people are curious about how game scripts check is made.

Maybe this code will be useful to someone, who knows?



# License
Silinium is licensed under the GNU General Public License v3.0, see [LICENSE](LICENSE). Credits are in [CREDITS.md](CREDITS.md).
