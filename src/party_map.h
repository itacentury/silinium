#ifndef PARTY_MAP_H
#define PARTY_MAP_H

/*
    Map and gametype selection for the hosted party (MP only): sends the host menu's
    select_map / select_game_mode choice to the members through the playlist rotation,
    switches the playlist if needed and handles the wager gametypes.
*/

#include <stdbool.h>

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
