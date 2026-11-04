#include "host_menu.h"

#include <ppu_intrinsics.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ppu_thread.h>
#include <sys/sys_time.h>
#include <sys/timer.h>

#include "cshook.h"
#include "host_menu_ui.h"
#include "t5.h"
#include "utils.h"

static bool party_is_active(int party) { return *(int*)(party + T5_PartyActiveOffset); }

static bool party_is_host(int party, int clientNum) {
    return party_is_active(party) &&
           *(int*)(party + T5_PartyHostIndexOffset) == clientNum;
}

// ClientCommand only runs on the host, so the host's server slot is the local player's.
// The party host index is unverified, so a client only counts as host if it agrees with
// cg.clientNum.
static bool is_host(int clientNum) {
    return CG_CLIENTINFO_BASE && CG_CLIENTNUM == clientNum &&
           (party_is_host(T5_LobbyParty, clientNum) ||
            party_is_host(T5_GameParty, clientNum));
}

static bool party_is_local_host(int party) {
    return party_is_active(party) && *(int*)(party + T5_PartyIsHostOffset);
}

static int party_used_count(int party) {
    int count = 0;
    for (int i = 0; i < HOST_MENU_MAX_CLIENTS; i++) {
        if (PARTY_MEMBER_USED(party, i)) {
            count++;
        }
    }
    return count;
}

// It's unverified which of the two structs is the pregame lobby (the game has a lobby
// list feeder for each), so take the active one with more members: a public lobby also
// holds the members of the private party taken into it. In matchmaking we can host the
// private party while being a client of the lobby, so the host flag can't pick it. On a
// tie take the one we don't host, so a tie never hands out host rights. Only called with
// both parties active.
static int larger_party() {
    int lobbyCount = party_used_count(T5_LobbyParty);
    int gameCount = party_used_count(T5_GameParty);
    if (lobbyCount != gameCount)
        return gameCount > lobbyCount ? T5_GameParty : T5_LobbyParty;
    return party_is_local_host(T5_LobbyParty) ? T5_GameParty : T5_LobbyParty;
}

bool host_menu_lobby_parties_both_active() {
    return party_is_active(T5_LobbyParty) && party_is_active(T5_GameParty);
}

// With both parties active the larger one is shown unless the player switched to the
// other one with Triangle.
int host_menu_lobby_view_party() {
    bool lobby = party_is_active(T5_LobbyParty);
    bool game = party_is_active(T5_GameParty);
    if (lobby && game) {
        int party = larger_party();
        if (!hostMenu.lobbyPartyOther) return party;
        return party == T5_LobbyParty ? T5_GameParty : T5_LobbyParty;
    }
    if (lobby) {
        return T5_LobbyParty;
    }
    if (game) {
        return T5_GameParty;
    }
    return 0;
}

int host_menu_lobby_party() {
    int party = host_menu_lobby_view_party();
    return party && party_is_local_host(party) ? party : 0;
}

// Server commands don't reach the pregame lobby, so the member is removed through the
// party code. The party and name were taken when the kick was chosen; if either changed
// since, the slot holds someone else. The party only has to still be hosted by us, not
// shown, so switching lists before it runs doesn't drop it. A ban adds the name to the
// list party joins are checked against, the same one banClient fills in game. queue_kick
// only asks for one in LobbyParty, the only party whose joins are checked against it.
static void lobby_kick(int party, int member, const char* name, bool ban) {
    if (!party_is_local_host(party) ||
        member == *(int*)(party + T5_PartyHostIndexOffset) ||
        !PARTY_MEMBER_USED(party, member)) {
        return;
    }
    const char* memberName = PARTY_MEMBER_NAME(party, member);
    if (strncmp(memberName, name, sizeof(hostMenu.lobbyKickName) - 1) != 0) {
        return;
    }
    if (ban) {
        Party_BanName(memberName);
    }
    Party_RemoveMember(party, member, 1,
                       ban ? "@EXE_PLAYERKICKED_BAN" : "@EXE_PLAYERKICKED");
}

static void write_stat_u8(int addr, uint8_t value) { *(uint8_t*)addr = value; }

static void write_stat_u32(int addr, uint32_t value) { *(uint32_t*)addr = value; }

static void set_stat_bit(int addr, int bit) {
    *(uint8_t*)(addr + (bit >> 3)) |= 1 << (bit & 7);
}

// Level 50, COD points and pro perks.
void host_menu_write_recovery() {
    write_stat_u32(T5_Stats_CodPoints, 0x00E1F505);  // 100000000
    write_stat_u32(T5_Stats_RankXp, 0x7F969800);     // 9999999, level 50
    write_stat_u32(T5_Stats_Rank, 0x31000000);  // 49, the menus show the level from it

    // Which items are perks is only in the stats table, so every item gets the pro bits.
    for (int i = 0; i < T5_Stats_ItemStatsCount; i++) {
        int bit = T5_Stats_ItemStatsBit + i * T5_Stats_ItemStatsBits;
        for (int b = bit; b < bit + 3; b++) {
            set_stat_bit(T5_Stats_ItemStats, b);
        }
    }
}

void host_menu_write_prestige(int prestige) {
    write_stat_u8(T5_Stats_Prestige, prestige);
}

static bool is_gametype_tdm() {
    dvar_s* dvar = Dvar_FindVar("g_gametype");
    return dvar && dvar->current.string && strcmp(dvar->current.string, "tdm") == 0;
}

int PadGetData_Hook(uint32_t port, CellPadData* data) {
    int res = PadGetData_Trampoline(port, data);

    if (res == CELL_PAD_OK && data->len > 0) {
        memcpy(&hostMenu.pad, data, sizeof(CellPadData));

        // Hide all buttons except Select from the game while the menu is open (sticks
        // stay active).
        if (hostMenu.open) {
            data->button[CELL_PAD_BTN_OFFSET_DIGITAL1] &= CELL_PAD_CTRL_SELECT;
            data->button[CELL_PAD_BTN_OFFSET_DIGITAL2] = 0;
            for (int i = CELL_PAD_BTN_OFFSET_PRESS_RIGHT;
                 i <= CELL_PAD_BTN_OFFSET_PRESS_R2; i++) {
                data->button[i] = 0;
            }
        }
    }
    return res;
}

// A menu script runs "statWriteDDL ... 0" on the prestige camos and the clan tag color
// when joining a lobby. The menus themselves never write 0 to these fields with it
// (deselecting goes through DDL_SetInt), so with Prestige Unlocks these writes are
// dropped. Returns the field index (20 camos, then the clan tag color), -1 for other
// bits.
static int prestige_unlock_field(int bit) {
    if (bit == T5_Stats_ClanTagSelectedBit) {
        return 20;
    }
    for (int c = 0; c < 5; c++) {
        int custom = T5_Stats_CustomClassCamoBit + c * T5_Stats_ClassBits;
        int prestige = T5_Stats_PrestigeClassCamoBit + c * T5_Stats_ClassBits;
        if (bit == custom) {
            return c * 2;
        }
        if (bit == custom + T5_Stats_SecondaryCamoBits) {
            return c * 2 + 1;
        }
        if (bit == prestige) {
            return 10 + c * 2;
        }
        if (bit == prestige + T5_Stats_SecondaryCamoBits) {
            return 10 + c * 2 + 1;
        }
    }
    return -1;
}

int DDL_SetFromString_Hook(void* state, const char* value, void* buffer) {
    static int logged;  // fields already logged, one bit each
    if (hostMenu.prestigeUnlocks && (int)buffer == T5_Stats_Base && value &&
        strcmp(value, "0") == 0) {
        int field = prestige_unlock_field(*(int*)state);
        if (field >= 0) {
            if (!(logged & (1 << field))) {
                printf(T5INFO "Prestige Unlocks: kept DDL bit 0x%X (statWriteDDL 0).",
                       *(int*)state);
            }
            logged |= 1 << field;
            return 1;
        }
    }
    return DDL_SetFromString_Trampoline(state, value, buffer);
}

// Kill feed message, written on the server thread and shown on the UI thread.
static char gameMessage[128];
static volatile bool gameMessagePending;

void host_menu_game_message(const char* format, ...) {
    if (gameMessagePending) {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(gameMessage, sizeof(gameMessage), format, args);
    va_end(args);
    // The text has to be visible before the flag that marks it ready.
    __lwsync();
    gameMessagePending = true;
}

// CL_ConsolePrint drops prints that don't come from the main thread, which guards buffers
// that aren't thread-safe. Menu_PaintAll is expected to run on it; if it doesn't, the
// message is dropped instead of patching the check out for every caller.
static void show_game_message() {
    static bool warned;
    if (!gameMessagePending) {
        return;
    }
    __lwsync();
    if (Sys_IsMainThread()) {
        CG_GameMessage(0, gameMessage);
    } else if (!warned) {
        warned = true;
        printf(T5WARNING
               "Menu_PaintAll is not on the main thread, dropped kill feed message.");
    }
    __lwsync();
    gameMessagePending = false;
}

// callkeyboard: requested on the server thread, opened on the UI thread, and the text is
// sent back on the server thread. Each step only runs in the state the previous one left.
enum { KEYBOARD_IDLE, KEYBOARD_REQUESTED, KEYBOARD_OPEN, KEYBOARD_ENTERED };
static volatile int keyboardState;
static char16 keyboardTitle[64];
static int keyboardClient;
static char keyboardText[65];
// String id of "keyboard_input", looked up on every server script load like
// spawnedPlayerString.
static unsigned int keyboardInputString;

bool host_menu_request_keyboard(const char* title, int clientNum) {
    if (keyboardState != KEYBOARD_IDLE || clientNum < 0 ||
        clientNum >= HOST_MENU_MAX_CLIENTS) {
        return false;
    }

    int i;
    for (i = 0; i < (int)(sizeof(keyboardTitle) / sizeof(*keyboardTitle)) - 1 && title[i];
         i++) {
        keyboardTitle[i] = (unsigned char)title[i];
    }
    keyboardTitle[i] = 0;
    keyboardClient = clientNum;
    // Title and client have to be visible before the state that marks them ready.
    __lwsync();
    keyboardState = KEYBOARD_REQUESTED;
    return true;
}

static void keyboard_input_callback(int localClientNum, const char16* text, int length) {
    if (keyboardState != KEYBOARD_OPEN) {
        return;
    }

    int i;
    for (i = 0; i < length && i < (int)sizeof(keyboardText) - 1 && text[i]; i++) {
        keyboardText[i] = text[i] > 0x7F ? '?' : (char)text[i];
    }
    keyboardText[i] = 0;
    __lwsync();
    keyboardState = KEYBOARD_ENTERED;
}

static void open_requested_keyboard() {
    static const char16 preset[] = {0};
    // The menu hides the pad from the game, so the keyboard waits until it's closed.
    if (keyboardState != KEYBOARD_REQUESTED || hostMenu.open) {
        return;
    }
    __lwsync();
    keyboardState = KEYBOARD_OPEN;
    UI_OpenKeyboard(0, keyboardTitle, preset, sizeof(keyboardText) - 1,
                    keyboard_input_callback);
}

// Runs in the server frame of the client that asked for the keyboard, so the entity is
// still that player's.
static void send_keyboard_input(void* ent) {
    if (keyboardState != KEYBOARD_ENTERED || ent != G_ENTITY(keyboardClient) ||
        !keyboardInputString) {
        return;
    }
    __lwsync();
    Scr_AddString(keyboardText, SCRIPTINSTANCE_SERVER);
    Scr_Notify(ent, keyboardInputString, 1);
    keyboardState = KEYBOARD_IDLE;
}

void Menu_PaintAll_Hook(void* a1, void* uiContext) {
    Menu_PaintAll_Trampoline(a1, uiContext);
    if (!uiContext) {
        return;
    }

    // Lobby menus can only be opened from the UI thread, and party state is only changed
    // from it.
    if (!Dvar_GetBool("cl_ingame")) {
        int kick = hostMenu.lobbyKickMember;
        if (kick != -1) {
            // Pairs with the one in queue_kick: party and name are read only after the
            // member that marks them ready, and released only once they were read.
            __lwsync();
            lobby_kick(hostMenu.lobbyKickParty, kick & ~HOST_MENU_KICK_BAN,
                       hostMenu.lobbyKickName, kick & HOST_MENU_KICK_BAN);
            __lwsync();
            hostMenu.lobbyKickMember = -1;
        }
        if (hostMenu.selectMapPending) {
            hostMenu.selectMapPending = false;
            Menus_OpenByName(0, uiContext, "select_map");
        }
        if (hostMenu.selectGametypePending) {
            hostMenu.selectGametypePending = false;
            Menus_OpenByName(0, uiContext, "select_game_mode");
        }
        if (hostMenu.changeNamePending) {
            hostMenu.changeNamePending = false;
            host_menu_ui_open_name_keyboard();
        }
    } else {
        // The same command as End Game in the pause menu. -1 is the menu index the game
        // sends for a menu that isn't a script menu.
        if (hostMenu.endGamePending) {
            hostMenu.endGamePending = false;
            char buffer[64];
            sprintf(buffer, "cmd mr %i -1 endround\n", *(int*)T5_ClServerId);
            Cbuf_AddText(0, buffer);
        }
        show_game_message();
        open_requested_keyboard();
    }
    host_menu_ui_draw();
}

// String id of "spawned_player", 0 until the first server script load. Game strings are
// freed with the match, so it is looked up again on every server script load. Looking it
// up once per load takes a single reference, freed with the match.
static unsigned int spawnedPlayerString;

void host_menu_on_server_scripts_loaded() {
    // Only resolved by host_menu_init, which doesn't run in ZM.
    if (!SL_GetString) {
        return;
    }
    spawnedPlayerString = SL_GetString("spawned_player", 1, SCRIPTINSTANCE_SERVER);
    keyboardInputString = SL_GetString("keyboard_input", 1, SCRIPTINSTANCE_SERVER);
}

int VM_Notify_Hook(unsigned int inst, unsigned int notifyListOwnerId,
                   unsigned int stringValue, void* top) {
    // Server script notifies only run on the host, and both settings are server-wide, so
    // any spawn re-applies them.
    if (inst == SCRIPTINSTANCE_SERVER && spawnedPlayerString &&
        stringValue == spawnedPlayerString) {
        // The scorelimit follows the max players set with the menu, only in team
        // deathmatch.
        if (hostMenu.maxPlayersSet && is_gametype_tdm()) {
            hostMenu.scorelimitPending = true;
        }
        if (hostMenu.depatchBounces) {
            host_menu_set_bounce_dvars(true);
        }
    }
    return VM_Notify_Trampoline(inst, notifyListOwnerId, stringValue, top);
}

// A blocked endround is reported at most once per client in this window, so a client
// repeating the command can't flood the host's kill feed.
#define ENDROUND_MESSAGE_COOLDOWN_US (5 * 1000 * 1000)
static system_time_t endroundMessageTime[HOST_MENU_MAX_CLIENTS];

// Blocks "mr <n> <menu> endround" from everyone except the host. The command name is
// matched with the game's I_stricmp like ClientCommand does, so "MR" can't slip past. The
// response is compared case-sensitively by the script, so other casings of "endround" are
// harmless.
void ClientCommand_Hook(int clientNum) {
    char cmd[37] = {0};
    char arg3[37] = {0};
    SV_Cmd_ArgvBuffer(0, cmd, sizeof(cmd));
    SV_Cmd_ArgvBuffer(3, arg3, sizeof(arg3));

    if (I_stricmp(cmd, "mr") == 0 && strcmp(arg3, "endround") == 0 &&
        !is_host(clientNum)) {
        if (CG_CLIENTINFO_BASE && clientNum >= 0 && clientNum < HOST_MENU_MAX_CLIENTS) {
            system_time_t now = sys_time_get_system_time();
            system_time_t* last = &endroundMessageTime[clientNum];
            if (!*last || now - *last >= ENDROUND_MESSAGE_COOLDOWN_US) {
                *last = now;
                host_menu_game_message("^1%s tried to end the game",
                                       CG_CLIENT_NAME(clientNum));
            }
        }
        return;
    }

    ClientCommand_Trampoline(clientNum);
}

// Server frame: runs the queued kick, scorelimit and keyboard requests.
void ClientEndFrame_Hook(void* ent, void* a2) {
    char buffer[64];

    int kick = hostMenu.kickClient;
    if (kick != -1) {
        sprintf(buffer,
                (kick & HOST_MENU_KICK_BAN) ? "banClient %i\n" : "clientkick %i\n",
                kick & ~HOST_MENU_KICK_BAN);
        Cbuf_AddText(0, buffer);
        hostMenu.kickClient = -1;
    }
    if (hostMenu.scorelimitPending) {
        hostMenu.scorelimitPending = false;
        sprintf(buffer, "set scr_tdm_scorelimit %i\n", host_menu_get_scorelimit());
        Cbuf_AddText(0, buffer);
    }
    send_keyboard_input(ent);
    ClientEndFrame_Trampoline(ent, a2);
}

static void host_menu_thread(uint64_t arg) {
    for (;;) {
        host_menu_ui_handle_input();

        bool inGame = Dvar_GetBool("cl_ingame");

        // Queued requests only belong to the match they were made in.
        if (!inGame) {
            hostMenu.kickClient = -1;
            hostMenu.scorelimitPending = false;
            hostMenu.endGamePending = false;
            keyboardState = KEYBOARD_IDLE;
            gameMessagePending = false;
        }
        // Lobby menus and kicks can't happen in game, so don't let them carry over to the
        // next lobby.
        else {
            hostMenu.selectMapPending = false;
            hostMenu.selectGametypePending = false;
            hostMenu.changeNamePending = false;
            hostMenu.lobbyKickMember = -1;
            // Each lobby starts on the larger list.
            hostMenu.lobbyPartyOther = false;
        }

        // The game resets the party dvars in the pregame lobby, keep them applied.
        if (hostMenu.forceHost && !inGame) {
            host_menu_set_party_dvars(false, hostMenu.playersToStart, 1, 1, 9);
        }
        // The game can set the DLC dvars again (lobby reload), keep them cleared while in
        // the lobby.
        if (hostMenu.disableDlc && !inGame) {
            host_menu_hide_dlc_maps();
        }
        sys_timer_usleep(10000);
    }
}

static void init_host_menu_offsets() {
    t5nmi(Cbuf_AddText);
    t5nmi(Menus_OpenByName);
    t5nmi(R_AddCmdDrawStretchPic);
    t5nmi(R_AddCmdDrawText);
    t5nmi(SV_Cmd_ArgvBuffer);
    t5nmi(I_stricmp);
    t5nmi(UI_OpenKeyboard);
    t5nmi(Dvar_GetBool);
    t5nmi(R_TextHeight);
    t5nmi(R_TextWidth);
    t5nmi(SL_GetString);
    t5nmi(R_RegisterFont);
    t5nmi(Material_RegisterHandle);
    t5nmi(Party_RemoveMember);
    t5nmi(Party_BanName);
    t5nmi(ClanTag_Format);
    t5nmi(RankIcon_Lookup);
    t5nmi(Dvar_GetString);
    t5nmi(Dvar_SetStringByName);
    t5nmi(CG_GameMessage);
    t5nmi(Sys_IsMainThread);
    t5nmi(Scr_Notify);
}

static int init_host_menu_hooks() {
    t5nmhi(PadGetData);
    t5nmhi(VM_Notify);
    t5nmhi(Menu_PaintAll);
    t5nmhi(ClientCommand);
    t5nmhi(ClientEndFrame);
    t5nmhi(DDL_SetFromString);
    t5nmhi(ClanName_Sanitize);

    int res;
    if ((res = cs_hook_install(PadGetData, CS_HOOK_TYPE_IMPORT)) < 0) {
        return res;
    }
    if ((res = cs_hook_install(VM_Notify, CS_HOOK_TYPE_CTR)) < 0) {
        return res;
    }
    if ((res = cs_hook_install(Menu_PaintAll, CS_HOOK_TYPE_CTR)) < 0) {
        return res;
    }
    if ((res = cs_hook_install(ClientCommand, CS_HOOK_TYPE_CTR)) < 0) {
        return res;
    }
    if ((res = cs_hook_install(ClientEndFrame, CS_HOOK_TYPE_CTR)) < 0) {
        return res;
    }
    if ((res = cs_hook_install(DDL_SetFromString, CS_HOOK_TYPE_CTR)) < 0) {
        return res;
    }
    if ((res = cs_hook_install(ClanName_Sanitize, CS_HOOK_TYPE_CTR)) < 0) {
        return res;
    }

    return 0;
}

// Replaces the game's sanitizer, which turns ^ into a space. Only % is dropped: the clan
// tag is sent to the other consoles in the lobby, and a % there turns into a format
// specifier wherever they print it with a printf-style function.
void ClanName_Sanitize_Hook() {
    const char* clanName = Dvar_GetString("clanName");
    if (!clanName || !strchr(clanName, '%')) {
        return;
    }

    char sanitized[32];
    int length = 0;
    for (const char* c = clanName; *c && length < sizeof(sanitized) - 1; c++) {
        if (*c != '%') {
            sanitized[length++] = *c;
        }
    }
    sanitized[length] = 0;
    Dvar_SetStringByName("clanName", sanitized);
}

// Lets the game's own menus take any class name and clan tag: skips the character,
// profanity and "3arc"/"COD" checks of the keyboard callbacks. Always on. The clan tag
// keeps its color codes through ClanName_Sanitize_Hook, which also runs when the profile
// is loaded and saved.
static void unfilter_names() {
    static const InstructionPatch patches[] = {
        {T5_ClanTagInvalidChars_Patch, 0x4192011C, PPC_NOP},
        {T5_ClanTagOffensive_Patch, 0x419A009C,
         PPC_B(T5_ClanTagOffensive_Patch, T5_ClanTagOffensive_Target)},
        {T5_ClanTagCodNotAllowed_Patch, 0x418200AC,
         PPC_B(T5_ClanTagCodNotAllowed_Patch, T5_ClanTagCodNotAllowed_Target)},
        {T5_KeyboardInvalidChars_Patch, 0x40860048,
         PPC_B(T5_KeyboardInvalidChars_Patch, T5_KeyboardInvalidChars_Target)},
    };
    for (int i = 0; i < sizeof(patches) / sizeof(*patches); i++) {
        apply_instruction_patch(&patches[i], true);
    }
}

static bool ready;

bool host_menu_ready() { return ready; }

int host_menu_init() {
    // The hooks read the menu state, so it has to be ready first.
    host_menu_ui_init();
    init_host_menu_offsets();
    unfilter_names();

    int res;
    if ((res = init_host_menu_hooks()) < 0) {
        return res;
    }

    sys_ppu_thread_t id;
    if ((res = sys_ppu_thread_create(&id, host_menu_thread, 0, 1000, 0x4000, 0,
                                     "[Silinium] host menu")) != CELL_OK) {
        return res;
    }

    ready = true;
    return 0;
}
