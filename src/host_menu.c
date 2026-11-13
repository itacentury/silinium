#include "host_menu.h"

#include <cell/atomic.h>
#include <ppu_intrinsics.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ppu_thread.h>
#include <sys/sys_time.h>
#include <sys/timer.h>

#include "anti_god_class.h"
#include "client_command.h"
#include "cshook.h"
#include "host_menu_ui.h"
#include "t5.h"
#include "utils.h"

bool host_menu_party_active(int party) { return *(int*)(party + T5_PartyActiveOffset); }

static bool party_is_host(int party, int clientNum) {
    return host_menu_party_active(party) &&
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
    return host_menu_party_active(party) && *(int*)(party + T5_PartyIsHostOffset);
}

bool host_menu_lobby_parties_both_active() {
    return host_menu_party_active(T5_LobbyParty) && host_menu_party_active(T5_GameParty);
}

// With both parties active LobbyParty ("Lobby") is shown unless the player switched to
// GameParty ("Party") with Triangle (labels verified in game). The choice names a struct,
// not the larger party, so the list doesn't change when members join one party before the
// other. In matchmaking we can host the private party while being a client of the lobby,
// so the shown party may not be one we host.
int host_menu_lobby_view_party() {
    bool lobby = host_menu_party_active(T5_LobbyParty);
    bool game = host_menu_party_active(T5_GameParty);
    if (lobby && game) {
        return hostMenu.lobbyShowGameParty ? T5_GameParty : T5_LobbyParty;
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

// The party copies the name into the local member only when it's added; members only
// see the new one with the next partystate (see send_party_rename). The host index is
// unverified, so the slot must still hold the old name.
static bool rename_local_party_member(int party, const char* oldName, const char* name) {
    if (!party_is_local_host(party)) {
        return false;
    }
    int member = *(int*)(party + T5_PartyHostIndexOffset);
    if (member < 0 || member >= T5_PartyMaxMembers || !PARTY_MEMBER_USED(party, member) ||
        strcmp(PARTY_MEMBER_NAME(party, member), oldName) != 0) {
        return false;
    }
    snprintf(PARTY_MEMBER_NAME_BUFFER(party, member), T5_PartyMemberNameSize, "%s", name);
    return true;
}

// Party_UpdateState also copies ui_mapname / ui_gametype into the party before sending.
// In a private match those are the stock map / mode selection the stock lobby menus send
// on load anyway; in other lobbies they may not be what the party advertises, so the
// name only shows up with the next partystate there. That xblive_privatematch marks the
// private match on 1.13 is unverified; without the dvar nothing is sent.
static void send_party_rename(int party) {
    if (Dvar_GetBool("cl_ingame") || !Dvar_GetBool("xblive_privatematch")) {
        return;
    }
    Party_UpdateState(party);
}

void host_menu_set_local_name(const char* name) {
    char oldName[T5_PartyMemberNameSize];
    snprintf(oldName, sizeof(oldName), "%s", (const char*)T5_PlayerName2);

    strcpy((char*)T5_PlayerName1, name);
    strcpy((char*)T5_PlayerName2, name);

    if (rename_local_party_member(T5_LobbyParty, oldName, name)) {
        send_party_rename(T5_LobbyParty);
    }
    if (rename_local_party_member(T5_GameParty, oldName, name)) {
        send_party_rename(T5_GameParty);
    }

    // The userinfo takes the name from PlayerName2; the flag makes the client send it
    // again, the server then updates the name for everyone. The |= is not atomic: it is
    // only safe because the keyboard callback and Menu_PaintAll presumably run on the
    // same thread as the client frame (0x1CDE70) that clears the flag (unverified).
    if (Dvar_GetBool("cl_ingame")) {
        *(int*)T5_DvarModifiedFlags |= DVAR_USERINFO;
    }
}

#define PARTY_REMOVE_COOLDOWN_US (1000 * 1000)

typedef struct {
    char name[sizeof(hostMenu.lobbyKickName)];
    system_time_t time;
} PartyRemoval;

// Indexed by party (0 LobbyParty, 1 GameParty) and member slot.
static PartyRemoval lastRemovals[2][HOST_MENU_MAX_CLIENTS];

static PartyRemoval* party_removal(int party, int member) {
    if (member < 0 || member >= HOST_MENU_MAX_CLIENTS) {
        return 0;
    }
    return &lastRemovals[party == T5_GameParty][member];
}

// Goes through the stock kick: Party_RemoveMember with notify sends "endparty" instead,
// and with the host's own party in the lobby that dropped every party member along with
// the kicked player (seen in game, the exact chain is unverified).
// Whether the kick frees the slot right away is unverified. If it doesn't, the
// banned member would be removed (and sent "kicked") every frame, so a slot just
// removed is skipped for a second. The cooldown is kept per slot, so several members
// waiting to be freed don't reset each other's. The name is compared so another player
// taking the slot in that second is still removed.
// The kick also removes every other slot at the member's netadr, the host's included, so
// players on this console are never kicked, like the stock kickPlayer does.
static void remove_party_member(int party, int member, const char* reason) {
    static bool logged;
    if (Live_IsLocalXuid(PARTY_MEMBER_XUID(party, member))) {
        return;
    }
    PartyRemoval* removal = party_removal(party, member);
    if (removal) {
        const char* name = PARTY_MEMBER_NAME(party, member);
        system_time_t now = sys_time_get_system_time();
        if (strncmp(removal->name, name, sizeof(removal->name) - 1) == 0 &&
            now - removal->time < PARTY_REMOVE_COOLDOWN_US) {
            return;
        }
        strncpy(removal->name, name, sizeof(removal->name) - 1);
        removal->name[sizeof(removal->name) - 1] = 0;
        removal->time = now;
    }
    Party_KickMember(party, member, reason);
    if (!logged) {
        logged = true;
        printf(T5INFO "Party_KickMember: slot %d still used afterwards: %d.", member,
               PARTY_MEMBER_USED(party, member));
    }
}

// Same size as the game's ban list, so a full list here fits in it.
#define HOST_MENU_MAX_BANS 16

// Names banned through the menu, kept for the whole session. The game's list is shared by
// both parties and cleared by the party reset at 0x1F98B0, so restore_bans puts these
// back.
static char bannedNames[HOST_MENU_MAX_BANS][sizeof(hostMenu.lobbyKickName)];
static int bannedNext;
// Set when Party_BanName did not add a name, cleared by Unban All.
static bool banRestoreDisabled;

static void remember_ban(const char* name) {
    for (int i = 0; i < HOST_MENU_MAX_BANS; i++) {
        if (strncmp(bannedNames[i], name, sizeof(bannedNames[i]) - 1) == 0) {
            return;
        }
    }
    strncpy(bannedNames[bannedNext], name, sizeof(bannedNames[bannedNext]) - 1);
    bannedNames[bannedNext][sizeof(bannedNames[bannedNext]) - 1] = 0;
    bannedNext = (bannedNext + 1) % HOST_MENU_MAX_BANS;
}

int host_menu_ban_count() {
    int count = 0;
    for (int i = 0; i < HOST_MENU_MAX_BANS; i++) {
        if (PARTY_BAN_NAME(i)[0]) {
            count++;
        }
    }
    return count;
}

static int free_ban_entries() { return HOST_MENU_MAX_BANS - host_menu_ban_count(); }

// Clears the menu's remembered bans first, so restore_bans has nothing to put back, then
// the game's list (menu and banClient bans), zeroed like the party reset's memset does,
// and lets restoring start again after it stopped.
static void unban_all() {
    memset(bannedNames, 0, sizeof(bannedNames));
    bannedNext = 0;
    banRestoreDisabled = false;
    memset((void*)(T5_LobbyParty + T5_PartyBanListOffset), 0,
           HOST_MENU_MAX_BANS * T5_PartyBanEntrySize);
    printf(T5INFO "Unban All: cleared the ban list.");
}

// Which transitions call the reset at 0x1F98B0 (7 callers) is unverified, so the list is
// checked every frame while we host a party. What Party_BanName does with a full list is
// only read from its disassembly (replace the oldest entry), not seen in game. Names only
// go into free entries, so if it replaces, a restored name never pushes out the next one
// (or a banClient entry) every frame; if it adds nothing, the check after the call
// catches it.
static void restore_bans() {
    if (banRestoreDisabled ||
        (!party_is_local_host(T5_LobbyParty) && !party_is_local_host(T5_GameParty))) {
        return;
    }
    int freeEntries = free_ban_entries();
    int restored = 0;
    for (int i = 0; i < HOST_MENU_MAX_BANS && freeEntries > 0; i++) {
        if (!bannedNames[i][0] || Party_IsBanned(bannedNames[i])) {
            continue;
        }
        Party_BanName(bannedNames[i]);
        if (!Party_IsBanned(bannedNames[i])) {
            // Retrying every frame would not change anything.
            banRestoreDisabled = true;
            printf(T5WARNING
                   "Party_BanName did not add %s to the ban list, stopped restoring "
                   "the host menu bans.",
                   bannedNames[i]);
            return;
        }
        freeEntries--;
        restored++;
    }
    if (restored > 0) {
        printf(T5INFO "Ban list was reset, restored %d host menu ban(s).", restored);
    }
}

// Server commands don't reach the pregame lobby, so the member is removed through the
// party code. The party and name were taken when the kick was chosen; if either changed
// since, the slot holds someone else. The party only has to still be hosted by us, not
// shown, so switching lists before it runs doesn't drop it. A ban adds the name to the
// list party joins are checked against, which banClient fills too and both parties share.
// A kick on LobbyParty also removes the member from GameParty if we host it. Only
// LobbyParty joins are checked against the list, so enforce_game_party_bans covers
// rejoins of GameParty.
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
    // remove_party_member never kicks players on this console, so a ban here would only
    // take up a list entry for the whole session.
    if (Live_IsLocalXuid(PARTY_MEMBER_XUID(party, member))) {
        return;
    }
    if (ban) {
        if (!Party_IsBanned(memberName)) {
            Party_BanName(memberName);
        }
        remember_ban(memberName);
    }
    remove_party_member(party, member,
                        ban ? "@EXE_PLAYERKICKED_BAN" : "@EXE_PLAYERKICKED");
}

// The join handler skips the ban check for party id 1 (GameParty, beq at 0x21348C), so a
// banned player can rejoin it. Remove them again as soon as they show up.
static void enforce_game_party_bans() {
    int party = T5_GameParty;
    if (!party_is_local_host(party)) {
        return;
    }
    int host = *(int*)(party + T5_PartyHostIndexOffset);
    for (int i = 0; i < HOST_MENU_MAX_CLIENTS; i++) {
        if (i == host || !PARTY_MEMBER_USED(party, i)) {
            continue;
        }
        if (Party_IsBanned(PARTY_MEMBER_NAME(party, i))) {
            remove_party_member(party, i, "@EXE_PLAYERKICKED_BAN");
        }
    }
}

static void write_stat_u8(int addr, uint8_t value) { *(uint8_t*)addr = value; }

static void write_stat_u32(int addr, uint32_t value) { *(uint32_t*)addr = value; }

// Level 50, COD points and pro perks.
void host_menu_write_recovery() {
    write_stat_u32(T5_Stats_CodPoints, 0x00E1F505);  // 100000000
    write_stat_u32(T5_Stats_RankXp, 0x7F969800);     // 9999999, level 50
    write_stat_u32(T5_Stats_Rank, 0x31000000);  // 49, the menus show the level from it

    // Which items are perks is only in the stats table, so every item gets the pro bits.
    for (int i = 0; i < T5_Stats_ItemStatsCount; i++) {
        int bit = T5_Stats_ItemStatsBit + i * T5_Stats_ItemStatsBits;
        ddl_write_bits((uint8_t*)T5_Stats_ItemStats, bit, 3, 0x7);  // the three pro bits
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

// Kill feed messages, queued on the server thread and shown on the UI thread once the
// host has spawned, so they don't scroll by behind the loading screen or the class menu.
// Only the server thread moves the head and only the UI thread the tail; a producer on
// another thread could lose a slot or show half-written text. All producers run on the
// server thread: ClientEndFrame in the game frame, ClientCommand and SV_ReceiveStats
// from its packet dispatcher (0x3EF7E8, direct call graph in bo1-re, indirect calls not
// covered). The Anti-Freeze reports come from ClientCommand and from the userinfo hooks
// (SV_UserinfoChanged, SV_GetUserinfo), which are assumed to run on the server thread
// too (unverified). Dropped while no server runs, so nothing carries over to the next
// match.
#define GAME_MESSAGE_COUNT 8
static char gameMessages[GAME_MESSAGE_COUNT][128];
static volatile uint32_t gameMessageHead;
static volatile uint32_t gameMessageTail;
// Set on the host's first spawn in a match, cleared on every server script load and while
// no server runs.
static volatile bool hostSpawned;

void host_menu_game_message(const char* format, ...) {
    uint32_t head = gameMessageHead;
    if (head - gameMessageTail == GAME_MESSAGE_COUNT) {
        return;
    }
    char* message = gameMessages[head % GAME_MESSAGE_COUNT];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(*gameMessages), format, args);
    va_end(args);
    // The text has to be visible before the head that marks it ready.
    __lwsync();
    gameMessageHead = head + 1;
}

// CL_ConsolePrint drops prints that don't come from the main thread, which guards buffers
// that aren't thread-safe. Menu_PaintAll is expected to run on it; if it doesn't, the
// messages are dropped instead of patching the check out for every caller.
static void show_game_messages() {
    static bool warned;
    if (!hostSpawned) {
        return;
    }
    for (uint32_t tail = gameMessageTail; tail != gameMessageHead; tail++) {
        __lwsync();
        if (Sys_IsMainThread()) {
            CG_GameMessage(0, gameMessages[tail % GAME_MESSAGE_COUNT]);
        } else if (!warned) {
            warned = true;
            printf(T5WARNING
                   "Menu_PaintAll is not on the main thread, dropped kill feed message.");
        }
        // The slot may only be reused once the text has been read.
        __lwsync();
        gameMessageTail = tail + 1;
    }
}

// callkeyboard: requested on the server thread, opened on the UI thread, and the text is
// sent back on the server thread. Each step only runs in the state the previous one left.
// In game Change Name holds the same state (KEYBOARD_NAME): a second UI_OpenKeyboard most
// likely replaces the open keyboard's callback, so only one of them may be up at a time.
// The game calls the callback on cancel too (text NULL, length 0), but not if the
// keyboard failed to open or ended with an unexpected result. So once the game's keyboard
// is closed, a callkeyboard keyboard still open answers like a cancel (empty text) and
// Change Name's state is released; the state is also released when the client of a
// callkeyboard left. Closed means fully unloaded: no keyboard is opened and no state
// released while the game's previous keyboard is still unloading, up to a time limit.
// In game the game only updates its keyboard while a menu is open, so the update is
// driven from here (update_game_keyboard).
enum {
    KEYBOARD_IDLE,
    KEYBOARD_REQUESTED,
    KEYBOARD_OPEN,
    KEYBOARD_ENTERED,
    KEYBOARD_NAME
};
static volatile uint32_t keyboardState;
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
    // The UI thread may have taken the keyboard for Change Name since the check above.
    return cellAtomicCompareAndSwap32((uint32_t*)&keyboardState, KEYBOARD_IDLE,
                                      KEYBOARD_REQUESTED) == KEYBOARD_IDLE;
}

// In the lobby there is no callkeyboard, so the name keyboard doesn't take the state.
static void open_name_keyboard(bool inGame) {
    if (inGame && cellAtomicCompareAndSwap32((uint32_t*)&keyboardState, KEYBOARD_IDLE,
                                             KEYBOARD_NAME) != KEYBOARD_IDLE) {
        printf(T5INFO "Change Name skipped, a script keyboard is open.");
        return;
    }
    host_menu_ui_open_name_keyboard();
}

void host_menu_name_keyboard_closed() {
    cellAtomicCompareAndSwap32((uint32_t*)&keyboardState, KEYBOARD_NAME, KEYBOARD_IDLE);
}

// How long the game's keyboard may stay finished or unloading (2/3) before it counts as
// closed anyway: the state values are only inferred, and a wait that never ends would
// leave every later keyboard pending and a callkeyboard script without its answer.
#define KEYBOARD_UNLOAD_TIMEOUT_US (2 * 1000 * 1000)

// A longer gap between two polls means nobody waited in between (no request pending, or a
// hitch): the wait starts over instead of counting the gap as unload time.
#define KEYBOARD_POLL_GAP_US (300 * 1000)

// Unloading (3) is not closed: its OSKDIALOG_UNLOADED still follows and sets 0, also
// under a keyboard opened in between, and a new dialog most likely can't be loaded before
// that. Open (1) never times out, the player may still be typing. Only called on the UI
// thread.
static bool game_keyboard_closed() {
    static system_time_t closingSince;
    static system_time_t lastPoll;
    static bool warned;
    int state = *(volatile int*)T5_KeyboardState;
    if (state == T5_KeyboardStateClosed || state == T5_KeyboardStateOpen) {
        closingSince = 0;
        warned = false;
        return state == T5_KeyboardStateClosed;
    }

    system_time_t now = sys_time_get_system_time();
    bool waitInterrupted = now - lastPoll > KEYBOARD_POLL_GAP_US;
    lastPoll = now;
    if (!closingSince || waitInterrupted) {
        closingSince = now;
        warned = false;
        return false;
    }
    if (now - closingSince < KEYBOARD_UNLOAD_TIMEOUT_US) {
        return false;
    }
    if (!warned) {
        warned = true;
        printf(T5WARNING "Game keyboard stuck in state %i, treated as closed.", state);
    }
    return true;
}

// An open keyboard stays up on the host's console; once its text is entered it is dropped
// here.
static void drop_keyboard_of_left_client() {
    uint32_t state = keyboardState;
    if ((state != KEYBOARD_REQUESTED && state != KEYBOARD_ENTERED) || !SV_CLIENTS ||
        SV_CLIENT_IN_USE(keyboardClient)) {
        return;
    }
    cellAtomicCompareAndSwap32((uint32_t*)&keyboardState, state, KEYBOARD_IDLE);
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

// In game UI_Refresh, the only caller of the keyboard update, runs only while a menu
// catches the keys. Without one a finished dialog is never unloaded: it is gone from the
// screen but keeps the pad and the PS button, and its callback never runs. A second call
// in the same frame from UI_Refresh is harmless, the first one already left state 2.
static void update_game_keyboard() {
    int state = *(volatile int*)T5_KeyboardState;
    if (state == T5_KeyboardStateOpen || state == T5_KeyboardStateFinished) {
        UI_UpdateKeyboard(0);
    }
}

// The game runs the callback before it leaves the finished state, so a callback that did
// run has already moved the state on and nothing matches. A callkeyboard keyboard without
// its callback is answered like a cancel, the script is still waiting on keyboard_input.
// Both this and the callback run on the UI thread, so the callback's state check holds.
static void release_closed_keyboard() {
    if (!game_keyboard_closed()) {
        return;
    }
    cellAtomicCompareAndSwap32((uint32_t*)&keyboardState, KEYBOARD_NAME, KEYBOARD_IDLE);
    keyboard_input_callback(0, NULL, 0);
}

static void open_requested_keyboard() {
    static const char16 preset[] = {0};
    // The menu hides the pad from the game, so the keyboard waits until it's closed.
    if (keyboardState != KEYBOARD_REQUESTED || hostMenu.open || !game_keyboard_closed()) {
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

// Always keeps ^ for color codes. % is dropped only from the clan tag: it is sent to the
// other consoles in the lobby, and a % there turns into a format specifier wherever they
// print it with a printf-style function. Class names stay local, so they keep it.
// Control characters are dropped only on request: the game draws button glyphs with them
// (\x05, \x11, \x14...), so a tag set through the dvar or GSC may use them on purpose,
// while a stats file can hold any bytes. Writes at most size - 1 characters.
static void name_filter(const char* name, char* out, int size, bool dropPercent,
                        bool dropControlCharacters) {
    int length = 0;
    for (const char* c = name; *c && length < size - 1; c++) {
        if ((dropPercent && *c == '%') ||
            (dropControlCharacters && (unsigned char)*c < 0x20)) {
            continue;
        }
        out[length++] = *c;
    }
    out[length] = 0;
}

// The dvars of the class names, in the order of the game's dvar table (see offsets.h).
static const char* const classNameDvars[STATS_FILE_CLASS_COUNT] = {
    "customclass1",   "customclass2",   "customclass3",   "customclass4",
    "customclass5",   "prestigeclass1", "prestigeclass2", "prestigeclass3",
    "prestigeclass4", "prestigeclass5",
};

static void read_dvar_string(const char* name, char* out, int size) {
    const char* value = Dvar_GetString(name);
    snprintf(out, size, "%s", value ? value : "");
}

void host_menu_read_profile_names(StatsFileProfile* profile) {
    read_dvar_string("clanName", profile->clanName, sizeof(profile->clanName));
    for (int i = 0; i < STATS_FILE_CLASS_COUNT; i++) {
        read_dvar_string(classNameDvars[i], profile->classNames[i],
                         sizeof(profile->classNames[i]));
    }
}

// Sets the loaded clan tag and class names like the game's class name keyboard does, so
// they reach the gamer profile: the game sets the dvars back from the profile, clanName
// included (GamerProfile_UpdateDvarsFromProfile). Empty class names keep the current
// name. Control characters are dropped because a stats file can hold any bytes, % only
// from the clan tag (see name_filter).
static void apply_loaded_profile_names(const StatsFileProfile* profile) {
    int controller = CL_ControllerIndexFromClientNum(0);
    GamerProfile_UpdateDvarsFromProfile(controller);
    // Same buffer as the save and ClanName_Sanitize_Hook. Writing the profile cuts the
    // tag to its first 4 bytes right away (seen on the console, a color code takes 2),
    // so a longer saved tag doesn't survive the load.
    char clanName[STATS_FILE_CLAN_MAX];
    name_filter(profile->clanName, clanName, sizeof(clanName), true, true);
    Dvar_SetStringByName("clanName", clanName);
    for (int i = 0; i < STATS_FILE_CLASS_COUNT; i++) {
        char className[STATS_FILE_CLASS_NAME_MAX];
        name_filter(profile->classNames[i], className, sizeof(className), false, true);
        if (*className) {
            Dvar_SetStringByName(classNameDvars[i], className);
        }
    }
    GamerProfile_UpdateProfileFromDvars(controller, 1);
}

// The game's select_map / select_game_mode menus only set ui_mapname / ui_gametype; the
// members only see them once the host sends partystate again. Stock lobby menus do that
// on menu load, which the menus opened by the host menu skip. Only armed while the menu
// the host menu opened is still open and a party is hosted, so stock flows (public
// lobbies, the stock private-match menus) never send partystate from here. Every select
// menu the host menu opened is bound on its own, so a second one (Select Gametype over
// Select Map) doesn't end the sync while the first is still open. Disarmed while no
// bound menu is left; the stack never holds more menus than its max.
static void* mapSyncMenus[T5_UiMenuStackMax];
static int mapSyncMenuCount;
static char syncedMap[64];
static char syncedGametype[64];

static void store_synced_party_map() {
    read_dvar_string("ui_mapname", syncedMap, sizeof(syncedMap));
    read_dvar_string("ui_gametype", syncedGametype, sizeof(syncedGametype));
}

static bool ui_menu_is_open(void* uiContext, const void* menu) {
    int count = UI_OPEN_MENU_COUNT(uiContext);
    if (count > T5_UiMenuStackMax) {
        count = T5_UiMenuStackMax;
    }
    for (int i = 0; i < count; i++) {
        if (UI_MENU_STACK_ENTRY(uiContext, i) == menu) {
            return true;
        }
    }
    return false;
}

static bool map_sync_menu_is_bound(const void* menu) {
    for (int i = 0; i < mapSyncMenuCount; i++) {
        if (mapSyncMenus[i] == menu) {
            return true;
        }
    }
    return false;
}

static void clear_party_map_sync() { mapSyncMenuCount = 0; }

static void prune_closed_map_sync_menus(void* uiContext) {
    int kept = 0;
    for (int i = 0; i < mapSyncMenuCount; i++) {
        if (ui_menu_is_open(uiContext, mapSyncMenus[i])) {
            mapSyncMenus[kept++] = mapSyncMenus[i];
        }
    }
    mapSyncMenuCount = kept;
}

// Binds the sync to the menu Menus_OpenByName just pushed. If the stack did not grow
// (menu not found, or the stack layout is wrong), the sync is not armed at all, so it
// can never stay bound to a lobby menu. The first new entry is bound, not the top: a
// menu the select menu's onOpen opens would sit above it. That relies on
// Menus_OpenByName pushing the menu before running onOpen, as in CoD4 (unverified).
// The current selection counts as synced when the sync arms, so partystate only goes
// out on a real change; arming a second menu keeps the baseline, so a change made in the
// first is not swallowed. Without a hosted party the baseline would belong to no lobby
// and could send partystate in the next one the player hosts.
static void arm_party_map_sync(void* uiContext, int openMenuCountBefore) {
    if (!party_is_local_host(T5_LobbyParty) && !party_is_local_host(T5_GameParty)) {
        return;
    }
    int count = UI_OPEN_MENU_COUNT(uiContext);
    if (openMenuCountBefore < 0 || count <= openMenuCountBefore ||
        count > T5_UiMenuStackMax) {
        printf(T5WARNING "Menu stack did not grow (%i -> %i), party map sync not armed.",
               openMenuCountBefore, count);
        return;
    }
    void* menu = UI_MENU_STACK_ENTRY(uiContext, openMenuCountBefore);
    if (map_sync_menu_is_bound(menu)) {
        return;
    }
    if (mapSyncMenuCount >= (int)(sizeof(mapSyncMenus) / sizeof(*mapSyncMenus))) {
        return;
    }
    if (!mapSyncMenuCount) {
        store_synced_party_map();
    }
    mapSyncMenus[mapSyncMenuCount++] = menu;
}

// Members only read the map from the party's rotation ring here, see
// T5_Com_IsPublicOnlineGame. Wager matches look up the playlist differently, so they are
// left alone.
static bool party_map_uses_rotation_ring() {
    return Com_IsPublicOnlineGame() && !Dvar_GetBool("xblive_wagermatch");
}

// The ui_gametype value of a rotation entry, resolved like 0x440910 does. The raw
// playlist name if the table does not have it (the game would stop with an error there).
static const char* rotation_entry_gametype(int entry) {
    const char* playlistName = PLAYLIST_ROTATION_GAMETYPE(entry);
    if (!playlistName) {
        return 0;
    }
    for (int i = 0; i < T5_PlaylistGametypeMax; i++) {
        const char* name = PLAYLIST_GAMETYPE_PLAYLIST_NAME(i);
        if (name && !I_stricmp(name, playlistName)) {
            return PLAYLIST_GAMETYPE_NAME(i);
        }
    }
    return playlistName;
}

// Lists the gametypes the map has in each playlist rotation, to see on the console why a
// selection was not found.
static void log_map_rotations(const char* map) {
    int playlist = 0;
    for (int slot = 0; slot < T5_PlaylistMax; slot++) {
        int count = PLAYLIST_ROTATION_COUNT(slot);
        if (!count) {
            continue;
        }
        int first = PLAYLIST_ROTATION_FIRST(slot);
        char line[256];
        int length = 0;
        line[0] = 0;
        for (int i = 0; i < count && i < T5_PlaylistRotationNone; i++) {
            const char* entryMap = PLAYLIST_ROTATION_MAP(first + i);
            if (!entryMap || I_stricmp(entryMap, map) || length >= (int)sizeof(line)) {
                continue;
            }
            const char* raw = PLAYLIST_ROTATION_GAMETYPE(first + i);
            const char* resolved = rotation_entry_gametype(first + i);
            length += snprintf(line + length, sizeof(line) - length, " %s (%s)",
                               resolved ? resolved : "-", raw ? raw : "-");
        }
        if (line[0]) {
            printf(T5INFO "Playlist %i has %s with:%s", playlist, map, line);
        }
        playlist++;
    }
}

// Index of the map in the rotation of the playlist slot, preferring the entry with the
// gametype as well. -1 if the map is not in the rotation, or only with another gametype
// while requireGametype is set.
static int playlist_slot_rotation_index(int slot, const char* map, const char* gametype,
                                        bool requireGametype) {
    int count = PLAYLIST_ROTATION_COUNT(slot);
    int first = PLAYLIST_ROTATION_FIRST(slot);
    int mapOnly = -1;
    // The ring holds bytes and T5_PlaylistRotationNone means no map.
    for (int i = 0; i < count && i < T5_PlaylistRotationNone; i++) {
        const char* entryMap = PLAYLIST_ROTATION_MAP(first + i);
        if (!entryMap || I_stricmp(entryMap, map)) {
            continue;
        }
        const char* entryGametype = rotation_entry_gametype(first + i);
        if (entryGametype && !I_stricmp(entryGametype, gametype)) {
            return i;
        }
        if (mapOnly < 0) {
            mapOnly = i;
        }
    }
    return requireGametype ? -1 : mapOnly;
}

// Slot of the playlist (the playlist dvar's value), counted over the slots with a
// rotation like 0x43F020 does. -1 if there is none.
static int playlist_slot(int playlist) {
    int rank = 0;
    for (int slot = 0; slot < T5_PlaylistMax; slot++) {
        if (!PLAYLIST_ROTATION_COUNT(slot)) {
            continue;
        }
        if (rank++ == playlist) {
            return slot;
        }
    }
    return -1;
}

// How many rotation entries of the playlist slot have the gametype.
static int playlist_slot_gametype_matches(int slot, const char* gametype) {
    int count = PLAYLIST_ROTATION_COUNT(slot);
    int first = PLAYLIST_ROTATION_FIRST(slot);
    int matches = 0;
    for (int i = 0; i < count && i < T5_PlaylistRotationNone; i++) {
        const char* entryGametype = rotation_entry_gametype(first + i);
        if (entryGametype && !I_stricmp(entryGametype, gametype)) {
            matches++;
        }
    }
    return matches;
}

// The COD Points a member must have for the playlist. Members that came in through a
// normal playlist have 0 in the party the check reads (see T5_PlaylistWagerCostOffset1),
// so a playlist that costs anything drops them.
static int playlist_slot_wager_cost(int slot) {
    return PLAYLIST_WAGER_COST1(slot) + PLAYLIST_WAGER_COST2(slot);
}

// The playlist other than current whose rotation has the map with the gametype and the
// largest share of that gametype, so a playlist of only that mode wins over mixed ones
// like Mosh Pit. Wager playlists that cost COD Points are left out. Its rotation index
// goes into index. -1 if there is none.
static int find_playlist_with_rotation(int current, const char* map, const char* gametype,
                                       int* index) {
    int best = -1;
    int bestMatches = 0;
    int bestCount = 1;
    int playlist = 0;
    for (int slot = 0; slot < T5_PlaylistMax; slot++) {
        int count = PLAYLIST_ROTATION_COUNT(slot);
        if (!count) {
            continue;
        }
        int found = playlist == current
                        ? -1
                        : playlist_slot_rotation_index(slot, map, gametype, true);
        if (found >= 0) {
            int matches = playlist_slot_gametype_matches(slot, gametype);
            int cost = playlist_slot_wager_cost(slot);
            printf(T5INFO "Playlist %i has %s / %s, %i of %i entries are %s, cost %i.",
                   playlist, map, gametype, matches, count, gametype, cost);
            if (cost <= 0 && (best < 0 || matches * bestCount > bestMatches * count)) {
                best = playlist;
                bestMatches = matches;
                bestCount = count;
                *index = found;
            }
        }
        playlist++;
    }
    return best;
}

#define GAMETYPES_TABLE "mp/gametypesTable.csv"

// A table cell as a number, -1 if it isn't one.
static int table_number(const char* cell) {
    if (!cell || !*cell) {
        return -1;
    }
    int value = 0;
    for (; *cell; cell++) {
        if (*cell < '0' || *cell > '9') {
            return -1;
        }
        value = value * 10 + (*cell - '0');
    }
    return value;
}

// The game's own list stops at maxnum_gametype, the wager gametypes come after it.
static bool gametype_is_wager(const char* gametype) {
    void* table = NULL;
    StringTable_Find(GAMETYPES_TABLE, &table);
    if (!table) {
        return false;
    }
    int index = table_number(StringTable_Lookup(table, 0, gametype, 4));
    int standardCount = table_number(StringTable_Lookup(table, 0, "maxnum_gametype", 1));
    return index >= 0 && standardCount >= 0 && index >= standardCount;
}

// The playlist the matchmaking session was offered in before the host menu switched the
// playlist, -1 while none is kept. Players searching that playlist must keep finding the
// lobby.
static int matchmakingPlaylist = -1;

typedef enum {
    ROTATION_PUSHED,
    ROTATION_PLAYLIST_SWITCHED,
    ROTATION_MISSING,
    ROTATION_HOST_GAMETYPE,
} RotationResult;

static void push_party_rotation_index(int party, int index) {
    int ringSlot =
        (int)(((unsigned int)PARTY_MAP_RING_INDEX(party) + 1) % T5_PartyMapRingSize);
    PARTY_MAP_RING_ENTRY(party, ringSlot) = (uint8_t)index;
    PARTY_MAP_RING_INDEX(party) = ringSlot;
    // Rewrites ui_mapname / ui_gametype.
    Playlist_SetRotationEntry(0, index);
}

// A wager gametype only in playlists that cost COD Points: the host plays it without a
// playlist switch, the members keep their mode. A new map still goes out through the
// current playlist's rotation, with whatever gametype it has there.
static RotationResult push_party_map_keep_gametype(int party, int slot, const char* map,
                                                   const char* gametype,
                                                   bool mapChanged) {
    char hostGametype[64];
    snprintf(hostGametype, sizeof(hostGametype), "%s", gametype);
    printf(T5INFO
           "%s / %s is only in wager playlists that cost COD Points, members keep "
           "their mode.",
           map, gametype);
    int index =
        mapChanged ? playlist_slot_rotation_index(slot, map, gametype, false) : -1;
    if (index >= 0) {
        push_party_rotation_index(party, index);
        Dvar_SetStringByName("ui_gametype", hostGametype);
    }
    store_synced_party_map();
    return ROTATION_HOST_GAMETYPE;
}

// Pushes the rotation index like the game's own rotation (0x1F0268) and applies the entry
// to the host as well, so the host's ui_gametype matches what members get. If only
// another playlist has the selection in its rotation, the host switches to it: partystate
// carries the playlist with the ring, so members resolve the index in the same playlist.
static RotationResult push_party_rotation_map(int party, const char* map,
                                              const char* gametype, bool mapChanged) {
    dvar_s* playlistDvar = Dvar_FindVar("playlist");
    if (!playlistDvar) {
        return ROTATION_MISSING;
    }
    int playlist = playlistDvar->current.integer;
    int slot = playlist_slot(playlist);
    int index = slot < 0 ? -1 : playlist_slot_rotation_index(slot, map, gametype, true);
    // A new map may keep the playlist with another gametype. A new gametype alone must
    // not fall back to an entry with another one, that would undo the host's choice; a
    // wager gametype neither, push_party_map_keep_gametype handles the map for it.
    if (index < 0 && mapChanged && slot >= 0 && !gametype_is_wager(gametype)) {
        index = playlist_slot_rotation_index(slot, map, gametype, false);
    }
    RotationResult result = ROTATION_PUSHED;
    if (index < 0) {
        int other = find_playlist_with_rotation(playlist, map, gametype, &index);
        if (other < 0 && slot >= 0 && gametype_is_wager(gametype)) {
            return push_party_map_keep_gametype(party, slot, map, gametype, mapChanged);
        }
        if (other < 0) {
            printf(T5WARNING
                   "%s / %s is in no playlist rotation, members keep the old one.",
                   map, gametype);
            log_map_rotations(map);
            return ROTATION_MISSING;
        }
        printf(T5INFO "%s / %s is not in playlist %i, switching to playlist %i.", map,
               gametype, playlist, other);
        if (matchmakingPlaylist < 0) {
            int offered = SESSION_INFO_PLAYLIST;
            matchmakingPlaylist = offered >= 0 ? offered : playlist;
        }
        Dvar_SetInt(playlistDvar, other);
        printf(T5INFO "Session still offered in playlist %i.", SESSION_INFO_PLAYLIST);
        result = ROTATION_PLAYLIST_SWITCHED;
    }
    // Rewrites ui_mapname / ui_gametype; map and gametype may point into the old values,
    // so they are not used afterwards.
    push_party_rotation_index(party, index);
    store_synced_party_map();
    return result;
}

static void send_party_map_if_changed(bool lobbyHosted, bool gameHosted) {
    const char* map = Dvar_GetString("ui_mapname");
    const char* gametype = Dvar_GetString("ui_gametype");
    if (!map || !gametype) {
        return;
    }
    bool mapChanged = strcmp(map, syncedMap) != 0;
    if (!mapChanged && !strcmp(gametype, syncedGametype)) {
        return;
    }
    store_synced_party_map();
    // The game's rotation only uses the LobbyParty ring (0x1F0268).
    if (lobbyHosted && party_map_uses_rotation_ring()) {
        RotationResult result =
            push_party_rotation_map(T5_LobbyParty, map, gametype, mapChanged);
        if (result == ROTATION_PLAYLIST_SWITCHED) {
            host_menu_ui_show_status("Switched to a playlist with this map and mode");
        } else if (result == ROTATION_HOST_GAMETYPE) {
            host_menu_ui_show_status("Wager mode: players see the old mode in the lobby");
        } else if (result == ROTATION_MISSING) {
            host_menu_ui_show_status(
                mapChanged ? "Not in any playlist, players keep the old map"
                           : "Not in any playlist, players keep the old mode");
        }
    }
    // Unlike send_party_rename not limited to private matches: the host picked this map /
    // mode on purpose through the host menu. Outside a private match the members only see
    // it through the rotation ring above.
    if (lobbyHosted) {
        Party_UpdateState(T5_LobbyParty);
    }
    if (gameHosted) {
        Party_UpdateState(T5_GameParty);
    }
}

// A wager gametype outside a wager lobby (the extended gametype list) runs with
// xblive_wagermatch 0: the HUD shows its wager kill feed next to the normal one, which it
// only hides with the dvar, and the server scripts show score popups. Private matches
// only: whether the dvar triggers wager bets or payouts in a public match is unknown,
// private matches save no stats. Consoles without Silinium still get the scripts' part
// from the host.
static bool wagerMatchForced;
static bool wagerMatchOriginal;
static bool wagerMatchSeenInGame;

static bool wager_match_wanted() {
    const char* gametype = Dvar_GetString("g_gametype");
    return !Com_IsPublicOnlineGame() && gametype && *gametype &&
           gametype_is_wager(gametype);
}

static void force_wager_match() {
    if (wagerMatchForced) {
        return;
    }
    wagerMatchOriginal = Dvar_GetBool("xblive_wagermatch");
    wagerMatchForced = true;
    wagerMatchSeenInGame = false;
    Dvar_SetStringByName("xblive_wagermatch", "1");
    printf(T5INFO "Wager gametype %s: xblive_wagermatch 1.",
           Dvar_GetString("g_gametype"));
}

static void restore_wager_match() {
    if (!wagerMatchForced) {
        return;
    }
    wagerMatchForced = false;
    Dvar_SetStringByName("xblive_wagermatch", wagerMatchOriginal ? "1" : "0");
    printf(T5INFO "Wager gametype over: xblive_wagermatch back to %i.",
           wagerMatchOriginal);
}

// The host forces it on the server script load already
// (host_menu_on_server_scripts_loaded), members once they are in the game. Restored after
// the match, not before it: the host loads the scripts before it is in the game, with
// sv_running already set. Without sv_running a load that never reached the game was
// aborted, the forced value must not stay for the next (possibly public wager) match.
static void update_wager_match() {
    bool inGame = Dvar_GetBool("cl_ingame");
    if (inGame && wager_match_wanted()) {
        force_wager_match();
        wagerMatchSeenInGame = true;
    } else if (!inGame && (wagerMatchSeenInGame || !Dvar_GetBool("sv_running"))) {
        restore_wager_match();
    }
}

// The game writes the session's playlist again when it recreates the session (e.g. after
// a match), the switched one would then hide the lobby from the original playlist.
static void keep_matchmaking_playlist() {
    if (matchmakingPlaylist < 0) {
        return;
    }
    dvar_s* playlistDvar = Dvar_FindVar("playlist");
    if (!playlistDvar || !party_is_local_host(T5_LobbyParty) ||
        !party_map_uses_rotation_ring() ||
        playlistDvar->current.integer == matchmakingPlaylist) {
        matchmakingPlaylist = -1;
        return;
    }
    int offered = SESSION_INFO_PLAYLIST;
    if (offered < 0 || offered == matchmakingPlaylist) {
        return;
    }
    Session_SetPlaylist(0, matchmakingPlaylist);
    if (SESSION_INFO_PLAYLIST != matchmakingPlaylist) {
        printf(T5WARNING "Session is offered in playlist %i, could not restore %i.",
               offered, matchmakingPlaylist);
        matchmakingPlaylist = -1;
        return;
    }
    printf(T5INFO "Session was offered in playlist %i, restored %i.", offered,
           matchmakingPlaylist);
}

// Shows what the table holds (description and icon columns included), for checking the
// rows extend_gametype_list adds.
static void log_gametype_table(void* table) {
    static bool logged;
    if (logged) {
        return;
    }
    logged = true;
    int columns = STRING_TABLE_COLUMN_COUNT(table);
    for (int row = 0; row < STRING_TABLE_ROW_COUNT(table); row++) {
        char line[256];
        int length = snprintf(line, sizeof(line), "%i:", row);
        for (int column = 0; column < columns && length < (int)sizeof(line); column++) {
            const char* cell = STRING_TABLE_CELL(table, row, column);
            length +=
                snprintf(line + length, sizeof(line) - length, " %s |", cell ? cell : "");
        }
        printf(T5INFO "gametypesTable row %s", line);
    }
}

// The game's list stops at maxnum_gametype, which leaves out the wager gametypes the
// table has right after it. Adds the following indices like UI_AssetCache does, in the
// table's order: the menu looks description and icon up by list index. UI_AssetCache may
// load the list again, so this runs before every open.
static void extend_gametype_list() {
    void* table = NULL;
    StringTable_Find(GAMETYPES_TABLE, &table);
    if (!table) {
        return;
    }
    log_gametype_table(table);
    for (int count = UI_GAMETYPE_COUNT; count >= 0 && count < T5_UiGametypeMax; count++) {
        char index[12];
        snprintf(index, sizeof(index), "%i", count);
        const char* name = StringTable_Lookup(table, 4, index, 0);
        if (!name || !*name) {
            return;
        }
        const char* localized = StringTable_Lookup(table, 4, index, 1);
        const char* splitscreen = StringTable_Lookup(table, 4, index, 5);
        strncpy(UI_GAMETYPE_NAME(count), name, T5_UiGametypeNameSize - 1);
        UI_GAMETYPE_NAME(count)[T5_UiGametypeNameSize - 1] = 0;
        strncpy(UI_GAMETYPE_LOCALIZED(count), localized ? localized : "",
                T5_UiGametypeLocalizedSize - 1);
        UI_GAMETYPE_LOCALIZED(count)[T5_UiGametypeLocalizedSize - 1] = 0;
        UI_GAMETYPE_SPLITSCREEN(count) = splitscreen && !I_stricmp(splitscreen, "YES");
        UI_GAMETYPE_COUNT = count + 1;
        printf(T5INFO "Added %s (%s) to the gametype list.", name,
               UI_GAMETYPE_LOCALIZED(count));
    }
}

static void sync_party_map(void* uiContext) {
    if (!mapSyncMenuCount) {
        return;
    }
    bool lobbyHosted = party_is_local_host(T5_LobbyParty);
    bool gameHosted = party_is_local_host(T5_GameParty);
    // The host left the lobby (e.g. to the main menu); the sync must not carry over to
    // the next one.
    if (!lobbyHosted && !gameHosted) {
        clear_party_map_sync();
        return;
    }
    // Before the close check, so a selection made in the frame the menu closes still
    // goes out.
    send_party_map_if_changed(lobbyHosted, gameHosted);
    prune_closed_map_sync_menus(uiContext);
}

void Menu_PaintAll_Hook(void* a1, void* uiContext) {
    static bool unbanAllWarned;
    Menu_PaintAll_Trampoline(a1, uiContext);
    if (!uiContext) {
        return;
    }
    update_wager_match();

    // Also safe in a match: the game only touches the ban list from the main thread,
    // banClient included, because server commands wait for the server frame (0x3F0650)
    // and then run on the main thread at 0x39C258 (traced over direct calls only).
    // Menu_PaintAll is expected to run there too (see show_game_messages), so the flag
    // stays set until it does. The guard is only needed in a match: the lobby callers
    // below rely on the UI-thread rule. Before restore_bans, so nothing comes back in the
    // same frame.
    if (hostMenu.unbanAllPending) {
        if (Sys_IsMainThread()) {
            hostMenu.unbanAllPending = false;
            unban_all();
        } else if (!unbanAllWarned) {
            unbanAllWarned = true;
            printf(T5WARNING
                   "Menu_PaintAll is not on the main thread, Unban All is waiting.");
        }
    }

    // Messages left over from a match the host never spawned in name its players. Kept
    // while a server runs, so the ones queued during the host's loading screen survive.
    // The spawn flag is reset too, it would otherwise carry over until the next server
    // script load. Nothing sets it while no server runs.
    if (!SV_CLIENTS) {
        gameMessageTail = gameMessageHead;
        hostSpawned = false;
    }

    // The stats buffer and dvars are written on the main thread, where the game's stats
    // code runs, so it never sees half loaded stats. The menu thread only loads the file.
    if (hostMenu.statsLoadPending) {
        __lwsync();
        stats_file_apply_loaded();
        hostMenu.prestige = *(uint8_t*)T5_Stats_Prestige;
        apply_loaded_profile_names(&hostMenu.loadedProfile);
        // The file buffer is released only once it was read.
        __lwsync();
        hostMenu.statsLoadPending = false;
    }

    bool inGame = Dvar_GetBool("cl_ingame");
    // Keyboards stay pending until the game's previous one is unloaded.
    if (hostMenu.changeNamePending && game_keyboard_closed()) {
        hostMenu.changeNamePending = false;
        open_name_keyboard(inGame);
    }
    if (hostMenu.resetNamePending) {
        hostMenu.resetNamePending = false;
        if (hostMenu.originalName[0]) {
            host_menu_set_local_name(hostMenu.originalName);
        }
        hostMenu.originalName[0] = 0;
    }

    // Lobby menus can only be opened from the UI thread, and party state is only changed
    // from it.
    if (!inGame) {
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
        restore_bans();
        enforce_game_party_bans();
        if (hostMenu.selectMapPending) {
            hostMenu.selectMapPending = false;
            int openMenuCount = UI_OPEN_MENU_COUNT(uiContext);
            Menus_OpenByName(0, uiContext, "select_map");
            arm_party_map_sync(uiContext, openMenuCount);
        }
        if (hostMenu.selectGametypePending) {
            hostMenu.selectGametypePending = false;
            int openMenuCount = UI_OPEN_MENU_COUNT(uiContext);
            extend_gametype_list();
            Menus_OpenByName(0, uiContext, "select_game_mode");
            arm_party_map_sync(uiContext, openMenuCount);
        }
        sync_party_map(uiContext);
        keep_matchmaking_playlist();
        if (hostMenu.saveStatsKeyboardPending && game_keyboard_closed()) {
            hostMenu.saveStatsKeyboardPending = false;
            host_menu_ui_open_stats_keyboard();
        }
    } else {
        // The sync belongs to the lobby the menu was opened in.
        clear_party_map_sync();
        // The same command as End Game in the pause menu. -1 is the menu index the game
        // sends for a menu that isn't a script menu.
        if (hostMenu.endGamePending) {
            hostMenu.endGamePending = false;
            char buffer[64];
            sprintf(buffer, "cmd mr %i -1 endround\n", *(int*)T5_ClServerId);
            Cbuf_AddText(0, buffer);
        }
        show_game_messages();
        update_game_keyboard();
        release_closed_keyboard();
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
    hostSpawned = false;
    anti_god_class_reset();
    // Before the gametype scripts run, they read xblive_wagermatch at their start.
    if (wager_match_wanted()) {
        force_wager_match();
    } else {
        restore_wager_match();
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
        // Scr_NotifyNumInternal computes the owner id with the same lookup.
        // cg.clientNum is the host's server slot.
        if (!hostSpawned && CG_CLIENTINFO_BASE &&
            notifyListOwnerId ==
                FindEntityId(SCRIPTINSTANCE_SERVER, CG_CLIENTNUM, CLASS_NUM_ENTITY, 0)) {
            hostSpawned = true;
            printf(T5INFO "Host spawn detected (client %d), kill feed enabled.",
                   CG_CLIENTNUM);
        }
    }
    return VM_Notify_Trampoline(inst, notifyListOwnerId, stringValue, top);
}

// Each kind of blocked attempt is reported at most once per client in this window, so a
// client repeating it can't flood the host's kill feed.
#define REPORT_MESSAGE_COOLDOWN_US (5 * 1000 * 1000)
static system_time_t reportTime[HOST_MENU_REPORT_COUNT][HOST_MENU_MAX_CLIENTS];

static const char* const reportText[HOST_MENU_REPORT_COUNT] = {
    [HOST_MENU_REPORT_END_GAME] = "tried to end the game",
    [HOST_MENU_REPORT_FREEZE] = "tried to freeze the game",
    [HOST_MENU_REPORT_GODMODE_CLASS] = "had a godmode class (fixed)",
    [HOST_MENU_REPORT_GODMODE_KILL] = "was killed for using a godmode class",
};

void host_menu_report_client(int clientNum, const char* name, HostMenuReport report) {
    if (clientNum < 0 || clientNum >= HOST_MENU_MAX_CLIENTS || report < 0 ||
        report >= HOST_MENU_REPORT_COUNT) {
        return;
    }
    if (!name) {
        if (!CG_CLIENTINFO_BASE) {
            return;
        }
        name = CG_CLIENT_NAME(clientNum);
    }
    if (!*name) {
        return;
    }

    system_time_t now = sys_time_get_system_time();
    system_time_t* last = &reportTime[report][clientNum];
    if (*last && now - *last < REPORT_MESSAGE_COOLDOWN_US) {
        return;
    }
    *last = now;
    host_menu_game_message("^1%s %s", name, reportText[report]);
}

// Blocks "mr <n> <menu> endround" from everyone except the host. The command name is
// matched with the game's I_stricmp like ClientCommand does, so "MR" can't slip past. The
// response is compared case-sensitively by the script, so other casings of "endround" are
// harmless.
bool host_menu_client_command_ok(int clientNum, const char* cmd) {
    if (I_stricmp(cmd, "mr") != 0) {
        return true;
    }

    char arg3[37] = {0};
    SV_Cmd_ArgvBuffer(3, arg3, sizeof(arg3));

    if (strcmp(arg3, "endround") == 0 && !is_host(clientNum)) {
        host_menu_report_client(clientNum, NULL, HOST_MENU_REPORT_END_GAME);
        return false;
    }
    return true;
}

// Server frame: runs the queued kick, scorelimit, class check and keyboard requests.
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
    if (hostMenu.antiGodClassCheckPending) {
        hostMenu.antiGodClassCheckPending = false;
        anti_god_class_check_clients();
    }
    drop_keyboard_of_left_client();
    send_keyboard_input(ent);
    anti_god_class_client_frame(ent);
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
            hostMenu.antiGodClassCheckPending = false;
            hostMenu.endGamePending = false;
            keyboardState = KEYBOARD_IDLE;
        }
        // Lobby menus and kicks can't happen in game, so don't let them carry over to the
        // next lobby.
        else {
            hostMenu.selectMapPending = false;
            hostMenu.selectGametypePending = false;
            hostMenu.saveStatsKeyboardPending = false;
            hostMenu.lobbyKickMember = -1;
        }
        host_menu_ui_run_stats_actions(inGame);
        // The list choice only applies while both parties are up; each lobby (and the one
        // after a match) starts on the Lobby list.
        if (inGame || !host_menu_lobby_parties_both_active()) {
            hostMenu.lobbyShowGameParty = false;
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
    t5nmi(UI_OpenKeyboard);
    t5nmi(UI_UpdateKeyboard);
    t5nmi(Dvar_GetBool);
    t5nmi(R_TextHeight);
    t5nmi(R_TextWidth);
    t5nmi(SL_GetString);
    t5nmi(R_RegisterFont);
    t5nmi(Party_KickMember);
    t5nmi(Live_IsLocalXuid);
    t5nmi(Party_UpdateState);
    t5nmi(Com_IsPublicOnlineGame);
    t5nmi(Playlist_SetRotationEntry);
    t5nmi(Party_BanName);
    t5nmi(Party_IsBanned);
    t5nmi(ClanTag_Format);
    t5nmi(RankIcon_Lookup);
    t5nmi(Dvar_GetString);
    t5nmi(Dvar_SetStringByName);
    t5nmi(Dvar_SetInt);
    t5nmi(Session_SetPlaylist);
    t5nmi(StringTable_Find);
    t5nmi(StringTable_Lookup);
    t5nmi(CL_ControllerIndexFromClientNum);
    t5nmi(GamerProfile_UpdateDvarsFromProfile);
    t5nmi(GamerProfile_UpdateProfileFromDvars);
    t5nmi(CG_GameMessage);
    t5nmi(Sys_IsMainThread);
    t5nmi(Scr_Notify);
    t5nmi(FindEntityId);
}

static int init_host_menu_hooks() {
    t5nmhi(PadGetData);
    t5nmhi(VM_Notify);
    t5nmhi(Menu_PaintAll);
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
    if ((res = client_command_init()) < 0) {
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

// Replaces the game's sanitizer, which turns ^ into a space (see name_filter).
void ClanName_Sanitize_Hook() {
    const char* clanName = Dvar_GetString("clanName");
    if (!clanName) {
        return;
    }

    char sanitized[STATS_FILE_CLAN_MAX];
    name_filter(clanName, sanitized, sizeof(sanitized), true, false);
    if (strcmp(sanitized, clanName) == 0) {
        return;
    }
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
    // Optional protection, the menu works without it.
    if ((res = anti_god_class_init()) < 0) {
        hostMenu.antiGodClasses = false;
        printf(T5ERROR "Anti Godmode Classes init failed (0x%08X), hid the option.", res);
    }

    sys_ppu_thread_t id;
    if ((res = sys_ppu_thread_create(&id, host_menu_thread, 0, 1000, 0x4000, 0,
                                     "[Silinium] host menu")) != CELL_OK) {
        return res;
    }

    ready = true;
    return 0;
}
