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

#define LOG_TAG "RIL-DATACONNECTION"
#include <utils/Log.h>

#ifdef RIL_SHLIB
extern const struct RIL_Env *s_rilenv;
#endif

static char sAPN[RIL_GENERIC_STRING_SIZE];
static char sAPNUser[RIL_GENERIC_STRING_SIZE];
static char sAPNPass[RIL_GENERIC_STRING_SIZE];
static char sAPNPdp[RIL_GENERIC_STRING_SIZE];
static int sAPNAuth = 0;

extern uint64_t modemModel;
extern uint64_t modemQuirks;

void requestOrSendDataCallList(RIL_Token *t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    int value;
    char *value_str;
    char *dns1;
    char *dns2;

#if(RIL_VERSION > 10)
    RIL_Data_Call_Response_v11 *response =
        alloca(sizeof(RIL_Data_Call_Response_v11));
#else
    RIL_Data_Call_Response_v9 *response =
        alloca(sizeof(RIL_Data_Call_Response_v9));
#endif 
    response->status = -1;
    response->suggestedRetryTime = -1;
    response->cid = -1;
    response->active = -1;
    response->type = "";
    response->ifname = "";
    response->addresses = "";
    response->dnses = "";
    response->gateways = "";
    response->pcscf = NULL;
#if(RIL_VERSION > 10)
    response->mtu = -1;
#endif

    RLOGD("#ECMC: requestOrSendDataCallList");
    if (((modemModel & MDM_LE910) || (modemModel & MDM_LE920))
            && (modemQuirks & QUIRK_ECMC))
        err = at_send_command_singleline("ATE1;#ECMC?;E0",
                "#ECMC:",
                &p_response,
                AT_TIMEOUT_ECM);
    else
        err = at_send_command_singleline("AT#ECMC?",
                "#ECMC:",
                &p_response,
                AT_TIMEOUT_ECM);
    if (err != 0 || p_response->success == 0) {
        if (t != NULL)
            RIL_onRequestComplete(*t, RIL_E_GENERIC_FAILURE, NULL, 0);
        else
            RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED,
                    NULL,
                    0);

        at_response_free(p_response);
        return;
    }

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &value);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &value);
    if (err < 0) goto error;
    if (0 == value) {
        if (t != NULL)
            RIL_onRequestComplete(*t, RIL_E_GENERIC_FAILURE, NULL, 0);
        else
            RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED,
                    NULL,
                    0);

        at_response_free(p_response);
        return;
    }

    response->status = 0;
    response->cid = 1;
    response->active = 2;
    response->type = "IPV4V6";
    response->ifname = ECM_INTERFACE;

    err = at_tok_nextstr(&line, &(response->addresses));
    if (err < 0) goto error;

    err = at_tok_nextstr(&line, &value_str);
    if (err < 0) goto error;

    err = at_tok_nextstr(&line, &(response->gateways));
    if (err < 0) goto error;

    err = at_tok_nextstr(&line, &dns1);
    if (err < 0) goto error;

    if ((modemModel & MDM_LE910) || (modemModel & MDM_LE920)) {
        /* LE910 has both dnses separated by a comma */
        char *spc;
        response->dnses = alloca(strlen(dns1) + 1);
        memset(response->dnses, '\0', strlen(dns1) + 1);
        strcpy(response->dnses, dns1);

        spc = strstr(response->dnses, ",");
        if (NULL != spc)
            *spc = ' ';
    } else {
        err = at_tok_nextstr(&line, &dns2);
        if (err < 0) goto error;

        /* dns1 + <blank char> + dns2 + \0 */
        response->dnses = alloca(strlen(dns1) + strlen(dns2) + 1 + 1);
        memset(response->dnses, '\0', strlen(dns1) + strlen(dns2) + 1);
        strcpy(response->dnses, dns1);
        strcat(response->dnses, " ");
        strcat(response->dnses, dns2);
    }

#if(RIL_VERSION > 10)
    if (t != NULL)
        RIL_onRequestComplete(*t, RIL_E_SUCCESS, response,
                sizeof(RIL_Data_Call_Response_v11));
    else
        RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED,
                response,
                sizeof(RIL_Data_Call_Response_v11));
#else
    if (t != NULL)
        RIL_onRequestComplete(*t, RIL_E_SUCCESS, response,
                sizeof(RIL_Data_Call_Response_v9));
    else
        RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED,
                response,
                sizeof(RIL_Data_Call_Response_v9));
#endif

    at_response_free(p_response);
    return;

error:
    if (t != NULL)
        RIL_onRequestComplete(*t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED,
                                  NULL, 0);

    at_response_free(p_response);
}

void requestDataCallList(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    requestOrSendDataCallList(&t);
}

void requestSetInitialAttachApn(void *data,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    char *line = NULL;
    int i;

    memset(sAPN, '\0', RIL_GENERIC_STRING_SIZE);
    memset(sAPNUser, '\0', RIL_GENERIC_STRING_SIZE);
    memset(sAPNPass, '\0', RIL_GENERIC_STRING_SIZE);
    memset(sAPNPdp, '\0', RIL_GENERIC_STRING_SIZE);
    sAPNAuth = 0;

    if (NULL != ((const char **)data)[0])
        strncpy(sAPN,
                (const char * __restrict__)((const char **)data)[0],
                128);
    else
        goto error;

    if (NULL != ((const char **)data)[1])
        strncpy(sAPNPdp,
                (const char * __restrict__)((const char **)data)[1],
                128);

    sAPNAuth = ((int *)data)[2];

    asprintf(&cmd,
            "AT+CGDCONT=1,\"%s\",\"%s\",,0,0",
             strlen(sAPNPdp) > 0 ? sAPNPdp : "IP" ,
             sAPN);
    err = at_send_command(cmd, &p_response, AT_TIMEOUT_NORMAL);
    free(cmd);
    if (err < 0 || p_response->success == 0)
        goto error;

    if (sAPNAuth > 0) {
        if (NULL != ((const char **)data)[3])
            strncpy(sAPNUser,
                    (const char * __restrict__)((const char **)data)[3],
                    128);

        if (NULL != ((const char **)data)[4])
            strncpy(sAPNPass,
                    (const char * __restrict__)((const char **)data)[4],
                    128);
    }


    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
    return;

error:
    memset(sAPN, '\0', RIL_GENERIC_STRING_SIZE);
    memset(sAPNUser, '\0', RIL_GENERIC_STRING_SIZE);
    memset(sAPNPass, '\0', RIL_GENERIC_STRING_SIZE);
    memset(sAPNPdp, '\0', RIL_GENERIC_STRING_SIZE);
    sAPNAuth = 0;

    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}
void requestSetupDataCall(void *data, size_t datalen, RIL_Token t)
{
    const char *apn, *user, *pass;
    char *cmd;
    int err;
    ATResponse *p_response = NULL;
    char *line;

    apn = ((const char **)data)[2];
    user = ((char **)data)[3];
    pass = ((char **)data)[4];

    int authMode;
    const char *pdp_type;
    int skip;
    int connectState = 0;

    authMode = atoi(((const char **)data)[5]);
    if (authMode < 0)
        authMode = 0;
    if (datalen > 6 * sizeof(char *)) {
        pdp_type = ((const char **)data)[6];
    } else {
        pdp_type = "IP";
    }

    asprintf(&cmd, "AT+CGDCONT=1,\"%s\",\"%s\",,0,0", pdp_type, apn);
    //FIXME check for error here
    err = at_send_command(cmd, NULL, AT_TIMEOUT_NORMAL);
    free(cmd);

    // Set required QoS params to default
    err = at_send_command("AT+CGQREQ=1", NULL, AT_TIMEOUT_NORMAL);

    // Set minimum QoS params to default
    err = at_send_command("AT+CGQMIN=1", NULL, AT_TIMEOUT_NORMAL);

    // packet-domain event reporting
    err = at_send_command("AT+CGEREP=1,0", NULL, AT_TIMEOUT_NORMAL);

    // Hangup anything that's happening there now
    err = at_send_command_singleline("AT#ECM?", "#ECM:", &p_response, AT_TIMEOUT_ECM);
    if (err != 0 || p_response->success == 0) {
        goto error;
    }

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &skip);
    if (err < 0) goto error;
    err = at_tok_nextint(&line, &connectState);
    if (err < 0) goto error;

    if (connectState == 1) {
        err = at_send_command("AT#ECMD=0", NULL, AT_TIMEOUT_ECM);
        sleep(1);
    }

    // Start data on PDP context 1
    err = property_set(PROPERTY_SERVICE_DISABLE, ECM_CLEAN_SERVICE);
    err = property_set(PROPERTY_SERVICE_ENABLE, ECM_DHCP_SERVICE);
    if (err) {
        RLOGE("Error in starting ECM DHCP client %d", err);
        goto error;
    }
    // Few seconds needed for dhcp client and modem synchronization
    sleep(1);

    if (authMode == 0) {
        asprintf(&cmd, "AT#ECM=1,0");
    } else {
        if (pass != NULL) {
            asprintf(&cmd, "AT#ECM=1,0,\"%s\",\"%s\",1", user, pass);
        } else {
            asprintf(&cmd, "AT#ECM=1,0,\"%s\"", user);
        }
    }
    err = at_send_command(cmd, &p_response, AT_TIMEOUT_ECM);
    free(cmd);
    if (err < 0 || p_response->success == 0) {
        goto error;
    }

    requestOrSendDataCallList(&t);

    at_response_free(p_response);

    return;
error:
    err = property_set(PROPERTY_SERVICE_DISABLE, ECM_DHCP_SERVICE);
    err = property_set(PROPERTY_SERVICE_ENABLE, ECM_CLEAN_SERVICE);
    if (err)
        RLOGE("Error in starting ECM data connection %d", err);
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);

}

void requestDeactivateDataCall(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line;
    int skip;
    int connectState = 0;

    err = property_set(PROPERTY_SERVICE_DISABLE, ECM_DHCP_SERVICE);
    err = property_set(PROPERTY_SERVICE_ENABLE, ECM_CLEAN_SERVICE);

    err = at_send_command_singleline("AT#ECM?", "#ECM:", &p_response, AT_TIMEOUT_ECM);
    if (err != 0 || p_response->success == 0) {
        goto error;
    }

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &skip);
    if (err < 0) goto error;
    err = at_tok_nextint(&line, &connectState);
    if (err < 0) goto error;

    if (connectState == 1) {
        err = at_send_command("AT#ECMD=0", &p_response, AT_TIMEOUT_ECM);
        if (err != 0 || p_response->success == 0)
            goto error;
    }

#if 0
    err = at_send_command("AT#ECMD=0",
            &p_response,
            AT_TIMEOUT_ECM);
    if (err != 0 || p_response->success == 0)
        goto error;
#endif
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}
