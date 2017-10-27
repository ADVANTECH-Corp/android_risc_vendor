/*
 * Copyright 2013, Telit Communications S.p.a.
 *
 * This file is licensed under a commercial license agreement.
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <alloca.h>
#include <getopt.h>
#include <sys/socket.h>
#include <termios.h>
#include <cutils/sockets.h>
#include <cutils/properties.h>

#include "ril.h"
#include <telephony/ril_cdma_sms.h>
#include "reference-ril.h"
#include "misc.h"
#include "serial/ril-serial.h"
#include "serial/atchannel.h"
#include "serial/at_tok.h"
#include "serial/at_timeout.h"

#define LOG_TAG "RIL-PM"
#include <utils/Log.h>

#ifdef RIL_SHLIB
extern const struct RIL_Env *s_rilenv;
#endif

extern int powerManagement;

void requestScreenState(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    int screenState;

    if (0 == powerManagement) {
        /* Power management not enabled: do nothing */
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

        return;
    }

    screenState = ((int *)data)[0];

    /* For USB connections selective suspend and remote wake-up
     * should be enabled for proper working
     */
    if (0 == screenState) {
        err = at_send_command("AT+CREG=1;+CGREG=1;#NITZ=0,0;+CSSN=0,0;"
                "#PSNT=0;+CFUN=5",
                NULL,
                AT_TIMEOUT_NORMAL);
        sleep(2);
    } else {
        sleep(1);
        err = at_send_command("AT+CFUN=1", NULL, AT_TIMEOUT_NORMAL);
        err |= at_send_command("AT+CREG=2;+CGREG=2", NULL, AT_TIMEOUT_NORMAL);
        err |= at_send_command("AT#NITZ=9,1", NULL, AT_TIMEOUT_NORMAL);
        err |= at_send_command("AT+CSSN=1,1", NULL, AT_TIMEOUT_NORMAL);
        err |= at_send_command("AT#PSNT=1", NULL, AT_TIMEOUT_NORMAL);
    }

    if (err < 0)
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}
