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

#define LOG_TAG "RIL-NETWORK"
#include <utils/Log.h>

#ifdef RIL_SHLIB
extern const struct RIL_Env *s_rilenv;
#endif

extern uint64_t modemModel;
extern uint64_t modemQuirks;

void requestQueryNetworkSelectionMode(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    int response = 0;

    err = at_send_command_singleline("AT+COPS?",
            "+COPS:",
            &p_response,
            AT_TIMEOUT_COPS);
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &response);
    if (err < 0) goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
    RLOGE("requestQueryNetworkSelectionMode must never return error when radio is on");
}

void requestSetNetworkSelectionAutomatic(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int           err;
    ATResponse   *p_response = NULL;

    err = at_send_command("AT+COPS=0", &p_response, AT_TIMEOUT_COPS);
    if (err < 0 || p_response->success == 0)
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
}

void requestOperator(void *data __unused, size_t datalen __unused, RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    ATLine *p_cur = NULL;
    char *line = NULL;
    char *p_ret[3];
    int value;
    int i;

    memset(p_ret, 0, sizeof(p_ret));

    err = at_send_command_multiline(
        "AT+COPS=3,0;+COPS?;+COPS=3,0;+COPS?;+COPS=3,2;+COPS?",
        "+COPS:", &p_response, AT_TIMEOUT_COPS);
    if (err != 0) goto error;

    /* we expect 3 lines here:
     * +COPS: 0,0,"T - Mobile"
     * +COPS: 0,1,"TMO"
     * +COPS: 0,2,"310170"
     */
    for (i = 0, p_cur = p_response->p_intermediates;
            p_cur != NULL;
            p_cur = p_cur->p_next, i++) {
        line = p_cur->line;

        if (1 == i) {
            p_ret[i] = "";
            continue;
        }

        err = at_tok_start(&line);
        if (err < 0) goto error;

        err = at_tok_nextint(&line, &value);
        if (err < 0) goto error;

        /* If we're unregistered, we may just get
         * a "+COPS: 0" response
         */
        if (!at_tok_hasmore(&line)) {
            p_ret[i] = NULL;
            continue;
        }

        err = at_tok_nextint(&line, &value);
        if (err < 0) goto error;

        /* a "+COPS: 0, n" response is also possible */
        if (!at_tok_hasmore(&line)) {
            p_ret[i] = NULL;
            continue;
        }

        err = at_tok_nextstr(&line, &(p_ret[i]));
        if (err < 0) goto error;
    }

    /* expect 3 lines exactly */
    if (3 != i)
        goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, p_ret, sizeof(p_ret));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

int setDataNetworkType(int state)
{
    switch (state) {
        case 0:
        return RADIO_TECH_GPRS;
        case 1:
        return RADIO_TECH_EDGE;
        case 2:
        return RADIO_TECH_UMTS;
        case 3:
        return RADIO_TECH_HSDPA;
        case 4:
            if ((modemModel & MDM_LE910) || (modemModel & MDM_LE920))
                return RADIO_TECH_LTE;
            else
                return RADIO_TECH_UNKNOWN;
        case 5:
        default:
        return RADIO_TECH_UNKNOWN;
    }
}

static int parseDataRegistrationState(char *str, int *p_items, int **pp_response)
{
    int err;
    char *line = str, *p;
    int *p_resp = NULL;
    int value;
    int commas;
    int i;

    RLOGD("parseDataRegistrationState. Parsing: %s",str);
    err = at_tok_start(&line);
    if (err < 0) goto error;

    /* Ok you have to be careful here
     * The solicited version of the CGREG response is
     * +CGREG: n, stat, [lac, cid, [AcT, rac]]
     * and the unsolicited version is
     * +CGREG: stat, [lac, cid, [AcT, rac]]
     * The <n> parameter is basically "is unsolicited cgreg on?"
     * which it should always be
     *
     * Now we should normally get the solicited version here,
     * but the unsolicited version could have snuck in
     * so we have to handle both
     *
     * Also since the LAC and CID are only reported when registered,
     * we can have 1, 2, 3, 4, 5 or 6 arguments here
     *
     */

    /* count number of commas */
    commas = 0;
    for (p = line ; *p != '\0' ;p++)
        if (*p == ',') commas++;

    p_resp = (int *)calloc(REG_DATA_STATE_LEN, sizeof(int));
    if (!p_resp) goto error;
    for (i = 0; i < REG_DATA_STATE_LEN; i++)
        p_resp[i] = -1;

    switch (commas) {
        case 0: /* +CGREG: <stat> */
            err = at_tok_nextint(&line, &p_resp[0]);
            if (err < 0) goto error_parsing;
        break;

        case 1: /* +CGREG: <n>, <stat> */
            err = at_tok_nextint(&line, &value);
            if (err < 0) goto error_parsing;
            err = at_tok_nextint(&line, &p_resp[0]);
            if (err < 0) goto error_parsing;
        break;

        case 2: /* +CGREG: <stat>, <lac>, <cid> */
        case 4: /* +CGREG: <stat>, <lac>, <cid>, <networkType>, <rac> */
            err = at_tok_nextint(&line, &p_resp[0]);
            if (err < 0) goto error_parsing;
            err = at_tok_nexthexint(&line, &p_resp[1]);
            if (err < 0) goto error_parsing;
            err = at_tok_nexthexint(&line, &p_resp[2]);
            if (err < 0) goto error_parsing;
        break;

        case 3: /* +CGREG: <n>, <stat>, <lac>, <cid> */
        case 5: /* +CGREG: <n>, <stat>, <lac>, <cid>, <AcT>, <rac> */
            err = at_tok_nextint(&line, &value);
            if (err < 0) goto error_parsing;
            err = at_tok_nextint(&line, &p_resp[0]);
            if (err < 0) goto error_parsing;
            err = at_tok_nexthexint(&line, &p_resp[1]);
            if (err < 0) goto error_parsing;
            err = at_tok_nexthexint(&line, &p_resp[2]);
            if (err < 0) goto error_parsing;
        break;
        default:
        goto error_parsing;
    }

    if (pp_response)
        *pp_response = p_resp;
    if (p_items)
        *p_items = REG_DATA_STATE_LEN;

    return 0;

error_parsing:
    free(p_resp);

error:
    return -1;
}

static int sendNetworkFailureCommand()
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    int value;
    int failureReason;

    err = at_send_command_singleline("AT#CEERNET",
            "#CEERNET:",
            &p_response,
            AT_TIMEOUT_NORMAL);
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &value);
    if (err < 0) goto error;
    failureReason = value;

    at_response_free(p_response);
    return failureReason;

error:
    at_response_free(p_response);
    return -1;
}

static int sendVoiceRadioTechCommand()
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    int value;
    int radioTech;
    int additionalRadioInfo;

    err = at_send_command_singleline("AT#PSNT?",
            "#PSNT:",
            &p_response,
            AT_TIMEOUT_NORMAL);
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &value);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &value);
    if (err < 0) goto error;
    radioTech = setDataNetworkType(value);

    err = at_tok_nextint(&line, &additionalRadioInfo);
    if (err < 0) goto basic_radio_info;
    if (1 == additionalRadioInfo)
        radioTech = RADIO_TECH_HSUPA;

    err = at_tok_nextint(&line, &value);
    if (err < 0) goto basic_radio_info;

    err = at_tok_nextint(&line, &additionalRadioInfo);
    if (err < 0) goto basic_radio_info;
    if (1 == additionalRadioInfo)
        radioTech = RADIO_TECH_HSPA;

basic_radio_info:
    at_response_free(p_response);
    return radioTech;

error:
    at_response_free(p_response);
    return -1;
}

void requestDataRegistrationState(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    int *p_registration = NULL;
    char **p_responseStr = NULL;
    int technology = -1;
    int count = 3;
    int j;

    err = at_send_command_singleline("AT+CGREG?",
            "+CGREG:",
            &p_response,
            AT_TIMEOUT_NORMAL);
    if (err != 0) goto error;

    line = p_response->p_intermediates->line;
    if (((modemModel & MDM_LE910) || (modemModel & MDM_LE920))
            && (modemQuirks & QUIRK_CGREG_LTE)) {
        ATResponse *p_response_cereg = NULL;
        char *line_cereg = NULL;
        int value = 0;
        char *cgreg_line = NULL;
        char *cgreg_line_modified = NULL;

        err = at_send_command_singleline("AT+CEREG?",
            "+CEREG:",
            &p_response_cereg,
            AT_TIMEOUT_NORMAL);
        if (err < 0 || p_response_cereg->success == 0)
            goto cereg_fail;

        line_cereg = p_response_cereg->p_intermediates->line;
        err = at_tok_start(&line_cereg);
        if (err < 0) goto cereg_fail;

        err = at_tok_nextint(&line_cereg, &value);
        if (err < 0) goto cereg_fail;

        err = at_tok_nextint(&line_cereg, &value);
        if (err < 0) goto cereg_fail;

        if (1 == value) {
            cgreg_line = strstr(line, "\r");
            if (cgreg_line != NULL)
                *cgreg_line = '\0';

            asprintf(&cgreg_line_modified, "%s,00\r\n", line);
            RLOGD("QUIRK_CGREG_LTE: %s", cgreg_line_modified);
            if (parseDataRegistrationState(cgreg_line_modified,
                    &count,
                    &p_registration)) {
                free(cgreg_line_modified);
                at_response_free(p_response_cereg);
                goto error;
            }
            free(cgreg_line_modified);
        }

cereg_fail:
        at_response_free(p_response_cereg);
        if (value != 1) {
            if (parseDataRegistrationState(line, &count, &p_registration))
                goto error;
        }
    } else {
        if (parseDataRegistrationState(line, &count, &p_registration))
            goto error;
    }

    p_responseStr = malloc(REG_DATA_STATE_LEN * sizeof(char *));
    if (!p_responseStr) {
        free(p_registration);
        goto error;
    }
    memset(p_responseStr, 0, REG_DATA_STATE_LEN * sizeof(char *));

    asprintf(&p_responseStr[0], "%x", p_registration[0]);
    if (p_registration[1] != -1)
        asprintf(&p_responseStr[1], "%x", p_registration[1]);
    else
        p_responseStr[1] = NULL;

    if (p_registration[2] != -1)
        asprintf(&p_responseStr[2], "%x", p_registration[2]);
    else
        p_responseStr[2] = NULL;

    if (p_registration[0] == 3)
        asprintf(&p_responseStr[4], "%d", sendNetworkFailureCommand());
    else
        asprintf(&p_responseStr[4], "%x", 0);
    free(p_registration);

    technology = sendVoiceRadioTechCommand();
    if (-1 != technology)
        asprintf(&p_responseStr[3], "%d", technology);
    else
        asprintf(&p_responseStr[3], "%d", RADIO_TECH_UNKNOWN);

    asprintf(&p_responseStr[5], "%d", 1);

    RIL_onRequestComplete(t,
            RIL_E_SUCCESS,
            p_responseStr,
            REG_DATA_STATE_LEN * sizeof(p_responseStr));

    for (j = 0; j < REG_DATA_STATE_LEN; j++ )
        free(p_responseStr[j]);
    free(p_responseStr);

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

static int parseVoiceRegistrationState(char *str,
        int *p_items,
        int **pp_response)
{
    int err;
    char *line = str, *p;
    int *p_resp = NULL;
    int value;
    int commas;
    int i;

    RLOGD("parseVoiceRegistrationState. Parsing: %s",str);
    err = at_tok_start(&line);
    if (err < 0) goto error;

    /* Ok you have to be careful here
     * The solicited version of the CGREG response is
     * +CREG: n, stat, [lac, cid, [AcT]]
     * and the unsolicited version is
     * +CREG: stat, [lac, cid, [AcT]]
     * The <n> parameter is basically "is unsolicited creg on?"
     * which it should always be
     *
     * Now we should normally get the solicited version here,
     * but the unsolicited version could have snuck in
     * so we have to handle both
     *
     * Also since the LAC and CID are only reported when registered,
     * we can have 1, 2, 3, 4 or 5 arguments here
     *
     */

    /* count number of commas */
    commas = 0;
    for (p = line ; *p != '\0' ;p++)
        if (*p == ',') commas++;

    p_resp = (int *)calloc(REG_VOICE_STATE_LEN, sizeof(int));
    if (!p_resp) goto error;
    for (i = 0; i < REG_VOICE_STATE_LEN; i++)
        p_resp[i] = -1;

    switch (commas) {
        case 0: /* +CREG: <stat> */
            err = at_tok_nextint(&line, &p_resp[0]);
            if (err < 0) goto error_parsing;
        break;

        case 1: /* +CREG: <n>, <stat> */
            err = at_tok_nextint(&line, &value);
            if (err < 0) goto error_parsing;
            err = at_tok_nextint(&line, &p_resp[0]);
            if (err < 0) goto error_parsing;
        break;

        case 2: /* +CREG: <stat>, <lac>, <cid> */
            err = at_tok_nextint(&line, &p_resp[0]);
            if (err < 0) goto error_parsing;
            err = at_tok_nexthexint(&line, &p_resp[1]);
            if (err < 0) goto error_parsing;
            err = at_tok_nexthexint(&line, &p_resp[2]);
            if (err < 0) goto error_parsing;
        break;

        case 3: /* +CREG: <n>, <stat>, <lac>, <cid> */
                /* +CREG: <stat>, <lac>, <cid>, <AcT> */
                /* Need a way to understand the scenario */
            err = at_tok_nextint(&line, &value);
            if (err < 0) goto error_parsing;
            if (value != 2) goto error_parsing;
            err = at_tok_nextint(&line, &p_resp[0]);
            if (err < 0) goto error_parsing;
            if (p_resp[0] > 5) goto error_parsing;
            err = at_tok_nexthexint(&line, &p_resp[1]);
            if (err < 0) goto error_parsing;
            err = at_tok_nexthexint(&line, &p_resp[2]);
            if (err < 0) goto error_parsing;
        break;

        case 4: /* +CREG: <n>, <stat>, <lac>, <cid>, <AcT> */
            err = at_tok_nextint(&line, &value);
            if (err < 0) goto error_parsing;
            err = at_tok_nextint(&line, &p_resp[0]);
            if (err < 0) goto error_parsing;
            err = at_tok_nexthexint(&line, &p_resp[1]);
            if (err < 0) goto error_parsing;
            err = at_tok_nexthexint(&line, &p_resp[2]);
            if (err < 0) goto error_parsing;
        break;

        default:
        goto error_parsing;
    }

    if (pp_response)
        *pp_response = p_resp;
    if (p_items)
        *p_items = REG_VOICE_STATE_LEN;

    return 0;

error_parsing:
    free(p_resp);

error:
    return -1;
}

void requestVoiceRegistrationState(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    int *p_registration = NULL;
    char **p_responseStr = NULL;
    int technology = -1;
    int count = 3;
    int j;

    err = at_send_command_singleline("AT+CREG?",
            "+CREG:",
            &p_response,
            AT_TIMEOUT_NORMAL);
    if (err != 0) goto error;

    line = p_response->p_intermediates->line;
    if (parseVoiceRegistrationState(line, &count, &p_registration)) goto error;

    p_responseStr = malloc(REG_VOICE_STATE_LEN * sizeof(char *));
    if (!p_responseStr) {
        free(p_registration);
        goto error;
    }
    memset(p_responseStr, 0, REG_VOICE_STATE_LEN * sizeof(char *));

    asprintf(&p_responseStr[0], "%x", p_registration[0]);
    p_registration[13] = 0;

    if (p_registration[1] != -1)
        asprintf(&p_responseStr[1], "%x", p_registration[1]);
    else
        p_responseStr[1] = NULL;

    if (p_registration[2] != -1)
        asprintf(&p_responseStr[2], "%x", p_registration[2]);
    else
        p_responseStr[2] = NULL;

    if (p_registration[0] == 3)
        asprintf(&p_responseStr[13], "%x", sendNetworkFailureCommand());

    free(p_registration);

    technology = sendVoiceRadioTechCommand();
    if (-1 != technology)
        asprintf(&p_responseStr[3], "%d", technology);
    else
        asprintf(&p_responseStr[3], "%d", RADIO_TECH_UNKNOWN);

    p_responseStr[5] = NULL;
    p_responseStr[6] = NULL;
    asprintf(&p_responseStr[7], "%d", 0);
    p_responseStr[10] = NULL;
    p_responseStr[11] = NULL;
    p_responseStr[12] = NULL;
    p_responseStr[12] = NULL;
    p_responseStr[4] = NULL;
    p_responseStr[8] = NULL;
    p_responseStr[9] = NULL;

    RIL_onRequestComplete(t,
            RIL_E_SUCCESS,
            p_responseStr,
            REG_VOICE_STATE_LEN * sizeof(p_responseStr));

    for (j = 0; j < REG_VOICE_STATE_LEN; j++ ) {
        if (p_responseStr[j] != NULL)
            free(p_responseStr[j]);
    }
    free(p_responseStr);
    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestVoiceRadioTech(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int radioTech;

    radioTech = sendVoiceRadioTechCommand();
    if (-1 != radioTech)
        RIL_onRequestComplete(t, RIL_E_SUCCESS, &radioTech, sizeof(int *));
    else
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

void requestSignalStrength(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    RIL_SignalStrength_v6 response;

    memset(&response, 0, sizeof(RIL_SignalStrength_v6));
    response.GW_SignalStrength.signalStrength = 99;
    response.GW_SignalStrength.bitErrorRate = -1;
    response.CDMA_SignalStrength.ecio = -1;
    response.CDMA_SignalStrength.dbm = -1;
    response.EVDO_SignalStrength.dbm = -1;
    response.EVDO_SignalStrength.ecio = -1;
    response.EVDO_SignalStrength.signalNoiseRatio = -1;
    response.LTE_SignalStrength.signalStrength = 99;
    response.LTE_SignalStrength.rsrp = 0x7FFFFFFF;
    response.LTE_SignalStrength.rsrq = 0x7FFFFFFF;
    response.LTE_SignalStrength.rssnr = 0x7FFFFFFF;
    response.LTE_SignalStrength.cqi = 0x7FFFFFFF;

    err = at_send_command_singleline("AT+CSQ",
            "+CSQ:",
            &p_response,
            AT_TIMEOUT_NORMAL);
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(response.GW_SignalStrength.signalStrength));
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(response.GW_SignalStrength.bitErrorRate));
    if (err < 0) goto error;

    RIL_onRequestComplete(t,
            RIL_E_SUCCESS,
            &response,
            sizeof(RIL_SignalStrength_v6));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

static int isNetworkPresent(char **p_networkList,
        int networkListSize,
        char *network)
{
    int i;

    if (0 == networkListSize)
        return 0;

    for (i = 0; i < networkListSize; i++) {
        if (strcmp(p_networkList[i], network) == 0)
            return 1;
    }

    return 0;
}

void requestQueryAvailableNetworks(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    char **p_ret;
    char **p_networkList;
    int networkListSize = 0;
    int tokens;
    int i, j;

    err = at_send_command_singleline("AT+COPS=?",
            "+COPS:",
            &p_response,
            AT_TIMEOUT_COPS);
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    tokens = at_tok_howmany_bracketstoken(&line);
    if (!(tokens > 0))
        goto error;
    tokens -= 2;

    p_networkList = (char **)alloca(tokens * sizeof(char *));

    tokens *= 4;
    p_ret = (char **)alloca(tokens * sizeof(char *));
    memset(p_ret, 0, tokens * sizeof(char *));

    for (i = 0, j = 0; i < tokens; i += 4) {
        char *value_brk = NULL;
        char *value_str = NULL;
        int status;

        if (0 != at_tok_nextbracket(&line, &value_brk))
            goto error;

        err = at_tok_nextint(&value_brk, &status);
        if (err < 0) goto error;

        err = at_tok_nextstr(&value_brk, &value_str);
        if (err < 0) goto error;

        if (isNetworkPresent(p_networkList, networkListSize, value_str))
            continue;

        p_networkList[networkListSize] = alloca(strlen(value_str) + 1);
        strcpy(p_networkList[networkListSize], value_str);
        networkListSize++;

        switch (status) {
            case 1:
                p_ret[j + 3] = alloca(strlen("available") + 1);
                strcpy(p_ret[j + 3], "available");
            break;
            case 2:
                p_ret[j + 3] = alloca(strlen("current") + 1);
                strcpy(p_ret[j + 3], "current");
            break;
            case 3:
                p_ret[j + 3] = alloca(strlen("forbidden") + 1);
                strcpy(p_ret[j + 3], "forbidden");
            break;
            case 0:
            default:
                p_ret[j + 3] = alloca(strlen("unknown") + 1);
                strcpy(p_ret[j + 3], "unknown");
        }

        p_ret[j] = alloca(strlen(value_str) + 1);
        strcpy(p_ret[j], value_str);

        err = at_tok_nextstr(&value_brk, &value_str);
        if (err < 0) goto error;
        p_ret[j + 1] = alloca(strlen(value_str) + 1);
        strcpy(p_ret[j + 1], value_str);

        err = at_tok_nextstr(&value_brk, &value_str);
        if (err < 0) goto error;
        p_ret[j + 2] = alloca(strlen(value_str) + 1);
        strcpy(p_ret[j + 2], value_str);

        j += 4;
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, p_ret, j * sizeof(char *));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestSetNetworkSelectionManual(void *data,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    ATLine *p_cur = NULL;
    int value;
    char *networkRegistered;

    asprintf(&cmd, "AT+COPS=1,2,\"%s\";+COPS?", (char *)data);

    err = at_send_command_multiline(cmd,
            "+COPS:",
            &p_response,
            AT_TIMEOUT_COPS);
    free(cmd);
    /* we expect 1 line here, e.g.:
     * +COPS: 1,2,"22288"
     */
    if (err != 0) goto error;

    p_cur = p_response->p_intermediates;
    if (p_cur != NULL) {
        char *line = p_cur->line;

        err = at_tok_start(&line);
        if (err < 0) goto error;

        err = at_tok_nextint(&line, &value);
        if (err < 0) goto error;

        /* If we're unregistered, we may just get
         * a "+COPS: 0" response
         */
        if (!at_tok_hasmore(&line))
            goto error;

        err = at_tok_nextint(&line, &value);
        if (err < 0) goto error;

        /* a "+COPS: 0, n" response is also possible */
        if (!at_tok_hasmore(&line))
            goto error;

        err = at_tok_nextstr(&line, &networkRegistered);
        if (err < 0) goto error;

        if (strcmp((char *)data, networkRegistered) != 0)
            goto error;
    } else {
        goto error;
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestGetPreferredNetworkType(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    int preferred;
    int i;

    err = at_send_command_singleline("AT+WS46?",
            "+WS46:",
            &p_response,
            AT_TIMEOUT_NORMAL);
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &i);
    if (err < 0) goto error;

    if ((modemModel & MDM_LE910) || (modemModel & MDM_LE920)) {
        switch (i) {
            case 29:
                preferred = PREF_NET_TYPE_GSM_WCDMA_AUTO;
            break;
            case 28:
                preferred = PREF_NET_TYPE_LTE_ONLY;
            break;
            case 31:
                preferred = PREF_NET_TYPE_LTE_WCDMA;
            break;
            case 12:
                preferred = PREF_NET_TYPE_GSM_ONLY;
            break;
            case 22:
                preferred = PREF_NET_TYPE_WCDMA;
            break;
            case 25:
                preferred = PREF_NET_TYPE_LTE_GSM_WCDMA;
            break;
        }
    } else {
        switch (i) {
            case 12:
                preferred = PREF_NET_TYPE_GSM_ONLY;
            break;
            case 22:
                preferred = PREF_NET_TYPE_WCDMA;
            break;
            case 25:
                preferred = PREF_NET_TYPE_GSM_WCDMA;
            break;
        }
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &preferred, sizeof(int));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestSetPreferredNetworkType(void *data,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    int value;
    int current;

    value = *(int *)data;

    if ((modemModel & MDM_LE910) || (modemModel & MDM_LE920)) {
        switch (value) {
            case PREF_NET_TYPE_GSM_WCDMA:
            case PREF_NET_TYPE_GSM_WCDMA_AUTO:
                current = 29;
            break;
            case PREF_NET_TYPE_LTE_ONLY:
                current = 28;
            break;
            case PREF_NET_TYPE_LTE_WCDMA:
                current = 31;
            break;
            case PREF_NET_TYPE_LTE_GSM_WCDMA:
                current = 25;
            break;
            case PREF_NET_TYPE_GSM_ONLY:
                current = 12;
            break;
            case PREF_NET_TYPE_WCDMA:
                current = 22;
            break;
            default:
                RIL_onRequestComplete(t, RIL_E_MODE_NOT_SUPPORTED, NULL, 0);
            return;
        }
    } else {
        switch (value) {
            case PREF_NET_TYPE_GSM_WCDMA_AUTO:
            case PREF_NET_TYPE_GSM_WCDMA:
                current = 25;
            break;
            case PREF_NET_TYPE_GSM_ONLY:
                current = 12;
            break;
            case PREF_NET_TYPE_WCDMA:
                current = 22;
            break;
            default:
                RIL_onRequestComplete(t, RIL_E_MODE_NOT_SUPPORTED, NULL, 0);
            return;
        }
    }

    asprintf(&cmd, "AT+WS46=%d", current);

    err = at_send_command(cmd, &p_response, AT_TIMEOUT_NORMAL);
    free(cmd);
    if (err < 0 || p_response->success == 0)
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
}
