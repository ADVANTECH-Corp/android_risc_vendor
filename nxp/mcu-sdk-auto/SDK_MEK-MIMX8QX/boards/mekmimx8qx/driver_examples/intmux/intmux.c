/*
 * Copyright 2017 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "fsl_common.h"
#include "fsl_debug_console.h"
#include "fsl_intmux.h"
#include "fsl_irqsteer.h"
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_lpuart.h"
#include "fsl_lpit.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/
/* Timer to generate input interrupt source for INTMUX */
#define DEMO_LPIT_BASE CM4__LPIT
#define LPIT_SOURCECLOCK CLOCK_GetIpFreq(kCLOCK_M4_0_Lpit)

#define DEMO_IRQSTEER_BASE IRQSTEER
#define DEMO_INTMUX_BASE CM4__INTMUX

#define DEMO_INTMUX_CHANNEL 0U
#define DEMO_INTMUX_SOURCE_IRQ M4_INTMUX_SOURCE_LPIT_IRQn
#define DEMO_M4_INT_OUT_IRQn M4_INT_OUT0_IRQn /* The IRQSTEER IRQ handle */


/*******************************************************************************
 * Prototypes
 ******************************************************************************/
void DEMO_InitIntmuxSource(void);

/*******************************************************************************
 * Variables
 ******************************************************************************/

/* Global variable indicating interrupt is handled*/
volatile bool isrFlag = false;

/*******************************************************************************
 * Code
 ******************************************************************************/
void M4_INT_OUT0_IRQHandler(void)
{
    uint32_t pendingIrq = INTMUX_GetChannelPendingSources(DEMO_INTMUX_BASE, DEMO_INTMUX_CHANNEL);
    if ((1 << (DEMO_INTMUX_SOURCE_IRQ - FSL_FEATURE_INTMUX_IRQ_START_INDEX)) & pendingIrq)
    {
        /* Clear interrupt flag.*/
        LPIT_ClearStatusFlags(DEMO_LPIT_BASE, kLPIT_Channel0TimerFlag);
        isrFlag = true;
    }
    /* Add for ARM errata 838869, affects Cortex-M4, Cortex-M4F Store immediate overlapping
    exception return operation might vector to incorrect interrupt */
    __DSB();
}


void DEMO_InitIntmuxSource(void)
{
    /* Structure of initialize LPIT */
    lpit_config_t lpitConfig;
    lpit_chnl_params_t lpitChannelConfig;

    /*
     * lpitConfig.enableRunInDebug = false;
     * lpitConfig.enableRunInDoze = false;
     */
    LPIT_GetDefaultConfig(&lpitConfig);

    /* Init lpit module */
    LPIT_Init(DEMO_LPIT_BASE, &lpitConfig);

    lpitChannelConfig.chainChannel          = false;
    lpitChannelConfig.enableReloadOnTrigger = false;
    lpitChannelConfig.enableStartOnTrigger  = false;
    lpitChannelConfig.enableStopOnTimeout   = false;
    lpitChannelConfig.timerMode             = kLPIT_PeriodicCounter;
    /* Set default values for the trigger source */
    lpitChannelConfig.triggerSelect = kLPIT_Trigger_TimerChn0;
    lpitChannelConfig.triggerSource = kLPIT_TriggerSource_External;

    /* Init lpit channel 0 */
    LPIT_SetupChannel(DEMO_LPIT_BASE, kLPIT_Chnl_0, &lpitChannelConfig);

    /* Set timer period for channel 0 */
    LPIT_SetTimerPeriod(DEMO_LPIT_BASE, kLPIT_Chnl_0, USEC_TO_COUNT(1000000U, LPIT_SOURCECLOCK));

    /* Enable timer interrupts for channel 0 */
    LPIT_EnableInterrupts(DEMO_LPIT_BASE, kLPIT_Channel0TimerInterruptEnable);

    /* Start timer */
    LPIT_StartTimer(DEMO_LPIT_BASE, kLPIT_Chnl_0);
}

/*!
 * @brief Main function
 */
int main(void)
{
    /* Init board hardware. */
    sc_ipc_t ipc;

    ipc = BOARD_InitRpc();
    BOARD_InitPins(ipc);
    BOARD_BootClockRUN();
    BOARD_InitDebugConsole();
    BOARD_InitMemory();

    /* Power on Peripherals. */
    if (sc_pm_set_resource_power_mode(ipc, SC_R_M4_0_PIT, SC_PM_PW_MODE_ON) != SC_ERR_NONE)
    {
        PRINTF("Error: Failed to power on LPIT\r\n");
    }

    if (sc_pm_set_resource_power_mode(ipc, SC_R_M4_0_INTMUX, SC_PM_PW_MODE_ON) != SC_ERR_NONE)
    {
        PRINTF("Error: Failed to power on INTMUX\r\n");
    }

    if (sc_pm_set_resource_power_mode(ipc, SC_R_IRQSTR_M4_0, SC_PM_PW_MODE_ON) != SC_ERR_NONE)
    {
        PRINTF("Error: Failed to power on IRQSTR\r\n");
    }

    /* Set peripheral's clock. */
    if (CLOCK_SetIpFreq(kCLOCK_M4_0_Lpit, SC_66MHZ) == 0)
    {
        PRINTF("Error: Failed to set LPIT frequency\r\n");
    }

    PRINTF("INTMUX example started.\r\n");

    /* 1. Init INTMUX, configure the IRQ source routed to system. */
    INTMUX_Init(DEMO_INTMUX_BASE);
    INTMUX_EnableInterrupt(DEMO_INTMUX_BASE, DEMO_INTMUX_CHANNEL, DEMO_INTMUX_SOURCE_IRQ);

    /* 2. Setup the local IRQSTEER to handle the system IRQ generated by INTMUX. */
    /* This step is used to show the interrupt from M4 Subsytem has been routed to system
      and could be used by other subsystem through IRQSTEER.*/
    IRQSTEER_Init(DEMO_IRQSTEER_BASE);
    IRQSTEER_EnableInterrupt(IRQSTEER, DEMO_M4_INT_OUT_IRQn);

    /* 3. Enable the interrupt source (INTMUX input source) */
    DEMO_InitIntmuxSource();

    while (1)
    {
        while (!isrFlag)
        {
        }
        isrFlag = false;
        PRINTF("The interrupt came from INTMUX was handled\r\n");
    }
}
