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

#define LOG_TAG "RIL-MECONTROL"
#include <utils/Log.h>

#ifdef RIL_SHLIB
extern const struct RIL_Env *s_rilenv;
#endif

extern RIL_RadioState sState;
extern uint64_t modemModel;

extern char s_pin1[RIL_GENERIC_STRING_SIZE];

void requestEnterSimPin(void* data,
        size_t datalen __unused,
        RIL_Token t,
        int request)
{
    int           err;
    char*         cmd;
    ATResponse   *p_response = NULL;
    const char**  p_strings;

    p_strings = (const char**)data;

    switch (request) {
        case RIL_REQUEST_ENTER_SIM_PIN:
            asprintf(&cmd, "AT+CPIN=\"%s\"", p_strings[0]);
        break;
        case RIL_REQUEST_CHANGE_SIM_PIN:
            asprintf(&cmd, "AT+CPWD=\"SC\",\"%s\",\"%s\"", p_strings[0], p_strings[1]);
        break;
        case RIL_REQUEST_CHANGE_SIM_PIN2:
            asprintf(&cmd, "AT+CPWD=\"P2\",\"%s\",\"%s\"", p_strings[0], p_strings[1]);
        break;
        case RIL_REQUEST_ENTER_SIM_PUK:
        case RIL_REQUEST_ENTER_SIM_PUK2:
            asprintf(&cmd, "AT+CPIN=\"%s\",\"%s\"", p_strings[0], p_strings[1]);
        break;
        default:
            err = -1;
        goto error;
    }

    err = at_send_command(cmd, &p_response, AT_TIMEOUT_CPWD);
    free(cmd);

error:
    if (err < 0 || p_response->success == 0) {
        char *line;
        int retries = 0;
        at_response_free(p_response);

        err = at_send_command_singleline("AT#PCT",
                "#PCT:",
                &p_response,
                AT_TIMEOUT_NORMAL);
        if (err < 0 || p_response->success == 0)
            goto error_retries;

        line = p_response->p_intermediates->line;
        err = at_tok_start(&line);
        if (err < 0) goto error_retries;

        err = at_tok_nextint(&line, &retries);
        if (err < 0) goto error_retries;

        RIL_onRequestComplete(t,
            RIL_E_PASSWORD_INCORRECT,
            &retries,
            sizeof(int *));

        at_response_free(p_response);
        return;

error_retries:
        RIL_onRequestComplete(t, RIL_E_PASSWORD_INCORRECT, NULL, 0);
    } else {
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

        if (RIL_REQUEST_ENTER_SIM_PIN == request) {
            memset(s_pin1, '\0', RIL_GENERIC_STRING_SIZE);
            strncpy(s_pin1, p_strings[0], RIL_GENERIC_STRING_SIZE - 1);
            RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED, NULL, 0);
        }

        if (RIL_REQUEST_CHANGE_SIM_PIN == request) {
            if (strlen(s_pin1) > 0) {
                memset(s_pin1, '\0', RIL_GENERIC_STRING_SIZE);
                strncpy(s_pin1, p_strings[1], RIL_GENERIC_STRING_SIZE - 1);
            }
        }
    }
    at_response_free(p_response);
}

void requestGetIMSI(void *data __unused, size_t datalen __unused, RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    char *value_str;

    err = at_send_command_singleline("AT#CIMI",
                "#CIMI:",
                &p_response,
                AT_TIMEOUT_NORMAL);
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextstr(&line, &value_str);
    if (err < 0) goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, value_str, sizeof(char *));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestGetIMEI(void *data __unused, size_t datalen __unused, RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    char *value_str;

    err = at_send_command_singleline("AT#CGSN",
                "#CGSN:",
                &p_response,
                AT_TIMEOUT_NORMAL);
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextstr(&line, &value_str);
    if (err < 0) goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, value_str, sizeof(char *));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestRadioPower(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    int onOff;

    assert (datalen >= sizeof(int *));
    onOff = ((int *)data)[0];

    if (onOff == 0 && sState != RADIO_STATE_OFF) {
        RIL_RadioState tempState = currentState();
        setRadioState(RADIO_STATE_OFF);

        err = at_send_command("AT+CFUN=4", &p_response, AT_TIMEOUT_CFUN);
        if (err < 0 || p_response->success == 0) {
            setRadioState(tempState);
            goto error;
        }
    } else if (onOff > 0 && sState == RADIO_STATE_OFF) {
        err = at_send_command("AT+CFUN=1", &p_response, AT_TIMEOUT_CFUN);
        if (err < 0 || p_response->success == 0) {
            /* Some stacks return an error when there is no SIM,
             * but they really turn the RF portion on
             * So, if we get an error, let's check to see if it
             * turned on anyway
             */

            if (1 != isRadioOn())
                goto error;
        }
        setRadioState(RADIO_STATE_ON);
    }

    /* Consider slow SIM reading at startup */
    sleep(4);

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestGetImeiSv(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;

    if (modemModel & MDM_LE910 ||
            modemModel & MDM_LE920) {
        char *line;
        char *value_str;

        err = at_send_command_singleline("AT+IMEISV",
            "+IMEISV:",
            &p_response,
            AT_TIMEOUT_NORMAL);
        if (err < 0 || p_response->success == 0)
            goto error;

        line = p_response->p_intermediates->line;
        err = at_tok_start(&line);
        if (err < 0) goto error;

        err = at_tok_nextstr(&line, &value_str);
        if (err < 0) goto error;

        RIL_onRequestComplete(t, RIL_E_SUCCESS, value_str, sizeof(char *));
    } else {
        err = at_send_command_numeric("AT+IMEISV", &p_response, AT_TIMEOUT_NORMAL);
        if (err < 0 || p_response->success == 0) {
            RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        } else {
            RIL_onRequestComplete(t,
                    RIL_E_SUCCESS,
                    p_response->p_intermediates->line,
                    sizeof(char *));
        }
    }

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestShutdown(RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    int onOff;

    if (RADIO_STATE_OFF != sState) {
        err = at_send_command("AT#SHDN", &p_response, AT_TIMEOUT_NORMAL);
        setRadioState(RADIO_STATE_UNAVAILABLE);
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
}

void requestDeviceIdentity(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *p_ret[4];
    int count = 4;

    err = at_send_command_numeric("AT+CGSN", &p_response, AT_TIMEOUT_NORMAL);
    if (err < 0 || p_response->success == 0)
        goto error;

    p_ret[0] = p_response->p_intermediates->line;
    p_ret[1] = p_ret[2] = p_ret[3] = "----";

    RIL_onRequestComplete(t, RIL_E_SUCCESS, p_ret, count * sizeof(char*));

    at_response_free(p_response);
    return;

error:
    RLOGE("requestDeviceIdentity must never return an error when radio is on");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestSetBandMode(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    int bandMode;

    bandMode = ((int *)data)[0];
    switch (bandMode) {
        case 0:
            asprintf(&cmd, "AT#AUTOBND=2");
        break;
        default:
            RIL_onRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
        return;
    }

    err = at_send_command(cmd, &p_response, AT_TIMEOUT_NORMAL);
    free(cmd);
    if (err < 0 || p_response->success == 0)
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
}

void requestSetFacilityLock(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    char *facilityCode;
    char *lock;
    char *password;
    char *serviceClassBitVector;
    int retries = -1;

    /* TBC for facilityCode support */
    facilityCode = (char *)((const char **)data)[0];
    lock = (char *)((const char **)data)[1];
    password = (char *)((const char **)data)[2];
    serviceClassBitVector = (char *)((const char **)data)[3];

    if ( (strcmp(facilityCode, "SC") == 0)
            || (strcmp(facilityCode, "FD") == 0) )
        asprintf(&cmd,
                "AT+CLCK=\"%s\",%s,\"%s\"",
                facilityCode,
                lock,
                password);
    else
        asprintf(&cmd,
                "AT+CLCK=\"%s\",%s,\"%s\",%s",
                facilityCode,
                lock,
                password,
                serviceClassBitVector);

    err = at_send_command(cmd, &p_response, AT_TIMEOUT_CLCK);
    free(cmd);
    if (err < 0 || p_response->success == 0)
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, &retries, sizeof(int *));

    at_response_free(p_response);
}

void requestBasebandVersion(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line;
    char *value_str;

    err = at_send_command_singleline("AT#CGMR",
            "#CGMR:",
            &p_response,
            AT_TIMEOUT_NORMAL);
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextstr(&line, &value_str);
    if (err < 0) goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, value_str, sizeof(char *));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestQueryFacility(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    ATLine *p_cur = NULL;
    int response = 0;

    const char *facilityCode = ((const char **)data)[0];
    const char *password = ((const char **)data)[1];
    const char *serviceClass = ((const char **)data)[2];

    if (strlen(password) > 0)
        asprintf(&cmd,
                "AT+CLCK=\"%s\",2,\"%s\",%s",
                facilityCode,
                password,
                serviceClass);
    else
        asprintf(&cmd,
                "AT+CLCK=\"%s\",2",
                facilityCode);

    err = at_send_command_multiline(cmd,
            "+CLCK:",
            &p_response,
            AT_TIMEOUT_CLCK);
    free(cmd);
    if (err < 0 || p_response->success == 0)
        goto error;

    for (p_cur = p_response->p_intermediates;
            p_cur != NULL;
            p_cur = p_cur->p_next) {
        int status, service;

        err = at_tok_start(&(p_cur->line));
        if (err < 0)
            goto error;

        err = at_tok_nextint(&(p_cur->line), &status);
        if (err < 0)
            goto error;

        if (1 == status) {
            err = at_tok_nextint(&(p_cur->line), &service);
            if (err < 0) {
                /* TBC +CLCK behavior */
                response = atoi(serviceClass);
                break;
            }
            response += service;
        }
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int *));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}
