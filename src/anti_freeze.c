#include "anti_freeze.h"

#include <cell/atomic.h>
#include <stdio.h>
#include <string.h>

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
   and sent on to every client.
*/

#define AF_ICON_SIZE 8  // '^', type, width, height, Material*
#define AF_MAX_TEXT 1024
#define AF_MATERIAL_CACHE 16
#define AF_REPORTED_CACHE 16

// Materials that passed af_material_ok. Icons are drawn every frame (button hints), so
// most checks are a cache hit.
static uint32_t knownMaterials[AF_MATERIAL_CACHE];
static int knownMaterialsNext = 0;

// Materials that failed af_material_ok. A bad text stays on screen for seconds (chat
// feed, scoreboard) and is drawn every frame.
static uint32_t badMaterials[AF_MATERIAL_CACHE];
static int badMaterialsNext = 0;

// Hashes of the bad texts already reported, so the log counts texts, not draw calls.
static uint32_t reportedTexts[AF_REPORTED_CACHE];
static int reportedTextsNext = 0;

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

// FNV-1a
static uint32_t af_hash(const char* s) {
    uint32_t hash = 0x811C9DC5;
    for (; *s; s++) hash = (hash ^ (unsigned char)*s) * 0x01000193;
    return hash;
}

static bool af_first_report(const char* text) {
    uint32_t hash = af_hash(text);
    for (int i = 0; i < AF_REPORTED_CACHE; i++)
        if (reportedTexts[i] == hash) return false;

    reportedTexts[reportedTextsNext] = hash;
    reportedTextsNext = (reportedTextsNext + 1) % AF_REPORTED_CACHE;
    return true;
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

// Removes every icon in place, valid or not: no string from the network needs one.
// Returns true if anything was removed.
static bool af_strip_icons(char* s) {
    char* out = s;
    const char* p = s;
    while (*p) {
        if (!af_is_icon(p)) {
            *out++ = *p++;
            continue;
        }
        // The whole icon, or what is left of it.
        for (int i = 0; i < AF_ICON_SIZE && *p; i++) p++;
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
    // Skip valid icons whole, their width, height and pointer bytes can contain "^\x01".
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

    if (af_first_report(text)) af_report("a text icon with an invalid material");

    size_t out = 0;
    p = text;
    while (*p && out < size - 1) {
        if (!af_is_icon(p)) {
            buffer[out++] = *p++;
            continue;
        }
        if (af_icon_ok(p)) {
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

// strtol(argv1) like the game reads it: the token after the command, quotes removed by
// the tokenizer. Saturates instead of overflowing. Not strtol itself: it sets errno, a
// TLS variable, which a PRX can't use.
static long long af_first_arg_number(const char* cmd) {
    while (*cmd && *cmd != ' ') cmd++;
    while (*cmd == ' ') cmd++;
    if (*cmd == '"') cmd++;

    bool negative = *cmd == '-';
    if (*cmd == '-' || *cmd == '+') cmd++;

    long long value = 0;
    for (; *cmd >= '0' && *cmd <= '9'; cmd++)
        if (value < 0x7FFFFFFF) value = value * 10 + (*cmd - '0');
    return negative ? -value : value;
}

static bool af_server_command_ok(const char* cmd) {
    // CG_ServerCommand and CL_CGameNeedsServerCommand switch on the first character only.
    if (cmd[0] == 'C') {
        // 0 is skipped by the game itself.
        long long weapon = af_first_arg_number(cmd);
        if (weapon != 0 && (weapon < 0 || weapon >= BG_GetNumWeapons())) {
            af_report("a server command with an invalid weapon");
            return false;
        }
    } else if (cmd[0] == 'd') {
        long long index = af_first_arg_number(cmd);
        if (index < 0 || index >= T5_MaxConfigstrings) {
            af_report("a server command with an invalid configstring");
            return false;
        }
    }
    return true;
}

int CL_CGameNeedsServerCommand_Hook(int localClientNum, int serverCommandNumber) {
    int clc = *(int*)T5_ClientConnection;
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

            // 0 makes CG_ExecuteNewServerCommands go on with the next command. Nothing is
            // tokenized yet.
            if (!af_server_command_ok(cmd)) {
                *(int*)(clc + T5_LastExecutedServerCommandOffset) = serverCommandNumber;
                return 0;
            }
        }
    }
    return CL_CGameNeedsServerCommand_Trampoline(localClientNum, serverCommandNumber);
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

static int af_sv_cmd_argc() {
    int nesting = *(int*)T5_SvCmdArgs;
    return *(int*)(T5_SvCmdArgs + T5_SvCmdArgsArgcOffset + nesting * 4);
}

// The host sends chat to every client, also to the ones without this module.
bool anti_freeze_client_command_ok(int clientNum, const char* cmd) {
    if (strcmp(cmd, "say") != 0 && strcmp(cmd, "say_team") != 0) return true;

    // All arguments, an empty one ("") doesn't end the command.
    char arg[AF_MAX_TEXT];
    int argc = af_sv_cmd_argc();
    for (int i = 1; i < argc; i++) {
        SV_Cmd_ArgvBuffer(i, arg, sizeof(arg));
        if (af_has_icon(arg)) {
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

int anti_freeze_init() {
    t5nmi(BG_GetNumWeapons);
    t5nmi(Material_RegisterHandle);
    t5nmi(SV_Cmd_ArgvBuffer);
    t5nmhi(CL_CGameNeedsServerCommand);
    t5nmhi(AddBaseDrawTextCmd);
    t5nmhi(SV_UserinfoChanged);
    t5nmhi(SV_GetUserinfo);

    int res;
    if ((res = cs_hook_install(CL_CGameNeedsServerCommand, CS_HOOK_TYPE_CTR)) < 0)
        return res;
    if ((res = cs_hook_install(AddBaseDrawTextCmd, CS_HOOK_TYPE_CTR)) < 0) return res;
    if ((res = cs_hook_install(SV_UserinfoChanged, CS_HOOK_TYPE_CTR)) < 0) return res;
    if ((res = cs_hook_install(SV_GetUserinfo, CS_HOOK_TYPE_CTR)) < 0) return res;

    return 0;
}
