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

#define LOG_TAG "RIL-SERIAL"
#include <utils/Log.h>

#ifdef RIL_SHLIB
extern const struct RIL_Env *s_rilenv;
#endif

extern const char * s_device_path;
extern int          s_device_socket;
extern int          s_closed;
extern int          s_port;

extern const struct timeval TIMEVAL_0;

extern RIL_RadioState sState;
extern ModemInfo *sMdmInfo;

extern UICC_Type UiccType;

static int sFD;     /* file desc of AT channel */
static char sATBuffer[MAX_AT_RESPONSE+1];
static char *sATBufferCur = NULL;

static int firstInitialization = 0;

static int32_t net2pmask[] = {
    MDM_GSM | (MDM_WCDMA << 8),                          // 0  - GSM / WCDMA Pref
    MDM_GSM,                                             // 1  - GSM only
    MDM_WCDMA,                                           // 2  - WCDMA only
    MDM_GSM | MDM_WCDMA,                                 // 3  - GSM / WCDMA Auto
    MDM_CDMA | MDM_EVDO,                                 // 4  - CDMA / EvDo Auto
    MDM_CDMA,                                            // 5  - CDMA only
    MDM_EVDO,                                            // 6  - EvDo only
    MDM_GSM | MDM_WCDMA | MDM_CDMA | MDM_EVDO,           // 7  - GSM/WCDMA, CDMA, EvDo
    MDM_LTE | MDM_CDMA | MDM_EVDO,                       // 8  - LTE, CDMA and EvDo
    MDM_LTE | MDM_GSM | MDM_WCDMA,                       // 9  - LTE, GSM/WCDMA
    MDM_LTE | MDM_CDMA | MDM_EVDO | MDM_GSM | MDM_WCDMA, // 10 - LTE, CDMA, EvDo, GSM/WCDMA
    MDM_LTE,                                             // 11 - LTE only
};

uint64_t modemModel = MDM_HE910;
uint64_t modemQuirks = QUIRK_NO;

void requestGetHardwareConfig(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    RIL_HardwareConfig *hwCfg;

    hwCfg = alloca(sizeof(RIL_HardwareConfig) * 2);
    if (NULL == hwCfg) goto error;

    if ((modemModel & MDM_LE910) ||
            (modemModel & MDM_LE920)) {
        hwCfg[0].type = RIL_HARDWARE_CONFIG_MODEM;
        strcpy(hwCfg[0].uuid, "38792120-0b12-21a5-b918-7730200b1a53");
        hwCfg[0].state = RIL_HARDWARE_CONFIG_STATE_ENABLED;
        hwCfg[0].cfg.modem.rilModel = 1;
        hwCfg[0].cfg.modem.rat = (RADIO_TECH_GPRS |
                RADIO_TECH_EDGE |
                RADIO_TECH_UMTS |
                RADIO_TECH_HSDPA |
                RADIO_TECH_LTE);
        hwCfg[0].cfg.modem.maxVoice = 3;
        hwCfg[0].cfg.modem.maxData = 1;
        hwCfg[0].cfg.modem.maxStandby = 3;

        hwCfg[1].type = RIL_HARDWARE_CONFIG_SIM;
        strcpy(hwCfg[1].uuid, "38792120-0b12-21a5-b918-7730200b1a53");
        hwCfg[1].state = RIL_HARDWARE_CONFIG_STATE_ENABLED;
        strcpy(hwCfg[1].cfg.sim.modemUuid, "38792120-0b12-21a5-b918-7730200b1a53");
    } else {
        hwCfg[0].type = RIL_HARDWARE_CONFIG_MODEM;
        strcpy(hwCfg[0].uuid, "58497520-0a87-11e5-b939-0800200c9a66");
        hwCfg[0].state = RIL_HARDWARE_CONFIG_STATE_ENABLED;
        hwCfg[0].cfg.modem.rilModel = 1;
        hwCfg[0].cfg.modem.rat = (RADIO_TECH_GPRS |
                RADIO_TECH_EDGE |
                RADIO_TECH_UMTS |
                RADIO_TECH_HSDPA);
        hwCfg[0].cfg.modem.maxVoice = 3;
        hwCfg[0].cfg.modem.maxData = 1;
        hwCfg[0].cfg.modem.maxStandby = 3;

        hwCfg[1].type = RIL_HARDWARE_CONFIG_SIM;
        strcpy(hwCfg[1].uuid, "58497520-0a87-11e5-b939-0800200c9a66");
        hwCfg[1].state = RIL_HARDWARE_CONFIG_STATE_ENABLED;
        strcpy(hwCfg[1].cfg.sim.modemUuid, "58497520-0a87-11e5-b939-0800200c9a66");
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, hwCfg, sizeof(hwCfg) * 2);

    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

/* TODO: to be implemented */
#if 0
static void requestGetCellInfoList(void *data __unused, size_t datalen __unused, RIL_Token t)
{
    uint64_t curTime = ril_nano_time();
    RIL_CellInfo ci[1] =
    {
        { // ci[0]
            1, // cellInfoType
            1, // registered
            curTime - 1000, // Fake some time in the past
            { // union CellInfo
                {  // RIL_CellInfoGsm gsm
                    {  // gsm.cellIdneityGsm
                        s_mcc, // mcc
                        s_mnc, // mnc
                        s_lac, // lac
                        s_cid, // cid
                        0  // psc
                    },
                    {  // gsm.signalStrengthGsm
                        10, // signalStrength
                        0  // bitErrorRate
                    }
                }
            }
        }
    };

    RIL_onRequestComplete(t, RIL_E_SUCCESS, ci, sizeof(ci));
}
#endif

void requestGetRadioCapability(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    RIL_RadioCapability rc;

    rc.version = RIL_RADIO_CAPABILITY_VERSION;
    if ((modemModel & MDM_LE910) ||
            (modemModel & MDM_LE920)) {
        rc.rat = (RAF_GPRS | RAF_EDGE | RAF_UMTS | RAF_HSPA | RAF_LTE);
    } else {
        rc.rat = (RAF_GPRS | RAF_EDGE | RAF_UMTS | RAF_HSPA);
    }
    strcpy(rc.logicalModemUuid, "com.telit.lm0");
    rc.session = 1;
    rc.phase = RC_PHASE_FINISH;
    rc.status = RC_STATUS_SUCCESS;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &rc, sizeof(RIL_RadioCapability));
}

static void setPropertyNetwork()
{
    RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED,
            NULL,
            0);
}

static void setPropertyEmergencyList()
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    char *value_str;
    char *auxTok;
    char *numberTok;
    char *emergencyNumbersList;
    int firstIndex, lastIndex;
    int i;

    err = at_send_command("AT+CPBS=\"EC\"", &p_response, AT_TIMEOUT_CPBS);
    if (err < 0 || p_response->success == 0)
        goto error;

    at_response_free(p_response);

    err = at_send_command_singleline("AT+CPBR=?",
            "+CPBR:",
            &p_response,
            AT_TIMEOUT_CPBR);
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    value_str = (char*)at_tok_getnexttok(&line);
    if (value_str == NULL) goto error;

    auxTok = value_str;
    numberTok= nextStrOccurrence(&auxTok, "(");
    if (numberTok == NULL) goto error;

    err = at_tok_nextint(&numberTok, &firstIndex);
    if (err < 0) goto error;

    auxTok = value_str;
    numberTok= nextStrOccurrence(&auxTok, "-");
    if (numberTok == NULL) goto error;

    err = at_tok_nextint(&numberTok, &lastIndex);
    if (err < 0) goto error;

    at_response_free(p_response);
    p_response = NULL;

    if (lastIndex > firstIndex) {
        char *cmd;

        emergencyNumbersList =
                alloca(sizeof(char) * EMERGENCY_NUMBERS_LIST_SIZE);
        memset(emergencyNumbersList, '\0', EMERGENCY_NUMBERS_LIST_SIZE);

        strcat(emergencyNumbersList, COMMON_EMERGENCY_NUMBERS);
        for (i = firstIndex; i <= lastIndex; i++) {
            int value;
            char *value_str;
            char *numberString;

            asprintf(&cmd, "AT+CPBR=%d", i);

            err = at_send_command_singleline(cmd,
                    "+CPBR:",
                    &p_response,
                    AT_TIMEOUT_CPBR);
            free(cmd);
            if (err < 0 || p_response->success == 0) goto end_for;

            line = p_response->p_intermediates->line;
            err = at_tok_start(&line);
            if (err < 0) goto end_for;

            err = at_tok_nextint(&line, &value);
            if (err < 0) goto end_for;

            err = at_tok_nextstr(&line, &value_str);
            if (err < 0) goto end_for;

            if ( (strlen(value_str) > 0) &&
                    (strstr(COMMON_EMERGENCY_NUMBERS, value_str) == NULL) ) {
                if ( (strlen(value_str) + strlen(emergencyNumbersList)) <
                        (EMERGENCY_NUMBERS_LIST_SIZE - 1)) {
                    asprintf(&numberString, ",%s", value_str);
                    strcat(emergencyNumbersList, numberString);
                    free(numberString);
                }
            }

end_for:
            at_response_free(p_response);
        }
    } else {
        goto error;
    }

    property_set(PROPERTY_EMERGENCY_LIST, emergencyNumbersList);
    at_send_command("AT+CPBS=\"SM\"", NULL, AT_TIMEOUT_CPBS);
    return;

error:
    RLOGE("setPropertyEmergencyList failure, using common emergency list");
    at_response_free(p_response);
    property_set(PROPERTY_EMERGENCY_LIST, COMMON_EMERGENCY_NUMBERS);
    at_send_command("AT+CPBS=\"SM\"", NULL, AT_TIMEOUT_CPBS);
}

int is_multimode_modem(ModemInfo *mdm __unused)
{
    /* Telit does not have multimode modem */
    return 0;
}

/** returns 1 if on, 0 if off, and -1 on error */
int isRadioOn()
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    int ret;

    err = at_send_command_singleline("AT+CFUN?",
            "+CFUN:",
            &p_response,
            AT_TIMEOUT_CFUN);
    /* Assume radio is off on error */
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &ret);
    if (err < 0) goto error;

    at_response_free(p_response);
    return (ret == 1);

error:
    at_response_free(p_response);
    return -1;
}

/** do post- SIM ready initialization */
void onSIMReady()
{
    at_send_command("AT#PSNT=1", NULL, AT_TIMEOUT_NORMAL);
    at_send_command("AT+CSMS=1", NULL, AT_TIMEOUT_NORMAL);

    /*
     * Always send SMS messages directly to the TE
     *
     * mode = 1 // discard when link is reserved (link should never be
     *             reserved)
     * mt = 2   // most messages routed to TE
     * bm = 2   // new cell BM's routed to TE
     * ds = 1   // Status reports routed to TE
     * bfr = 1  // flush buffer
     */
    at_send_command("AT+CNMI=2,2,2,1,1", NULL, AT_TIMEOUT_NORMAL);
}

void onQssChanged(void *param)
{
    int qss, radioState;
    char *response = (char *)param;

    at_tok_start(&response);
    at_tok_nextint(&response, &qss);

    switch (qss) {
        case 0:
            UiccType = UICC_TYPE_UNKNOWN;
        break;
        case 2:
            onSIMReady();

            if (firstInitialization == 0) {
                firstInitialization = 1;
                finalizeInit();
            }
        break;
    }

    RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED, NULL, 0);

    free(param);
}

void onDataCallListChanged(void *param __unused)
{
    requestOrSendDataCallList(NULL);
}

/**
 * Called by atchannel when an unsolicited line appears
 * This is called on atchannel's reader thread. AT commands may
 * not be issued here
 */
void onUnsolicited (const char *s, const char *sms_pdu)
{
    int err;
    char *line = NULL, *p;

    /* Ignore unsolicited responses until we're initialized.
     * This is OK because the RIL library will poll for initial state
     */
    if (sState == RADIO_STATE_UNAVAILABLE)
        return;

    if (strStartsWith(s, "#NITZ:")) {
        char *response;
        char *dummy;
        char *lineStart;

        line = strdup(s);
        lineStart = line;
        at_tok_start(&line);

        dummy = nextStrOccurrence(&line, " ");
        response = line;

        if (dummy == NULL)
            RLOGE("invalid NITZ line %s\n", s);
        else
            RIL_onUnsolicitedResponse(RIL_UNSOL_NITZ_TIME_RECEIVED,
                    response,
                    strlen(response));
        free(lineStart);
    } else if (strStartsWith(s, "+CRING:")
            || strStartsWith(s, "RING")
            || strStartsWith(s, "NO CARRIER")
            || strStartsWith(s, "+CCWA")
    ) {
        RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED,
                NULL,
                0);
    } else if (strStartsWith(s, "+CREG:")
            || strStartsWith(s, "+CGREG:")
    ) {
        if (sState != RADIO_STATE_OFF)
            RIL_onUnsolicitedResponse (
                    RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED,
                    NULL,
                    0);
            RIL_requestTimedCallback(onDataCallListChanged, NULL, NULL);
    } else if (strStartsWith(s, "+CMT:")) {
        RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_NEW_SMS,
                sms_pdu,
                strlen(sms_pdu));
    } else if (strStartsWith(s, "+CDS:")) {
        RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_NEW_SMS_STATUS_REPORT,
                sms_pdu,
                strlen(sms_pdu));
    } else if (strStartsWith(s, "+CGEV: ME DEACT")
            || strStartsWith(s, "+CGEV: ME DETACH")
            || strStartsWith(s, "+CGEV: NW DETACH")
            || strStartsWith(s, "+CGEV: NW DEACT") ) {
        /* Give time to +CGACT to report correct value */
        struct timeval contextPoll = {2,0};
        RIL_requestTimedCallback(onDataCallListChanged, NULL, &contextPoll);
    } else if (strStartsWith(s, "+CGEV:")) {
        /* Really, we can ignore NW CLASS and ME CLASS events here,
         * but right now we don't since extranous
         * RIL_UNSOL_DATA_CALL_LIST_CHANGED calls are tolerated
         */
        /* can't issue AT commands here -- call on main thread */
        RIL_requestTimedCallback(onDataCallListChanged, NULL, NULL);
    } else if (strStartsWith(s, "#QSS:")) {
        /* can't issue AT commands here -- call on main thread */
        char *response;

        response = strdup(s);

        RIL_requestTimedCallback(onQssChanged, (void *)response, NULL);
    } else if (strStartsWith(s, "+WIND:")) {
        RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED,
                NULL,
                0);
    } else if (strStartsWith(s, "#PSNT:")) {
        if (sState != RADIO_STATE_OFF)
            /* THIS SHOULD PROBABLY BE RIL_UNSOL_VOICE_RADIO_TECH_CHANGED */
            RIL_onUnsolicitedResponse(
                    RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED,
                    NULL,
                    0);
    } else if (strStartsWith(s, "+CUSD:")) {
        /* TBC dcs code */
        char *response[2];
        char *lineStart;
        int m;

        line = strdup(s);
        lineStart = line;
        at_tok_start(&line);

        err = at_tok_nextint(&line, &m);
        if (err != 0) {
            RLOGE("CUSD unsolicited error: %s\n", s);
            free(lineStart);
            return;
        }
        asprintf(&response[0], "%d", m);

        switch (m) {
            case 0:
            case 1:
                err = at_tok_nextstr(&line, &response[1]);
                if (err != 0) {
                    RLOGE("CUSD unsolicited error: %s\n", s);
                    free(lineStart);
                    return;
                }
            default:
                response[1] = NULL;
        }

        RIL_onUnsolicitedResponse(RIL_UNSOL_ON_USSD,
                response,
                2 * sizeof(char *));
        free(response[0]);
        free(lineStart);
    } else if (strStartsWith(s, "+CSSI:")) {
        RIL_SuppSvcNotification ssvcNot;
        char *lineStart;

        ssvcNot.notificationType = 0;
        ssvcNot.number = NULL;

        line = strdup(s);
        lineStart = line;
        at_tok_start(&line);

        err = at_tok_nextint(&line, &(ssvcNot.code));
        if (err != 0) {
            RLOGE("CSSI unsolicited error: %s\n", s);
            free(lineStart);
            return;
        }

        err = at_tok_nextint(&line, &(ssvcNot.index));
        if (err != 0)
            RLOGW("CSSI unsolicited missing index: %s\n", s);

        RIL_onUnsolicitedResponse(RIL_UNSOL_SUPP_SVC_NOTIFICATION,
                &ssvcNot,
                sizeof(RIL_SuppSvcNotification));
        free(lineStart);
    } else if (strStartsWith(s, "+CSSU:")) {
        RIL_SuppSvcNotification ssvcNot;
        char *number;
        char *lineStart;

        ssvcNot.notificationType = 1;
        ssvcNot.number = NULL;

        line = strdup(s);
        lineStart = line;
        at_tok_start(&line);

        err = at_tok_nextint(&line, &(ssvcNot.code));
        if (err != 0) {
            RLOGE("CSSU unsolicited error: %s\n", s);
            goto error_cssu;
        }

        err = at_tok_nextint(&line, &(ssvcNot.index));
        if (err != 0) {
            RLOGW("CSSU unsolicited missing index: %s\n", s);

            RIL_onUnsolicitedResponse(RIL_UNSOL_SUPP_SVC_NOTIFICATION,
                    &ssvcNot,
                    sizeof(RIL_SuppSvcNotification));

            goto error_cssu;
        }

        err = at_tok_nextstr(&line, &number);
        if (err != 0) {
            RLOGW("CSSU missing number: %s\n", s);
        } else {
            ssvcNot.number = alloca( strlen(number) * sizeof(char) );
            strcpy(ssvcNot.number, number);

            err = at_tok_nextint(&line, &(ssvcNot.type));
            if (err != 0) {
                RLOGE("CSSU number present, but type missing: %s\n", s);
                goto error_cssu;
            }
        }

        RIL_onUnsolicitedResponse(RIL_UNSOL_SUPP_SVC_NOTIFICATION,
                &ssvcNot,
                sizeof(RIL_SuppSvcNotification));

error_cssu:
      free(lineStart);
    }
}

/* Called on command or reader thread */
static void onATReaderClosed()
{
    RLOGI("AT channel closed\n");
    at_close();
    s_closed = 1;

    setRadioState (RADIO_STATE_UNAVAILABLE);
}

/* Called on command thread */
static void onATTimeout()
{
    RLOGI("AT channel timeout; closing\n");
    at_close();

    s_closed = 1;

    /* FIXME cause a radio reset here through GPIOs*/

    setRadioState (RADIO_STATE_UNAVAILABLE);
}

/* Identify quirks for modem in use */
void identifyQuirks(char *model, char *firmwareVersion)
{
    int major = 0;
    int minor = 0;
    int eb = 0;
    char *tok;
    char *fw = firmwareVersion;

    RLOGI("Modem model: %s", model);
    RLOGI("Firmware version: %s", firmwareVersion);

    tok = strsep(&fw, ".");
    if ((tok != NULL) && (fw == NULL)) {
        RLOGE("Firmware version string is not in the expected format");
    } else {
        /* First token is platform code: not used, collecting second one */
        tok = strsep(&fw, ".");
        if (tok != NULL)
            major = atoi(tok);
        else
            goto identification;

        /* Looking for eb */
        tok = strsep(&fw, "-");
        if ((tok != NULL) && (fw != NULL)) {
            tok += 2;
            minor = atoi(tok);

            tok = strstr(fw, "B");
            if (tok != NULL) {
                tok++;
                eb = atoi(tok);
            } else {
                tok = strstr(fw, "A");
                if (tok != NULL) {
                    tok++;
                    eb = atoi(tok);
                }
            }
        } else if ((tok != NULL) && (fw == NULL)) {
            /* No eb */
            tok += 2;
            minor = atoi(tok);
        }
    }

    RLOGI("Major: %d, minor: %d, eb: %d",
            major,
            minor,
            eb);

identification:
    if (NULL != strstr(model, "HE910")) {
        RLOGI("Found HE910 modem\n");
        modemModel = MDM_HE910;
        modemQuirks = QUIRK_NO;
    } else if (NULL != strstr(model, "UE910")) {
        RLOGI("Found UE910 modem\n");
        modemModel = MDM_UE910;
        modemQuirks = QUIRK_NO;
    } else if (NULL != strstr(model, "LE910")) {
        RLOGI("Found LE910 modem");
        modemModel = MDM_LE910;
        modemQuirks = QUIRK_NO;

        if (major < 2) {
            if ((major == 0) ||
                    ((major == 1) && (minor == 0) && (eb < 61))) {
                modemQuirks |= QUIRK_CGREG_LTE;
                RLOGI("QUIRK_CGREG_LTE activated");
            }

            if ((major == 0) ||
                    ((major == 1) && (minor == 0) && (eb == 63))) {
                modemQuirks |= QUIRK_ECMC;
                RLOGI("QUIRK_ECMC activated");
            }
        }
    } else if (NULL != strstr(model, "LE920")) {
        RLOGI("Found LE920 modem");
        modemModel = MDM_LE920;
        modemQuirks = QUIRK_NO;

        if (major < 2) {
            if ((major == 0) ||
                    ((major == 1) && (minor == 0) && (eb < 61))) {
                modemQuirks |= QUIRK_CGREG_LTE;
                RLOGI("QUIRK_CGREG_LTE activated");
            }

            if ((major == 0) ||
                    ((major == 1) && (minor == 0) && (eb == 63))) {
                modemQuirks |= QUIRK_ECMC;
                RLOGI("QUIRK_ECMC activated");
            }
        }
    } else {
        /* Default choice HE910 */
        modemModel = MDM_HE910;
        modemQuirks = QUIRK_NO;
    }
}

/**
 * Find out if our modem is GSM, CDMA or both (Multimode)
 * Identify model and quirks
 */
void probeForModemMode(ModemInfo *info)
{
    int err;
    ATResponse *p_response_cgmr = NULL;
    ATResponse *p_response_cgmm = NULL;
    char *line;
    char *model;
    char *firmwareVersion;

    err = at_send_command_singleline("AT#CGMR",
            "#CGMR:",
            &p_response_cgmr,
            AT_TIMEOUT_NORMAL);
    if (err < 0 || p_response_cgmr->success == 0)
        goto error;

    line = p_response_cgmr->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextstr(&line, &firmwareVersion);
    if (err < 0) goto error;

    err = at_send_command_singleline("AT#CGMM",
            "#CGMM:",
            &p_response_cgmm,
            AT_TIMEOUT_NORMAL);
    if (err < 0 || p_response_cgmm->success == 0)
        goto error;

    line = p_response_cgmm->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextstr(&line, &model);
    if (err < 0) goto error;

    identifyQuirks(model, firmwareVersion);

error:
    at_response_free(p_response_cgmr);
    at_response_free(p_response_cgmm);

    assert (info);

    if (is_multimode_modem(info)) {
        RLOGI("Found MultimodeModem. Supported techs: %8.8x. Current tech: %d",
                info->supportedTechs,
                info->currentTech);
        return;
    }

    /* Being here means that our modem is not multimode */
    info->isMultimode = 0;

    info->supportedTechs = MDM_GSM | MDM_WCDMA;
    info->currentTech = MDM_GSM;
    RLOGI("Found GSM Modem");
}

static void finalizeInit()
{
    at_send_command("AT+CSCS=\"UCS2\"", NULL, AT_TIMEOUT_NORMAL);
    at_send_command("AT+CCWA=1", NULL, AT_TIMEOUT_CCWA);
    at_send_command("AT+COLP=0", NULL, AT_TIMEOUT_NORMAL);
    at_send_command("AT#NITZ=9,1", NULL, AT_TIMEOUT_NORMAL);
    at_send_command("AT+CUSD=1", NULL, AT_TIMEOUT_NORMAL);
    at_send_command("AT+CSSN=1,1", NULL, AT_TIMEOUT_NORMAL);
    at_send_command("AT+CGEREP=1,0", NULL, AT_TIMEOUT_NORMAL);
    at_send_command("AT#DIALMODE=0", NULL, AT_TIMEOUT_NORMAL);
    at_send_command("AT+CMGF=0", NULL, AT_TIMEOUT_NORMAL);

    setPropertyNetwork();
    setPropertyEmergencyList();
}

/**
 * Initialize everything that can be configured while we're still in
 * AT+CFUN=4
 */
void initializeCallback(void *param __unused)
{
    int err;
    ATResponse *p_response = NULL;

    setRadioState (RADIO_STATE_UNAVAILABLE);

    at_handshake();

    probeForModemMode(sMdmInfo);
    /* note: we don't check errors here. Everything important will
       be handled in onATTimeout and onATReaderClosed */
    at_send_command("ATE0", NULL, AT_TIMEOUT_NORMAL);

    /*  atchannel is tolerant of echo but it must */
    /*  have verbose result codes */
    at_send_command("ATE0Q0V1", NULL, AT_TIMEOUT_NORMAL);

    /* QSS unsolicited activation */
    at_send_command("AT#QSS=2", NULL, AT_TIMEOUT_NORMAL);

    /* GPRS network attach */
    at_send_command("AT+CGATT=1", NULL, AT_TIMEOUT_CGATT);

    /* offline start */
    at_send_command("AT+CFUN=4", NULL, AT_TIMEOUT_CFUN);
    setRadioState (RADIO_STATE_OFF);
    /* This sleep is NEEDED: many customers complain of the delays between
     * +CFUN states, but they are all needed and cannot be removed
     */
    sleep(4);

    /* Profile saving */
    at_send_command("AT&W", NULL, AT_TIMEOUT_NORMAL);
    at_send_command("AT&P", NULL, AT_TIMEOUT_NORMAL);

    /*  No auto-answer */
    at_send_command("ATS0=0", NULL, AT_TIMEOUT_NORMAL);

    /*  Extended errors */
    at_send_command("AT+CMEE=1", NULL, AT_TIMEOUT_NORMAL);

    /*  Network registration events */
    err = at_send_command("AT+CREG=2", &p_response, AT_TIMEOUT_NORMAL);

    /* some handsets -- in tethered mode -- don't support CREG=2 */
    if (err < 0 || p_response->success == 0)
        at_send_command("AT+CREG=1", NULL, AT_TIMEOUT_NORMAL);

    at_response_free(p_response);

    /*  GPRS registration events */
    at_send_command("AT+CGREG=2", NULL, AT_TIMEOUT_NORMAL);

    /*  Not muted */
    if ((modemModel & MDM_HE910) ||
            (modemModel & MDM_UE910))
        at_send_command("AT+CMUT=0", NULL, AT_TIMEOUT_NORMAL);

    /* Unsolicited for current network registration status */
    if ((modemModel & MDM_HE910) ||
            (modemModel & MDM_UE910))
        at_send_command("AT+WIND=32", NULL, AT_TIMEOUT_NORMAL);
}

const char * getRilVersion(void)
{
    if (modemModel & MDM_HE910)
        return "Telit android ril R6.00.01.b2 - HE910";
    else if (modemModel & MDM_UE910)
        return "Telit android ril R6.00.01.b2 - UE910";
    else if (modemModel & MDM_LE910)
        return "Telit android ril R6.00.01.b2 - LE910";
    else if (modemModel & MDM_LE920)
        return "Telit android ril R6.00.01.b2 - LE920";
    else
        return "Telit android ril R6.00.01.b2 - Unidentified modem";
}

void *mainLoop(void *param __unused)
{
    int fd;
    int ret;

    AT_DUMP("== ", "entering mainLoop()", -1 );
    at_set_on_reader_closed(onATReaderClosed);
    at_set_on_timeout(onATTimeout);

    for (;;) {
        fd = -1;
        while  (fd < 0) {
            if (s_port > 0) {
                fd = socket_loopback_client(s_port, SOCK_STREAM);
            } else if (s_device_socket) {
                    fd = socket_local_client( s_device_path,
                                            ANDROID_SOCKET_NAMESPACE_FILESYSTEM,
                                            SOCK_STREAM );
            } else if (s_device_path != NULL) {
                fd = open (s_device_path, O_RDWR);
                if ( fd >= 0 ) {
                    /* disable echo on serial ports */
                    struct termios ios;
                    tcgetattr( fd, &ios );
                    ios.c_lflag = 0;  /* disable ECHO, ICANON, etc... */
                    /*
                    cfmakeraw(&ios);
                    cfsetispeed(&ios, B115200);
                    cfsetospeed(&ios, B115200);
                    */
                    tcsetattr( fd, TCSANOW, &ios );
                }
            }

            if (fd < 0) {
                perror ("opening AT interface. retrying...");
                sleep(10);
                /* never returns */
            }
        }

        s_closed = 0;
        ret = at_open(fd, onUnsolicited);

        if (ret < 0) {
            RLOGE ("AT error %d on at_open\n", ret);
            return 0;
        }

        RIL_requestTimedCallback(initializeCallback, NULL, &TIMEVAL_0);

        /* Give initializeCallback a chance to dispatched, since
         * we don't presently have a cancellation mechanism
         */
        sleep(1);

        waitForClose();
        RLOGI("Re-opening after close");
    }
}
