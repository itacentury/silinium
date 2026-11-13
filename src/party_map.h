#ifndef PARTY_MAP_H
#define PARTY_MAP_H

/*
    Map and gametype selection for the hosted party (MP only): sends the host menu's
    select_map / select_game_mode choice to the members through the playlist rotation,
    switches the playlist if needed and handles the wager gametypes.
*/

#include <stdbool.h>
#include <stdint.h>

#include "defines.h"
#include "host_menu.h"
#include "offsets.h"

#define PLAYLIST_ROTATION_FIRST(i) \
    (*(int*)(PLAYLIST_ENTRY(i) + T5_PlaylistRotationFirstOffset))
#define PLAYLIST_ROTATION_COUNT(i) \
    (*(int*)(PLAYLIST_ENTRY(i) + T5_PlaylistRotationCountOffset))
#define PLAYLIST_WAGER_COST1(i) (*(int*)(PLAYLIST_ENTRY(i) + T5_PlaylistWagerCostOffset1))
#define PLAYLIST_WAGER_COST2(i) (*(int*)(PLAYLIST_ENTRY(i) + T5_PlaylistWagerCostOffset2))
#define PLAYLIST_ROTATION_ENTRY(i) \
    (T5_PlaylistRotationEntries + (i) * T5_PlaylistRotationEntrySize)
#define PLAYLIST_ROTATION_MAP(i) (*(const char**)PLAYLIST_ROTATION_ENTRY(i))
#define PLAYLIST_ROTATION_GAMETYPE(i) (*(const char**)(PLAYLIST_ROTATION_ENTRY(i) + 4))
#define PLAYLIST_GAMETYPE_ENTRY(i) (T5_PlaylistGametypes + (i) * T5_PlaylistGametypeSize)
#define PLAYLIST_GAMETYPE_NAME(i) (*(const char**)PLAYLIST_GAMETYPE_ENTRY(i))
#define PLAYLIST_GAMETYPE_PLAYLIST_NAME(i) \
    (*(const char**)(PLAYLIST_GAMETYPE_ENTRY(i) + 4))
#define SESSION_INFO (*(int*)T5_SessionInfo)
#define SESSION_INFO_PLAYLIST \
    (SESSION_INFO ? *(int*)(SESSION_INFO + T5_SessionInfoPlaylistOffset) : -1)
#define UI_GAMETYPE_COUNT (*(int*)T5_UiGametypeCount)
#define UI_GAMETYPE(i) (T5_UiGametypes + (i) * T5_UiGametypeSize)
#define UI_GAMETYPE_NAME(i) ((char*)UI_GAMETYPE(i))
#define UI_GAMETYPE_LOCALIZED(i) ((char*)(UI_GAMETYPE(i) + T5_UiGametypeLocalizedOffset))
#define UI_GAMETYPE_SPLITSCREEN(i) \
    (*(int*)(UI_GAMETYPE(i) + T5_UiGametypeSplitscreenOffset))
#define PARTY_MAP_RING_INDEX(party) (*(int*)((party) + T5_PartyMapRingIndexOffset))
#define PARTY_MAP_RING_ENTRY(party, i) \
    (*(uint8_t*)((party) + T5_PartyMapRingOffset + (i)))

t5nd(bool, Com_IsPublicOnlineGame, ());
t5nd(void, Playlist_SetRotationEntry, (int localClientNum, int rotationIndex));
t5nd(void, Dvar_SetInt, (dvar_s * dvar, int value));
t5nd(void, Session_SetPlaylist, (int localClientNum, int playlist));
t5nd(void, StringTable_Find, (const char* name, void** table));
t5nd(const char*, StringTable_Lookup,
     (void* table, int compareColumn, const char* value, int valueColumn));

// Resolves the natives of this module (MP only).
void party_map_init();
// Binds the sync to the menu Menus_OpenByName just pushed. UI thread, in the lobby.
void party_map_arm_sync(void* uiContext, int openMenuCountBefore);
// Sends a changed map / gametype to the members while a bound menu is open. UI thread.
void party_map_sync(void* uiContext);
void party_map_clear_sync();
// Adds the wager gametypes to the game's gametype list, before select_game_mode opens.
void party_map_extend_gametype_list();
// Keeps the session offered in its playlist after a playlist switch. Every frame.
void party_map_keep_matchmaking_playlist(bool inGame);
// Forces xblive_wagermatch for a wager gametype in a private match and restores it after.
// Every frame.
void party_map_update_wager_match();
// Called once per server script load, before the gametype scripts run.
void party_map_on_server_scripts_loaded();

#endif /* PARTY_MAP_H */
