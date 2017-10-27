/* //device/system/reference-ril/reference-ril.c
**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

/*
 * Copyright 2013, Telit Communications S.p.a.
 *
 * The original code has been changed for supporting Telit modems
 *
 * The modified code is licensed under a commercial license agreement.
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
#include <telephony/ril_cdma_sms.h>

#include "ril.h"
#include "reference-ril.h"

#define LOG_TAG "RIL"
#include <utils/Log.h>

#include "misc.h"

#define GSM_NETWORK             0x00000001
#define WCDMA_GSM_NETWORK       0x00000201
#define CDMA_NETWORK            0x00000004
#define EVDO_CDMA_NETWORK       0x0000000C

ModemInfo *sMdmInfo;
// TECH returns the current technology in the format used by the modem.
// It can be used as an l-value
#define TECH(mdminfo)                 ((mdminfo)->currentTech)
// TECH_BIT returns the bitmask equivalent of the current tech
#define TECH_BIT(mdminfo)            (1 << ((mdminfo)->currentTech))
#define IS_MULTIMODE(mdminfo)         ((mdminfo)->isMultimode)
#define TECH_SUPPORTED(mdminfo, tech) ((mdminfo)->supportedTechs & (tech))
#define PREFERRED_NETWORK(mdminfo)    ((mdminfo)->preferredNetworkMode)
// CDMA Subscription Source
#define SSOURCE(mdminfo)              ((mdminfo)->subscription_source)

static int net2modem[] = {
    MDM_GSM | MDM_WCDMA,                                 // 0  - GSM / WCDMA Pref
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

static int is3gpp2(int radioTech) {
    switch (radioTech) {
        case RADIO_TECH_IS95A:
        case RADIO_TECH_IS95B:
        case RADIO_TECH_1xRTT:
        case RADIO_TECH_EVDO_0:
        case RADIO_TECH_EVDO_A:
        case RADIO_TECH_EVDO_B:
        case RADIO_TECH_EHRPD:
            return 1;
        default:
            return 0;
    }
}

extern const char * requestToString(int request);

/*** Static Variables ***/
static const RIL_RadioFunctions s_callbacks = {
    RIL_VERSION,
    onRequest,
    currentState,
    onSupports,
    onCancel,
    getVersion
};

#ifdef RIL_SHLIB
const struct RIL_Env *s_rilenv;
#endif

RIL_RadioState sState = RADIO_STATE_UNAVAILABLE;

static pthread_mutex_t s_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_state_cond = PTHREAD_COND_INITIALIZER;

int s_port = -1;
const char * s_device_path = NULL;
int          s_device_socket = 0;

/* trigger change to this with s_state_cond */
int s_closed = 0;

const struct timeval TIMEVAL_SIMPOLL = {1,0};
const struct timeval TIMEVAL_CALLSTATEPOLL = {0,500000};
const struct timeval TIMEVAL_0 = {0,0};

/* fd for /sys/power/wake_lock and wake_unlock */
int fd_wakeLock = -1;
int fd_wakeUnlock = -1;
int powerManagement = 0;
#define WAKE_LOCK_PATH "/sys/power/wake_lock"
#define WAKE_UNLOCK_PATH "/sys/power/wake_unlock"
#define WAKE_LOCK_NAME "ril-ps"

int s_ims_gsm_retry   = 0; /* 1==causes sms over gsm to temp fail */
int s_ims_gsm_fail    = 0; /* 1==causes sms over gsm to permanent fail */

/**
 * networkModePossible. Decides whether the network mode is appropriate for the
 * specified modem
 */
static int networkModePossible(ModemInfo *mdm, int nm)
{
    if ((net2modem[nm] & mdm->supportedTechs) == net2modem[nm]) {
       return 1;
    }
    return 0;
}

static int techFromModemType(int mdmtype)
{
    int ret = -1;
    switch (1 << mdmtype) {
        case MDM_CDMA:
            ret = RADIO_TECH_1xRTT;
            break;
        case MDM_EVDO:
            ret = RADIO_TECH_EVDO_A;
            break;
        case MDM_GSM:
            ret = RADIO_TECH_GPRS;
            break;
        case MDM_WCDMA:
            ret = RADIO_TECH_HSPA;
            break;
        case MDM_LTE:
            ret = RADIO_TECH_LTE;
            break;
    }
    return ret;
}

/*** Callback methods from the RIL library to us ***/

/**
 * Call from RIL to us to make a RIL_REQUEST
 *
 * Must be completed with a call to RIL_onRequestComplete()
 *
 * RIL_onRequestComplete() may be called from any thread, before or after
 * this function returns.
 *
 * Will always be called from the same thread, so returning here implies
 * that the radio is ready to process another command (whether or not
 * the previous command has completed).
 */
static void onRequest(int request, void *data, size_t datalen, RIL_Token t)
{
    RLOGD("onRequest: %s datalen: %d, data %p",
            requestToString(request),
            datalen,
            data);

    /* Ignore all requests except RIL_REQUEST_GET_SIM_STATUS
     * when RADIO_STATE_UNAVAILABLE.
     */
    if (sState == RADIO_STATE_UNAVAILABLE
        && request != RIL_REQUEST_GET_SIM_STATUS
    ) {
        RIL_onRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);

        return;
    }

    /* Ignore all non-power requests when RADIO_STATE_OFF
     * (except RIL_REQUEST_GET_SIM_STATUS)
     */
    if (sState == RADIO_STATE_OFF
        && !(request == RIL_REQUEST_RADIO_POWER
            || request == RIL_REQUEST_GET_SIM_STATUS)
    ) {
        RIL_onRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);

        return;
    }

    switch (request) {
        case RIL_REQUEST_GET_IMEISV:
            requestGetImeiSv(data, datalen, t);
        break;
        case RIL_REQUEST_QUERY_TTY_MODE:
            requestQueryTtyMode(data, datalen, t);
        break;
        case RIL_REQUEST_SET_TTY_MODE:
            requestSetTtyMode(data, datalen, t);
        break;
        case RIL_REQUEST_GET_HARDWARE_CONFIG:
            requestGetHardwareConfig(data, datalen, t);
        break;
#if 0
        case RIL_REQUEST_GET_CELL_INFO_LIST:
            requestGetCellInfoList(data, datalen, t);
        break;
#endif
        case RIL_REQUEST_SET_INITIAL_ATTACH_APN:
            requestSetInitialAttachApn(data, datalen, t);
        break;
        case RIL_REQUEST_GET_RADIO_CAPABILITY:
            requestGetRadioCapability(data, datalen, t);
        break;
        case RIL_REQUEST_SHUTDOWN:
            requestShutdown(t);
        break;
        case RIL_REQUEST_GET_NEIGHBORING_CELL_IDS:
            requestGetNeighboringCell(data, datalen, t);
        break;
        case RIL_REQUEST_SEND_SMS_EXPECT_MORE:
            requestSendSmsExpectMore(data, datalen, t);
        break;
        case RIL_REQUEST_REPORT_SMS_MEMORY_STATUS:
            requestReportSmsMemoryStatus(data, datalen, t);
        break;
        case RIL_REQUEST_GET_SMSC_ADDRESS:
            requestGetSmscAddress(data, datalen, t);
        break;
        case RIL_REQUEST_DEVICE_IDENTITY:
            requestDeviceIdentity(data, datalen, t);
        break;
        case RIL_REQUEST_GSM_SMS_BROADCAST_ACTIVATION:
            requestGsmSmsBroadcastActivation(data, datalen, t);
        break;
        case RIL_REQUEST_EXPLICIT_CALL_TRANSFER:
            requestExplicitCallTransfer(data, datalen, t);
        break;
        case RIL_REQUEST_SET_BAND_MODE:
            requestSetBandMode(data, datalen, t);
        break;
        case RIL_REQUEST_QUERY_CLIP:
            requestQueryClip(data, datalen, t);
        break;
        case RIL_REQUEST_GET_MUTE:
            requestGetMute(data, datalen, t);
        break;
        case RIL_REQUEST_QUERY_AVAILABLE_NETWORKS:
            requestQueryAvailableNetworks(data, datalen, t);
        break;
        case RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL:
            requestSetNetworkSelectionManual(data, datalen, t);
        break;
        case RIL_REQUEST_CHANGE_BARRING_PASSWORD:
            requestChangeBarringPassword(data, datalen, t);
        break;
        case RIL_REQUEST_SET_FACILITY_LOCK:
            requestSetFacilityLock(data, datalen, t);
        break;
        case RIL_REQUEST_DEACTIVATE_DATA_CALL:
            requestDeactivateDataCall(data, datalen, t);
        break;
        case RIL_REQUEST_QUERY_CALL_WAITING:
            requestQueryCallWaiting(data, datalen, t);
        break;
        case RIL_REQUEST_SET_CALL_FORWARD:
            requestSetCallForward(data, datalen, t);
        break;
        case RIL_REQUEST_QUERY_CALL_FORWARD_STATUS:
            requestQueryCallForwardStatus(data, datalen, t);
        break;
        case RIL_REQUEST_SET_SUPP_SVC_NOTIFICATION:
            requestSetSuppSvcNotification(data, datalen, t);
        break;
        case RIL_REQUEST_SET_LOCATION_UPDATES:
            requestSetLocationUpdates(data, datalen, t);
        break;
        case RIL_REQUEST_SET_SMSC_ADDRESS:
            requestSetSmscAddress(data, datalen, t);
        break;
        case RIL_REQUEST_SET_CALL_WAITING:
            requestSetCallWaiting(data, datalen, t);
        break;
        case RIL_REQUEST_GET_CLIR:
            requestGetClir(data, datalen, t);
        break;
        case RIL_REQUEST_SET_CLIR:
            requestSetClir(data, datalen, t);
        break;
        case RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE:
            requestGetPreferredNetworkType(data, datalen, t);
        break;
        case RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE:
            requestSetPreferredNetworkType(data, datalen, t);
        break;
        case RIL_REQUEST_SET_MUTE:
            requestSetMute(data, datalen, t);
        break;
        case RIL_REQUEST_BASEBAND_VERSION:
            requestBasebandVersion(data, datalen, t);
        break;
        case RIL_REQUEST_QUERY_FACILITY_LOCK:
            requestQueryFacility(data, datalen, t);
        break;
        case RIL_REQUEST_SCREEN_STATE:
            if (fd_wakeLock > 0) {
                write(fd_wakeLock, WAKE_LOCK_NAME, strlen(WAKE_LOCK_NAME));
                RLOGI("Wake lock acquired\n");
            }
            requestScreenState(data, datalen, t);
            if (fd_wakeUnlock > 0) {
                write(fd_wakeUnlock, WAKE_LOCK_NAME, strlen(WAKE_LOCK_NAME));
                RLOGI("Wake lock released\n");
            }
        break;
        case RIL_REQUEST_GET_SIM_STATUS:
            requestGetSimStatus(data, datalen, t);
        break;
        case RIL_REQUEST_GET_CURRENT_CALLS:
            requestGetCurrentCalls(data, datalen, t);
        break;
        case RIL_REQUEST_DIAL:
            requestDial(data, datalen, t);
        break;
        case RIL_REQUEST_HANGUP:
            requestHangup(data, datalen, t);
        break;
        case RIL_REQUEST_HANGUP_WAITING_OR_BACKGROUND:
            requestHangupWaitingOrBackground(data, datalen, t);
        break;
        case RIL_REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND:
            requestHangupForegroundResumeBackground(data, datalen, t);
        break;
        case RIL_REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE:
            requestSwitchWaitingOrHoldingAndActive(data, datalen, t);
        break;
        case RIL_REQUEST_ANSWER:
            requestAnswer(data, datalen, t);
        break;
        case RIL_REQUEST_CONFERENCE:
            requestConference(data, datalen, t);
        break;
        case RIL_REQUEST_UDUB:
            requestUdub(data, datalen, t);
        break;
        case RIL_REQUEST_LAST_CALL_FAIL_CAUSE:
            requestLastCallFailCause(data, datalen, t);
        break;
        case RIL_REQUEST_SEPARATE_CONNECTION:
            requestSeparateConnections(data, datalen, t);
        break;
        case RIL_REQUEST_SIGNAL_STRENGTH:
            requestSignalStrength(data, datalen, t);
        break;
        case RIL_REQUEST_DATA_REGISTRATION_STATE:
            requestDataRegistrationState(data, datalen, t);
        break;
        case RIL_REQUEST_VOICE_REGISTRATION_STATE:
            requestVoiceRegistrationState(data, datalen, t);
        break;
        case RIL_REQUEST_OPERATOR:
            requestOperator(data, datalen, t);
        break;
        case RIL_REQUEST_RADIO_POWER:
            requestRadioPower(data, datalen, t);
        break;
        case RIL_REQUEST_DTMF:
            requestDTMF(data, datalen, t);
        break;
        case RIL_REQUEST_SEND_SMS:
            requestSendSMS(data, datalen, t);
        break;
        case RIL_REQUEST_SETUP_DATA_CALL:
            requestSetupDataCall(data, datalen, t);
        break;
        case RIL_REQUEST_SMS_ACKNOWLEDGE:
            requestSMSAcknowledge(data, datalen, t);
        break;
        case RIL_REQUEST_GET_IMSI:
            requestGetIMSI(data, datalen, t);
        break;
        case RIL_REQUEST_GET_IMEI:
            requestGetIMEI(data, datalen, t);
        break;
        case RIL_REQUEST_SIM_IO:
            requestSIM_IO(data,datalen,t);
        break;
        case RIL_REQUEST_SEND_USSD:
            requestSendUSSD(data, datalen, t);
        break;
        case RIL_REQUEST_CANCEL_USSD:
            requestCancelUSSD(data, datalen, t);
        break;
        case RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC:
            requestSetNetworkSelectionAutomatic(data, datalen, t);
        break;
        case RIL_REQUEST_DATA_CALL_LIST:
            requestDataCallList(data, datalen, t);
        break;
        case RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE:
            requestQueryNetworkSelectionMode(data, datalen, t);
        break;
        case RIL_REQUEST_WRITE_SMS_TO_SIM:
            requestWriteSmsToSim(data, datalen, t);
        break;
        case RIL_REQUEST_DELETE_SMS_ON_SIM:
            requestDeleteSmsOnSim(data, datalen, t);
        break;
        case RIL_REQUEST_ENTER_SIM_PIN:
        case RIL_REQUEST_ENTER_SIM_PUK:
        case RIL_REQUEST_ENTER_SIM_PIN2:
        case RIL_REQUEST_ENTER_SIM_PUK2:
        case RIL_REQUEST_CHANGE_SIM_PIN:
        case RIL_REQUEST_CHANGE_SIM_PIN2:
            requestEnterSimPin(data, datalen, t, request);
        break;
        case RIL_REQUEST_VOICE_RADIO_TECH:
            requestVoiceRadioTech(data, datalen, t);
        break;
        case RIL_REQUEST_OEM_HOOK_RAW:
            /* echo back data */
            RIL_onRequestComplete(t, RIL_E_SUCCESS, data, datalen);

        break;
        case RIL_REQUEST_OEM_HOOK_STRINGS: {
            int i;
            const char ** cur;

            RLOGD("got OEM_HOOK_STRINGS: 0x%8p %lu", data, (long)datalen);

            for (i = (datalen / sizeof (char *)), cur = (const char **)data;
                    i > 0;
                    cur++, i --)
                RLOGD("> '%s'", *cur);

            /* echo back strings */
            RIL_onRequestComplete(t, RIL_E_SUCCESS, data, datalen);

        break;
        }
        default:
            RLOGD("Request not supported. Tech: %d",TECH(sMdmInfo));
            RIL_onRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);

        break;
    }
}

/**
 * Synchronous call from the RIL to us to return current radio state.
 * RADIO_STATE_UNAVAILABLE should be the initial state.
 */
RIL_RadioState
currentState()
{
    return sState;
}
/**
 * Call from RIL to us to find out whether a specific request code
 * is supported by this implementation.
 *
 * Return 1 for "supported" and 0 for "unsupported"
 */

static int
onSupports (int requestCode __unused)
{
    //@@@ todo

    return 1;
}

static void onCancel (RIL_Token t __unused)
{
    //@@@todo
}

static const char * getVersion(void)
{
    return getRilVersion();
}

void
setRadioState(RIL_RadioState newState)
{
    RLOGD("setRadioState(%d)", newState);
    RIL_RadioState oldState;

    pthread_mutex_lock(&s_state_mutex);

    oldState = sState;

    if (s_closed > 0) {
        /* If we're closed, the only reasonable state is
         * RADIO_STATE_UNAVAILABLE
         * This is here because things on the main thread
         * may attempt to change the radio state after the closed
         * event happened in another thread
         */
        newState = RADIO_STATE_UNAVAILABLE;
    }

    if (sState != newState || s_closed > 0) {
        sState = newState;

        pthread_cond_broadcast (&s_state_cond);
    }

    pthread_mutex_unlock(&s_state_mutex);

    /* do these outside of the mutex */
    if (sState != oldState) {
        RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED,
                NULL,
                0);
    }
}

void waitForClose()
{
    pthread_mutex_lock(&s_state_mutex);

    while (s_closed == 0) {
        pthread_cond_wait(&s_state_cond, &s_state_mutex);
    }

    pthread_mutex_unlock(&s_state_mutex);
}

static void usage(char *s __unused)
{
#ifdef RIL_SHLIB
    fprintf(stderr, "reference-ril requires: -p <tcp port> or -d /dev/tty_device\n");
#else
    fprintf(stderr, "usage: %s [-p <tcp port>] [-d /dev/tty_device]\n", s);
    exit(-1);
#endif
}

#ifdef RIL_SHLIB
pthread_t s_tid_mainloop;

const RIL_RadioFunctions *RIL_Init(const struct RIL_Env *env, int argc, char **argv)
{
    int ret;
    int fd = -1;
    int opt;
    pthread_attr_t attr;

    s_rilenv = env;

    while ( -1 != (opt = getopt(argc, argv, "p:d:s:c:"))) {
        switch (opt) {
            case 'p':
                s_port = atoi(optarg);
                if (s_port == 0) {
                    usage(argv[0]);
                    return NULL;
                }
                RLOGI("Opening loopback port %d\n", s_port);
            break;

            case 'd':
                s_device_path = optarg;
                RLOGI("Opening tty device %s\n", s_device_path);
            break;

            case 's':
                s_device_path   = optarg;
                s_device_socket = 1;
                RLOGI("Opening socket %s\n", s_device_path);
            break;

            case 'c':
                RLOGI("Client id received %s\n", optarg);
            break;

            default:
                usage(argv[0]);
                return NULL;
        }
    }

    if (s_port < 0 && s_device_path == NULL) {
        usage(argv[0]);
        return NULL;
    }

    sMdmInfo = calloc(1, sizeof(ModemInfo));
    if (!sMdmInfo) {
        RLOGE("Unable to alloc memory for ModemInfo");
        return NULL;
    }

    fd_wakeLock = open (WAKE_LOCK_PATH, O_RDWR);
    if (-1 != fd_wakeLock) {
        fd_wakeUnlock = open (WAKE_UNLOCK_PATH, O_RDWR);
        if (-1 != fd_wakeUnlock) {
            powerManagement = 1;
            RLOGI("Wake lock file descriptors created: power management available\n");
        } else {
            close(fd_wakeLock);
            fd_wakeLock = -1;
            RLOGI("Unable to open /sys/power/wake_unlock: power management unavailable\n");
        }
    } else {
        RLOGI("Unable to open /sys/power/wake_lock: power management unavailable\n");
    }

    pthread_attr_init (&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    ret = pthread_create(&s_tid_mainloop, &attr, mainLoop, NULL);

    return &s_callbacks;
}
#else /* RIL_SHLIB */
int main (int argc, char **argv)
{
    int ret;
    int fd = -1;
    int opt;

    while ( -1 != (opt = getopt(argc, argv, "p:d:"))) {
        switch (opt) {
            case 'p':
                s_port = atoi(optarg);
                if (s_port == 0) {
                    usage(argv[0]);
                }
                RLOGI("Opening loopback port %d\n", s_port);
            break;

            case 'd':
                s_device_path = optarg;
                RLOGI("Opening tty device %s\n", s_device_path);
            break;

            case 's':
                s_device_path   = optarg;
                s_device_socket = 1;
                RLOGI("Opening socket %s\n", s_device_path);
            break;

            default:
                usage(argv[0]);
        }
    }

    if (s_port < 0 && s_device_path == NULL) {
        usage(argv[0]);
    }

    RIL_register(&s_callbacks);

    mainLoop(NULL);

    return 0;
}

#endif /* RIL_SHLIB */
