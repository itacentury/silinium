#ifndef CLIENT_COMMAND_H
#define CLIENT_COMMAND_H

/*
    Owns the ClientCommand hook and runs the filters of the anti-freeze and the host menu.
   Both install it, so neither depends on the other's init. MP only.
*/

#include <stdbool.h>
#include <types.h>

#include "defines.h"
#include "offsets.h"

t5nhd(void, ClientCommand, (int clientNum));

// Installs the hook, a second call does nothing.
int client_command_init();

#endif /* CLIENT_COMMAND_H */
