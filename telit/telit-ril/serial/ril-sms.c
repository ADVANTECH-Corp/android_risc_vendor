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

#define LOG_TAG "RIL-SMS"
#include <utils/Log.h>

#ifdef RIL_SHLIB
extern const struct RIL_Env *s_rilenv;
#endif

extern int s_ims_gsm_retry;
extern int s_ims_gsm_fail;

void requestDeleteSmsOnSim(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;

    asprintf(&cmd, "AT+CMGD=%d", ((int *)data)[0]);
    err = at_send_command(cmd, &p_response, AT_TIMEOUT_CMGD);
    free(cmd);
    if (err < 0 || p_response->success == 0)
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
}

void requestWriteSmsToSim(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    char *line = NULL;
    RIL_SMS_WriteArgs *p_args;
    int index;
    int length;

    p_args = (RIL_SMS_WriteArgs *)data;

    length = strlen(p_args->pdu)/2;
    asprintf(&cmd, "AT+CMGW=%d,%d", length, p_args->status);

    err = at_send_command_sms(cmd,
            p_args->pdu,
            "+CMGW:",
            &p_response,
            AT_TIMEOUT_CMGW);
    free(cmd);
    if (err != 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &index);
    if (err < 0) goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &index, sizeof(int *));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestSMSAcknowledge(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    int ackSuccess;

    ackSuccess = ((int *)data)[0];

    if (1 == ackSuccess)
        err = at_send_command("AT+CNMA=0", NULL, AT_TIMEOUT_NORMAL);
    else if (0 == ackSuccess)
        err = at_send_command("AT+CNMA=2", NULL, AT_TIMEOUT_NORMAL);
    else
        goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

void requestSendSMS(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    char *cmd1, *cmd2;
    ATResponse *p_response = NULL;
    char *line = NULL;
    RIL_SMS_Response response;
    const char *smsc;
    const char *pdu;
    int tpLayerLength;
    int value;

    memset(&response, 0, sizeof(response));
    RLOGD("requestSendSMS datalen =%zu", datalen);

    if (s_ims_gsm_fail != 0) goto error;
    if (s_ims_gsm_retry != 0) goto error2;

    smsc = ((const char **)data)[0];
    pdu = ((const char **)data)[1];

    tpLayerLength = strlen(pdu)/2;

    /* "NULL for default SMSC" */
    if (smsc == NULL)
        smsc= "00";

    asprintf(&cmd1, "AT+CMGS=%d", tpLayerLength);
    asprintf(&cmd2, "%s%s", smsc, pdu);

    err = at_send_command_sms(cmd1, cmd2, "+CMGS:", &p_response, AT_TIMEOUT_CMGS);
    free(cmd1);
    free(cmd2);
    if (err != 0 || p_response->success == 0) goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &value);
    if (err < 0) goto error;

    response.messageRef = value;
    response.ackPDU = NULL;
    response.errorCode = 0;
    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(response));

    at_response_free(p_response);
    return;

error:
    response.messageRef = -2;
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, &response, sizeof(response));

    at_response_free(p_response);
    return;

error2:
    /* send retry error */
    response.messageRef = -1;
    RIL_onRequestComplete(t, RIL_E_SMS_SEND_FAIL_RETRY, &response, sizeof(response));

    at_response_free(p_response);
}

void requestSendSmsExpectMore(void *data, size_t datalen, RIL_Token t)
{
    at_send_command("AT+CMMS=1", NULL, AT_TIMEOUT_NORMAL);
    requestSendSMS(data, datalen, t);
}

void requestReportSmsMemoryStatus(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    char *value_str;
    int used, total, result;

    err = at_send_command_singleline("AT+CPMS?",
            "+CPMS:",
            &p_response,
            AT_TIMEOUT_NORMAL);
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextstr(&line, &value_str);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &used);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &total);
    if (err < 0) goto error;

    if ( (total - used) > 0 )
        result = 1;
    else
        result = 0;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &result, sizeof(int *));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestGetSmscAddress(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    char *smscAddress = NULL;

    err = at_send_command_singleline("AT+CSCA?",
            "+CSCA:",
            &p_response,
            AT_TIMEOUT_CSCA);
    if (err < 0 || p_response->success == 0)
      goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextstr(&line, &smscAddress);
    if (err < 0) goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, smscAddress, sizeof(char *));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestGsmSmsBroadcastActivation(void *data,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    int smsBroadcastActivation;

    smsBroadcastActivation = ((int *)data)[0];
    switch (smsBroadcastActivation) {
        case 0:
            asprintf(&cmd, "AT+CNMI=,,2,,");
        break;
        case 1:
        default:
            asprintf(&cmd, "AT+CNMI=,,0,,");
        break;
    }

    err = at_send_command(cmd, &p_response, AT_TIMEOUT_NORMAL);
    free(cmd);
    if (err < 0 || p_response->success == 0)
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
}

void requestSetSmscAddress(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    char *smscAddress;

    smscAddress = (char *)data;

    asprintf(&cmd, "AT+CSCA=\"%s\"", smscAddress);

    err = at_send_command(cmd, &p_response, AT_TIMEOUT_CSCA);
    free(cmd);
    if (err < 0 || p_response->success == 0)
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
}
