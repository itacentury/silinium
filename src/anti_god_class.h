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
t5nd(unsigned int, FindVariable,
     (unsigned int inst, unsigned int parentId, unsigned int name));
t5nd(void, RemoveVariable, (unsigned int inst, unsigned int parentId, unsigned int name));
t5nhd(unsigned int, SL_TransferToCanonicalString,
      (unsigned int inst, unsigned int stringValue));
t5nd(const char*, SL_ConvertToString, (unsigned int stringValue, unsigned int inst));

// Installs the stats hook that fixes godmode/invisible custom classes of clients and the
// compile hook that finds the class script fields (MP only, needs the host menu state).
int anti_god_class_init();

// Whether both hooks are installed, the host menu hides the option otherwise.
bool anti_god_class_available();

// Checks the clients whose stats are already in, for turning the option on in game, and
// kills a living player who plays a fixed godmode class. Server thread only, the toggle
// queues it for ClientEndFrame.
void anti_god_class_check_clients();

// Drops all queued kills, on every server script load, since a new match reuses the
// slots. Also drops the ids of the class fields this load didn't compile.
void anti_god_class_reset();

// Runs the kill anti_god_class_check_clients queued for this player once the game built
// its DObj. Server thread, from ClientEndFrame.
void anti_god_class_client_frame(void* ent);

#endif /* ANTI_GOD_CLASS_H */
