#include "host_menu.h"

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
#include "notify.h"
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

// Whether Party_RemoveMember frees the slot right away is unverified. If it doesn't, the
// banned member would be removed (and sent "endparty") every frame, so a slot just
// removed is skipped for a second. The cooldown is kept per slot, so several members
// waiting to be freed don't reset each other's. The name is compared so another player
// taking the slot in that second is still removed.
static void remove_party_member(int party, int member, const char* reason) {
    static bool logged;
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
    Party_RemoveMember(party, member, 1, reason);
    if (!logged) {
        logged = true;
        printf(T5INFO "Party_RemoveMember: slot %d still used afterwards: %d.", member,
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
// Only LobbyParty joins are checked against it, so enforce_game_party_bans covers
// GameParty.
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
    // tag to its first 4 characters right away (seen on the console), so a longer saved
    // tag doesn't survive the load.
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

void Menu_PaintAll_Hook(void* a1, void* uiContext) {
    static bool unbanAllWarned;
    Menu_PaintAll_Trampoline(a1, uiContext);
    if (!uiContext) {
        return;
    }

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
        restore_bans();
        enforce_game_party_bans();
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
        if (hostMenu.saveStatsKeyboardPending) {
            hostMenu.saveStatsKeyboardPending = false;
            host_menu_ui_open_stats_keyboard();
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
        show_game_messages();
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
            hostMenu.changeNamePending = false;
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
    t5nmi(Dvar_GetBool);
    t5nmi(R_TextHeight);
    t5nmi(R_TextWidth);
    t5nmi(SL_GetString);
    t5nmi(R_RegisterFont);
    t5nmi(Party_RemoveMember);
    t5nmi(Party_BanName);
    t5nmi(Party_IsBanned);
    t5nmi(ClanTag_Format);
    t5nmi(RankIcon_Lookup);
    t5nmi(Dvar_GetString);
    t5nmi(Dvar_SetStringByName);
    t5nmi(CL_ControllerIndexFromClientNum);
    t5nmi(GamerProfile_UpdateDvarsFromProfile);
    t5nmi(GamerProfile_UpdateProfileFromDvars);
    t5nmi(CG_GameMessage);
    t5nmi(Sys_IsMainThread);
    t5nmi(Scr_Notify);
    t5nmi(FindEntityId);
    notify_init();
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
