#ifndef ANTI_GOD_CLASS_H
#define ANTI_GOD_CLASS_H

#include <stdint.h>

#include "defines.h"
#include "offsets.h"

// The game passes netadr_t by value in r3-r5 (assumed from the disassembly). The hook takes
// the three register words as they are instead of depending on how the compiler would pass
// a 20-byte struct.
t5nhd(void, SV_ReceiveStats, (uint64_t from0, uint64_t from1, uint64_t from2, void* msg));

// Installs the stats hook that fixes godmode/invisible custom classes of clients (MP
// only, needs the host menu state).
int anti_god_class_init();

// Checks the clients whose stats are already in, for turning the option on in game.
// Server thread only, the toggle queues it for ClientEndFrame.
void anti_god_class_check_clients();

#endif /* ANTI_GOD_CLASS_H */
