#include "host_menu_ui.h"

#include <ppu_intrinsics.h>
#include <stdio.h>
#include <string.h>
#include <sys/timer.h>

#include "host_menu.h"
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

static void set_prestige() { host_menu_write_prestige(hostMenu.prestige); }

static void change_name() {
    if (!hostMenu.originalName[0]) {
        strcpy(hostMenu.originalName, (char*)T5_PlayerName2);
    }
    hostMenu.changeNamePending = true;
}

static void reset_name() {
    if (hostMenu.originalName[0]) {
        strcpy((char*)T5_PlayerName1, hostMenu.originalName);
        strcpy((char*)T5_PlayerName2, hostMenu.originalName);
    }
    hostMenu.originalName[0] = 0;
}

static void open_player_menu() { hostMenu.current = HOST_MENU_KICK; }

// Only on a slot that holds someone, the modal acts on whoever is in it.
static void open_player_modal() {
    int member = hostMenu.cursor[HOST_MENU_KICK];
    bool inGame = Dvar_GetBool("cl_ingame");
    if (inGame) {
        if (!CG_CLIENTINFO_BASE || !*CG_CLIENT_NAME(member)) {
            return;
        }
        printf(T5INFO "Player menu: slot %i (%s), local client %i (%s).", member,
               CG_CLIENT_NAME(member), CG_CLIENTNUM, (char*)T5_PlayerName2);
    } else {
        int party = host_menu_lobby_party();
        if (!party || !PARTY_MEMBER_USED(party, member)) {
            return;
        }
    }
    hostMenu.cursor[HOST_MENU_PLAYER] = 0;
    hostMenu.playerModalInGame = inGame;
    hostMenu.current = HOST_MENU_PLAYER;
}

// The modal's slot is a member index in the lobby and a client num in game, so it can't
// survive the switch.
static void close_stale_player_modal() {
    if (hostMenu.current == HOST_MENU_PLAYER &&
        Dvar_GetBool("cl_ingame") != hostMenu.playerModalInGame) {
        hostMenu.current = HOST_MENU_KICK;
    }
}

// The join handler (0x212FE8) skips the ban check for party id 1 (GameParty), so a ban
// there would only kick.
static bool ban_allowed(bool inGame) {
    return inGame || host_menu_lobby_party() == T5_LobbyParty;
}

static void queue_kick(bool ban) {
    int member = hostMenu.cursor[HOST_MENU_KICK];
    if (Dvar_GetBool("cl_ingame")) {
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
    int party = host_menu_lobby_party();
    if (!party || hostMenu.lobbyKickMember != -1 || !PARTY_MEMBER_USED(party, member)) {
        return;
    }
    if (ban && !ban_allowed(false)) {
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
// Keyboard
// ---------------------------------------------------------------------------

static void keyboard_callback(int localClientNum, const char16* text, int length) {
    char name[17];
    int i;
    for (i = 0; i < length && i < (int)sizeof(name) - 1 && text[i]; i++) {
        name[i] = text[i] > 0x7F ? '?' : (char)text[i];
    }
    name[i] = 0;

    if (!*name) {
        return;
    }

    strcpy((char*)T5_PlayerName1, name);
    strcpy((char*)T5_PlayerName2, name);
}

void host_menu_ui_open_name_keyboard() {
    UI_OpenKeyboard(0, keyboardTitle, keyboardPreset, 16, keyboard_callback);
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
           (!option->visible || option->visible(inGame));
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

void host_menu_ui_init() {
    memset(&hostMenu, 0, sizeof(hostMenu));
    hostMenu.current = HOST_MENU_MAIN;
    hostMenu.kickClient = -1;
    hostMenu.lobbyKickMember = -1;
    hostMenu.showControls = true;
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
    add_option(HOST_MENU_MAIN, "Depatch Bounces", HOST_MENU_TYPE_BOOL,
               &hostMenu.depatchBounces, 0, 0, depatch_bounces);
    add_option(HOST_MENU_MAIN, "Disable DLC", HOST_MENU_TYPE_BOOL, &hostMenu.disableDlc,
               0, 0, disable_dlc);
    add_option(HOST_MENU_MAIN, "Quick Recovery", HOST_MENU_TYPE_ACTION, 0, 0, 0,
               host_menu_write_recovery)
        ->lobbyOnly = true;
    add_option(HOST_MENU_MAIN, "Prestige Unlocks", HOST_MENU_TYPE_BOOL,
               &hostMenu.prestigeUnlocks, 0, 0, prestige_unlocks);
    add_option(HOST_MENU_MAIN, "Prestige", HOST_MENU_TYPE_INT, &hostMenu.prestige, 0, 16,
               set_prestige)
        ->lobbyOnly = true;
    add_option(HOST_MENU_MAIN, "Change Name", HOST_MENU_TYPE_ACTION, 0, 0, 0, change_name)
        ->lobbyOnly = true;
    add_option(HOST_MENU_MAIN, "Reset Name", HOST_MENU_TYPE_ACTION, 0, 0, 0, reset_name);
    add_option(HOST_MENU_MAIN, "End Game", HOST_MENU_TYPE_ACTION, 0, 0, 0, end_game)
        ->inGameOnly = true;
    add_option(HOST_MENU_MAIN, "Player Menu", HOST_MENU_TYPE_SUBMENU, 0, 0, 0,
               open_player_menu);

    hostMenu.title[HOST_MENU_KICK] = "Players";
    hostMenu.parent[HOST_MENU_KICK] = HOST_MENU_MAIN;
    for (int i = 0; i < HOST_MENU_MAX_CLIENTS; i++) {
        add_option(HOST_MENU_KICK, "", HOST_MENU_TYPE_ACTION, 0, 0, 0,
                   open_player_modal);  // names are drawn live
    }

    hostMenu.title[HOST_MENU_PLAYER] =
        "Player";  // the modal shows the player's name instead
    hostMenu.parent[HOST_MENU_PLAYER] = HOST_MENU_KICK;
    add_option(HOST_MENU_PLAYER, "Kick", HOST_MENU_TYPE_ACTION, 0, 0, 0, kick_selected);
    add_option(HOST_MENU_PLAYER, "Ban", HOST_MENU_TYPE_ACTION, 0, 0, 0, ban_selected)
        ->visible = ban_allowed;
}

// ---------------------------------------------------------------------------
// Input (runs in the menu thread)
// ---------------------------------------------------------------------------

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
    int m = hostMenu.current;
    bool inGame = Dvar_GetBool("cl_ingame");
    HostMenuOption* option = &hostMenu.options[m][hostMenu.cursor[m]];

    // The cursor may sit on an option that got hidden since (e.g. the match started).
    if (!option_visible(option, inGame)) {
        move_cursor(m, 1, inGame);
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

static const char* player_name(int party, bool inGame, int i) {
    const char* name = "Not In Game";
    if (inGame && CG_CLIENTINFO_BASE) {
        name = CG_CLIENT_NAME(i);
    } else if (party) {
        name = PARTY_MEMBER_USED(party, i) ? PARTY_MEMBER_NAME(party, i) : "";
    }
    return *name ? name : "No Client Connected";
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
// Width of the icon and level column (scaled to 0.5), the same for every occupied row so
// the tags and names all start at one x.
static float playerRankColumn;
// The rank and prestige playerRankIcons[i] was looked up for, -1 before the first lookup,
// so the rank icon table is only searched when they change.
static int playerIconRanks[HOST_MENU_MAX_CLIENTS] = {-1, -1, -1, -1, -1, -1, -1, -1, -1,
                                                     -1, -1, -1, -1, -1, -1, -1, -1, -1};
static int playerIconPrestiges[HOST_MENU_MAX_CLIENTS];
// The player list box's widest width this visit to the list, 0 outside it.
static int playerListWidth;

// Rank icon size and the gap after the icon and after the level.
#define RANK_ICON_SIZE 15.0f
#define RANK_GAP 4.0f

// "[tag]", "" without a tag. In the lobby colored like in the lobby list (the clan tag
// feature of the party member), in game without any color like on the scoreboard, so the
// player's own color codes are dropped too. out must hold CLAN_TAG_MAX bytes.
static void player_clan_tag(int party, bool inGame, int i, char* out) {
    out[0] = 0;
    if (inGame) {
        // An empty slot may still hold the tag of whoever left.
        if (!CG_CLIENTINFO_BASE || !*CG_CLIENT_NAME(i)) return;
        // Reads stay within the tag field. The game keeps only 7 bytes of the tag in
        // game, color codes included, so a tag the lobby shows in full can be cut here.
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
                if (c + 4 >= size) break;
                c += 4 + (unsigned char)tag[c + 4];
                continue;
            }
            clan[length++] = tag[c];
        }
        clan[length] = 0;
        if (*clan) sprintf(out, "[%s]", clan);
        return;
    }

    if (!party || !PARTY_MEMBER_USED(party, i)) return;
    const char* clan = PARTY_MEMBER_CLAN(party, i);
    if (!*clan) return;
    // The clan field's size is unknown (at most 0x9C bytes), so a long clan is cut, not
    // the ], in the plain and the colored form: "[^%i" + "^7]" + NUL take at most 8 bytes
    // of out.
    char cut[CLAN_TAG_MAX - 7];
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
        if (!CG_CLIENTINFO_BASE) return;
        for (int i = 0; i < HOST_MENU_MAX_CLIENTS; i++)
            if (!strcmp(CG_CLIENT_NAME(i), host)) {
                player_clan_tag(0, true, i, out);
                return;
            }
        return;
    }
    static const int parties[] = {T5_LobbyParty, T5_GameParty};
    for (int p = 0; p < 2; p++) {
        int party = parties[p];
        if (!*(int*)(party + T5_PartyActiveOffset)) continue;
        for (int i = 0; i < HOST_MENU_MAX_CLIENTS; i++)
            if (PARTY_MEMBER_USED(party, i) &&
                !strcmp(PARTY_MEMBER_NAME(party, i), host)) {
                player_clan_tag(party, false, i, out);
                return;
            }
    }
}

// Host, map and gametype, one box each below the controls box.
static void draw_session_info(bool inGame) {
    const char* map = inGame ? "" : dvar_string("ui_mapname");
    const char* gametype = inGame ? "" : dvar_string("ui_gametype");
    if (!*map) map = dvar_string("mapname");
    if (!*gametype) gametype = dvar_string("g_gametype");

    const char* host = dvar_string("party_hostname");
    char tag[CLAN_TAG_MAX] = "";
    if (*host) host_clan_tag(inGame, host, tag);
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
    for (int i = 0; i < 2; i++) draw_text_box(lines[i], 10, 20 + (i + 2) * step, 0.5f);
}

// The rankId (level - 1) and prestige like the lobby list and the scoreboard show them,
// false for an empty slot. Both are clamped, a modded peer can send any value.
static bool player_rank(int party, bool inGame, int i, int* rank, int* prestige) {
    if (inGame) {
        if (!CG_CLIENTINFO_BASE || !*CG_CLIENT_NAME(i)) return false;
        *rank = CG_CLIENT_RANK(i);
        *prestige = CG_CLIENT_PRESTIGE(i);
    } else {
        if (!party || !PARTY_MEMBER_USED(party, i)) return false;
        *rank = PARTY_MEMBER_RANK(party, i);
        *prestige = PARTY_MEMBER_PRESTIGE(party, i);
    }
    if (*rank < 0) *rank = 0;
    if (*rank > 254) *rank = 254;  // level fits in 3 digits
    if (*prestige < 0) *prestige = 0;
    if (*prestige > 255) *prestige = 255;
    return true;
}

// Fills row i's rank icon and level text.
static void build_player_rank(int party, bool inGame, int i) {
    int rank, prestige;
    if (!player_rank(party, inGame, i, &rank, &prestige)) {
        playerLevels[i][0] = 0;
        return;
    }
    if (rank != playerIconRanks[i] || prestige != playerIconPrestiges[i]) {
        playerRankIcons[i] = 0;
        RankIcon_Lookup(rank, prestige, &playerRankIcons[i]);
        playerIconRanks[i] = rank;
        playerIconPrestiges[i] = prestige;
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

// Right below the selected player's row, or above it when it would reach past the bottom
// of the list box.
static void draw_player_modal(int party, bool inGame, float listBottom) {
    int member = hostMenu.cursor[HOST_MENU_KICK];
    const char* name = player_name(party, inGame, member);
    float height = visible_option_count(HOST_MENU_PLAYER, inGame) * 16.5f + 40.0f;
    float rowY = member * 16.5f + 258;
    float y = rowY + 6;
    if (y + height > listBottom) {
        y = rowY - 16 - height;
    }

    draw_shader(560, y, 180, height, modalBackground, whiteMaterial);
    draw_outline(560, y, 180, height, 2, accent);
    draw_text(name, get_aligned_x(650, 0.45f, name, ALIGN_CENTER), y + 20, 0.45f, accent);
    draw_options(HOST_MENU_PLAYER, inGame, 570, 730, y + 38);
}

// Builds every row's rank, tag and their widths for the draw loop and returns the list
// box width: wide enough for the longest "icon level [tag]name" row plus 10 padding each
// side, at least the default 300 and at most 600, so a long tag from a modded peer can't
// push the box off screen (its row overflows instead). The width is an even whole number
// so the box centered at 650 stays pixel-aligned.
static int build_player_rows(int party, bool inGame) {
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
            if (w > levelWidth) levelWidth = w;
        }
    }
    playerRankColumn = RANK_ICON_SIZE + RANK_GAP + levelWidth + RANK_GAP;

    float widest = 0;
    for (int i = 0; i < HOST_MENU_MAX_CLIENTS; i++) {
        float w = (*playerLevels[i] ? playerRankColumn : 0) + playerTagWidths[i] +
                  R_TextWidth(player_name(party, inGame, i), 0x7FFFFFFF, font) * 0.5f;
        if (w > widest) widest = w;
    }
    // Round up to an even whole number so left = 650 - width / 2 and the accent
    // borders land on whole pixels.
    int width = (int)widest + 20;
    if (width < widest + 20) width++;  // ceil (widest >= 0)
    width = (width + 1) & ~1;          // round up to even
    if (width < 300) width = 300;
    if (width > 600) width = 600;
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
        return;
    }

    close_stale_player_modal();
    int m = hostMenu.current;
    bool inGame = Dvar_GetBool("cl_ingame");
    // The player modal is drawn over the player list, so the box keeps showing the list.
    bool players = m == HOST_MENU_KICK || m == HOST_MENU_PLAYER;
    int boxMenu = players ? HOST_MENU_KICK : m;
    int party = (players && !inGame) ? host_menu_lobby_party() : 0;
    bool notHost = players && !inGame && !party;

    float height = (notHost ? 1 : visible_option_count(boxMenu, inGame)) * 16.5f + 48.0f;
    // The player list grows around the center at 650, so titles and the modal stay
    // centered. It keeps its widest width until the list is left, so names don't shift
    // sideways when a long-named player leaves.
    float width = 300;
    if (players && !notHost) {
        int w = build_player_rows(party, inGame);
        if (w > playerListWidth) playerListWidth = w;
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
    draw_text(hostMenu.title[boxMenu],
              get_aligned_x(650, 0.45f, hostMenu.title[boxMenu], ALIGN_CENTER), 238,
              0.45f, accent);
    if (players) {
        if (notHost) {
            draw_text_box("\x02 - Back", 10, 20, 0.5f);
        } else if (m == HOST_MENU_PLAYER) {
            draw_text_box(
                "\x14\x15\x16\x17 - Scroll/Rotate | \x01 - Select | \x02 - Back", 10, 20,
                0.5f);
        } else {
            draw_text_box("\x14\x15 - Scroll | \x01 - Select | \x02 - Back", 10, 20,
                          0.5f);
        }
        draw_session_info(inGame);
        if (notHost) {
            draw_text("Only the party host can manage players", 510, 258, 0.5f, white);
            return;
        }
        // Tag and name are drawn apart, so the ^7 that ends a colored tag doesn't turn
        // the selected name white.
        for (int i = 0; i < HOST_MENU_MAX_CLIENTS; i++) {
            const float* color = hostMenu.cursor[HOST_MENU_KICK] == i ? accent : white;
            float y = i * 16.5f + 258;
            float x = left + 10;
            // Icon, level and tag/name each start at a fixed x, also without an icon.
            if (*playerLevels[i]) {
                if (playerRankIcons[i])
                    draw_shader(x, y - RANK_ICON_SIZE, RANK_ICON_SIZE, RANK_ICON_SIZE,
                                white, playerRankIcons[i]);
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

    draw_text_box("\x14\x15\x16\x17 - Scroll/Rotate | \x01 - Select | \x02 - Exit", 10,
                  20, 0.5f);
    draw_session_info(inGame);
    draw_options(m, inGame, 510, 790, 258);
}
