/*
 * Copyright 2013, Telit Communications S.p.a.
 *
 * This file is licensed under a commercial license agreement.
 */

#ifndef ANDROID_RIL_REFERENCE_H
#define ANDROID_RIL_REFERENCE_H 1

/* Modem Technology bits */
#define MDM_GSM         0x01
#define MDM_WCDMA       0x02
#define MDM_CDMA        0x04
#define MDM_EVDO        0x08
#define MDM_LTE         0x10

#define COMMON_EMERGENCY_NUMBERS "112,911"
#define EMERGENCY_NUMBERS_LIST_SIZE 256

#define PROPERTY_EMERGENCY_LIST "ro.ril.ecclist"

#define RIL_onRequestComplete(t, e, response, responselen) s_rilenv->OnRequestComplete(t,e, response, responselen)
#define RIL_onUnsolicitedResponse(a,b,c) s_rilenv->OnUnsolicitedResponse(a,b,c)
#define RIL_requestTimedCallback(a,b,c) s_rilenv->RequestTimedCallback(a,b,c)

typedef struct {
    int supportedTechs; // Bitmask of supported Modem Technology bits
    int currentTech;    // Technology the modem is currently using (in the format used by modem)
    int isMultimode;

    // Preferred mode bitmask. This is actually 4 byte-sized bitmasks with different priority values,
    // in which the byte number from LSB to MSB give the priority.
    //
    //          |MSB|   |   |LSB
    // value:   |00 |00 |00 |00
    // byte #:  |3  |2  |1  |0
    //
    // Higher byte order give higher priority. Thus, a value of 0x0000000f represents
    // a preferred mode of GSM, WCDMA, CDMA, and EvDo in which all are equally preferrable, whereas
    // 0x00000201 represents a mode with GSM and WCDMA, in which WCDMA is preferred over GSM
    int32_t preferredNetworkMode;
    int subscription_source;
} ModemInfo;

typedef enum {
    SIM_ABSENT = 0,
    SIM_NOT_READY = 1,
    SIM_READY = 2, /* SIM_READY means the radio state is RADIO_STATE_SIM_READY */
    SIM_PIN = 3,
    SIM_PUK = 4,
    SIM_NETWORK_PERSONALIZATION = 5,
    RUIM_ABSENT = 6,
    RUIM_NOT_READY = 7,
    RUIM_READY = 8,
    RUIM_PIN = 9,
    RUIM_PUK = 10,
    RUIM_NETWORK_PERSONALIZATION = 11
} SIM_Status;

void *
mainLoop(void *param);

static void onRequest (int request, void *data, size_t datalen, RIL_Token t);
RIL_RadioState currentState();
static int onSupports (int requestCode);
static void onCancel (RIL_Token t);
static const char *getVersion();
const char * getRilVersion(void);

static int techFromModemType(int mdmtype);
void setRadioState(RIL_RadioState newState);
void waitForClose();

void requestGetImeiSv(void *data, size_t datalen, RIL_Token t);
void requestQueryTtyMode(void *data, size_t datalen, RIL_Token t);
void requestSetTtyMode(void *data, size_t datalen, RIL_Token t);
void requestGetHardwareConfig(void *data, size_t datalen, RIL_Token t);
void requestGetRadioCapability(void *data, size_t datalen, RIL_Token t);
void requestShutdown(RIL_Token t);
void requestGetNeighboringCell(void *data, size_t datalen, RIL_Token t);
void requestSendSMS(void *data, size_t datalen, RIL_Token t);
void requestSendSmsExpectMore(void *data, size_t datalen, RIL_Token t);
void requestGetSmscAddress(void *data, size_t datalen, RIL_Token t);
void requestReportSmsMemoryStatus(void *data, size_t datalen, RIL_Token t);
void requestSetCallForward(void *data, size_t datalen, RIL_Token t);
void requestQueryCallForwardStatus(void *data, size_t datalen, RIL_Token t);
void requestSetSuppSvcNotification(void *data, size_t datalen, RIL_Token t);
void requestSetLocationUpdates(void *data, size_t datalen, RIL_Token t);
void requestQueryCallWaiting(void *data, size_t datalen, RIL_Token t);
void requestSetCallWaiting(void *data, size_t datalen, RIL_Token t);
void requestSetSmscAddress(void *data, size_t datalen, RIL_Token t);
void requestGetClir(void *data, size_t datalen, RIL_Token t);
void requestSetClir(void *data, size_t datalen, RIL_Token t);
void requestGetSimStatus(void *data, size_t datalen, RIL_Token t);
void requestGetPreferredNetworkType(void *data, size_t datalen, RIL_Token t);
void requestSetPreferredNetworkType(void *data, size_t datalen, RIL_Token t);
void requestSetMute(void *data, size_t datalen, RIL_Token t);
void requestAnswer(void *data, size_t datalen, RIL_Token t);
void requestUdub(void *data, size_t datalen, RIL_Token t);
void requestConference(void *data, size_t datalen, RIL_Token t);
void requestSwitchWaitingOrHoldingAndActive(void *data, size_t datalen, RIL_Token t);
void requestHangupForegroundResumeBackground(void *data, size_t datalen, RIL_Token t);
void requestHangupWaitingOrBackground(void *data, size_t datalen, RIL_Token t);
void requestHangup(void *data, size_t datalen, RIL_Token t);
void requestDial(void *data, size_t datalen, RIL_Token t);
void requestBasebandVersion(void *data, size_t datalen, RIL_Token t);
void requestQueryFacility(void *data, size_t datalen, RIL_Token t);
void requestScreenState(void *data, size_t datalen, RIL_Token t);
void requestDeactivateDataCall(void *data, size_t datalen, RIL_Token t);
void requestSetFacilityLock(void *data, size_t datalen, RIL_Token t);
void requestChangeBarringPassword(void *data, size_t datalen, RIL_Token t);
void requestSetNetworkSelectionManual(void *data, size_t datalen, RIL_Token t);
void requestQueryAvailableNetworks(void *data, size_t datalen, RIL_Token t);
void requestGetMute(void *data, size_t datalen, RIL_Token t);
void requestQueryClip(void *data, size_t datalen, RIL_Token t);
void requestSetBandMode(void *data, size_t datalen, RIL_Token t);
void requestExplicitCallTransfer(void *data, size_t datalen, RIL_Token t);
void requestGsmSmsBroadcastActivation(void *data, size_t datalen, RIL_Token t);
void requestGetCurrentCalls(void *data, size_t datalen, RIL_Token t);
void requestDeviceIdentity(void *data, size_t datalen, RIL_Token t);
void requestLastCallFailCause(void *data, size_t datalen, RIL_Token t);
void requestSeparateConnections(void *data, size_t datalen, RIL_Token t);
void requestSignalStrength(void *data, size_t datalen, RIL_Token t);
void requestDataRegistrationState(void *data, size_t datalen, RIL_Token t);
void requestVoiceRegistrationState(void *data, size_t datalen, RIL_Token t);
void requestVoiceRadioTech(void *data, size_t datalen, RIL_Token t);
void requestOperator(void *data, size_t datalen, RIL_Token t);
void requestRadioPower(void *data, size_t datalen, RIL_Token t);
void requestDTMF(void *data, size_t datalen, RIL_Token t);
void requestSetupDataCall(void *data, size_t datalen, RIL_Token t);
void requestSMSAcknowledge(void *data, size_t datalen, RIL_Token t);
void requestGetIMSI(void *data, size_t datalen, RIL_Token t);
void requestGetIMEI(void *data, size_t datalen, RIL_Token t);
void requestSIM_IO(void *data, size_t datalen, RIL_Token t);
void requestSendUSSD(void *data, size_t datalen, RIL_Token t);
void requestCancelUSSD(void *data, size_t datalen, RIL_Token t);
void requestSetNetworkSelectionAutomatic(void *data, size_t datalen, RIL_Token t);
void requestDataCallList(void *data, size_t datalen, RIL_Token t);
void requestQueryNetworkSelectionMode(void *data, size_t datalen, RIL_Token t);
void requestWriteSmsToSim(void *data, size_t datalen, RIL_Token t);
void requestDeleteSmsOnSim(void *data, size_t datalen, RIL_Token t);
void requestEnterSimPin(void*  data, size_t  datalen, RIL_Token  t, int request);
void requestSetInitialAttachApn(void *data, size_t datalen, RIL_Token t);
void requestOrSendDataCallList(RIL_Token *t);

#endif /*ANDROID_RIL_REFERENCE_H */
