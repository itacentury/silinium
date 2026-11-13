#include "host_menu_ui.h"

#include <cell/atomic.h>
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
static const float border[4] = {0.4549f, 0.7765f, 0.9882f, 0.2f};
static const float modalBackground[4] = {0, 0, 0, 0.85f};
static const float textBoxBackground[4] = {0, 0, 0, 0.2f};
static const float panelBackground[4] = {0, 0, 0, 0.86f};
// Selection bar, tab underline, toggle on, modal border.
static const float selection[4] = {0.4549f, 0.7765f, 0.9882f, 1};
static const float selectedText[4] = {0.043f, 0.051f, 0.059f, 1};
// Info line and values on the right.
static const float valueColor[4] = {0.788f, 0.820f, 0.839f, 1};
// Inactive tabs and footer.
static const float mutedColor[4] = {0.667f, 0.706f, 0.729f, 1};
static const float toggleOff[4] = {0.545f, 0.588f, 0.616f, 1};
static const float divider[4] = {1, 1, 1, 0.18f};

// Panel layout in screen units. Derived from a 1280x720 mockup (factor ~0.55); start
// values, only verifiable on a console.
#define PANEL_CENTER_X 650.0f
#define PANEL_TOP 200.0f
#define PANEL_MIN_WIDTH 300
#define PANEL_MAX_WIDTH 600
#define PANEL_PADDING 9.0f
#define TITLE_HEIGHT 24.0f
#define TITLE_SCALE 0.55f
#define TITLE_SPACING 2.0f
#define INFO_HEIGHT 12.0f
#define INFO_SCALE 0.37f
#define TAB_HEIGHT 19.0f
#define TAB_SCALE 0.42f
#define TAB_GAP 12.0f
#define TAB_UNDERLINE 2.0f
#define LIST_PADDING 3.5f
#define ROW_HEIGHT 16.5f
#define ROW_SCALE 0.5f
#define FOOTER_HEIGHT 17.5f
#define FOOTER_SCALE 0.37f
#define TOGGLE_SIZE 12.0f
#define TOGGLE_BORDER 2.0f
#define ARROW_SIZE 15.0f
#define MODAL_WIDTH 180.0f
#define MODAL_TITLE_HEIGHT 20.0f
#define MODAL_TITLE_SCALE 0.45f
// Share of R_TextHeight above the baseline, used to center text vertically (unverified,
// tune on a console).
#define TEXT_CAP_RATIO 0.7f

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

// Animation durations in µs and slide distances in screen units, start values to tune on
// a console.
#define ANIM_OPEN_US 160000
#define ANIM_CLOSE_US 120000
#define ANIM_SCROLL_US 100000  // below the 150 ms scroll repeat, so it settles in time
#define ANIM_PAGE_US 140000    // tab switch, submenu in/out, panel resize, tab underline
#define ANIM_MODAL_US 120000
#define ANIM_TOGGLE_US 100000
#define ANIM_STATUS_IN_US 150000
#define ANIM_STATUS_OUT_US 300000
#define ANIM_OPEN_SLIDE 8.0f   // the panel rises by this into place
#define ANIM_PAGE_SLIDE 12.0f  // a new page's rows come in from this far left or right
#define ANIM_MODAL_SLIDE 6.0f

// Time of the frame being drawn, read once per frame so all animations agree.
static system_time_t frameTime;
// Multiplied into the alpha of everything draw_shader and draw_text draw.
static float drawAlpha = 1;

// 0..1 over duration µs from start.
static float anim_progress(system_time_t start, int duration) {
    system_time_t elapsed = frameTime - start;
    if (elapsed >= (system_time_t)duration) {
        return 1;
    }
    return elapsed > 0 ? (float)elapsed / duration : 0;
}

// Fast start, soft landing.
static float ease_out(float t) {
    float inverse = 1 - t;
    return 1 - inverse * inverse * inverse;
}

static float clamp01(float value) {
    if (value < 0) {
        return 0;
    }
    if (value > 1) {
        return 1;
    }
    return value;
}

// A value easing from `from` to `to`, started at `start`.
typedef struct Tween {
    float from;
    float to;
    system_time_t start;
} Tween;

static float tween_value(const Tween* tween, int duration) {
    float t = ease_out(anim_progress(tween->start, duration));
    return tween->from + (tween->to - tween->from) * t;
}

// Eases to target from wherever the tween is now, so a new target mid-way doesn't jump.
static void tween_to(Tween* tween, float target, int duration) {
    if (tween->to == target) {
        return;
    }
    tween->from = tween_value(tween, duration);
    tween->to = target;
    tween->start = frameTime;
}

static void tween_snap(Tween* tween, float value) {
    tween->from = value;
    tween->to = value;
    tween->start = 0;
}

// Set for the first frame after the menu opened: everything starts where it is instead of
// sliding over from where it was when the menu closed.
static bool snapAnimations;

static float tween_follow(Tween* tween, float target, int duration) {
    if (snapAnimations) {
        tween_snap(tween, target);
    } else {
        tween_to(tween, target, duration);
    }
    return tween_value(tween, duration);
}

static void mix_color(float* out, const float* a, const float* b, float t) {
    for (int i = 0; i < 4; i++) {
        out[i] = a[i] + (b[i] - a[i]) * t;
    }
}

static void draw_shader(float x, float y, float w, float h, const float* color,
                        void* material) {
    float faded[4] = {color[0], color[1], color[2], color[3] * drawAlpha};
    R_AddCmdDrawStretchPic(x, y, w, h, 0, 0, 1, 1, faded, material);
}

// Whether the ^ color codes in a text keep the alpha passed here is unverified.
static void draw_text(const char* text, float x, float y, float scale,
                      const float* color) {
    float faded[4] = {color[0], color[1], color[2], color[3] * drawAlpha};
    R_AddCmdDrawText(text, 0x7FFFFFFF, font, x, y, scale, scale, 0, faded, 0);
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

static float text_width(const char* text, float scale) {
    return R_TextWidth(text, 0x7FFFFFFF, font) * scale;
}

// Baseline that centers text of the given scale in the band top..top + height.
static float text_baseline(float top, float height, float scale) {
    return top + (height + R_TextHeight(font) * scale * TEXT_CAP_RATIO) / 2;
}

static const char* dvar_string(const char* name) {
    dvar_s* dvar = Dvar_FindVar(name);
    return dvar && dvar->current.string ? dvar->current.string : "";
}

// Unknown names are shown as they are.
static const char* lookup_name(const DisplayName* names, int count,
                               const char* internal) {
    for (int i = 0; i < count; i++) {
        if (!strcmp(names[i].internal, internal)) {
            return names[i].display;
        }
    }
    return internal;
}

// In the lobby the map and gametype picked for the next match, in game the running ones.
static const char* session_map(bool inGame) {
    const char* map = inGame ? "" : dvar_string("ui_mapname");
    return *map ? map : dvar_string("mapname");
}

static const char* session_gametype(bool inGame) {
    const char* gametype = inGame ? "" : dvar_string("ui_gametype");
    return *gametype ? gametype : dvar_string("g_gametype");
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

// Starts the close fade right away, but hides the buttons from the game until the
// pressed one is let go.
static void close_menu() {
    hostMenu.closing = true;
    sys_timer_usleep(200000);
    hostMenu.open = false;
    // open has to be visible before closing clears, the draw reads closing first.
    __lwsync();
    hostMenu.closing = false;
}

static void select_map() {
    close_menu();
    hostMenu.selectMapPending = true;
}

static void select_gametype() {
    close_menu();
    hostMenu.selectGametypePending = true;
}

static void end_game() {
    close_menu();
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
    close_menu();
    hostMenu.changeNamePending = true;
}

static void reset_name() { hostMenu.resetNamePending = true; }

// Both options are lobby only, so these show the picks for the next match.
static void map_value_text(char* out, int size) {
    const char* map =
        lookup_name(mapNames, sizeof(mapNames) / sizeof(*mapNames), session_map(false));
    snprintf(out, size, "%s", *map ? map : "-");
}

// The internal name uppercased, "tdm" -> "TDM".
static void gametype_value_text(char* out, int size) {
    const char* gametype = session_gametype(false);
    snprintf(out, size, "%s", *gametype ? gametype : "-");
    for (char* c = out; *c; c++) {
        if (*c >= 'a' && *c <= 'z') {
            *c -= 'a' - 'A';
        }
    }
}

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
    return player_slot_used(party, inGame, option - hostMenu.options[HOST_MENU_PLAYERS]);
}

// Only on a slot that holds someone, the modal acts on whoever is in it.
static void open_player_modal() {
    int member = hostMenu.cursor[HOST_MENU_PLAYERS];
    bool inGame = Dvar_GetBool("cl_ingame");
    if (!can_manage_players(inGame)) {
        return;
    }
    if (inGame) {
        if (!player_slot_used(0, true, member)) {
            return;
        }
        if (member == CG_CLIENTNUM) {
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
                          hostMenu.cursor[HOST_MENU_PLAYERS])) {
        hostMenu.current = HOST_MENU_PLAYERS;
    }
}

static void queue_kick(bool ban) {
    int member = hostMenu.cursor[HOST_MENU_PLAYERS];
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
    hostMenu.current = HOST_MENU_PLAYERS;
}

static void ban_selected() {
    queue_kick(true);
    hostMenu.current = HOST_MENU_PLAYERS;
}

// ---------------------------------------------------------------------------
// Save / Load Stats
// ---------------------------------------------------------------------------

#define STATUS_DURATION_US (4 * 1000 * 1000)

// Result of the last stats action, shown in the top left for a few seconds.
// Written by the menu thread and drawn by the UI thread, which copies it bounded, so a
// text being rewritten can't be read past its end.
static char statusText[64];
static volatile system_time_t statusTime;

static void show_status(const char* text) {
    snprintf(statusText, sizeof(statusText), "%s", text);
    __lwsync();
    statusTime = sys_time_get_system_time();
}

// From the UI thread. A menu thread report at the same moment can mix the two texts;
// the draw copies bounded, so that only shows a garbled line.
void host_menu_ui_show_status(const char* text) { show_status(text); }

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

// The list of HOST_MENU_STATS_LIST, rebuilt each time it is opened and after a delete
// while the UI thread draws it. The directory is read into the buffer the options don't
// point to, then the options are switched over row by row, so each drawn row is a
// complete old or new name. The buffer written was last shown two rebuilds ago.
#define STATS_LIST_MAX 20
static char statsNames[2][STATS_LIST_MAX][STATS_FILE_NAME_MAX + 1];
static int statsNamesActive;  // buffer the options of HOST_MENU_STATS_LIST point into
static int statsCount;

static void open_stats_modal();

// In place instead of add_option, so the drawn option count never drops to 0 meanwhile.
static void set_stats_option(int i, const char* text, void (*func)(void)) {
    HostMenuOption* option = &hostMenu.options[HOST_MENU_STATS_LIST][i];
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
    hostMenu.optionCount[HOST_MENU_STATS_LIST] = count ? count : 1;
    statsNamesActive = next;
    statsCount = count;
    int last = hostMenu.optionCount[HOST_MENU_STATS_LIST] - 1;
    if (hostMenu.cursor[HOST_MENU_STATS_LIST] > last) {
        hostMenu.cursor[HOST_MENU_STATS_LIST] = last;
    }
}

// The keyboard only works while the menu is closed, so it is opened by Menu_PaintAll.
static void save_stats() {
    close_menu();
    hostMenu.saveStatsKeyboardPending = true;
}

static void open_stats_menu() {
    build_stats_list();
    hostMenu.current = HOST_MENU_STATS_LIST;
}

static void open_stats_modal() {
    if (hostMenu.cursor[HOST_MENU_STATS_LIST] >= statsCount) {
        return;
    }
    hostMenu.cursor[HOST_MENU_STATS_FILE] = 0;
    hostMenu.current = HOST_MENU_STATS_FILE;
}

// The name is copied, so a list rebuilt meanwhile doesn't change the file acted on.
static void queue_stats_action(HostMenuStatsAction action) {
    int i = hostMenu.cursor[HOST_MENU_STATS_LIST];
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
    hostMenu.current = HOST_MENU_STATS_LIST;
}

// The list and its modal are lobby only like the option that opens them.
static void close_stale_stats_menu(bool inGame) {
    if (inGame && (hostMenu.current == HOST_MENU_STATS_LIST ||
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

// The tabs in the order of the tab bar, menus without a parent.
static const HostMenuId tabs[] = {HOST_MENU_MAIN, HOST_MENU_PATCHES, HOST_MENU_ACCOUNT,
                                  HOST_MENU_PLAYERS};
#define TAB_COUNT ((int)(sizeof(tabs) / sizeof(*tabs)))

static bool is_tab(int m) { return hostMenu.parent[m] == -1; }

// Index in tabs of the tab menu m belongs to.
static int tab_index(int m) {
    while (!is_tab(m)) {
        m = hostMenu.parent[m];
    }
    for (int i = 0; i < TAB_COUNT; i++) {
        if (tabs[i] == m) {
            return i;
        }
    }
    return 0;
}

static void init_tab(HostMenuId m, const char* label) {
    hostMenu.title[m] = label;
    hostMenu.parent[m] = -1;
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

    init_tab(HOST_MENU_MAIN, "MAIN");
    add_option(HOST_MENU_MAIN, "Show Controls", HOST_MENU_TYPE_BOOL,
               &hostMenu.showControls, 0, 0, nothing);
    add_option(HOST_MENU_MAIN, "Force Host", HOST_MENU_TYPE_BOOL, &hostMenu.forceHost, 0,
               0, force_host);
    add_option(HOST_MENU_MAIN, "Players to Start", HOST_MENU_TYPE_INT,
               &hostMenu.playersToStart, 1, HOST_MENU_MAX_CLIENTS, set_players_to_start);
    add_option(HOST_MENU_MAIN, "Max Players", HOST_MENU_TYPE_INT, &hostMenu.maxPlayers, 1,
               HOST_MENU_MAX_CLIENTS, set_max_players);
    HostMenuOption* option = add_option(HOST_MENU_MAIN, "Select Map",
                                        HOST_MENU_TYPE_ACTION, 0, 0, 0, select_map);
    option->lobbyOnly = true;
    option->valueText = map_value_text;
    option = add_option(HOST_MENU_MAIN, "Select Gametype", HOST_MENU_TYPE_ACTION, 0, 0, 0,
                        select_gametype);
    option->lobbyOnly = true;
    option->valueText = gametype_value_text;
    add_option(HOST_MENU_MAIN, "End Game", HOST_MENU_TYPE_ACTION, 0, 0, 0, end_game)
        ->inGameOnly = true;

    init_tab(HOST_MENU_PATCHES, "PATCHES");
    add_option(HOST_MENU_PATCHES, "Depatch Bounces", HOST_MENU_TYPE_BOOL,
               &hostMenu.depatchBounces, 0, 0, depatch_bounces);
    add_option(HOST_MENU_PATCHES, "Disable DLC", HOST_MENU_TYPE_BOOL,
               &hostMenu.disableDlc, 0, 0, disable_dlc);
    add_option(HOST_MENU_PATCHES, "Anti Godmode Classes", HOST_MENU_TYPE_BOOL,
               &hostMenu.antiGodClasses, 0, 0, anti_god_classes)
        ->visible = anti_god_classes_visible;

    init_tab(HOST_MENU_ACCOUNT, "ACCOUNT");
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

    init_tab(HOST_MENU_PLAYERS, "PLAYERS");
    for (int i = 0; i < HOST_MENU_MAX_CLIENTS; i++) {
        add_option(HOST_MENU_PLAYERS, "", HOST_MENU_TYPE_ACTION, 0, 0, 0,
                   open_player_modal)  // names are drawn live
            ->visible = player_slot_visible;
    }

    hostMenu.parent[HOST_MENU_PLAYER] = HOST_MENU_PLAYERS;
    add_option(HOST_MENU_PLAYER, "Kick", HOST_MENU_TYPE_ACTION, 0, 0, 0, kick_selected);
    add_option(HOST_MENU_PLAYER, "Ban", HOST_MENU_TYPE_ACTION, 0, 0, 0, ban_selected);

    hostMenu.title[HOST_MENU_STATS_LIST] = "LOAD STATS";
    hostMenu.parent[HOST_MENU_STATS_LIST] =
        HOST_MENU_ACCOUNT;  // filled by open_stats_menu

    hostMenu.parent[HOST_MENU_STATS_FILE] = HOST_MENU_STATS_LIST;
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
    return m == HOST_MENU_PLAYERS && !inGame && host_menu_lobby_parties_both_active();
}

// Square clears the ban list, only in the list itself and only while it holds a name. Not
// limited to the host: in game the list is still the one the lobby checks afterwards.
static bool can_unban_all(int m, bool inGame) {
    return m == HOST_MENU_PLAYERS && (inGame || host_menu_lobby_view_party()) &&
           host_menu_ban_count() > 0;
}

// Next (+1) or previous (-1) tab, wrapping around.
static void switch_tab(int m, int step) {
    int i = (tab_index(m) + step + TAB_COUNT) % TAB_COUNT;
    hostMenu.current = tabs[i];
}

void host_menu_ui_handle_input() {
    uint16_t digital1 = hostMenu.pad.button[CELL_PAD_BTN_OFFSET_DIGITAL1];
    uint16_t digital2 = hostMenu.pad.button[CELL_PAD_BTN_OFFSET_DIGITAL2];

    if (!hostMenu.open) {
        if (digital1 == CELL_PAD_CTRL_R3 && digital2 == CELL_PAD_CTRL_L1) {
            // Back on the tab that was open last.
            hostMenu.current = tabs[tab_index(hostMenu.current)];
            cellAtomicStore32(&hostMenu.shoulderTaps, 0);
            hostMenu.open = true;
            sys_timer_usleep(200000);
        }
        return;
    }

    // Consumed on every poll, so a tap in a sub-menu doesn't switch tabs later.
    uint32_t shouldersPressed = cellAtomicStore32(&hostMenu.shoulderTaps, 0);

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

    if (shouldersPressed && is_tab(m)) {
        switch_tab(m, shouldersPressed & CELL_PAD_CTRL_R1 ? 1 : -1);
    } else if (digital1 & CELL_PAD_CTRL_UP) {
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
        if (is_tab(m)) {
            close_menu();
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

// The player list's tags and their widths (scaled to ROW_SCALE), built once per frame by
// build_player_rows.
static char playerTags[HOST_MENU_MAX_CLIENTS][CLAN_TAG_MAX];
// The tags without color codes, drawn in the row color under the selection bar.
static char playerPlainTags[HOST_MENU_MAX_CLIENTS][CLAN_TAG_MAX];
static float playerTagWidths[HOST_MENU_MAX_CLIENTS];
// Each row's rank icon (0 when the game has none) and level text ("" for an empty slot).
static void* playerRankIcons[HOST_MENU_MAX_CLIENTS];
static char playerLevels[HOST_MENU_MAX_CLIENTS][4];
// Width of the icon and level column (scaled to ROW_SCALE). Rows with a level are
// indented by it, so the tags and names all start at one x; rows without one start at the
// left edge.
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
// The panel's widest width in the player list since the list was entered or its party
// changed, 0 outside the list. Only the draw code touches these.
static int playerListWidth;
// The party the list showed last frame (0 in game), so the width is refit on a change.
static int playerListParty;

// Rank icon size and the gap after the icon and after the level.
#define RANK_ICON_SIZE 15.0f
#define RANK_GAP 4.0f
// Range of mp/rankIconTable.csv: rankId rows 0-49, prestige columns 0-15.
#define RANK_ICON_MAX_RANK 49
#define RANK_ICON_MAX_PRESTIGE 15

// Copies text without its ^0-^9 color codes and embedded icons into out (outSize bytes,
// NUL-terminated). Reads at most size bytes of text, so it is safe on fixed-size fields
// without a NUL.
static void strip_color_codes(const char* text, int size, char* out, int outSize) {
    int length = 0;
    for (int c = 0; c < size && text[c] && length < outSize - 1; c++) {
        bool caret = text[c] == '^' && c + 1 < size;
        if (caret && text[c + 1] >= '0' && text[c + 1] <= '9') {
            c++;
            continue;
        }
        // Embedded icon (^\x01 / ^\x02, width, height, name length, name) is dropped
        // as a whole.
        if (caret && (text[c + 1] == 1 || text[c + 1] == 2)) {
            if (c + 4 >= size) {
                break;
            }
            c += 4 + (unsigned char)text[c + 4];
            continue;
        }
        out[length++] = text[c];
    }
    out[length] = 0;
}

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
        char clan[T5_CgClientClanSize];
        strip_color_codes(CG_CLIENT_CLAN(i), T5_CgClientClanSize, clan, sizeof(clan));
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

// Appends part to the " - " separated line at *length, skipping empty parts.
static void append_info_part(char* line, int size, int* length, const char* part) {
    if (!*part || *length >= size) {
        return;
    }
    *length +=
        snprintf(line + *length, size - *length, "%s%s", *length ? " - " : "", part);
}

// The info line "host - map - gametype" into line, leaving out empty parts, the host's
// clan tag into tag (drawn apart, so its ^7 doesn't recolor the rest). The host name
// loses its color codes, they would recolor map and gametype. Returns the width of both
// together.
static float build_info_line(bool inGame, char* tag, char* line, int size) {
    const char* host = dvar_string("party_hostname");
    tag[0] = 0;
    if (*host) {
        host_clan_tag(inGame, host, tag);
    }
    char plainHost[64];
    strip_color_codes(host, (int)strlen(host), plainHost, sizeof(plainHost));
    line[0] = 0;
    int length = 0;
    append_info_part(line, size, &length, plainHost);
    append_info_part(
        line, size, &length,
        lookup_name(mapNames, sizeof(mapNames) / sizeof(*mapNames), session_map(inGame)));
    append_info_part(
        line, size, &length,
        lookup_name(gametypeNames, sizeof(gametypeNames) / sizeof(*gametypeNames),
                    session_gametype(inGame)));
    return (*tag ? text_width(tag, INFO_SCALE) : 0) + text_width(line, INFO_SCALE);
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

// Panel sections from top to bottom: title, info line, tabs, list, footer.
typedef struct PanelLayout {
    float left;
    float width;
    float top;
    float tabsTop;    // the 1px divider sits at tabsTop + TAB_HEIGHT
    float listTop;    // top of the first row
    float footerTop;  // the 1px divider above the footer
    float bottom;
} PanelLayout;

// rows may be fractional while the panel resizes, slide moves it down while it fades.
static PanelLayout panel_layout(float width, float rows, float slide) {
    PanelLayout layout;
    layout.width = width;
    layout.left = PANEL_CENTER_X - width / 2;
    layout.top = PANEL_TOP + slide;
    layout.tabsTop = layout.top + TITLE_HEIGHT + INFO_HEIGHT;
    layout.listTop = layout.tabsTop + TAB_HEIGHT + 1 + LIST_PADDING;
    layout.footerTop = layout.listTop + rows * ROW_HEIGHT + LIST_PADDING;
    layout.bottom = layout.footerTop + 1 + FOOTER_HEIGHT;
    return layout;
}

// Rounded up to an even whole number within the panel limits, so the panel centered at
// PANEL_CENTER_X stays pixel aligned.
static int panel_width(float content) {
    int width = (int)content;
    if (width < content) {
        width++;  // ceil (content >= 0)
    }
    width = (width + 1) & ~1;  // round up to even
    if (width < PANEL_MIN_WIDTH) {
        width = PANEL_MIN_WIDTH;
    }
    if (width > PANEL_MAX_WIDTH) {
        width = PANEL_MAX_WIDTH;
    }
    return width;
}

// Text with extra space between the characters.
static void draw_spaced_text(const char* text, float x, float y, float scale,
                             float spacing, const float* color) {
    char c[2] = {0, 0};
    for (; *text; text++) {
        c[0] = *text;
        draw_text(c, x, y, scale, color);
        x += text_width(c, scale) + spacing;
    }
}

// Title, info line and the tab bar with activeTab (index in tabs) highlighted.
static void draw_header(const PanelLayout* layout, const char* tag, const char* info,
                        int activeTab) {
    float x = layout->left + PANEL_PADDING;
    draw_spaced_text("SILINIUM", x, text_baseline(layout->top, TITLE_HEIGHT, TITLE_SCALE),
                     TITLE_SCALE, TITLE_SPACING, white);

    float infoX = x;
    float infoY = text_baseline(layout->top + TITLE_HEIGHT, INFO_HEIGHT, INFO_SCALE);
    if (*tag) {
        draw_text(tag, infoX, infoY, INFO_SCALE, valueColor);
        infoX += text_width(tag, INFO_SCALE);
    }
    draw_text(info, infoX, infoY, INFO_SCALE, valueColor);

    // Tab positions relative to the panel's left edge, so a resizing panel doesn't make
    // the underline lag behind.
    float tabX[TAB_COUNT];
    float tabWidth[TAB_COUNT];
    for (int i = 0; i < TAB_COUNT; i++) {
        tabX[i] = x - layout->left;
        tabWidth[i] = text_width(hostMenu.title[tabs[i]], TAB_SCALE);
        x += tabWidth[i] + TAB_GAP;
    }
    static Tween underlineX;
    static Tween underlineWidth;
    float lineX = layout->left + tween_follow(&underlineX, tabX[activeTab], ANIM_PAGE_US);
    float lineWidth = tween_follow(&underlineWidth, tabWidth[activeTab], ANIM_PAGE_US);

    // Only the normal font is registered, so the active tab is white instead of bold. A
    // label turns white as far as the gliding underline covers it.
    float tabY = text_baseline(layout->tabsTop, TAB_HEIGHT, TAB_SCALE);
    float dividerY = layout->tabsTop + TAB_HEIGHT;
    for (int i = 0; i < TAB_COUNT; i++) {
        float labelX = layout->left + tabX[i];
        float coveredLeft = labelX > lineX ? labelX : lineX;
        float labelRight = labelX + tabWidth[i];
        float lineRight = lineX + lineWidth;
        float coveredRight = labelRight < lineRight ? labelRight : lineRight;
        float coverage = 0;
        if (tabWidth[i] > 0) {
            coverage = clamp01((coveredRight - coveredLeft) / tabWidth[i]);
        }
        float color[4];
        mix_color(color, mutedColor, white, coverage);
        draw_text(hostMenu.title[tabs[i]], labelX, tabY, TAB_SCALE, color);
    }
    draw_shader(lineX, dividerY - TAB_UNDERLINE, lineWidth, TAB_UNDERLINE, selection,
                whiteMaterial);
    draw_shader(layout->left, dividerY, layout->width, 1, divider, whiteMaterial);
}

// The title of a submenu page, right in the title row, as the tab bar only names tabs.
static void draw_page_label(const PanelLayout* layout, int page) {
    const char* title = hostMenu.title[page];
    if (is_tab(page) || !title) {
        return;
    }
    float right = layout->left + layout->width - PANEL_PADDING;
    draw_text(title, right - text_width(title, INFO_SCALE),
              text_baseline(layout->top, TITLE_HEIGHT, INFO_SCALE), INFO_SCALE,
              mutedColor);
}

// Controls on the left, position on the right.
static void draw_footer(const PanelLayout* layout, const char* controls,
                        const char* position) {
    draw_shader(layout->left, layout->footerTop, layout->width, 1, divider,
                whiteMaterial);
    float y = text_baseline(layout->footerTop + 1, FOOTER_HEIGHT, FOOTER_SCALE);
    draw_text(controls, layout->left + PANEL_PADDING, y, FOOTER_SCALE, mutedColor);
    float right = layout->left + layout->width - PANEL_PADDING;
    draw_text(position, right - text_width(position, FOOTER_SCALE), y, FOOTER_SCALE,
              mutedColor);
}

// The value each toggle showed last frame and when it last flipped, so a flip eases the
// core in or out.
static bool shownToggle[HOST_MENU_MAX_MENUS][HOST_MENU_MAX_OPTIONS];
static system_time_t toggleTime[HOST_MENU_MAX_MENUS][HOST_MENU_MAX_OPTIONS];

// Takes the current values as shown, so a toggle changed while its page was hidden
// (for example by Load Stats) does not animate when the page appears.
static void sync_toggles(void) {
    for (int menu = 0; menu < HOST_MENU_MAX_MENUS; menu++) {
        for (int option = 0; option < hostMenu.optionCount[menu]; option++) {
            HostMenuOption* o = &hostMenu.options[menu][option];
            if (o->type == HOST_MENU_TYPE_BOOL) {
                shownToggle[menu][option] = *(bool*)o->value;
            }
        }
    }
}

// 0 (off) .. 1 (on), easing after a flip.
static float toggle_amount(int m, int i, bool on) {
    if (on != shownToggle[m][i]) {
        shownToggle[m][i] = on;
        toggleTime[m][i] = frameTime;
    }
    float t = ease_out(anim_progress(toggleTime[m][i], ANIM_TOGGLE_US));
    return on ? t : 1 - t;
}

// On: border, gap and a filled core. Off: only the border. Dark on the selection bar,
// where blue would vanish. on and selected are 0..1 while they animate; the core grows
// from the center.
static void draw_toggle(float x, float y, float on, float selected) {
    float color[4];
    mix_color(color, toggleOff, selection, on);
    mix_color(color, color, selectedText, selected);
    draw_outline(x, y, TOGGLE_SIZE, TOGGLE_SIZE, TOGGLE_BORDER, color);
    if (on > 0) {
        float inset = TOGGLE_BORDER * 2;
        float core = (TOGGLE_SIZE - inset * 2) * on;
        float offset = (TOGGLE_SIZE - core) / 2;
        draw_shader(x + offset, y + offset, core, core, color, whiteMaterial);
    }
}

// How much the selection bar at barRow covers row: 1 when it sits on it, 0 a row away.
static float bar_overlap(float barRow, int row) {
    float distance = barRow - row;
    return clamp01(1 - (distance < 0 ? -distance : distance));
}

// Row of the cursor among the visible options of menu m.
static int cursor_row(int m, bool inGame) {
    int row = 0;
    for (int i = 0; i < hostMenu.cursor[m]; i++) {
        if (option_visible(&hostMenu.options[m][i], inGame)) {
            row++;
        }
    }
    return row;
}

// The visible options of menu m as rows of a list spanning left..left + width, the first
// row's top at top. The selection bar sits at barRow (fractional while it slides), rows
// reaching below bottom are left out (the panel is still growing). The row content is
// moved sideways by shift, the selection bar is not, so it never leaves the list.
static void draw_options(int m, bool inGame, float left, float width, float shift,
                         float top, float barRow, float bottom) {
    float barTop = top + barRow * ROW_HEIGHT;
    if (visible_option_count(m, inGame) && barTop + ROW_HEIGHT <= bottom + 0.5f) {
        draw_shader(left, barTop, width, ROW_HEIGHT, selection, whiteMaterial);
    }
    left += shift;
    float right = left + width - PANEL_PADDING;
    int row = 0;
    for (int i = 0; i < hostMenu.optionCount[m]; i++) {
        HostMenuOption* option = &hostMenu.options[m][i];
        if (!option_visible(option, inGame)) {
            continue;
        }

        float rowTop = top + row * ROW_HEIGHT;
        if (rowTop + ROW_HEIGHT > bottom + 0.5f) {
            break;
        }
        bool selected = hostMenu.cursor[m] == i;
        // Text darkens as far as the bar slides under it, so it never goes dark on dark.
        float covered = bar_overlap(barRow, row);
        float textColor[4];
        mix_color(textColor, white, selectedText, covered);
        float y = text_baseline(rowTop, ROW_HEIGHT, ROW_SCALE);
        draw_text(option->text, left + PANEL_PADDING, y, ROW_SCALE, textColor);

        char value[32] = "";
        switch (option->type) {
            case HOST_MENU_TYPE_SUBMENU:
                draw_shader(right - ARROW_SIZE, rowTop + (ROW_HEIGHT - ARROW_SIZE) / 2,
                            ARROW_SIZE, ARROW_SIZE, textColor, arrowMaterial);
                break;
            case HOST_MENU_TYPE_INT:
                snprintf(value, sizeof(value), selected ? "< %i >" : "%i",
                         *(signed char*)option->value);
                break;
            case HOST_MENU_TYPE_BOOL:
                draw_toggle(right - TOGGLE_SIZE, rowTop + (ROW_HEIGHT - TOGGLE_SIZE) / 2,
                            toggle_amount(m, i, *(bool*)option->value), covered);
                break;
            default:
                break;
        }
        if (option->valueText) {
            option->valueText(value, sizeof(value));
        }

        if (*value) {
            float color[4];
            mix_color(color, valueColor, selectedText, covered);
            draw_text(value, right - text_width(value, ROW_SCALE), y, ROW_SCALE, color);
        }
        row++;
    }
}

// The options of modalMenu below list row listRow, or above it when they would reach
// past the bottom of the panel and fit above it within the list, so they never cover the
// header on short lists. alpha and slide (pushed down by it) animate it in and out,
// barRow is its selection bar.
static void draw_modal(const PanelLayout* layout, int listRow, int modalMenu,
                       const char* title, bool inGame, float alpha, float slide,
                       float barRow) {
    float height = TOGGLE_BORDER + MODAL_TITLE_HEIGHT +
                   visible_option_count(modalMenu, inGame) * ROW_HEIGHT + LIST_PADDING +
                   TOGGLE_BORDER;
    float rowTop = layout->listTop + listRow * ROW_HEIGHT;
    float y = rowTop + ROW_HEIGHT;
    if (y + height > layout->bottom && rowTop - height >= layout->listTop) {
        y = rowTop - height;
    }
    y += slide;
    float left = PANEL_CENTER_X - MODAL_WIDTH / 2;

    float panelAlpha = drawAlpha;
    drawAlpha *= alpha;
    draw_shader(left, y, MODAL_WIDTH, height, modalBackground, whiteMaterial);
    draw_outline(left, y, MODAL_WIDTH, height, TOGGLE_BORDER, selection);
    draw_text(title, PANEL_CENTER_X - text_width(title, MODAL_TITLE_SCALE) / 2,
              text_baseline(y + TOGGLE_BORDER, MODAL_TITLE_HEIGHT, MODAL_TITLE_SCALE),
              MODAL_TITLE_SCALE, white);
    // Inside the border, so the selection bar doesn't cover it.
    draw_options(modalMenu, inGame, left + TOGGLE_BORDER, MODAL_WIDTH - TOGGLE_BORDER * 2,
                 0, y + TOGGLE_BORDER + MODAL_TITLE_HEIGHT, barRow, y + height);
    drawAlpha = panelAlpha;
}

// The modal over the list it was opened from: fades in when it opens, and out when it is
// left while that list is still shown. Its selection bar slides like the list's.
static int shownModal = -1;
static bool modalClosing;
static Tween modalAlpha;
static Tween modalBar;
// Frozen while the modal fades out: by then a delete may have rebuilt the stats list (and
// clamped its cursor) or a kick emptied the slot, so the live title would switch to
// someone or something else and the modal could jump to another row.
static char modalTitle[32];
static int modalRow;

static void draw_modal_animated(const PanelLayout* layout, int m, int party,
                                bool inGame) {
    bool modal = m == HOST_MENU_PLAYER || m == HOST_MENU_STATS_FILE;
    if (modal && (m != shownModal || modalClosing)) {
        // A reopen during the fade out continues from the alpha on screen.
        if (m != shownModal) {
            tween_snap(&modalAlpha, 0);
        }
        shownModal = m;
        modalClosing = false;
        tween_to(&modalAlpha, 1, ANIM_MODAL_US);
        tween_snap(&modalBar, cursor_row(m, inGame));
    } else if (!modal && shownModal != -1 && !modalClosing) {
        modalClosing = true;
        tween_to(&modalAlpha, 0, ANIM_MODAL_US);
    }
    if (shownModal == -1) {
        return;
    }
    int listMenu = hostMenu.parent[shownModal];
    float alpha = tween_value(&modalAlpha, ANIM_MODAL_US);
    if (modalClosing && (alpha <= 0 || m != listMenu)) {
        shownModal = -1;
        return;
    }

    float barRow =
        tween_follow(&modalBar, cursor_row(shownModal, inGame), ANIM_SCROLL_US);
    if (!modalClosing) {
        const char* title =
            shownModal == HOST_MENU_PLAYER
                ? player_name(party, inGame, hostMenu.cursor[HOST_MENU_PLAYERS])
                : hostMenu
                      .options[HOST_MENU_STATS_LIST]
                              [hostMenu.cursor[HOST_MENU_STATS_LIST]]
                      .text;
        snprintf(modalTitle, sizeof(modalTitle), "%s", title);
        modalRow = cursor_row(listMenu, inGame);
    }
    draw_modal(layout, modalRow, shownModal, modalTitle, inGame, alpha,
               (1 - alpha) * ANIM_MODAL_SLIDE, barRow);
}

// Drawn in the given left column row: the top row while the menu is open,
// below the open hint while it is closed (Save Stats closes it for the keyboard).
static void draw_status(int row) {
    system_time_t time = statusTime;
    system_time_t age = frameTime - time;
    if (!time || age > STATUS_DURATION_US) {
        return;
    }
    // Pairs with the one in show_status: the text is read only after the time that marks
    // it ready.
    __lwsync();
    char text[sizeof(statusText)];
    memcpy(text, statusText, sizeof(text));
    text[sizeof(text) - 1] = 0;
    float step = R_TextHeight(font) * 0.5f + 10;
    float fadeIn = clamp01((float)age / ANIM_STATUS_IN_US);
    float fadeOut = clamp01((float)(STATUS_DURATION_US - age) / ANIM_STATUS_OUT_US);
    drawAlpha = fadeIn < fadeOut ? fadeIn : fadeOut;
    draw_text_box(text, 10, 20 + row * step, 0.5f);
    drawAlpha = 1;
}

// Builds every row's rank, tag and their widths for the draw loop and returns the panel
// width: wide enough for the longest "icon level [tag]name" row plus padding, within the
// panel limits, so a long tag from a modded peer can't push the panel off screen (its row
// overflows instead).
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
    float levelWidth = text_width("50", ROW_SCALE);
    for (int i = 0; i < HOST_MENU_MAX_CLIENTS; i++) {
        char* tag = playerTags[i];
        player_clan_tag(party, inGame, i, tag);
        strip_color_codes(tag, CLAN_TAG_MAX, playerPlainTags[i], CLAN_TAG_MAX);
        playerTagWidths[i] = *tag ? text_width(tag, ROW_SCALE) : 0;
        build_player_rank(party, inGame, i);
        if (*playerLevels[i]) {
            float w = text_width(playerLevels[i], ROW_SCALE);
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
                  text_width(player_name(party, inGame, i), ROW_SCALE);
        if (w > widest) {
            widest = w;
        }
    }
    return panel_width(widest + PANEL_PADDING * 2);
}

// The player rows, "Not in a lobby" outside a lobby or "No players" for an empty list.
// The selection bar, the row limit and shift work like in draw_options.
static void draw_player_list(const PanelLayout* layout, float shift, int party,
                             bool inGame, bool notHost, float barRow) {
    float bottom = layout->footerTop - LIST_PADDING;
    float left = layout->left + shift;
    const char* empty = notHost ? "Not in a lobby"
                        : !visible_option_count(HOST_MENU_PLAYERS, inGame) ? "No players"
                                                                           : 0;
    if (empty) {
        draw_text(empty, left + PANEL_PADDING,
                  text_baseline(layout->listTop, ROW_HEIGHT, ROW_SCALE), ROW_SCALE,
                  white);
        return;
    }
    float barTop = layout->listTop + barRow * ROW_HEIGHT;
    if (barTop + ROW_HEIGHT <= bottom + 0.5f) {
        draw_shader(layout->left, barTop, layout->width, ROW_HEIGHT, selection,
                    whiteMaterial);
    }
    // Tag and name are drawn apart, so the ^7 that ends a colored tag doesn't turn the
    // selected name white. The colored tag crossfades to its plain copy as the bar covers
    // the row: its color codes override the row color, so it wouldn't go dark otherwise.
    // Only occupied slots get a row.
    int row = 0;
    for (int i = 0; i < HOST_MENU_MAX_CLIENTS; i++) {
        if (!player_slot_used(party, inGame, i)) {
            continue;
        }
        float rowTop = layout->listTop + row * ROW_HEIGHT;
        if (rowTop + ROW_HEIGHT > bottom + 0.5f) {
            break;
        }
        float overlap = bar_overlap(barRow, row++);
        float color[4];
        mix_color(color, white, selectedText, overlap);
        float y = text_baseline(rowTop, ROW_HEIGHT, ROW_SCALE);
        float x = left + PANEL_PADDING;
        // On player rows icon, level and tag/name each start at a fixed x, also without
        // an icon, so the names form one straight column. Players without a level start
        // at the left edge.
        if (*playerLevels[i]) {
            if (playerRankIcons[i]) {
                draw_shader(x, rowTop + (ROW_HEIGHT - RANK_ICON_SIZE) / 2, RANK_ICON_SIZE,
                            RANK_ICON_SIZE, white, playerRankIcons[i]);
            }
            draw_text(playerLevels[i], x + RANK_ICON_SIZE + RANK_GAP, y, ROW_SCALE,
                      color);
            x += playerRankColumn;
        }
        if (*playerTags[i]) {
            float faded[4] = {color[0], color[1], color[2], color[3]};
            if (overlap < 1) {
                faded[3] = color[3] * (1 - overlap);
                draw_text(playerTags[i], x, y, ROW_SCALE, faded);
            }
            if (overlap > 0) {
                faded[3] = color[3] * overlap;
                draw_text(playerPlainTags[i], x, y, ROW_SCALE, faded);
            }
            x += playerTagWidths[i];
        }
        draw_text(player_name(party, inGame, i), x, y, ROW_SCALE, color);
    }
}

// The footer's controls for menu m.
static void build_controls(int m, bool inGame, bool notHost, bool viewOnly, char* out,
                           int size) {
    if (!is_tab(m)) {
        snprintf(out, size, "\x14\x15 Scroll  \x01 Select  \x02 Back");
        return;
    }
    // The R1 glyph is unknown, so the tab keys are spelled out.
    const char* exitHint = "\x02 Exit  L1/R1 Tab";
    if (m != HOST_MENU_PLAYERS) {
        snprintf(out, size, "\x14\x15\x16\x17 Scroll/Rotate  \x01 Select  %s", exitHint);
        return;
    }
    if (notHost) {
        snprintf(out, size, "%s", exitHint);
        return;
    }
    const char* switchHint = can_switch_lists(m, inGame) ? "  \x04 Switch List" : "";
    // \x03 as the Square glyph is inferred from \x01 Cross, \x02 Circle and \x04 Triangle
    // (unverified).
    char unbanHint[32] = "";
    if (can_unban_all(m, inGame)) {
        snprintf(unbanHint, sizeof(unbanHint), "  \x03 Unban All (%i)",
                 host_menu_ban_count());
    }
    snprintf(out, size, "\x14\x15 Scroll%s%s%s  %s", viewOnly ? "" : "  \x01 Select",
             switchHint, unbanHint, exitHint);
}

// The list page the panel shows (a tab or the Load Stats list), and how it came in:
// from the right (+1) when going to the next tab or into a submenu, from the left (-1)
// otherwise.
static int shownPage = -1;
static int pageDirection;
static system_time_t pageTime;
static Tween panelWidth;
static Tween panelRows;
static Tween listBar;

static int page_direction(int from, int to) {
    if (is_tab(from) && is_tab(to)) {
        int step = (tab_index(to) - tab_index(from) + TAB_COUNT) % TAB_COUNT;
        return step == 1 ? 1 : -1;
    }
    return hostMenu.parent[from] == to ? -1 : 1;
}

// The open panel, faded to alpha and pushed down by slide (opening and closing).
static void draw_panel(float alpha, float slide) {
    close_stale_player_modal();
    bool inGame = Dvar_GetBool("cl_ingame");
    close_stale_stats_menu(inGame);
    int m = hostMenu.current;
    // The player modal is drawn over the player list, so the panel keeps showing the
    // list.
    bool players = m == HOST_MENU_PLAYERS || m == HOST_MENU_PLAYER;
    // The stats modal is drawn over the stats list the same way.
    bool stats = m == HOST_MENU_STATS_LIST || m == HOST_MENU_STATS_FILE;
    int boxMenu = players ? HOST_MENU_PLAYERS : stats ? HOST_MENU_STATS_LIST : m;
    // Clients see the list too, only the host can open the player modal.
    int party = (players && !inGame) ? host_menu_lobby_view_party() : 0;
    bool notHost = players && !inGame && !party;  // not in a lobby
    bool viewOnly = players && !notHost && !can_manage_players(inGame);

    char controls[128];
    build_controls(m, inGame, notHost, viewOnly, controls, sizeof(controls));
    // With both parties active the position names the list shown.
    char position[32] = "";
    if (!notHost && visible_option_count(boxMenu, inGame)) {
        bool bothLists = players && !inGame && host_menu_lobby_parties_both_active();
        const char* list = !bothLists                    ? ""
                           : hostMenu.lobbyShowGameParty ? "Party  "
                                                         : "Lobby  ";
        snprintf(position, sizeof(position), "%s%i / %i", list,
                 cursor_row(boxMenu, inGame) + 1, visible_option_count(boxMenu, inGame));
    }
    char tag[CLAN_TAG_MAX];
    char info[128];
    float infoWidth = build_info_line(inGame, tag, info, sizeof(info));
    float footerWidth = text_width(controls, FOOTER_SCALE) + PANEL_PADDING +
                        text_width(position, FOOTER_SCALE);
    int width = panel_width((infoWidth > footerWidth ? infoWidth : footerWidth) +
                            PANEL_PADDING * 2);

    // The player list grows around the center, so the tabs and the modal stay centered.
    // It keeps its widest width until the list is left, so names don't shift sideways
    // when a long-named player leaves.
    if (players && !notHost) {
        if (party != playerListParty) {
            playerListParty = party;
            playerListWidth = 0;  // fit the panel to the new list
        }
        int w = build_player_rows(party, inGame);
        if (w > playerListWidth) {
            playerListWidth = w;
        }
        if (playerListWidth > width) {
            width = playerListWidth;
        }
    } else {
        playerListWidth = 0;
    }

    if (snapAnimations || boxMenu != shownPage) {
        sync_toggles();
    }
    if (boxMenu != shownPage) {
        bool slideIn = shownPage != -1 && !snapAnimations;
        pageDirection = slideIn ? page_direction(shownPage, boxMenu) : 0;
        pageTime = slideIn ? frameTime : 0;
        shownPage = boxMenu;
        tween_snap(&listBar, cursor_row(boxMenu, inGame));
    }
    int rows = visible_option_count(boxMenu, inGame);
    if (notHost || !rows) {
        rows = 1;  // "Not in a lobby" / "No players"
    }
    float barRow = tween_follow(&listBar, cursor_row(boxMenu, inGame), ANIM_SCROLL_US);
    PanelLayout layout =
        panel_layout(tween_follow(&panelWidth, width, ANIM_PAGE_US),
                     tween_follow(&panelRows, rows, ANIM_PAGE_US), slide);

    drawAlpha = alpha;
    draw_shader(layout.left, layout.top, layout.width, layout.bottom - layout.top,
                panelBackground, whiteMaterial);
    draw_header(&layout, tag, info, tab_index(m));
    draw_footer(&layout, controls, position);

    // A new page's rows fade in while their content slides into place. The selection bar
    // only fades, so it stays inside the panel.
    float page = ease_out(anim_progress(pageTime, ANIM_PAGE_US));
    float shift = pageDirection * (1 - page) * ANIM_PAGE_SLIDE;
    drawAlpha = alpha * page;
    draw_page_label(&layout, boxMenu);
    if (players) {
        draw_player_list(&layout, shift, party, inGame, notHost, barRow);
    } else {
        draw_options(boxMenu, inGame, layout.left, layout.width, shift, layout.listTop,
                     barRow, layout.footerTop - LIST_PADDING);
    }
    drawAlpha = alpha;
    draw_modal_animated(&layout, m, party, inGame);
    drawAlpha = 1;
}

// Last state of hostMenu.open and the panel alpha it fades, for the open and close
// animation. The duration is that of the running fade (open and close differ).
static bool shownOpen;
static Tween menuAlpha;
static int menuAlphaDuration = ANIM_OPEN_US;

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

    frameTime = sys_time_get_system_time();
    // closing first: close_menu() clears it only after open, so a finished close can't
    // read as open for a frame.
    bool closing = hostMenu.closing;
    __lwsync();
    bool open = hostMenu.open && !closing;
    if (open != shownOpen) {
        shownOpen = open;
        // Reopening mid-fade continues from the alpha on screen, which is measured with
        // the duration of the fade it belongs to.
        tween_to(&menuAlpha, open ? 1 : 0, menuAlphaDuration);
        menuAlphaDuration = open ? ANIM_OPEN_US : ANIM_CLOSE_US;
        snapAnimations = open;
    }
    float alpha = tween_value(&menuAlpha, menuAlphaDuration);

    if (!open) {
        // The menu thread already treats the menu as closed, only the picture lingers.
        if (alpha > 0) {
            draw_panel(alpha, (1 - alpha) * ANIM_OPEN_SLIDE);
        } else {
            playerListWidth = 0;
        }
        if (hostMenu.showControls) {
            draw_text_box("Press \x05 + \x11 to Open Silinium", 10, 20, 0.5f);
        }
        draw_status(hostMenu.showControls ? 1 : 0);
        return;
    }
    draw_status(0);

    draw_panel(alpha, (1 - alpha) * ANIM_OPEN_SLIDE);
    snapAnimations = false;
}
