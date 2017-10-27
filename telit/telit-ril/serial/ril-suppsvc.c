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

#define LOG_TAG "RIL-SUPPSVC"
#include <utils/Log.h>

#ifdef RIL_SHLIB
extern const struct RIL_Env *s_rilenv;
#endif

void requestCancelUSSD(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int           err;
    ATResponse   *p_response = NULL;

    err = at_send_command_numeric("AT+CUSD=2", &p_response, AT_TIMEOUT_NORMAL);
    if (err < 0 || p_response->success == 0)
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
}

void requestSendUSSD(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    char* cmd;
    ATResponse *p_response = NULL;
    const char *ussdRequest;

    ussdRequest = (char *)(data);
    asprintf(&cmd, "AT+CUSD=1,\"%s\"", ussdRequest);

    err = at_send_command(cmd, &p_response, AT_TIMEOUT_NORMAL);
    free(cmd);
    if (err < 0 || p_response->success == 0)
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
}

void requestSetSuppSvcNotification(void *data,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    int enabled;

    enabled = ((int *)data)[0];

    asprintf(&cmd, "AT+CSSN=%d,%d", enabled, enabled);

    err = at_send_command(cmd, &p_response, AT_TIMEOUT_NORMAL);
    free(cmd);
    if (err < 0 || p_response->success == 0)
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
}
