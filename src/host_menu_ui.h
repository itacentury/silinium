#ifndef HOST_MENU_UI_H
#define HOST_MENU_UI_H

#include <cell/pad.h>
#include <stdbool.h>

#include "defines.h"
#include "stats_file.h"

#define HOST_MENU_MAX_MENUS 7
#define HOST_MENU_MAX_OPTIONS 30
#define HOST_MENU_KICK_BAN \
    0x40  // or'd into kickClient and lobbyKickMember, above any client num or member
          // index

typedef enum HostMenuOptionType {
    HOST_MENU_TYPE_ACTION = 0,
    HOST_MENU_TYPE_BOOL = 1,    // value: bool*, shows a box, filled when on
    HOST_MENU_TYPE_INT = 2,     // value: signed char*, LEFT/RIGHT cycles min..max
    HOST_MENU_TYPE_SUBMENU = 5  // shows ui_arrow_right
} HostMenuOptionType;

typedef enum HostMenuId {
    // The tabs, menus without a parent.
    HOST_MENU_MAIN = 0,
    HOST_MENU_PATCHES = 1,  // toggles for host-side game patches
    HOST_MENU_ACCOUNT = 2,  // own account: recovery, prestige, stats files, name
    HOST_MENU_KICK = 3,     // player list, the PLAYERS tab
    HOST_MENU_PLAYER =
        4,  // actions for the player selected in HOST_MENU_KICK, drawn as a modal over it
    HOST_MENU_STATS = 5,      // saved stats files
    HOST_MENU_STATS_FILE = 6  // actions for the file selected in HOST_MENU_STATS, a modal
} HostMenuId;

typedef enum HostMenuStatsAction {
    HOST_MENU_STATS_NONE,
    HOST_MENU_STATS_LOAD,
    HOST_MENU_STATS_DELETE
} HostMenuStatsAction;

typedef struct HostMenuOption {
    const char* text;
    HostMenuOptionType type;
    void* value;
    signed char min;
    signed char max;
    void (*func)(void);
    bool lobbyOnly;   // hidden while in game
    bool inGameOnly;  // hidden while in lobby
    // Extra condition for showing it, 0 = none.
    bool (*visible)(const struct HostMenuOption* option, bool inGame);
    void (*valueText)(char* out, int size);  // text shown on the right, 0 = none
} HostMenuOption;

typedef struct HostMenu {
    int current;
    int cursor[HOST_MENU_MAX_MENUS];
    int optionCount[HOST_MENU_MAX_MENUS];
    int parent[HOST_MENU_MAX_MENUS];
    const char* title[HOST_MENU_MAX_MENUS];
    HostMenuOption options[HOST_MENU_MAX_MENUS][HOST_MENU_MAX_OPTIONS];

    CellPadData pad;  // last pad state, written by the cellPadGetData hook
    bool open;
    bool showControls;
    bool changeNamePending;
    bool resetNamePending;  // set by Reset Name, run by Menu_PaintAll
    bool selectMapPending;
    bool selectGametypePending;
    bool endGamePending;
    bool scorelimitPending;
    bool antiGodClassCheckPending;  // set by the Anti Godmode Classes toggle, run by
                                    // ClientEndFrame
    bool maxPlayersSet;  // Max Players was applied with the menu, the TDM scorelimit
                         // follows it
    char originalName[33];

    bool forceHost;
    bool depatchBounces;
    bool prestigeUnlocks;
    bool disableDlc;
    bool antiGodClasses;  // fix an invalid body in the clients' classes
    signed char playersToStart;
    signed char maxPlayers;
    signed char prestige;
    bool playerModalInGame;  // cl_ingame when the player modal was opened, the slot means
                             // something else once it changes
    int playerModalParty;    // party the modal was opened in the lobby for, 0 in game
    signed char
        kickClient;  // -1 = none, otherwise client num | HOST_MENU_KICK_BAN when banning
    signed char lobbyKickMember;  // -1 = none, otherwise party member index |
                                  // HOST_MENU_KICK_BAN when banning
    int lobbyKickParty;           // party the queued lobby kick was chosen in
    char lobbyKickName[32];       // name of the member it was chosen for, as long as
                                  // Party_BanName copies
    bool unbanAllPending;     // set by Square in the player list, run by Menu_PaintAll
    bool lobbyShowGameParty;  // with both parties active, the player list shows GameParty
                              // ("Party") instead of LobbyParty ("Lobby")

    // Save Stats: the option sets saveStatsKeyboardPending, Menu_PaintAll opens the
    // keyboard, its callback fills name, clan tag and class names and sets
    // saveStatsPending, the menu thread saves.
    bool saveStatsKeyboardPending;
    bool saveStatsPending;
    char saveStatsName[STATS_FILE_NAME_MAX + 1];
    StatsFileProfile saveStatsProfile;
    // Load and Delete from the stats modal, run by the menu thread on the file named.
    HostMenuStatsAction statsAction;
    char statsActionName[STATS_FILE_NAME_MAX + 1];
    // Loaded stats and their clan tag and class names, applied by Menu_PaintAll. Save,
    // Load and Delete wait while set, save and load share the file buffer.
    bool statsLoadPending;
    StatsFileProfile loadedProfile;
} HostMenu;

extern HostMenu hostMenu;

void host_menu_ui_init();
void host_menu_ui_handle_input();
void host_menu_ui_draw();
void host_menu_ui_open_name_keyboard();
void host_menu_ui_open_stats_keyboard();
// Shows a short status line below the session info / open hint for a few seconds.
void host_menu_ui_show_status(const char* text);
// Runs queued Save/Load/Delete Stats requests, in the menu thread (file access).
void host_menu_ui_run_stats_actions(bool inGame);
void host_menu_set_party_dvars(bool connectToOthers, int minPlayers, int pregameTimer,
                               int connectTimeout, int maxTeamDiff);
void host_menu_set_bounce_dvars(bool depatched);
void host_menu_hide_dlc_maps();
int host_menu_get_scorelimit();

#endif /* HOST_MENU_UI_H */
