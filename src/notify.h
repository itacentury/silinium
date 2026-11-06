#ifndef NOTIFY_H
#define NOTIFY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "defines.h"

struct sockaddr;

// The game's sys_net imports, named apart from the SDK's declarations.
t5nd(int, Net_socket, (int domain, int type, int protocol));
t5nd(int, Net_connect, (int s, const struct sockaddr* name, unsigned int nameLength));
t5nd(int, Net_send, (int s, const void* message, size_t length, int flags));
t5nd(int, Net_recv, (int s, void* buffer, size_t length, int flags));
t5nd(int, Net_setsockopt,
     (int s, int level, int name, const void* value, unsigned int valueLength));
t5nd(int, Net_socketclose, (int s));

// MP only, the stubs have no ZM offsets.
void notify_init();
// Shows text as a PS3 (XMB) notification through webMAN's /popup.ps3 on 127.0.0.1. The
// notification lives in the VSH process, which a plugin in the game can't call, so
// without webMAN nothing is shown. Blocks for up to a second, not for the render thread.
// Returns whether webMAN took the request.
bool notify_popup(const char* text);

#endif /* NOTIFY_H */
