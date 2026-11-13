#include "party_map.h"

#include <stdio.h>
#include <string.h>

#include "host_menu.h"
#include "host_menu_ui.h"
#include "t5.h"

// The game's select_map / select_game_mode menus only set ui_mapname / ui_gametype; the
// members only see them once the host sends partystate again. Stock lobby menus do that
// on menu load, which the menus opened by the host menu skip. Only armed while the menu
// the host menu opened is still open and a party is hosted, so stock flows (public
// lobbies, the stock private-match menus) never send partystate from here. Every select
// menu the host menu opened is bound on its own, so a second one (Select Gametype over
// Select Map) doesn't end the sync while the first is still open. Disarmed while no
// bound menu is left; the stack never holds more menus than its max. A change that comes
// with a moved LobbyParty ring is the game's own rotation and only becomes the baseline.
static void* mapSyncMenus[T5_UiMenuStackMax];
static int mapSyncMenuCount;
static char syncedMap[64];
static char syncedGametype[64];
static int syncedRingIndex;

static void store_synced_party_map() {
    host_menu_read_dvar_string("ui_mapname", syncedMap, sizeof(syncedMap));
    host_menu_read_dvar_string("ui_gametype", syncedGametype, sizeof(syncedGametype));
    syncedRingIndex = PARTY_MAP_RING_INDEX(T5_LobbyParty);
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

void party_map_clear_sync() { mapSyncMenuCount = 0; }

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
void party_map_arm_sync(void* uiContext, int openMenuCountBefore) {
    if (!host_menu_party_is_local_host(T5_LobbyParty) &&
        !host_menu_party_is_local_host(T5_GameParty)) {
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
            if (playlist_slot_wager_cost(slot) <= 0 &&
                (best < 0 || matches * bestCount > bestMatches * count)) {
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
// playlist, -1 while none is kept. The switch stays for the following matches (their
// rules and the rotation after them come from it), only the session keeps being offered
// in this playlist, in the lobby and in game, so players searching it keep finding it.
static int matchmakingPlaylist = -1;

typedef enum {
    ROTATION_PUSHED,
    ROTATION_PLAYLIST_SWITCHED,
    ROTATION_MISSING,
    ROTATION_HOST_GAMETYPE,
    ROTATION_HOST_GAMETYPE_MAP_MISSING,
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
    if (mapChanged && index < 0) {
        printf(T5WARNING
               "%s is not in the current playlist rotation, members keep the old "
               "map.",
               map);
        return ROTATION_HOST_GAMETYPE_MAP_MISSING;
    }
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
    // The game's rotation (0x1F0268) moved the ring and already sent partystate; our own
    // pushes re-baseline the ring index right away. A host selection in the same frame is
    // dropped, the rotation overwrote it anyway.
    bool rotatedByGame =
        lobbyHosted && PARTY_MAP_RING_INDEX(T5_LobbyParty) != syncedRingIndex;
    store_synced_party_map();
    if (rotatedByGame) {
        printf(T5INFO "The game's rotation set %s / %s, not syncing it.", map, gametype);
        return;
    }
    // The game's rotation only uses the LobbyParty ring (0x1F0268).
    if (lobbyHosted && party_map_uses_rotation_ring()) {
        RotationResult result =
            push_party_rotation_map(T5_LobbyParty, map, gametype, mapChanged);
        if (result == ROTATION_PLAYLIST_SWITCHED) {
            host_menu_ui_show_status("Switched to a playlist with this map and mode");
        } else if (result == ROTATION_HOST_GAMETYPE) {
            host_menu_ui_show_status("Wager mode: players see the old mode in the lobby");
        } else if (result == ROTATION_HOST_GAMETYPE_MAP_MISSING) {
            host_menu_ui_show_status("Wager mode: players keep the old map and mode");
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
static bool wagerMatchWasInGame;

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

// Before the gametype scripts run, they read xblive_wagermatch at their start.
void party_map_on_server_scripts_loaded() {
    if (wager_match_wanted()) {
        force_wager_match();
    } else {
        restore_wager_match();
    }
}

// The host forces it on the server script load already
// (party_map_on_server_scripts_loaded), members once they are in the game. Restored after
// the match, not before it: the host loads the scripts before it is in the game, with
// sv_running already set. Without sv_running a load that never reached the game was
// aborted, the forced value must not stay for the next (possibly public wager) match.
// The gametype can't change while cl_ingame stays set, so the check (a stringtable
// lookup) runs once when entering the game instead of every frame.
void party_map_update_wager_match() {
    bool inGame = Dvar_GetBool("cl_ingame");
    bool enteredGame = inGame && !wagerMatchWasInGame;
    wagerMatchWasInGame = inGame;
    if (enteredGame && wager_match_wanted()) {
        force_wager_match();
        wagerMatchSeenInGame = true;
    } else if (!inGame && (wagerMatchSeenInGame || !Dvar_GetBool("sv_running"))) {
        restore_wager_match();
    }
}

// The game writes the session's playlist again from the playlist dvar at the match start
// (0x20D420) and when it recreates the session, the switched one would then hide the
// lobby from the original playlist, so it is checked every frame, in game as well.
// Whether LobbyParty stays active and hosted during the match is unverified: while
// loading and in game the running server stands for hosting, only without it does the
// lobby decide that the host left.
void party_map_keep_matchmaking_playlist(bool inGame) {
    if (matchmakingPlaylist < 0) {
        return;
    }
    dvar_s* playlistDvar = Dvar_FindVar("playlist");
    if (!playlistDvar || !party_map_uses_rotation_ring() ||
        playlistDvar->current.integer == matchmakingPlaylist) {
        matchmakingPlaylist = -1;
        return;
    }
    if (!host_menu_party_is_local_host(T5_LobbyParty) && !Dvar_GetBool("sv_running")) {
        if (!inGame) {
            matchmakingPlaylist = -1;
        }
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

// The game's list stops at maxnum_gametype, which leaves out the wager gametypes the
// table has right after it. Adds the following indices like UI_AssetCache does, in the
// table's order: the menu looks description and icon up by list index. UI_AssetCache may
// load the list again, so this runs before every open.
void party_map_extend_gametype_list() {
    void* table = NULL;
    StringTable_Find(GAMETYPES_TABLE, &table);
    if (!table) {
        return;
    }
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

void party_map_sync(void* uiContext) {
    if (!mapSyncMenuCount) {
        return;
    }
    bool lobbyHosted = host_menu_party_is_local_host(T5_LobbyParty);
    bool gameHosted = host_menu_party_is_local_host(T5_GameParty);
    // The host left the lobby (e.g. to the main menu); the sync must not carry over to
    // the next one.
    if (!lobbyHosted && !gameHosted) {
        party_map_clear_sync();
        return;
    }
    // Before the close check, so a selection made in the frame the menu closes still
    // goes out.
    send_party_map_if_changed(lobbyHosted, gameHosted);
    prune_closed_map_sync_menus(uiContext);
}
