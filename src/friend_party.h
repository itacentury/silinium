#ifndef FRIEND_PARTY_H
#define FRIEND_PARTY_H

/*
    Friends As Party (MP only): shows the local player's PSN friends in the party color
    (blue compass arrow and name) like party members, as the PC version does with Steam
    friends. Client side, only the console running the plugin sees it.
*/

#include <stdbool.h>
#include <stdint.h>

#include "defines.h"

#define FRIEND_PARTY_MAX_CLIENTS 18

t5nhd(int, CG_IsPartyMember, (int localClientNum, int clientNum));

// Resolves the friend imports and installs the hook. Returns the negative hook error, the
// feature only disables itself if the imports aren't resolved.
int friend_party_init();
// Rereads the friends list and matches it against the clients' names, at most every
// FRIEND_PARTY_REFRESH_US. Main thread, every frame.
void friend_party_refresh(bool inGame);

#endif /* FRIEND_PARTY_H */
