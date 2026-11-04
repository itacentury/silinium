#include "client_command.h"

#include "anti_freeze.h"
#include "cshook.h"
#include "host_menu.h"
#include "t5.h"

// Runs on the host for every command a client sends. Both filters only read the command
// and raw game memory, so they run even if the other module failed to init.
void ClientCommand_Hook(int clientNum) {
    char cmd[37] = {0};
    SV_Cmd_ArgvBuffer(0, cmd, sizeof(cmd));

    if (!anti_freeze_client_command_ok(clientNum, cmd) ||
        !host_menu_client_command_ok(clientNum, cmd))
        return;

    ClientCommand_Trampoline(clientNum);
}

static bool installed;

int client_command_init() {
    if (installed) return 0;

    t5nmhi(ClientCommand);

    int res;
    if ((res = cs_hook_install(ClientCommand, CS_HOOK_TYPE_CTR)) < 0) return res;

    installed = true;
    return 0;
}
