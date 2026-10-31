#include "scrfct.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_menu.h"
#include "t5.h"
#include "utils.h"

void scrfct_setmemory() {
    // Verify that we have 2 parameters for this function.
    if (Scr_GetNumParam(0) == 2) {
        // Getting parameters, we don't check the type using a function again here but we
        // could have did it.
        char* hexAddress = Scr_GetString(0, 0);
        char* hexData = Scr_GetString(1, 0);

        if (hexAddress && hexData) {
            // Allocate input size +2 in case to handle zero padding and null terminated
            // char.
            char hexAddressFixed[strlen(hexAddress) + 2];
            char hexDataFixed[strlen(hexData) + 2];

            hex_str_to_padded_hex_str(hexAddressFixed, hexAddress);
            hex_str_to_padded_hex_str(hexDataFixed, hexData);

            size_t addressSize = strlen(hexAddressFixed) / 2;
            size_t dataSize = strlen(hexDataFixed) / 2;

            int offset = hex_str_to_int32(hexAddressFixed, addressSize);

            char buffer[dataSize];
            memset(buffer, 0, dataSize);
            hex_str_to_buffer(buffer, hexDataFixed, dataSize);

            sys_dbg_process_write(offset, buffer, dataSize);

            printf(T5INFO
                   "Function 'setMemory' called from gsc with the following parameters: "
                   "%s, %s.",
                   hexAddress, hexData);
        } else
            printf(T5ERROR "Cannot resolve setmemory parameters call from gsc.");
    }
}

// callkeyboard(title, clientNum): opens the keyboard on the host console and returns 1,
// or 0 if another keyboard request is still running. The text is sent to the player as
// notify: self waittill("keyboard_input", text);
void scrfct_callkeyboard() {
    if (Scr_GetNumParam(SCRIPTINSTANCE_SERVER) != 2) {
        printf(T5ERROR "'callkeyboard' needs 2 parameters (title, clientNum).");
        Scr_AddInt(0, SCRIPTINSTANCE_SERVER);
        return;
    }

    const char* title = Scr_GetString(0, SCRIPTINSTANCE_SERVER);
    int clientNum = Scr_GetInt(1, SCRIPTINSTANCE_SERVER);
    bool opened = title && host_menu_request_keyboard(title, clientNum);
    if (!opened)
        printf(T5WARNING
               "'callkeyboard' for client %i ignored, a keyboard is still open.",
               clientNum);
    Scr_AddInt(opened, SCRIPTINSTANCE_SERVER);
}