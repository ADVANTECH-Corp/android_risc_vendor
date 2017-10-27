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

#define LOG_TAG "RIL-AUDIO"
#include <utils/Log.h>

#ifdef RIL_SHLIB
extern const struct RIL_Env *s_rilenv;
#endif

void requestQueryTtyMode(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    int value;

    err = at_send_command_singleline("AT#TTY?",
            "#TTY:",
            &p_response,
            AT_TIMEOUT_NORMAL);
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &value);
    if (err < 0) goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &value, sizeof(int *));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestSetTtyMode(void *data,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    int ttyMode;

    ttyMode = ((int *)data)[0];
    switch (ttyMode) {
        case 0:
        case 1:
            asprintf(&cmd,
                    "AT#TTY=%d",
                    ttyMode);
        break;
        default:
            RIL_onRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
        return;
    }

    err = at_send_command(cmd, &p_response, AT_TIMEOUT_NORMAL);
    free(cmd);
    if (err < 0 || p_response->success == 0)
        goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}
