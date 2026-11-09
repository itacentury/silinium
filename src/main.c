#include <stdio.h>
#include <sys/ppu_thread.h>
#include <sys/prx.h>
#include <sys/sys_time.h>
#include <sys/timer.h>

#include "notify.h"
#include "t5.h"
#include "utils.h"

SYS_MODULE_INFO(Silinium, 0, 1, 1);
SYS_MODULE_START(start);

// The launcher runs right after the modules are loaded, possibly before the game has
// initialized its network, so the first attempts may fail; sys_net is expected to just
// return an error then (unverified on console). The deadline keeps the retries short in
// case connect isn't bounded by SO_SNDTIMEO, though a single blocking connect can't be
// cut short.
#define LOADED_NOTIFY_ATTEMPTS 10
#define LOADED_NOTIFY_DEADLINE_US (15 * 1000 * 1000)

static void notify_loaded() {
    system_time_t startTime = sys_time_get_system_time();
    for (int attempt = 0; attempt < LOADED_NOTIFY_ATTEMPTS; attempt++) {
        if (notify_popup_quiet("Silinium loaded", NOTIFY_ICON_INFO, NOTIFY_SOUND_NONE)) {
            return;
        }
        if (attempt + 1 == LOADED_NOTIFY_ATTEMPTS ||
            sys_time_get_system_time() - startTime >= LOADED_NOTIFY_DEADLINE_US) {
            break;
        }
        sys_timer_sleep(1);
    }
    printf(T5WARNING "No webMAN on 127.0.0.1, the loaded notification was not shown.");
}

void launcher() {
    sys_prx_get_module_list_t pInfo;
    pInfo.max = 25;
    sys_prx_id_t ids[pInfo.max];
    pInfo.idlist = ids;
    pInfo.size = sizeof(pInfo);

    while (pInfo.count < 18) {
        sys_prx_get_module_list(0, &pInfo);
        sys_timer_sleep(1);
    }

    if (init_game() == 0) {
        printf(T5INFO "Silinium ready.");
        // Notifications are MP only, the sys_net stubs have no ZM offsets.
        if (isMultiplayer) {
            notify_loaded();
        }
    }

    sys_ppu_thread_exit(0);
}

int start(void) {
    printf("\n********************************************");
    printf("            Silinium by Century             ");
    printf("        fork of T5GSCLoader by iMCSx        ");
    printf("********************************************\n");
    printf(T5INFO "Waiting modules...");

    // Create a thread that wait eboot's modules are getting loaded to be sure that
    // imports opd are resolved.
    sys_ppu_thread_t id;
    sys_ppu_thread_create(&id, launcher, 0, 1000, 0x8000, 0, "[Silinium] launcher");
    return SYS_PRX_RESIDENT;
}
