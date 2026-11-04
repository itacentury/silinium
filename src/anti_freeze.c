#include "anti_freeze.h"

#include <cell/atomic.h>
#include <stdio.h>
#include <string.h>

#include "client_command.h"
#include "cshook.h"
#include "host_menu.h"
#include "t5.h"
#include "utils.h"

/*
    What the "Freeze PS3" options of RTM tools send, and why it freezes:

    - "C <n>" (server command): CG_ServerCommand (0x164BB8) passes strtol(argv1) to
   BG_GetWeaponDef (0xC0A98), which indexes the weapon table without a bounds check. A
   host sends "C 2026" or "C 44444444444" with SV_GameSendServerCommand.
    - "d <index> <value>" (server command): the configstring index is only checked with a
   Com_Error above 0xAC1.
    - Text icons: "^", 0x01 or 0x02, width, height and a 4 byte Material pointer. The game
   builds them itself with a pointer of its own memory, the renderer (0x798490) uses the
   pointer as is. Sent from another console (chat, player name), the bytes after "^\x01"
   become a pointer to garbage, e.g. "^\x01xx\x12gfx_snow_sheet;" -> 0x12676678. Every
   console that draws the text freezes. A name set through userinfo is stored by the host
   and sent on to every client. In the lobby, the host sends every party member's name and
   clan tag on in the partystate message.
*/

#define AF_ICON_SIZE 8  // '^', type, width, height, Material*
#define AF_MAX_TEXT 1024
#define AF_MATERIAL_CACHE 16
// Every icon takes 8 bytes, only the last one can be cut off by the end of the text.
#define AF_MAX_ICONS (AF_MAX_TEXT / AF_ICON_SIZE + 1)
// More than the bad texts on screen at once (chat feed, scoreboard names).
#define AF_BAD_TEXT_CACHE 32

// Materials that passed af_material_ok. Icons are drawn every frame (button hints), so
// most checks are a cache hit. All caches are cleared when zones load or unload.
static uint32_t knownMaterials[AF_MATERIAL_CACHE];
static int knownMaterialsNext = 0;

// Materials that failed af_material_ok. A bad text stays on screen for seconds (chat
// feed, scoreboard) and is drawn every frame.
static uint32_t badMaterials[AF_MATERIAL_CACHE];
static int badMaterialsNext = 0;

// Bad texts with the verdict of each of their icons. A few texts can carry more distinct
// bad pointers than badMaterials holds, each draw would evict what the next icon needs
// and check every icon again, every frame. Also makes the log count texts, not draws.
typedef struct {
    uint32_t hash;
    char text[AF_MAX_TEXT];  // Compared in full, an FNV-1a collision is easy to build.
    uint8_t bad[(AF_MAX_ICONS + 7) / 8];  // Bit per icon, in the order they are met.
} af_bad_text;
static af_bad_text badTexts[AF_BAD_TEXT_CACHE];  // text[0] == 0 if unused
static int badTextsNext = 0;

static uint32_t droppedCount = 0;

static void af_report(const char* what) {
    // A tool can send thousands, keep the log readable. Called from the server and the
    // main thread, the count is taken once so both see their own value.
    uint32_t count = cellAtomicIncr32(&droppedCount) + 1;  // returns the old value
    if (count <= 8 || (count & 0xFF) == 0)
        printf(T5WARNING "Anti-Freeze: dropped %s (%u so far).", what, count);
}

static bool af_is_icon(const char* p) { return p[0] == '^' && (p[1] == 1 || p[1] == 2); }

static bool af_has_icon(const char* s) {
    for (; *s; s++)
        if (af_is_icon(s)) return true;
    return false;
}

// A real icon points to a loaded material, and looking up the material's name returns the
// same pointer. The memory is read with sys_dbg_process_read, a garbage pointer must not
// crash here either.
static bool af_material_check(uint32_t material) {
    uint32_t namePtr;
    if (sys_dbg_process_read(material, &namePtr, sizeof(namePtr)) != 0 || !namePtr)
        return false;

    // Byte by byte, a name near the end of a page is still readable.
    char name[64];
    int len = 0;
    for (; len < (int)sizeof(name); len++) {
        if (sys_dbg_process_read(namePtr + len, &name[len], 1) != 0) return false;
        if (!name[len]) break;
        if (name[len] <= ' ' || name[len] > '~') return false;
    }
    if (len == 0 || len == (int)sizeof(name)) return false;

    return (uint32_t)Material_RegisterHandle(name, 0) == material;
}

static bool af_material_ok(uint32_t material) {
    if (!material || (material & 3)) return false;

    for (int i = 0; i < AF_MATERIAL_CACHE; i++) {
        if (knownMaterials[i] == material) return true;
        if (badMaterials[i] == material) return false;
    }

    if (!af_material_check(material)) {
        badMaterials[badMaterialsNext] = material;
        badMaterialsNext = (badMaterialsNext + 1) % AF_MATERIAL_CACHE;
        return false;
    }

    knownMaterials[knownMaterialsNext] = material;
    knownMaterialsNext = (knownMaterialsNext + 1) % AF_MATERIAL_CACHE;
    return true;
}

// Material pointers are only valid while their zone is loaded: after an unload the memory
// is reused, and a pointer rejected before a zone loaded can be a real material after.
static void af_clear_materials() {
    memset(knownMaterials, 0, sizeof(knownMaterials));
    memset(badMaterials, 0, sizeof(badMaterials));
    memset(badTexts, 0, sizeof(badTexts));
}

void DB_LoadXAssets_Hook(void* zoneInfo, int zoneCount, int sync) {
    af_clear_materials();
    DB_LoadXAssets_Trampoline(zoneInfo, zoneCount, sync);
    // Text drawn while it ran (loading screen) may have cached the old pointers again.
    af_clear_materials();
}

// FNV-1a
static uint32_t af_hash(const char* s) {
    uint32_t hash = 0x811C9DC5;
    for (; *s; s++) hash = (hash ^ (unsigned char)*s) * 0x01000193;
    return hash;
}

static af_bad_text* af_find_bad_text(const char* text, uint32_t hash) {
    for (int i = 0; i < AF_BAD_TEXT_CACHE; i++)
        if (badTexts[i].hash == hash && badTexts[i].text[0] &&
            strcmp(badTexts[i].text, text) == 0)
            return &badTexts[i];
    return NULL;
}

// NULL if the text doesn't fit, it is then checked on every draw.
static af_bad_text* af_add_bad_text(const char* text, uint32_t hash) {
    if (strlen(text) >= AF_MAX_TEXT) return NULL;

    af_bad_text* entry = &badTexts[badTextsNext];
    badTextsNext = (badTextsNext + 1) % AF_BAD_TEXT_CACHE;
    entry->hash = hash;
    strcpy(entry->text, text);
    memset(entry->bad, 0, sizeof(entry->bad));
    return entry;
}

static bool af_icon_ok(const char* p) {
    // The game copies the text with strlen, a NUL inside the icon cuts it off and the
    // renderer reads past the end.
    for (int i = 1; i < AF_ICON_SIZE; i++)
        if (!p[i]) return false;

    const unsigned char* ptr = (const unsigned char*)p + 4;
    return af_material_ok((uint32_t)ptr[0] << 24 | (uint32_t)ptr[1] << 16 |
                          (uint32_t)ptr[2] << 8 | ptr[3]);
}

// Removes every icon in place, valid or not: no string from the network needs one. Only
// the icon bytes up to a '\' or '"' go, in userinfo and server commands those are the
// separators and closing quotes after the name. Returns true if anything was removed.
static bool af_strip_icons(char* s) {
    char* out = s;
    const char* p = s;
    while (*p) {
        // A removed icon between a '^' and a 0x01 or 0x02 would join them to a new one.
        if ((*p == 1 || *p == 2) && out > s && out[-1] == '^') {
            p++;
            continue;
        }
        if (!af_is_icon(p)) {
            *out++ = *p++;
            continue;
        }
        p += 2;  // '^' and type, af_is_icon checked both
        for (int i = 2; i < AF_ICON_SIZE && *p && *p != '\\' && *p != '"'; i++) p++;
    }
    *out = 0;
    return out != p;
}

// Moves an offset into the original text back by the bytes removed before it.
static void af_shift_offset(int* offset, int at, int removed) {
    if (*offset <= at) return;
    *offset -= *offset - at < removed ? *offset - at : removed;
}

// Returns text if it has no bad icon, else a copy in buffer without them. cursorPos and
// maxChars index into text and are moved to index into the copy.
static const char* af_sanitize_text(const char* text, char* buffer, size_t size,
                                    int* cursorPos, int* maxChars) {
    if (!af_has_icon(text)) return text;

    uint32_t hash = af_hash(text);
    af_bad_text* entry = af_find_bad_text(text, hash);
    // Known bad texts replay their verdicts, new ones record them.
    bool replay = entry != NULL;
    if (!replay) {
        // Skip valid icons whole, their width, height and pointer bytes can contain
        // "^\x01".
        const char* p = text;
        while (*p) {
            if (!af_is_icon(p)) {
                p++;
                continue;
            }
            if (!af_icon_ok(p)) break;
            p += AF_ICON_SIZE;
        }
        if (!*p) return text;

        af_report("a text icon with an invalid material");
        entry = af_add_bad_text(text, hash);
    }

    size_t out = 0;
    int icon = 0;
    const char* p = text;
    while (*p && out < size - 1) {
        if (!af_is_icon(p)) {
            buffer[out++] = *p++;
            continue;
        }
        bool ok;
        if (replay) {
            ok = !(entry->bad[icon / 8] & (1 << (icon % 8)));
        } else {
            ok = af_icon_ok(p);
            if (!ok && entry) entry->bad[icon / 8] |= 1 << (icon % 8);
        }
        icon++;
        if (ok) {
            if (out + AF_ICON_SIZE > size - 1) break;
            memcpy(&buffer[out], p, AF_ICON_SIZE);
            out += AF_ICON_SIZE;
            p += AF_ICON_SIZE;
            continue;
        }
        // Skip the whole icon, or what is left of it.
        int at = (int)(p - text);
        for (int i = 0; i < AF_ICON_SIZE && *p; i++) p++;
        int removed = (int)(p - text) - at;
        af_shift_offset(cursorPos, at, removed);
        af_shift_offset(maxChars, at, removed);
    }
    buffer[out] = 0;
    return buffer;
}

// Argc of a CmdArgs (T5_CmdArgsSlot or T5_SvCmdArgs) at its current nesting.
static int af_args_argc(int base) {
    return *(int*)(base + T5_CmdArgsArgcOffset + *(int*)base * 4);
}

// Argv of a CmdArgs at its current nesting, "" past argc like the game.
static const char* af_args_argv(int base, int arg) {
    if (arg >= af_args_argc(base)) return "";
    return (*(const char***)(base + T5_CmdArgsArgvOffset + *(int*)base * 4))[arg];
}

// Cmd_Argv of the command the game tokenized last.
static const char* af_cmd_argv(int arg) {
    return af_args_argv((int)Sys_GetValue(T5_CmdArgsSlot), arg);
}

// strtol(argv) the way the game reads numbers, truncated to the int the game keeps.
static int af_cmd_arg_number(int arg) {
    return (int)game_strtol(af_cmd_argv(arg), NULL, 10);
}

// "d <index> <value>" is run inside CL_CGameNeedsServerCommand, before it returns. The
// game's own tokenizer calls, in the same order as the game, so argv is what it sees.
// The game switches on argv(0)[0] alone, so "dxyz" is handled as "d" too.
static bool af_configstring_ok(const char* cmd) {
    bool ok = true;
    Cmd_TokenizeString(cmd);
    if (af_cmd_argv(0)[0] == 'd') {
        Cmd_EndTokenizedString();
        Cmd_TokenizeStringWithLimit(cmd, 3);
        // Compared unsigned by the game, a negative index is too big as well.
        ok = (uint32_t)af_cmd_arg_number(1) < T5_MaxConfigstrings;
    }
    Cmd_EndTokenizedString();

    if (!ok) af_report("a server command with an invalid configstring");
    return ok;
}

// "C <weapon>" is run by CG_ServerCommand after CL_CGameNeedsServerCommand returned, with
// the arguments it left tokenized. Like "d", it's matched on argv(0)[0] alone.
static bool af_weapon_ok() {
    if (af_cmd_argv(0)[0] != 'C') return true;

    // 0 is skipped by the game itself, BG_GetWeaponDef takes the index unsigned.
    int weapon = af_cmd_arg_number(1);
    if (weapon == 0 || (uint32_t)weapon < (uint32_t)BG_GetNumWeapons()) return true;

    af_report("a server command with an invalid weapon");
    return false;
}

int CL_CGameNeedsServerCommand_Hook(int localClientNum, int serverCommandNumber) {
    int clc = *(int*)T5_ClientConnection;
    bool checked = false;
    if (clc) {
        clc += localClientNum * T5_ClientConnectionSize;
        int sequence = *(int*)(clc + T5_ServerCommandSequenceOffset);

        // Outside of this window the game raises its own error, leave that to it.
        if (serverCommandNumber <= sequence &&
            serverCommandNumber > sequence - T5_MaxReliableCommands) {
            char* cmd = (char*)(clc + T5_ServerCommandsOffset +
                                (serverCommandNumber & (T5_MaxReliableCommands - 1)) *
                                    T5_ServerCommandSize);

            // No server command needs a text icon, the game only builds them locally. The
            // rest of the command still runs, e.g. a configstring with a player name.
            if (af_strip_icons(cmd)) af_report("a text icon in a server command");

            // 0 makes CG_ExecuteNewServerCommands go on with the next command.
            if (!af_configstring_ok(cmd)) {
                *(int*)(clc + T5_LastExecutedServerCommandOffset) = serverCommandNumber;
                return 0;
            }
            checked = true;
        }
    }

    int res = CL_CGameNeedsServerCommand_Trampoline(localClientNum, serverCommandNumber);
    // On 1 the command is still tokenized for CG_ServerCommand. Returning 0 instead, the
    // caller doesn't end it, so end it here. The game already set the last executed one.
    if (checked && res && !af_weapon_ok()) {
        Cmd_EndTokenizedString();
        return 0;
    }
    return res;
}

void* AddBaseDrawTextCmd_Hook(const char* text, int maxChars, void* font, float x,
                              float y, float w, float xScale, float yScale,
                              float rotation, const float* color, int style,
                              int cursorPos, char cursor) {
    // The game copies the text into the render command, a copy on the stack is enough.
    char buffer[AF_MAX_TEXT];
    if (text)
        text = af_sanitize_text(text, buffer, sizeof(buffer), &cursorPos, &maxChars);
    return AddBaseDrawTextCmd_Trampoline(text, maxChars, font, x, y, w, xScale, yScale,
                                         rotation, color, style, cursorPos, cursor);
}

// The host sends chat to every client, also to the ones without this module. "SAY" is
// chat too, the game matches the command names case-insensitively.
bool anti_freeze_client_command_ok(int clientNum, const char* cmd) {
    if (I_stricmp(cmd, "say") != 0 && I_stricmp(cmd, "say_team") != 0) return true;

    // All arguments, an empty one ("") doesn't end the command.
    int argc = af_args_argc(T5_SvCmdArgs);
    for (int i = 1; i < argc; i++) {
        if (af_has_icon(af_args_argv(T5_SvCmdArgs, i))) {
            af_report("chat with a text icon");
            printf(T5WARNING "Anti-Freeze: the chat came from client %i.", clientNum);
            return false;
        }
    }
    return true;
}

static void af_report_name(int clientNum) {
    af_report("a text icon in a player name");
    printf(T5WARNING "Anti-Freeze: the name came from client %i.", clientNum);
}

// The host sends names and clan tags to every client. Both hooks only run on the host:
// SV_UserinfoChanged cleans the stored userinfo, SV_GetUserinfo covers the game reading
// it before SV_UserinfoChanged ran.
void SV_UserinfoChanged_Hook(void* cl) {
    if (af_strip_icons((char*)cl + T5_ClientUserinfoOffset))
        af_report_name(((int)cl - *(int*)T5_SvClients) / T5_ClientSize);
    SV_UserinfoChanged_Trampoline(cl);
}

void SV_GetUserinfo_Hook(int index, char* buffer, int bufferSize) {
    SV_GetUserinfo_Trampoline(index, buffer, bufferSize);
    if (bufferSize > 0 && af_strip_icons(buffer)) af_report_name(index);
}

// af_strip_icons on a party member field, it may not be NUL-terminated.
static bool af_strip_field(char* field, int size) {
    char buffer[T5_PartyMemberNameSize + 1];  // the bigger field
    memcpy(buffer, field, size);
    buffer[size] = 0;
    if (!af_strip_icons(buffer)) return false;
    // Shorter than the field now, it fits with its NUL.
    strcpy(field, buffer);
    return true;
}

// Lobby names and clan tags don't go through userinfo, the host sends them in partystate.
// Only the host's partystate sender calls this. Cleans the party struct in place, so the
// host's lobby list and every later partystate are clean too.
void Party_WriteMembers_Hook(int party, int unknown, int firstMember, int targetMember,
                             void* msg) {
    for (int i = 0; i < HOST_MENU_MAX_CLIENTS; i++) {
        if (!PARTY_MEMBER_USED(party, i)) continue;
        char* member = (char*)PARTY_MEMBER(party, i);
        bool name =
            af_strip_field(member + T5_PartyMemberNameOffset, T5_PartyMemberNameSize);
        bool clan = af_strip_field(member + T5_PartyMemberClanOffset,
                                   T5_PartyMemberClanSize);
        if (name || clan) {
            af_report("a text icon in a party member name");
            printf(T5WARNING "Anti-Freeze: the name came from party member %i.", i);
        }
    }
    Party_WriteMembers_Trampoline(party, unknown, firstMember, targetMember, msg);
}

int anti_freeze_init() {
    t5nmi(BG_GetNumWeapons);
    t5nmi(Cmd_TokenizeString);
    t5nmi(Cmd_TokenizeStringWithLimit);
    t5nmi(Cmd_EndTokenizedString);
    t5nmi(Sys_GetValue);
    t5nmi(game_strtol);
    t5nmhi(CL_CGameNeedsServerCommand);
    t5nmhi(AddBaseDrawTextCmd);
    t5nmhi(SV_UserinfoChanged);
    t5nmhi(SV_GetUserinfo);
    t5nmhi(Party_WriteMembers);
    t5nmhi(DB_LoadXAssets);

    int res;
    // Before the text filter, so its caches are always cleared once it runs.
    if ((res = cs_hook_install(DB_LoadXAssets, CS_HOOK_TYPE_CTR)) < 0) return res;
    if ((res = cs_hook_install(CL_CGameNeedsServerCommand, CS_HOOK_TYPE_CTR)) < 0)
        return res;
    if ((res = cs_hook_install(AddBaseDrawTextCmd, CS_HOOK_TYPE_CTR)) < 0) return res;
    if ((res = cs_hook_install(SV_UserinfoChanged, CS_HOOK_TYPE_CTR)) < 0) return res;
    if ((res = cs_hook_install(SV_GetUserinfo, CS_HOOK_TYPE_CTR)) < 0) return res;
    if ((res = cs_hook_install(Party_WriteMembers, CS_HOOK_TYPE_CTR)) < 0) return res;
    // The chat filter runs from the ClientCommand hook.
    if ((res = client_command_init()) < 0) return res;

    return 0;
}
