#ifndef ANDROID_RIL_SERIAL_H
#define ANDROID_RIL_SERIAL_H 1

#define PROPERTY_SERVICE_ENABLE "ctl.start"
#define PROPERTY_SERVICE_DISABLE "ctl.stop"
#define PROPERTY_SERVICE_STATE_RUNNING "running"
#define PROPERTY_SERVICE_STATE_STOPPED "stopped"

#define ECM_INTERFACE "wwan0"
#define ECM_DHCP_SERVICE "dhcpcd_" ECM_INTERFACE
#define ECM_CLEAN_SERVICE "clean_" ECM_INTERFACE

#define REG_DATA_STATE_LEN 6
#define REG_VOICE_STATE_LEN 15

#define MAX_AT_RESPONSE 0x1000
#define RIL_GENERIC_STRING_SIZE 128

typedef enum {
    UICC_TYPE_UNKNOWN,
    UICC_TYPE_SIM,
    UICC_TYPE_USIM,
} UICC_Type;

#define MDM_HE910       (1u << 0)
#define MDM_LE910       (1u << 1)
#define MDM_LE920       (1u << 2)
#define MDM_UE910       (1u << 7)

#define QUIRK_NO        (1u << 0)
/* +CGREG? response has one parameter missing with
 * fw version < 17.01.520-B063
 */
#define QUIRK_CGREG_LTE (1u << 3)
/* 17.01.520-B063 has #ECMC? missing response with echo turned off
 * and +CFUN=1
 */
#define QUIRK_ECMC      (1u << 4)

void onDataCallListChanged(void *param);
void onQssChanged(void *param);
void onSIMReady();

int isRadioOn();
static void finalizeInit();
static int getCardStatus(RIL_CardStatus_v6 **pp_card_status);
static void freeCardStatus(RIL_CardStatus_v6 *p_card_status);
void probeForModemMode(ModemInfo *info);
int setDataNetworkType(int state);

#endif // ANDROID_RIL_SERIAL_H
