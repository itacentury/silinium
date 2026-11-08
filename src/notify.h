#ifndef NOTIFY_H
#define NOTIFY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "defines.h"

struct sockaddr;

// The game's sys_net imports, named apart from the SDK's declarations. Point to the
// resolved libnet functions, not to the game's stubs (see notify_init).
t5nd(int, Net_socket, (int domain, int type, int protocol));
t5nd(int, Net_connect, (int s, const struct sockaddr* name, unsigned int nameLength));
t5nd(int, Net_send, (int s, const void* message, size_t length, int flags));
t5nd(int, Net_recv, (int s, void* buffer, size_t length, int flags));
t5nd(int, Net_setsockopt,
     (int s, int level, int name, const void* value, unsigned int valueLength));
t5nd(int, Net_socketclose, (int s));

// Indices into webMAN MOD's rco_images[] (vsh_notify.h), not game values.
typedef enum {
    NOTIFY_ICON_INFO = 0,
    NOTIFY_ICON_FRIEND = 1,
    NOTIFY_ICON_CAUTION = 3,
    NOTIFY_ICON_SETTINGS = 8,
    NOTIFY_ICON_TROPHY_BRONZE = 9,
    NOTIFY_ICON_TROPHY_SILVER = 10,
    NOTIFY_ICON_TROPHY_GOLD = 11,
    NOTIFY_ICON_TROPHY_PLATINUM = 12,
    NOTIFY_ICON_GAME = 33,
    NOTIFY_ICON_ERROR = 35,  // tex_indi_Sign_out, webMAN's own error icon
} NotifyIcon;

// webMAN MOD's play_sound_id() IDs.
typedef enum {
    NOTIFY_SOUND_NONE = -1,
    NOTIFY_SOUND_CANCEL = 0,
    NOTIFY_SOUND_BEEP1 = 1,
    NOTIFY_SOUND_BEEP2 = 2,
    NOTIFY_SOUND_BEEP3 = 3,
    NOTIFY_SOUND_CURSOR = 4,
    NOTIFY_SOUND_TROPHY = 5,
    NOTIFY_SOUND_DECIDE = 6,
    NOTIFY_SOUND_OPTION = 7,
    NOTIFY_SOUND_OK = 8,
    NOTIFY_SOUND_ERROR = 9,
} NotifySound;

// MP only, the stubs have no ZM offsets.
void notify_init();
// Shows text as a PS3 (XMB) notification through webMAN's /popup.ps3 on 127.0.0.1. The
// notification lives in the VSH process, which a plugin in the game can't call, so
// without webMAN nothing is shown. webMAN drops the icon while a game is running
// (IS_INGAME in show_msg_with_icon), so in BO1 it most likely shows its default icon;
// unverified on console. Blocks for up to about half a second (send and receive
// timeout of 250 ms each, connect timeout unverified), not for the render thread.
// Returns whether webMAN took the request.
bool notify_popup(const char* text, NotifyIcon icon, NotifySound sound);

#endif /* NOTIFY_H */
