#include "notify.h"

#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "defines.h"
#include "offsets.h"

#define WEBMAN_PORT 80
// webMAN answers over loopback within milliseconds; keep this short, the menu thread
// that calls notify_popup also handles pad input.
#define WEBMAN_TIMEOUT_MS 250

#define PPC_ORIS_R12_R12 0x658C0000
#define PPC_LWZ_R12_R12 0x818C0000

static bool netResolved;

// The function an import stub calls, read from the import slot the stub loads like
// cs_hook_resolve_import_opd does. The stub itself can't be called from the plugin: it
// saves r2 into the caller's TOC save slot, so the caller would restore the game's TOC
// instead of its own. NULL if the stub doesn't have the expected form.
static popd32 resolve_import(uint32_t stub) {
    uint32_t high = *(uint32_t*)(stub + 4);
    uint32_t low = *(uint32_t*)(stub + 8);
    if ((high & 0xFFFF0000) != PPC_ORIS_R12_R12 ||
        (low & 0xFFFF0000) != PPC_LWZ_R12_R12) {
        return NULL;
    }
    return *(popd32*)(((high & 0xFFFF) << 16) + (short)(low & 0xFFFF));
}

static bool url_safe(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

// Percent-encodes text into out (size bytes), cut off where the next character no longer
// fits.
static void url_encode(const char* text, char* out, size_t size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t length = 0;
    for (; *text; text++) {
        unsigned char c = *text;
        if (url_safe(c)) {
            if (length + 1 >= size) {
                break;
            }
            out[length++] = c;
            continue;
        }
        if (length + 3 >= size) {
            break;
        }
        out[length++] = '%';
        out[length++] = hex[c >> 4];
        out[length++] = hex[c & 0xF];
    }
    out[length] = 0;
}

// Whether lv2 also bounds connect with SO_SNDTIMEO is unverified.
static void set_timeouts(int s) {
    struct timeval timeout = {0, WEBMAN_TIMEOUT_MS * 1000};
    Net_setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    Net_setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

bool notify_popup(const char* text, NotifyIcon icon, NotifySound sound) {
    if (!netResolved) {
        return false;
    }
    static bool warned;
    char encoded[256];
    url_encode(text, encoded, sizeof(encoded));
    // webMAN cuts the text at "&snd=" before it parses "&icon=", so the sound goes last.
    char soundParameter[8] = "";
    if (sound != NOTIFY_SOUND_NONE) {
        snprintf(soundParameter, sizeof(soundParameter), "&snd=%d", sound);
    }
    char request[352];
    int length =
        snprintf(request, sizeof(request),
                 "GET /popup.ps3?%s&icon=%d%s HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n",
                 encoded, icon, soundParameter);
    if (length < 0 || length >= (int)sizeof(request)) {
        return false;
    }

    int s = Net_socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return false;
    }
    set_timeouts(s);

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_len = sizeof(address);
    address.sin_family = AF_INET;
    address.sin_port = htons(WEBMAN_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // Reaching the VSH's webMAN over loopback from the game process is unverified.
    if (Net_connect(s, (struct sockaddr*)&address, sizeof(address)) != 0) {
        if (!warned) {
            warned = true;
            printf(T5WARNING
                   "No webMAN on 127.0.0.1:%d, notifications only show in the menu.",
                   WEBMAN_PORT);
        }
        Net_socketclose(s);
        return false;
    }

    bool sent = Net_send(s, request, length, 0) == length;
    if (sent) {
        // Closing before webMAN answered could reset the connection before it read the
        // request, so wait for the first bytes of the answer.
        char answer[16];
        Net_recv(s, answer, sizeof(answer), 0);
    }
    Net_socketclose(s);
    return sent;
}

void notify_init() {
    Net_socket = (Net_socket_t)resolve_import(T5_Net_socket);
    Net_connect = (Net_connect_t)resolve_import(T5_Net_connect);
    Net_send = (Net_send_t)resolve_import(T5_Net_send);
    Net_recv = (Net_recv_t)resolve_import(T5_Net_recv);
    Net_setsockopt = (Net_setsockopt_t)resolve_import(T5_Net_setsockopt);
    Net_socketclose = (Net_socketclose_t)resolve_import(T5_Net_socketclose);
    netResolved = Net_socket && Net_connect && Net_send && Net_recv && Net_setsockopt &&
                  Net_socketclose;
    if (!netResolved) {
        printf(T5WARNING "sys_net imports not resolved, PS3 notifications are disabled.");
    }
}
