#include "host_menu_ui.h"

#include <ppu_intrinsics.h>
#include <stdio.h>
#include <string.h>
#include <sys/sys_time.h>
#include <sys/timer.h>

#include "anti_god_class.h"
#include "host_menu.h"
#include "notify.h"
#include "t5.h"
#include "utils.h"

#define FONT "fonts/normalFont"

HostMenu hostMenu;

static const char* savedPlaylistRules[64];
static int savedPlaylistMax[64];
static char playlistRuleBuffer[64][0x100];

static const float white[4] = {1, 1, 1, 1};
static const float accent[4] = {0.9882f, 0.6667f, 0.4549f, 1};
static const float border[4] = {0.9882f, 0.6667f, 0.4549f, 0.2f};
static const float background[4] = {0, 0, 0, 0.45f};
static const float modalBackground[4] = {0, 0, 0, 0.85f};
static const float textBoxBackground[4] = {0, 0, 0, 0.2f};

typedef struct DisplayName {
    const char* internal;
    const char* display;
} DisplayName;

static const DisplayName mapNames[] = {
    {"mp_array", "Array"},
    {"mp_cairo", "Havana"},
    {"mp_cosmodrome", "Launch"},
    {"mp_cracked", "Cracked"},
    {"mp_crisis", "Crisis"},
    {"mp_duga", "Grid"},
    {"mp_firingrange", "Firing Range"},
    {"mp_hanoi", "Hanoi"},
    {"mp_havoc", "Jungle"},
    {"mp_mountain", "Summit"},
    {"mp_nuked", "Nuketown"},
    {"mp_radiation", "Radiation"},
    {"mp_russianbase", "WMD"},
    {"mp_villa", "Villa"},
    {"mp_berlinwall2", "Berlin Wall"},
    {"mp_discovery", "Discovery"},
    {"mp_kowloon", "Kowloon"},
    {"mp_stadium", "Stadium"},
    {"mp_gridlock", "Convoy"},
    {"mp_hotel", "Hotel"},
    {"mp_outskirts", "Stockpile"},
    {"mp_zoo", "Zoo"},
    {"mp_golfcourse", "Hazard"},
    {"mp_area51", "Hangar 18"},
    {"mp_drivein", "Drive-In"},
    {"mp_silo", "Silo"},
};

static const DisplayName gametypeNames[] = {
    {"tdm", "Team Deathmatch"}, {"dm", "Free-for-All"},   {"sd", "Search & Destroy"},
    {"dom", "Domination"},      {"koth", "Headquarters"}, {"ctf", "Capture the Flag"},
    {"sab", "Sabotage"},        {"dem", "Demolition"},    {"hlnd", "Sticks and Stones"},
    {"gun", "Gun Game"},        {"shrp", "Sharpshooter"}, {"oic", "One in the Chamber"},
};

static const char16 keyboardTitle[] = {'C', 'h', 'a', 'n', 'g', 'e',
                                       ' ', 'N', 'a', 'm', 'e', 0};
static const char16 keyboardPreset[] = {0};
static const char16 statsKeyboardTitle[] = {'S', 'a', 'v', 'e', ' ', 'S',
                                            't', 'a', 't', 's', 0};

// Registered once on the first draw.
static void* font;
static void* whiteMaterial;
static void* arrowMaterial;

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

typedef enum TextAlign { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } TextAlign;

static float get_aligned_x(float x, float scale, const char* text, TextAlign align) {
    if (align == ALIGN_CENTER) {
        return x - (R_TextWidth(text, 0x7FFFFFFF, font) / 2) * scale;
    }
    if (align == ALIGN_RIGHT) {
        return x - R_TextWidth(text, 0x7FFFFFFF, font) * scale;
    }
    return x;
}

static void draw_shader(float x, float y, float w, float h, const float* color,
                        void* material) {
    R_AddCmdDrawStretchPic(x, y, w, h, 0, 0, 1, 1, color, material);
}

static void draw_text(const char* text, float x, float y, float scale,
                      const float* color) {
    R_AddCmdDrawText(text, 0x7FFFFFFF, font, x, y, scale, scale, 0, color, 0);
}

// Rectangle border, drawn inside x, y, w, h.
static void draw_outline(float x, float y, float w, float h, float thickness,
                         const float* color) {
    draw_shader(x, y, w, thickness, color, whiteMaterial);                  // top
    draw_shader(x, y + h - thickness, w, thickness, color, whiteMaterial);  // bottom
    draw_shader(x, y, thickness, h, color, whiteMaterial);                  // left
    draw_shader(x + w - thickness, y, thickness, h, color, whiteMaterial);  // right
}

// Text with a 2px border and a translucent black background.
static void draw_text_box(const char* text, float x, float y, float scale) {
    float h = R_TextHeight(font) * scale;
    float w = R_TextWidth(text, 0x7FFFFFFF, font) * scale;
    float top = y - h;

    draw_outline(x - 1, top - 3, w + 8, h + 6, 2, border);
    draw_shader(x + 1, top - 1, w + 4, h + 2, textBoxBackground, whiteMaterial);

    draw_text(text, x + 3, y, scale, white);
}

static const char* dvar_string(const char* name) {
    dvar_s* dvar = Dvar_FindVar(name);
    return dvar && dvar->current.string ? dvar->current.string : "";
}

// Unknown names are shown as they are.
static const char* lookup_name(const DisplayName* names, int count,
                               const char* internal) {
    if (!*internal) {
        return "-";
    }
    for (int i = 0; i < count; i++) {
        if (!strcmp(names[i].internal, internal)) {
            return names[i].display;
        }
    }
    return internal;
}

// ---------------------------------------------------------------------------
// Option callbacks
// ---------------------------------------------------------------------------

void host_menu_set_party_dvars(bool connectToOthers, int minPlayers, int pregameTimer,
                               int connectTimeout, int maxTeamDiff) {
    dvar_s* dvar;
    if ((dvar = Dvar_FindVar("party_connectToOthers"))) {
        dvar->current.enabled = connectToOthers;
    }
    if ((dvar = Dvar_FindVar("party_minplayers"))) {
        dvar->current.integer = minPlayers;
    }
    if ((dvar = Dvar_FindVar("party_pregamestarttimerlength"))) {
        dvar->current.integer = pregameTimer;
    }
    if ((dvar = Dvar_FindVar("party_connectTimeout"))) {
        dvar->current.integer = connectTimeout;
    }
    if ((dvar = Dvar_FindVar("party_maxTeamDiff"))) {
        dvar->current.integer = maxTeamDiff;
    }
}

void host_menu_set_bounce_dvars(bool depatched) {
    dvar_s* slowdown = *(dvar_s**)T5_DvarPtr_BBA19C;
    dvar_s* fallHeight = *(dvar_s**)T5_DvarPtr_BBA1A4;

    if (slowdown) {
        slowdown->current.enabled = !depatched;
    }
    if (fallHeight) {
        fallHeight->current.value = depatched ? 9999.0f : 300.0f;
    }
}

int host_menu_get_scorelimit() {
    if (hostMenu.maxPlayers < 5) {
        return 3000;
    }
    if (hostMenu.maxPlayers < 7) {
        return 5000;
    }
    if (hostMenu.maxPlayers < 13) {
        return 7500;
    }
    return 10000;
}

static void nothing() {}

static void force_host() {
    static const InstructionPatch patch = {T5_ForceHost_Patch, 0x4182FD0C, PPC_NOP};
    if (hostMenu.forceHost) {
        host_menu_set_party_dvars(false, hostMenu.playersToStart, 1, 1, 9);
    } else {
        host_menu_set_party_dvars(true, 4, 10, 8000, 1);
    }
    apply_instruction_patch(&patch, hostMenu.forceHost);
}

// The hook only forces values when the game writes them, so apply a new player count
// right away.
static void set_players_to_start() {
    if (hostMenu.forceHost) {
        host_menu_set_party_dvars(false, hostMenu.playersToStart, 1, 1, 9);
    }
}

// Appends "party_maxplayers N" to every playlist rule string.
static void set_max_players() {
    bool applied = false;
    for (int i = 0; i < 64; i++) {
        const char* rules = *(const char**)PLAYLIST_ENTRY(i);
        if (!rules) {
            savedPlaylistRules[i] = 0;
            continue;
        }

        // Anything other than our buffer is a fresh rule string from the game (first
        // apply or playlist reload).
        if (rules != playlistRuleBuffer[i]) {
            savedPlaylistRules[i] = rules;
            savedPlaylistMax[i] = *(int*)(PLAYLIST_ENTRY(i) + 0x58);
        }

        int len = snprintf(playlistRuleBuffer[i], sizeof(playlistRuleBuffer[i]),
                           "%s party_maxplayers %i", savedPlaylistRules[i],
                           hostMenu.maxPlayers);
        if (len < 0 || len >= (int)sizeof(playlistRuleBuffer[i])) {
            // Too long for our buffer: keep the game's rules untouched rather than cut
            // them off.
            *(const char**)PLAYLIST_ENTRY(i) = savedPlaylistRules[i];
            *(int*)(PLAYLIST_ENTRY(i) + 0x58) = savedPlaylistMax[i];
            continue;
        }

        *(int*)(PLAYLIST_ENTRY(i) + 0x58) = HOST_MENU_MAX_CLIENTS;
        *(char**)PLAYLIST_ENTRY(i) = playlistRuleBuffer[i];
        applied = true;
    }
    // Only let the TDM scorelimit follow Max Players when some playlist actually got it.
    hostMenu.maxPlayersSet = applied;
}

static void depatch_bounces() {
    static const InstructionPatch patches[] = {
        {T5_Bounce_Patch1, 0x41820084, PPC_NOP},
        {T5_Bounce_Patch2, 0x409A0018, PPC_NOP},
    };
    for (int i = 0; i < sizeof(patches) / sizeof(*patches); i++) {
        apply_instruction_patch(&patches[i], hostMenu.depatchBounces);
    }
    host_menu_set_bounce_dvars(hostMenu.depatchBounces);
}

// The plevel branches either become a nop (branch to locked) or an unconditional b
// (branch past the lock).
static const InstructionPatch prestigeUnlockPatches[] = {
    {T5_PrestigeUnlock_ItemLocked, 0x41800008, PPC_NOP},
    {T5_PrestigeUnlock_ItemParentLocked, 0x41800008, PPC_NOP},
    {T5_PrestigeUnlock_ServerItemLocked, 0x41990008, PPC_NOP},
    {T5_PrestigeUnlock_WeaponOptionLocked, 0x41840008, PPC_NOP},
    {T5_PrestigeUnlock_WeaponOptionPlevel, 0x409E01FC, 0x480001FC},
    {T5_PrestigeUnlock_ItemPurchased, 0x419DFF50, PPC_NOP},
    {T5_PrestigeUnlock_ItemPurchase, 0x419D02A4, PPC_NOP},
    {T5_PrestigeUnlock_ItemPurchaseClassified, 0x41910018, PPC_NOP},
    {T5_PrestigeUnlock_ItemClassified, 0x40850010, 0x48000010},
    {T5_PrestigeUnlock_ClanTagLocked, 0x409900A4, 0x480000A4},
    {T5_PrestigeUnlock_EmblemBgLocked1, 0x419C0088, PPC_NOP},
    {T5_PrestigeUnlock_EmblemBgLocked2, 0x41990030, PPC_NOP},
    {T5_PrestigeUnlock_EmblemBgLockedUi, 0x4199FFB8, PPC_NOP},
    {T5_PrestigeUnlock_EmblemIconLocked, 0x40990080, 0x48000080},
    {T5_PrestigeUnlock_EmblemIconPurchased, 0x40980038, 0x48000038},
    {T5_PrestigeUnlock_EmblemIconPurchase, 0x4098016C, 0x4800016C},
    {T5_PrestigeUnlock_EmblemSaveCheck, 0x40840028, 0x48000028},
    {T5_PrestigeUnlock_EmblemIconList1, 0x40800054, 0x48000054},
    {T5_PrestigeUnlock_EmblemIconList2, 0x409C008C, 0x4800008C},
    {T5_PrestigeUnlock_EmblemIconLockedUi, 0x4199FFC4, PPC_NOP},
};

// Lets everything locked by prestige be bought and used at any prestige (rank locks and
// costs stay), turning it off restores the checks. What was bought or picked meanwhile is
// in the stats and stays.
static void prestige_unlocks() {
    for (int i = 0; i < sizeof(prestigeUnlockPatches) / sizeof(*prestigeUnlockPatches);
         i++) {
        apply_instruction_patch(&prestigeUnlockPatches[i], hostMenu.prestigeUnlocks);
    }
}

static void select_map() {
    sys_timer_usleep(200000);  // let X go before the game sees buttons again
    hostMenu.open = false;
    hostMenu.selectMapPending = true;
}

static void select_gametype() {
    sys_timer_usleep(200000);  // let X go before the game sees buttons again
    hostMenu.open = false;
    hostMenu.selectGametypePending = true;
}

static void end_game() {
    sys_timer_usleep(200000);  // let X go before the game sees buttons again
    hostMenu.open = false;
    hostMenu.endGamePending = true;
}

static const char* dlcDvars[] = {"ui_showDLCMaps", "dlc2", "dlc3", "dlc4", "dlc5"};
#define DLC_DVAR_COUNT (sizeof(dlcDvars) / sizeof(*dlcDvars))
static bool dlcOriginal[DLC_DVAR_COUNT];

// Clears the DLC map dvars. One that is set again was (re)set by the game, so that's what
// to restore later.
void host_menu_hide_dlc_maps() {
    for (int i = 0; i < DLC_DVAR_COUNT; i++) {
        dvar_s* dvar = Dvar_FindVar(dlcDvars[i]);
        if (dvar && dvar->current.enabled) {
            dlcOriginal[i] = true;
            dvar->current.enabled = false;
        }
    }
}

// Hides the DLC maps by clearing their dvars, turning it off restores what they were
// before.
static void disable_dlc() {
    for (int i = 0; i < DLC_DVAR_COUNT; i++) {
        dvar_s* dvar = Dvar_FindVar(dlcDvars[i]);
        if (hostMenu.disableDlc) {
            dlcOriginal[i] = dvar && dvar->current.enabled;
        } else if (dvar) {
            dvar->current.enabled = dlcOriginal[i];
        }
    }
    if (hostMenu.disableDlc) {
        host_menu_hide_dlc_maps();
    }
}

// The check writes the clients' stats, so it runs on the server thread.
static void anti_god_classes() {
    hostMenu.antiGodClassCheckPending = hostMenu.antiGodClasses;
}

// Without its hooks the check would run on stats that were never fixed.
static bool anti_god_classes_visible(const HostMenuOption* option, bool inGame) {
    return anti_god_class_available();
}

static void set_prestige() { host_menu_write_prestige(hostMenu.prestige); }

// The keyboard only works while the menu is closed, so it is opened by Menu_PaintAll.
static void change_name() {
    if (!hostMenu.originalName[0]) {
        strcpy(hostMenu.originalName, (char*)T5_PlayerName2);
    }
    sys_timer_usleep(200000);  // let X go before the game sees buttons again
    hostMenu.open = false;
    hostMenu.changeNamePending = true;
}

static void reset_name() { hostMenu.resetNamePending = true; }

static void open_player_menu() { hostMenu.current = HOST_MENU_KICK; }

static void open_account_menu() { hostMenu.current = HOST_MENU_ACCOUNT; }

static void open_patches_menu() { hostMenu.current = HOST_MENU_PATCHES; }

// Only the host can act on players: in game the host runs the listen server, in the lobby
// it hosts the lobby's party.
static bool can_manage_players(bool inGame) {
    return inGame ? Dvar_GetBool("sv_running") : host_menu_lobby_party() != 0;
}

// Whether slot i holds someone: a client num in game, a member index of party in the
// lobby.
static bool player_slot_used(int party, bool inGame, int i) {
    if (inGame) {
        return CG_CLIENTINFO_BASE && *CG_CLIENT_NAME(i);
    }
    return party && PARTY_MEMBER_USED(party, i);
}

// The player list only shows occupied slots. The option index is the slot.
static bool player_slot_visible(const HostMenuOption* option, bool inGame) {
    int party = inGame ? 0 : host_menu_lobby_view_party();
    return player_slot_used(party, inGame, option - hostMenu.options[HOST_MENU_KICK]);
}

static void show_status(const char* text);

// Only on a slot that holds someone, the modal acts on whoever is in it.
static void open_player_modal() {
    int member = hostMenu.cursor[HOST_MENU_KICK];
    bool inGame = Dvar_GetBool("cl_ingame");
    if (!can_manage_players(inGame)) {
        return;
    }
    if (inGame) {
        if (!player_slot_used(0, true, member)) {
            return;
        }
        hostMenu.playerModalParty = 0;
    } else {
        int party = host_menu_lobby_party();
        if (!player_slot_used(party, false, member)) {
            return;
        }
        // The kick removes every slot at the member's netadr, so a split-screen player on
        // this console can't be kicked without the host.
        if (Live_IsLocalXuid(PARTY_MEMBER_XUID(party, member))) {
            show_status("Players on this console can't be kicked");
            return;
        }
        hostMenu.playerModalParty = party;
    }
    hostMenu.cursor[HOST_MENU_PLAYER] = 0;
    hostMenu.playerModalInGame = inGame;
    hostMenu.current = HOST_MENU_PLAYER;
}

// The modal's slot is a member index in the lobby and a client num in game, so it can't
// survive the switch. The same slot in the other party is someone else, so it also closes
// when the shown party changes (one of the two went away), when host rights are lost and
// when the player leaves (the row it was drawn at is gone).
static void close_stale_player_modal() {
    if (hostMenu.current != HOST_MENU_PLAYER) {
        return;
    }
    bool inGame = Dvar_GetBool("cl_ingame");
    if (inGame != hostMenu.playerModalInGame || !can_manage_players(inGame) ||
        (!inGame && host_menu_lobby_view_party() != hostMenu.playerModalParty) ||
        !player_slot_used(hostMenu.playerModalParty, inGame,
                          hostMenu.cursor[HOST_MENU_KICK])) {
        hostMenu.current = HOST_MENU_KICK;
    }
}

static void queue_kick(bool ban) {
    int member = hostMenu.cursor[HOST_MENU_KICK];
    bool inGame = Dvar_GetBool("cl_ingame");
    if (!can_manage_players(inGame)) {
        return;
    }
    if (inGame) {
        if (!CG_CLIENTINFO_BASE || member == CG_CLIENTNUM) {
            return;
        }
        // Slot and ban flag share one value, so the game thread never sees one without
        // the other.
        hostMenu.kickClient = (signed char)(member | (ban ? HOST_MENU_KICK_BAN : 0));
        return;
    }

    // Remember who was picked, so the kick doesn't hit whoever is in the slot by the time
    // it runs.
    int party = hostMenu.playerModalParty;
    if (!party || host_menu_lobby_party() != party || hostMenu.lobbyKickMember != -1 ||
        !PARTY_MEMBER_USED(party, member)) {
        return;
    }
    hostMenu.lobbyKickParty = party;
    strncpy(hostMenu.lobbyKickName, PARTY_MEMBER_NAME(party, member),
            sizeof(hostMenu.lobbyKickName) - 1);
    hostMenu.lobbyKickName[sizeof(hostMenu.lobbyKickName) - 1] = 0;
    // Party and name have to be visible before the member that marks the request ready.
    // The ban flag goes in the member itself, like in game, so the UI thread never sees
    // one without the other.
    __lwsync();
    hostMenu.lobbyKickMember = (signed char)(member | (ban ? HOST_MENU_KICK_BAN : 0));
}

static void kick_selected() {
    queue_kick(false);
    hostMenu.current = HOST_MENU_KICK;
}

static void ban_selected() {
    queue_kick(true);
    hostMenu.current = HOST_MENU_KICK;
}

// ---------------------------------------------------------------------------
// Save / Load Stats
// ---------------------------------------------------------------------------

#define STATUS_DURATION_US (4 * 1000 * 1000)

// Result of the last stats action, shown below the session info for a few seconds.
// Written by the menu thread and drawn by the UI thread, which copies it bounded, so a
// text being rewritten can't be read past its end.
static char statusText[64];
static volatile system_time_t statusTime;

static void show_status(const char* text) {
    snprintf(statusText, sizeof(statusText), "%s", text);
    __lwsync();
    statusTime = sys_time_get_system_time();
}

// As a PS3 notification, or in the menu if webMAN didn't take it. Blocks while webMAN is
// asked, so only the menu thread reports.
static void report_stats_result(const char* text, bool success) {
    char popup[96];
    snprintf(popup, sizeof(popup), "Silinium: %s", text);
    bool shown = success ? notify_popup(popup, NOTIFY_ICON_INFO, NOTIFY_SOUND_OK)
                         : notify_popup(popup, NOTIFY_ICON_CAUTION, NOTIFY_SOUND_ERROR);
    if (!shown) {
        show_status(text);
    }
}

// Set by the keyboard callback on the UI thread, reported by the menu thread.
static volatile bool saveSkippedPending;

// The list of HOST_MENU_STATS, rebuilt each time it is opened and after a delete while
// the UI thread draws it. The directory is read into the buffer the options don't point
// to, then the options are switched over row by row, so each drawn row is a complete old
// or new name. The buffer written was last shown two rebuilds ago.
#define STATS_LIST_MAX 20
static char statsNames[2][STATS_LIST_MAX][STATS_FILE_NAME_MAX + 1];
static int statsNamesActive;  // buffer the options of HOST_MENU_STATS point into
static int statsCount;

static void open_stats_modal();

// In place instead of add_option, so the drawn option count never drops to 0 meanwhile.
static void set_stats_option(int i, const char* text, void (*func)(void)) {
    HostMenuOption* option = &hostMenu.options[HOST_MENU_STATS][i];
    option->text = text;
    option->type = HOST_MENU_TYPE_ACTION;
    option->value = 0;
    option->min = 0;
    option->max = 0;
    option->func = func;
}

static void build_stats_list() {
    int next = !statsNamesActive;
    int count = stats_file_list(statsNames[next], STATS_LIST_MAX);
    // The names have to be visible before the options pointing to them.
    __lwsync();
    for (int i = 0; i < count; i++) {
        set_stats_option(i, statsNames[next][i], open_stats_modal);
    }
    if (!count) {
        set_stats_option(0, "No Saved Stats", nothing);
    }
    __lwsync();
    hostMenu.optionCount[HOST_MENU_STATS] = count ? count : 1;
    statsNamesActive = next;
    statsCount = count;
    int last = hostMenu.optionCount[HOST_MENU_STATS] - 1;
    if (hostMenu.cursor[HOST_MENU_STATS] > last) {
        hostMenu.cursor[HOST_MENU_STATS] = last;
    }
}

// The keyboard only works while the menu is closed, so it is opened by Menu_PaintAll.
static void save_stats() {
    sys_timer_usleep(200000);  // let X go before the game sees buttons again
    hostMenu.open = false;
    hostMenu.saveStatsKeyboardPending = true;
}

static void open_stats_menu() {
    build_stats_list();
    hostMenu.current = HOST_MENU_STATS;
}

static void open_stats_modal() {
    if (hostMenu.cursor[HOST_MENU_STATS] >= statsCount) {
        return;
    }
    hostMenu.cursor[HOST_MENU_STATS_FILE] = 0;
    hostMenu.current = HOST_MENU_STATS_FILE;
}

// The name is copied, so a list rebuilt meanwhile doesn't change the file acted on.
static void queue_stats_action(HostMenuStatsAction action) {
    int i = hostMenu.cursor[HOST_MENU_STATS];
    if (i >= statsCount) {
        return;
    }
    if (hostMenu.statsAction != HOST_MENU_STATS_NONE) {
        report_stats_result("Busy: another stats action is pending", false);
        return;
    }
    strcpy(hostMenu.statsActionName, statsNames[statsNamesActive][i]);
    hostMenu.statsAction = action;
}

static void load_selected_stats() {
    queue_stats_action(HOST_MENU_STATS_LOAD);
    hostMenu.current = HOST_MENU_ACCOUNT;
}

static void delete_selected_stats() {
    queue_stats_action(HOST_MENU_STATS_DELETE);
    hostMenu.current = HOST_MENU_STATS;
}

// The list and its modal are lobby only like the option that opens them.
static void close_stale_stats_menu(bool inGame) {
    if (inGame && (hostMenu.current == HOST_MENU_STATS ||
                   hostMenu.current == HOST_MENU_STATS_FILE)) {
        hostMenu.current = HOST_MENU_ACCOUNT;
    }
}

static void run_save_stats() {
    char text[64];
    uint32_t flags = hostMenu.prestigeUnlocks ? STATS_FILE_PRESTIGE_UNLOCKS : 0;
    int err = stats_file_save(hostMenu.saveStatsName, &hostMenu.saveStatsProfile, flags);
    if (err) {
        snprintf(text, sizeof(text), "Save failed: %s", stats_file_error_text(err));
    } else {
        snprintf(text, sizeof(text), "Stats '%s' saved", hostMenu.saveStatsName);
    }
    report_stats_result(text, !err);
}

// Prestige Unlocks is taken over as saved: stats with prestige items picked through it
// lose them on the next lobby join without it (see DDL_SetFromString_Hook).
static void run_load_stats() {
    char text[64];
    uint32_t flags;
    int err = stats_file_load(hostMenu.statsActionName, &hostMenu.loadedProfile, &flags);
    if (err) {
        snprintf(text, sizeof(text), "Load failed: %s", stats_file_error_text(err));
        report_stats_result(text, false);
        return;
    }
    // Stats and clan tag have to be visible before the flag that marks them ready.
    __lwsync();
    hostMenu.statsLoadPending = true;
    hostMenu.prestigeUnlocks = flags & STATS_FILE_PRESTIGE_UNLOCKS;
    prestige_unlocks();
    snprintf(text, sizeof(text), "Stats '%s' loaded", hostMenu.statsActionName);
    report_stats_result(text, true);
}

static void run_delete_stats() {
    char text[64];
    int err = stats_file_delete(hostMenu.statsActionName);
    if (err) {
        snprintf(text, sizeof(text), "Delete failed: %s", stats_file_error_text(err));
    } else {
        snprintf(text, sizeof(text), "Stats '%s' deleted", hostMenu.statsActionName);
    }
    report_stats_result(text, !err);
    build_stats_list();
}

void host_menu_ui_run_stats_actions(bool inGame) {
    if (saveSkippedPending) {
        saveSkippedPending = false;
        report_stats_result("Save skipped: previous save still pending", false);
    }
    // Queued actions stay queued until Menu_PaintAll took the loaded stats over.
    if (hostMenu.statsLoadPending) {
        return;
    }
    if (hostMenu.saveStatsPending) {
        // Pairs with the one in the keyboard callback: name and clan tag are ready.
        __lwsync();
        run_save_stats();
        hostMenu.saveStatsPending = false;
    }
    // Only the menu thread queues and runs these, so no barrier is needed.
    HostMenuStatsAction action = hostMenu.statsAction;
    if (action == HOST_MENU_STATS_LOAD && !inGame) {
        run_load_stats();
    } else if (action == HOST_MENU_STATS_LOAD) {
        report_stats_result("Load skipped: stats can only be loaded in the lobby", false);
    } else if (action == HOST_MENU_STATS_DELETE) {
        run_delete_stats();
    }
    hostMenu.statsAction = HOST_MENU_STATS_NONE;
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

static void keyboard_callback(int localClientNum, const char16* text, int length) {
    host_menu_name_keyboard_closed();
    char name[17];
    int i;
    for (i = 0; i < length && i < (int)sizeof(name) - 1 && text[i]; i++) {
        name[i] = text[i] > 0x7F ? '?' : (char)text[i];
    }
    name[i] = 0;

    if (!*name) {
        return;
    }

    host_menu_set_local_name(name);
}

void host_menu_ui_open_name_keyboard() {
    UI_OpenKeyboard(0, keyboardTitle, keyboardPreset, 16, keyboard_callback);
}

// Runs on the UI thread, so the clan tag and class names are read here rather than in the
// menu thread.
static void stats_keyboard_callback(int localClientNum, const char16* text, int length) {
    if (hostMenu.saveStatsPending) {
        saveSkippedPending = true;
        return;
    }
    char entered[STATS_FILE_NAME_MAX + 1];
    int i;
    for (i = 0; i < length && i < (int)sizeof(entered) - 1 && text[i]; i++) {
        entered[i] = text[i] > 0x7F ? '_' : (char)text[i];
    }
    entered[i] = 0;
    if (!*entered) {
        return;
    }

    stats_file_sanitize_name(entered, hostMenu.saveStatsName);
    host_menu_read_profile_names(&hostMenu.saveStatsProfile);
    // Name, clan tag and class names have to be visible before the flag that marks them
    // ready.
    __lwsync();
    hostMenu.saveStatsPending = true;
}

void host_menu_ui_open_stats_keyboard() {
    UI_OpenKeyboard(0, statsKeyboardTitle, keyboardPreset, STATS_FILE_NAME_MAX,
                    stats_keyboard_callback);
}

// ---------------------------------------------------------------------------
// Menu structure
// ---------------------------------------------------------------------------

static HostMenuOption* add_option(HostMenuId m, const char* text, HostMenuOptionType type,
                                  void* value, signed char min, signed char max,
                                  void (*func)(void)) {
    HostMenuOption* option = &hostMenu.options[m][hostMenu.optionCount[m]++];
    option->text = text;
    option->type = type;
    option->value = value;
    option->min = min;
    option->max = max;
    option->func = func;
    return option;
}

static bool option_visible(const HostMenuOption* option, bool inGame) {
    return !(option->lobbyOnly && inGame) && !(option->inGameOnly && !inGame) &&
           (!option->visible || option->visible(option, inGame));
}

static int visible_option_count(int m, bool inGame) {
    int count = 0;
    for (int i = 0; i < hostMenu.optionCount[m]; i++) {
        if (option_visible(&hostMenu.options[m][i], inGame)) {
            count++;
        }
    }
    return count;
}

// The row the cursor's option is drawn in: the visible options before it.
static int visible_row(int m, bool inGame) {
    int row = 0;
    for (int i = 0; i < hostMenu.cursor[m]; i++) {
        if (option_visible(&hostMenu.options[m][i], inGame)) {
            row++;
        }
    }
    return row;
}

// Moves the cursor by step (+1/-1) until it lands on a visible option, wrapping around.
static void move_cursor(int m, int step, bool inGame) {
    for (int n = 0; n < hostMenu.optionCount[m]; n++) {
        hostMenu.cursor[m] += step;
        if (hostMenu.cursor[m] < 0) {
            hostMenu.cursor[m] = hostMenu.optionCount[m] - 1;
        } else if (hostMenu.cursor[m] > hostMenu.optionCount[m] - 1) {
            hostMenu.cursor[m] = 0;
        }
        if (option_visible(&hostMenu.options[m][hostMenu.cursor[m]], inGame)) {
            return;
        }
    }
}

// Moves the cursor off an option that got hidden: to the next visible one below, or the
// last one above when there is none below. No wrap, so a player leaving from the last row
// doesn't send the cursor to the top. Stays put when nothing is visible.
static void move_cursor_off_hidden(int m, bool inGame) {
    for (int i = hostMenu.cursor[m] + 1; i < hostMenu.optionCount[m]; i++) {
        if (option_visible(&hostMenu.options[m][i], inGame)) {
            hostMenu.cursor[m] = i;
            return;
        }
    }
    for (int i = hostMenu.cursor[m] - 1; i >= 0; i--) {
        if (option_visible(&hostMenu.options[m][i], inGame)) {
            hostMenu.cursor[m] = i;
            return;
        }
    }
}

void host_menu_ui_init() {
    memset(&hostMenu, 0, sizeof(hostMenu));
    hostMenu.current = HOST_MENU_MAIN;
    hostMenu.kickClient = -1;
    hostMenu.lobbyKickMember = -1;
    hostMenu.showControls = true;
    hostMenu.antiGodClasses = true;
    hostMenu.playersToStart = 4;
    hostMenu.maxPlayers = 12;

    hostMenu.title[HOST_MENU_MAIN] = "Main Menu";
    hostMenu.parent[HOST_MENU_MAIN] = -1;
    add_option(HOST_MENU_MAIN, "Show Controls", HOST_MENU_TYPE_BOOL,
               &hostMenu.showControls, 0, 0, nothing);
    add_option(HOST_MENU_MAIN, "Force Host", HOST_MENU_TYPE_BOOL, &hostMenu.forceHost, 0,
               0, force_host);
    add_option(HOST_MENU_MAIN, "Players to Start", HOST_MENU_TYPE_INT,
               &hostMenu.playersToStart, 1, HOST_MENU_MAX_CLIENTS, set_players_to_start);
    add_option(HOST_MENU_MAIN, "Max Players", HOST_MENU_TYPE_INT, &hostMenu.maxPlayers, 1,
               HOST_MENU_MAX_CLIENTS, set_max_players);
    add_option(HOST_MENU_MAIN, "Select Map", HOST_MENU_TYPE_ACTION, 0, 0, 0, select_map)
        ->lobbyOnly = true;
    add_option(HOST_MENU_MAIN, "Select Gametype", HOST_MENU_TYPE_ACTION, 0, 0, 0,
               select_gametype)
        ->lobbyOnly = true;
    add_option(HOST_MENU_MAIN, "End Game", HOST_MENU_TYPE_ACTION, 0, 0, 0, end_game)
        ->inGameOnly = true;
    add_option(HOST_MENU_MAIN, "Patches", HOST_MENU_TYPE_SUBMENU, 0, 0, 0,
               open_patches_menu);
    add_option(HOST_MENU_MAIN, "Account Settings", HOST_MENU_TYPE_SUBMENU, 0, 0, 0,
               open_account_menu);
    add_option(HOST_MENU_MAIN, "Player Menu", HOST_MENU_TYPE_SUBMENU, 0, 0, 0,
               open_player_menu);

    hostMenu.title[HOST_MENU_PATCHES] = "Patches";
    hostMenu.parent[HOST_MENU_PATCHES] = HOST_MENU_MAIN;
    add_option(HOST_MENU_PATCHES, "Depatch Bounces", HOST_MENU_TYPE_BOOL,
               &hostMenu.depatchBounces, 0, 0, depatch_bounces);
    add_option(HOST_MENU_PATCHES, "Disable DLC", HOST_MENU_TYPE_BOOL,
               &hostMenu.disableDlc, 0, 0, disable_dlc);
    add_option(HOST_MENU_PATCHES, "Anti Godmode Classes", HOST_MENU_TYPE_BOOL,
               &hostMenu.antiGodClasses, 0, 0, anti_god_classes)
        ->visible = anti_god_classes_visible;

    hostMenu.title[HOST_MENU_ACCOUNT] = "Account Settings";
    hostMenu.parent[HOST_MENU_ACCOUNT] = HOST_MENU_MAIN;
    add_option(HOST_MENU_ACCOUNT, "Quick Recovery", HOST_MENU_TYPE_ACTION, 0, 0, 0,
               host_menu_write_recovery)
        ->lobbyOnly = true;
    add_option(HOST_MENU_ACCOUNT, "Prestige Unlocks", HOST_MENU_TYPE_BOOL,
               &hostMenu.prestigeUnlocks, 0, 0, prestige_unlocks);
    add_option(HOST_MENU_ACCOUNT, "Prestige", HOST_MENU_TYPE_INT, &hostMenu.prestige, 0,
               16, set_prestige)
        ->lobbyOnly = true;
    add_option(HOST_MENU_ACCOUNT, "Save Stats", HOST_MENU_TYPE_ACTION, 0, 0, 0,
               save_stats)
        ->lobbyOnly = true;
    add_option(HOST_MENU_ACCOUNT, "Load Stats", HOST_MENU_TYPE_SUBMENU, 0, 0, 0,
               open_stats_menu)
        ->lobbyOnly = true;
    add_option(HOST_MENU_ACCOUNT, "Change Name", HOST_MENU_TYPE_ACTION, 0, 0, 0,
               change_name);
    add_option(HOST_MENU_ACCOUNT, "Reset Name", HOST_MENU_TYPE_ACTION, 0, 0, 0,
               reset_name);

    hostMenu.title[HOST_MENU_KICK] = "Players";
    hostMenu.parent[HOST_MENU_KICK] = HOST_MENU_MAIN;
    for (int i = 0; i < HOST_MENU_MAX_CLIENTS; i++) {
        add_option(HOST_MENU_KICK, "", HOST_MENU_TYPE_ACTION, 0, 0, 0,
                   open_player_modal)  // names are drawn live
            ->visible = player_slot_visible;
    }

    hostMenu.title[HOST_MENU_PLAYER] =
        "Player";  // the modal shows the player's name instead
    hostMenu.parent[HOST_MENU_PLAYER] = HOST_MENU_KICK;
    add_option(HOST_MENU_PLAYER, "Kick", HOST_MENU_TYPE_ACTION, 0, 0, 0, kick_selected);
    add_option(HOST_MENU_PLAYER, "Ban", HOST_MENU_TYPE_ACTION, 0, 0, 0, ban_selected);

    hostMenu.title[HOST_MENU_STATS] = "Load Stats";
    hostMenu.parent[HOST_MENU_STATS] = HOST_MENU_ACCOUNT;  // filled by open_stats_menu

    hostMenu.title[HOST_MENU_STATS_FILE] = "Stats";  // the modal shows the file name
    hostMenu.parent[HOST_MENU_STATS_FILE] = HOST_MENU_STATS;
    add_option(HOST_MENU_STATS_FILE, "Load", HOST_MENU_TYPE_ACTION, 0, 0, 0,
               load_selected_stats);
    add_option(HOST_MENU_STATS_FILE, "Delete", HOST_MENU_TYPE_ACTION, 0, 0, 0,
               delete_selected_stats);
}

// ---------------------------------------------------------------------------
// Input (runs in the menu thread)
// ---------------------------------------------------------------------------

// Triangle switches between the lobby's two parties, only in the list itself (the modal
// acts on a member of the shown one).
static bool can_switch_lists(int m, bool inGame) {
    return m == HOST_MENU_KICK && !inGame && host_menu_lobby_parties_both_active();
}

// Square clears the ban list, only in the list itself and only while it holds a name. Not
// limited to the host: in game the list is still the one the lobby checks afterwards.
static bool can_unban_all(int m, bool inGame) {
    return m == HOST_MENU_KICK && (inGame || host_menu_lobby_view_party()) &&
           host_menu_ban_count() > 0;
}

void host_menu_ui_handle_input() {
    uint16_t digital1 = hostMenu.pad.button[CELL_PAD_BTN_OFFSET_DIGITAL1];
    uint16_t digital2 = hostMenu.pad.button[CELL_PAD_BTN_OFFSET_DIGITAL2];

    if (!hostMenu.open) {
        if (digital1 == CELL_PAD_CTRL_R3 && digital2 == CELL_PAD_CTRL_L1) {
            hostMenu.current = HOST_MENU_MAIN;
            hostMenu.open = true;
            sys_timer_usleep(200000);
        }
        return;
    }

    close_stale_player_modal();
    bool inGame = Dvar_GetBool("cl_ingame");
    close_stale_stats_menu(inGame);
    int m = hostMenu.current;
    HostMenuOption* option = &hostMenu.options[m][hostMenu.cursor[m]];

    // The cursor may sit on an option that got hidden since (e.g. the match started, or
    // the selected player left).
    if (!option_visible(option, inGame)) {
        move_cursor_off_hidden(m, inGame);
        option = &hostMenu.options[m][hostMenu.cursor[m]];
    }

    if (digital1 & CELL_PAD_CTRL_UP) {
        move_cursor(m, -1, inGame);
        sys_timer_usleep(150000);
    } else if (digital1 & CELL_PAD_CTRL_DOWN) {
        move_cursor(m, 1, inGame);
        sys_timer_usleep(150000);
    } else if (digital2 & CELL_PAD_CTRL_CROSS) {
        if (option_visible(option, inGame)) {
            if (option->type == HOST_MENU_TYPE_BOOL) {
                *(bool*)option->value ^= 1;
            }
            option->func();
        }
        sys_timer_usleep(200000);
    } else if (digital2 & CELL_PAD_CTRL_CIRCLE) {
        if (m == HOST_MENU_MAIN) {
            sys_timer_usleep(200000);
            hostMenu.open = false;
        } else {
            hostMenu.current = hostMenu.parent[m];
            sys_timer_usleep(150000);
        }
    } else if (digital2 & CELL_PAD_CTRL_TRIANGLE) {
        if (can_switch_lists(m, inGame)) {
            hostMenu.lobbyShowGameParty ^= 1;
        }
        sys_timer_usleep(200000);
    } else if (digital2 & CELL_PAD_CTRL_SQUARE) {
        if (can_unban_all(m, inGame)) {
            hostMenu.unbanAllPending = true;
        }
        sys_timer_usleep(200000);
    } else if (digital1 & CELL_PAD_CTRL_LEFT) {
        if (option->type == HOST_MENU_TYPE_INT) {
            signed char* value = (signed char*)option->value;
            if (--*value < option->min) {
                *value = option->max;
            }
        }
        sys_timer_usleep(125000);
    } else if (digital1 & CELL_PAD_CTRL_RIGHT) {
        if (option->type == HOST_MENU_TYPE_INT) {
            signed char* value = (signed char*)option->value;
            if (++*value > option->max) {
                *value = option->min;
            }
        }
        sys_timer_usleep(125000);
    }
}

// ---------------------------------------------------------------------------
// Drawing (called from the Menu_PaintAll hook)
// ---------------------------------------------------------------------------

// Only for slots player_slot_used accepts, the list doesn't show the others.
static const char* player_name(int party, bool inGame, int i) {
    if (inGame) {
        return CG_CLIENTINFO_BASE ? CG_CLIENT_NAME(i) : "";
    }
    return PARTY_MEMBER_NAME(party, i);
}

// Size of the buffer ClanTag_Format writes to, like the lobby list's.
#define CLAN_TAG_MAX 0x80

// The player list's tags and their widths (scaled to 0.5), built once per frame by
// build_player_rows.
static char playerTags[HOST_MENU_MAX_CLIENTS][CLAN_TAG_MAX];
static float playerTagWidths[HOST_MENU_MAX_CLIENTS];
// Each row's rank icon (0 when the game has none) and level text ("" for an empty slot).
static void* playerRankIcons[HOST_MENU_MAX_CLIENTS];
static char playerLevels[HOST_MENU_MAX_CLIENTS][4];
// Width of the icon and level column (scaled to 0.5). Rows with a level are indented by
// it, so the tags and names all start at one x; rows without one start at the left edge.
static float playerRankColumn;
// The rank + 1 and prestige playerRankIcons[i] was looked up for, rank 0 before the
// first lookup, so the rank icon table is only searched when they change. Reset to 0 by
// build_player_rows when cl_ingame or mapname changes.
static int playerIconRanks[HOST_MENU_MAX_CLIENTS];
static int playerIconPrestiges[HOST_MENU_MAX_CLIENTS];
// cl_ingame and mapname the cached icons were looked up under. The lobby and the match
// may load the icons from different zones and the UI zone can be unloaded on map load,
// so the cache is dropped when either changes.
static bool playerIconsInGame;
static char playerIconsMap[64];
// The player list box's widest width since the list was entered or its party changed,
// 0 outside the list. Only the draw code touches these.
static int playerListWidth;
// The party the list showed last frame (0 in game), so the width is refit on a change.
static int playerListParty;

// Rank icon size and the gap after the icon and after the level.
#define RANK_ICON_SIZE 15.0f
#define RANK_GAP 4.0f
// Range of mp/rankIconTable.csv: rankId rows 0-49, prestige columns 0-15.
#define RANK_ICON_MAX_RANK 49
#define RANK_ICON_MAX_PRESTIGE 15

// "[tag]", "" without a tag. In the lobby colored like in the lobby list (the clan tag
// feature of the party member), in game without any color like on the scoreboard, so the
// player's own color codes are dropped too. out must hold CLAN_TAG_MAX bytes.
static void player_clan_tag(int party, bool inGame, int i, char* out) {
    out[0] = 0;
    if (inGame) {
        // An empty slot may still hold the tag of whoever left.
        if (!CG_CLIENTINFO_BASE || !*CG_CLIENT_NAME(i)) {
            return;
        }
        // Reads stay within the tag field. The field is 8 bytes like the lobby's, so in
        // game the tag only loses its color codes and icons, not visible characters.
        const char* tag = CG_CLIENT_CLAN(i);
        const int size = T5_CgClientClanSize;
        char clan[T5_CgClientClanSize];
        int length = 0;
        for (int c = 0; c < size && tag[c] && length < (int)sizeof(clan) - 1; c++) {
            bool caret = tag[c] == '^' && c + 1 < size;
            if (caret && tag[c + 1] >= '0' && tag[c + 1] <= '9') {
                c++;
                continue;
            }
            // Embedded icon (^\x01 / ^\x02, width, height, name length, name) is dropped
            // as a whole.
            if (caret && (tag[c + 1] == 1 || tag[c + 1] == 2)) {
                if (c + 4 >= size) {
                    break;
                }
                c += 4 + (unsigned char)tag[c + 4];
                continue;
            }
            clan[length++] = tag[c];
        }
        clan[length] = 0;
        if (*clan) {
            sprintf(out, "[%s]", clan);
        }
        return;
    }

    if (!party || !PARTY_MEMBER_USED(party, i)) {
        return;
    }
    const char* clan = PARTY_MEMBER_CLAN(party, i);
    if (!*clan) {
        return;
    }
    // The clan field is 8 bytes, so at most 7 characters are read: the read stays within
    // the field even if a peer left no NUL in it. "[^%i" + cut + "^7]" + NUL fit in out.
    char cut[T5_PartyMemberClanSize];
    snprintf(cut, sizeof(cut), "%.*s", (int)sizeof(cut) - 1, clan);
    char plain[CLAN_TAG_MAX];
    snprintf(plain, sizeof(plain), "[%s]", cut);
    strcpy(out, plain);
    ClanTag_Format(PARTY_MEMBER_CLAN_FEATURE(party, i), cut, "", plain, out);
}

// The host's tag like player_clan_tag builds it, "" if the host isn't found. The host is
// found by name, the party host index is unverified. In the lobby both parties are
// searched, also when the local player isn't the host. out must hold CLAN_TAG_MAX bytes.
static void host_clan_tag(bool inGame, const char* host, char* out) {
    out[0] = 0;
    if (inGame) {
        if (!CG_CLIENTINFO_BASE) {
            return;
        }
        for (int i = 0; i < HOST_MENU_MAX_CLIENTS; i++) {
            if (!strcmp(CG_CLIENT_NAME(i), host)) {
                player_clan_tag(0, true, i, out);
                return;
            }
        }
        return;
    }
    static const int parties[] = {T5_LobbyParty, T5_GameParty};
    for (int p = 0; p < 2; p++) {
        int party = parties[p];
        if (!host_menu_party_active(party)) {
            continue;
        }
        for (int i = 0; i < HOST_MENU_MAX_CLIENTS; i++) {
            if (PARTY_MEMBER_USED(party, i) &&
                !strcmp(PARTY_MEMBER_NAME(party, i), host)) {
                player_clan_tag(party, false, i, out);
                return;
            }
        }
    }
}

// Host, map and gametype, one box each below the controls box.
static void draw_session_info(bool inGame) {
    const char* map = inGame ? "" : dvar_string("ui_mapname");
    const char* gametype = inGame ? "" : dvar_string("ui_gametype");
    if (!*map) {
        map = dvar_string("mapname");
    }
    if (!*gametype) {
        gametype = dvar_string("g_gametype");
    }

    const char* host = dvar_string("party_hostname");
    char tag[CLAN_TAG_MAX] = "";
    if (*host) {
        host_clan_tag(inGame, host, tag);
    }
    char hostLine[CLAN_TAG_MAX + 64];
    snprintf(hostLine, sizeof(hostLine), "Host: %s%s", tag, *host ? host : "-");
    char lines[2][64];
    snprintf(lines[0], sizeof(lines[0]), "Map: %s",
             lookup_name(mapNames, sizeof(mapNames) / sizeof(*mapNames), map));
    snprintf(lines[1], sizeof(lines[1]), "Gametype: %s",
             lookup_name(gametypeNames, sizeof(gametypeNames) / sizeof(*gametypeNames),
                         gametype));

    float step = R_TextHeight(font) * 0.5f + 10;
    draw_text_box(hostLine, 10, 20 + step, 0.5f);
    for (int i = 0; i < 2; i++) {
        draw_text_box(lines[i], 10, 20 + (i + 2) * step, 0.5f);
    }
}

// The rankId (level - 1) and prestige like the lobby list and the scoreboard show them,
// false for an empty slot. Both are clamped, a modded peer can send any value. The clamp
// keeps the level in 3 digits; the rank icon lookup clamps further to the table's range.
static bool player_rank(int party, bool inGame, int i, int* rank, int* prestige) {
    if (inGame) {
        if (!CG_CLIENTINFO_BASE || !*CG_CLIENT_NAME(i)) {
            return false;
        }
        *rank = CG_CLIENT_RANK(i);
        *prestige = CG_CLIENT_PRESTIGE(i);
    } else {
        if (!party || !PARTY_MEMBER_USED(party, i)) {
            return false;
        }
        *rank = PARTY_MEMBER_RANK(party, i);
        *prestige = PARTY_MEMBER_PRESTIGE(party, i);
    }
    if (*rank < 0) {
        *rank = 0;
    }
    if (*rank > 254) {
        *rank = 254;  // level fits in 3 digits
    }
    if (*prestige < 0) {
        *prestige = 0;
    }
    if (*prestige > 255) {
        *prestige = 255;
    }
    return true;
}

// Fills row i's rank icon and level text.
static void build_player_rank(int party, bool inGame, int i) {
    int rank, prestige;
    if (!player_rank(party, inGame, i, &rank, &prestige)) {
        playerLevels[i][0] = 0;
        return;
    }
    // Whether the game's lookup handles values outside the table is unverified.
    int iconRank = rank > RANK_ICON_MAX_RANK ? RANK_ICON_MAX_RANK : rank;
    int iconPrestige =
        prestige > RANK_ICON_MAX_PRESTIGE ? RANK_ICON_MAX_PRESTIGE : prestige;
    if (iconRank + 1 != playerIconRanks[i] || iconPrestige != playerIconPrestiges[i]) {
        playerRankIcons[i] = 0;
        RankIcon_Lookup(iconRank, iconPrestige, &playerRankIcons[i]);
        playerIconRanks[i] = iconRank + 1;
        playerIconPrestiges[i] = iconPrestige;
    }
    sprintf(playerLevels[i], "%i", rank + 1);
}

// The visible options of menu m, rows starting at y, values right aligned at right.
static void draw_options(int m, bool inGame, float x, float right, float y) {
    int row = 0;
    for (int i = 0; i < hostMenu.optionCount[m]; i++) {
        HostMenuOption* option = &hostMenu.options[m][i];
        if (!option_visible(option, inGame)) {
            continue;
        }

        float rowY = row * 16.5f + y;
        draw_text(option->text, x, rowY, 0.5f, hostMenu.cursor[m] == i ? accent : white);

        char valueBuffer[8];
        const char* valueText = 0;
        switch (option->type) {
            case HOST_MENU_TYPE_SUBMENU:
                draw_shader(right - 10, rowY - 16, 15, 15, accent, arrowMaterial);
                break;
            case HOST_MENU_TYPE_INT:
                sprintf(valueBuffer, "%i", *(signed char*)option->value);
                valueText = valueBuffer;
                break;
            case HOST_MENU_TYPE_BOOL:
                // Centered like the submenu arrow, full when on, only the border when
                // off.
                if (*(bool*)option->value) {
                    draw_shader(right - 10, rowY - 13.5f, 10, 10, accent, whiteMaterial);
                } else {
                    draw_outline(right - 10, rowY - 13.5f, 10, 10, 2, accent);
                }
                break;
            default:
                break;
        }

        if (valueText) {
            draw_text(valueText, get_aligned_x(right, 0.5f, valueText, ALIGN_RIGHT), rowY,
                      0.5f, white);
        }
        row++;
    }
}

// The rows a list box needs so modalMenu, opened on any of the list's first listRows
// rows, fits below its row or, flipped above it by draw_modal, stays below the box
// titles. Flipped, the modal on row r starts at 258 + r * 16.5 - 16 - height, which
// clears the titles once it is at or below the first row's text top (242), i.e. r * 16.5
// >= height. The rows before that must fit it below: 258 + r * 16.5 + 6 + height <= rows
// * 16.5 + 248.
static int modal_list_min_rows(int modalMenu, int listRows, bool inGame) {
    float height = visible_option_count(modalMenu, inGame) * 16.5f + 40.0f;
    int firstFlip = 0;
    while (firstFlip * 16.5f < height) {
        firstFlip++;
    }
    int last = firstFlip - 1 < listRows - 1 ? firstFlip - 1 : listRows - 1;
    if (last < 0) {
        return 0;
    }
    float need = last + (16.0f + height) / 16.5f;
    int rows = (int)need;
    if (rows < need) {
        rows++;  // ceil (need >= 0)
    }
    return rows;
}

// The options of modalMenu below the selected row of listMenu, or above it when they
// would reach past the bottom of the list box. The box must be modal_list_min_rows high,
// or the flipped modal covers its titles.
static void draw_modal(int listMenu, int modalMenu, const char* title, bool inGame,
                       float listBottom) {
    float height = visible_option_count(modalMenu, inGame) * 16.5f + 40.0f;
    float rowY = visible_row(listMenu, inGame) * 16.5f + 258;
    float y = rowY + 6;
    if (y + height > listBottom) {
        y = rowY - 16 - height;
    }

    draw_shader(560, y, 180, height, modalBackground, whiteMaterial);
    draw_outline(560, y, 180, height, 2, accent);
    draw_text(title, get_aligned_x(650, 0.45f, title, ALIGN_CENTER), y + 20, 0.45f,
              accent);
    draw_options(modalMenu, inGame, 570, 730, y + 38);
}

static void draw_player_modal(int party, bool inGame, float listBottom) {
    const char* name = player_name(party, inGame, hostMenu.cursor[HOST_MENU_KICK]);
    draw_modal(HOST_MENU_KICK, HOST_MENU_PLAYER, name, inGame, listBottom);
}

// Drawn in the given left column row: below the session info while the menu is open,
// below the open hint while it is closed (Save Stats closes it for the keyboard).
static void draw_status(int row) {
    system_time_t time = statusTime;
    if (!time || sys_time_get_system_time() - time > STATUS_DURATION_US) {
        return;
    }
    // Pairs with the one in show_status: the text is read only after the time that marks
    // it ready.
    __lwsync();
    char text[sizeof(statusText)];
    memcpy(text, statusText, sizeof(text));
    text[sizeof(text) - 1] = 0;
    float step = R_TextHeight(font) * 0.5f + 10;
    draw_text_box(text, 10, 20 + row * step, 0.5f);
}

// Builds every row's rank, tag and their widths for the draw loop and returns the list
// box width: wide enough for the longest "icon level [tag]name" row plus 10 padding each
// side, at least the default 300 and at most 600, so a long tag from a modded peer can't
// push the box off screen (its row overflows instead). The width is an even whole number
// so the box centered at 650 stays pixel-aligned.
static int build_player_rows(int party, bool inGame) {
    // Compared with the last build, so a switch while the menu was closed is caught too.
    char map[sizeof(playerIconsMap)];
    snprintf(map, sizeof(map), "%s", dvar_string("mapname"));
    if (inGame != playerIconsInGame || strcmp(map, playerIconsMap)) {
        memset(playerIconRanks, 0, sizeof(playerIconRanks));
        playerIconsInGame = inGame;
        strcpy(playerIconsMap, map);
    }
    // The level column fits at least "50" (the highest level), so it doesn't shrink when
    // the last player with a two digit level leaves.
    float levelWidth = R_TextWidth("50", 0x7FFFFFFF, font) * 0.5f;
    for (int i = 0; i < HOST_MENU_MAX_CLIENTS; i++) {
        char* tag = playerTags[i];
        player_clan_tag(party, inGame, i, tag);
        playerTagWidths[i] = *tag ? R_TextWidth(tag, 0x7FFFFFFF, font) * 0.5f : 0;
        build_player_rank(party, inGame, i);
        if (*playerLevels[i]) {
            float w = R_TextWidth(playerLevels[i], 0x7FFFFFFF, font) * 0.5f;
            if (w > levelWidth) {
                levelWidth = w;
            }
        }
    }
    playerRankColumn = RANK_ICON_SIZE + RANK_GAP + levelWidth + RANK_GAP;

    float widest = 0;
    for (int i = 0; i < HOST_MENU_MAX_CLIENTS; i++) {
        if (!player_slot_used(party, inGame, i)) {
            continue;
        }
        float w = (*playerLevels[i] ? playerRankColumn : 0) + playerTagWidths[i] +
                  R_TextWidth(player_name(party, inGame, i), 0x7FFFFFFF, font) * 0.5f;
        if (w > widest) {
            widest = w;
        }
    }
    // Round up to an even whole number so left = 650 - width / 2 and the accent
    // borders land on whole pixels.
    int width = (int)widest + 20;
    if (width < widest + 20) {
        width++;  // ceil (widest >= 0)
    }
    width = (width + 1) & ~1;  // round up to even
    if (width < 300) {
        width = 300;
    }
    if (width > 600) {
        width = 600;
    }
    return width;
}

void host_menu_ui_draw() {
    if (!font) {
        font = R_RegisterFont(FONT, 0);
    }
    if (!whiteMaterial) {
        whiteMaterial = Material_RegisterHandle("white", 0);
    }
    if (!arrowMaterial) {
        arrowMaterial = Material_RegisterHandle("ui_arrow_right", 0);
    }

    if (!hostMenu.open) {
        playerListWidth = 0;
        if (hostMenu.showControls) {
            draw_text_box("Press \x05 + \x11 to Open Silinium", 10, 20, 0.5f);
        }
        draw_status(hostMenu.showControls ? 1 : 0);
        return;
    }
    draw_status(4);

    close_stale_player_modal();
    bool inGame = Dvar_GetBool("cl_ingame");
    close_stale_stats_menu(inGame);
    int m = hostMenu.current;
    // The player modal is drawn over the player list, so the box keeps showing the list.
    bool players = m == HOST_MENU_KICK || m == HOST_MENU_PLAYER;
    // The stats modal is drawn over the stats list the same way.
    bool stats = m == HOST_MENU_STATS || m == HOST_MENU_STATS_FILE;
    int boxMenu = players ? HOST_MENU_KICK : stats ? HOST_MENU_STATS : m;
    // Clients see the list too, only the host can open the player modal.
    int party = (players && !inGame) ? host_menu_lobby_view_party() : 0;
    bool notHost = players && !inGame && !party;  // not in a lobby
    bool viewOnly = players && !notHost && !can_manage_players(inGame);

    int rows = notHost ? 1 : visible_option_count(boxMenu, inGame);
    // The player list fits the modal for a full lobby, so the box doesn't change height
    // as players join or leave. "No Saved Stats" never opens the modal.
    int minRows = 0;
    if (players && !notHost) {
        minRows = modal_list_min_rows(HOST_MENU_PLAYER, HOST_MENU_MAX_CLIENTS, inGame);
    } else if (stats && statsCount) {
        minRows = modal_list_min_rows(HOST_MENU_STATS_FILE, rows, inGame);
    }
    if (rows < minRows) {
        rows = minRows;
    }
    float height = rows * 16.5f + 48.0f;
    // The player list grows around the center at 650, so titles and the modal stay
    // centered. It keeps its widest width until the list is left, so names don't shift
    // sideways when a long-named player leaves.
    float width = 300;
    if (players && !notHost) {
        if (party != playerListParty) {
            playerListParty = party;
            playerListWidth = 0;  // fit the box to the new list
        }
        int w = build_player_rows(party, inGame);
        if (w > playerListWidth) {
            playerListWidth = w;
        }
        width = playerListWidth;
    } else {
        playerListWidth = 0;
    }
    float left = 650 - width / 2;
    draw_shader(left - 2, 200, 2, height, accent, whiteMaterial);
    draw_shader(left + width - 1, 200, 2, height, accent, whiteMaterial);
    draw_shader(left, 200, width, height, background, whiteMaterial);

    draw_text("Silinium", get_aligned_x(650, 0.6f, "Silinium", ALIGN_CENTER), 220, 0.6f,
              white);
    // With both parties active the title names the list shown.
    bool bothLists = players && !inGame && host_menu_lobby_parties_both_active();
    const char* title = hostMenu.title[boxMenu];
    if (bothLists) {
        title = hostMenu.lobbyShowGameParty ? "Players (Party)" : "Players (Lobby)";
    }
    draw_text(title, get_aligned_x(650, 0.45f, title, ALIGN_CENTER), 238, 0.45f, accent);
    if (players) {
        const char* switchHint =
            can_switch_lists(m, inGame) ? " | \x04 - Switch List" : "";
        // \x03 as the Square glyph is inferred from \x01 Cross, \x02 Circle and \x04
        // Triangle (unverified).
        char unbanHint[32] = "";
        if (can_unban_all(m, inGame)) {
            snprintf(unbanHint, sizeof(unbanHint), " | \x03 - Unban All (%i)",
                     host_menu_ban_count());
        }
        char controls[128];
        if (notHost) {
            snprintf(controls, sizeof(controls), "\x02 - Back");
        } else if (viewOnly) {
            snprintf(controls, sizeof(controls), "\x14\x15 - Scroll%s%s | \x02 - Back",
                     switchHint, unbanHint);
        } else if (m == HOST_MENU_PLAYER) {
            snprintf(controls, sizeof(controls),
                     "\x14\x15\x16\x17 - Scroll/Rotate | \x01 - Select | \x02 - Back");
        } else {
            snprintf(controls, sizeof(controls),
                     "\x14\x15 - Scroll | \x01 - Select%s%s | \x02 - Back", switchHint,
                     unbanHint);
        }
        draw_text_box(controls, 10, 20, 0.5f);
        draw_session_info(inGame);
        if (notHost) {
            draw_text("Not in a lobby", 510, 258, 0.5f, white);
            return;
        }
        if (!visible_option_count(HOST_MENU_KICK, inGame)) {
            draw_text("No players", left + 10, 258, 0.5f, white);
            return;
        }
        // Tag and name are drawn apart, so the ^7 that ends a colored tag doesn't turn
        // the selected name white. Only occupied slots get a row.
        int row = 0;
        for (int i = 0; i < HOST_MENU_MAX_CLIENTS; i++) {
            if (!player_slot_used(party, inGame, i)) {
                continue;
            }
            const float* color = hostMenu.cursor[HOST_MENU_KICK] == i ? accent : white;
            float y = row++ * 16.5f + 258;
            float x = left + 10;
            // On player rows icon, level and tag/name each start at a fixed x, also
            // without an icon, so the names form one straight column. Players without a
            // level start at the left edge.
            if (*playerLevels[i]) {
                if (playerRankIcons[i]) {
                    draw_shader(x, y - RANK_ICON_SIZE, RANK_ICON_SIZE, RANK_ICON_SIZE,
                                white, playerRankIcons[i]);
                }
                draw_text(playerLevels[i], x + RANK_ICON_SIZE + RANK_GAP, y, 0.5f, color);
                x += playerRankColumn;
            }
            if (*playerTags[i]) {
                draw_text(playerTags[i], x, y, 0.5f, color);
                x += playerTagWidths[i];
            }
            draw_text(player_name(party, inGame, i), x, y, 0.5f, color);
        }
        if (m == HOST_MENU_PLAYER) {
            draw_player_modal(party, inGame, 200 + height);
        }
        return;
    }

    draw_text_box(m == HOST_MENU_MAIN
                      ? "\x14\x15\x16\x17 - Scroll/Rotate | \x01 - Select | \x02 - Exit"
                      : "\x14\x15 - Scroll | \x01 - Select | \x02 - Back",
                  10, 20, 0.5f);
    draw_session_info(inGame);
    draw_options(boxMenu, inGame, 510, 790, 258);
    if (m == HOST_MENU_STATS_FILE) {
        draw_modal(
            HOST_MENU_STATS, HOST_MENU_STATS_FILE,
            hostMenu.options[HOST_MENU_STATS][hostMenu.cursor[HOST_MENU_STATS]].text,
            inGame, 200 + height);
    }
}
