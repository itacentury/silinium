#ifndef ANTI_FREEZE_H
#define ANTI_FREEZE_H

/*
    Drops server commands and text from other consoles that freeze the game (the "Freeze
   PS3" options of RTM tools). MP only.
*/

#include <stdbool.h>
#include <types.h>

#include "defines.h"
#include "offsets.h"

t5nd(int, BG_GetNumWeapons, ());
t5nd(void, Cmd_TokenizeString, (const char* text));
t5nd(void, Cmd_TokenizeStringWithLimit, (const char* text, int maxTokens));
t5nd(void, Cmd_EndTokenizedString, ());
t5nd(void*, Sys_GetValue, (int index));
t5nd(long, game_strtol, (const char* s, char** end, int base));

t5nhd(int, CL_CGameNeedsServerCommand, (int localClientNum, int serverCommandNumber));
t5nhd(void*, AddBaseDrawTextCmd,
      (const char* text, int maxChars, void* font, float x, float y, float w,
       float xScale, float yScale, float rotation, const float* color, int style,
       int cursorPos, char cursor));
t5nhd(void, SV_UserinfoChanged, (void* cl));
t5nhd(void, SV_GetUserinfo, (int index, char* buffer, int bufferSize));
t5nhd(void, Party_WriteMembers,
      (int party, int unknown, int firstMember, int targetMember, void* msg));
t5nhd(void, DB_LoadXAssets, (void* zoneInfo, int zoneCount, int sync));

int anti_freeze_init();
// Host side: false if the client command being run (say, say_team) carries a text icon,
// see anti_freeze.c. Called from the ClientCommand hook, see client_command.c.
bool anti_freeze_client_command_ok(int clientNum, const char* cmd);

#endif /* ANTI_FREEZE_H */
