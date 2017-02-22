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

#define LOG_TAG "RIL-CALL"
#include <utils/Log.h>

#ifdef RIL_SHLIB
extern const struct RIL_Env *s_rilenv;
#endif

extern const struct timeval TIMEVAL_CALLSTATEPOLL;

void requestDTMF(void *data, size_t datalen __unused, RIL_Token t)
{
    char *cmd;
    char c;

    c = ((char *)data)[0];

    asprintf(&cmd, "AT+VTS=%c", (int)c);
    at_send_command(cmd, NULL, AT_TIMEOUT_NORMAL);
    free(cmd);

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

void requestSeparateConnections(void *data, size_t datalen __unused, RIL_Token t)
{
    char *cmd;
    int party;

    party = ((int*)data)[0];

    /* Make sure that party is in a valid range.
     * (Note: The Telephony middle layer imposes a range of 1 to 7.
     * It's sufficient for us to just make sure it's single digit.)
     */
    if (party > 0 && party < 10) {
        asprintf(&cmd, "AT+CHLD=2%d", party);
        at_send_command(cmd, NULL, AT_TIMEOUT_CHLD);
        free(cmd);
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
    } else {
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    }
}

void requestLastCallFailCause(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    int lastCallFailCause;

    err = at_send_command_singleline("AT#CEER",
            "#CEER:",
            &p_response,
            AT_TIMEOUT_NORMAL);
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &lastCallFailCause);
    if (err < 0) goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &lastCallFailCause, sizeof(int *));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestConference(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    /* 3GPP 22.030 6.5.5
     * "Adds a held call to the conversation"
     */
    at_send_command("AT+CHLD=3", NULL, AT_TIMEOUT_CHLD);

    /* success or failure is ignored by the upper layer here.
     * it will call GET_CURRENT_CALLS and determine success that way */
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

void requestUdub(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    at_send_command("AT#UDUB", NULL, AT_TIMEOUT_NORMAL);

    /* success or failure is ignored by the upper layer here.
     * it will call GET_CURRENT_CALLS and determine success that way */
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

void requestAnswer(void *data __unused, size_t datalen __unused, RIL_Token t)
{
  at_send_command("ATA", NULL, AT_TIMEOUT_A);

  /* success or failure is ignored by the upper layer here.
   * it will call GET_CURRENT_CALLS and determine success that way */
  RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

void requestSwitchWaitingOrHoldingAndActive(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    /* 3GPP 22.030 6.5.5
     * "Places all active calls (if any exist) on hold and accepts
     *  the other (held or waiting) call."
     */
    at_send_command("AT+CHLD=2", NULL, AT_TIMEOUT_CHLD);

    /* success or failure is ignored by the upper layer here.
     * it will call GET_CURRENT_CALLS and determine success that way */
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

void requestHangupForegroundResumeBackground(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    /* 3GPP 22.030 6.5.5
     * "Releases all active calls (if any exist) and accepts
     *  the other (held or waiting) call."
     */
    at_send_command("AT+CHLD=1", NULL, AT_TIMEOUT_CHLD);

    /* success or failure is ignored by the upper layer here.
     * it will call GET_CURRENT_CALLS and determine success that way */
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

void requestHangupWaitingOrBackground(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    /* 3GPP 22.030 6.5.5
     * "Releases all held calls or sets User Determined User Busy
     *  (UDUB) for a waiting call."
     */
    at_send_command("AT+CHLD=0", NULL, AT_TIMEOUT_CHLD);

    /* success or failure is ignored by the upper layer here.
     * it will call GET_CURRENT_CALLS and determine success that way */
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

void requestHangup(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    char *cmd;
    int *p_line = NULL;

    p_line = (int *)data;

    /* 3GPP 22.030 6.5.5
     * "Releases a specific active call X"
     */
    asprintf(&cmd, "AT+CHLD=1%d", p_line[0]);

    err = at_send_command(cmd, NULL, AT_TIMEOUT_CHLD);
    free(cmd);

    /* success or failure is ignored by the upper layer here.
     * it will call GET_CURRENT_CALLS and determine success that way */
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

void requestDial(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    char *cmd;
    RIL_Dial *p_dial;
    const char *clir;

    p_dial = (RIL_Dial *)data;
    switch (p_dial->clir) {
        case 1: clir = "I"; break; /* invocation */
        case 2: clir = "i"; break; /* suppression */
        default:
        case 0: clir = ""; break; /* subscription default */
    }

    asprintf(&cmd, "ATD%s%s;", p_dial->address, clir);

    err = at_send_command(cmd, NULL, AT_TIMEOUT_D);
    free(cmd);

    /* success or failure is ignored by the upper layer here.
       it will call GET_CURRENT_CALLS and determine success that way */
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

static void sendCallStateChanged(void *param __unused)
{
    RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED,
        NULL,
        0);
}

static int clccStateToRILState(int state, RIL_CallState *p_state)
{
    switch(state) {
        case 0: *p_state = RIL_CALL_ACTIVE;   return 0;
        case 1: *p_state = RIL_CALL_HOLDING;  return 0;
        case 2: *p_state = RIL_CALL_DIALING;  return 0;
        case 3: *p_state = RIL_CALL_ALERTING; return 0;
        case 4: *p_state = RIL_CALL_INCOMING; return 0;
        case 5: *p_state = RIL_CALL_WAITING;  return 0;
        default: return -1;
    }
}

/**
 * Note: directly modified line and has *p_call point directly into
 * modified line
 */
static int callFromCLCCLine(char *line, RIL_Call *p_call)
{
    int err;
    int state;
    int mode;
    /* +CLCC: 1,0,2,0,0,\"+18005551212\",145
     * index,isMT,state,mode,isMpty(,number,TOA)?
     */
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(p_call->index));
    if (err < 0) goto error;

    err = at_tok_nextbool(&line, &(p_call->isMT));
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &state);
    if (err < 0) goto error;

    err = clccStateToRILState(state, &(p_call->state));
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &mode);
    if (err < 0) goto error;
    p_call->isVoice = (mode == 0);

    err = at_tok_nextbool(&line, &(p_call->isMpty));
    if (err < 0) goto error;

    if (at_tok_hasmore(&line)) {
        err = at_tok_nextstr(&line, &(p_call->number));

        /* tolerate null here */
        if (err < 0) return 0;

        /* Some lame implementations return strings
         * like "NOT AVAILABLE" in the CLCC line
         */
        if (p_call->number != NULL
                && 0 == strspn(p_call->number, "+0123456789"))
            p_call->number = NULL;

        err = at_tok_nextint(&line, &p_call->toa);
        if (err < 0) goto error;
    }

    p_call->uusInfo = NULL;

    return 0;

error:
    return -1;
}

void requestGetCurrentCalls(void *data __unused, size_t datalen __unused, RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    ATLine *p_cur = NULL;
    RIL_Call *p_calls;
    RIL_Call **pp_calls;
    int countCalls;
    int countValidCalls;
    int needRepoll = 0;
    int i;

    err = at_send_command_multiline("AT+CLCC",
            "+CLCC:",
            &p_response,
            AT_TIMEOUT_NORMAL);
    if (err != 0) goto error;

    switch (at_get_cme_error(p_response)) {
        case CME_SUCCESS:
        break;
        case CME_SIM_NOT_INSERTED:
        case CME_OPERATION_NOT_ALLOWED:
        case CME_SIM_PIN_REQUIRED:
        case CME_SIM_PUK_REQUIRED:
            /* Workaround for +CLCC endless request
             * when SIM not inserted or locked
             */
            RLOGW("requestGetCurrentCalls SIM_NOT_INSERTED or locked");
            RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

            at_response_free(p_response);
        return;
        default:
        goto error;
    }

    /* count the calls */
    for (countCalls = 0, p_cur = p_response->p_intermediates;
            p_cur != NULL;
            p_cur = p_cur->p_next)
        countCalls++;

    /* yes, there's an array of pointers and then an array of structures */
    pp_calls = (RIL_Call **)alloca(countCalls * sizeof(RIL_Call *));
    p_calls = (RIL_Call *)alloca(countCalls * sizeof(RIL_Call));
    memset (p_calls, 0, countCalls * sizeof(RIL_Call));

    /* init the pointer array */
    for(i = 0; i < countCalls ; i++)
        pp_calls[i] = &(p_calls[i]);

    for (countValidCalls = 0, p_cur = p_response->p_intermediates;
            p_cur != NULL;
            p_cur = p_cur->p_next) {
        err = callFromCLCCLine(p_cur->line, p_calls + countValidCalls);
        if (err != 0)
            continue;

        if (p_calls[countValidCalls].state != RIL_CALL_ACTIVE
                && p_calls[countValidCalls].state != RIL_CALL_HOLDING)
            needRepoll = 1;

        countValidCalls++;
    }

    RIL_onRequestComplete(t,
            RIL_E_SUCCESS,
            pp_calls,
            countValidCalls * sizeof (RIL_Call *));

    at_response_free(p_response);

#ifdef POLL_CALL_STATE
    if (countValidCalls) { /* We don't seem to get a "NO CARRIER" message from
                            * smd, so we're forced to poll until the call ends.
                            */
#else
    if (needRepoll) {
#endif
        RIL_requestTimedCallback (sendCallStateChanged, NULL, &TIMEVAL_CALLSTATEPOLL);
    }

    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestExplicitCallTransfer(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;

    err = at_send_command("AT+CHLD=4", &p_response, AT_TIMEOUT_CHLD);
    if (err < 0 || p_response->success == 0)
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
}

void requestQueryClip(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    int value;
    int response;

    err = at_send_command_singleline("AT+CLIP?",
            "+CLIP:",
            &p_response,
            AT_TIMEOUT_CLIP);
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &value);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &response);
    if (err < 0) goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int *));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestGetMute(void *data __unused, size_t datalen __unused, RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    int response;

    err = at_send_command_singleline("AT+CMUT?",
            "+CMUT:",
            &p_response,
            AT_TIMEOUT_NORMAL);
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &response);
    if (err < 0) goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int *));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestChangeBarringPassword(void *data,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    char *facilityCode;
    char *oldPassword;
    char *newPassword;

    /* TBC for facilityCode support */
    facilityCode = ((char **)data)[0];
    oldPassword = ((char **)data)[1];
    newPassword = ((char **)data)[2];

    asprintf(&cmd,
            "AT+CPWD=\"%s\",\"%s\",\"%s\"",
            facilityCode,
            oldPassword,
            newPassword);

    err = at_send_command(cmd, &p_response, AT_TIMEOUT_CPWD);
    free(cmd);
    if (err < 0 || p_response->success == 0)
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
}

void requestQueryCallWaiting(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    ATLine *p_cur = NULL;
    int *p_resultWaiting;
    int serviceClassQuery;

    serviceClassQuery = ((int *)data)[0];
    if (0 == serviceClassQuery)
        asprintf(&cmd, "AT+CCWA=1,2");
    else
        asprintf(&cmd, "AT+CCWA=1,2,%d", serviceClassQuery);

    err = at_send_command_multiline(cmd,
            "+CCWA:",
            &p_response,
            AT_TIMEOUT_CCWA);
    free(cmd);
    if (err != 0 || p_response->success == 0)
        goto error;

    p_resultWaiting = (int *)alloca(2 * sizeof(int));
    memset(p_resultWaiting, 0, 2 * sizeof(int));

    /* LINE PARSING */
    for (p_cur = p_response->p_intermediates;
            p_cur != NULL;
            p_cur = p_cur->p_next) {
        char *line = p_cur->line;
        int status;
        int serviceClass;

        err = at_tok_start(&line);
        if (err < 0) goto error;

        err = at_tok_nextint(&line, &status);
        if (err < 0) goto error;
        if (1 == status) {
            /* Correct implementation according to specs
             *
             * p_resultWaiting[0] = 1;
             * err = at_tok_nextint(&line, &serviceClass);
             * if (err < 0) goto error;
             * p_resultWaiting[1] |= serviceClass;
             */

            err = at_tok_nextint(&line, &serviceClass);
            if (err < 0) goto error;
            if (1 == serviceClass) {
                p_resultWaiting[0] = 1;
                p_resultWaiting[1] = 1;
            }
        }
    }

    RIL_onRequestComplete(t,
            RIL_E_SUCCESS,
            p_resultWaiting,
            2 * sizeof (int *));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestSetCallForward(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    RIL_CallForwardInfo *p_callForwardInfo;

    p_callForwardInfo = ((RIL_CallForwardInfo *)data);

    /* TBC for serviceClass correspondence
     * At present <time> is not managed
     */
    if (0 == p_callForwardInfo->status)
        asprintf(&cmd,
                "AT+CCFC=%d,%d",
                p_callForwardInfo->reason,
                p_callForwardInfo->status);
    else
        asprintf(&cmd,
                "AT+CCFC=%d,%d,\"%s\",%d,%d",
                p_callForwardInfo->reason,
                p_callForwardInfo->status,
                p_callForwardInfo->number == NULL ? "" : p_callForwardInfo->number,
                p_callForwardInfo->toa,
                p_callForwardInfo->serviceClass);

    err = at_send_command(cmd, &p_response, AT_TIMEOUT_CCFC);
    free(cmd);

    if (err < 0 || p_response->success == 0)
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
}

void requestQueryCallForwardStatus(void *data,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    ATLine *p_cur = NULL;
    RIL_CallForwardInfo *requestCallForward;
    RIL_CallForwardInfo *p_forwardInfo;
    RIL_CallForwardInfo **pp_forwardInfo;
    int countForwardInfo;
    int i;

    requestCallForward = (RIL_CallForwardInfo *)data;
    asprintf(&cmd, "AT+CCFC=%d,2", requestCallForward->reason);
    err = at_send_command_multiline(cmd,
            "+CCFC:",
            &p_response,
            AT_TIMEOUT_CCFC);
    free(cmd);
    if (err != 0 || p_response->success == 0)
        goto error;

    for (countForwardInfo = 0, p_cur = p_response->p_intermediates;
            p_cur != NULL;
            p_cur = p_cur->p_next)
        countForwardInfo++;

    /* yes, there's an array of pointers and then an array of structures */
    pp_forwardInfo = (RIL_CallForwardInfo **)
            alloca(countForwardInfo * sizeof(RIL_CallForwardInfo *));
    p_forwardInfo = (RIL_CallForwardInfo *)
            alloca(countForwardInfo * sizeof(RIL_CallForwardInfo));
    memset (p_forwardInfo, 0, countForwardInfo * sizeof(RIL_CallForwardInfo));

    /* init the pointer array */
    for(i = 0; i < countForwardInfo ; i++)
        pp_forwardInfo[i] = &(p_forwardInfo[i]);

    /* LINE PARSING */
    for (countForwardInfo = 0, p_cur = p_response->p_intermediates;
            p_cur != NULL;
            p_cur = p_cur->p_next) {
        char *line = p_cur->line;
        char *value_str;

        RIL_CallForwardInfo *p_tForwardInfo = p_forwardInfo + countForwardInfo;

        err = at_tok_start(&line);
        if (err < 0) goto error;

        err = at_tok_nextint(&line, &(p_tForwardInfo->status));
        if (err < 0) goto error;

        /* TBC correspondence */
        err = at_tok_nextint(&line, &(p_tForwardInfo->serviceClass));
        if (err < 0) goto error;

        err = at_tok_nextstr(&line, &value_str);
        if (err < 0) {
            p_tForwardInfo->number = NULL;
            goto nextLine;
        }
        p_tForwardInfo->number = alloca(strlen(value_str) + 1);
        strcpy(p_tForwardInfo->number, value_str);

        err = at_tok_nextint(&line, &(p_tForwardInfo->toa));
        if (err < 0) {
            if ( (p_tForwardInfo->number == NULL)
                    || (strlen(p_tForwardInfo->number) == 0) ) {
                p_tForwardInfo->toa = 0;
                goto nextLine;
            } else {
                goto error;
            }
        }

      /* At present we are not considering <time> in Telit modem answer */
nextLine:
        countForwardInfo++;
    }

    RIL_onRequestComplete(t,
            RIL_E_SUCCESS,
            pp_forwardInfo,
            countForwardInfo * sizeof (RIL_CallForwardInfo *));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestSetCallWaiting(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    int ccwaParameters[2];

    ccwaParameters[0] = ((int *)data)[0];
    ccwaParameters[1] = ((int *)data)[1];

    asprintf(&cmd, "AT+CCWA=1,%d,%d", ccwaParameters[0], ccwaParameters[1]);

    err = at_send_command(cmd, &p_response, AT_TIMEOUT_CCWA);
    free(cmd);
    if (err < 0 || p_response->success == 0)
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
}

void requestGetClir(void *data __unused, size_t datalen __unused, RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    int clirParameters[2];
    int act;

    err = at_send_command_singleline("AT+CLIR?",
            "+CLIR:",
            &p_response,
            AT_TIMEOUT_CLIR);
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &clirParameters[0]);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &clirParameters[1]);
    if (err < 0) goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS,
            &clirParameters[0],
            2 * sizeof(int *));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestSetClir(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    int clirParameter;

    clirParameter = ((int *)data)[0];

    asprintf(&cmd, "AT+CLIR=%d", clirParameter);

    err = at_send_command(cmd, &p_response, AT_TIMEOUT_CLIR);
    free(cmd);
    if (err < 0 || p_response->success == 0)
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
}

void requestSetMute(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    int *p_mute;

    p_mute = (int *)data;

    asprintf(&cmd, "AT+CMUT=%d", p_mute[0]);

    err = at_send_command(cmd, &p_response, AT_TIMEOUT_NORMAL);
    free(cmd);
    if (err < 0 || p_response->success == 0)
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
}
