#ifndef ANTI_GOD_CLASS_H
#define ANTI_GOD_CLASS_H

#include <stdint.h>

#include "defines.h"
#include "offsets.h"

// The game passes netadr_t by value in r3-r5 (assumed from the disassembly). The hook
// takes the three register words as they are instead of depending on how the compiler
// would pass a 20-byte struct.
t5nhd(void, SV_ReceiveStats, (uint64_t from0, uint64_t from1, uint64_t from2, void* msg));
t5nd(void, player_die,
     (void* self, void* inflictor, void* attacker, int damage, int meansOfDeath,
      int weapon, const float* dir, int hitLoc, int psTimeOffset));
t5nd(void*, Com_GetServerDObj, (int entnum));
t5nd(void, G_DObjUpdate, (void* ent));

// Installs the stats hook that fixes godmode/invisible custom classes of clients (MP
// only, needs the host menu state).
int anti_god_class_init();

// Checks the clients whose stats are already in, for turning the option on in game, and
// kills a living player whose classes were fixed. Server thread only, the toggle queues
// it for ClientEndFrame.
void anti_god_class_check_clients();

#endif /* ANTI_GOD_CLASS_H */
