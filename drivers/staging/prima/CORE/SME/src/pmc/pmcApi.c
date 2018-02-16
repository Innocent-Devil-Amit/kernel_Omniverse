/*
 * Copyright (c) 2012-2017 The Linux Foundation. All rights reserved.
 *
 * Previously licensed under the ISC license by Qualcomm Atheros, Inc.
 *
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This file was originally distributed by Qualcomm Atheros, Inc.
 * under proprietary terms before Copyright ownership was assigned
 * to the Linux Foundation.
 */

/******************************************************************************
*
* Name:  pmcApi.c
*
* Description: Routines that make up the Power Management Control (PMC) API.
*
*
******************************************************************************/

#include "palTypes.h"
#include "aniGlobal.h"
#include "palTimer.h"
#include "csrLinkList.h"
#include "smsDebug.h"
#include "sme_Trace.h"
#include "pmcApi.h"
#include "pmc.h"
#include "cfgApi.h"
#include "smeInside.h"
#include "csrInsideApi.h"
#include "wlan_ps_wow_diag.h"
#include "wlan_qct_wda.h"
#include "limSessionUtils.h"
#include "csrInsideApi.h"

extern void pmcReleaseCommand( tpAniSirGlobal pMac, tSmeCmd *pCommand );

void pmcCloseDeferredMsgList(tpAniSirGlobal pMac);
void pmcCloseDeviceStateUpdateList(tpAniSirGlobal pMac);
void pmcCloseRequestStartUapsdList(tpAniSirGlobal pMac);
void pmcCloseRequestBmpsList(tpAniSirGlobal pMac);
void pmcCloseRequestFullPowerList(tpAniSirGlobal pMac);
void pmcClosePowerSaveCheckList(tpAniSirGlobal pMac);

/******************************************************************************
*
* Name:  pmcOpen
*
* Description:
*    Does a PMC open operation on the device.
*
* Parameters:
*    hHal - HAL handle for device
*
* Returns:
*    eHAL_STATUS_SUCCESS - open successful
*    eHAL_STATUS_FAILURE - open not successful
*
******************************************************************************/
eHalStatus pmcOpen (tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    pmcLog(pMac, LOG2, FL("Entering pmcOpen"));

    /* Initialize basic PMC information about device. */
    pMac->pmc.powerSource = BATTERY_POWER;
    pMac->pmc.pmcState = STOPPED;
    pMac->pmc.pmcReady = FALSE;

    /* Initialize Power Save Modes */
    pMac->pmc.impsEnabled = FALSE;
    pMac->pmc.autoBmpsEntryEnabled = FALSE;
    pMac->pmc.smpsEnabled = FALSE;
    pMac->pmc.uapsdEnabled = TRUE;
    pMac->pmc.bmpsEnabled = TRUE;
    pMac->pmc.standbyEnabled = TRUE;
    pMac->pmc.wowlEnabled = TRUE;
    pMac->pmc.rfSuppliesVotedOff= FALSE;

    vos_mem_set(&(pMac->pmc.bmpsConfig), sizeof(tPmcBmpsConfigParams), 0);
    vos_mem_set(&(pMac->pmc.impsConfig), sizeof(tPmcImpsConfigParams), 0);
    vos_mem_set(&(pMac->pmc.smpsConfig), sizeof(tPmcSmpsConfigParams), 0);

    /* Allocate a timer to use with IMPS. */
    if (vos_timer_init(&pMac->pmc.hImpsTimer, VOS_TIMER_TYPE_SW, pmcImpsTimerExpired, hHal) != VOS_STATUS_SUCCESS)
    {
        pmcLog(pMac, LOGE, FL("Cannot allocate timer for IMPS"));
        return eHAL_STATUS_FAILURE;
    }

    /* Allocate a timer used in Full Power State to measure traffic
       levels and determine when to enter BMPS. */
    if (!VOS_IS_STATUS_SUCCESS(vos_timer_init(&pMac->pmc.hTrafficTimer, 
                                VOS_TIMER_TYPE_SW, pmcTrafficTimerExpired, hHal)))
    {
        pmcLog(pMac, LOGE, FL("Cannot allocate timer for traffic measurement"));
        return eHAL_STATUS_FAILURE;
    }

    //Initialize the default value for Bmps related config. 
    pMac->pmc.bmpsConfig.trafficMeasurePeriod = BMPS_TRAFFIC_TIMER_DEFAULT;
    pMac->pmc.bmpsConfig.bmpsPeriod = WNI_CFG_LISTEN_INTERVAL_STADEF;

    /* Allocate a timer used to schedule a deferred power save mode exit. */
    if (vos_timer_init(&pMac->pmc.hExitPowerSaveTimer, VOS_TIMER_TYPE_SW,
                      pmcExitPowerSaveTimerExpired, hHal) !=VOS_STATUS_SUCCESS)
    {
        pmcLog(pMac, LOGE, FL("Cannot allocate exit power save mode timer"));
        PMC_ABORT;
        return eHAL_STATUS_FAILURE;
    }
    
    /* Initialize lists for power save check routines and request full power callback routines. */
    if (csrLLOpen(pMac->hHdd, &pMac->pmc.powerSaveCheckList) != eHAL_STATUS_SUCCESS)
    {
        pmcLog(pMac, LOGE, FL("Cannot initialize power save check routine list"));
        PMC_ABORT;
        return eHAL_STATUS_FAILURE;
    }
    if (csrLLOpen(pMac->hHdd, &pMac->pmc.requestFullPowerList) != eHAL_STATUS_SUCCESS)
    {
        pmcLog(pMac, LOGE, FL("Cannot initialize request full power callback routine list"));
        PMC_ABORT;
        return eHAL_STATUS_FAILURE;
    }

    /* Initialize lists for request BMPS callback routines. */
    if (csrLLOpen(pMac->hHdd, &pMac->pmc.requestBmpsList) !=
      eHAL_STATUS_SUCCESS)
    {
        pmcLog(pMac, LOGE, "PMC: cannot initialize request BMPS callback routine list");
        return eHAL_STATUS_FAILURE;
    }

    /* Initialize lists for request start UAPSD callback routines. */
    if (csrLLOpen(pMac->hHdd, &pMac->pmc.requestStartUapsdList) != eHAL_STATUS_SUCCESS)
    {
        pmcLog(pMac, LOGE, "PMC: cannot initialize request start UAPSD callback routine list");
        return eHAL_STATUS_FAILURE;
    }

    /* Initialize lists for device state update indication callback routines. */
    if (csrLLOpen(pMac->hHdd, &pMac->pmc.deviceStateUpdateIndList) != eHAL_STATUS_SUCCESS)
    {
        pmcLog(pMac, LOGE, "PMC: cannot initialize device state update indication callback list");
        return eHAL_STATUS_FAILURE;
    }

    if (csrLLOpen(pMac->hHdd, &pMac->pmc.deferredMsgList) != eHAL_STATUS_SUCCESS)
    {
        pmcLog(pMac, LOGE, FL("Cannot initialize deferred msg list"));
        PMC_ABORT;
        return eHAL_STATUS_FAILURE;
    }

    return eHAL_STATUS_SUCCESS;
}


/******************************************************************************
*
* Name:  pmcStart
*
* Description:
*    Does a PMC start operation on the device.
*
* Parameters:
*    hHal - HAL handle for device
*
* Returns:
*    eHAL_STATUS_SUCCESS - start successful
*    eHAL_STATUS_FAILURE - start not successful
*
******************************************************************************/
eHalStatus pmcStart (tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    tSirMacHTMIMOPowerSaveState  htMimoPowerSaveState;

    pmcLog(pMac, LOG2, FL("Entering pmcStart"));

    /* Initialize basic PMC information about device. */
    pMac->pmc.pmcState = FULL_POWER;
    pMac->pmc.requestFullPowerPending = FALSE;
    pMac->pmc.uapsdSessionRequired = FALSE;
    pMac->pmc.wowlModeRequired = FALSE;
    pMac->pmc.wowlExitSrc = eWOWL_EXIT_USER;
    pMac->pmc.isAPWOWExit = FALSE;
    pMac->pmc.bmpsRequestedByHdd = FALSE;
    pMac->pmc.remainInPowerActiveTillDHCP = FALSE;
    pMac->pmc.full_power_till_set_key = false;
    pMac->pmc.remainInPowerActiveThreshold = 0;

    /* WLAN Switch initial states. */
    pMac->pmc.hwWlanSwitchState = ePMC_SWITCH_ON;
    pMac->pmc.swWlanSwitchState = ePMC_SWITCH_ON;

    /* No IMPS callback routine yet. */
    pMac->pmc.impsCallbackRoutine = NULL;

    /* No STANDBY callback routine yet. */
    pMac->pmc.standbyCallbackRoutine = NULL;

    /* No WOWL callback routine yet. */
    pMac->pmc.enterWowlCallbackRoutine = NULL;

    /* Initialize BMPS traffic counts. */
    pMac->pmc.cLastTxUnicastFrames = 0;
    pMac->pmc.cLastRxUnicastFrames = 0;
    pMac->pmc.ImpsReqFailed = VOS_FALSE;
    pMac->pmc.ImpsReqFailCnt = 0;
    pMac->pmc.ImpsReqTimerFailed = 0;
    pMac->pmc.ImpsReqTimerfailCnt = 0;

    /* Configure SMPS. */
    if (pMac->pmc.smpsEnabled && (pMac->pmc.powerSource != AC_POWER || pMac->pmc.smpsConfig.enterOnAc))
    {
        if (pMac->pmc.smpsConfig.mode == ePMC_DYNAMIC_SMPS)
            htMimoPowerSaveState = eSIR_HT_MIMO_PS_DYNAMIC;
        if (pMac->pmc.smpsConfig.mode == ePMC_STATIC_SMPS)
            htMimoPowerSaveState = eSIR_HT_MIMO_PS_STATIC;
    }
    else
        htMimoPowerSaveState = eSIR_HT_MIMO_PS_NO_LIMIT;
    
    if (pmcSendMessage(hHal, eWNI_PMC_SMPS_STATE_IND, &htMimoPowerSaveState,
                       sizeof(tSirMacHTMIMOPowerSaveState)) != eHAL_STATUS_SUCCESS)
        return eHAL_STATUS_FAILURE;

#if defined(ANI_LOGDUMP)
    pmcDumpInit(hHal);
#endif

    return eHAL_STATUS_SUCCESS;
}


/******************************************************************************
*
* Name:  pmcStop
*
* Description:
*    Does a PMC stop operation on the device.
*
* Parameters:
*    hHal - HAL handle for device
*
* Returns:
*    eHAL_STATUS_SUCCESS - stop successful
*    eHAL_STATUS_FAILURE - stop not successful
*
******************************************************************************/
eHalStatus pmcStop (tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    tListElem *pEntry;
    tPmcDeferredMsg *pDeferredMsg;

    pmcLog(pMac, LOG2, FL("Entering pmcStop"));

    /* Cancel any running timers. */
    if (vos_timer_stop(&pMac->pmc.hImpsTimer) != VOS_STATUS_SUCCESS)
    {
        pmcLog(pMac, LOGE, FL("Cannot cancel IMPS timer"));
    }

    pmcStopTrafficTimer(hHal);

    if (vos_timer_stop(&pMac->pmc.hExitPowerSaveTimer) != VOS_STATUS_SUCCESS)
    {
        pmcLog(pMac, LOGE, FL("Cannot cancel exit power save mode timer"));
    }

    /* Do all the callbacks. */
    pmcDoCallbacks(hHal, eHAL_STATUS_FAILURE);
    pmcDoBmpsCallbacks(hHal, eHAL_STATUS_FAILURE);
    pMac->pmc.uapsdSessionRequired = FALSE;
    pmcDoStartUapsdCallbacks(hHal, eHAL_STATUS_FAILURE);
    pmcDoStandbyCallbacks(hHal, eHAL_STATUS_FAILURE);

    //purge the deferred msg list
    csrLLLock( &pMac->pmc.deferredMsgList );
    while( NULL != ( pEntry = csrLLRemoveHead( &pMac->pmc.deferredMsgList, eANI_BOOLEAN_FALSE ) ) )
    {
        pDeferredMsg = GET_BASE_ADDR( pEntry, tPmcDeferredMsg, link );
        vos_mem_free(pDeferredMsg);
    }
    csrLLUnlock( &pMac->pmc.deferredMsgList );

    /* PMC is stopped. */
    pMac->pmc.pmcState = STOPPED;
    pMac->pmc.pmcReady = FALSE;

    return eHAL_STATUS_SUCCESS;
}


/******************************************************************************
*
* Name:  pmcClose
*
* Description:
*    Does a PMC close operation on the device.
*
* Parameters:
*    hHal - HAL handle for device
*
* Returns:
*    eHAL_STATUS_SUCCESS - close successful
*    eHAL_STATUS_FAILURE - close not successful
*
******************************************************************************/
eHalStatus pmcClose (tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    pmcLog(pMac, LOG2, FL("Entering pmcClose"));

    /* Free up allocated resources. */
    if (vos_timer_destroy(&pMac->pmc.hImpsTimer) != VOS_STATUS_SUCCESS)
    {
        pmcLog(pMac, LOGE, FL("Cannot deallocate IMPS timer"));
    }
    if (!VOS_IS_STATUS_SUCCESS(vos_timer_destroy(&pMac->pmc.hTrafficTimer)))
    {
        pmcLog(pMac, LOGE, FL("Cannot deallocate traffic timer"));
    }
    if (vos_timer_destroy(&pMac->pmc.hExitPowerSaveTimer) != VOS_STATUS_SUCCESS)
    {
        pmcLog(pMac, LOGE, FL("Cannot deallocate exit power save mode timer"));
    }

    /*
        The following list's entries are dynamically allocated so they need their own 
        cleanup function
    */
    pmcClosePowerSaveCheckList(pMac);
    pmcCloseRequestFullPowerList(pMac);
    pmcCloseRequestBmpsList(pMac);
    pmcCloseRequestStartUapsdList(pMac);
    pmcCloseDeviceStateUpdateList(pMac);
    pmcCloseDeferredMsgList(pMac);

    return eHAL_STATUS_SUCCESS;
}


/******************************************************************************
*
* Name:  pmcSignalPowerEvent
*
* Description:
*    Signals to PMC that a power event has occurred.
*
* Parameters:
*    hHal - HAL handle for device
*    event - the event that has occurred
*
* Returns:
*    eHAL_STATUS_SUCCESS - signaling successful
*    eHAL_STATUS_FAILURE - signaling not successful
*
******************************************************************************/
eHalStatus pmcSignalPowerEvent (tHalHandle hHal, tPmcPowerEvent event)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
#ifndef GEN6_ONWARDS
    tSirMacHTMIMOPowerSaveState  htMimoPowerSaveState;
#endif

    pmcLog(pMac, LOG2, FL("Entering pmcSignalPowerEvent, event %d"), event);

    /* Take action based on the event being signaled. */
    switch (event)
    {
#ifndef GEN6_ONWARDS
    case ePMC_SYSTEM_HIBERNATE:
        return pmcEnterLowPowerState(hHal);

    case ePMC_SYSTEM_RESUME:
        return pmcExitLowPowerState(hHal);

    case ePMC_HW_WLAN_SWITCH_OFF:
        pMac->pmc.hwWlanSwitchState = ePMC_SWITCH_OFF;
        return pmcEnterLowPowerState(hHal);

    case ePMC_HW_WLAN_SWITCH_ON:
        pMac->pmc.hwWlanSwitchState = ePMC_SWITCH_ON;
        return pmcExitLowPowerState(hHal);

    case ePMC_SW_WLAN_SWITCH_OFF:
        pMac->pmc.swWlanSwitchState = ePMC_SWITCH_OFF;
        return pmcEnterLowPowerState(hHal);

    case ePMC_SW_WLAN_SWITCH_ON:
        pMac->pmc.swWlanSwitchState = ePMC_SWITCH_ON;
        return pmcExitLowPowerState(hHal);

    case ePMC_BATTERY_OPERATION:
        pMac->pmc.powerSource = BATTERY_POWER;

        /* Turn on SMPS. */
        if (pMac->pmc.smpsEnabled)
        {
            if (pMac->pmc.smpsConfig.mode == ePMC_DYNAMIC_SMPS)
                htMimoPowerSaveState = eSIR_HT_MIMO_PS_DYNAMIC;
            if (pMac->pmc.smpsConfig.mode == ePMC_STATIC_SMPS)
                htMimoPowerSaveState = eSIR_HT_MIMO_PS_STATIC;
            if (pmcSendMessage(hHal, eWNI_PMC_SMPS_STATE_IND, &htMimoPowerSaveState,
                               sizeof(tSirMacHTMIMOPowerSaveState)) != eHAL_STATUS_SUCCESS)   
                return eHAL_STATUS_FAILURE;
        }
        return eHAL_STATUS_SUCCESS;

    case ePMC_AC_OPERATION:
        pMac->pmc.powerSource = AC_POWER;

        /* Turn off SMPS. */
        if (!pMac->pmc.smpsConfig.enterOnAc)
        {
            htMimoPowerSaveState = eSIR_HT_MIMO_PS_NO_LIMIT;
            if (pmcSendMessage(hHal, eWNI_PMC_SMPS_STATE_IND, &htMimoPowerSaveState,
                               sizeof(tSirMacHTMIMOPowerSaveState)) != eHAL_STATUS_SUCCESS)
                return eHAL_STATUS_FAILURE;
        }
        return eHAL_STATUS_SUCCESS;
#endif //GEN6_ONWARDS
    default:
        pmcLog(pMac, LOGE, FL("Invalid event %d"), event);
        PMC_ABORT;
        return eHAL_STATUS_FAILURE;
    }
}


/******************************************************************************
*
* Name:  pmcSetConfigPowerSave
*
* Description:
*    Configures one of the power saving modes.
*
* Parameters:
*    hHal - HAL handle for device
*    psMode - the power saving mode to configure
*    pConfigParams - pointer to configuration parameters specific to the
*                    power saving mode
*
* Returns:
*    eHAL_STATUS_SUCCESS - configuration successful
*    eHAL_STATUS_FAILURE - configuration not successful
*
******************************************************************************/
eHalStatus pmcSetConfigPowerSave (tHalHandle hHal, tPmcPowerSavingMode psMode, void *pConfigParams)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

#ifdef FEATURE_WLAN_DIAG_SUPPORT
    WLAN_VOS_DIAG_EVENT_DEF(psRequest, vos_event_wlan_powersave_payload_type);
#endif

    pmcLog(pMac, LOG2, FL("Entering pmcSetConfigPowerSave, power save mode %d"), psMode);

    /* Configure the specified power saving mode. */
    switch (psMode)
    {
    
    case ePMC_IDLE_MODE_POWER_SAVE:
        pMac->pmc.impsConfig = *(tpPmcImpsConfigParams)pConfigParams;
        pmcLog(pMac, LOG3, FL("IMPS configuration"));
        pmcLog(pMac, LOG3, "          enter on AC: %d",
               pMac->pmc.impsConfig.enterOnAc);
        break;

    case ePMC_BEACON_MODE_POWER_SAVE:
        pMac->pmc.bmpsConfig = *(tpPmcBmpsConfigParams)pConfigParams;
        pmcLog(pMac, LOG3, FL("BMPS configuration"));
        pmcLog(pMac, LOG3, "          enter on AC: %d",
               pMac->pmc.bmpsConfig.enterOnAc);
        pmcLog(pMac, LOG3, "          TX threshold: %d",
               pMac->pmc.bmpsConfig.txThreshold);
        pmcLog(pMac, LOG3, "          RX threshold: %d",
               pMac->pmc.bmpsConfig.rxThreshold);
        pmcLog(pMac, LOG3, "          traffic measurement period (ms): %d",
               pMac->pmc.bmpsConfig.trafficMeasurePeriod);
        pmcLog(pMac, LOG3, "          BMPS period: %d",
               pMac->pmc.bmpsConfig.bmpsPeriod);
        pmcLog(pMac, LOG3, "          beacons to forward code: %d",
               pMac->pmc.bmpsConfig.forwardBeacons);
        pmcLog(pMac, LOG3, "          value of N: %d",
               pMac->pmc.bmpsConfig.valueOfN);
        pmcLog(pMac, LOG3, "          use PS poll: %d",
               pMac->pmc.bmpsConfig.usePsPoll);
        pmcLog(pMac, LOG3, "          set PM on last frame: %d",
               pMac->pmc.bmpsConfig.setPmOnLastFrame);
        pmcLog(pMac, LOG3, "          value of enableBeaconEarlyTermination: %d",
               pMac->pmc.bmpsConfig.enableBeaconEarlyTermination);
        pmcLog(pMac, LOG3, "          value of bcnEarlyTermWakeInterval: %d",
               pMac->pmc.bmpsConfig.bcnEarlyTermWakeInterval);

#ifdef FEATURE_WLAN_DIAG_SUPPORT    
        vos_mem_zero(&psRequest, sizeof(vos_event_wlan_powersave_payload_type));
        psRequest.event_subtype = WLAN_BMPS_SET_CONFIG;
        /* possible loss of data due to mismatch but expectation is that
        values can reasonably be expected to fit in target widths */
        psRequest.bmps_auto_timer_duration = (v_U16_t)pMac->pmc.bmpsConfig.trafficMeasurePeriod;
        psRequest.bmps_period = (v_U16_t)pMac->pmc.bmpsConfig.bmpsPeriod; 

        WLAN_VOS_DIAG_EVENT_REPORT(&psRequest, EVENT_WLAN_POWERSAVE_GENERIC);
#endif


        break;

    case ePMC_SPATIAL_MULTIPLEX_POWER_SAVE:
        pMac->pmc.smpsConfig = *(tpPmcSmpsConfigParams)pConfigParams;
        pmcLog(pMac, LOG3, FL("SMPS configuration"));
        pmcLog(pMac, LOG3, "          mode: %d", pMac->pmc.smpsConfig.mode);
        pmcLog(pMac, LOG3, "          enter on AC: %d",
               pMac->pmc.smpsConfig.enterOnAc);
        break;

    default:
        pmcLog(pMac, LOGE, FL("Invalid power save mode %d"), psMode);
        PMC_ABORT;
        return eHAL_STATUS_FAILURE;
    }

    //Send the power save config down to PE/HAL/FW if BMPS mode is being configured
    //and pmcReady has been invoked
    if(PMC_IS_READY(pMac) && psMode == ePMC_BEACON_MODE_POWER_SAVE)
    {
       if (pmcSendPowerSaveConfigMessage(hHal) != eHAL_STATUS_SUCCESS)
           return eHAL_STATUS_FAILURE;
    }

    return eHAL_STATUS_SUCCESS;
}

/******************************************************************************
*
* Name:  pmcGetConfigPowerSave
*
* Description:
*    Get the config for the specified power save mode
*
* Parameters:
*    hHal - HAL handle for device
*    psMode - the power saving mode to configure
*    pConfigParams - pointer to configuration parameters specific to the
*                    power saving mode
*
* Returns:
*    eHAL_STATUS_SUCCESS - configuration successful
*    eHAL_STATUS_FAILURE - configuration not successful
*
******************************************************************************/
eHalStatus pmcGetConfigPowerSave (tHalHandle hHal, tPmcPowerSavingMode psMode, void *pConfigParams)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    pmcLog(pMac, LOG2, FL("Entering pmcGetConfigPowerSave, power save mode %d"), psMode);

    /* Configure the specified power saving mode. */
    switch (psMode)
    {
    
    case ePMC_IDLE_MODE_POWER_SAVE:
        *(tpPmcImpsConfigParams)pConfigParams = pMac->pmc.impsConfig;
        break;

    case ePMC_BEACON_MODE_POWER_SAVE:
        *(tpPmcBmpsConfigParams)pConfigParams = pMac->pmc.bmpsConfig;
        break;

    case ePMC_SPATIAL_MULTIPLEX_POWER_SAVE:
        *(tpPmcSmpsConfigParams)pConfigParams = pMac->pmc.smpsConfig;
        break;

    default:
        pmcLog(pMac, LOGE, FL("Invalid power save mode %d"), psMode);
        return eHAL_STATUS_FAILURE;
    }

    return eHAL_STATUS_SUCCESS;
}
/******************************************************************************
*
* Name:  pmcEnablePowerSave
*
* Description:
*    Enables one of the power saving modes.
*
* Parameters:
*    hHal - HAL handle for device
*    psMode - the power saving mode to enable
*
* Returns:
*    eHAL_STATUS_SUCCESS - successfully enabled
*    eHAL_STATUS_FAILURE - not successfully enabled
*
******************************************************************************/
eHalStatus pmcEnablePowerSave (tHalHandle hHal, tPmcPowerSavingMode psMode)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    tSirMacHTMIMOPowerSaveState  htMimoPowerSaveState;

#ifdef FEATURE_WLAN_DIAG_SUPPORT    
    WLAN_VOS_DIAG_EVENT_DEF(psRequest, vos_event_wlan_powersave_payload_type);

    vos_mem_zero(&psRequest, sizeof(vos_event_wlan_powersave_payload_type));
    psRequest.event_subtype = WLAN_PS_MODE_ENABLE_REQ;
    psRequest.enable_disable_powersave_mode = psMode;

    WLAN_VOS_DIAG_EVENT_REPORT(&psRequest, EVENT_WLAN_POWERSAVE_GENERIC);
#endif
    
    pmcLog(pMac, LOG2, FL("Entering pmcEnablePowerSave, power save mode %d"), psMode);

    /* Enable the specified power saving mode. */
    switch (psMode)
    {

    case ePMC_IDLE_MODE_POWER_SAVE:
        pMac->pmc.impsEnabled = TRUE;
        break;

    case ePMC_BEACON_MODE_POWER_SAVE:
        pMac->pmc.bmpsEnabled = TRUE;
        break;

    case ePMC_SPATIAL_MULTIPLEX_POWER_SAVE:
        pMac->pmc.smpsEnabled = TRUE;

        /* If PMC already started, then turn on SMPS. */
        if (pMac->pmc.pmcState != STOPPED)
            if (pMac->pmc.powerSource != AC_POWER ||
                pMac->pmc.smpsConfig.enterOnAc)
            {
                if (pMac->pmc.smpsConfig.mode == ePMC_DYNAMIC_SMPS)
                    htMimoPowerSaveState = eSIR_HT_MIMO_PS_DYNAMIC;
                if (pMac->pmc.smpsConfig.mode == ePMC_STATIC_SMPS)
                    htMimoPowerSaveState = eSIR_HT_MIMO_PS_STATIC;
                if (pmcSendMessage(hHal, eWNI_PMC_SMPS_STATE_IND, &htMimoPowerSaveState,
                                   sizeof(tSirMacHTMIMOPowerSaveState)) != eHAL_STATUS_SUCCESS)
                    return eHAL_STATUS_FAILURE;
            }
        break;

    case ePMC_UAPSD_MODE_POWER_SAVE:
        pMac->pmc.uapsdEnabled = TRUE;
        break;

    case ePMC_STANDBY_MODE_POWER_SAVE:
        pMac->pmc.standbyEnabled = TRUE;
        break;

    case ePMC_WOWL_MODE_POWER_SAVE:
        pMac->pmc.wowlEnabled = TRUE;
        break;

    default:
        pmcLog(pMac, LOGE, FL("Invalid power save mode %d"), psMode);
        PMC_ABORT;
        return eHAL_STATUS_FAILURE;
    }

    return eHAL_STATUS_SUCCESS;
}
/* ---------------------------------------------------------------------------
    \fn pmcStartAutoBmpsTimer
    \brief  Starts a timer that periodically polls all the registered
            module for entry into Bmps mode. This timer is started only if BMPS is
            enabled and whenever the device is in full power.
    \param  hHal - The handle returned by macOpen.
    \return eHalStatus     
  ---------------------------------------------------------------------------*/
eHalStatus pmcStartAutoBmpsTimer (tHalHandle hHal) 
{
   tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

#ifdef FEATURE_WLAN_DIAG_SUPPORT    
   WLAN_VOS_DIAG_EVENT_DEF(psRequest, vos_event_wlan_powersave_payload_type);

   vos_mem_zero(&psRequest, sizeof(vos_event_wlan_powersave_payload_type));
   psRequest.event_subtype = WLAN_START_BMPS_AUTO_TIMER_REQ;

   WLAN_VOS_DIAG_EVENT_REPORT(&psRequest, EVENT_WLAN_POWERSAVE_GENERIC);
#endif

   pmcLog(pMac, LOG2, FL("Entering pmcStartAutoBmpsTimer"));

   /* Check if BMPS is enabled. */
   if (!pMac->pmc.bmpsEnabled)
   {
      pmcLog(pMac, LOGE, "PMC: Cannot enable BMPS timer. BMPS is disabled");
      return eHAL_STATUS_FAILURE;
   }

   pMac->pmc.autoBmpsEntryEnabled = TRUE;

   /* Check if there is an Infra session. If there is no Infra session, timer will be started 
         when STA associates to AP */

   if (pmcShouldBmpsTimerRun(pMac))
   {
      if (pmcStartTrafficTimer(hHal, pMac->pmc.bmpsConfig.trafficMeasurePeriod) != eHAL_STATUS_SUCCESS)
         return eHAL_STATUS_FAILURE;
   }



   return eHAL_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------------
    \fn pmcStopAutoBmpsTimer
    \brief  Stops the Auto BMPS Timer that was started using sme_startAutoBmpsTimer
            Stopping the timer does not cause a device state change. Only the timer
            is stopped. If "Full Power" is desired, use the pmcRequestFullPower API
    \param  hHal - The handle returned by macOpen.
    \return eHalStatus     
  ---------------------------------------------------------------------------*/
eHalStatus pmcStopAutoBmpsTimer (tHalHandle hHal)
{
   tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

#ifdef FEATURE_WLAN_DIAG_SUPPORT    
   WLAN_VOS_DIAG_EVENT_DEF(psRequest, vos_event_wlan_powersave_payload_type);

   vos_mem_zero(&psRequest, sizeof(vos_event_wlan_powersave_payload_type));
   psRequest.event_subtype = WLAN_STOP_BMPS_AUTO_TIMER_REQ;

   WLAN_VOS_DIAG_EVENT_REPORT(&psRequest, EVENT_WLAN_POWERSAVE_GENERIC);
#endif

   pmcLog(pMac, LOG2, FL("Entering pmcStopAutoBmpsTimer"));

   pMac->pmc.autoBmpsEntryEnabled = FALSE;
   /* If uapsd session is not required or HDD has not requested BMPS, stop the auto bmps timer.*/
   if (!pMac->pmc.uapsdSessionRequired && !pMac->pmc.bmpsRequestedByHdd)
      pmcStopTrafficTimer(hHal);

   return eHAL_STATUS_SUCCESS;
}

/******************************************************************************
*
* Name:  pmcDisablePowerSave
*
* Description:
*    Disables one of the power saving modes.
*
* Parameters:
*    hHal - HAL handle for device
*    psMode - the power saving mode to disable
*
* Returns:
*    eHAL_STATUS_SUCCESS - successfully disabled
*    eHAL_STATUS_FAILURE - not successfully disabled
*
******************************************************************************/
eHalStatus pmcDisablePowerSave (tHalHandle hHal, tPmcPowerSavingMode psMode)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    tSirMacHTMIMOPowerSaveState  htMimoPowerSaveState;

#ifdef FEATURE_WLAN_DIAG_SUPPORT    
    WLAN_VOS_DIAG_EVENT_DEF(psRequest, vos_event_wlan_powersave_payload_type);

    vos_mem_zero(&psRequest, sizeof(vos_event_wlan_powersave_payload_type));
    psRequest.event_subtype = WLAN_PS_MODE_DISABLE_REQ;
    psRequest.enable_disable_powersave_mode = psMode;

    WLAN_VOS_DIAG_EVENT_REPORT(&psRequest, EVENT_WLAN_POWERSAVE_GENERIC);
#endif

    pmcLog(pMac, LOG2, FL("Entering pmcDisablePowerSave, power save mode %d"), psMode);

    /* Disable the specified power saving mode. */
    switch (psMode)
    {

    case ePMC_IDLE_MODE_POWER_SAVE:
        pMac->pmc.impsEnabled = FALSE;
        break;

    case ePMC_BEACON_MODE_POWER_SAVE:
        pMac->pmc.bmpsEnabled = FALSE;
        break;

    case ePMC_SPATIAL_MULTIPLEX_POWER_SAVE:
        pMac->pmc.smpsEnabled = FALSE;

        /* Turn off SMPS. */
        htMimoPowerSaveState = eSIR_HT_MIMO_PS_NO_LIMIT;
        if (pmcSendMessage(hHal, eWNI_PMC_SMPS_STATE_IND, &htMimoPowerSaveState,
                           sizeof(tSirMacHTMIMOPowerSaveState)) != eHAL_STATUS_SUCCESS)
            return eHAL_STATUS_FAILURE;
        break;

    case ePMC_UAPSD_MODE_POWER_SAVE:
        pMac->pmc.uapsdEnabled = FALSE;
        break;

    case ePMC_STANDBY_MODE_POWER_SAVE:
        pMac->pmc.standbyEnabled = FALSE;
        break;

    case ePMC_WOWL_MODE_POWER_SAVE:
        pMac->pmc.wowlEnabled = FALSE;
        break;

    default:
        pmcLog(pMac, LOGE, FL("Invalid power save mode %d"), psMode);
        PMC_ABORT;
        return eHAL_STATUS_FAILURE;
    }

    return eHAL_STATUS_SUCCESS;
}


/******************************************************************************
*
* Name:  pmcQueryPowerState
*
* Description:
*    Returns the current power state of the device.
*
* Parameters:
*    hHal - HAL handle for device
*    pPowerState - pointer to location to return power state
*    pHwWlanSwitchState - pointer to location to return Hardware WLAN
*                         Switch state
*    pSwWlanSwitchState - pointer to location to return Software WLAN
*                         Switch state
*
* Returns:
*    eHAL_STATUS_SUCCESS - power state successfully returned
*    eHAL_STATUS_FAILURE - power state not successfully returned
*
******************************************************************************/
eHalStatus pmcQueryPowerState (tHalHandle hHal, tPmcPowerState *pPowerState,
                               tPmcSwitchState *pHwWlanSwitchState, tPmcSwitchState *pSwWlanSwitchState)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    pmcLog(pMac, LOG2, FL("Entering pmcQueryPowerState"));

    /* Return current power state based on PMC state. */
    if(pPowerState != NULL)
    {
        /* Return current power state based on PMC state. */
        switch (pMac->pmc.pmcState)
        {
    
        case FULL_POWER:
            *pPowerState = ePMC_FULL_POWER;
            break;

        default:
            *pPowerState = ePMC_LOW_POWER;
            break;
        }
    }

    /* Return current switch settings. */
    if(pHwWlanSwitchState != NULL)
       *pHwWlanSwitchState = pMac->pmc.hwWlanSwitchState;
    if(pSwWlanSwitchState != NULL)
       *pSwWlanSwitchState = pMac->pmc.swWlanSwitchState;

    return eHAL_STATUS_SUCCESS;
}


/******************************************************************************
*
* Name:  pmcIsPowerSaveEnabled
*
* Description:
*    Checks if the device is able to enter one of the power save modes.
*    "Able to enter" means the power save mode is enabled for the device
*    and the host is using the correct power source for entry into the
*    power save mode.  This routine does not indicate whether the device
*    is actually in the power save mode at a particular point in time.
*
* Parameters:
*    hHal - HAL handle for device
*    psMode - the power saving mode
*
* Returns:
*    TRUE if device is able to enter the power save mode, FALSE otherwise
*
******************************************************************************/
tANI_BOOLEAN pmcIsPowerSaveEnabled (tHalHandle hHal, tPmcPowerSavingMode psMode)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    pmcLog(pMac, LOG2, FL("Entering pmcIsPowerSaveEnabled, power save mode %d"), psMode);

    /* Check ability to enter based on the specified power saving mode. */
    switch (psMode)
    {
    
    case ePMC_IDLE_MODE_POWER_SAVE:
        return pMac->pmc.impsEnabled && (pMac->pmc.powerSource != AC_POWER || pMac->pmc.impsConfig.enterOnAc);

    case ePMC_BEACON_MODE_POWER_SAVE:
        return pMac->pmc.bmpsEnabled;

    case ePMC_SPATIAL_MULTIPLEX_POWER_SAVE:
        return pMac->pmc.smpsEnabled && (pMac->pmc.powerSource != AC_POWER || pMac->pmc.smpsConfig.enterOnAc);

    case ePMC_UAPSD_MODE_POWER_SAVE:
        return pMac->pmc.uapsdEnabled;

    case ePMC_STANDBY_MODE_POWER_SAVE:
        return pMac->pmc.standbyEnabled;

    case ePMC_WOWL_MODE_POWER_SAVE:
        return pMac->pmc.wowlEnabled;
        break;

    default:
        pmcLog(pMac, LOGE, FL("Invalid power save mode %d"), psMode);
        PMC_ABORT;
        return FALSE;
    }
}


/******************************************************************************
*
* Name:  pmcRequestFullPower
*
* Description:
*    Request that the device be brought to full power state.
*
* Parameters:
*    hHal - HAL handle for device
*    callbackRoutine - routine to call when device actually achieves full
*                      power state if "eHAL_STATUS_PMC_PENDING" is returned
*    callbackContext - value to be passed as parameter to routine specified
*                      above
*    fullPowerReason -  Reason for requesting full power mode. This is used
*                       by PE to decide whether data null should be sent to
*                       AP when exiting BMPS mode. Caller should use the
*                       eSME_LINK_DISCONNECTED reason if link is disconnected
*                       and there is no need to tell the AP that we are going
*                       out of power save.
*
* Returns:
*    eHAL_STATUS_SUCCESS - device brought to full power state
*    eHAL_STATUS_FAILURE - device cannot be brought to full power state
*    eHAL_STATUS_PMC_PENDING - device is being brought to full power state,
*                              callbackRoutine will be called when completed
*
******************************************************************************/
eHalStatus pmcRequestFullPower (tHalHandle hHal, void (*callbackRoutine) (void *callbackContext, eHalStatus status),
                                void *callbackContext, tRequestFullPowerReason fullPowerReason)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    tpRequestFullPowerEntry pRequestFullPowerEntry;
    tListElem *pEntry;

#ifdef FEATURE_WLAN_DIAG_SUPPORT    
    WLAN_VOS_DIAG_EVENT_DEF(psRequest, vos_event_wlan_powersave_payload_type);

    vos_mem_zero(&psRequest, sizeof(vos_event_wlan_powersave_payload_type));
    psRequest.event_subtype = WLAN_ENTER_FULL_POWER_REQ;
    psRequest.full_power_request_reason = fullPowerReason;
 
    WLAN_VOS_DIAG_EVENT_REPORT(&psRequest, EVENT_WLAN_POWERSAVE_GENERIC);
#endif

    pmcLog(pMac, LOG2, FL("Entering pmcRequestFullPower"));

    if( !PMC_IS_READY(pMac) )
    {
        pmcLog(pMac, LOGE, FL("Requesting Full Power when PMC not ready"));
        pmcLog(pMac, LOGE, FL("pmcReady = %d pmcState = %s"),
            pMac->pmc.pmcReady, pmcGetPmcStateStr(pMac->pmc.pmcState));
        return eHAL_STATUS_FAILURE;
    }

    /* If HDD is requesting full power, clear any buffered requests for WOWL and BMPS that were
       requested by HDD previously */
    if(SIR_IS_FULL_POWER_NEEDED_BY_HDD(fullPowerReason))
    {
        pMac->pmc.bmpsRequestedByHdd = FALSE;
        pMac->pmc.wowlModeRequired = FALSE;
    }

    /* If already in full power, just return. */
    if (pMac->pmc.pmcState == FULL_POWER)
        return eHAL_STATUS_SUCCESS;

    /* If in IMPS State, then cancel the timer. */
    if (pMac->pmc.pmcState == IMPS)
        if (vos_timer_stop(&pMac->pmc.hImpsTimer) != VOS_STATUS_SUCCESS)
        {
            pmcLog(pMac, LOGE, FL("Cannot cancel IMPS timer"));
        }

    /* If able to enter Request Full Power State, then request is pending.
       Allocate entry for request full power callback routine list. */
    //If caller doesn't need a callback, simply waits up the chip.
    if (callbackRoutine)
    {
        pRequestFullPowerEntry = vos_mem_malloc(sizeof(tRequestFullPowerEntry));
        if (NULL == pRequestFullPowerEntry)
        {
            pmcLog(pMac, LOGE,
                   FL("Cannot allocate memory for request full power routine list entry"));
            PMC_ABORT;
            return eHAL_STATUS_FAILURE;
        }

        /* Store routine and context in entry. */
        pRequestFullPowerEntry->callbackRoutine = callbackRoutine;
        pRequestFullPowerEntry->callbackContext = callbackContext;

        /* Add entry to list. */
        csrLLInsertTail(&pMac->pmc.requestFullPowerList, &pRequestFullPowerEntry->link, TRUE);
    }
    /* Enter Request Full Power State. */
    if (pmcEnterRequestFullPowerState(hHal, fullPowerReason) != eHAL_STATUS_SUCCESS)
    {
        /* If pmcEnterRequestFullPowerState fails ; driver need to remove callback
         * from requestFullPowerList */
        if (callbackRoutine)
        {
            pEntry = csrLLRemoveTail(&pMac->pmc.requestFullPowerList, TRUE);
            pRequestFullPowerEntry = GET_BASE_ADDR(pEntry, tRequestFullPowerEntry, link);
            vos_mem_free(pRequestFullPowerEntry);
        }
        return eHAL_STATUS_FAILURE;
    }

    return eHAL_STATUS_PMC_PENDING;
}


/******************************************************************************
*
* Name:  pmcRequestImps
*
* Description:
*    Request that the device be placed in Idle Mode Power Save (IMPS).
*    The Common Scan/Roam Module makes this request.  The device will be
*    placed into IMPS for the specified amount of time, and then returned
*    to full power.
*
* Parameters:
*    hHal - HAL handle for device
*    impsPeriod - amount of time to remain in IMPS (milliseconds)
*    callbackRoutine - routine to call when IMPS period has finished and
*                      the device has been brought to full power
*    callbackContext - value to be passed as parameter to routine specified
*                      above
*
* Returns:
*    eHAL_STATUS_SUCCESS - device will enter IMPS
*    eHAL_STATUS_PMC_DISABLED - IMPS is disabled
*    eHAL_STATUS_PMC_NOT_NOW - another module is prohibiting entering IMPS
*                              at this time
*    eHAL_STATUS_PMC_AC_POWER - IMPS is disabled when host operating from
*                               AC power
*    eHAL_STATUS_PMC_ALREADY_IN_IMPS - device is already in IMPS
*    eHAL_STATUS_PMC_SYS_ERROR - system error that prohibits entering IMPS
*
******************************************************************************/
eHalStatus pmcRequestImps (tHalHandle hHal, tANI_U32 impsPeriod,
                           void (*callbackRoutine) (void *callbackContext, eHalStatus status),
                           void *callbackContext)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    eHalStatus status;

#ifdef FEATURE_WLAN_DIAG_SUPPORT    
    WLAN_VOS_DIAG_EVENT_DEF(psRequest, vos_event_wlan_powersave_payload_type);

    vos_mem_zero(&psRequest, sizeof(vos_event_wlan_powersave_payload_type));
    psRequest.event_subtype = WLAN_IMPS_ENTER_REQ;
    psRequest.imps_period = impsPeriod;

    WLAN_VOS_DIAG_EVENT_REPORT(&psRequest, EVENT_WLAN_POWERSAVE_GENERIC);
#endif


    pmcLog(pMac, LOG2, FL("Entering pmcRequestImps"));

    status = pmcEnterImpsCheck( pMac );
    if( HAL_STATUS_SUCCESS( status ) )
    {
        /* Enter Request IMPS State. */
        status = pmcEnterRequestImpsState( hHal );
        if (HAL_STATUS_SUCCESS( status ))
    {
            /* Save the period and callback routine for when we need it. */
            pMac->pmc.impsPeriod = impsPeriod;
            pMac->pmc.impsCallbackRoutine = callbackRoutine;
            pMac->pmc.impsCallbackContext = callbackContext;

    }
        else
    {
            status = eHAL_STATUS_PMC_SYS_ERROR;
    }
    }

    return status;
}


/******************************************************************************
*
* Name:  pmcRegisterPowerSaveCheck
*
* Description:
*    Allows a routine to be registered so that the routine is called whenever
*    the device is about to enter one of the power save modes.  This routine
*    will say whether the device is allowed to enter the power save mode at
*    the time of the call.
*
* Parameters:
*    hHal - HAL handle for device
*    checkRoutine - routine to call before entering a power save mode, should
*                   return TRUE if the device is allowed to enter the power
*                   save mode, FALSE otherwise
*    checkContext - value to be passed as parameter to routine specified above
*
* Returns:
*    eHAL_STATUS_SUCCESS - successfully registered
*    eHAL_STATUS_FAILURE - not successfully registered
*
******************************************************************************/
eHalStatus pmcRegisterPowerSaveCheck (tHalHandle hHal, tANI_BOOLEAN (*checkRoutine) (void *checkContext),
                                      void *checkContext)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    tpPowerSaveCheckEntry pEntry;

    pmcLog(pMac, LOG2, FL("Entering pmcRegisterPowerSaveCheck"));

    /* Allocate entry for power save check routine list. */
    pEntry = vos_mem_malloc(sizeof(tPowerSaveCheckEntry));
    if ( NULL == pEntry )
    {
        pmcLog(pMac, LOGE, FL("Cannot allocate memory for power save check routine list entry"));
        PMC_ABORT;
        return eHAL_STATUS_FAILURE;
    }

    /* Store routine and context in entry. */
    pEntry->checkRoutine = checkRoutine;
    pEntry->checkContext = checkContext;

    /* Add entry to list. */
    csrLLInsertTail(&pMac->pmc.powerSaveCheckList, &pEntry->link, FALSE);

    return eHAL_STATUS_SUCCESS;
}


/******************************************************************************
*
* Name:  pmcDeregisterPowerSaveCheck
*
* Description:
*    Reregisters a routine that was previously registered with
*    pmcRegisterPowerSaveCheck.
*
* Parameters:
*    hHal - HAL handle for device
*    checkRoutine - routine to deregister
*
* Returns:
*    eHAL_STATUS_SUCCESS - successfully deregistered
*    eHAL_STATUS_FAILURE - not successfully deregistered
*
******************************************************************************/
eHalStatus pmcDeregisterPowerSaveCheck (tHalHandle hHal, tANI_BOOLEAN (*checkRoutine) (void *checkContext))
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    tListElem *pEntry;
    tpPowerSaveCheckEntry pPowerSaveCheckEntry;

    pmcLog(pMac, LOG2, FL("Entering pmcDeregisterPowerSaveCheck"));

    /* Find entry in the power save check routine list that matches
       the specified routine and remove it. */
    pEntry = csrLLPeekHead(&pMac->pmc.powerSaveCheckList, FALSE);
    while (pEntry != NULL)
    {
        pPowerSaveCheckEntry = GET_BASE_ADDR(pEntry, tPowerSaveCheckEntry, link);
        if (pPowerSaveCheckEntry->checkRoutine == checkRoutine)
        {
            if (csrLLRemoveEntry(&pMac->pmc.powerSaveCheckList, pEntry, FALSE))
            {
                vos_mem_free(pPowerSaveCheckEntry);
            }
            else
            {
                pmcLog(pMac, LOGE, FL("Cannot remove power save check routine list entry"));
                return eHAL_STATUS_FAILURE;
            }
            return eHAL_STATUS_SUCCESS;
        }
        pEntry = csrLLNext(&pMac->pmc.powerSaveCheckList, pEntry, FALSE);
    }

    /* Could not find matching entry. */
    return eHAL_STATUS_FAILURE;
}


static void pmcProcessResponse( tpAniSirGlobal pMac, tSirSmeRsp *pMsg )
{
    tListElem *pEntry = NULL;
    tSmeCmd *pCommand = NULL;
    tANI_BOOLEAN fRemoveCommand = eANI_BOOLEAN_TRUE;
    tCsrRoamSession *pSession = NULL;

    pEntry = csrLLPeekHead(&pMac->sme.smeCmdActiveList, LL_ACCESS_LOCK);
    if(pEntry)
    {
        pCommand = GET_BASE_ADDR(pEntry, tSmeCmd, Link);

        pmcLog(pMac, LOG2, FL("process message = 0x%x"), pMsg->messageType);

    /* Process each different type of message. */
    switch (pMsg->messageType)
    {

    /* We got a response to our IMPS request.  */
    case eWNI_PMC_ENTER_IMPS_RSP:
        pmcLog(pMac, LOG2, FL("Rcvd eWNI_PMC_ENTER_IMPS_RSP with status = %d"), pMsg->statusCode);
            if( (eSmeCommandEnterImps != pCommand->command) && (eSmeCommandEnterStandby != pCommand->command) )
            {
                pmcLog(pMac, LOGW, FL("Rcvd eWNI_PMC_ENTER_IMPS_RSP without request"));
                fRemoveCommand = eANI_BOOLEAN_FALSE;
                break;
            }
        if(pMac->pmc.pmcState == REQUEST_IMPS)
        {
            /* Enter IMPS State if response indicates success. */
            if (pMsg->statusCode == eSIR_SME_SUCCESS)
            {
                pMac->pmc.ImpsReqFailed = VOS_FALSE;
                pmcEnterImpsState(pMac);
                if (!(pMac->pmc.ImpsReqFailed || pMac->pmc.ImpsReqTimerFailed) && pMac->pmc.ImpsReqFailCnt)
                {
                    pmcLog(pMac, LOGE,
                           FL("Response message to request to enter IMPS was failed %d times before success"),
                       pMac->pmc.ImpsReqFailCnt);
                       pMac->pmc.ImpsReqFailCnt = 0;
                }
            }

            /* If response is failure, then we stay in Full Power State and tell everyone that we aren't going into IMPS. */
            else
            {
                pMac->pmc.ImpsReqFailed = VOS_TRUE;
                if (!(pMac->pmc.ImpsReqFailCnt & 0xF))
                {
                    pmcLog(pMac, LOGE,
                           FL("Response message to request to enter IMPS indicates failure, status %x, FailCnt - %d"),
                       pMsg->statusCode, ++pMac->pmc.ImpsReqFailCnt);
                }
                else
                {
                    pMac->pmc.ImpsReqFailCnt++;
                }
                pmcEnterFullPowerState(pMac);
            }
        }
        else if (pMac->pmc.pmcState == REQUEST_STANDBY)
        {
            /* Enter STANDBY State if response indicates success. */
            if (pMsg->statusCode == eSIR_SME_SUCCESS)
            {
                pmcEnterStandbyState(pMac);
                pmcDoStandbyCallbacks(pMac, eHAL_STATUS_SUCCESS);
            }

            /* If response is failure, then we stay in Full Power State
               and tell everyone that we aren't going into STANDBY. */
            else
            {
                pmcLog(pMac, LOGE, "PMC: response message to request to enter "
                       "standby indicates failure, status %d", pMsg->statusCode);
                pmcEnterFullPowerState(pMac);
                pmcDoStandbyCallbacks(pMac, eHAL_STATUS_FAILURE);
            }
        }
        else
        {
            pmcLog(pMac, LOGE, "PMC: Enter IMPS rsp rcvd when device is "
               "in %d state", pMac->pmc.pmcState);
        }
        break;

    /* We got a response to our wake from IMPS request. */
    case eWNI_PMC_EXIT_IMPS_RSP:
            pmcLog(pMac, LOG2, FL("Rcvd eWNI_PMC_EXIT_IMPS_RSP with status = %d"), pMsg->statusCode);
            if( eSmeCommandExitImps != pCommand->command )
            {
                pmcLog(pMac, LOGW, FL("Rcvd eWNI_PMC_EXIT_IMPS_RSP without request"));
                fRemoveCommand = eANI_BOOLEAN_FALSE;
                break;
            }
            /* Check that we are in the correct state for this message. */
            if (pMac->pmc.pmcState != REQUEST_FULL_POWER)
            {
                pmcLog(pMac, LOGE, FL("Got Exit IMPS Response Message while "
                   "in state %d"), pMac->pmc.pmcState);
                break;
            }

            /* Enter Full Power State. */
            if (pMsg->statusCode != eSIR_SME_SUCCESS)
            {
                pmcLog(pMac, LOGE, FL("Response message to request to exit "
                   "IMPS indicates failure, status %d"), pMsg->statusCode);
                if (vos_is_logp_in_progress(VOS_MODULE_ID_SME, NULL))
                {
                    pmcLog(pMac, LOGE, FL("SSR Is in progress do not send "
                                          "exit imps req again"));
                }
                else if( (pMac->pmc.ImpsRspFailCnt <=
                           BMPS_IMPS_FAILURE_REPORT_THRESHOLD))
                {
                    pMac->pmc.ImpsRspFailCnt++;
                    if (eHAL_STATUS_SUCCESS ==
                        pmcSendMessage(pMac, eWNI_PMC_EXIT_IMPS_REQ, NULL, 0) )
                    {
                        fRemoveCommand = eANI_BOOLEAN_FALSE;
                        pMac->pmc.pmcState = REQUEST_FULL_POWER;
                        pmcLog(pMac, LOGE, FL("eWNI_PMC_EXIT_IMPS_REQ sent again"
                                              " to PE"));
                        break;
                    }
                }
                else
                {
                    pMac->pmc.ImpsRspFailCnt = 0;
                    VOS_ASSERT(0);
                    break;
                }
            }
            else
            {
                pMac->pmc.ImpsRspFailCnt = 0;
            }

            pmcEnterFullPowerState(pMac);
        break;

    /* We got a response to our BMPS request.  */
    case eWNI_PMC_ENTER_BMPS_RSP:
            pmcLog(pMac, LOG2, FL("Rcvd eWNI_PMC_ENTER_BMPS_RSP with status = %d"), pMsg->statusCode);
            if( eSmeCommandEnterBmps != pCommand->command )
            {
                pmcLog(pMac, LOGW, FL("Rcvd eWNI_PMC_ENTER_BMPS_RSP without request"));
                fRemoveCommand = eANI_BOOLEAN_FALSE;
                break;
            }
            pMac->pmc.bmpsRequestQueued = eANI_BOOLEAN_FALSE;
            /* Check that we are in the correct state for this message. */
            if (pMac->pmc.pmcState != REQUEST_BMPS)
            {
                pmcLog(pMac, LOGE,
                       FL("Got Enter BMPS Response Message while in state %d"), pMac->pmc.pmcState);
                break;
            }

        /* Enter BMPS State if response indicates success. */
        if (pMsg->statusCode == eSIR_SME_SUCCESS)
        {
                pmcEnterBmpsState(pMac);
            /* Note: If BMPS was requested because of start UAPSD,
               there will no entries for BMPS callback routines and
               pmcDoBmpsCallbacks will be a No-Op*/
                pmcDoBmpsCallbacks(pMac, eHAL_STATUS_SUCCESS);
         }
        /* If response is failure, then we stay in Full Power State and tell everyone that we aren't going into BMPS. */
        else
        {
                pmcLog(pMac, LOGE,
                       FL("Response message to request to enter BMPS indicates failure, status %d"),
                   pMsg->statusCode);
                pmcEnterFullPowerState(pMac);
                //Do not call UAPSD callback here since it may be re-entered
                pmcDoBmpsCallbacks(pMac, eHAL_STATUS_FAILURE);
        }
        break;

    /* We got a response to our wake from BMPS request. */
    case eWNI_PMC_EXIT_BMPS_RSP:
            pmcLog(pMac, LOG2, FL("Rcvd eWNI_PMC_EXIT_BMPS_RSP with status = %d"), pMsg->statusCode);
            if( eSmeCommandExitBmps != pCommand->command )
            {
                pmcLog(pMac, LOGW, FL("Rcvd eWNI_PMC_EXIT_BMPS_RSP without request"));
                fRemoveCommand = eANI_BOOLEAN_FALSE;
                break;
            }
            /* Check that we are in the correct state for this message. */
            if (pMac->pmc.pmcState != REQUEST_FULL_POWER)
            {
                pmcLog(pMac, LOGE,
                       FL("Got Exit BMPS Response Message while in state %d"), pMac->pmc.pmcState);
                break;
            }

            /* Enter Full Power State. */
            if (pMsg->statusCode != eSIR_SME_SUCCESS)
            {
                pmcLog(pMac, LOGP,
                       FL("Response message to request to exit BMPS indicates failure, status %d"),
                       pMsg->statusCode);
                /*Status is not succes, so set back the pmc state as BMPS*/
                pMac->pmc.pmcState = BMPS;
            }
            else
                pmcEnterFullPowerState(pMac);
        break;

        /* We got a response to our Start UAPSD request.  */
        case eWNI_PMC_ENTER_UAPSD_RSP:
            pmcLog(pMac, LOG2, FL("Rcvd eWNI_PMC_ENTER_UAPSD_RSP with status = %d"), pMsg->statusCode);
            if( eSmeCommandEnterUapsd != pCommand->command )
            {
                pmcLog(pMac, LOGW, FL("Rcvd eWNI_PMC_ENTER_UAPSD_RSP without request"));
                fRemoveCommand = eANI_BOOLEAN_FALSE;
                break;
            }
            /* Check that we are in the correct state for this message. */
            if (pMac->pmc.pmcState != REQUEST_START_UAPSD)
            {
                pmcLog(pMac, LOGE,
                       FL("Got Enter Uapsd rsp Message while in state %d"), pMac->pmc.pmcState);
                break;
            }

            /* Enter UAPSD State if response indicates success. */
            if (pMsg->statusCode == eSIR_SME_SUCCESS) 
            {
                pmcEnterUapsdState(pMac);
                pmcDoStartUapsdCallbacks(pMac, eHAL_STATUS_SUCCESS);
            }
            else
            {
                /* If response is failure, then we try to put the chip back in
                   BMPS mode*/
                tANI_BOOLEAN OrigUapsdReqState = pMac->pmc.uapsdSessionRequired;
                pmcLog(pMac, LOGE, "PMC: response message to request to enter "
                   "UAPSD indicates failure, status %d", pMsg->statusCode);

                //Need to reset the UAPSD flag so pmcEnterBmpsState won't try to enter UAPSD.
                pMac->pmc.uapsdSessionRequired = FALSE;
                pmcEnterBmpsState(pMac);

                if (pMsg->statusCode != eSIR_SME_UAPSD_REQ_INVALID)
                {
                    //UAPSD will not be retied in this case so tell requester we are done with failure
                    pmcDoStartUapsdCallbacks(pMac, eHAL_STATUS_FAILURE);
                }
                else
                    pMac->pmc.uapsdSessionRequired = OrigUapsdReqState;
            }
         break;

      /* We got a response to our Stop UAPSD request.  */
      case eWNI_PMC_EXIT_UAPSD_RSP:
         pmcLog(pMac, LOG2, FL("Rcvd eWNI_PMC_EXIT_UAPSD_RSP with status = %d"), pMsg->statusCode);
            if( eSmeCommandExitUapsd != pCommand->command )
            {
                pmcLog(pMac, LOGW, FL("Rcvd eWNI_PMC_EXIT_UAPSD_RSP without request"));
                fRemoveCommand = eANI_BOOLEAN_FALSE;
                break;
            }
            /* Check that we are in the correct state for this message. */
            if (pMac->pmc.pmcState != REQUEST_STOP_UAPSD)
            {
                pmcLog(pMac, LOGE,
                       FL("Got Exit Uapsd rsp Message while in state %d"), pMac->pmc.pmcState);
                break;
            }

         /* Enter BMPS State */
         if (pMsg->statusCode != eSIR_SME_SUCCESS) {
            pmcLog(pMac, LOGP, "PMC: response message to request to exit "
               "UAPSD indicates failure, status %d", pMsg->statusCode);
         }
            pmcEnterBmpsState(pMac);
         break;

      /* We got a response to our enter WOWL request.  */
      case eWNI_PMC_ENTER_WOWL_RSP:

            if( eSmeCommandEnterWowl != pCommand->command )
            {
                pmcLog(pMac, LOGW, FL("Rcvd eWNI_PMC_ENTER_WOWL_RSP without request"));
                fRemoveCommand = eANI_BOOLEAN_FALSE;
                break;
            }
            /* Check that we are in the correct state for this message. */
            if (pMac->pmc.pmcState != REQUEST_ENTER_WOWL)
            {
                pmcLog(pMac, LOGE, FL("Got eWNI_PMC_ENTER_WOWL_RSP while in state %s"),
                    pmcGetPmcStateStr(pMac->pmc.pmcState));
                break;
            }

         /* Enter WOWL State if response indicates success. */
         if (pMsg->statusCode == eSIR_SME_SUCCESS) {
                pmcEnterWowlState(pMac);
                pmcDoEnterWowlCallbacks(pMac, eHAL_STATUS_SUCCESS);
         }

         /* If response is failure, then we try to put the chip back in
            BMPS mode*/
         else {
            pmcLog(pMac, LOGE, "PMC: response message to request to enter "
               "WOWL indicates failure, status %d", pMsg->statusCode);
                pmcEnterBmpsState(pMac);
                pmcDoEnterWowlCallbacks(pMac, eHAL_STATUS_FAILURE);
         }
         break;

      /* We got a response to our exit WOWL request.  */
      case eWNI_PMC_EXIT_WOWL_RSP:

            if( eSmeCommandExitWowl != pCommand->command )
            {
                pmcLog(pMac, LOGW, FL("Rcvd eWNI_PMC_EXIT_WOWL_RSP without request"));
                fRemoveCommand = eANI_BOOLEAN_FALSE;
                break;
            }
            /* Check that we are in the correct state for this message. */
            if (pMac->pmc.pmcState != REQUEST_EXIT_WOWL)
            {
                pmcLog(pMac, LOGE, FL("Got Exit WOWL rsp Message while in state %d"), pMac->pmc.pmcState);
                break;
            }

            /* Enter BMPS State */
            if (pMsg->statusCode != eSIR_SME_SUCCESS) {
                pmcLog(pMac, LOGP, "PMC: response message to request to exit "
                       "WOWL indicates failure, status %d", pMsg->statusCode);
            }

            pSession = CSR_GET_SESSION(pMac, pMsg->sessionId);
            if (pSession && pSession->pCurRoamProfile &&
                CSR_IS_INFRA_AP(pSession->pCurRoamProfile))
            {
                pMac->pmc.pmcState = FULL_POWER;
                pMac->pmc.isAPWOWExit = TRUE;
                pMac->pmc.requestFullPowerPending = false;
                break;
            }
            else
            {
                pMac->pmc.isAPWOWExit = FALSE;
            }

            pmcEnterBmpsState(pMac);
         break;

    default:
        pmcLog(pMac, LOGE, FL("Invalid message type %d received"), pMsg->messageType);
        PMC_ABORT;
        break;
        }//switch

        if( fRemoveCommand )
        {
            if( csrLLRemoveEntry( &pMac->sme.smeCmdActiveList, pEntry, LL_ACCESS_LOCK ) )
            {
                pmcReleaseCommand( pMac, pCommand );
                smeProcessPendingQueue( pMac );
            }
        }
    }
    else
    {
        pmcLog(pMac, LOGE, FL("message type %d received but no request is found"), pMsg->messageType);
    }
}


/******************************************************************************
*
* Name:  pmcMessageProcessor
*
* Description:
*    Process a message received by PMC.
*
* Parameters:
*    hHal - HAL handle for device
*    pMsg - pointer to received message
*
* Returns:
*    nothing
*
******************************************************************************/
void pmcMessageProcessor (tHalHandle hHal, tSirSmeRsp *pMsg)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    pmcLog(pMac, LOG2, FL("Message type %d"), pMsg->messageType);

    switch( pMsg->messageType )
    {
    case eWNI_PMC_EXIT_BMPS_IND:
    //When PMC needs to handle more indication from PE, they need to be added here.
    {
        /* Device left BMPS on its own. */
        pmcLog(pMac, LOGW, FL("Rcvd eWNI_PMC_EXIT_BMPS_IND with status = %d"), pMsg->statusCode);
        /* Check that we are in the correct state for this message. */
        switch(pMac->pmc.pmcState)
        {
        case BMPS:
        case REQUEST_START_UAPSD:
        case UAPSD:
        case REQUEST_STOP_UAPSD:
        case REQUEST_ENTER_WOWL:
        case WOWL:
        case REQUEST_EXIT_WOWL:
        case REQUEST_FULL_POWER:
            pmcLog(pMac, LOGW, FL("Got eWNI_PMC_EXIT_BMPS_IND while in state %d"), pMac->pmc.pmcState);
            break;
        default:
            pmcLog(pMac, LOGE, FL("Got eWNI_PMC_EXIT_BMPS_IND while in state %d"), pMac->pmc.pmcState);
            PMC_ABORT;
            break;
        }

        /* Enter Full Power State. */
        if (pMsg->statusCode != eSIR_SME_SUCCESS)
        {
            pmcLog(pMac, LOGP, FL("Exit BMPS indication indicates failure, status %d"), pMsg->statusCode);
        }
        else
        {
            tpSirSmeExitBmpsInd pExitBmpsInd = (tpSirSmeExitBmpsInd)pMsg;
            pmcEnterRequestFullPowerState(hHal, pExitBmpsInd->exitBmpsReason);
        }
        break;
    }

    default:
        pmcProcessResponse( pMac, pMsg );
        break;
    }

}


tANI_BOOLEAN pmcValidateConnectState( tHalHandle hHal )
{
   tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

   if ( !csrIsInfraConnected( pMac ) )
   {
      pmcLog(pMac, LOGW, "PMC: STA not associated. BMPS cannot be entered");
      return eANI_BOOLEAN_FALSE;
   }

   //Cannot have other session
   if ( csrIsIBSSStarted( pMac ) )
   {
      pmcLog(pMac, LOGW, "PMC: IBSS started. BMPS cannot be entered");
      return eANI_BOOLEAN_FALSE;
   }
   if ( csrIsBTAMPStarted( pMac ) )
   {
      pmcLog(pMac, LOGW, "PMC: BT-AMP exists. BMPS cannot be entered");
      return eANI_BOOLEAN_FALSE;
   }
   if ((vos_concurrent_open_sessions_running()) &&
       (csrIsConcurrentInfraConnected( pMac ) ||
       (vos_get_concurrency_mode()& VOS_SAP) ||
       (vos_get_concurrency_mode()& VOS_P2P_GO)))
   {
      pmcLog(pMac, LOGW, "PMC: Multiple active sessions exists. BMPS cannot be entered");
      return eANI_BOOLEAN_FALSE;
   }
#ifdef FEATURE_WLAN_TDLS
   if (pMac->isTdlsPowerSaveProhibited)
   {
      pmcLog(pMac, LOGE, FL("TDLS peer(s) connected/discovery sent. Dont enter BMPS"));
      return eANI_BOOLEAN_FALSE;
   }
#endif
   return eANI_BOOLEAN_TRUE;
}

tANI_BOOLEAN pmcAllowImps( tHalHandle hHal )
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    //Cannot have other session like IBSS or BT AMP running
    if ( csrIsIBSSStarted( pMac ) )
    {
       pmcLog(pMac, LOGW, "PMC: IBSS started. IMPS cannot be entered");
       return eANI_BOOLEAN_FALSE;
    }
    if ( csrIsBTAMPStarted( pMac ) )
    {
       pmcLog(pMac, LOGW, "PMC: BT-AMP exists. IMPS cannot be entered");
       return eANI_BOOLEAN_FALSE;
    }

    //All sessions must be disconnected to allow IMPS
    if ( !csrIsAllSessionDisconnected( pMac ) )
    {
       pmcLog(pMac, LOGW, "PMC: Atleast one connected session. IMPS cannot be entered");
       return eANI_BOOLEAN_FALSE;
    }

    return eANI_BOOLEAN_TRUE;
}

/******************************************************************************
*
* Name:  pmcRequestBmps
*
* Description:
*    Request that the device be put in BMPS state.
*
* Parameters:
*    hHal - HAL handle for device
*    callbackRoutine - Callback routine invoked in case of success/failure
*    callbackContext - value to be passed as parameter to routine specified
*                      above
*
* Returns:
*    eHAL_STATUS_SUCCESS - device is in BMPS state
*    eHAL_STATUS_FAILURE - device cannot be brought to BMPS state
*    eHAL_STATUS_PMC_PENDING - device is being brought to BMPS state,
*
******************************************************************************/
eHalStatus pmcRequestBmps (
    tHalHandle hHal,
    void (*callbackRoutine) (void *callbackContext, eHalStatus status),
    void *callbackContext)
{
   tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
   tpRequestBmpsEntry pEntry;
   eHalStatus status;

#ifdef FEATURE_WLAN_DIAG_SUPPORT    
   WLAN_VOS_DIAG_EVENT_DEF(psRequest, vos_event_wlan_powersave_payload_type);

   vos_mem_zero(&psRequest, sizeof(vos_event_wlan_powersave_payload_type));
   psRequest.event_subtype = WLAN_BMPS_ENTER_REQ;

   WLAN_VOS_DIAG_EVENT_REPORT(&psRequest, EVENT_WLAN_POWERSAVE_GENERIC);
#endif

   pmcLog(pMac, LOG2, "PMC: entering pmcRequestBmps");

   /* If already in BMPS, just return. */
   if (pMac->pmc.pmcState == BMPS || REQUEST_START_UAPSD == pMac->pmc.pmcState || UAPSD == pMac->pmc.pmcState)
   {
      pmcLog(pMac, LOG2, "PMC: Device already in BMPS pmcState %d", pMac->pmc.pmcState);
      pMac->pmc.bmpsRequestedByHdd = TRUE;
      return eHAL_STATUS_SUCCESS;
   }
   
   status = pmcEnterBmpsCheck( pMac );
   if(HAL_STATUS_SUCCESS( status ))
   {
      /* If DUT exits from WoWL because of wake-up indication then it enters
       * into WoWL again. Disable WoWL only when user explicitly disables.
       */
      if(pMac->pmc.wowlModeRequired == FALSE &&
         pMac->pmc.wowlExitSrc == eWOWL_EXIT_WAKEIND &&
         !pMac->pmc.isAPWOWExit)
      {
          pMac->pmc.wowlModeRequired = TRUE;
      }

      status = pmcEnterRequestBmpsState(hHal);

      /* Enter Request BMPS State. */
      if ( HAL_STATUS_SUCCESS( status ) )
      {
         /* Remember that HDD requested BMPS. This flag will be used to put the
            device back into BMPS if any module other than HDD (e.g. CSR, QoS, or BAP)
            requests full power for any reason */
         pMac->pmc.bmpsRequestedByHdd = TRUE;

         /* If able to enter Request BMPS State, then request is pending.
            Allocate entry for request BMPS callback routine list. */
         pEntry = vos_mem_malloc(sizeof(tRequestBmpsEntry));
         if ( NULL == pEntry )
         {
            pmcLog(pMac, LOGE, "PMC: cannot allocate memory for request "
                  "BMPS routine list entry");
            return eHAL_STATUS_FAILURE;
         }

         /* Store routine and context in entry. */
         pEntry->callbackRoutine = callbackRoutine;
         pEntry->callbackContext = callbackContext;

         /* Add entry to list. */
         csrLLInsertTail(&pMac->pmc.requestBmpsList, &pEntry->link, FALSE);

         status = eHAL_STATUS_PMC_PENDING;
      }
      else
      {
         status = eHAL_STATUS_FAILURE;
      }
   }
   /* Retry to enter the BMPS if the
      status = eHAL_STATUS_PMC_NOT_NOW */
   else if (status == eHAL_STATUS_PMC_NOT_NOW)
   {
      pmcStopTrafficTimer(hHal);
      pmcLog(pMac, LOG1, FL("Can't enter BMPS+++"));
      if (pmcShouldBmpsTimerRun(pMac))
      {
         if (pmcStartTrafficTimer(pMac,
                                  pMac->pmc.bmpsConfig.trafficMeasurePeriod)
                                  != eHAL_STATUS_SUCCESS)
         {
            pmcLog(pMac, LOG1, FL("Cannot start BMPS Retry timer"));
         }
         pmcLog(pMac, LOG1,
                FL("BMPS Retry Timer already running or started"));
      }
   }

   return status;
}

/******************************************************************************
*
* Name:  pmcSta