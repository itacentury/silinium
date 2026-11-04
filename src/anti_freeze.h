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

t5nhd(int, CL_CGameNeedsServerCommand, (int localClientNum, int serverCommandNumber));
t5nhd(void*, AddBaseDrawTextCmd,
      (const char* text, int maxChars, void* font, float x, float y, float w,
       float xScale, float yScale, float rotation, const float* color, int style,
       int cursorPos, char cursor));

int anti_freeze_init();
// Host side: false if the client command being run (say, say_team) carries a text icon,
// see anti_freeze.c.
bool anti_freeze_client_command_ok(int clientNum, const char* cmd);

#endif /* ANTI_FREEZE_H */
