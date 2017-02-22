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
#include "serial/fcp_parser.h"

#define LOG_TAG "RIL-SIM"
#include <utils/Log.h>

#define CRSM_SW1_SUCCESS 0x90
#define CRSM_SW1_BYTESLEFT 0x61

#ifdef RIL_SHLIB
extern const struct RIL_Env *s_rilenv;
#endif

extern RIL_RadioState sState;
extern const struct timeval TIMEVAL_SIMPOLL;

UICC_Type UiccType = UICC_TYPE_UNKNOWN;

extern uint64_t modemModel;

char s_pin1[RIL_GENERIC_STRING_SIZE] = "";

static int convertSimIoFcp(RIL_SIM_IO_Response *sr, char **cvt)
{
    int err = 0;
    size_t fcplen;
    struct ts_51011_921_resp resp;
    void *cvt_buf = NULL;

    if (!sr->simResponse || !cvt) {
        err = -1;
        goto error;
    }

    fcplen = strlen(sr->simResponse);
    if ((fcplen == 0) || (fcplen & 1)) {
        err = -1;
        goto error;
    }

    err = fcp_to_ts_51011(sr->simResponse, fcplen, &resp);
    if (err < 0){
        goto error;
    }

    cvt_buf = malloc(sizeof(resp) * 2 + 1);
    if (!cvt_buf) {
        err = -1;
        goto error;
    }

    err = binaryToString((unsigned char*)(&resp),
            sizeof(resp), cvt_buf);
    if (err < 0){
        goto error;
    }

    /* cvt_buf ownership is moved to the caller */
    *cvt = cvt_buf;
    cvt_buf = NULL;

finally:
    return err;

error:
    free(cvt_buf);
    goto finally;
}

/**
 * Fetch information about UICC card type (SIM/USIM)
 *
 * \return UICC_Type: type of UICC card.
 */
static UICC_Type getUICCType()
{
    ATResponse *atresponse = NULL;
    int err;
    int sw1;
    int sw2;
    char *line = NULL;
    char *dir = NULL;

    if (currentState() == RADIO_STATE_OFF ||
        currentState() == RADIO_STATE_UNAVAILABLE) {
        UiccType = UICC_TYPE_UNKNOWN;
        goto exit;
    }

    /* No need to get type again, it is stored */
    if (UiccType != UICC_TYPE_UNKNOWN)
        goto exit;

    /*
     * For USIM, 'AT+CRSM=192,28423,0,0,15' will respond with TLV tag "62"
     * which indicates FCP template (refer to ETSI TS 101 220). Currently,
     * USIM detection succeeds on Innocomm Amazon1901 and Huawei EM770W.
     */
    err = at_send_command_singleline("AT+CRSM=192,28423,0,0,15",
            "+CRSM:",
            &atresponse,
            AT_TIMEOUT_CRSM);
    if (err != 0 || !atresponse->success)
        goto error;

    if (atresponse->p_intermediates != NULL) {
        line = atresponse->p_intermediates->line;

        err = at_tok_start(&line);
        if (err < 0)
            goto error;

        err = at_tok_nextint(&line, &sw1);
        if (err < 0)
            goto error;
        err = at_tok_nextint(&line, &sw2);
        if (err < 0)
            goto error;

        // Here we are interested in the first part of the returned
        // message, so it is OK to get only part of the bytes.
        if (CRSM_SW1_SUCCESS != sw1 && CRSM_SW1_BYTESLEFT != sw1) {
            RLOGW("CRSM returned error: 0X%X, 0X%X", sw1, sw2);
            goto error;
        }

        if (at_tok_hasmore(&line)) {
            err = at_tok_nextstr(&line, &dir);
            if (err < 0) goto error;
        }

        if (NULL != dir && strstr(dir, "62") == dir) {
            UiccType = UICC_TYPE_USIM;
            RLOGI("Detected card type USIM - stored");
            goto finally;
        }
    }

    UiccType = UICC_TYPE_SIM;
    RLOGI("Detected card type SIM - stored");
    goto finally;

error:
    UiccType = UICC_TYPE_UNKNOWN;
    RLOGW("%s(): Failed to detect card type - Retry at next request", __func__);

finally:
    at_response_free(atresponse);

exit:
    return UiccType;
}

void requestSIM_IO(void *data, size_t datalen __unused, RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    char *line = NULL;
    RIL_SIM_IO_v6 *p_args;
    RIL_SIM_IO_Response sr;
    int cvt_done = 0;

    p_args = (RIL_SIM_IO_v6 *)data;

ask_again:
    memset(&sr, 0, sizeof(sr));

    /* FIXME handle pin2 */
    if (p_args->data == NULL)
        asprintf(&cmd,
                "AT+CRSM=%d,%d,%d,%d,%d",
                p_args->command,
                p_args->fileid,
                p_args->p1,
                p_args->p2,
                p_args->p3);
    else
        asprintf(&cmd,
                "AT+CRSM=%d,%d,%d,%d,%d,\"%s\"",
                p_args->command,
                p_args->fileid,
                p_args->p1,
                p_args->p2,
                p_args->p3,
                p_args->data);

    err = at_send_command_singleline(cmd, "+CRSM:", &p_response, AT_TIMEOUT_CRSM);
    free(cmd);
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(sr.sw1));
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(sr.sw2));
    if (err < 0) goto error;

    if (at_tok_hasmore(&line)) {
        err = at_tok_nextstr(&line, &(sr.simResponse));
        if (err < 0) goto error;
    }

    if ((modemModel & MDM_LE910) || (modemModel & MDM_LE920)) {
        if (0x61 == sr.sw1) {
            RLOGD("More data to read: reading again");
            p_args->p3 += sr.sw2;
            at_response_free(p_response);
            goto ask_again;
        }
    }

    /*
     * In case the command is GET_RESPONSE and cardtype is 3G SIM
     * conversion to 2G FCP is required
     */
    if (p_args->command == 0xC0 && getUICCType() == UICC_TYPE_USIM) {
        if (convertSimIoFcp(&sr, &sr.simResponse) < 0) {
            //rilErrorCode = RIL_E_GENERIC_FAILURE;
            goto error;
        }
        cvt_done = 1; /* sr.simResponse needs to be freed */
    }
    RIL_onRequestComplete(t, RIL_E_SUCCESS, &sr, sizeof(sr));

    at_response_free(p_response);
    if (cvt_done)
        free(sr.simResponse);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

/** Returns SIM_NOT_READY on error */
static SIM_Status
getSIMStatus()
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    char *value_str;
    int ret;

    RLOGD("getSIMStatus(). sState: %d",sState);

    err = at_send_command_singleline("AT+CPIN?",
            "+CPIN:",
            &p_response,
            AT_TIMEOUT_CPIN);
    if (err != 0) {
        ret = SIM_NOT_READY;
        goto done;
    }

    switch (at_get_cme_error(p_response)) {
        case CME_SUCCESS:
        break;

        case CME_SIM_BUSY:
            /* When removing the sim in LE910 mini-PCI EVK this command
             * returns CME ERROR: 14 when it should be CME ERROR: 10
             */
            if ((modemModel & MDM_LE910) || (modemModel & MDM_LE920)) {
                ret = SIM_ABSENT;
            } else {
                ret = SIM_NOT_READY;
            }
        goto done;

        case CME_SIM_NOT_INSERTED:
            ret = SIM_ABSENT;
        goto done;

        default:
            ret = SIM_NOT_READY;
        goto done;
    }

    /* CPIN? has succeeded, now look at the result */
    line = p_response->p_intermediates->line;
    err = at_tok_start (&line);
    if (err < 0) {
        ret = SIM_NOT_READY;
        goto done;
    }

    err = at_tok_nextstr(&line, &value_str);
    if (err < 0) {
        ret = SIM_NOT_READY;
        goto done;
    }

    if (0 == strcmp (value_str, "SIM PIN")) {
        if (strlen(s_pin1) > 0) {
            char *cmd;
            ATResponse *p_response_set = NULL;

            asprintf(&cmd, "AT+CPIN=\"%s\"", s_pin1);
            err = at_send_command(cmd, &p_response_set, AT_TIMEOUT_CPWD);
            free(cmd);

            if (err < 0 || p_response_set->success == 0) {
                memset(s_pin1, '\0', RIL_GENERIC_STRING_SIZE);
                ret = SIM_PIN;
            } else {
                ret = SIM_READY;
            }

            /* Few seconds for sim recognition */
            sleep(2);
            at_response_free(p_response_set);
            goto done;
        } else {
            ret = SIM_PIN;
            goto done;
        }
    } else if (0 == strcmp (value_str, "SIM PUK")) {
        ret = SIM_PUK;
        goto done;
    } else if (0 == strcmp (value_str, "PH-NET PIN")) {
        ret = SIM_NETWORK_PERSONALIZATION;
        goto done;
    } else if (0 != strcmp (value_str, "READY"))  {
        /* we're treating unsupported lock types as "sim absent" */
        ret = SIM_ABSENT;
        goto done;
    }

    ret = SIM_READY;

done:
    at_response_free(p_response);
    return ret;
}

/**
 * Get the current card status.
 *
 * This must be freed using freeCardStatus.
 * @return: On success returns RIL_E_SUCCESS
 */
static int getCardStatus(RIL_CardStatus_v6 **pp_card_status) {
    static RIL_AppStatus app_status_array[] = {
        /* SIM_ABSENT = 0 */
        { RIL_APPTYPE_UNKNOWN, RIL_APPSTATE_UNKNOWN, RIL_PERSOSUBSTATE_UNKNOWN,
            NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
        /* SIM_NOT_READY = 1 */
        { RIL_APPTYPE_SIM, RIL_APPSTATE_DETECTED, RIL_PERSOSUBSTATE_UNKNOWN,
            NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
        /* SIM_READY = 2 */
        { RIL_APPTYPE_SIM, RIL_APPSTATE_READY, RIL_PERSOSUBSTATE_READY,
            NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
        /* SIM_PIN = 3 */
        { RIL_APPTYPE_SIM, RIL_APPSTATE_PIN, RIL_PERSOSUBSTATE_UNKNOWN,
            NULL, NULL, 0, RIL_PINSTATE_ENABLED_NOT_VERIFIED,
            RIL_PINSTATE_UNKNOWN },
        /* SIM_PUK = 4 */
        { RIL_APPTYPE_SIM, RIL_APPSTATE_PUK, RIL_PERSOSUBSTATE_UNKNOWN,
            NULL, NULL, 0, RIL_PINSTATE_ENABLED_BLOCKED,
            RIL_PINSTATE_UNKNOWN },
        /* SIM_NETWORK_PERSONALIZATION = 5 */
        { RIL_APPTYPE_SIM, RIL_APPSTATE_SUBSCRIPTION_PERSO,
            RIL_PERSOSUBSTATE_SIM_NETWORK, NULL, NULL, 0,
            RIL_PINSTATE_ENABLED_NOT_VERIFIED, RIL_PINSTATE_UNKNOWN },
        /* RUIM_ABSENT = 6 */
        { RIL_APPTYPE_UNKNOWN, RIL_APPSTATE_UNKNOWN, RIL_PERSOSUBSTATE_UNKNOWN,
            NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
        /* RUIM_NOT_READY = 7 */
        { RIL_APPTYPE_RUIM, RIL_APPSTATE_DETECTED, RIL_PERSOSUBSTATE_UNKNOWN,
            NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
        /* RUIM_READY = 8 */
        { RIL_APPTYPE_RUIM, RIL_APPSTATE_READY, RIL_PERSOSUBSTATE_READY,
            NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
        /* RUIM_PIN = 9 */
        { RIL_APPTYPE_RUIM, RIL_APPSTATE_PIN, RIL_PERSOSUBSTATE_UNKNOWN,
            NULL, NULL, 0, RIL_PINSTATE_ENABLED_NOT_VERIFIED,
            RIL_PINSTATE_UNKNOWN },
        /* RUIM_PUK = 10 */
        { RIL_APPTYPE_RUIM, RIL_APPSTATE_PUK, RIL_PERSOSUBSTATE_UNKNOWN,
            NULL, NULL, 0, RIL_PINSTATE_ENABLED_BLOCKED,
            RIL_PINSTATE_UNKNOWN },
        /* RUIM_NETWORK_PERSONALIZATION = 11 */
        { RIL_APPTYPE_RUIM, RIL_APPSTATE_SUBSCRIPTION_PERSO,
            RIL_PERSOSUBSTATE_SIM_NETWORK, NULL, NULL, 0,
            RIL_PINSTATE_ENABLED_NOT_VERIFIED, RIL_PINSTATE_UNKNOWN }
    };

    RIL_CardState card_state;
    int num_apps;
    int i;

    int sim_status = getSIMStatus();
    if (sim_status == SIM_ABSENT) {
        card_state = RIL_CARDSTATE_ABSENT;
        num_apps = 0;
    } else {
        card_state = RIL_CARDSTATE_PRESENT;
        num_apps = 1;
    }

    /* Allocate and initialize base card status. */
    RIL_CardStatus_v6 *p_card_status = malloc(sizeof(RIL_CardStatus_v6));
    p_card_status->card_state = card_state;
    p_card_status->universal_pin_state = RIL_PINSTATE_UNKNOWN;
    p_card_status->gsm_umts_subscription_app_index = RIL_CARD_MAX_APPS;
    p_card_status->cdma_subscription_app_index = RIL_CARD_MAX_APPS;
    p_card_status->ims_subscription_app_index = RIL_CARD_MAX_APPS;
    p_card_status->num_applications = num_apps;

    /* Initialize application status */
    for (i = 0; i < RIL_CARD_MAX_APPS; i++) {
        p_card_status->applications[i] = app_status_array[SIM_ABSENT];
    }

    /* Pickup the appropriate application status
     * that reflects sim_status for gsm.
     */
    p_card_status->num_applications = num_apps;
    p_card_status->universal_pin_state = app_status_array[sim_status].pin1;
    if (num_apps != 0) {
        p_card_status->gsm_umts_subscription_app_index = 0;
        p_card_status->cdma_subscription_app_index = -1;
        p_card_status->ims_subscription_app_index = -1;
        p_card_status->applications[0] = app_status_array[sim_status];

        if (UICC_TYPE_USIM == getUICCType() &&
                (sim_status > 0) &&
                (sim_status < 6)) {
            p_card_status->applications[0].app_type = RIL_APPTYPE_USIM;
        }
    } else {
        p_card_status->gsm_umts_subscription_app_index = -1;
        p_card_status->cdma_subscription_app_index = -1;
        p_card_status->ims_subscription_app_index = -1;
    }

    *pp_card_status = p_card_status;
    return RIL_E_SUCCESS;
}

/**
 * Free the card status returned by getCardStatus
 */
static void freeCardStatus(RIL_CardStatus_v6 *p_card_status) {
    free(p_card_status);
}

void requestGetSimStatus(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    RIL_CardStatus_v6 *p_card_status;
    char *buffer;
    int buffer_size;

    int result = getCardStatus(&p_card_status);
    if (RIL_E_SUCCESS == result) {
        buffer = (char *)p_card_status;
        buffer_size = sizeof(*p_card_status);
    } else {
        buffer = NULL;
        buffer_size = 0;
    }

    RIL_onRequestComplete(t, result, buffer, buffer_size);
    freeCardStatus(p_card_status);
}
