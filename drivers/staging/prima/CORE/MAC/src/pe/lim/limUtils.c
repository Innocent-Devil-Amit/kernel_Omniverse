/*
 * Copyright (c) 2011-2017 The Linux Foundation. All rights reserved.
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


/*
 * This file limUtils.cc contains the utility functions
 * LIM uses.
 * Author:        Chandra Modumudi
 * Date:          02/13/02
 * History:-
 * Date           Modified by    Modification Information
 * --------------------------------------------------------------------
 */

#include "schApi.h"
#include "limUtils.h"
#include "limTypes.h"
#include "limSecurityUtils.h"
#include "limPropExtsUtils.h"
#include "limSendMessages.h"
#include "limSerDesUtils.h"
#include "limAdmitControl.h"
#include "limStaHashApi.h"
#include "dot11f.h"
#include "dot11fdefs.h"
#include "wmmApsd.h"
#include "limTrace.h"
#ifdef FEATURE_WLAN_DIAG_SUPPORT
#include "vos_diag_core_event.h"
#endif //FEATURE_WLAN_DIAG_SUPPORT
#include "limIbssPeerMgmt.h"
#include "limSessionUtils.h"
#include "limSession.h"
#include "vos_nvitem.h"
#ifdef WLAN_FEATURE_11W
#include "wniCfg.h"
#endif
#ifdef SAP_AUTH_OFFLOAD
#include "limAssocUtils.h"
#endif

/* Static global used to mark situations where pMac->lim.gLimTriggerBackgroundScanDuringQuietBss is SET
 * and limTriggerBackgroundScanDuringQuietBss() returned failure.  In this case, we will stop data
 * traffic instead of going into scan.  The recover function limProcessQuietBssTimeout() needs to have
 * this information. */
static tAniBool glimTriggerBackgroundScanDuringQuietBss_Status = eSIR_TRUE;

/* 11A Channel list to decode RX BD channel information */
static const tANI_U8 abChannel[]= {36,40,44,48,52,56,60,64,100,104,108,112,116,
            120,124,128,132,136,140,149,153,157,161,165,144};
#define abChannelSize (sizeof(abChannel)/  \
        sizeof(abChannel[0]))

#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
static const tANI_U8 aUnsortedChannelList[]= {52,56,60,64,100,104,108,112,116,
            120,124,128,132,136,140,36,40,44,48,149,153,157,161,165,144};
#define aUnsortedChannelListSize (sizeof(aUnsortedChannelList)/  \
        sizeof(aUnsortedChannelList[0]))
#endif

//#define LIM_MAX_ACTIVE_SESSIONS 3  //defined temporarily for BT-AMP SUPPORT
#define SUCCESS 1                   //defined temporarily for BT-AMP

#define MAX_BA_WINDOW_SIZE_FOR_CISCO 25
#define MAX_DTIM_PERIOD 15
#define MAX_DTIM_COUNT  15
#define DTIM_PERIOD_DEFAULT 1
#define DTIM_COUNT_DEFAULT  1
static void
limProcessChannelSwitchSuspendLink(tpAniSirGlobal pMac,
                                    eHalStatus status,
                                    tANI_U32 *ctx);
/** -------------------------------------------------------------
\fn limCheck11BRateBitmap
\brief Verifies if basic rates are set.
\param     tANI_U16 pRateBitmap
\return tANI_BOOLEAN - true or false
  -------------------------------------------------------------*/

tANI_BOOLEAN limCheck11BRateBitmap(tANI_U16 pRateBitmap)
{
        return ( ( pRateBitmap & ( 1 << 0 ) ) || ( pRateBitmap & ( 1 << 1 ) ) ||
                        ( pRateBitmap & ( 1 << 2 ) ) ||
                               ( pRateBitmap & ( 1 << 3 ) ) ? 1 : 0 ) ;
}

/** -------------------------------------------------------------
\fn limAssignDialogueToken
\brief Assigns dialogue token.
\param     tpAniSirGlobal    pMac
\return tpDialogueToken - dialogueToken data structure.
  -------------------------------------------------------------*/

tpDialogueToken
limAssignDialogueToken(tpAniSirGlobal pMac)
{
    static tANI_U8 token;
    tpDialogueToken pCurrNode;
    pCurrNode = vos_mem_malloc(sizeof(tDialogueToken));
    if ( NULL == pCurrNode )
    {
        PELOGE(limLog(pMac, LOGE, FL("AllocateMemory failed"));)
        return NULL;
    }

    vos_mem_set((void *) pCurrNode, sizeof(tDialogueToken), 0);
    //first node in the list is being added.
    if(NULL == pMac->lim.pDialogueTokenHead)
    {
        pMac->lim.pDialogueTokenHead = pMac->lim.pDialogueTokenTail = pCurrNode;
    }
    else
    {
        pMac->lim.pDialogueTokenTail->next = pCurrNode;
        pMac->lim.pDialogueTokenTail = pCurrNode;
    }
    //assocId and tid of the node will be filled in by caller.
    pCurrNode->next = NULL;
    pCurrNode->token = token++;

    /* Dialog token should be a non-zero value */
    if (0 == pCurrNode->token)
       pCurrNode->token = token;

    PELOG4(limLog(pMac, LOG4, FL("token assigned = %d"), pCurrNode->token);)
    return pCurrNode;
}

/** -------------------------------------------------------------
\fn limSearchAndDeleteDialogueToken
\brief search dialogue token in the list and deletes it if found. returns failure if not found.
\param     tpAniSirGlobal    pMac
\param     tANI_U8 token
\param     tANI_U16 assocId
\param     tANI_U16 tid
\return eSirRetStatus - status of the search
  -------------------------------------------------------------*/


tSirRetStatus
limSearchAndDeleteDialogueToken(tpAniSirGlobal pMac, tANI_U8 token, tANI_U16 assocId, tANI_U16 tid)
{
    tpDialogueToken pCurrNode = pMac->lim.pDialogueTokenHead;
    tpDialogueToken pPrevNode = pMac->lim.pDialogueTokenHead;

    /* if the list is empty */
    if(NULL == pCurrNode)
      return eSIR_FAILURE;

    /* If the matching node is the first node.*/
    if ((token == pCurrNode->token) &&
        (assocId == pCurrNode->assocId) &&
        (tid == pCurrNode->tid)) {
        pMac->lim.pDialogueTokenHead = pCurrNode->next;
        /* There was only one node in the list.
         * So tail pointer also needs to be adjusted.
         */
        if (NULL == pMac->lim.pDialogueTokenHead)
            pMac->lim.pDialogueTokenTail = NULL;
        vos_mem_free(pCurrNode);
        return eSIR_SUCCESS;
    }

    /* first node did not match. so move to the next one. */
    pCurrNode = pCurrNode->next;

    while (NULL != pCurrNode) {
         if ((token == pCurrNode->token) &&
           (assocId == pCurrNode->assocId) &&
           (tid == pCurrNode->tid)) {
           break;
        }
        pPrevNode = pCurrNode;
        pCurrNode = pCurrNode->next;
    }

    if (pCurrNode) {
        pPrevNode->next = pCurrNode->next;
        /* if the node being deleted is the last one
         * then we also need to move the tail pointer
         * to the prevNode.
         */
        if(NULL == pCurrNode->next)
              pMac->lim.pDialogueTokenTail = pPrevNode;
        vos_mem_free(pCurrNode);
        return eSIR_SUCCESS;
    }

    limLog(pMac, LOGW,
       FL("LIM does not have matching dialogue token node"));
    return eSIR_FAILURE;
}


/** -------------------------------------------------------------
\fn limDeleteDialogueTokenList
\brief deletes the complete lim dialogue token linked list.
\param     tpAniSirGlobal    pMac
\return     None
  -------------------------------------------------------------*/
void
limDeleteDialogueTokenList(tpAniSirGlobal pMac)
{
    tpDialogueToken pCurrNode = pMac->lim.pDialogueTokenHead;

    while(NULL != pMac->lim.pDialogueTokenHead)
    {
        pCurrNode = pMac->lim.pDialogueTokenHead;    
        pMac->lim.pDialogueTokenHead = pMac->lim.pDialogueTokenHead->next;
        vos_mem_free(pCurrNode);
        pCurrNode = NULL;
    }
    pMac->lim.pDialogueTokenTail = NULL;
}

void
limGetBssidFromBD(tpAniSirGlobal pMac, tANI_U8 * pRxPacketInfo, tANI_U8 *bssId, tANI_U32 *pIgnore)
{
    tpSirMacDataHdr3a pMh = WDA_GET_RX_MPDUHEADER3A(pRxPacketInfo);
    *pIgnore = 0;

    if (pMh->fc.toDS == 1 && pMh->fc.fromDS == 0)
    {
        vos_mem_copy( bssId, pMh->addr1, 6);
        *pIgnore = 1;
    }
    else if (pMh->fc.toDS == 0 && pMh->fc.fromDS == 1)
    {
        vos_mem_copy ( bssId, pMh->addr2, 6);
        *pIgnore = 1;
    }
    else if (pMh->fc.toDS == 0 && pMh->fc.fromDS == 0)
    {
        vos_mem_copy( bssId, pMh->addr3, 6);
        *pIgnore = 0;
    }
    else
    {
        vos_mem_copy( bssId, pMh->addr1, 6);
        *pIgnore = 1;
    }
}

char *
limMlmStateStr(tLimMlmStates state)
{
    switch (state)
    {
        case eLIM_MLM_OFFLINE_STATE:
            return "eLIM_MLM_OFFLINE_STATE";
        case eLIM_MLM_IDLE_STATE:
            return "eLIM_MLM_IDLE_STATE";
        case eLIM_MLM_WT_PROBE_RESP_STATE:
            return "eLIM_MLM_WT_PROBE_RESP_STATE";
        case eLIM_MLM_PASSIVE_SCAN_STATE:
            return "eLIM_MLM_PASSIVE_SCAN_STATE";
        case eLIM_MLM_WT_JOIN_BEACON_STATE:
            return "eLIM_MLM_WT_JOIN_BEACON_STATE";
        case eLIM_MLM_JOINED_STATE:
            return "eLIM_MLM_JOINED_STATE";
        case eLIM_MLM_BSS_STARTED_STATE:
            return "eLIM_MLM_BSS_STARTED_STATE";
        case eLIM_MLM_WT_AUTH_FRAME2_STATE:
            return "eLIM_MLM_WT_AUTH_FRAME2_STATE";
        case eLIM_MLM_WT_AUTH_FRAME3_STATE:
            return "eLIM_MLM_WT_AUTH_FRAME3_STATE";
        case eLIM_MLM_WT_AUTH_FRAME4_STATE:
            return "eLIM_MLM_WT_AUTH_FRAME4_STATE";
        case eLIM_MLM_AUTH_RSP_TIMEOUT_STATE:
            return "eLIM_MLM_AUTH_RSP_TIMEOUT_STATE";
        case eLIM_MLM_AUTHENTICATED_STATE:
            return "eLIM_MLM_AUTHENTICATED_STATE";
        case eLIM_MLM_WT_ASSOC_RSP_STATE:
            return "eLIM_MLM_WT_ASSOC_RSP_STATE";
        case eLIM_MLM_WT_REASSOC_RSP_STATE:
            return "eLIM_MLM_WT_REASSOC_RSP_STATE";
        case eLIM_MLM_WT_FT_REASSOC_RSP_STATE:
            return "eLIM_MLM_WT_FT_REASSOC_RSP_STATE";
        case eLIM_MLM_WT_DEL_STA_RSP_STATE:
            return "eLIM_MLM_WT_DEL_STA_RSP_STATE";
        case eLIM_MLM_WT_DEL_BSS_RSP_STATE:
            return "eLIM_MLM_WT_DEL_BSS_RSP_STATE";
        case eLIM_MLM_WT_ADD_STA_RSP_STATE:
            return "eLIM_MLM_WT_ADD_STA_RSP_STATE";
        case eLIM_MLM_WT_ADD_BSS_RSP_STATE:
            return "eLIM_MLM_WT_ADD_BSS_RSP_STATE";
        case eLIM_MLM_REASSOCIATED_STATE:
            return "eLIM_MLM_REASSOCIATED_STATE";
        case eLIM_MLM_LINK_ESTABLISHED_STATE:
            return "eLIM_MLM_LINK_ESTABLISHED_STATE";
        case eLIM_MLM_WT_ASSOC_CNF_STATE:
            return "eLIM_MLM_WT_ASSOC_CNF_STATE";
        case eLIM_MLM_WT_ADD_BSS_RSP_ASSOC_STATE:
            return "eLIM_MLM_WT_ADD_BSS_RSP_ASSOC_STATE";
        case eLIM_MLM_WT_ADD_BSS_RSP_REASSOC_STATE:
            return "eLIM_MLM_WT_ADD_BSS_RSP_REASSOC_STATE";
        case eLIM_MLM_WT_ADD_BSS_RSP_FT_REASSOC_STATE:
            return "eLIM_MLM_WT_ADD_BSS_RSP_FT_REASSOC_STATE";
        case eLIM_MLM_WT_ASSOC_DEL_STA_RSP_STATE:
            return "eLIM_MLM_WT_ASSOC_DEL_STA_RSP_STATE";
        case eLIM_MLM_WT_SET_BSS_KEY_STATE:
            return "eLIM_MLM_WT_SET_BSS_KEY_STATE";
        case eLIM_MLM_WT_SET_STA_KEY_STATE:
            return "eLIM_MLM_WT_SET_STA_KEY_STATE";
        default:
            return "INVALID MLM state";
    }
}

void
limPrintMlmState(tpAniSirGlobal pMac, tANI_U16 logLevel, tLimMlmStates state)
{
    limLog(pMac, logLevel, limMlmStateStr(state));
}

char *
limSmeStateStr(tLimSmeStates state)
{
#ifdef FIXME_GEN6
    switch (state)
    {
        case eLIM_SME_OFFLINE_STATE:
            return "eLIM_SME_OFFLINE_STATE";
        case eLIM_SME_IDLE_STATE:
            return "eLIM_SME_IDLE_STATE";
        case eLIM_SME_SUSPEND_STATE:
            return "eLIM_SME_SUSPEND_STATE";
        case eLIM_SME_WT_SCAN_STATE:
            return "eLIM_SME_WT_SCAN_STATE";
        case eLIM_SME_WT_JOIN_STATE:
            return "eLIM_SME_WT_JOIN_STATE";
        case eLIM_SME_WT_AUTH_STATE:
            return "eLIM_SME_WT_AUTH_STATE";
        case eLIM_SME_WT_ASSOC_STATE:
            return "eLIM_SME_WT_ASSOC_STATE";
        case eLIM_SME_WT_REASSOC_STATE:
            return "eLIM_SME_WT_REASSOC_STATE";
        case eLIM_SME_WT_REASSOC_LINK_FAIL_STATE:
            return "eLIM_SME_WT_REASSOC_LINK_FAIL_STATE";
        case eLIM_SME_JOIN_FAILURE_STATE:
            return "eLIM_SME_JOIN_FAILURE_STATE";
        case eLIM_SME_ASSOCIATED_STATE:
            return "eLIM_SME_ASSOCIATED_STATE";
        case eLIM_SME_REASSOCIATED_STATE:
            return "eLIM_SME_REASSOCIATED_STATE";
        case eLIM_SME_LINK_EST_STATE:
            return "eLIM_SME_LINK_EST_STATE";
        case eLIM_SME_LINK_EST_WT_SCAN_STATE:
            return "eLIM_SME_LINK_EST_WT_SCAN_STATE";
        case eLIM_SME_WT_PRE_AUTH_STATE:
            return "eLIM_SME_WT_PRE_AUTH_STATE";
        case eLIM_SME_WT_DISASSOC_STATE:
            return "eLIM_SME_WT_DISASSOC_STATE";
        case eLIM_SME_WT_DEAUTH_STATE:
            return "eLIM_SME_WT_DEAUTH_STATE";
        case eLIM_SME_WT_START_BSS_STATE:
            return "eLIM_SME_WT_START_BSS_STATE";
        case eLIM_SME_WT_STOP_BSS_STATE:
            return "eLIM_SME_WT_STOP_BSS_STATE";
        case eLIM_SME_NORMAL_STATE:
            return "eLIM_SME_NORMAL_STATE";
        case eLIM_SME_CHANNEL_SCAN_STATE:
            return "eLIM_SME_CHANNEL_SCAN_STATE";
        case eLIM_SME_NORMAL_CHANNEL_SCAN_STATE:
            return "eLIM_SME_NORMAL_CHANNEL_SCAN_STATE";
        default:
            return "INVALID SME state";
    }
#endif
return "";
}


char* limDot11ModeStr(tpAniSirGlobal pMac, tANI_U8 dot11Mode)
{
#ifdef FIXME_GEN6

    switch(dot11Mode)
        {
            case WNI_CFG_DOT11_MODE_ALL:
                return "ALL";
            case WNI_CFG_DOT11_MODE_11A:
                return "11A";
            case WNI_CFG_DOT11_MODE_11B:
                return "11B";
            case WNI_CFG_DOT11_MODE_11G:
                return "11G";
            case WNI_CFG_DOT11_MODE_11N:
                return "11N";
            case WNI_CFG_DOT11_MODE_POLARIS:
                return "Polaris";
            case WNI_CFG_DOT11_MODE_TITAN:
                return "Titan";
            case WNI_CFG_DOT11_MODE_TAURUS:
                return "Taurus";
            default:
                return "Invalid Dot11 Mode";
        }
#endif
return "";
}


char* limStaOpRateModeStr(tStaRateMode opRateMode)
{
#ifdef FIXME_GEN6

    switch(opRateMode)
        {
            case eSTA_TAURUS:
                return "Taurus";
            case eSTA_11a:
                return "11A";
            case eSTA_11b:
                return "11B";
            case eSTA_11bg:
                return "11G";
            case eSTA_11n:
                return "11N";
            case eSTA_POLARIS:
                return "Polaris";
            case eSTA_TITAN:
                return "Titan";
            default:
                return "Invalid Dot11 Mode";
        }
#endif
return "";
}

char* limBssTypeStr(tSirBssType bssType)
{
    switch(bssType)
    {
        case eSIR_INFRASTRUCTURE_MODE:
            return "eSIR_INFRASTRUCTURE_MODE";
        case eSIR_IBSS_MODE:
            return "eSIR_IBSS_MODE";
        case eSIR_BTAMP_STA_MODE:
            return "eSIR_BTAMP_STA_MODE";
        case eSIR_BTAMP_AP_MODE:
            return "eSIR_BTAMP_AP_MODE";
        case eSIR_AUTO_MODE:
            return "eSIR_AUTO_MODE";
        default:
            return "Invalid BSS Type";
    }
}

void
limPrintSmeState(tpAniSirGlobal pMac, tANI_U16 logLevel, tLimSmeStates state)
{
    limLog(pMac, logLevel, limSmeStateStr(state));
}

char *limMsgStr(tANI_U32 msgType)
{
#ifdef FIXME_GEN6
    switch (msgType)
    {
        case eWNI_SME_START_REQ:
            return "eWNI_SME_START_REQ";
        case eWNI_SME_START_RSP:
            return "eWNI_SME_START_RSP";
        case eWNI_SME_SYS_READY_IND:
            return "eWNI_SME_SYS_READY_IND";
        case eWNI_SME_SCAN_REQ:
            return "eWNI_SME_SCAN_REQ";
#ifdef FEATURE_OEM_DATA_SUPPORT
        case eWNI_SME_OEM_DATA_REQ:
            return "eWNI_SME_OEM_DATA_REQ";
        case eWNI_SME_OEM_DATA_RSP:
            return "eWNI_SME_OEM_DATA_RSP";
#endif
        case eWNI_SME_SCAN_RSP:
            return "eWNI_SME_SCAN_RSP";
        case eWNI_SME_JOIN_REQ:
            return "eWNI_SME_JOIN_REQ";
        case eWNI_SME_JOIN_RSP:
            return "eWNI_SME_JOIN_RSP";
        case eWNI_SME_SETCONTEXT_REQ:
            return "eWNI_SME_SETCONTEXT_REQ";
        case eWNI_SME_SETCONTEXT_RSP:
            return "eWNI_SME_SETCONTEXT_RSP";
        case eWNI_SME_REASSOC_REQ:
            return "eWNI_SME_REASSOC_REQ";
        case eWNI_SME_REASSOC_RSP:
            return "eWNI_SME_REASSOC_RSP";
        case eWNI_SME_AUTH_REQ:
            return "eWNI_SME_AUTH_REQ";
        case eWNI_SME_AUTH_RSP:
            return "eWNI_SME_AUTH_RSP";
        case eWNI_SME_DISASSOC_REQ:
            return "eWNI_SME_DISASSOC_REQ";
        case eWNI_SME_DISASSOC_RSP:
            return "eWNI_SME_DISASSOC_RSP";
        case eWNI_SME_DISASSOC_IND:
            return "eWNI_SME_DISASSOC_IND";
        case eWNI_SME_DISASSOC_CNF:
            return "eWNI_SME_DISASSOC_CNF";
        case eWNI_SME_DEAUTH_REQ:
            return "eWNI_SME_DEAUTH_REQ";
        case eWNI_SME_DEAUTH_RSP:
            return "eWNI_SME_DEAUTH_RSP";
        case eWNI_SME_DEAUTH_IND:
            return "eWNI_SME_DEAUTH_IND";
        case eWNI_SME_WM_STATUS_CHANGE_NTF:
            return "eWNI_SME_WM_STATUS_CHANGE_NTF";
        case eWNI_SME_START_BSS_REQ:
            return "eWNI_SME_START_BSS_REQ";
        case eWNI_SME_START_BSS_RSP:
            return "eWNI_SME_START_BSS_RSP";
        case eWNI_SME_AUTH_IND:
            return "eWNI_SME_AUTH_IND";
        case eWNI_SME_ASSOC_IND:
            return "eWNI_SME_ASSOC_IND";
        case eWNI_SME_ASSOC_CNF:
            return "eWNI_SME_ASSOC_CNF";
        case eWNI_SME_REASSOC_IND:
            return "eWNI_SME_REASSOC_IND";
        case eWNI_SME_REASSOC_CNF:
            return "eWNI_SME_REASSOC_CNF";
        case eWNI_SME_SWITCH_CHL_REQ:
            return "eWNI_SME_SWITCH_CHL_REQ";
        case eWNI_SME_SWITCH_CHL_RSP:
            return "eWNI_SME_SWITCH_CHL_RSP";
        case eWNI_SME_SWITCH_CHL_CB_PRIMARY_REQ:
            return "eWNI_SME_SWITCH_CHL_CB_PRIMARY_REQ";
        case eWNI_SME_SWITCH_CHL_CB_SECONDARY_REQ:
            return "eWNI_SME_SWITCH_CHL_CB_SECONDARY_REQ";
        case eWNI_SME_STOP_BSS_REQ:
            return "eWNI_SME_STOP_BSS_REQ";
        case eWNI_SME_STOP_BSS_RSP:
            return "eWNI_SME_STOP_BSS_RSP";
        case eWNI_SME_PROMISCUOUS_MODE_REQ:
            return "eWNI_SME_PROMISCUOUS_MODE_REQ";
        case eWNI_SME_PROMISCUOUS_MODE_RSP:
            return "eWNI_SME_PROMISCUOUS_MODE_RSP";
        case eWNI_SME_NEIGHBOR_BSS_IND:
            return "eWNI_SME_NEIGHBOR_BSS_IND";
        case eWNI_SME_MEASUREMENT_REQ:
            return "eWNI_SME_MEASUREMENT_REQ";
        case eWNI_SME_MEASUREMENT_RSP:
            return "eWNI_SME_MEASUREMENT_RSP";
        case eWNI_SME_MEASUREMENT_IND:
            return "eWNI_SME_MEASUREMENT_IND";
        case eWNI_SME_SET_WDS_INFO_REQ:
            return "eWNI_SME_SET_WDS_INFO_REQ";
        case eWNI_SME_SET_WDS_INFO_RSP:
            return "eWNI_SME_SET_WDS_INFO_RSP";
        case eWNI_SME_WDS_INFO_IND:
            return "eWNI_SME_WDS_INFO_IND";
        case eWNI_SME_DEAUTH_CNF:
            return "eWNI_SME_DEAUTH_CNF";
        case eWNI_SME_MIC_FAILURE_IND:
            return "eWNI_SME_MIC_FAILURE_IND";
        case eWNI_SME_LOST_LINK_PARAMS_IND:
            return "eWNI_SME_LOST_LINK_PARAMS_IND";
        case eWNI_SME_ADDTS_REQ:
            return "eWNI_SME_ADDTS_REQ";
        case eWNI_SME_ADDTS_RSP:
            return "eWNI_SME_ADDTS_RSP";
        case eWNI_SME_ADDTS_CNF:
            return "eWNI_SME_ADDTS_CNF";
        case eWNI_SME_ADDTS_IND:
            return "eWNI_SME_ADDTS_IND";
        case eWNI_SME_DELTS_REQ:
            return "eWNI_SME_DELTS_REQ";
        case eWNI_SME_DELTS_RSP:
            return "eWNI_SME_DELTS_RSP";
        case eWNI_SME_DELTS_IND:
            return "eWNI_SME_DELTS_IND";
#if defined WLAN_FEATURE_VOWIFI_11R || defined FEATURE_WLAN_ESE || defined(FEATURE_WLAN_LFR)
        case eWNI_SME_GET_ROAM_RSSI_REQ:
            return "eWNI_SME_GET_ROAM_RSSI_REQ";
        case eWNI_SME_GET_ROAM_RSSI_RSP:
            return "eWNI_SME_GET_ROAM_RSSI_RSP";
#endif

        case WDA_SUSPEND_ACTIVITY_RSP:
            return "WDA_SUSPEND_ACTIVITY_RSP";
        case SIR_LIM_RETRY_INTERRUPT_MSG:
            return "SIR_LIM_RETRY_INTERRUPT_MSG";
        case SIR_BB_XPORT_MGMT_MSG:
            return "SIR_BB_XPORT_MGMT_MSG";
        case SIR_LIM_INV_KEY_INTERRUPT_MSG:
            return "SIR_LIM_INV_KEY_INTERRUPT_MSG";
        case SIR_LIM_KEY_ID_INTERRUPT_MSG:
            return "SIR_LIM_KEY_ID_INTERRUPT_MSG";
        case SIR_LIM_REPLAY_THRES_INTERRUPT_MSG:
            return "SIR_LIM_REPLAY_THRES_INTERRUPT_MSG";
        case SIR_LIM_MIN_CHANNEL_TIMEOUT:
            return "SIR_LIM_MIN_CHANNEL_TIMEOUT";
        case SIR_LIM_MAX_CHANNEL_TIMEOUT:
            return "SIR_LIM_MAX_CHANNEL_TIMEOUT";
        case SIR_LIM_JOIN_FAIL_TIMEOUT:
            return "SIR_LIM_JOIN_FAIL_TIMEOUT";
        case SIR_LIM_AUTH_FAIL_TIMEOUT:
            return "SIR_LIM_AUTH_FAIL_TIMEOUT";
        case SIR_LIM_AUTH_RSP_TIMEOUT:
            return "SIR_LIM_AUTH_RSP_TIMEOUT";
        case SIR_LIM_ASSOC_FAIL_TIMEOUT:
            return "SIR_LIM_ASSOC_FAIL_TIMEOUT";
        case SIR_LIM_REASSOC_FAIL_TIMEOUT:
            return "SIR_LIM_REASSOC_FAIL_TIMEOUT";
        case SIR_LIM_HEART_BEAT_TIMEOUT:
            return "SIR_LIM_HEART_BEAT_TIMEOUT";
        case SIR_LIM_ADDTS_RSP_TIMEOUT:
            return "SIR_LIM_ADDTS_RSP_TIMEOUT";
        case SIR_LIM_CHANNEL_SCAN_TIMEOUT:
            return "SIR_LIM_CHANNEL_SCAN_TIMEOUT";
        case SIR_LIM_LINK_TEST_DURATION_TIMEOUT:
            return "SIR_LIM_LINK_TEST_DURATION_TIMEOUT";
        case SIR_LIM_KEEPALIVE_TIMEOUT:
            return "SIR_LIM_KEEPALIVE_TIMEOUT";
        case SIR_LIM_UPDATE_OLBC_CACHEL_TIMEOUT:
            return "SIR_LIM_UPDATE_OLBC_CACHEL_TIMEOUT";
        case SIR_LIM_CNF_WAIT_TIMEOUT:
            return "SIR_LIM_CNF_WAIT_TIMEOUT";
        case SIR_LIM_RADAR_DETECT_IND:
            return "SIR_LIM_RADAR_DETECT_IND";
#ifdef WLAN_FEATURE_VOWIFI_11R
        case SIR_LIM_FT_PREAUTH_RSP_TIMEOUT:
            return "SIR_LIM_FT_PREAUTH_RSP_TIMEOUT";
#endif

#ifdef WLAN_FEATURE_LFR_MBB
        case SIR_LIM_PREAUTH_MBB_RSP_TIMEOUT:
            return "SIR_LIM_PREAUTH_MBB_RSP_TIMEOUT";
        case SIR_LIM_REASSOC_MBB_RSP_TIMEOUT:
            return "SIR_LIM_REASSOC_MBB_RSP_TIMEOUT";
#endif

        case SIR_HAL_APP_SETUP_NTF:
            return "SIR_HAL_APP_SETUP_NTF";
        case SIR_HAL_INITIAL_CAL_FAILED_NTF:
            return "SIR_HAL_INITIAL_CAL_FAILED_NTF";
        case SIR_HAL_NIC_OPER_NTF:
            return "SIR_HAL_NIC_OPER_NTF";
        case SIR_HAL_INIT_START_REQ:
            return "SIR_HAL_INIT_START_REQ";
        case SIR_HAL_SHUTDOWN_REQ:
            return "SIR_HAL_SHUTDOWN_REQ";
        case SIR_HAL_SHUTDOWN_CNF:
            return "SIR_HAL_SHUTDOWN_CNF";
        case SIR_HAL_RESET_REQ:
            return "SIR_HAL_RESET_REQ";
        case SIR_HAL_RESET_CNF:
            return "SIR_HAL_RESET_CNF";
        case SIR_WRITE_TO_TD:
            return "SIR_WRITE_TO_TD";

        case WNI_CFG_PARAM_UPDATE_IND:
            return "WNI_CFG_PARAM_UPDATE_IND";
        case WNI_CFG_DNLD_REQ:
            return "WNI_CFG_DNLD_REQ";
        case WNI_CFG_DNLD_CNF:
            return "WNI_CFG_DNLD_CNF";
        case WNI_CFG_GET_RSP:
            return "WNI_CFG_GET_RSP";
        case WNI_CFG_SET_CNF:
            return "WNI_CFG_SET_CNF";
        case WNI_CFG_GET_ATTRIB_RSP:
            return "WNI_CFG_GET_ATTRIB_RSP";
        case WNI_CFG_ADD_GRP_ADDR_CNF:
            return "WNI_CFG_ADD_GRP_ADDR_CNF";
        case WNI_CFG_DEL_GRP_ADDR_CNF:
            return "WNI_CFG_DEL_GRP_ADDR_CNF";
        case ANI_CFG_GET_RADIO_STAT_RSP:
            return "ANI_CFG_GET_RADIO_STAT_RSP";
        case ANI_CFG_GET_PER_STA_STAT_RSP:
            return "ANI_CFG_GET_PER_STA_STAT_RSP";
        case ANI_CFG_GET_AGG_STA_STAT_RSP:
            return "ANI_CFG_GET_AGG_STA_STAT_RSP";
        case ANI_CFG_CLEAR_STAT_RSP:
            return "ANI_CFG_CLEAR_STAT_RSP";
        case WNI_CFG_DNLD_RSP:
            return "WNI_CFG_DNLD_RSP";
        case WNI_CFG_GET_REQ:
            return "WNI_CFG_GET_REQ";
        case WNI_CFG_SET_REQ:
            return "WNI_CFG_SET_REQ";
        case WNI_CFG_SET_REQ_NO_RSP:
            return "WNI_CFG_SET_REQ_NO_RSP";
        case eWNI_PMC_ENTER_IMPS_RSP:
            return "eWNI_PMC_ENTER_IMPS_RSP";
        case eWNI_PMC_EXIT_IMPS_RSP:
            return "eWNI_PMC_EXIT_IMPS_RSP";
        case eWNI_PMC_ENTER_BMPS_RSP:
            return "eWNI_PMC_ENTER_BMPS_RSP";
        case eWNI_PMC_EXIT_BMPS_RSP:
            return "eWNI_PMC_EXIT_BMPS_RSP";
        case eWNI_PMC_EXIT_BMPS_IND:
            return "eWNI_PMC_EXIT_BMPS_IND";
        case eWNI_SME_SET_BCN_FILTER_REQ:
            return "eWNI_SME_SET_BCN_FILTER_REQ";
#if defined(FEATURE_WLAN_ESE) && defined(FEATURE_WLAN_ESE_UPLOAD)
        case eWNI_SME_GET_TSM_STATS_REQ:
            return "eWNI_SME_GET_TSM_STATS_REQ";
        case eWNI_SME_GET_TSM_STATS_RSP:
            return "eWNI_SME_GET_TSM_STATS_RSP";
#endif /* FEATURE_WLAN_ESE && FEATURE_WLAN_ESE_UPLOAD */
        default:
            return "INVALID SME message";
    }
#endif
return "";
}



char *limResultCodeStr(tSirResultCodes resultCode)
{
    switch (resultCode)
    {
      case eSIR_SME_SUCCESS:
            return "eSIR_SME_SUCCESS";
      case eSIR_EOF_SOF_EXCEPTION:
            return "eSIR_EOF_SOF_EXCEPTION";
      case eSIR_BMU_EXCEPTION:
            return "eSIR_BMU_EXCEPTION";
      case eSIR_LOW_PDU_EXCEPTION:
            return "eSIR_LOW_PDU_EXCEPTION";
      case eSIR_USER_TRIG_RESET:
            return"eSIR_USER_TRIG_RESET";
      case eSIR_LOGP_EXCEPTION:
            return "eSIR_LOGP_EXCEPTION";
      case eSIR_CP_EXCEPTION:
            return "eSIR_CP_EXCEPTION";
      case eSIR_STOP_BSS:
            return "eSIR_STOP_BSS";
      case eSIR_AHB_HANG_EXCEPTION:
            return "eSIR_AHB_HANG_EXCEPTION";
      case eSIR_DPU_EXCEPTION:
            return "eSIR_DPU_EXCEPTION";
      case eSIR_RXP_EXCEPTION:
            return "eSIR_RXP_EXCEPTION";
      case eSIR_MCPU_EXCEPTION:
            return "eSIR_MCPU_EXCEPTION";
      case eSIR_MCU_EXCEPTION:
            return "eSIR_MCU_EXCEPTION";
      case eSIR_MTU_EXCEPTION:
            return "eSIR_MTU_EXCEPTION";
      case eSIR_MIF_EXCEPTION:
            return "eSIR_MIF_EXCEPTION";
      case eSIR_FW_EXCEPTION:
            return "eSIR_FW_EXCEPTION";
      case eSIR_MAILBOX_SANITY_CHK_FAILED:
            return "eSIR_MAILBOX_SANITY_CHK_FAILED";
      case eSIR_RADIO_HW_SWITCH_STATUS_IS_OFF:
            return "eSIR_RADIO_HW_SWITCH_STATUS_IS_OFF";
      case eSIR_CFB_FLAG_STUCK_EXCEPTION:
            return "eSIR_CFB_FLAG_STUCK_EXCEPTION";
      case eSIR_SME_BASIC_RATES_NOT_SUPPORTED_STATUS:
            return "eSIR_SME_BASIC_RATES_NOT_SUPPORTED_STATUS";
      case eSIR_SME_INVALID_PARAMETERS:
            return "eSIR_SME_INVALID_PARAMETERS";
      case eSIR_SME_UNEXPECTED_REQ_RESULT_CODE:
            return "eSIR_SME_UNEXPECTED_REQ_RESULT_CODE";
      case eSIR_SME_RESOURCES_UNAVAILABLE:
            return "eSIR_SME_RESOURCES_UNAVAILABLE";
      case eSIR_SME_SCAN_FAILED:
            return "eSIR_SME_SCAN_FAILED";
      case eSIR_SME_BSS_ALREADY_STARTED_OR_JOINED:
            return "eSIR_SME_BSS_ALREADY_STARTED_OR_JOINED";
      case eSIR_SME_LOST_LINK_WITH_PEER_RESULT_CODE:
            return "eSIR_SME_LOST_LINK_WITH_PEER_RESULT_CODE";
      case eSIR_SME_REFUSED:
            return "eSIR_SME_REFUSED";
      case eSIR_SME_JOIN_TIMEOUT_RESULT_CODE:
            return "eSIR_SME_JOIN_TIMEOUT_RESULT_CODE";
      case eSIR_SME_AUTH_TIMEOUT_RESULT_CODE:
            return "eSIR_SME_AUTH_TIMEOUT_RESULT_CODE";
      case eSIR_SME_ASSOC_TIMEOUT_RESULT_CODE:
            return "eSIR_SME_ASSOC_TIMEOUT_RESULT_CODE";
      case eSIR_SME_REASSOC_TIMEOUT_RESULT_CODE:
            return "eSIR_SME_REASSOC_TIMEOUT_RESULT_CODE";
      case eSIR_SME_MAX_NUM_OF_PRE_AUTH_REACHED:
            return "eSIR_SME_MAX_NUM_OF_PRE_AUTH_REACHED";
      case eSIR_SME_AUTH_REFUSED:
            return "eSIR_SME_AUTH_REFUSED";
      case eSIR_SME_INVALID_WEP_DEFAULT_KEY:
            return "eSIR_SME_INVALID_WEP_DEFAULT_KEY";
      case eSIR_SME_ASSOC_REFUSED:
            return "eSIR_SME_ASSOC_REFUSED";
      case eSIR_SME_REASSOC_REFUSED:
            return "eSIR_SME_REASSOC_REFUSED";
      case eSIR_SME_STA_NOT_AUTHENTICATED:
            return "eSIR_SME_STA_NOT_AUTHENTICATED";
      case eSIR_SME_STA_NOT_ASSOCIATED:
            return "eSIR_SME_STA_NOT_ASSOCIATED";
      case eSIR_SME_STA_DISASSOCIATED:
            return "eSIR_SME_STA_DISASSOCIATED";
      case eSIR_SME_ALREADY_JOINED_A_BSS:
            return "eSIR_SME_ALREADY_JOINED_A_BSS";
      case eSIR_ULA_COMPLETED:
            return "eSIR_ULA_COMPLETED";
      case eSIR_ULA_FAILURE:
            return "eSIR_ULA_FAILURE";
      case eSIR_SME_LINK_ESTABLISHED:
            return "eSIR_SME_LINK_ESTABLISHED";
      case eSIR_SME_UNABLE_TO_PERFORM_MEASUREMENTS:
            return "eSIR_SME_UNABLE_TO_PERFORM_MEASUREMENTS";
      case eSIR_SME_UNABLE_TO_PERFORM_DFS:
            return "eSIR_SME_UNABLE_TO_PERFORM_DFS";
      case eSIR_SME_DFS_FAILED:
            return "eSIR_SME_DFS_FAILED";
      case eSIR_SME_TRANSFER_STA:
            return "eSIR_SME_TRANSFER_STA";
      case eSIR_SME_INVALID_LINK_TEST_PARAMETERS:
            return "eSIR_SME_INVALID_LINK_TEST_PARAMETERS";
      case eSIR_SME_LINK_TEST_MAX_EXCEEDED:
            return "eSIR_SME_LINK_TEST_MAX_EXCEEDED";
      case eSIR_SME_UNSUPPORTED_RATE:
            return "eSIR_SME_UNSUPPORTED_RATE";
      case eSIR_SME_LINK_TEST_TIMEOUT:
            return "eSIR_SME_LINK_TEST_TIMEOUT";
      case eSIR_SME_LINK_TEST_COMPLETE:
            return "eSIR_SME_LINK_TEST_COMPLETE";
      case eSIR_SME_LINK_TEST_INVALID_STATE:
            return "eSIR_SME_LINK_TEST_INVALID_STATE";
      case eSIR_SME_LINK_TEST_INVALID_ADDRESS:
            return "eSIR_SME_LINK_TEST_INVALID_ADDRESS";
      case eSIR_SME_POLARIS_RESET:
            return "eSIR_SME_POLARIS_RESET";
      case eSIR_SME_SETCONTEXT_FAILED:
            return "eSIR_SME_SETCONTEXT_FAILED";
      case eSIR_SME_BSS_RESTART:
            return "eSIR_SME_BSS_RESTART";
      case eSIR_SME_MORE_SCAN_RESULTS_FOLLOW:
            return "eSIR_SME_MORE_SCAN_RESULTS_FOLLOW";
      case eSIR_SME_INVALID_ASSOC_RSP_RXED:
            return "eSIR_SME_INVALID_ASSOC_RSP_RXED";
      case eSIR_SME_MIC_COUNTER_MEASURES:
            return "eSIR_SME_MIC_COUNTER_MEASURES";
      case eSIR_SME_ADDTS_RSP_TIMEOUT:
            return "eSIR_SME_ADDTS_RSP_TIMEOUT";
      case eSIR_SME_RECEIVED:
            return "eSIR_SME_RECEIVED";
      case eSIR_SME_CHANNEL_SWITCH_FAIL:
            return "eSIR_SME_CHANNEL_SWITCH_FAIL";
#ifdef GEN4_SCAN
      case eSIR_SME_CHANNEL_SWITCH_DISABLED:
            return "eSIR_SME_CHANNEL_SWITCH_DISABLED";
      case eSIR_SME_HAL_SCAN_INIT_FAILED:
            return "eSIR_SME_HAL_SCAN_INIT_FAILED";
      case eSIR_SME_HAL_SCAN_START_FAILED:
            return "eSIR_SME_HAL_SCAN_START_FAILED";
      case eSIR_SME_HAL_SCAN_END_FAILED:
            return "eSIR_SME_HAL_SCAN_END_FAILED";
      case eSIR_SME_HAL_SCAN_FINISH_FAILED:
            return "eSIR_SME_HAL_SCAN_FINISH_FAILED";
      case eSIR_SME_HAL_SEND_MESSAGE_FAIL:
            return "eSIR_SME_HAL_SEND_MESSAGE_FAIL";
#else // GEN4_SCAN
      case eSIR_SME_CHANNEL_SWITCH_DISABLED:
            return "eSIR_SME_CHANNEL_SWITCH_DISABLED";
      case eSIR_SME_HAL_SEND_MESSAGE_FAIL:
            return "eSIR_SME_HAL_SEND_MESSAGE_FAIL";
#endif // GEN4_SCAN

        default:
            return "INVALID resultCode";
    }
}

void
limPrintMsgName(tpAniSirGlobal pMac, tANI_U16 logLevel, tANI_U32 msgType)
{
    limLog(pMac, logLevel, limMsgStr(msgType));
}

void
limPrintMsgInfo(tpAniSirGlobal pMac, tANI_U16 logLevel, tSirMsgQ *msg)
{
    if (logLevel <= pMac->utils.gLogDbgLevel[SIR_LIM_MODULE_ID - LOG_FIRST_MODULE_ID])
    {
        switch (msg->type)
        {
            case SIR_BB_XPORT_MGMT_MSG:
                limPrintMsgName(pMac, logLevel,msg->type);
                break;
            default:
                limPrintMsgName(pMac, logLevel,msg->type);
                break;
        }
    }
}

/**
 * limInitMlm()
 *
 *FUNCTION:
 * This function is called by limProcessSmeMessages() to
 * initialize MLM state machine on STA
 *
 *PARAMS:
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * NA
 *
 * @param  pMac      Pointer to Global MAC structure
 * @return None
 */
void
limInitMlm(tpAniSirGlobal pMac)
{
    tANI_U32 retVal;

    pMac->lim.gLimTimersCreated = 0;

    MTRACE(macTrace(pMac, TRACE_CODE_MLM_STATE, NO_SESSION, pMac->lim.gLimMlmState));

    /// Initialize scan result hash table
    limReInitScanResults(pMac); //sep26th review

#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
    /// Initialize lfr scan result hash table
    // Could there be a problem in multisession with SAP/P2P GO, when in the
    // middle of FW bg scan, SAP started; Again that could be a problem even on
    // infra + SAP/P2P GO too - TBD
    limReInitLfrScanResults(pMac);
#endif
  
    /// Initialize number of pre-auth contexts
    pMac->lim.gLimNumPreAuthContexts = 0;

    /// Initialize MAC based Authentication STA list
    limInitPreAuthList(pMac);

    //pMac->lim.gpLimMlmJoinReq = NULL;

    if (pMac->lim.gLimTimersCreated)
        return;

    // Create timers used by LIM
    retVal = limCreateTimers(pMac);
    if(retVal == TX_SUCCESS)
    {
        pMac->lim.gLimTimersCreated = 1;
    }
    else
    {
        limLog(pMac, LOGP, FL(" limCreateTimers Failed to create lim timers "));
    }
} /*** end limInitMlm() ***/



/**
 * limCleanupMlm()
 *
 *FUNCTION:
 * This function is called to cleanup any resources
 * allocated by the  MLM state machine.
 *
 *PARAMS:
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * It is assumed that BSS is already informed that we're leaving it
 * before this function is called.
 *
 * @param  pMac      Pointer to Global MAC structure
 * @param  None
 * @return None
 */
void
limCleanupMlm(tpAniSirGlobal pMac)
{
    tANI_U32   n;
    tLimPreAuthNode *pAuthNode;
#ifdef WLAN_FEATURE_11W
    tANI_U32  bss_entry, sta_entry;
    tpDphHashNode pStaDs = NULL;
    tpPESession psessionEntry = NULL;
#endif

    if (pMac->lim.gLimTimersCreated == 1)
    {
        // Deactivate and delete MIN/MAX channel timers.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimMinChannelTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimMinChannelTimer);
        tx_timer_deactivate(&pMac->lim.limTimers.gLimMaxChannelTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimMaxChannelTimer);
        tx_timer_deactivate(&pMac->lim.limTimers.gLimPeriodicProbeReqTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimPeriodicProbeReqTimer);


        // Deactivate and delete channel switch timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimChannelSwitchTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimChannelSwitchTimer);


        // Deactivate and delete addts response timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimAddtsRspTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimAddtsRspTimer);

        // Deactivate and delete Join failure timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimJoinFailureTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimJoinFailureTimer);

        // Deactivate and delete Periodic Join Probe Request timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimPeriodicJoinProbeReqTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimPeriodicJoinProbeReqTimer);

        // Deactivate and delete Auth Retry timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimPeriodicAuthRetryTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimPeriodicAuthRetryTimer);

        // Deactivate and delete Association failure timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimAssocFailureTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimAssocFailureTimer);

        // Deactivate and delete Reassociation failure timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimReassocFailureTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimReassocFailureTimer);

        // Deactivate and delete Authentication failure timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimAuthFailureTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimAuthFailureTimer);

        // Deactivate and delete Heartbeat timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimHeartBeatTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimHeartBeatTimer);

        // Deactivate and delete wait-for-probe-after-Heartbeat timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimProbeAfterHBTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimProbeAfterHBTimer);

        // Deactivate and delete Quiet timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimQuietTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimQuietTimer);

        // Deactivate and delete Quiet BSS timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimQuietBssTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimQuietBssTimer);

        // Deactivate and delete LIM background scan timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimBackgroundScanTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimBackgroundScanTimer);


        // Deactivate and delete cnf wait timer
        for (n = 0; n < pMac->lim.maxStation; n++)
        {
            tx_timer_deactivate(&pMac->lim.limTimers.gpLimCnfWaitTimer[n]);
            tx_timer_delete(&pMac->lim.limTimers.gpLimCnfWaitTimer[n]);
        }

        // Deactivate and delete keepalive timer
        tx_timer_deactivate(&pMac->lim.limTimers.gLimKeepaliveTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimKeepaliveTimer);

        pAuthNode = pMac->lim.gLimPreAuthTimerTable.pTable;
        
        //Deactivate any Authentication response timers
        limDeletePreAuthList(pMac);

        for (n = 0; n < pMac->lim.gLimPreAuthTimerTable.numEntry; n++,pAuthNode++)
        {
            // Delete any Authentication response
            // timers, which might have been started.
            tx_timer_delete(&pAuthNode->timer);
        }

        tx_timer_deactivate(&pMac->lim.limTimers.gLimUpdateOlbcCacheTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimUpdateOlbcCacheTimer);
        tx_timer_deactivate(&pMac->lim.limTimers.gLimPreAuthClnupTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimPreAuthClnupTimer);

#if 0 // The WPS PBC clean up timer is disabled
        if (pMac->lim.gLimSystemRole == eLIM_AP_ROLE)
        {
            if(pMac->lim.limTimers.gLimWPSOverlapTimerObj.isTimerCreated == eANI_BOOLEAN_TRUE)
            {
                tx_timer_deactivate(&pMac->lim.limTimers.gLimWPSOverlapTimerObj.gLimWPSOverlapTimer);
                tx_timer_delete(&pMac->lim.limTimers.gLimWPSOverlapTimerObj.gLimWPSOverlapTimer);
                pMac->lim.limTimers.gLimWPSOverlapTimerObj.isTimerCreated = eANI_BOOLEAN_FALSE;
            }
        }
#endif
#ifdef WLAN_FEATURE_VOWIFI_11R
        // Deactivate and delete FT Preauth response timer
        tx_timer_deactivate(&pMac->lim.limTimers.gLimFTPreAuthRspTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimFTPreAuthRspTimer);
#endif


#if defined(FEATURE_WLAN_ESE) && !defined(FEATURE_WLAN_ESE_UPLOAD)
        // Deactivate and delete TSM
        tx_timer_deactivate(&pMac->lim.limTimers.gLimEseTsmTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimEseTsmTimer);
#endif /* FEATURE_WLAN_ESE && !FEATURE_WLAN_ESE_UPLOAD */

        tx_timer_deactivate(&pMac->lim.limTimers.gLimDisassocAckTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimDisassocAckTimer);

        tx_timer_deactivate(&pMac->lim.limTimers.gLimDeauthAckTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimDeauthAckTimer);

        tx_timer_deactivate(&pMac->lim.limTimers.gLimP2pSingleShotNoaInsertTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimP2pSingleShotNoaInsertTimer);

        tx_timer_deactivate(&pMac->lim.limTimers.gLimActiveToPassiveChannelTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimActiveToPassiveChannelTimer);

        tx_timer_deactivate(&pMac->lim.limTimers.g_lim_ap_ecsa_timer);
        tx_timer_delete(&pMac->lim.limTimers.g_lim_ap_ecsa_timer);

        pMac->lim.gLimTimersCreated = 0;
    }

#ifdef WLAN_FEATURE_11W
    /*
     * When SSR is triggered, we need to loop through
     * each STA associated per BSSId and deactivate/delete
     * the pmfSaQueryTimer for it
     */
    if (vos_is_logp_in_progress(VOS_MODULE_ID_PE, NULL))
    {
        VOS_TRACE(VOS_MODULE_ID_PE, VOS_TRACE_LEVEL_ERROR,
                  FL("SSR is detected, proceed to clean up pmfSaQueryTimer"));
        for (bss_entry = 0; bss_entry < pMac->lim.maxBssId; bss_entry++)
        {
             if (pMac->lim.gpSession[bss_entry].valid)
             {
                 for (sta_entry = 1; sta_entry < pMac->lim.gLimAssocStaLimit;
                      sta_entry++)
                 {
                      psessionEntry = &pMac->lim.gpSession[bss_entry];
                      pStaDs = dphGetHashEntry(pMac, sta_entry,
                                              &psessionEntry->dph.dphHashTable);
                      if (NULL == pStaDs)
                      {
                          continue;
                      }
                      VOS_TRACE(VOS_MODULE_ID_PE, VOS_TRACE_LEVEL_ERROR,
                                FL("Deleting pmfSaQueryTimer for staid[%d]"),
                                pStaDs->staIndex) ;
                      tx_timer_deactivate(&pStaDs->pmfSaQueryTimer);
                      tx_timer_delete(&pStaDs->pmfSaQueryTimer);
                }
            }
        }
    }
#endif

    /// Cleanup cached scan list
    limReInitScanResults(pMac);
#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
    /// Cleanup cached scan list
    limReInitLfrScanResults(pMac);
#endif

} /*** end limCleanupMlm() ***/



/**
 * limCleanupLmm()
 *
 *FUNCTION:
 * This function is called to cleanup any resources
 * allocated by LMM sub-module.
 *
 *PARAMS:
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * NA
 *
 * @param  pMac      Pointer to Global MAC structure
 * @return None
 */

void
limCleanupLmm(tpAniSirGlobal pMac)
{
} /*** end limCleanupLmm() ***/



/**
 * limIsAddrBC()
 *
 *FUNCTION:
 * This function is called in various places within LIM code
 * to determine whether passed MAC address is a broadcast or not
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * NA
 *
 * @param macAddr  Indicates MAC address that need to be determined
 *                 whether it is Broadcast address or not
 *
 * @return true if passed address is Broadcast address else false
 */

tANI_U8
limIsAddrBC(tSirMacAddr macAddr)
{
    int i;
    for (i = 0; i < 6; i++)
    {
        if ((macAddr[i] & 0xFF) != 0xFF)
            return false;
    }

    return true;
} /****** end limIsAddrBC() ******/



/**
 * limIsGroupAddr()
 *
 *FUNCTION:
 * This function is called in various places within LIM code
 * to determine whether passed MAC address is a group address or not
 *
 *LOGIC:
 * If least significant bit of first octet of the MAC address is
 * set to 1, it is a Group address.
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * NA
 *
 * @param macAddr  Indicates MAC address that need to be determined
 *                 whether it is Group address or not
 *
 * @return true if passed address is Group address else false
 */

tANI_U8
limIsGroupAddr(tSirMacAddr macAddr)
{
    if ((macAddr[0] & 0x01) == 0x01)
        return true;
    else
        return false;
} /****** end limIsGroupAddr() ******/

/**
 * limPostMsgApiNoWait()
 *
 *FUNCTION:
 * This function is called from other thread while posting a
 * message to LIM message Queue gSirLimMsgQ with NO_WAIT option
 *
 *LOGIC:
 * NA
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * NA
 *
 * @param  pMsg - Pointer to the Global MAC structure
 * @param  pMsg - Pointer to the message structure
 * @return None
 */

tANI_U32
limPostMsgApiNoWait(tpAniSirGlobal pMac, tSirMsgQ *pMsg)
{
    limProcessMessages(pMac, pMsg);
    return TX_SUCCESS;
} /*** end limPostMsgApiNoWait() ***/



/**
 * limPrintMacAddr()
 *
 *FUNCTION:
 * This function is called to print passed MAC address
 * in : format.
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * @param  macAddr  - MacAddr to be printed
 * @param  logLevel - Loglevel to be used
 *
 * @return None.
 */

void
limPrintMacAddr(tpAniSirGlobal pMac, tSirMacAddr macAddr, tANI_U8 logLevel)
{
    limLog(pMac, logLevel,
           FL(MAC_ADDRESS_STR), MAC_ADDR_ARRAY(macAddr));
} /****** end limPrintMacAddr() ******/


/*
 * limResetDeferredMsgQ()
 *
 *FUNCTION:
 * This function resets the deferred message queue parameters.
 *
 *PARAMS:
 * @param pMac     - Pointer to Global MAC structure
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * NA
 *
 *RETURNS:
 * None
 */

void limResetDeferredMsgQ(tpAniSirGlobal pMac)
{
    pMac->lim.gLimDeferredMsgQ.size =
    pMac->lim.gLimDeferredMsgQ.write =
    pMac->lim.gLimDeferredMsgQ.read = 0;

}


#define LIM_DEFERRED_Q_CHECK_THRESHOLD  (MAX_DEFERRED_QUEUE_LEN/2)
#define LIM_MAX_NUM_MGMT_FRAME_DEFERRED (MAX_DEFERRED_QUEUE_LEN/2)

/*
 * limWriteDeferredMsgQ()
 *
 *FUNCTION:
 * This function queues up a deferred message for later processing on the
 * STA side.
 *
 *PARAMS:
 * @param pMac     - Pointer to Global MAC structure
 * @param limMsg   - a LIM message
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * NA
 *
 *RETURNS:
 * None
 */

tANI_U8 limWriteDeferredMsgQ(tpAniSirGlobal pMac, tpSirMsgQ limMsg)
{
    PELOG1(limLog(pMac, LOG1,
           FL("**  Queue a deferred message (size %d, write %d) - type 0x%x  **"),
           pMac->lim.gLimDeferredMsgQ.size, pMac->lim.gLimDeferredMsgQ.write,
           limMsg->type);)

        /*
         ** check if the deferred message queue is full
         **/
    if (pMac->lim.gLimDeferredMsgQ.size >= MAX_DEFERRED_QUEUE_LEN)
    {
        if (!(pMac->lim.deferredMsgCnt & 0xF))
        {
            limLog(pMac, LOGE,
             FL("Deferred Message Queue is full. Msg:%d Messages Failed:%d"),
                                    limMsg->type, ++pMac->lim.deferredMsgCnt);
            vos_fatal_event_logs_req(WLAN_LOG_TYPE_NON_FATAL,
                     WLAN_LOG_INDICATOR_HOST_DRIVER,
                     WLAN_LOG_REASON_QUEUE_FULL,
                     FALSE, TRUE);
        }
        else
        {
            pMac->lim.deferredMsgCnt++;
        }
        return TX_QUEUE_FULL;
    }

    /*
    ** In the application, there should not be more than 1 message get
    ** queued up. If happens, flags a warning. In the future, this can
    ** happen.
    **/
    if (pMac->lim.gLimDeferredMsgQ.size > 0)
    {
        PELOGW(limLog(pMac, LOGW, FL("%d Deferred messages (type 0x%x, scan %d, global sme %d, global mlme %d, addts %d)"),
               pMac->lim.gLimDeferredMsgQ.size, limMsg->type,
               limIsSystemInScanState(pMac),
               pMac->lim.gLimSmeState, pMac->lim.gLimMlmState,
               pMac->lim.gLimAddtsSent);)
    }

    /*
    ** To prevent the deferred Q is full of management frames, only give them certain space
    **/
    if( SIR_BB_XPORT_MGMT_MSG == limMsg->type )
    {
        if( LIM_DEFERRED_Q_CHECK_THRESHOLD < pMac->lim.gLimDeferredMsgQ.size )
        {
            tANI_U16 idx, count = 0;
            for(idx = 0; idx < pMac->lim.gLimDeferredMsgQ.size; idx++)
            {
                if( SIR_BB_XPORT_MGMT_MSG == pMac->lim.gLimDeferredMsgQ.deferredQueue[idx].type )
                {
                    count++;
                }
            }
            if( LIM_MAX_NUM_MGMT_FRAME_DEFERRED < count )
            {
                //We reach the quota for management frames, drop this one
                PELOGW(limLog(pMac, LOGW, FL("Cannot deferred. Msg: %d Too many (count=%d) already"), limMsg->type, count);)
                //Return error, caller knows what to do
                return TX_QUEUE_FULL;
            }
        }
    }

    ++pMac->lim.gLimDeferredMsgQ.size;

    /* reset the count here since we are able to defer the message */
    if(pMac->lim.deferredMsgCnt != 0)
    {
        pMac->lim.deferredMsgCnt = 0;
    }

    /*
    ** if the write pointer hits the end of the queue, rewind it
    **/
    if (pMac->lim.gLimDeferredMsgQ.write >= MAX_DEFERRED_QUEUE_LEN)
        pMac->lim.gLimDeferredMsgQ.write = 0;

    /*
    ** save the message to the queue and advanced the write pointer
    **/
    vos_mem_copy( (tANI_U8 *)&pMac->lim.gLimDeferredMsgQ.deferredQueue[
                    pMac->lim.gLimDeferredMsgQ.write++],
                  (tANI_U8 *)limMsg,
                  sizeof(tSirMsgQ));
    return TX_SUCCESS;

}

/*
 * limReadDeferredMsgQ()
 *
 *FUNCTION:
 * This function dequeues a deferred message for processing on the
 * STA side.
 *
 *PARAMS:
 * @param pMac     - Pointer to Global MAC structure
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 *
 *
 *RETURNS:
 * Returns the message at the head of the deferred message queue
 */

tSirMsgQ* limReadDeferredMsgQ(tpAniSirGlobal pMac)
{
    tSirMsgQ    *msg;

    /*
    ** check any messages left. If no, return
    **/
    if (pMac->lim.gLimDeferredMsgQ.size <= 0)
        return NULL;

    /*
    ** decrement the queue size
    **/
    pMac->lim.gLimDeferredMsgQ.size--;

    /*
    ** retrieve the message from the head of the queue
    **/
    msg = &pMac->lim.gLimDeferredMsgQ.deferredQueue[pMac->lim.gLimDeferredMsgQ.read];

    /*
    ** advance the read pointer
    **/
    pMac->lim.gLimDeferredMsgQ.read++;

    /*
    ** if the read pointer hits the end of the queue, rewind it
    **/
    if (pMac->lim.gLimDeferredMsgQ.read >= MAX_DEFERRED_QUEUE_LEN)
        pMac->lim.gLimDeferredMsgQ.read = 0;

   PELOG1(limLog(pMac, LOG1,
           FL("**  DeQueue a deferred message (size %d read %d) - type 0x%x  **"),
           pMac->lim.gLimDeferredMsgQ.size, pMac->lim.gLimDeferredMsgQ.read,
           msg->type);)

   PELOG1(limLog(pMac, LOG1, FL("DQ msg -- scan %d, global sme %d, global mlme %d, addts %d"),
           limIsSystemInScanState(pMac),
           pMac->lim.gLimSmeState, pMac->lim.gLimMlmState,
           pMac->lim.gLimAddtsSent);)

    return(msg);
}

tSirRetStatus
limSysProcessMmhMsgApi(tpAniSirGlobal pMac,
                    tSirMsgQ *pMsg,
                    tANI_U8 qType)
{
// FIXME
   SysProcessMmhMsg(pMac, pMsg);
   return eSIR_SUCCESS;
}

char *limFrameStr(tANI_U32 type, tANI_U32 subType)
{
#ifdef FIXME_GEN6

    if (type == SIR_MAC_MGMT_FRAME)
    {
        switch (subType)
        {
            case SIR_MAC_MGMT_ASSOC_REQ:
                return "MAC_MGMT_ASSOC_REQ";
            case SIR_MAC_MGMT_ASSOC_RSP:
                return "MAC_MGMT_ASSOC_RSP";
            case SIR_MAC_MGMT_REASSOC_REQ:
                return "MAC_MGMT_REASSOC_REQ";
            case SIR_MAC_MGMT_REASSOC_RSP:
                return "MAC_MGMT_REASSOC_RSP";
            case SIR_MAC_MGMT_PROBE_REQ:
                return "MAC_MGMT_PROBE_REQ";
            case SIR_MAC_MGMT_PROBE_RSP:
                return "MAC_MGMT_PROBE_RSP";
            case SIR_MAC_MGMT_BEACON:
                return "MAC_MGMT_BEACON";
            case SIR_MAC_MGMT_ATIM:
                return "MAC_MGMT_ATIM";
            case SIR_MAC_MGMT_DISASSOC:
                return "MAC_MGMT_DISASSOC";
            case SIR_MAC_MGMT_AUTH:
                return "MAC_MGMT_AUTH";
            case SIR_MAC_MGMT_DEAUTH:
                return "MAC_MGMT_DEAUTH";
            case SIR_MAC_MGMT_ACTION:
                return "MAC_MGMT_ACTION";
            case SIR_MAC_MGMT_RESERVED15:
                return "MAC_MGMT_RESERVED15";
            default:
                return "Unknown MGMT Frame";
        }
    }

    else if (type == SIR_MAC_CTRL_FRAME)
    {
        switch (subType)
        {
            case SIR_MAC_CTRL_RR:
                return "MAC_CTRL_RR";
            case SIR_MAC_CTRL_BAR:
                return "MAC_CTRL_BAR";
            case SIR_MAC_CTRL_BA:
                return "MAC_CTRL_BA";
            case SIR_MAC_CTRL_PS_POLL:
                return "MAC_CTRL_PS_POLL";
            case SIR_MAC_CTRL_RTS:
                return "MAC_CTRL_RTS";
            case SIR_MAC_CTRL_CTS:
                return "MAC_CTRL_CTS";
            case SIR_MAC_CTRL_ACK:
                return "MAC_CTRL_ACK";
            case SIR_MAC_CTRL_CF_END:
                return "MAC_CTRL_CF_END";
            case SIR_MAC_CTRL_CF_END_ACK:
                return "MAC_CTRL_CF_END_ACK";
            default:
                return "Unknown CTRL Frame";
        }
    }

    else if (type == SIR_MAC_DATA_FRAME)
    {
        switch (subType)
        {
            case SIR_MAC_DATA_DATA:
                return "MAC_DATA_DATA";
            case SIR_MAC_DATA_DATA_ACK:
                return "MAC_DATA_DATA_ACK";
            case SIR_MAC_DATA_DATA_POLL:
                return "MAC_DATA_DATA_POLL";
            case SIR_MAC_DATA_DATA_ACK_POLL:
                return "MAC_DATA_DATA_ACK_POLL";
            case SIR_MAC_DATA_NULL:
                return "MAC_DATA_NULL";
            case SIR_MAC_DATA_NULL_ACK:
                return "MAC_DATA_NULL_ACK";
            case SIR_MAC_DATA_NULL_POLL:
                return "MAC_DATA_NULL_POLL";
            case SIR_MAC_DATA_NULL_ACK_POLL:
                return "MAC_DATA_NULL_ACK_POLL";
            case SIR_MAC_DATA_QOS_DATA:
                return "MAC_DATA_QOS_DATA";
            case SIR_MAC_DATA_QOS_DATA_ACK:
                return "MAC_DATA_QOS_DATA_ACK";
            case SIR_MAC_DATA_QOS_DATA_POLL:
                return "MAC_DATA_QOS_DATA_POLL";
            case SIR_MAC_DATA_QOS_DATA_ACK_POLL:
                return "MAC_DATA_QOS_DATA_ACK_POLL";
            case SIR_MAC_DATA_QOS_NULL:
                return "MAC_DATA_QOS_NULL";
            case SIR_MAC_DATA_QOS_NULL_ACK:
                return "MAC_DATA_QOS_NULL_ACK";
            case SIR_MAC_DATA_QOS_NULL_POLL:
                return "MAC_DATA_QOS_NULL_POLL";
            case SIR_MAC_DATA_QOS_NULL_ACK_POLL:
                return "MAC_DATA_QOS_NULL_ACK_POLL";
            default:
                return "Unknown Data Frame";
        }
    }
    else
        return "Unknown";
#endif
return "";
}

void limHandleUpdateOlbcCache(tpAniSirGlobal pMac)
{
    int i;
    static int enable;
    tUpdateBeaconParams beaconParams;

    tpPESession       psessionEntry = limIsApSessionActive(pMac);

    if (psessionEntry == NULL)
    {
        PELOGE(limLog(pMac, LOGE, FL(" Session not found"));)
        return;
    }

    vos_mem_set( ( tANI_U8* )&beaconParams, sizeof( tUpdateBeaconParams), 0);
    beaconParams.bssIdx = psessionEntry->bssIdx;
    
    beaconParams.paramChangeBitmap = 0;
    /*
    ** This is doing a 2 pass check. The first pass is to invalidate
    ** all the cache entries. The second pass is to decide whether to
    ** disable protection.
    **/
    if (!enable)
    {

            PELOG2(limLog(pMac, LOG2, FL("Resetting OLBC cache"));)
            psessionEntry->gLimOlbcParams.numSta = 0;
            psessionEntry->gLimOverlap11gParams.numSta = 0;
            psessionEntry->gLimOverlapHt20Params.numSta = 0;
            psessionEntry->gLimNonGfParams.numSta = 0;
            psessionEntry->gLimLsigTxopParams.numSta = 0;

        for (i=0; i < LIM_PROT_STA_OVERLAP_CACHE_SIZE; i++)
            pMac->lim.protStaOverlapCache[i].active = false;

        enable = 1;
    }
    else
    {

        if (!psessionEntry->gLimOlbcParams.numSta)
        {
            if (psessionEntry->gLimOlbcParams.protectionEnabled)
            {
                if (!psessionEntry->gLim11bParams.protectionEnabled)
                {
                    PELOG1(limLog(pMac, LOG1, FL("Overlap cache all clear and no 11B STA detected"));)
                    limEnable11gProtection(pMac, false, true, &beaconParams, psessionEntry);
                }
            }
        }

        if (!psessionEntry->gLimOverlap11gParams.numSta)
        {
            if (psessionEntry->gLimOverlap11gParams.protectionEnabled)
            {
                if (!psessionEntry->gLim11gParams.protectionEnabled)
                {
                    PELOG1(limLog(pMac, LOG1, FL("Overlap cache all clear and no 11G STA detected"));)
                    limEnableHtProtectionFrom11g(pMac, false, true, &beaconParams,psessionEntry);
                }
            }
        }

        if (!psessionEntry->gLimOverlapHt20Params.numSta)
        {
            if (psessionEntry->gLimOverlapHt20Params.protectionEnabled)
            {
                if (!psessionEntry->gLimHt20Params.protectionEnabled)
                {
                    PELOG1(limLog(pMac, LOG1, FL("Overlap cache all clear and no HT20 STA detected"));)
                    limEnable11gProtection(pMac, false, true, &beaconParams,psessionEntry);
                }
            }
        }

        enable = 0;
    }

    if(beaconParams.paramChangeBitmap)
    {
        schSetFixedBeaconFields(pMac,psessionEntry);
        limSendBeaconParams(pMac, &beaconParams, psessionEntry);
    }

    // Start OLBC timer
    if (tx_timer_activate(&pMac->lim.limTimers.gLimUpdateOlbcCacheTimer) != TX_SUCCESS)
    {
        limLog(pMac, LOGE, FL("tx_timer_activate failed"));
    }
}

/**
 * limIsNullSsid()
 *
 *FUNCTION:
 * This function checks if Ssid supplied is Null SSID
 *
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * NA
 *
 * @param tSirMacSSid *
 *
 *
 * @return true if SSID is Null SSID else false
 */

tANI_U8
limIsNullSsid( tSirMacSSid *pSsid )
{
    tANI_U8 fNullSsid = false;
    tANI_U32 SsidLength;
    tANI_U8 *pSsidStr;

    do
    {
        if ( 0 == pSsid->length )
        {
            fNullSsid = true;
            break;
        }

#define ASCII_SPACE_CHARACTER 0x20
        /* If the first charactes is space and SSID length is 1
         * then consider it as NULL SSID*/
        if ((ASCII_SPACE_CHARACTER == pSsid->ssId[0]) &&
            (pSsid->length == 1))
        {
             fNullSsid = true;
             break;
        }
        else
        {
            /* check if all the charactes in SSID are NULL*/
            SsidLength = pSsid->length;
            pSsidStr = pSsid->ssId;

            while ( SsidLength )
            {
                if( *pSsidStr )
                    break;

                pSsidStr++;
                SsidLength--;
            }

            if( 0 == SsidLength )
            {
                fNullSsid = true;
                break;
            }
        }
    }
    while( 0 );

    return fNullSsid;
} /****** end limIsNullSsid() ******/




/** -------------------------------------------------------------
\fn limUpdateProtStaParams
\brief updates protection related counters.
\param      tpAniSirGlobal    pMac
\param      tSirMacAddr peerMacAddr
\param      tLimProtStaCacheType protStaCacheType
\param      tHalBitVal gfSupported
\param      tHalBitVal lsigTxopSupported
\return      None
  -------------------------------------------------------------*/
void
limUpdateProtStaParams(tpAniSirGlobal pMac,
tSirMacAddr peerMacAddr, tLimProtStaCacheType protStaCacheType,
tHalBitVal gfSupported, tHalBitVal lsigTxopSupported,
tpPESession psessionEntry)
{
  tANI_U32 i;

  PELOG1(limLog(pMac,LOG1, FL("A STA is associated:"));
  limLog(pMac,LOG1, FL("Addr : "));
  limPrintMacAddr(pMac, peerMacAddr, LOG1);)

  for (i=0; i<LIM_PROT_STA_CACHE_SIZE; i++)
  {
      if (psessionEntry->protStaCache[i].active)
      {
          PELOG1(limLog(pMac, LOG1, FL("Addr: "));)
          PELOG1(limPrintMacAddr(pMac, psessionEntry->protStaCache[i].addr, LOG1);)

          if (vos_mem_compare(
              psessionEntry->protStaCache[i].addr,
              peerMacAddr, sizeof(tSirMacAddr)))
          {
              PELOG1(limLog(pMac, LOG1, FL("matching cache entry at %d already active."), i);)
              return;
          }
      }
  }

  for (i=0; i<LIM_PROT_STA_CACHE_SIZE; i++)
  {
      if (!psessionEntry->protStaCache[i].active)
          break;
  }

  if (i >= LIM_PROT_STA_CACHE_SIZE)
  {
      PELOGE(limLog(pMac, LOGE, FL("No space in ProtStaCache"));)
      return;
  }

  vos_mem_copy( psessionEntry->protStaCache[i].addr,
                peerMacAddr,
                sizeof(tSirMacAddr));

  psessionEntry->protStaCache[i].protStaCacheType = protStaCacheType;
  psessionEntry->protStaCache[i].active = true;
  if(eLIM_PROT_STA_CACHE_TYPE_llB == protStaCacheType)
  {
      psessionEntry->gLim11bParams.numSta++;
      limLog(pMac,LOG1, FL("11B, "));
  }
  else if(eLIM_PROT_STA_CACHE_TYPE_llG == protStaCacheType)
  {
      psessionEntry->gLim11gParams.numSta++;
      limLog(pMac,LOG1, FL("11G, "));
  }
  else   if(eLIM_PROT_STA_CACHE_TYPE_HT20 == protStaCacheType)
  {
      psessionEntry->gLimHt20Params.numSta++;
      limLog(pMac,LOG1, FL("HT20, "));
  }

  if(!gfSupported)
  {
     psessionEntry->gLimNonGfParams.numSta++;
      limLog(pMac,LOG1, FL("NonGf, "));
  }
  if(!lsigTxopSupported)
  {
      psessionEntry->gLimLsigTxopParams.numSta++;
      limLog(pMac,LOG1, FL("!lsigTxopSupported"));
  }
}// ---------------------------------------------------------------------

/** -------------------------------------------------------------
\fn limDecideApProtection
\brief Decides all the protection related staiton coexistence and also sets
\        short preamble and short slot appropriately. This function will be called
\        when AP is ready to send assocRsp tp the station joining right now.
\param      tpAniSirGlobal    pMac
\param      tSirMacAddr peerMacAddr
\return      None
  -------------------------------------------------------------*/
void
limDecideApProtection(tpAniSirGlobal pMac, tSirMacAddr peerMacAddr, tpUpdateBeaconParams pBeaconParams,tpPESession psessionEntry)
{
    tANI_U16              tmpAid;
    tpDphHashNode    pStaDs;
    tSirRFBand  rfBand = SIR_BAND_UNKNOWN;
    tANI_U32 phyMode;
    tLimProtStaCacheType protStaCacheType = eLIM_PROT_STA_CACHE_TYPE_INVALID;
    tHalBitVal gfSupported = eHAL_SET, lsigTxopSupported = eHAL_SET;

    pBeaconParams->paramChangeBitmap = 0;
    // check whether to enable protection or not
    pStaDs = dphLookupHashEntry(pMac, peerMacAddr, &tmpAid, &psessionEntry->dph.dphHashTable);
    if(NULL == pStaDs)
    {
      limLog(pMac, LOG1, FL("pStaDs is NULL"));
      return;
    }
    limGetRfBand(pMac, &rfBand, psessionEntry);
    //if we are in 5 GHZ band
    if(SIR_BAND_5_GHZ == rfBand)
    {
        //We are 11N. we need to protect from 11A and Ht20. we don't need any other protection in 5 GHZ.
        //HT20 case is common between both the bands and handled down as common code.
        if(true == psessionEntry->htCapability)
        {
            //we are 11N and 11A station is joining.        
            //protection from 11A required.            
            if(false == pStaDs->mlmStaContext.htCapability)
            {
                limEnable11aProtection(pMac, true, false, pBeaconParams,psessionEntry);
                return;
            }
        }
    }
    else if(SIR_BAND_2_4_GHZ== rfBand)
    {
        limGetPhyMode(pMac, &phyMode, psessionEntry);

        //We are 11G. Check if we need protection from 11b Stations.
        if ((phyMode == WNI_CFG_PHY_MODE_11G) &&
              (false == psessionEntry->htCapability))
        {

            if (pStaDs->erpEnabled== eHAL_CLEAR)
            {
                protStaCacheType = eLIM_PROT_STA_CACHE_TYPE_llB;
                // enable protection
                PELOG3(limLog(pMac, LOG3, FL("Enabling protection from 11B"));)
                limEnable11gProtection(pMac, true, false, pBeaconParams,psessionEntry);
            }
        }

        //HT station.
        if (true == psessionEntry->htCapability)
        {
            //check if we need protection from 11b station
            if ((pStaDs->erpEnabled == eHAL_CLEAR) &&
                (!pStaDs->mlmStaContext.htCapability))
            {
                protStaCacheType = eLIM_PROT_STA_CACHE_TYPE_llB;
                // enable protection
                PELOG3(limLog(pMac, LOG3, FL("Enabling protection from 11B"));)
                limEnable11gProtection(pMac, true, false, pBeaconParams, psessionEntry);
            }
            //station being joined is non-11b and non-ht ==> 11g device
            else if(!pStaDs->mlmStaContext.htCapability)
            {
                protStaCacheType = eLIM_PROT_STA_CACHE_TYPE_llG;
                //enable protection
                limEnableHtProtectionFrom11g(pMac, true, false, pBeaconParams, psessionEntry);
            }
            //ERP mode is enabled for the latest station joined
            //latest station joined is HT capable
            //This case is being handled in common code (commn between both the bands) below.
        }
    }

    //we are HT and HT station is joining. This code is common for both the bands.
    if((true == psessionEntry->htCapability) &&
        (true == pStaDs->mlmStaContext.htCapability))
    {
        if(!pStaDs->htGreenfield)
        {
          limEnableHTNonGfProtection(pMac, true, false, pBeaconParams, psessionEntry);
          gfSupported = eHAL_CLEAR;
        }
        //Station joining is HT 20Mhz
        if(eHT_CHANNEL_WIDTH_20MHZ == pStaDs->htSupportedChannelWidthSet)
        {
            protStaCacheType = eLIM_PROT_STA_CACHE_TYPE_HT20;
            limEnableHT20Protection(pMac, true, false, pBeaconParams, psessionEntry);
        }
        //Station joining does not support LSIG TXOP Protection
        if(!pStaDs->htLsigTXOPProtection)
        {
            limEnableHTLsigTxopProtection(pMac, false, false, pBeaconParams,psessionEntry);
            lsigTxopSupported = eHAL_CLEAR;
        }
    }

    limUpdateProtStaParams(pMac, peerMacAddr, protStaCacheType,
              gfSupported, lsigTxopSupported, psessionEntry);

    return;
}


/** -------------------------------------------------------------
\fn limEnableOverlap11gProtection
\brief wrapper function for setting overlap 11g protection.
\param      tpAniSirGlobal    pMac
\param      tpUpdateBeaconParams pBeaconParams
\param      tpSirMacMgmtHdr         pMh
\return      None
  -------------------------------------------------------------*/
void
limEnableOverlap11gProtection(tpAniSirGlobal pMac,
tpUpdateBeaconParams pBeaconParams, tpSirMacMgmtHdr pMh,tpPESession psessionEntry)
{
    limUpdateOverlapStaParam(pMac, pMh->bssId, &(psessionEntry->gLimOlbcParams));

    if (psessionEntry->gLimOlbcParams.numSta &&
        !psessionEntry->gLimOlbcParams.protectionEnabled)
    {
        // enable protection
        PELOG1(limLog(pMac, LOG1, FL("OLBC happens!!!"));)
        limEnable11gProtection(pMac, true, true, pBeaconParams,psessionEntry);
    }
}


/** -------------------------------------------------------------
\fn limUpdateShortPreamble
\brief Updates short preamble if needed when a new station joins.
\param      tpAniSirGlobal    pMac
\param      tSirMacAddr peerMacAddr
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/
void
limUpdateShortPreamble(tpAniSirGlobal pMac, tSirMacAddr peerMacAddr,
    tpUpdateBeaconParams pBeaconParams, tpPESession psessionEntry)
{
    tANI_U16         tmpAid;
    tpDphHashNode    pStaDs;
    tANI_U32         phyMode;
    tANI_U16         i;

    // check whether to enable protection or not
    pStaDs = dphLookupHashEntry(pMac, peerMacAddr, &tmpAid, &psessionEntry->dph.dphHashTable);

    limGetPhyMode(pMac, &phyMode, psessionEntry);

    if (pStaDs != NULL && phyMode == WNI_CFG_PHY_MODE_11G)

    {        
        if (pStaDs->shortPreambleEnabled == eHAL_CLEAR)
        {
            PELOG1(limLog(pMac,LOG1,FL("Short Preamble is not enabled in Assoc Req from "));
                    limPrintMacAddr(pMac, peerMacAddr, LOG1);)

                for (i=0; i<LIM_PROT_STA_CACHE_SIZE; i++)
                {
                if ((psessionEntry->limSystemRole == eLIM_AP_ROLE )  &&
                     psessionEntry->gLimNoShortParams.staNoShortCache[i].active)
                    {
                        if (vos_mem_compare(
                                    psessionEntry->gLimNoShortParams.staNoShortCache[i].addr,
                                    peerMacAddr, sizeof(tSirMacAddr)))
                            return;
                }else if(psessionEntry->limSystemRole != eLIM_AP_ROLE)
                {
                    if (pMac->lim.gLimNoShortParams.staNoShortCache[i].active)
                     {
                         if (vos_mem_compare(
                                             pMac->lim.gLimNoShortParams.staNoShortCache[i].addr,
                                             peerMacAddr, sizeof(tSirMacAddr)))
                             return;
                      }
                    }
                }


            for (i=0; i<LIM_PROT_STA_CACHE_SIZE; i++)
            {
                if ( (psessionEntry->limSystemRole == eLIM_AP_ROLE )  &&
                      !psessionEntry->gLimNoShortParams.staNoShortCache[i].active)
                     break;
                else        
                {
                    if (!pMac->lim.gLimNoShortParams.staNoShortCache[i].active)
                    break;
                }
            }

            if (i >= LIM_PROT_STA_CACHE_SIZE)
            {
                if(psessionEntry->limSystemRole == eLIM_AP_ROLE){
                    limLog(pMac, LOGE, FL("No space in Short cache (#active %d, #sta %d) for sta "),
                            i, psessionEntry->gLimNoShortParams.numNonShortPreambleSta);
                    limPrintMacAddr(pMac, peerMacAddr, LOGE);
                    return;
                }
                else
                {
                    limLog(pMac, LOGE, FL("No space in Short cache (#active %d, #sta %d) for sta "),
                            i, pMac->lim.gLimNoShortParams.numNonShortPreambleSta);
                    limPrintMacAddr(pMac, peerMacAddr, LOGE);
                    return;
                }

            }


            if (psessionEntry->limSystemRole == eLIM_AP_ROLE){
                vos_mem_copy( psessionEntry->gLimNoShortParams.staNoShortCache[i].addr,
                        peerMacAddr,  sizeof(tSirMacAddr));
                psessionEntry->gLimNoShortParams.staNoShortCache[i].active = true;
                psessionEntry->gLimNoShortParams.numNonShortPreambleSta++;
            }else
            {
                vos_mem_copy(  pMac->lim.gLimNoShortParams.staNoShortCache[i].addr,
                               peerMacAddr,  sizeof(tSirMacAddr));
                pMac->lim.gLimNoShortParams.staNoShortCache[i].active = true;
                pMac->lim.gLimNoShortParams.numNonShortPreambleSta++;        
            } 


            // enable long preamble
            PELOG1(limLog(pMac, LOG1, FL("Disabling short preamble"));)

            if (limEnableShortPreamble(pMac, false, pBeaconParams, psessionEntry) != eSIR_SUCCESS)
                PELOGE(limLog(pMac, LOGE, FL("Cannot enable long preamble"));)
        }
    }
}

/** -------------------------------------------------------------
\fn limUpdateShortSlotTime
\brief Updates short slot time if needed when a new station joins.
\param      tpAniSirGlobal    pMac
\param      tSirMacAddr peerMacAddr
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/

void
limUpdateShortSlotTime(tpAniSirGlobal pMac, tSirMacAddr peerMacAddr,
    tpUpdateBeaconParams pBeaconParams, tpPESession psessionEntry)
{
    tANI_U16              tmpAid;
    tpDphHashNode    pStaDs;
    tANI_U32 phyMode;
    tANI_U32 val;
    tANI_U16 i;

    // check whether to enable protection or not
    pStaDs = dphLookupHashEntry(pMac, peerMacAddr, &tmpAid, &psessionEntry->dph.dphHashTable);
    limGetPhyMode(pMac, &phyMode, psessionEntry);

    /* Only in case of softap in 11g mode, slot time might change depending on the STA being added. In 11a case, it should
     * be always 1 and in 11b case, it should be always 0
     */
    if (pStaDs != NULL && phyMode == WNI_CFG_PHY_MODE_11G)
    {
        /* Only when the new STA has short slot time disabled, we need to change softap's overall slot time settings
         * else the default for softap is always short slot enabled. When the last long slot STA leaves softAP, we take care of
         * it in limDecideShortSlot
         */
        if (pStaDs->shortSlotTimeEnabled == eHAL_CLEAR)
        {
            PELOG1(limLog(pMac, LOG1, FL("Short Slot Time is not enabled in Assoc Req from "));
            limPrintMacAddr(pMac, peerMacAddr, LOG1);)
            for (i=0; i<LIM_PROT_STA_CACHE_SIZE; i++)
            {
                if ((psessionEntry->limSystemRole == eLIM_AP_ROLE ) &&
                     psessionEntry->gLimNoShortSlotParams.staNoShortSlotCache[i].active)
                {
                    if (vos_mem_compare(
                         psessionEntry->gLimNoShortSlotParams.staNoShortSlotCache[i].addr,
                         peerMacAddr, sizeof(tSirMacAddr)))
                        return;
                }
                else if(psessionEntry->limSystemRole != eLIM_AP_ROLE )
                {
                    if (pMac->lim.gLimNoShortSlotParams.staNoShortSlotCache[i].active)
                    {
                        if (vos_mem_compare(
                            pMac->lim.gLimNoShortSlotParams.staNoShortSlotCache[i].addr,
                            peerMacAddr, sizeof(tSirMacAddr)))
                            return;
                     }
                }
            }

            for (i=0; i<LIM_PROT_STA_CACHE_SIZE; i++)
            {
                if ((psessionEntry->limSystemRole == eLIM_AP_ROLE ) &&
                     !psessionEntry->gLimNoShortSlotParams.staNoShortSlotCache[i].active)
                    break;
                 else
                 {
                     if (!pMac->lim.gLimNoShortSlotParams.staNoShortSlotCache[i].active)
                          break;
                 }
            }

            if (i >= LIM_PROT_STA_CACHE_SIZE)
            {
                if(psessionEntry->limSystemRole == eLIM_AP_ROLE){
                    limLog(pMac, LOGE, FL("No space in ShortSlot cache (#active %d, #sta %d) for sta "),
                            i, psessionEntry->gLimNoShortSlotParams.numNonShortSlotSta);
                    limPrintMacAddr(pMac, peerMacAddr, LOGE);
                    return;
                }else
                {
                    limLog(pMac, LOGE, FL("No space in ShortSlot cache (#active %d, #sta %d) for sta "),
                           i, pMac->lim.gLimNoShortSlotParams.numNonShortSlotSta);
                    limPrintMacAddr(pMac, peerMacAddr, LOGE);
                    return;
                }
            }


            if(psessionEntry->limSystemRole == eLIM_AP_ROLE){
                vos_mem_copy(  psessionEntry->gLimNoShortSlotParams.staNoShortSlotCache[i].addr,
                               peerMacAddr, sizeof(tSirMacAddr));
                psessionEntry->gLimNoShortSlotParams.staNoShortSlotCache[i].active = true;
                psessionEntry->gLimNoShortSlotParams.numNonShortSlotSta++;
            }else
            {
                vos_mem_copy( pMac->lim.gLimNoShortSlotParams.staNoShortSlotCache[i].addr,
                          peerMacAddr, sizeof(tSirMacAddr));
                pMac->lim.gLimNoShortSlotParams.staNoShortSlotCache[i].active = true;
                pMac->lim.gLimNoShortSlotParams.numNonShortSlotSta++;
            }
            wlan_cfgGetInt(pMac, WNI_CFG_11G_SHORT_SLOT_TIME_ENABLED, &val);

            /* Here we check if we are AP role and short slot enabled (both admin and oper modes) but we have atleast one STA connected with
             * only long slot enabled, we need to change our beacon/pb rsp to broadcast short slot disabled
             */
            if ( (psessionEntry->limSystemRole == eLIM_AP_ROLE) && 
                 (val && psessionEntry->gLimNoShortSlotParams.numNonShortSlotSta && psessionEntry->shortSlotTimeSupported))
            {
                // enable long slot time
                pBeaconParams->fShortSlotTime = false;
                pBeaconParams->paramChangeBitmap |= PARAM_SHORT_SLOT_TIME_CHANGED;
                PELOG1(limLog(pMac, LOG1, FL("Disable short slot time. Enable long slot time."));)
                psessionEntry->shortSlotTimeSupported = false;
            }
            else if ( psessionEntry->limSystemRole != eLIM_AP_ROLE)
            {
                if (val && pMac->lim.gLimNoShortSlotParams.numNonShortSlotSta && psessionEntry->shortSlotTimeSupported)
                {
                    // enable long slot time
                    pBeaconParams->fShortSlotTime = false;
                    pBeaconParams->paramChangeBitmap |= PARAM_SHORT_SLOT_TIME_CHANGED;
                    PELOG1(limLog(pMac, LOG1, FL("Disable short slot time. Enable long slot time."));)
                    psessionEntry->shortSlotTimeSupported = false;
                 }
            }
        }
    }
}


/** -------------------------------------------------------------
\fn limDecideStaProtectionOnAssoc
\brief Decide protection related settings on Sta while association.
\param      tpAniSirGlobal    pMac
\param      tpSchBeaconStruct pBeaconStruct
\return      None
  -------------------------------------------------------------*/
void
limDecideStaProtectionOnAssoc(tpAniSirGlobal pMac,
    tpSchBeaconStruct pBeaconStruct, tpPESession psessionEntry)
{
    tSirRFBand rfBand = SIR_BAND_UNKNOWN;
    tANI_U32 phyMode = WNI_CFG_PHY_MODE_NONE;

    limGetRfBand(pMac, &rfBand, psessionEntry);
    limGetPhyMode(pMac, &phyMode, psessionEntry);

    if(SIR_BAND_5_GHZ == rfBand)
    {
        if((eSIR_HT_OP_MODE_MIXED == pBeaconStruct->HTInfo.opMode)  ||
                    (eSIR_HT_OP_MODE_OVERLAP_LEGACY == pBeaconStruct->HTInfo.opMode))
        {
            if(pMac->lim.cfgProtection.fromlla)
                psessionEntry->beaconParams.llaCoexist = true;
        }
        else if(eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT == pBeaconStruct->HTInfo.opMode)
        {
            if(pMac->lim.cfgProtection.ht20)
                psessionEntry->beaconParams.ht20Coexist = true;
        }

    }
    else if(SIR_BAND_2_4_GHZ == rfBand)
    {
        //spec 7.3.2.13
        //UseProtection will be set when nonERP STA is associated.
        //NonERPPresent bit will be set when:
        //--nonERP Sta is associated OR
        //--nonERP Sta exists in overlapping BSS
        //when useProtection is not set then protection from nonERP stations is optional.

        //CFG protection from 11b is enabled and
        //11B device in the BSS
         /* TODO, This is not sessionized */
        if (phyMode != WNI_CFG_PHY_MODE_11B) 
        {
            if (pMac->lim.cfgProtection.fromllb &&
                pBeaconStruct->erpPresent &&
                (pBeaconStruct->erpIEInfo.useProtection ||
                pBeaconStruct->erpIEInfo.nonErpPresent))
            {
                psessionEntry->beaconParams.llbCoexist = true;
            }
            //AP has no 11b station associated.
            else
            {
                psessionEntry->beaconParams.llbCoexist = false;
            }
        }
        //following code block is only for HT station.
        if((psessionEntry->htCapability) &&
              (pBeaconStruct->HTInfo.present))
        {
            tDot11fIEHTInfo htInfo = pBeaconStruct->HTInfo;
           
            //Obss Non HT STA present mode 
            psessionEntry->beaconParams.gHTObssMode =  (tANI_U8)htInfo.obssNonHTStaPresent;

            
          //CFG protection from 11G is enabled and
            //our AP has at least one 11G station associated.       
            if(pMac->lim.cfgProtection.fromllg &&
                  ((eSIR_HT_OP_MODE_MIXED == htInfo.opMode)  ||
                        (eSIR_HT_OP_MODE_OVERLAP_LEGACY == htInfo.opMode))&&
                      (!psessionEntry->beaconParams.llbCoexist))
            {
                if(pMac->lim.cfgProtection.fromllg)
                    psessionEntry->beaconParams.llgCoexist = true;
            }

            //AP has only HT stations associated and at least one station is HT 20
            //disable protection from any non-HT devices.
            //decision for disabling protection from 11b has already been taken above.
            if(eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT == htInfo.opMode)
            {
                //Disable protection from 11G station.
                psessionEntry->beaconParams.llgCoexist = false;
          //CFG protection from HT 20 is enabled.
          if(pMac->lim.cfgProtection.ht20)
                psessionEntry->beaconParams.ht20Coexist = true;
            }
            //Disable protection from non-HT and HT20 devices.
            //decision for disabling protection from 11b has already been taken above.
            if(eSIR_HT_OP_MODE_PURE == htInfo.opMode)
            {
                psessionEntry->beaconParams.llgCoexist = false;
                psessionEntry->beaconParams.ht20Coexist = false;
            }

        }
    }

    //protection related factors other than HT operating mode. Applies to 2.4 GHZ as well as 5 GHZ.
    if((psessionEntry->htCapability) &&
          (pBeaconStruct->HTInfo.present))
    {
        tDot11fIEHTInfo htInfo = pBeaconStruct->HTInfo;
        psessionEntry->beaconParams.fRIFSMode = 
            ( tANI_U8 ) htInfo.rifsMode;
        psessionEntry->beaconParams.llnNonGFCoexist = 
            ( tANI_U8 )htInfo.nonGFDevicesPresent;
        psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport = 
            ( tANI_U8 )htInfo.lsigTXOPProtectionFullSupport;
    }
}


/** -------------------------------------------------------------
\fn limDecideStaProtection
\brief Decides protection related settings on Sta while processing beacon.
\param      tpAniSirGlobal    pMac
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/
void
limDecideStaProtection(tpAniSirGlobal pMac,
    tpSchBeaconStruct pBeaconStruct, tpUpdateBeaconParams pBeaconParams, tpPESession psessionEntry)
{

    tSirRFBand rfBand = SIR_BAND_UNKNOWN;
    tANI_U32 phyMode = WNI_CFG_PHY_MODE_NONE;

    limGetRfBand(pMac, &rfBand, psessionEntry);
    limGetPhyMode(pMac, &phyMode, psessionEntry);
       
    if(SIR_BAND_5_GHZ == rfBand)
    {
        //we are HT capable.
        if((true == psessionEntry->htCapability) &&
            (pBeaconStruct->HTInfo.present))
        {
            //we are HT capable, AP's HT OPMode is mixed / overlap legacy ==> need protection from 11A.        
            if((eSIR_HT_OP_MODE_MIXED == pBeaconStruct->HTInfo.opMode) ||
              (eSIR_HT_OP_MODE_OVERLAP_LEGACY == pBeaconStruct->HTInfo.opMode))
            {
                limEnable11aProtection(pMac, true, false, pBeaconParams,psessionEntry);
            }
            //we are HT capable, AP's HT OPMode is HT20 ==> disable protection from 11A if enabled. enabled 
            //protection from HT20 if needed.
            else if(eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT== pBeaconStruct->HTInfo.opMode)
            {
                limEnable11aProtection(pMac, false, false, pBeaconParams,psessionEntry);            
                limEnableHT20Protection(pMac, true, false, pBeaconParams,psessionEntry);
            }
            else if(eSIR_HT_OP_MODE_PURE == pBeaconStruct->HTInfo.opMode)
            {
                limEnable11aProtection(pMac, false, false, pBeaconParams,psessionEntry);            
                limEnableHT20Protection(pMac, false, false, pBeaconParams,psessionEntry);
            }
        }
    }
    else if(SIR_BAND_2_4_GHZ == rfBand)
    {
        /* spec 7.3.2.13
         * UseProtection will be set when nonERP STA is associated.
         * NonERPPresent bit will be set when:
         * --nonERP Sta is associated OR
         * --nonERP Sta exists in overlapping BSS
         * when useProtection is not set then protection from nonERP stations is optional.
         */

        if (phyMode != WNI_CFG_PHY_MODE_11B) 
        {
            if (pBeaconStruct->erpPresent &&
                  (pBeaconStruct->erpIEInfo.useProtection ||
                  pBeaconStruct->erpIEInfo.nonErpPresent))
            {
                limEnable11gProtection(pMac, true, false, pBeaconParams, psessionEntry);
            }
            //AP has no 11b station associated.
            else
            {
                //disable protection from 11b station
                limEnable11gProtection(pMac, false, false, pBeaconParams, psessionEntry);
            }
         }

        //following code block is only for HT station.
        if((psessionEntry->htCapability) &&
              (pBeaconStruct->HTInfo.present))
        {
          
            tDot11fIEHTInfo htInfo = pBeaconStruct->HTInfo;
            //AP has at least one 11G station associated.
            if(((eSIR_HT_OP_MODE_MIXED == htInfo.opMode)  ||
                  (eSIR_HT_OP_MODE_OVERLAP_LEGACY == htInfo.opMode))&&
                (!psessionEntry->beaconParams.llbCoexist))
            {
                limEnableHtProtectionFrom11g(pMac, true, false, pBeaconParams,psessionEntry);
        
            }

            //no HT operating mode change  ==> no change in protection settings except for MIXED_MODE/Legacy Mode.
            //in Mixed mode/legacy Mode even if there is no change in HT operating mode, there might be change in 11bCoexist
            //or 11gCoexist. that is why this check is being done after mixed/legacy mode check.
            if ( pMac->lim.gHTOperMode != ( tSirMacHTOperatingMode )htInfo.opMode )
            {
                pMac->lim.gHTOperMode       = ( tSirMacHTOperatingMode )htInfo.opMode;

                 //AP has only HT stations associated and at least one station is HT 20
                 //disable protection from any non-HT devices.
                 //decision for disabling protection from 11b has already been taken above.
                if(eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT == htInfo.opMode)
                {
                    //Disable protection from 11G station.
                    limEnableHtProtectionFrom11g(pMac, false, false, pBeaconParams,psessionEntry);
        
                    limEnableHT20Protection(pMac, true, false, pBeaconParams,psessionEntry);
                }
                //Disable protection from non-HT and HT20 devices.
                //decision for disabling protection from 11b has already been taken above.
                else if(eSIR_HT_OP_MODE_PURE == htInfo.opMode)
                {
                    limEnableHtProtectionFrom11g(pMac, false, false, pBeaconParams,psessionEntry);
                    limEnableHT20Protection(pMac, false, false, pBeaconParams,psessionEntry);
            
                }
            }
        }
    }

    //following code block is only for HT station. ( 2.4 GHZ as well as 5 GHZ)
    if((psessionEntry->htCapability) &&
          (pBeaconStruct->HTInfo.present))
    {
        tDot11fIEHTInfo htInfo = pBeaconStruct->HTInfo;    
        //Check for changes in protection related factors other than HT operating mode.
        //Check for changes in RIFS mode, nonGFDevicesPresent, lsigTXOPProtectionFullSupport.
        if ( psessionEntry->beaconParams.fRIFSMode != 
                ( tANI_U8 ) htInfo.rifsMode )
        {
            pBeaconParams->fRIFSMode = 
                psessionEntry->beaconParams.fRIFSMode  = 
                ( tANI_U8 ) htInfo.rifsMode;
            pBeaconParams->paramChangeBitmap |= PARAM_RIFS_MODE_CHANGED;
        }

        if ( psessionEntry->beaconParams.llnNonGFCoexist != 
                htInfo.nonGFDevicesPresent )
        {
            pBeaconParams->llnNonGFCoexist = 
                psessionEntry->beaconParams.llnNonGFCoexist = 
                ( tANI_U8 )htInfo.nonGFDevicesPresent;
            pBeaconParams->paramChangeBitmap |= 
                PARAM_NON_GF_DEVICES_PRESENT_CHANGED;
        }

        if ( psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport != 
                ( tANI_U8 )htInfo.lsigTXOPProtectionFullSupport )
        {
            pBeaconParams->fLsigTXOPProtectionFullSupport =  
                psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport = 
                ( tANI_U8 )htInfo.lsigTXOPProtectionFullSupport;
            pBeaconParams->paramChangeBitmap |= 
                PARAM_LSIG_TXOP_FULL_SUPPORT_CHANGED;
        }
        
    // For Station just update the global lim variable, no need to send message to HAL
    // Station already taking care of HT OPR Mode=01, meaning AP is seeing legacy
    //stations in overlapping BSS.
       if ( psessionEntry->beaconParams.gHTObssMode != ( tANI_U8 )htInfo.obssNonHTStaPresent )
            psessionEntry->beaconParams.gHTObssMode = ( tANI_U8 )htInfo.obssNonHTStaPresent ;
            
    }
}


/**
 * limProcessChannelSwitchTimeout()
 *
 *FUNCTION:
 * This function is invoked when Channel Switch Timer expires at
 * the STA.  Now, STA must stop traffic, and then change/disable
 * primary or secondary channel.
 *
 *
 *NOTE:
 * @param  pMac           - Pointer to Global MAC structure
 * @return None
 */
void limProcessChannelSwitchTimeout(tpAniSirGlobal pMac)
{
    tpPESession psessionEntry = NULL;
    tANI_U8    channel; // This is received and stored from channelSwitch Action frame
   
    if ((psessionEntry = peFindSessionBySessionId(pMac,
       pMac->lim.limTimers.gLimChannelSwitchTimer.sessionId))== NULL) {
        limLog(pMac, LOGW,FL("Session Does not exist for given sessionID"));
        return;
    }

    if (psessionEntry->limSystemRole != eLIM_STA_ROLE)
    {
        PELOGW(limLog(pMac, LOGW, "Channel switch can be done only in STA role, Current Role = %d", psessionEntry->limSystemRole);)
        return;
    }
    if (psessionEntry->gLimSpecMgmt.dot11hChanSwState !=
                                  eLIM_11H_CHANSW_RUNNING) {
        limLog(pMac, LOGW,
            FL("Channel switch timer should not have been running in state %d"),
            psessionEntry->gLimSpecMgmt.dot11hChanSwState);
        return;
    }
    channel = psessionEntry->gLimChannelSwitch.primaryChannel;

    /*
     *  This potentially can create issues if the function tries to set
     * channel while device is in power-save, hence putting an extra check
     * to verify if the device is in power-save or not
     */
    if(!limIsSystemInActiveState(pMac))
    {
        PELOGW(limLog(pMac, LOGW, FL("Device is not in active state, cannot switch channel"));)
        return;
    }
         
    // Restore Channel Switch parameters to default
    psessionEntry->gLimChannelSwitch.switchTimeoutValue = 0;

    /* Channel-switch timeout has occurred. reset the state */
    psessionEntry->gLimSpecMgmt.dot11hChanSwState = eLIM_11H_CHANSW_END;

    /*
     * If Lim allows Switch channel on same channel on which preauth
     * is going on then LIM will not post resume link(WDA_FINISH_SCAN)
     * during preauth rsp handling hence firmware may crash on ENTER/
     * EXIT BMPS request.
     */
    if(pMac->ft.ftPEContext.pFTPreAuthReq)
    {
        limLog(pMac, LOGE,
           FL("Avoid Switch Channel req during pre auth"));
        return;
    }
    /* If link is already suspended mean some off
     * channel operation or scan is in progress, Allowing
     * Change channel here will lead to either Init Scan
     * sent twice or missing Finish scan when change
     * channel is completed, this may lead
     * to driver in invalid state and crash.
     */
    if (limIsLinkSuspended(pMac))
    {
       limLog(pMac, LOGE, FL("Link is already suspended for "
               "some other reason. Return here for sessionId:%d"),
               pMac->lim.limTimers.gLimChannelSwitchTimer.sessionId);
       return;
    }

    /* Check if the AP is switching to a channel that we support.
     * Else, just don't bother to switch. Indicate HDD to look for a 
     * better AP to associate
     */
    if(!limIsChannelValidForChannelSwitch(pMac, channel))
    {
        /* We need to restore pre-channelSwitch state on the STA */
        if(limRestorePreChannelSwitchState(pMac, psessionEntry) != eSIR_SUCCESS)
        {
            limLog(pMac, LOGP, FL("Could not restore pre-channelSwitch (11h) state, resetting the system"));
            return;
        }

        /* If the channel-list that AP is asking us to switch is invalid,
         * then we cannot switch the channel. Just disassociate from AP. 
         * We will find a better AP !!!
         */
        if ((psessionEntry->limMlmState == eLIM_MLM_LINK_ESTABLISHED_STATE) &&
           (psessionEntry->limSmeState != eLIM_SME_WT_DISASSOC_STATE)&&
           (psessionEntry->limSmeState != eLIM_SME_WT_DEAUTH_STATE)) {
              limLog(pMac, LOGE, FL("Invalid channel!! Disconnect.."));
              limTearDownLinkWithAp(pMac,
                        pMac->lim.limTimers.gLimChannelSwitchTimer.sessionId,
                        eSIR_MAC_UNSPEC_FAILURE_REASON);
        }
        return;
    }
    limCovertChannelScanType(pMac, psessionEntry->currentOperChannel, false);
    pMac->lim.dfschannelList.timeStamp[psessionEntry->currentOperChannel] = 0;
    switch(psessionEntry->gLimChannelSwitch.state)
    {
        case eLIM_CHANNEL_SWITCH_PRIMARY_ONLY:
        case eLIM_CHANNEL_SWITCH_PRIMARY_AND_SECONDARY:
            if ( isLimSessionOffChannel(pMac,
                pMac->lim.limTimers.gLimChannelSwitchTimer.sessionId) )
            {
                limSuspendLink(pMac,
                    eSIR_DONT_CHECK_LINK_TRAFFIC_BEFORE_SCAN,
                    limProcessChannelSwitchSuspendLink,
                    (tANI_U32*)psessionEntry );
            }
            else
            {
                limProcessChannelSwitchSuspendLink(pMac,
                    eHAL_STATUS_SUCCESS,
                    (tANI_U32*)psessionEntry);
            }
            break;

        case eLIM_CHANNEL_SWITCH_SECONDARY_ONLY:
            PELOGW(limLog(pMac, LOGW, FL("CHANNEL_SWITCH_SECONDARY_ONLY "));)
            limSwitchPrimarySecondaryChannel(pMac, psessionEntry,
                                             psessionEntry->currentOperChannel,
                                             psessionEntry->gLimChannelSwitch.secondarySubBand);
            psessionEntry->gLimChannelSwitch.state = eLIM_CHANNEL_SWITCH_IDLE;
            break;
        case eLIM_CHANNEL_SWITCH_IDLE:
        default:
            PELOGE(limLog(pMac, LOGE, FL("incorrect state "));)
            if(limRestorePreChannelSwitchState(pMac, psessionEntry) != eSIR_SUCCESS)
            {
                limLog(pMac, LOGP, FL("Could not restore pre-channelSwitch (11h) state, resetting the system"));
            }
            return;  /* Please note, this is 'return' and not 'break' */
    }
}

/**
 * lim_process_ecsa_ie()- Process ECSA IE in beacon/ probe resp
 * @mac_ctx: pointer to global mac structure
 * @ecsa_ie: ecsa ie
 * @session: Session entry.
 *
 * This function is called when ECSA IE is received on STA interface.
 *
 * Return: void
 */
static void
lim_process_ecsa_ie(tpAniSirGlobal mac_ctx,
     tDot11fIEext_chan_switch_ann *ecsa_ie, tpPESession session)
{
    struct ecsa_frame_params ecsa_req;

    limLog(mac_ctx, LOG1, FL("Received ECSA IE in beacon/probe resp"));

    if (session->currentOperChannel == ecsa_ie->new_channel) {
        limLog(mac_ctx, LOGE, FL("New channel %d is same as old channel ignore req"),
               ecsa_ie->new_channel);
        return;
    }

    ecsa_req.new_channel = ecsa_ie->new_channel;
    ecsa_req.op_class = ecsa_ie->new_reg_class;
    ecsa_req.switch_mode = ecsa_ie->switch_mode;
    ecsa_req.switch_count = ecsa_ie->switch_count;
    limLog(mac_ctx, LOG1, FL("New channel %d op class %d switch mode %d switch count %d"),
           ecsa_req.new_channel, ecsa_req.op_class,
           ecsa_req.switch_mode, ecsa_req.switch_count);

    lim_handle_ecsa_req(mac_ctx, &ecsa_req, session);
}


/**
 * limUpdateChannelSwitch()
 *
 *FUNCTION:
 * This function is invoked whenever Station receives
 * either 802.11h channel switch IE or airgo proprietary
 * channel switch IE.
 *
 *NOTE:
 * @param  pMac           - Pointer to Global MAC structure
 * @return  tpSirProbeRespBeacon - Pointer to Beacon/Probe Rsp
 * @param psessionentry
 */
void
limUpdateChannelSwitch(struct sAniSirGlobal *pMac,  tpSirProbeRespBeacon pBeacon, tpPESession psessionEntry)
{

    tANI_U16                         beaconPeriod;
    tChannelSwitchPropIEStruct       *pPropChnlSwitch;
    tDot11fIEChanSwitchAnn           *pChnlSwitch;
#ifdef WLAN_FEATURE_11AC
    tDot11fIEWiderBWChanSwitchAnn    *pWiderChnlSwitch;
#endif
   if (pBeacon->ecsa_present)
       return lim_process_ecsa_ie(pMac,
                                  &pBeacon->ext_chan_switch_ann, psessionEntry);

    beaconPeriod = psessionEntry->beaconParams.beaconInterval;

    /* STA either received proprietary channel switch IE or 802.11h
     * standard channel switch IE.
     */
    if (pBeacon->propIEinfo.propChannelSwitchPresent)
    {
        pPropChnlSwitch = &(pBeacon->propIEinfo.channelSwitch);

        /* Add logic to determine which change this is:  */
        /*      primary, secondary, both.  For now assume both. */
        psessionEntry->gLimChannelSwitch.state = eLIM_CHANNEL_SWITCH_PRIMARY_AND_SECONDARY;
        psessionEntry->gLimChannelSwitch.primaryChannel = pPropChnlSwitch->primaryChannel;
        psessionEntry->gLimChannelSwitch.secondarySubBand = (ePhyChanBondState)pPropChnlSwitch->subBand;
        psessionEntry->gLimChannelSwitch.switchCount = pPropChnlSwitch->channelSwitchCount;
        psessionEntry->gLimChannelSwitch.switchTimeoutValue =
                 SYS_MS_TO_TICKS(beaconPeriod)* (pPropChnlSwitch->channelSwitchCount);
        psessionEntry->gLimChannelSwitch.switchMode = pPropChnlSwitch->mode;
    }
    else
    {
       pChnlSwitch = &(pBeacon->channelSwitchIE);
       psessionEntry->gLimChannelSwitch.primaryChannel = pChnlSwitch->newChannel;
       psessionEntry->gLimChannelSwitch.switchCount = pChnlSwitch->switchCount;
       psessionEntry->gLimChannelSwitch.switchTimeoutValue =
                 SYS_MS_TO_TICKS(beaconPeriod)* (pChnlSwitch->switchCount);
       psessionEntry->gLimChannelSwitch.switchMode = pChnlSwitch->switchMode; 
#ifdef WLAN_FEATURE_11AC
       pWiderChnlSwitch = &(pBeacon->WiderBWChanSwitchAnn);
       if(pBeacon->WiderBWChanSwitchAnnPresent) 
       {
           psessionEntry->gLimWiderBWChannelSwitch.newChanWidth = pWiderChnlSwitch->newChanWidth;
           psessionEntry->gLimWiderBWChannelSwitch.newCenterChanFreq0 = pWiderChnlSwitch->newCenterChanFreq0;
           psessionEntry->gLimWiderBWChannelSwitch.newCenterChanFreq1 = pWiderChnlSwitch->newCenterChanFreq1;
       }
#endif

        /* Only primary channel switch element is present */
        psessionEntry->gLimChannelSwitch.state = eLIM_CHANNEL_SWITCH_PRIMARY_ONLY;
        psessionEntry->gLimChannelSwitch.secondarySubBand = PHY_SINGLE_CHANNEL_CENTERED;

        /* Do not bother to look and operate on extended channel switch element
         * if our own channel-bonding state is not enabled
         */
        if (psessionEntry->htSupportedChannelWidthSet)
        {
            if (pBeacon->sec_chan_offset_present)
            {
                if ((pBeacon->sec_chan_offset.secondaryChannelOffset == PHY_DOUBLE_CHANNEL_LOW_PRIMARY) ||
                    (pBeacon->sec_chan_offset.secondaryChannelOffset == PHY_DOUBLE_CHANNEL_HIGH_PRIMARY))
                {
                    psessionEntry->gLimChannelSwitch.state = eLIM_CHANNEL_SWITCH_PRIMARY_AND_SECONDARY;
                    psessionEntry->gLimChannelSwitch.secondarySubBand = pBeacon->sec_chan_offset.secondaryChannelOffset;
                }
#ifdef WLAN_FEATURE_11AC
                if(psessionEntry->vhtCapability && pBeacon->WiderBWChanSwitchAnnPresent)
                {
                    if (pWiderChnlSwitch->newChanWidth == WNI_CFG_VHT_CHANNEL_WIDTH_80MHZ)
                    {
                        if(pBeacon->sec_chan_offset_present)
                        {
                            if ((pBeacon->sec_chan_offset.secondaryChannelOffset == PHY_DOUBLE_CHANNEL_LOW_PRIMARY) ||
                                (pBeacon->sec_chan_offset.secondaryChannelOffset == PHY_DOUBLE_CHANNEL_HIGH_PRIMARY))
                            {
                                psessionEntry->gLimChannelSwitch.state = eLIM_CHANNEL_SWITCH_PRIMARY_AND_SECONDARY;
                                psessionEntry->gLimChannelSwitch.secondarySubBand = limGet11ACPhyCBState(pMac, 
                                                                                                         psessionEntry->gLimChannelSwitch.primaryChannel,
                                                                                                         pBeacon->sec_chan_offset.secondaryChannelOffset,
                                                                                                         pWiderChnlSwitch->newCenterChanFreq0,
                                                                                                         psessionEntry);
                            }
                        }
                    }
                }
#endif
            }
        }
     }


    if (eSIR_SUCCESS != limStartChannelSwitch(pMac, psessionEntry))
    {
        PELOGW(limLog(pMac, LOGW, FL("Could not start Channel Switch"));)
    }

    limLog(pMac, LOGW,
        FL("session %d primary chl %d, subband %d, count  %d (%d ticks) "),
        psessionEntry->peSessionId,
        psessionEntry->gLimChannelSwitch.primaryChannel,
        psessionEntry->gLimChannelSwitch.secondarySubBand,
        psessionEntry->gLimChannelSwitch.switchCount,
        psessionEntry->gLimChannelSwitch.switchTimeoutValue);
    return;
}

/**
 * lim_select_cbmode()- select cb mode for the channel and BW
 * @sta_ds: peer sta
 * @channel: channel
 * @chan_bw: BW
 *
 * Return: cb mode for a channel and BW
 */
static inline int lim_select_cbmode(tDphHashNode *sta_ds, uint8_t channel,
       uint8_t chan_bw)
{
    if (sta_ds->mlmStaContext.vhtCapability && chan_bw) {
        if (channel== 36 || channel == 52 || channel == 100 ||
            channel == 116 || channel == 149)
           return PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_LOW;
        else if (channel == 40 || channel == 56 || channel == 104 ||
             channel == 120 || channel == 153)
           return PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_LOW;
        else if (channel == 44 || channel == 60 || channel == 108 ||
                 channel == 124 || channel == 157)
           return PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_HIGH;
        else if (channel == 48 || channel == 64 || channel == 112 ||
             channel == 128 || channel == 161)
            return PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_HIGH;
        else if (channel == 165)
            return PHY_SINGLE_CHANNEL_CENTERED;
    } else if (sta_ds->mlmStaContext.htCapability) {
        if (channel== 40 || channel == 48 || channel == 56 ||
             channel == 64 || channel == 104 || channel == 112 ||
             channel == 120 || channel == 128 || channel == 136 ||
             channel == 144 || channel == 153 || channel == 161)
           return PHY_DOUBLE_CHANNEL_HIGH_PRIMARY;
        else if (channel== 36 || channel == 44 || channel == 52 ||
             channel == 60 || channel == 100 || channel == 108 ||
             channel == 116 || channel == 124 || channel == 132 ||
             channel == 140 || channel == 149 || channel == 157)
           return PHY_DOUBLE_CHANNEL_LOW_PRIMARY;
        else if (channel == 165)
           return PHY_SINGLE_CHANNEL_CENTERED;
    }
    return PHY_SINGLE_CHANNEL_CENTERED;
}

void lim_handle_ecsa_req(tpAniSirGlobal mac_ctx, struct ecsa_frame_params *ecsa_req,
              tpPESession session)
{
   offset_t ch_offset;
   tpDphHashNode sta_ds = NULL ;
   uint16_t aid = 0;

   if (!LIM_IS_STA_ROLE(session)) {
       limLog(mac_ctx, LOGE, FL("Session not in sta role"));
       return;
   }

   sta_ds = dphLookupHashEntry(mac_ctx, session->bssId, &aid,
                               &session->dph.dphHashTable);
   if (!sta_ds) {
       limLog(mac_ctx, LOGE, FL("pStaDs does not exist for given sessionID"));
       return;
   }

   session->gLimChannelSwitch.primaryChannel = ecsa_req->new_channel;
   session->gLimChannelSwitch.switchCount = ecsa_req->switch_count;
   session->gLimChannelSwitch.switchTimeoutValue =
            SYS_MS_TO_TICKS(session->beaconParams.beaconInterval) *
            ecsa_req->switch_count;
   session->gLimChannelSwitch.switchMode = ecsa_req->switch_mode;

   /* Only primary channel switch element is present */
   session->gLimChannelSwitch.state = eLIM_CHANNEL_SWITCH_PRIMARY_ONLY;
   session->gLimChannelSwitch.secondarySubBand = PHY_SINGLE_CHANNEL_CENTERED;
   session->gLimWiderBWChannelSwitch.newChanWidth = 0;

   ch_offset = limGetOffChMaxBwOffsetFromChannel(
                       mac_ctx->scan.countryCodeCurrent,
                       ecsa_req->new_channel,
                       sta_ds->mlmStaContext.vhtCapability);
   if (ch_offset == BW80) {
       session->gLimWiderBWChannelSwitch.newChanWidth =
                                  WNI_CFG_VHT_CHANNEL_WIDTH_80MHZ;
   } else {
       session->gLimWiderBWChannelSwitch.newChanWidth =
                                   WNI_CFG_VHT_CHANNEL_WIDTH_20_40MHZ;
   }

   /*
    * Do not bother to look and operate on extended channel switch element
    * if our own channel-bonding state is not enabled
    */
   if (session->htSupportedChannelWidthSet) {
       session->gLimChannelSwitch.secondarySubBand = lim_select_cbmode(sta_ds,
                                ecsa_req->new_channel,
                                session->gLimWiderBWChannelSwitch.newChanWidth);
       if (session->gLimChannelSwitch.secondarySubBand > 0)
              session->gLimChannelSwitch.state =
                                     eLIM_CHANNEL_SWITCH_PRIMARY_AND_SECONDARY;
   }

   if (eSIR_SUCCESS != limStartChannelSwitch(mac_ctx, session)) {
       limLog(mac_ctx, LOGW, FL("Could not start Channel Switch"));
   }

   limLog(mac_ctx, LOG1,
        FL("session %d primary chl %d, subband %d, count  %d (%d ticks) "),
        session->peSessionId,
        session->gLimChannelSwitch.primaryChannel,
        session->gLimChannelSwitch.secondarySubBand,
        session->gLimChannelSwitch.switchCount,
        session->gLimChannelSwitch.switchTimeoutValue);
}

/**
 * limCancelDot11hChannelSwitch
 *
 *FUNCTION:
 * This function is called when STA does not send updated channel-swith IE
 * after indicating channel-switch start. This will cancel the channel-swith
 * timer which is already running.
 * 
 *LOGIC:
 *
 *ASSUMPTIONS:
 *
 *NOTE:
 *
 * @param  pMac    - Pointer to Global MAC structure
 *
 * @return None
 */
void limCancelDot11hChannelSwitch(tpAniSirGlobal pMac, tpPESession psessionEntry)
{
    if (psessionEntry->limSystemRole != eLIM_STA_ROLE)
        return;
        
    PELOGW(limLog(pMac, LOGW, FL("Received a beacon without channel switch IE"));)
    MTRACE(macTrace(pMac, TRACE_CODE_TIMER_DEACTIVATE, psessionEntry->peSessionId, eLIM_CHANNEL_SWITCH_TIMER));

    if (tx_timer_deactivate(&pMac->lim.limTimers.gLimChannelSwitchTimer) != eSIR_SUCCESS)
    {
        PELOGE(limLog(pMac, LOGE, FL("tx_timer_deactivate failed!"));)
    }

    /* We need to restore pre-channelSwitch state on the STA */
    if (limRestorePreChannelSwitchState(pMac, psessionEntry) != eSIR_SUCCESS)
    {
        PELOGE(limLog(pMac, LOGE, FL("LIM: Could not restore pre-channelSwitch (11h) state, resetting the system"));)
                
    }
}

/**----------------------------------------------
\fn     limCancelDot11hQuiet
\brief  Cancel the quieting on Station if latest
        beacon doesn't contain quiet IE in it.

\param  pMac
\return NONE
-----------------------------------------------*/
void limCancelDot11hQuiet(tpAniSirGlobal pMac, tpPESession psessionEntry)
{
    if (psessionEntry->limSystemRole != eLIM_STA_ROLE)
        return;

    if (psessionEntry->gLimSpecMgmt.quietState == eLIM_QUIET_BEGIN) 
    {
         MTRACE(macTrace(pMac, TRACE_CODE_TIMER_DEACTIVATE, psessionEntry->peSessionId, eLIM_QUIET_TIMER));
        if (tx_timer_deactivate(&pMac->lim.limTimers.gLimQuietTimer) != TX_SUCCESS)
        {
            PELOGE(limLog(pMac, LOGE, FL("tx_timer_deactivate failed"));)
        }
    }
    else if (psessionEntry->gLimSpecMgmt.quietState == eLIM_QUIET_RUNNING)
    {
        MTRACE(macTrace(pMac, TRACE_CODE_TIMER_DEACTIVATE, psessionEntry->peSessionId, eLIM_QUIET_BSS_TIMER));
        if (tx_timer_deactivate(&pMac->lim.limTimers.gLimQuietBssTimer) != TX_SUCCESS)
        {
            PELOGE(limLog(pMac, LOGE, FL("tx_timer_deactivate failed"));)
        }
        /**
         * If the channel switch is already running in silent mode, dont resume the
         * transmission. Channel switch timer when timeout, transmission will be resumed.
         */
        if(!((psessionEntry->gLimSpecMgmt.dot11hChanSwState == eLIM_11H_CHANSW_RUNNING) &&
                (psessionEntry->gLimChannelSwitch.switchMode == eSIR_CHANSW_MODE_SILENT)))
        {
            limFrameTransmissionControl(pMac, eLIM_TX_ALL, eLIM_RESUME_TX);
            limRestorePreQuietState(pMac, psessionEntry);
        }
    }
    psessionEntry->gLimSpecMgmt.quietState = eLIM_QUIET_INIT;
}

/**
 * limProcessQuietTimeout
 *
 * FUNCTION:
 * This function is active only on the STA.
 * Handles SIR_LIM_QUIET_TIMEOUT
 *
 * LOGIC:
 * This timeout can occur under only one circumstance:
 *
 * 1) When gLimQuietState = eLIM_QUIET_BEGIN
 * This indicates that the timeout "interval" has
 * expired. This is a trigger for the STA to now
 * shut-off Tx/Rx for the specified gLimQuietDuration
 * -> The TIMER object gLimQuietBssTimer is
 * activated
 * -> With timeout = gLimQuietDuration
 * -> gLimQuietState is set to eLIM_QUIET_RUNNING
 *
 * ASSUMPTIONS:
 * Using two TIMER objects -
 * gLimQuietTimer & gLimQuietBssTimer
 *
 * NOTE:
 *
 * @param  pMac - Pointer to Global MAC structure
 *
 * @return None
 */
void limProcessQuietTimeout(tpAniSirGlobal pMac)
{
    //fetch the sessionEntry based on the sessionId
    //priority - MEDIUM
    tpPESession psessionEntry;

    if((psessionEntry = peFindSessionBySessionId(pMac, pMac->lim.limTimers.gLimQuietTimer.sessionId))== NULL) 
    {
        limLog(pMac, LOGE,FL("Session Does not exist for given sessionID"));
        return;
    }

  limLog(pMac, LOG1, FL("quietState = %d"), psessionEntry->gLimSpecMgmt.quietState);
  switch( psessionEntry->gLimSpecMgmt.quietState )
  {
    case eLIM_QUIET_BEGIN:
      // Time to Stop data traffic for quietDuration
      //limDeactivateAndChangeTimer(pMac, eLIM_QUIET_BSS_TIMER);
      if (TX_SUCCESS !=
      tx_timer_deactivate(&pMac->lim.limTimers.gLimQuietBssTimer))
      {
          limLog( pMac, LOGE,
            FL("Unable to de-activate gLimQuietBssTimer! Will attempt to activate anyway..."));
      }

      // gLimQuietDuration appears to be in units of ticks
      // Use it as is
      if (TX_SUCCESS !=
          tx_timer_change( &pMac->lim.limTimers.gLimQuietBssTimer,
            psessionEntry->gLimSpecMgmt.quietDuration,
            0))
      {
          limLog( pMac, LOGE,
            FL("Unable to change gLimQuietBssTimer! Will still attempt to activate anyway..."));
      }
      MTRACE(macTrace(pMac, TRACE_CODE_TIMER_ACTIVATE, pMac->lim.limTimers.gLimQuietTimer.sessionId, eLIM_QUIET_BSS_TIMER));
#ifdef GEN6_TODO
        /* revisit this piece of code to assign the appropriate sessionId below
         * priority - HIGH
         */
        pMac->lim.limTimers.gLimQuietBssTimer.sessionId = sessionId;
#endif              
      if( TX_SUCCESS !=
          tx_timer_activate( &pMac->lim.limTimers.gLimQuietBssTimer ))
      {
        limLog( pMac, LOGW,
            FL("Unable to activate gLimQuietBssTimer! The STA will be unable to honor Quiet BSS..."));
      }
      else
      {
        // Transition to eLIM_QUIET_RUNNING
        psessionEntry->gLimSpecMgmt.quietState = eLIM_QUIET_RUNNING;

        /* If we have sta bk scan triggered and trigger bk scan actually started successfully, */
        /* print message, otherwise, stop data traffic and stay quiet */
        if( pMac->lim.gLimTriggerBackgroundScanDuringQuietBss &&
          (eSIR_TRUE == (glimTriggerBackgroundScanDuringQuietBss_Status = limTriggerBackgroundScanDuringQuietBss( pMac ))) )
        {
           limLog( pMac, LOG2,
               FL("Attempting to trigger a background scan..."));
        }
        else
        {
           // Shut-off Tx/Rx for gLimSpecMgmt.quietDuration
           /* freeze the transmission */
           limFrameTransmissionControl(pMac, eLIM_TX_ALL, eLIM_STOP_TX);

           limLog( pMac, LOG2,
                FL("Quiet BSS: STA shutting down for %d ticks"),
                psessionEntry->gLimSpecMgmt.quietDuration );
        }
      }
      break;

    case eLIM_QUIET_RUNNING:
    case eLIM_QUIET_INIT:
    case eLIM_QUIET_END:
    default:
      //
      // As of now, nothing to be done
      //
      break;
  }
}

/**
 * limProcessQuietBssTimeout
 *
 * FUNCTION:
 * This function is active on the AP and STA.
 * Handles SIR_LIM_QUIET_BSS_TIMEOUT
 *
 * LOGIC:
 * On the AP -
 * When the SIR_LIM_QUIET_BSS_TIMEOUT is triggered, it is
 * an indication for the AP to START sending out the
 * Quiet BSS IE.
 * If 802.11H is enabled, the Quiet BSS IE is sent as per
 * the 11H spec
 * If 802.11H is not enabled, the Quiet BSS IE is sent as
 * a Proprietary IE. This will be understood by all the
 * TITAN STA's
 * Transitioning gLimQuietState to eLIM_QUIET_BEGIN will
 * initiate the SCH to include the Quiet BSS IE in all
 * its subsequent Beacons/PR's.
 * The Quiet BSS IE will be included in all the Beacons
 * & PR's until the next DTIM period
 *
 * On the STA -
 * When gLimQuietState = eLIM_QUIET_RUNNING
 * This indicates that the STA was successfully shut-off
 * for the specified gLimQuietDuration. This is a trigger
 * for the STA to now resume data traffic.
 * -> gLimQuietState is set to eLIM_QUIET_INIT
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @param  pMac - Pointer to Global MAC structure
 *
 * @return None
 */
void limProcessQuietBssTimeout( tpAniSirGlobal pMac )
{
    tpPESession psessionEntry;

    if((psessionEntry = peFindSessionBySessionId(pMac, pMac->lim.limTimers.gLimQuietBssTimer.sessionId))== NULL) 
    {
        limLog(pMac, LOGP,FL("Session Does not exist for given sessionID"));
        return;
    }

  limLog(pMac, LOG1, FL("quietState = %d"), psessionEntry->gLimSpecMgmt.quietState);
  if (eLIM_AP_ROLE == psessionEntry->limSystemRole)
  {
  }
  else
  {
    // eLIM_STA_ROLE
    switch( psessionEntry->gLimSpecMgmt.quietState )
    {
      case eLIM_QUIET_RUNNING:
        // Transition to eLIM_QUIET_INIT
        psessionEntry->gLimSpecMgmt.quietState = eLIM_QUIET_INIT;

        if( !pMac->lim.gLimTriggerBackgroundScanDuringQuietBss || (glimTriggerBackgroundScanDuringQuietBss_Status == eSIR_FALSE) )
        {
          // Resume data traffic only if channel switch is not running in silent mode.
          if (!((psessionEntry->gLimSpecMgmt.dot11hChanSwState == eLIM_11H_CHANSW_RUNNING) &&
                  (psessionEntry->gLimChannelSwitch.switchMode == eSIR_CHANSW_MODE_SILENT)))
          {
              limFrameTransmissionControl(pMac, eLIM_TX_ALL, eLIM_RESUME_TX);
              limRestorePreQuietState(pMac, psessionEntry);
          }
      
          /* Reset status flag */
          if(glimTriggerBackgroundScanDuringQuietBss_Status == eSIR_FALSE)
              glimTriggerBackgroundScanDuringQuietBss_Status = eSIR_TRUE;

          limLog( pMac, LOG2,
              FL("Quiet BSS: Resuming traffic..."));
        }
        else
        {
          //
          // Nothing specific to be done in this case
          // A background scan that was triggered during
          // SIR_LIM_QUIET_TIMEOUT will complete on its own
          //
          limLog( pMac, LOG2,
              FL("Background scan should be complete now..."));
        }
        break;

      case eLIM_QUIET_INIT:
      case eLIM_QUIET_BEGIN:
      case eLIM_QUIET_END:
        PELOG2(limLog(pMac, LOG2, FL("Quiet state not in RUNNING"));)
        /* If the quiet period has ended, then resume the frame transmission */
        limFrameTransmissionControl(pMac, eLIM_TX_ALL, eLIM_RESUME_TX);
        limRestorePreQuietState(pMac, psessionEntry);
        psessionEntry->gLimSpecMgmt.quietState = eLIM_QUIET_INIT;
        break;

      default:
        //
        // As of now, nothing to be done
        //
        break;
    }
  }
}
/**
 * limProcessWPSOverlapTimeout
 *
 * FUNCTION: This function call limWPSPBCTimeout() to clean WPS PBC probe request entries
 *
 * LOGIC:
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @param pMac - Pointer to Global MAC structure
 *
 * @return None
 */
#if 0
void limProcessWPSOverlapTimeout(tpAniSirGlobal pMac)
{

    tpPESession psessionEntry;
    tANI_U32 sessionId;
    
    if (tx_timer_activate(&pMac->lim.limTimers.gLimWPSOverlapTimerObj.gLimWPSOverlapTimer) != TX_SUCCESS)
    {
            limLog(pMac, LOGP, FL("tx_timer_activate failed"));
    }

    sessionId = pMac->lim.limTimers.gLimWPSOverlapTimerObj.sessionId;

    PELOGE(limLog(pMac, LOGE, FL("WPS overlap timeout, sessionId=%d"), sessionId);)

    if((psessionEntry = peFindSessionBySessionId(pMac, sessionId)) == NULL) 
    {
        PELOGE(limLog(pMac, LOGP,FL("Session Does not exist for given sessionID"));)
        return;
    }
    
    limWPSPBCTimeout(pMac, psessionEntry);
}
#endif

/**----------------------------------------------
\fn        limStartQuietTimer
\brief    Starts the quiet timer.

\param pMac
\return NONE
-----------------------------------------------*/
void limStartQuietTimer(tpAniSirGlobal pMac, tANI_U8 sessionId)
{
    tpPESession psessionEntry;
    psessionEntry = peFindSessionBySessionId(pMac, sessionId);

    if(psessionEntry == NULL) {
        limLog(pMac, LOGP,FL("Session Does not exist for given sessionID"));
        return;
    }


    if (psessionEntry->limSystemRole != eLIM_STA_ROLE)
        return;
    // First, de-activate Timer, if its already active
    limCancelDot11hQuiet(pMac, psessionEntry);
    
    MTRACE(macTrace(pMac, TRACE_CODE_TIMER_ACTIVATE, sessionId, eLIM_QUIET_TIMER));
    if( TX_SUCCESS != tx_timer_deactivate(&pMac->lim.limTimers.gLimQuietTimer))
    {
        limLog( pMac, LOGE,
            FL( "Unable to deactivate gLimQuietTimer! Will still attempt to re-activate anyway..." ));
    }

    // Set the NEW timeout value, in ticks
    if( TX_SUCCESS != tx_timer_change( &pMac->lim.limTimers.gLimQuietTimer,
                      SYS_MS_TO_TICKS(psessionEntry->gLimSpecMgmt.quietTimeoutValue), 0))
    {
        limLog( pMac, LOGE,
            FL( "Unable to change gLimQuietTimer! Will still attempt to re-activate anyway..." ));
    }
    
    pMac->lim.limTimers.gLimQuietTimer.sessionId = sessionId;
    if( TX_SUCCESS != tx_timer_activate(&pMac->lim.limTimers.gLimQuietTimer))
    {
        limLog( pMac, LOGE,
            FL("Unable to activate gLimQuietTimer! STA cannot honor Quiet BSS!"));
        limRestorePreQuietState(pMac, psessionEntry);

        psessionEntry->gLimSpecMgmt.quietState = eLIM_QUIET_INIT;
        return;
    }
}


/** ------------------------------------------------------------------------ **/
/**
 * keep track of the number of ANI peers associated in the BSS
 * For the first and last ANI peer, we have to update EDCA params as needed
 *
 * When the first ANI peer joins the BSS, we notify SCH
 * When the last ANI peer leaves the BSS, we notfiy SCH
 */
void
limUtilCountStaAdd(
    tpAniSirGlobal pMac,
    tpDphHashNode  pSta,
    tpPESession psessionEntry)
{

    if ((! pSta) || (! pSta->valid) || (! pSta->aniPeer) || (pSta->fAniCount))
        return;

    pSta->fAniCount = 1;

    if (pMac->lim.gLimNumOfAniSTAs++ != 0)
        return;

    // get here only if this is the first ANI peer in the BSS
    schEdcaProfileUpdate(pMac, psessionEntry);
}

void
limUtilCountStaDel(
    tpAniSirGlobal pMac,
    tpDphHashNode  pSta,
    tpPESession psessionEntry)
{

    if ((pSta == NULL) || (pSta->aniPeer == eHAL_CLEAR) || (! pSta->fAniCount))
        return;

    /* Only if sta is invalid and the validInDummyState bit is set to 1,
     * then go ahead and update the count and profiles. This ensures
     * that the "number of ani station" count is properly incremented/decremented.
     */
    if (pSta->valid == 1)
         return;

    pSta->fAniCount = 0;

    if (pMac->lim.gLimNumOfAniSTAs <= 0)
    {
        limLog(pMac, LOGE, FL("CountStaDel: ignoring Delete Req when AniPeer count is %d"),
               pMac->lim.gLimNumOfAniSTAs);
        return;
    }

    pMac->lim.gLimNumOfAniSTAs--;

    if (pMac->lim.gLimNumOfAniSTAs != 0)
        return;

    // get here only if this is the last ANI peer in the BSS
    schEdcaProfileUpdate(pMac, psessionEntry);
}

/**
 * limSwitchChannelCback()
 *
 *FUNCTION:
 *  This is the callback function registered while requesting to switch channel
 *  after AP indicates a channel switch for spectrum management (11h).
 * 
 *NOTE:
 * @param  pMac               Pointer to Global MAC structure
 * @param  status             Status of channel switch request
 * @param  data               User data
 * @param  psessionEntry      Session information 
 * @return NONE
 */
void limSwitchChannelCback(tpAniSirGlobal pMac, eHalStatus status, 
                           tANI_U32 *data, tpPESession psessionEntry)
{
   tSirMsgQ    mmhMsg = {0};
   tSirSmeSwitchChannelInd *pSirSmeSwitchChInd;

   psessionEntry->currentOperChannel = psessionEntry->currentReqChannel; 
   
   /* We need to restore pre-channelSwitch state on the STA */
   if (limRestorePreChannelSwitchState(pMac, psessionEntry) != eSIR_SUCCESS)
   {
      limLog(pMac, LOGP, FL("Could not restore pre-channelSwitch (11h) state, resetting the system"));
      return;
   }
   
   mmhMsg.type = eWNI_SME_SWITCH_CHL_REQ;
   pSirSmeSwitchChInd = vos_mem_malloc(sizeof(tSirSmeSwitchChannelInd));
   if ( NULL == pSirSmeSwitchChInd )
   {
      limLog(pMac, LOGP, FL("Failed to allocate buffer for buffer descriptor"));
      return;
   }
  
   pSirSmeSwitchChInd->messageType = eWNI_SME_SWITCH_CHL_REQ;
   pSirSmeSwitchChInd->length = sizeof(tSirSmeSwitchChannelInd);
   pSirSmeSwitchChInd->newChannelId = psessionEntry->gLimChannelSwitch.primaryChannel;
   pSirSmeSwitchChInd->sessionId = psessionEntry->smeSessionId;
   //BSS ID
   vos_mem_copy( pSirSmeSwitchChInd->bssId, psessionEntry->bssId, sizeof(tSirMacAddr));
   mmhMsg.bodyptr = pSirSmeSwitchChInd;
   mmhMsg.bodyval = 0;
   
   MTRACE(macTrace(pMac, TRACE_CODE_TX_SME_MSG, psessionEntry->peSessionId,
                                                            mmhMsg.type));
   SysProcessMmhMsg(pMac, &mmhMsg);
}

/**
 * limSwitchPrimaryChannel()
 *
 *FUNCTION:
 *  This function changes the current operating channel
 *  and sets the new new channel ID in WNI_CFG_CURRENT_CHANNEL.
 *
 *NOTE:
 * @param  pMac        Pointer to Global MAC structure
 * @param  newChannel  new chnannel ID
 * @return NONE
 */
void limSwitchPrimaryChannel(tpAniSirGlobal pMac, tANI_U8 newChannel,tpPESession psessionEntry)
{
#if !defined WLAN_FEATURE_VOWIFI  
    tANI_U32 localPwrConstraint;
#endif
    
    limLog(pMac, LOG1, FL(" old chnl %d --> new chnl %d "),
           psessionEntry->currentOperChannel, newChannel);
    psessionEntry->currentReqChannel = newChannel;
    psessionEntry->limRFBand = limGetRFBand(newChannel);

    psessionEntry->channelChangeReasonCode=LIM_SWITCH_CHANNEL_OPERATION;

    pMac->lim.gpchangeChannelCallback = limSwitchChannelCback;
    pMac->lim.gpchangeChannelData = NULL;

#if defined WLAN_FEATURE_VOWIFI  
    limSendSwitchChnlParams(pMac, newChannel, PHY_SINGLE_CHANNEL_CENTERED,
                                                   psessionEntry->maxTxPower, psessionEntry->peSessionId);
#else
    if(wlan_cfgGetInt(pMac, WNI_CFG_LOCAL_POWER_CONSTRAINT, &localPwrConstraint) != eSIR_SUCCESS)
    {
        limLog( pMac, LOGP, FL( "Unable to read Local Power Constraint from cfg" ));
        return;
    }
    limSendSwitchChnlParams(pMac, newChannel, PHY_SINGLE_CHANNEL_CENTERED,
                                                   (tPowerdBm)localPwrConstraint, psessionEntry->peSessionId);
#endif
    return;
}

/**
 * limSwitchPrimarySecondaryChannel()
 *
 *FUNCTION:
 *  This function changes the primary and secondary channel.
 *  If 11h is enabled and user provides a "new channel ID"
 *  that is different from the current operating channel,
 *  then we must set this new channel in WNI_CFG_CURRENT_CHANNEL,
 *  assign notify LIM of such change.
 *
 *NOTE:
 * @param  pMac        Pointer to Global MAC structure
 * @param  newChannel  New chnannel ID (or current channel ID)
 * @param  subband     CB secondary info:
 *                       - eANI_CB_SECONDARY_NONE
 *                       - eANI_CB_SECONDARY_UP
 *                       - eANI_CB_SECONDARY_DOWN
 * @return NONE
 */
void limSwitchPrimarySecondaryChannel(tpAniSirGlobal pMac, tpPESession psessionEntry, tANI_U8 newChannel, ePhyChanBondState subband)
{
#if !defined WLAN_FEATURE_VOWIFI  
    tANI_U32 localPwrConstraint;
#endif

#if !defined WLAN_FEATURE_VOWIFI  
    if(wlan_cfgGetInt(pMac, WNI_CFG_LOCAL_POWER_CONSTRAINT, &localPwrConstraint) != eSIR_SUCCESS) {
        limLog( pMac, LOGP, FL( "Unable to get Local Power Constraint from cfg" ));
        return;
    }
#endif
    /* Assign the callback to resume TX once channel is changed.
     */
    psessionEntry->currentReqChannel = newChannel;
    psessionEntry->limRFBand = limGetRFBand(newChannel);

    psessionEntry->channelChangeReasonCode=LIM_SWITCH_CHANNEL_OPERATION;

    pMac->lim.gpchangeChannelCallback = limSwitchChannelCback;
    pMac->lim.gpchangeChannelData = NULL;

#if defined WLAN_FEATURE_VOWIFI  
                limSendSwitchChnlParams(pMac, newChannel, subband, psessionEntry->maxTxPower, psessionEntry->peSessionId);
#else
                limSendSwitchChnlParams(pMac, newChannel, subband, (tPowerdBm)localPwrConstraint, psessionEntry->peSessionId);
#endif

#ifdef FEATURE_WLAN_DIAG_SUPPORT
       limDiagEventReport(pMac, WLAN_PE_DIAG_CHANNEL_SWITCH_ANOUNCEMENT,
                 psessionEntry, eSIR_SUCCESS, LIM_SWITCH_CHANNEL_OPERATION);
#endif

    // Store the new primary and secondary channel in session entries if different
    if (psessionEntry->currentOperChannel != newChannel)
    {
        limLog(pMac, LOGW,
            FL("switch old chnl %d --> new chnl %d "),
            psessionEntry->currentOperChannel, newChannel);
        psessionEntry->currentOperChannel = newChannel;
    }
    if (psessionEntry->htSecondaryChannelOffset != limGetHTCBState(subband))
    {
        limLog(pMac, LOGW,
            FL("switch old sec chnl %d --> new sec chnl %d "),
            psessionEntry->htSecondaryChannelOffset, limGetHTCBState(subband));
        psessionEntry->htSecondaryChannelOffset = limGetHTCBState(subband);
        if (psessionEntry->htSecondaryChannelOffset == PHY_SINGLE_CHANNEL_CENTERED)
        {
            psessionEntry->htSupportedChannelWidthSet = WNI_CFG_CHANNEL_BONDING_MODE_DISABLE;
            psessionEntry->apCenterChan = 0;
        }
        else
        {
            psessionEntry->htSupportedChannelWidthSet = WNI_CFG_CHANNEL_BONDING_MODE_ENABLE;
        }
        psessionEntry->htRecommendedTxWidthSet = psessionEntry->htSupportedChannelWidthSet;
    }

    if (psessionEntry->htSecondaryChannelOffset == PHY_SINGLE_CHANNEL_CENTERED)
        return;

    if (subband > PHY_DOUBLE_CHANNEL_HIGH_PRIMARY)
        psessionEntry->apCenterChan =
               limGetCenterChannel(pMac, newChannel,
                                   subband, WNI_CFG_VHT_CHANNEL_WIDTH_80MHZ);
    else
        psessionEntry->apCenterChan =
               limGetCenterChannel(pMac, newChannel,
                                   subband, WNI_CFG_VHT_CHANNEL_WIDTH_20_40MHZ);

    return;
}


/**
 * limActiveScanAllowed()
 *
 *FUNCTION:
 * Checks if active scans are permitted on the given channel
 *
 *LOGIC:
 * The config variable SCAN_CONTROL_LIST contains pairs of (channelNum, activeScanAllowed)
 * Need to check if the channelNum matches, then depending on the corresponding
 * scan flag, return true (for activeScanAllowed==1) or false (otherwise).
 *
 *ASSUMPTIONS:
 *
 *NOTE:
 *
 * @param  pMac       Pointer to Global MAC structure
 * @param  channelNum channel number
 * @return None
 */

tANI_U8 limActiveScanAllowed(
    tpAniSirGlobal pMac,
    tANI_U8             channelNum)
{
    tANI_U32 i;
    tANI_U8  channelPair[WNI_CFG_SCAN_CONTROL_LIST_LEN];
    tANI_U32 len = WNI_CFG_SCAN_CONTROL_LIST_LEN;
    if (wlan_cfgGetStr(pMac, WNI_CFG_SCAN_CONTROL_LIST, channelPair, &len)
        != eSIR_SUCCESS)
    {
        PELOGE(limLog(pMac, LOGE, FL("Unable to get scan control list"));)
        return false;
    }

    if (len > WNI_CFG_SCAN_CONTROL_LIST_LEN)
    {
        limLog(pMac, LOGE, FL("Invalid scan control list length:%d"),
               len);
        return false;
    }

    for (i=0; (i+1) < len; i+=2)
    {
        if (channelPair[i] == channelNum)
            return ((channelPair[i+1] == eSIR_ACTIVE_SCAN) ? true : false);
    }
    return false;
}

/**
 * limTriggerBackgroundScanDuringQuietBss()
 *
 *FUNCTION:
 * This function is applicable to the STA only.
 * This function is called by limProcessQuietTimeout(),
 * when it is time to honor the Quiet BSS IE from the AP.
 *
 *LOGIC:
 * If 11H is enabled:
 * We cannot trigger a background scan. The STA needs to
 * shut-off Tx/Rx.
 * If 11 is not enabled:
 * Determine if the next channel that we are going to
 * scan is NOT the same channel (or not) on which the
 * Quiet BSS was requested.
 * If yes, then we cannot trigger a background scan on
 * this channel. Return with a false.
 * If no, then trigger a background scan. Return with
 * a true.
 *
 *ASSUMPTIONS:
 *
 *NOTE:
 * This API is redundant if the existing API,
 * limTriggerBackgroundScan(), were to return a valid
 * response instead of returning void.
 * If possible, try to revisit this API
 *
 * @param  pMac Pointer to Global MAC structure
 * @return eSIR_TRUE, if a background scan was attempted
 *         eSIR_FALSE, if not
 */
tAniBool limTriggerBackgroundScanDuringQuietBss( tpAniSirGlobal pMac )
{
    tAniBool bScanTriggered = eSIR_FALSE;

    

    //TBD-RAJESH HOW TO GET sessionEntry?????
    tpPESession psessionEntry = &pMac->lim.gpSession[0];

    if (psessionEntry->limSystemRole != eLIM_STA_ROLE)
        return bScanTriggered;
    
    if( !psessionEntry->lim11hEnable )
    {
        tSirMacChanNum bgScanChannelList[WNI_CFG_BG_SCAN_CHANNEL_LIST_LEN];
        tANI_U32 len = WNI_CFG_BG_SCAN_CHANNEL_LIST_LEN;

        // Determine the next scan channel

        // Get background scan channel list from CFG
        if( eSIR_SUCCESS == wlan_cfgGetStr( pMac,
          WNI_CFG_BG_SCAN_CHANNEL_LIST,
          (tANI_U8 *) bgScanChannelList,
          (tANI_U32 *) &len ))
        {
            // Ensure that we do not go off scanning on the same
        // channel on which the Quiet BSS was requested
            if( psessionEntry->currentOperChannel!=
                bgScanChannelList[pMac->lim.gLimBackgroundScanChannelId] )
            {
            // For now, try and attempt a background scan. It will
            // be ideal if this API actually returns a success or
            // failure instead of having a void return type
            limTriggerBackgroundScan( pMac );

            bScanTriggered = eSIR_TRUE;
        }
        else
        {
            limLog( pMac, LOGW,
                FL("The next SCAN channel is the current operating channel on which a Quiet BSS is requested.! A background scan will not be triggered during this Quiet BSS period..."));
        }
    }
    else
    {
      limLog( pMac, LOGW,
          FL("Unable to retrieve WNI_CFG_VALID_CHANNEL_LIST from CFG! A background scan will not be triggered during this Quiet BSS period..."));
    }
  }
  return bScanTriggered;
}


/**
 * limGetHTCapability()
 *
 *FUNCTION:
 * A utility function that returns the "current HT capability state" for the HT
 * capability of interest (as requested in the API)
 *
 *LOGIC:
 * This routine will return with the "current" setting of a requested HT
 * capability. This state info could be retrieved from -
 * a) CFG (for static entries)
 * b) Run time info
 *   - Dynamic state maintained by LIM
 *   - Configured at radio init time by SME
 *
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 *
 * @param  pMac  Pointer to Global MAC structure
 * @param  htCap The HT capability being queried
 * @return tANI_U8 The current state of the requested HT capability is returned in a
 *            tANI_U8 variable
 */

tANI_U8 limGetHTCapability( tpAniSirGlobal pMac,
        tANI_U32 htCap, tpPESession psessionEntry)
{
tANI_U8 retVal = 0;
tANI_U8 *ptr;
tANI_U32  cfgValue;
tSirMacHTCapabilityInfo macHTCapabilityInfo = {0};
tSirMacExtendedHTCapabilityInfo macExtHTCapabilityInfo = {0};
tSirMacTxBFCapabilityInfo macTxBFCapabilityInfo = {0};
tSirMacASCapabilityInfo macASCapabilityInfo = {0};

  //
  // Determine which CFG to read from. Not ALL of the HT
  // related CFG's need to be read each time this API is
  // accessed
  //
  if( htCap >= eHT_ANTENNA_SELECTION &&
      htCap < eHT_SI_GRANULARITY )
  {
    // Get Antenna Seletion HT Capabilities
    if( eSIR_SUCCESS != wlan_cfgGetInt( pMac, WNI_CFG_AS_CAP, &cfgValue ))
      cfgValue = 0;
    ptr = (tANI_U8 *) &macASCapabilityInfo;
    *((tANI_U8 *)ptr) =  (tANI_U8) (cfgValue & 0xff);
  }
  else
  {
    if( htCap >= eHT_TX_BEAMFORMING &&
        htCap < eHT_ANTENNA_SELECTION )
    {
      // Get Transmit Beam Forming HT Capabilities
      if( eSIR_SUCCESS != wlan_cfgGetInt( pMac, WNI_CFG_TX_BF_CAP, &cfgValue ))
        cfgValue = 0;
      ptr = (tANI_U8 *) &macTxBFCapabilityInfo;
      *((tANI_U32 *)ptr) =  (tANI_U32) (cfgValue);
    }
    else
    {
      if( htCap >= eHT_PCO &&
          htCap < eHT_TX_BEAMFORMING )
      {
        // Get Extended HT Capabilities
        if( eSIR_SUCCESS != wlan_cfgGetInt( pMac, WNI_CFG_EXT_HT_CAP_INFO, &cfgValue ))
          cfgValue = 0;
        ptr = (tANI_U8 *) &macExtHTCapabilityInfo;
        *((tANI_U16 *)ptr) =  (tANI_U16) (cfgValue & 0xffff);
      }
      else
      {
        if( htCap < eHT_MAX_RX_AMPDU_FACTOR )
        {
          // Get HT Capabilities
          if( eSIR_SUCCESS != wlan_cfgGetInt( pMac, WNI_CFG_HT_CAP_INFO, &cfgValue ))
            cfgValue = 0;
          ptr = (tANI_U8 *) &macHTCapabilityInfo;
          // CR 265282 MDM SoftAP 2.4PL: SoftAP boot up crash in 2.4 PL builds while same WLAN SU is working on 2.1 PL
          *ptr++ = cfgValue & 0xff;
          *ptr = (cfgValue >> 8) & 0xff;
        }
      }
    }
  }

  switch( htCap )
  {
    case eHT_LSIG_TXOP_PROTECTION:
      retVal = pMac->lim.gHTLsigTXOPProtection;
      break;

    case eHT_STBC_CONTROL_FRAME:
      retVal = (tANI_U8) macHTCapabilityInfo.stbcControlFrame;
      break;

    case eHT_PSMP:
      retVal = pMac->lim.gHTPSMPSupport;
      break;

    case eHT_DSSS_CCK_MODE_40MHZ:
      retVal = pMac->lim.gHTDsssCckRate40MHzSupport;
      break;

    case eHT_MAX_AMSDU_LENGTH:
      retVal = (tANI_U8) macHTCapabilityInfo.maximalAMSDUsize;
      break;

    case eHT_DELAYED_BA:
      retVal = (tANI_U8) macHTCapabilityInfo.delayedBA;
      break;

    case eHT_RX_STBC:
      retVal = (tANI_U8) macHTCapabilityInfo.rxSTBC;
      break;

    case eHT_TX_STBC:
      retVal = (tANI_U8) macHTCapabilityInfo.txSTBC;
      break;

    case eHT_SHORT_GI_40MHZ:
      retVal = (tANI_U8) macHTCapabilityInfo.shortGI40MHz;
      break;

    case eHT_SHORT_GI_20MHZ:
      retVal = (tANI_U8) macHTCapabilityInfo.shortGI20MHz;
      break;

    case eHT_GREENFIELD:
      retVal = (tANI_U8) macHTCapabilityInfo.greenField;
      break;

    case eHT_MIMO_POWER_SAVE:
      retVal = (tANI_U8) pMac->lim.gHTMIMOPSState;
      break;

    case eHT_SUPPORTED_CHANNEL_WIDTH_SET:
      retVal = (tANI_U8) psessionEntry->htSupportedChannelWidthSet;
      break;

    case eHT_ADVANCED_CODING:
      retVal = (tANI_U8) macHTCapabilityInfo.advCodingCap;
      break;

    case eHT_MAX_RX_AMPDU_FACTOR:
      retVal = pMac->lim.gHTMaxRxAMpduFactor;
      break;

    case eHT_MPDU_DENSITY:
      retVal = pMac->lim.gHTAMpduDensity;
      break;

    case eHT_PCO:
      retVal = (tANI_U8) macExtHTCapabilityInfo.pco;
      break;

    case eHT_TRANSITION_TIME:
      retVal = (tANI_U8) macExtHTCapabilityInfo.transitionTime;
      break;

    case eHT_MCS_FEEDBACK:
      retVal = (tANI_U8) macExtHTCapabilityInfo.mcsFeedback;
      break;

    case eHT_TX_BEAMFORMING:
      retVal = (tANI_U8) macTxBFCapabilityInfo.txBF;
      break;

    case eHT_ANTENNA_SELECTION:
      retVal = (tANI_U8) macASCapabilityInfo.antennaSelection;
      break;

    case eHT_SI_GRANULARITY:
      retVal = pMac->lim.gHTServiceIntervalGranularity;
      break;

    case eHT_CONTROLLED_ACCESS:
      retVal = pMac->lim.gHTControlledAccessOnly;
      break;

    case eHT_RIFS_MODE:
      retVal = psessionEntry->beaconParams.fRIFSMode;
      break;

    case eHT_RECOMMENDED_TX_WIDTH_SET:
      retVal = psessionEntry->htRecommendedTxWidthSet;
      break;

    case eHT_EXTENSION_CHANNEL_OFFSET:
      retVal = psessionEntry->htSecondaryChannelOffset;
      break;

    case eHT_OP_MODE:
      if(psessionEntry->limSystemRole == eLIM_AP_ROLE )
          retVal = psessionEntry->htOperMode;
      else
          retVal = pMac->lim.gHTOperMode;
      break;

    case eHT_BASIC_STBC_MCS:
      retVal = pMac->lim.gHTSTBCBasicMCS;
      break;

    case eHT_DUAL_CTS_PROTECTION:
      retVal = pMac->lim.gHTDualCTSProtection;
      break;

    case eHT_LSIG_TXOP_PROTECTION_FULL_SUPPORT:
      retVal = psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport;
      break;

    case eHT_PCO_ACTIVE:
      retVal = pMac->lim.gHTPCOActive;
      break;

    case eHT_PCO_PHASE:
      retVal = pMac->lim.gHTPCOPhase;
      break;

    default:
      break;
  }

  return retVal;
}

void limGetMyMacAddr(tpAniSirGlobal pMac, tANI_U8 *mac)
{
    vos_mem_copy( mac, pMac->lim.gLimMyMacAddr, sizeof(tSirMacAddr));
    return;
}




/** -------------------------------------------------------------
\fn limEnable11aProtection
\brief based on config setting enables\disables 11a protection.
\param      tANI_U8 enable : 1=> enable protection, 0=> disable protection.
\param      tANI_U8 overlap: 1=> called from overlap context, 0 => called from assoc context.
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/
tSirRetStatus
limEnable11aProtection(tpAniSirGlobal pMac, tANI_U8 enable,
    tANI_U8 overlap, tpUpdateBeaconParams pBeaconParams,tpPESession psessionEntry)
{
    if(NULL == psessionEntry)
    {
        PELOG3(limLog(pMac, LOG3, FL("psessionEntry is NULL"));)
        return eSIR_FAILURE;
    }        
        //overlapping protection configuration check.
        if(overlap)
        {
        }
        else
        {
            //normal protection config check
            if ((psessionEntry->limSystemRole == eLIM_AP_ROLE) &&
                (!psessionEntry->cfgProtection.fromlla))
            {
                // protection disabled.
                PELOG3(limLog(pMac, LOG3, FL("protection from 11a is disabled"));)
                return eSIR_SUCCESS;
            }
        }

    if (enable)
    {
        //If we are AP and HT capable, we need to set the HT OP mode
        //appropriately.
        if(((eLIM_AP_ROLE == psessionEntry->limSystemRole)||(eLIM_BT_AMP_AP_ROLE == psessionEntry->limSystemRole))&&
              (true == psessionEntry->htCapability))
        {
            if(overlap)
            {
                pMac->lim.gLimOverlap11aParams.protectionEnabled = true;
                if((eSIR_HT_OP_MODE_OVERLAP_LEGACY != pMac->lim.gHTOperMode) &&
                   (eSIR_HT_OP_MODE_MIXED != pMac->lim.gHTOperMode))
                {
                    pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                    psessionEntry->htOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                    limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);          
                    limEnableHtOBSSProtection(pMac,  true, overlap, pBeaconParams,psessionEntry);         
                }
            }
            else
            {
                psessionEntry->gLim11aParams.protectionEnabled = true;
                if(eSIR_HT_OP_MODE_MIXED != pMac->lim.gHTOperMode)
                {
                    pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_MIXED;
                    psessionEntry->htOperMode = eSIR_HT_OP_MODE_MIXED;
                    limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                    limEnableHtOBSSProtection(pMac,  true, overlap, pBeaconParams,psessionEntry);         
                    
                }
            }
        }

        //This part is common for staiton as well.
        if(false == psessionEntry->beaconParams.llaCoexist)
        {
            PELOG1(limLog(pMac, LOG1, FL(" => protection from 11A Enabled"));)
            pBeaconParams->llaCoexist = psessionEntry->beaconParams.llaCoexist = true;
            pBeaconParams->paramChangeBitmap |= PARAM_llACOEXIST_CHANGED;
        }
    }
    else if (true == psessionEntry->beaconParams.llaCoexist)
    {
        //for AP role.
        //we need to take care of HT OP mode change if needed.
        //We need to take care of Overlap cases.
        if(eLIM_AP_ROLE == psessionEntry->limSystemRole)
        {
            if(overlap)
            {
                //Overlap Legacy protection disabled.
                pMac->lim.gLimOverlap11aParams.protectionEnabled = false;

                //We need to take care of HT OP mode iff we are HT AP.
                if(psessionEntry->htCapability)
                {
                   // no HT op mode change if any of the overlap protection enabled.
                    if(!(pMac->lim.gLimOverlap11aParams.protectionEnabled ||
                         pMac->lim.gLimOverlapHt20Params.protectionEnabled ||
                         pMac->lim.gLimOverlapNonGfParams.protectionEnabled))

                    {
                        //Check if there is a need to change HT OP mode.
                        if(eSIR_HT_OP_MODE_OVERLAP_LEGACY == pMac->lim.gHTOperMode)
                        {
                            limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                            limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);        

                            if(psessionEntry->gLimHt20Params.protectionEnabled)
                                pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                            else
                                pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_PURE;
                        }
                    }
                }
            }
            else
            {
                //Disable protection from 11A stations.
                psessionEntry->gLim11aParams.protectionEnabled = false;
                limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);

                //Check if any other non-HT protection enabled.
                //Right now we are in HT OP Mixed mode.
                //Change HT op mode appropriately.

                //Change HT OP mode to 01 if any overlap protection enabled
                if(pMac->lim.gLimOverlap11aParams.protectionEnabled ||
                   pMac->lim.gLimOverlapHt20Params.protectionEnabled ||
                   pMac->lim.gLimOverlapNonGfParams.protectionEnabled)

                {
                        pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                        limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                }
                else if(psessionEntry->gLimHt20Params.protectionEnabled)
                {
                        pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                }
                else
                {
                        pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_PURE;
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                }
            }
        if(!pMac->lim.gLimOverlap11aParams.protectionEnabled &&
           !psessionEntry->gLim11aParams.protectionEnabled)
            {
                PELOG1(limLog(pMac, LOG1, FL("===> Protection from 11A Disabled"));)
                pBeaconParams->llaCoexist = psessionEntry->beaconParams.llaCoexist = false;
                pBeaconParams->paramChangeBitmap |= PARAM_llACOEXIST_CHANGED;
            }
        }
        //for station role
        else
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Protection from 11A Disabled"));)
            pBeaconParams->llaCoexist = psessionEntry->beaconParams.llaCoexist = false;
            pBeaconParams->paramChangeBitmap |= PARAM_llACOEXIST_CHANGED;
        }
    }

    return eSIR_SUCCESS;
}

/** -------------------------------------------------------------
\fn limEnable11gProtection
\brief based on config setting enables\disables 11g protection.
\param      tANI_U8 enable : 1=> enable protection, 0=> disable protection.
\param      tANI_U8 overlap: 1=> called from overlap context, 0 => called from assoc context.
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/

tSirRetStatus
limEnable11gProtection(tpAniSirGlobal pMac, tANI_U8 enable,
    tANI_U8 overlap, tpUpdateBeaconParams pBeaconParams,tpPESession psessionEntry)
{

    //overlapping protection configuration check.
    if(overlap)
    {
    }
    else
    {
        //normal protection config check
        if((psessionEntry->limSystemRole == eLIM_AP_ROLE ) &&
                !psessionEntry->cfgProtection.fromllb)
        {
            // protection disabled.
            PELOG1(limLog(pMac, LOG1, FL("protection from 11b is disabled"));)
            return eSIR_SUCCESS;
        }else if(psessionEntry->limSystemRole != eLIM_AP_ROLE)
        {   
            if(!pMac->lim.cfgProtection.fromllb)
            {
                // protection disabled.
                PELOG1(limLog(pMac, LOG1, FL("protection from 11b is disabled"));)
                return eSIR_SUCCESS;
            }
        }
    }

    if (enable)
    {
        //If we are AP and HT capable, we need to set the HT OP mode
        //appropriately.
        if(eLIM_AP_ROLE == psessionEntry->limSystemRole)
        {
            if(overlap)
            {
                psessionEntry->gLimOlbcParams.protectionEnabled = true;
                PELOGE(limLog(pMac, LOGE, FL("protection from olbc is enabled"));)
                if(true == psessionEntry->htCapability)
                {
                    if((eSIR_HT_OP_MODE_OVERLAP_LEGACY != psessionEntry->htOperMode) &&
                            (eSIR_HT_OP_MODE_MIXED != psessionEntry->htOperMode))
                    {
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                    }
                    //CR-263021: OBSS bit is not switching back to 0 after disabling the overlapping legacy BSS
                    // This fixes issue of OBSS bit not set after 11b, 11g station leaves
                    limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                    //Not processing OBSS bit from other APs, as we are already taking care
                    //of Protection from overlapping BSS based on erp IE or useProtection bit
                    limEnableHtOBSSProtection(pMac,  true, overlap, pBeaconParams, psessionEntry);
                }
            }
            else
            {
                psessionEntry->gLim11bParams.protectionEnabled = true;
                PELOGE(limLog(pMac, LOGE, FL("protection from 11b is enabled"));)
                if(true == psessionEntry->htCapability)
                {
                    if(eSIR_HT_OP_MODE_MIXED != psessionEntry->htOperMode)
                    {
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_MIXED;
                        limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                        limEnableHtOBSSProtection(pMac,  true, overlap, pBeaconParams,psessionEntry);     
                    }
                }
            }
        }else if ((eLIM_BT_AMP_AP_ROLE == psessionEntry->limSystemRole) &&
                (true == psessionEntry->htCapability))
            {
                if(overlap)
                {
                    psessionEntry->gLimOlbcParams.protectionEnabled = true;
                    if((eSIR_HT_OP_MODE_OVERLAP_LEGACY != pMac->lim.gHTOperMode) &&
                            (eSIR_HT_OP_MODE_MIXED != pMac->lim.gHTOperMode))
                    {
                        pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                    }
                    //CR-263021: OBSS bit is not switching back to 0 after disabling the overlapping legacy BSS
                    // This fixes issue of OBSS bit not set after 11b, 11g station leaves
                    limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                    //Not processing OBSS bit from other APs, as we are already taking care
                    //of Protection from overlapping BSS based on erp IE or useProtection bit
                    limEnableHtOBSSProtection(pMac,  true, overlap, pBeaconParams, psessionEntry);
                }
                else
                {
                    psessionEntry->gLim11bParams.protectionEnabled = true;
                    if(eSIR_HT_OP_MODE_MIXED != pMac->lim.gHTOperMode)
                    { 
                        pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_MIXED;
                        limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                        limEnableHtOBSSProtection(pMac,  true, overlap, pBeaconParams,psessionEntry);     
                    }
                }
            }

        //This part is common for staiton as well.
        if(false == psessionEntry->beaconParams.llbCoexist)
        {
            PELOG1(limLog(pMac, LOG1, FL("=> 11G Protection Enabled"));)
            pBeaconParams->llbCoexist = psessionEntry->beaconParams.llbCoexist = true;
            pBeaconParams->paramChangeBitmap |= PARAM_llBCOEXIST_CHANGED;
        }
    }
    else if (true == psessionEntry->beaconParams.llbCoexist)
    {
        //for AP role.
        //we need to take care of HT OP mode change if needed.
        //We need to take care of Overlap cases.
        if(eLIM_AP_ROLE == psessionEntry->limSystemRole)
        {
            if(overlap)
            {
                //Overlap Legacy protection disabled.
                psessionEntry->gLimOlbcParams.protectionEnabled = false;

                //We need to take care of HT OP mode if we are HT AP.
                if(psessionEntry->htCapability)
                {
                    // no HT op mode change if any of the overlap protection enabled.
                    if(!(psessionEntry->gLimOverlap11gParams.protectionEnabled ||
                                psessionEntry->gLimOverlapHt20Params.protectionEnabled ||
                                psessionEntry->gLimOverlapNonGfParams.protectionEnabled))
                    {
                        //Check if there is a need to change HT OP mode.
                        if(eSIR_HT_OP_MODE_OVERLAP_LEGACY == psessionEntry->htOperMode)
                        {
                            limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                            limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);            
                            if(psessionEntry->gLimHt20Params.protectionEnabled){
                                //Commenting out beacuse of CR 258588 WFA cert
                                //psessionEntry->htOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                                psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                            }
                            else
                                psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                        }
                    }
                }
            }
            else
            {
                //Disable protection from 11B stations.
                psessionEntry->gLim11bParams.protectionEnabled = false;
                PELOGE(limLog(pMac, LOGE, FL("===> 11B Protection Disabled"));)
                    //Check if any other non-HT protection enabled.
                if(!psessionEntry->gLim11gParams.protectionEnabled)
                {
                    //Right now we are in HT OP Mixed mode.
                    //Change HT op mode appropriately.
                    limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);            

                    //Change HT OP mode to 01 if any overlap protection enabled
                    if(psessionEntry->gLimOlbcParams.protectionEnabled ||
                            psessionEntry->gLimOverlap11gParams.protectionEnabled ||
                            psessionEntry->gLimOverlapHt20Params.protectionEnabled ||
                            psessionEntry->gLimOverlapNonGfParams.protectionEnabled)
                    {
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                        PELOGE(limLog(pMac, LOGE, FL("===> 11G Protection Disabled"));)
                        limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                    }
                    else if(psessionEntry->gLimHt20Params.protectionEnabled)
                    {
                        //Commenting because of CR 258588 WFA cert
                        //psessionEntry->htOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                        PELOGE(limLog(pMac, LOGE, FL("===> 11G Protection Disabled"));)
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    }
                    else
                    {
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    }
                }
            }
            if(!psessionEntry->gLimOlbcParams.protectionEnabled &&
                    !psessionEntry->gLim11bParams.protectionEnabled)
            {
                PELOGE(limLog(pMac, LOGE, FL("===> 11G Protection Disabled"));)
                pBeaconParams->llbCoexist = psessionEntry->beaconParams.llbCoexist = false;
                pBeaconParams->paramChangeBitmap |= PARAM_llBCOEXIST_CHANGED;
            }
        }else if(eLIM_BT_AMP_AP_ROLE == psessionEntry->limSystemRole)
            {
                if(overlap)
                {
                    //Overlap Legacy protection disabled.
                psessionEntry->gLimOlbcParams.protectionEnabled = false;

                    //We need to take care of HT OP mode iff we are HT AP.
                    if(psessionEntry->htCapability)
                    {
                        // no HT op mode change if any of the overlap protection enabled.
                        if(!(pMac->lim.gLimOverlap11gParams.protectionEnabled ||
                                    pMac->lim.gLimOverlapHt20Params.protectionEnabled ||
                                    pMac->lim.gLimOverlapNonGfParams.protectionEnabled))

                        {
                            //Check if there is a need to change HT OP mode.
                            if(eSIR_HT_OP_MODE_OVERLAP_LEGACY == pMac->lim.gHTOperMode)
                            {
                                limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                                limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);            
                            if(psessionEntry->gLimHt20Params.protectionEnabled)
                                    pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                                else
                                    pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_PURE;
                            }
                        }
                    }
                }
                else
                {
                    //Disable protection from 11B stations.
                psessionEntry->gLim11bParams.protectionEnabled = false;
                    //Check if any other non-HT protection enabled.
                if(!psessionEntry->gLim11gParams.protectionEnabled)
                    {
                        //Right now we are in HT OP Mixed mode.
                        //Change HT op mode appropriately.
                        limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);            

                        //Change HT OP mode to 01 if any overlap protection enabled
                    if(psessionEntry->gLimOlbcParams.protectionEnabled ||
                                pMac->lim.gLimOverlap11gParams.protectionEnabled ||
                                pMac->lim.gLimOverlapHt20Params.protectionEnabled ||
                                pMac->lim.gLimOverlapNonGfParams.protectionEnabled)

                        {
                            pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                            limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                        }
                    else if(psessionEntry->gLimHt20Params.protectionEnabled)
                        {
                            pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                            limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                        }
                        else
                        {
                            pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_PURE;
                            limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                        }
                    }
                }
            if(!psessionEntry->gLimOlbcParams.protectionEnabled &&
                  !psessionEntry->gLim11bParams.protectionEnabled)
                {
                    PELOG1(limLog(pMac, LOG1, FL("===> 11G Protection Disabled"));)
                pBeaconParams->llbCoexist = psessionEntry->beaconParams.llbCoexist = false;
                    pBeaconParams->paramChangeBitmap |= PARAM_llBCOEXIST_CHANGED;
                }
            }
        //for station role
            else
            {
                PELOG1(limLog(pMac, LOG1, FL("===> 11G Protection Disabled"));)
            pBeaconParams->llbCoexist = psessionEntry->beaconParams.llbCoexist = false;
                pBeaconParams->paramChangeBitmap |= PARAM_llBCOEXIST_CHANGED;
            }
    }
    return eSIR_SUCCESS;
}
    
/** -------------------------------------------------------------
\fn limEnableHtProtectionFrom11g
\brief based on cofig enables\disables protection from 11g.
\param      tANI_U8 enable : 1=> enable protection, 0=> disable protection.
\param      tANI_U8 overlap: 1=> called from overlap context, 0 => called from assoc context.
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/
tSirRetStatus
limEnableHtProtectionFrom11g(tpAniSirGlobal pMac, tANI_U8 enable,
    tANI_U8 overlap, tpUpdateBeaconParams pBeaconParams,tpPESession psessionEntry)
{
    if(!psessionEntry->htCapability)
        return eSIR_SUCCESS; // protection from 11g is only for HT stations.

    //overlapping protection configuration check.
    if(overlap)
    {
        if((psessionEntry->limSystemRole == eLIM_AP_ROLE ) && (!psessionEntry->cfgProtection.overlapFromllg))
        {
            // protection disabled.
            PELOG3(limLog(pMac, LOG3, FL("overlap protection from 11g is disabled")););
            return eSIR_SUCCESS;
        }else if ((psessionEntry->limSystemRole == eLIM_BT_AMP_AP_ROLE) && (!pMac->lim.cfgProtection.overlapFromllg))
        {
            // protection disabled.
            PELOG3(limLog(pMac, LOG3, FL("overlap protection from 11g is disabled")););
            return eSIR_SUCCESS;
        }
        }
    else
    {
        //normal protection config check
       if((psessionEntry->limSystemRole == eLIM_AP_ROLE ) && 
           !psessionEntry->cfgProtection.fromllg){
            // protection disabled.
            PELOG3(limLog(pMac, LOG3, FL("protection from 11g is disabled"));)
            return eSIR_SUCCESS;
         }else if(psessionEntry->limSystemRole != eLIM_AP_ROLE )
       {
          if(!pMac->lim.cfgProtection.fromllg)
           {
                // protection disabled.
                PELOG3(limLog(pMac, LOG3, FL("protection from 11g is disabled"));)
                return eSIR_SUCCESS;
            }
        }
     }
    if (enable)
    {
        //If we are AP and HT capable, we need to set the HT OP mode
        //appropriately.

        if(eLIM_AP_ROLE == psessionEntry->limSystemRole)
        {
            if(overlap)
            {
                psessionEntry->gLimOverlap11gParams.protectionEnabled = true;
                //11g exists in overlap BSS.
                //need not to change the operating mode to overlap_legacy
                //if higher or same protection operating mode is enabled right now.
                if((eSIR_HT_OP_MODE_OVERLAP_LEGACY != psessionEntry->htOperMode) &&
                    (eSIR_HT_OP_MODE_MIXED != psessionEntry->htOperMode))
                {
                    psessionEntry->htOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                }
                limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                limEnableHtOBSSProtection(pMac,  true, overlap, pBeaconParams, psessionEntry);
            }
            else
            {
                //11g is associated to an AP operating in 11n mode.
                //Change the HT operating mode to 'mixed mode'.
                psessionEntry->gLim11gParams.protectionEnabled = true;
                if(eSIR_HT_OP_MODE_MIXED != psessionEntry->htOperMode)
                {
                    psessionEntry->htOperMode = eSIR_HT_OP_MODE_MIXED;
                    limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                    limEnableHtOBSSProtection(pMac,  true, overlap, pBeaconParams,psessionEntry);
                }
            }
        }else if(eLIM_BT_AMP_AP_ROLE == psessionEntry->limSystemRole)
        {
            if(overlap)
            {
                pMac->lim.gLimOverlap11gParams.protectionEnabled = true;
                //11g exists in overlap BSS.
                //need not to change the operating mode to overlap_legacy
                //if higher or same protection operating mode is enabled right now.
                if((eSIR_HT_OP_MODE_OVERLAP_LEGACY != pMac->lim.gHTOperMode) &&
                    (eSIR_HT_OP_MODE_MIXED != pMac->lim.gHTOperMode))
                {
                    pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                    limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                }
            }
            else
            {
                //11g is associated to an AP operating in 11n mode.
                //Change the HT operating mode to 'mixed mode'.
                psessionEntry->gLim11gParams.protectionEnabled = true;
                if(eSIR_HT_OP_MODE_MIXED != pMac->lim.gHTOperMode)
                {
                    pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_MIXED;
                    limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                    limEnableHtOBSSProtection(pMac,  true, overlap, pBeaconParams,psessionEntry);
                }
            }
        }

        //This part is common for staiton as well.
        if(false == psessionEntry->beaconParams.llgCoexist)
        {
            pBeaconParams->llgCoexist = psessionEntry->beaconParams.llgCoexist = true;
            pBeaconParams->paramChangeBitmap |= PARAM_llGCOEXIST_CHANGED;
        }
        else if (true == psessionEntry->gLimOverlap11gParams.protectionEnabled)
        {
            // As operating mode changed after G station assoc some way to update beacon
            // This addresses the issue of mode not changing to - 11 in beacon when OBSS overlap is enabled
            //pMac->sch.schObject.fBeaconChanged = 1;
            pBeaconParams->paramChangeBitmap |= PARAM_llGCOEXIST_CHANGED;
        }
    }
    else if (true == psessionEntry->beaconParams.llgCoexist)
    {
        //for AP role.
        //we need to take care of HT OP mode change if needed.
        //We need to take care of Overlap cases.

        if(eLIM_AP_ROLE == psessionEntry->limSystemRole)
        {
            if(overlap)
            {
                //Overlap Legacy protection disabled.
                if (psessionEntry->gLim11gParams.numSta == 0)
                psessionEntry->gLimOverlap11gParams.protectionEnabled = false;

                // no HT op mode change if any of the overlap protection enabled.
                if(!(psessionEntry->gLimOlbcParams.protectionEnabled ||
                    psessionEntry->gLimOverlapHt20Params.protectionEnabled ||
                    psessionEntry->gLimOverlapNonGfParams.protectionEnabled))
                {
                    //Check if there is a need to change HT OP mode.
                    if(eSIR_HT_OP_MODE_OVERLAP_LEGACY == psessionEntry->htOperMode)
                    {
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                        limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);            

                        if(psessionEntry->gLimHt20Params.protectionEnabled){
                            //Commenting because of CR 258588 WFA cert
                            //psessionEntry->htOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                            psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                        }
                        else
                            psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                    }
                }
            }
            else
            {
                //Disable protection from 11G stations.
                psessionEntry->gLim11gParams.protectionEnabled = false;
                //Check if any other non-HT protection enabled.
                if(!psessionEntry->gLim11bParams.protectionEnabled)
                {

                    //Right now we are in HT OP Mixed mode.
                    //Change HT op mode appropriately.
                    limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);            

                    //Change HT OP mode to 01 if any overlap protection enabled
                    if(psessionEntry->gLimOlbcParams.protectionEnabled ||
                        psessionEntry->gLimOverlap11gParams.protectionEnabled ||
                        psessionEntry->gLimOverlapHt20Params.protectionEnabled ||
                        psessionEntry->gLimOverlapNonGfParams.protectionEnabled)

                    {
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                        limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                    }
                    else if(psessionEntry->gLimHt20Params.protectionEnabled)
                    {
                        //Commenting because of CR 258588 WFA cert
                        //psessionEntry->htOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    }
                    else
                    {
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    }
                }
            }
            if(!psessionEntry->gLimOverlap11gParams.protectionEnabled &&
                  !psessionEntry->gLim11gParams.protectionEnabled)
            {
                PELOG1(limLog(pMac, LOG1, FL("===> Protection from 11G Disabled"));)
                pBeaconParams->llgCoexist = psessionEntry->beaconParams.llgCoexist = false;
                pBeaconParams->paramChangeBitmap |= PARAM_llGCOEXIST_CHANGED;
            }
        }else if(eLIM_BT_AMP_AP_ROLE == psessionEntry->limSystemRole)
        {
            if(overlap)
            {
                //Overlap Legacy protection disabled.
                pMac->lim.gLimOverlap11gParams.protectionEnabled = false;

                // no HT op mode change if any of the overlap protection enabled.
                if(!(psessionEntry->gLimOlbcParams.protectionEnabled ||
                    psessionEntry->gLimOverlapHt20Params.protectionEnabled ||
                    psessionEntry->gLimOverlapNonGfParams.protectionEnabled))
                {
                    //Check if there is a need to change HT OP mode.
                    if(eSIR_HT_OP_MODE_OVERLAP_LEGACY == pMac->lim.gHTOperMode)
                    {
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                        limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);            

                        if(psessionEntry->gLimHt20Params.protectionEnabled)
                            pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                        else
                            pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_PURE;
                    }
                }
            }
            else
            {
                //Disable protection from 11G stations.
                psessionEntry->gLim11gParams.protectionEnabled = false;
                //Check if any other non-HT protection enabled.
                if(!psessionEntry->gLim11bParams.protectionEnabled)
                {

                    //Right now we are in HT OP Mixed mode.
                    //Change HT op mode appropriately.
                    limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);            

                    //Change HT OP mode to 01 if any overlap protection enabled
                    if(psessionEntry->gLimOlbcParams.protectionEnabled ||
                        pMac->lim.gLimOverlap11gParams.protectionEnabled ||
                        pMac->lim.gLimOverlapHt20Params.protectionEnabled ||
                        pMac->lim.gLimOverlapNonGfParams.protectionEnabled)

                    {
                        pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                        limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                    }
                    else if(psessionEntry->gLimHt20Params.protectionEnabled)
                    {
                        pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    }
                    else
                    {
                        pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_PURE;
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    }
                }
            }
            if(!pMac->lim.gLimOverlap11gParams.protectionEnabled &&
                  !psessionEntry->gLim11gParams.protectionEnabled)
            {
                PELOG1(limLog(pMac, LOG1, FL("===> Protection from 11G Disabled"));)
                pBeaconParams->llgCoexist = psessionEntry->beaconParams.llgCoexist = false;
                pBeaconParams->paramChangeBitmap |= PARAM_llGCOEXIST_CHANGED;
            }
        }
        //for station role
        else
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Protection from 11G Disabled"));)
            pBeaconParams->llgCoexist = psessionEntry->beaconParams.llgCoexist = false;
            pBeaconParams->paramChangeBitmap |= PARAM_llGCOEXIST_CHANGED;
        }
    }
    return eSIR_SUCCESS;
}
//FIXME_PROTECTION : need to check for no APSD whenever we want to enable this protection.
//This check will be done at the caller.

/** -------------------------------------------------------------
\fn limEnableHtObssProtection
\brief based on cofig enables\disables obss protection.
\param      tANI_U8 enable : 1=> enable protection, 0=> disable protection.
\param      tANI_U8 overlap: 1=> called from overlap context, 0 => called from assoc context.
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/
tSirRetStatus
limEnableHtOBSSProtection(tpAniSirGlobal pMac, tANI_U8 enable,
    tANI_U8 overlap, tpUpdateBeaconParams pBeaconParams,tpPESession psessionEntry)
{


    if(!psessionEntry->htCapability)
        return eSIR_SUCCESS; // this protection  is only for HT stations.

    //overlapping protection configuration check.
    if(overlap)
    {
        //overlapping protection configuration check.
    } 
    else 
    {
        //normal protection config check
        if((psessionEntry->limSystemRole == eLIM_AP_ROLE) && !psessionEntry->cfgProtection.obss) 
        { //ToDo Update this field
            // protection disabled.
            PELOG1(limLog(pMac, LOG1, FL("protection from Obss is disabled"));)
            return eSIR_SUCCESS;
        }else if(psessionEntry->limSystemRole != eLIM_AP_ROLE)
        {
            if(!pMac->lim.cfgProtection.obss) 
            { //ToDo Update this field
                // protection disabled.
                PELOG1(limLog(pMac, LOG1, FL("protection from Obss is disabled"));)
                return eSIR_SUCCESS;
            }
        }
    }


    if (eLIM_AP_ROLE == psessionEntry->limSystemRole){
        if ((enable) && (false == psessionEntry->beaconParams.gHTObssMode) )
        {
            PELOG1(limLog(pMac, LOG1, FL("=>obss protection enabled"));)
            psessionEntry->beaconParams.gHTObssMode = true;
            pBeaconParams->paramChangeBitmap |= PARAM_OBSS_MODE_CHANGED; // UPDATE AN ENUM FOR OBSS MODE <todo>
        
         }
         else if (!enable && (true == psessionEntry->beaconParams.gHTObssMode)) 
         {
            PELOG1(limLog(pMac, LOG1, FL("===> obss Protection disabled"));)
            psessionEntry->beaconParams.gHTObssMode = false;
            pBeaconParams->paramChangeBitmap |= PARAM_OBSS_MODE_CHANGED;

         }
//CR-263021: OBSS bit is not switching back to 0 after disabling the overlapping legacy BSS
         if (!enable && !overlap)
         {
             psessionEntry->gLimOverlap11gParams.protectionEnabled = false;
         }
    } else
    {
        if ((enable) && (false == psessionEntry->beaconParams.gHTObssMode) )
        {
            PELOG1(limLog(pMac, LOG1, FL("=>obss protection enabled"));)
            psessionEntry->beaconParams.gHTObssMode = true;
            pBeaconParams->paramChangeBitmap |= PARAM_OBSS_MODE_CHANGED; // UPDATE AN ENUM FOR OBSS MODE <todo>

        }
        else if (!enable && (true == psessionEntry->beaconParams.gHTObssMode)) 
        {

            PELOG1(limLog(pMac, LOG1, FL("===> obss Protection disabled"));)
            psessionEntry->beaconParams.gHTObssMode = false;
            pBeaconParams->paramChangeBitmap |= PARAM_OBSS_MODE_CHANGED;

        }
    }
    return eSIR_SUCCESS;
}
/** -------------------------------------------------------------
\fn limEnableHT20Protection
\brief based on cofig enables\disables protection from Ht20.
\param      tANI_U8 enable : 1=> enable protection, 0=> disable protection.
\param      tANI_U8 overlap: 1=> called from overlap context, 0 => called from assoc context.
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/
tSirRetStatus
limEnableHT20Protection(tpAniSirGlobal pMac, tANI_U8 enable,
    tANI_U8 overlap, tpUpdateBeaconParams pBeaconParams,tpPESession psessionEntry)
{
    if(!psessionEntry->htCapability)
        return eSIR_SUCCESS; // this protection  is only for HT stations.

        //overlapping protection configuration check.
        if(overlap)
        {
        }
        else
        {
            //normal protection config check
            if((psessionEntry->limSystemRole == eLIM_AP_ROLE ) &&
                !psessionEntry->cfgProtection.ht20)
            {
                // protection disabled.
                PELOG3(limLog(pMac, LOG3, FL("protection from HT20 is disabled"));)
                return eSIR_SUCCESS;
            }else if(psessionEntry->limSystemRole != eLIM_AP_ROLE )
            {
                if(!pMac->lim.cfgProtection.ht20)
                {
                    // protection disabled.
                    PELOG3(limLog(pMac, LOG3, FL("protection from HT20 is disabled"));)
                    return eSIR_SUCCESS;
                }
            }
        }

    if (enable)
    {
        //If we are AP and HT capable, we need to set the HT OP mode
        //appropriately.

        if(eLIM_AP_ROLE == psessionEntry->limSystemRole){
            if(overlap)
            {
                psessionEntry->gLimOverlapHt20Params.protectionEnabled = true;
                if((eSIR_HT_OP_MODE_OVERLAP_LEGACY != psessionEntry->htOperMode) &&
                    (eSIR_HT_OP_MODE_MIXED != psessionEntry->htOperMode))
                {
                    psessionEntry->htOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                    limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                }
            }
            else
            {
               psessionEntry->gLimHt20Params.protectionEnabled = true;
                if(eSIR_HT_OP_MODE_PURE == psessionEntry->htOperMode)
                {
                    //Commenting because of CR 258588 WFA cert
                    //psessionEntry->htOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                    psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                    limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);
                }
            }
        }else if(eLIM_BT_AMP_AP_ROLE == psessionEntry->limSystemRole)
        {
            if(overlap)
            {
                pMac->lim.gLimOverlapHt20Params.protectionEnabled = true;
                if((eSIR_HT_OP_MODE_OVERLAP_LEGACY != pMac->lim.gHTOperMode) &&
                    (eSIR_HT_OP_MODE_MIXED != pMac->lim.gHTOperMode))
                {
                    pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                    limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                }
            }
            else
            {
                psessionEntry->gLimHt20Params.protectionEnabled = true;
                if(eSIR_HT_OP_MODE_PURE == pMac->lim.gHTOperMode)
                {
                    pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                    limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);
                }
            }
        }

        //This part is common for staiton as well.
        if(false == psessionEntry->beaconParams.ht20Coexist)
        {
            PELOG1(limLog(pMac, LOG1, FL("=> Prtection from HT20 Enabled"));)
            pBeaconParams->ht20MhzCoexist = psessionEntry->beaconParams.ht20Coexist = true;
            pBeaconParams->paramChangeBitmap |= PARAM_HT20MHZCOEXIST_CHANGED;
        }
    }
    else if (true == psessionEntry->beaconParams.ht20Coexist)
    {
        //for AP role.
        //we need to take care of HT OP mode change if needed.
        //We need to take care of Overlap cases.
        if(eLIM_AP_ROLE == psessionEntry->limSystemRole){
            if(overlap)
            {
                //Overlap Legacy protection disabled.
                psessionEntry->gLimOverlapHt20Params.protectionEnabled = false;

                // no HT op mode change if any of the overlap protection enabled.
                if(!(psessionEntry->gLimOlbcParams.protectionEnabled ||
                    psessionEntry->gLimOverlap11gParams.protectionEnabled ||
                    psessionEntry->gLimOverlapHt20Params.protectionEnabled ||
                    psessionEntry->gLimOverlapNonGfParams.protectionEnabled))
                {

                    //Check if there is a need to change HT OP mode.
                    if(eSIR_HT_OP_MODE_OVERLAP_LEGACY == psessionEntry->htOperMode)
                    {
                        if(psessionEntry->gLimHt20Params.protectionEnabled)
                        {
                            //Commented beacuse of CR 258588 for WFA Cert
                            //psessionEntry->htOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                            psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                            limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                            limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);            
                        }
                        else
                        {
                            psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                        }
                    }
                }
            }
            else
            {
                //Disable protection from 11G stations.
                psessionEntry->gLimHt20Params.protectionEnabled = false;

                //Change HT op mode appropriately.
                if(eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT == psessionEntry->htOperMode)
                {
                    psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                    limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);        
                }
            }
            PELOG1(limLog(pMac, LOG1, FL("===> Protection from HT 20 Disabled"));)
            pBeaconParams->ht20MhzCoexist = psessionEntry->beaconParams.ht20Coexist = false;
            pBeaconParams->paramChangeBitmap |= PARAM_HT20MHZCOEXIST_CHANGED;
        }else if(eLIM_BT_AMP_AP_ROLE == psessionEntry->limSystemRole)
        {
            if(overlap)
            {
                //Overlap Legacy protection disabled.
                pMac->lim.gLimOverlapHt20Params.protectionEnabled = false;

                // no HT op mode change if any of the overlap protection enabled.
                if(!(psessionEntry->gLimOlbcParams.protectionEnabled ||
                    pMac->lim.gLimOverlap11gParams.protectionEnabled ||
                    pMac->lim.gLimOverlapHt20Params.protectionEnabled ||
                    pMac->lim.gLimOverlapNonGfParams.protectionEnabled))
                {

                    //Check if there is a need to change HT OP mode.
                    if(eSIR_HT_OP_MODE_OVERLAP_LEGACY == pMac->lim.gHTOperMode)
                    {
                        if(psessionEntry->gLimHt20Params.protectionEnabled)
                        {
                            pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                            limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                            limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);            
                        }
                        else
                        {
                            pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_PURE;
                        }
                    }
                }
            }
            else
            {
                //Disable protection from 11G stations.
                psessionEntry->gLimHt20Params.protectionEnabled = false;

                //Change HT op mode appropriately.
                if(eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT == pMac->lim.gHTOperMode)
                {
                    pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_PURE;
                    limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);        
                }
            }
            PELOG1(limLog(pMac, LOG1, FL("===> Protection from HT 20 Disabled"));)
            pBeaconParams->ht20MhzCoexist = psessionEntry->beaconParams.ht20Coexist = false;
            pBeaconParams->paramChangeBitmap |= PARAM_HT20MHZCOEXIST_CHANGED;
        }
        //for station role
        else
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Protection from HT20 Disabled"));)
            pBeaconParams->ht20MhzCoexist = psessionEntry->beaconParams.ht20Coexist = false;
            pBeaconParams->paramChangeBitmap |= PARAM_HT20MHZCOEXIST_CHANGED;
        }
    }

    return eSIR_SUCCESS;
}

/** -------------------------------------------------------------
\fn limEnableHTNonGfProtection
\brief based on cofig enables\disables protection from NonGf.
\param      tANI_U8 enable : 1=> enable protection, 0=> disable protection.
\param      tANI_U8 overlap: 1=> called from overlap context, 0 => called from assoc context.
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/
tSirRetStatus
limEnableHTNonGfProtection(tpAniSirGlobal pMac, tANI_U8 enable,
    tANI_U8 overlap, tpUpdateBeaconParams pBeaconParams,tpPESession psessionEntry)
{
    if(!psessionEntry->htCapability)
        return eSIR_SUCCESS; // this protection  is only for HT stations.

        //overlapping protection configuration check.
        if(overlap)
        {
        }
        else
        {
            //normal protection config check
            if((psessionEntry->limSystemRole == eLIM_AP_ROLE ) &&
                !psessionEntry->cfgProtection.nonGf)
            {
                // protection disabled.
                PELOG3(limLog(pMac, LOG3, FL("protection from NonGf is disabled"));)
                return eSIR_SUCCESS;
            }else if(psessionEntry->limSystemRole != eLIM_AP_ROLE)
            {
                //normal protection config check
                if(!pMac->lim.cfgProtection.nonGf)
                {
                    // protection disabled.
                    PELOG3(limLog(pMac, LOG3, FL("protection from NonGf is disabled"));)
                    return eSIR_SUCCESS;
                 }
            }
        }
    if(psessionEntry->limSystemRole == eLIM_AP_ROLE){
        if ((enable) && (false == psessionEntry->beaconParams.llnNonGFCoexist))
        {
            PELOG1(limLog(pMac, LOG1, FL(" => Prtection from non GF Enabled"));)
            pBeaconParams->llnNonGFCoexist = psessionEntry->beaconParams.llnNonGFCoexist = true;
            pBeaconParams->paramChangeBitmap |= PARAM_NON_GF_DEVICES_PRESENT_CHANGED;
        }
        else if (!enable && (true == psessionEntry->beaconParams.llnNonGFCoexist))
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Protection from Non GF Disabled"));)
            pBeaconParams->llnNonGFCoexist = psessionEntry->beaconParams.llnNonGFCoexist = false;
            pBeaconParams->paramChangeBitmap |= PARAM_NON_GF_DEVICES_PRESENT_CHANGED;
        }
    }else
    {
        if ((enable) && (false == psessionEntry->beaconParams.llnNonGFCoexist))
        {
            PELOG1(limLog(pMac, LOG1, FL(" => Prtection from non GF Enabled"));)
            pBeaconParams->llnNonGFCoexist = psessionEntry->beaconParams.llnNonGFCoexist = true;
            pBeaconParams->paramChangeBitmap |= PARAM_NON_GF_DEVICES_PRESENT_CHANGED;
        }
        else if (!enable && (true == psessionEntry->beaconParams.llnNonGFCoexist))
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Protection from Non GF Disabled"));)
            pBeaconParams->llnNonGFCoexist = psessionEntry->beaconParams.llnNonGFCoexist = false;
            pBeaconParams->paramChangeBitmap |= PARAM_NON_GF_DEVICES_PRESENT_CHANGED;
        }
    }

    return eSIR_SUCCESS;
}

/** -------------------------------------------------------------
\fn limEnableHTLsigTxopProtection
\brief based on cofig enables\disables LsigTxop protection.
\param      tANI_U8 enable : 1=> enable protection, 0=> disable protection.
\param      tANI_U8 overlap: 1=> called from overlap context, 0 => called from assoc context.
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/
tSirRetStatus
limEnableHTLsigTxopProtection(tpAniSirGlobal pMac, tANI_U8 enable,
    tANI_U8 overlap, tpUpdateBeaconParams pBeaconParams,tpPESession psessionEntry)
{
    if(!psessionEntry->htCapability)
        return eSIR_SUCCESS; // this protection  is only for HT stations.

        //overlapping protection configuration check.
        if(overlap)
        {
        }
        else
        {
            //normal protection config check
            if((psessionEntry->limSystemRole == eLIM_AP_ROLE ) &&
               !psessionEntry->cfgProtection.lsigTxop)
            {
                // protection disabled.
                PELOG3(limLog(pMac, LOG3, FL(" protection from LsigTxop not supported is disabled"));)
                return eSIR_SUCCESS;
            }else if(psessionEntry->limSystemRole != eLIM_AP_ROLE)
            {
                //normal protection config check
                if(!pMac->lim.cfgProtection.lsigTxop)
                {
                    // protection disabled.
                    PELOG3(limLog(pMac, LOG3, FL(" protection from LsigTxop not supported is disabled"));)
                    return eSIR_SUCCESS;
                }
            }
        }


    if(psessionEntry->limSystemRole == eLIM_AP_ROLE){
        if ((enable) && (false == psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport))
        {
            PELOG1(limLog(pMac, LOG1, FL(" => Prtection from LsigTxop Enabled"));)
            pBeaconParams->fLsigTXOPProtectionFullSupport = psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport = true;
            pBeaconParams->paramChangeBitmap |= PARAM_LSIG_TXOP_FULL_SUPPORT_CHANGED;
        }
        else if (!enable && (true == psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport))
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Protection from LsigTxop Disabled"));)
            pBeaconParams->fLsigTXOPProtectionFullSupport= psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport = false;
            pBeaconParams->paramChangeBitmap |= PARAM_LSIG_TXOP_FULL_SUPPORT_CHANGED;
        }
    }else
    {
        if ((enable) && (false == psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport))
        {
            PELOG1(limLog(pMac, LOG1, FL(" => Prtection from LsigTxop Enabled"));)
            pBeaconParams->fLsigTXOPProtectionFullSupport = psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport = true;
            pBeaconParams->paramChangeBitmap |= PARAM_LSIG_TXOP_FULL_SUPPORT_CHANGED;
        }
        else if (!enable && (true == psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport))
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Protection from LsigTxop Disabled"));)
            pBeaconParams->fLsigTXOPProtectionFullSupport= psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport = false;
            pBeaconParams->paramChangeBitmap |= PARAM_LSIG_TXOP_FULL_SUPPORT_CHANGED;
        }
    }
    return eSIR_SUCCESS;
}
//FIXME_PROTECTION : need to check for no APSD whenever we want to enable this protection.
//This check will be done at the caller.
/** -------------------------------------------------------------
\fn limEnableHtRifsProtection
\brief based on cofig enables\disables Rifs protection.
\param      tANI_U8 enable : 1=> enable protection, 0=> disable protection.
\param      tANI_U8 overlap: 1=> called from overlap context, 0 => called from assoc context.
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/
tSirRetStatus
limEnableHtRifsProtection(tpAniSirGlobal pMac, tANI_U8 enable,
    tANI_U8 overlap, tpUpdateBeaconParams pBeaconParams,tpPESession psessionEntry)
{
    if(!psessionEntry->htCapability)
        return eSIR_SUCCESS; // this protection  is only for HT stations.


        //overlapping protection configuration check.
        if(overlap)
        {
        }
        else
        {
             //normal protection config check
            if((psessionEntry->limSystemRole == eLIM_AP_ROLE) &&
               !psessionEntry->cfgProtection.rifs)
            {
                // protection disabled.
                PELOG3(limLog(pMac, LOG3, FL(" protection from Rifs is disabled"));)
                return eSIR_SUCCESS;
            }else if(psessionEntry->limSystemRole != eLIM_AP_ROLE )
            {
               //normal protection config check
               if(!pMac->lim.cfgProtection.rifs)
               {
                  // protection disabled.
                  PELOG3(limLog(pMac, LOG3, FL(" protection from Rifs is disabled"));)
                  return eSIR_SUCCESS;
               }
            }
        }

    if(psessionEntry->limSystemRole == eLIM_AP_ROLE){
        // Disabling the RIFS Protection means Enable the RIFS mode of operation in the BSS
        if ((!enable) && (false == psessionEntry->beaconParams.fRIFSMode))
        {
            PELOG1(limLog(pMac, LOG1, FL(" => Rifs protection Disabled"));)
            pBeaconParams->fRIFSMode = psessionEntry->beaconParams.fRIFSMode = true;
            pBeaconParams->paramChangeBitmap |= PARAM_RIFS_MODE_CHANGED;
        }
        // Enabling the RIFS Protection means Disable the RIFS mode of operation in the BSS
        else if (enable && (true == psessionEntry->beaconParams.fRIFSMode))
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Rifs Protection Enabled"));)
            pBeaconParams->fRIFSMode = psessionEntry->beaconParams.fRIFSMode = false;
            pBeaconParams->paramChangeBitmap |= PARAM_RIFS_MODE_CHANGED;
        }
    }else
    {
        // Disabling the RIFS Protection means Enable the RIFS mode of operation in the BSS
        if ((!enable) && (false == psessionEntry->beaconParams.fRIFSMode))
        {
            PELOG1(limLog(pMac, LOG1, FL(" => Rifs protection Disabled"));)
            pBeaconParams->fRIFSMode = psessionEntry->beaconParams.fRIFSMode = true;
            pBeaconParams->paramChangeBitmap |= PARAM_RIFS_MODE_CHANGED;
        }
    // Enabling the RIFS Protection means Disable the RIFS mode of operation in the BSS
        else if (enable && (true == psessionEntry->beaconParams.fRIFSMode))
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Rifs Protection Enabled"));)
            pBeaconParams->fRIFSMode = psessionEntry->beaconParams.fRIFSMode = false;
            pBeaconParams->paramChangeBitmap |= PARAM_RIFS_MODE_CHANGED;
        }
    }
    return eSIR_SUCCESS;
}

// ---------------------------------------------------------------------
/**
 * limEnableShortPreamble
 *
 * FUNCTION:
 * Enable/Disable short preamble
 *
 * LOGIC:
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @param enable        Flag to enable/disable short preamble
 * @return None
 */

tSirRetStatus
limEnableShortPreamble(tpAniSirGlobal pMac, tANI_U8 enable, tpUpdateBeaconParams pBeaconParams, tpPESession psessionEntry)
{
    tANI_U32 val;

    if (wlan_cfgGetInt(pMac, WNI_CFG_SHORT_PREAMBLE, &val) != eSIR_SUCCESS)
    {
        /* Could not get short preamble enabled flag from CFG. Log error. */
        limLog(pMac, LOGP, FL("could not retrieve short preamble flag"));
        return eSIR_FAILURE;
    }

    if (!val)  
        return eSIR_SUCCESS;

    if (wlan_cfgGetInt(pMac, WNI_CFG_11G_SHORT_PREAMBLE_ENABLED, &val) != eSIR_SUCCESS)
    {
        limLog(pMac, LOGP, FL("could not retrieve 11G short preamble switching  enabled flag"));
        return eSIR_FAILURE;
    }

    if (!val)   // 11G short preamble switching is disabled.
        return eSIR_SUCCESS;

    if ( psessionEntry->limSystemRole == eLIM_AP_ROLE )
    {
        if (enable && (psessionEntry->beaconParams.fShortPreamble == 0))
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Short Preamble Enabled"));)
            psessionEntry->beaconParams.fShortPreamble = true;
            pBeaconParams->fShortPreamble = (tANI_U8) psessionEntry->beaconParams.fShortPreamble;
            pBeaconParams->paramChangeBitmap |= PARAM_SHORT_PREAMBLE_CHANGED;
        }
        else if (!enable && (psessionEntry->beaconParams.fShortPreamble == 1))
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Short Preamble Disabled"));)
            psessionEntry->beaconParams.fShortPreamble = false;
            pBeaconParams->fShortPreamble = (tANI_U8) psessionEntry->beaconParams.fShortPreamble;
            pBeaconParams->paramChangeBitmap |= PARAM_SHORT_PREAMBLE_CHANGED;
        }
    }

    return eSIR_SUCCESS;
        }

/**
 * limTxComplete
 *
 * Function:
 * This is LIM's very own "TX MGMT frame complete" completion routine.
 *
 * Logic:
 * LIM wants to send a MGMT frame (broadcast or unicast)
 * LIM allocates memory using palPktAlloc( ..., **pData, **pPacket )
 * LIM transmits the MGMT frame using the API:
 *  halTxFrame( ... pPacket, ..., (void *) limTxComplete, pData )
 * HDD, via halTxFrame/DXE, "transfers" the packet over to BMU
 * HDD, if it determines that a TX completion routine (in this case
 * limTxComplete) has been provided, will invoke this callback
 * LIM will try to free the TX MGMT packet that was earlier allocated, in order
 * to send this MGMT frame, using the PAL API palPktFree( ... pData, pPacket )
 *
 * Assumptions:
 * Presently, this is ONLY being used for MGMT frames/packets
 * TODO:
 * Would it do good for LIM to have some sort of "signature" validation to
 * ensure that the pData argument passed in was a buffer that was actually
 * allocated by LIM and/or is not corrupted?
 *
 * Note: FIXME and TODO
 * Looks like palPktFree() is interested in pPacket. But, when this completion
 * routine is called, only pData is made available to LIM!!
 *
 * @param void A pointer to pData. Shouldn't it be pPacket?!
 *
 * @return none
 */
void limTxComplete( tHalHandle hHal, void *pData )
{
  tpAniSirGlobal pMac;
  pMac = (tpAniSirGlobal)hHal;

#ifdef FIXME_PRIMA
  /* the trace logic needs to be fixed for Prima.  Refer to CR 306075 */
#ifdef TRACE_RECORD
    {
        tpSirMacMgmtHdr mHdr;
        v_U8_t         *pRxBd;
        vos_pkt_t      *pVosPkt;
        VOS_STATUS      vosStatus;



        pVosPkt = (vos_pkt_t *)pData;
        vosStatus = vos_pkt_peek_data( pVosPkt, 0, (v_PVOID_t *)&pRxBd, WLANHAL_RX_BD_HEADER_SIZE);

        if(VOS_IS_STATUS_SUCCESS(vosStatus))
        {
            mHdr = WDA_GET_RX_MAC_HEADER(pRxBd);

        }   
    }
#endif
#endif

  palPktFree( pMac->hHdd,
              HAL_TXRX_FRM_802_11_MGMT,
              (void *) NULL,           // this is ignored and will likely be removed from this API
              (void *) pData );        // lim passed in pPacket in the pData pointer that is given in this completion routine
}

/**
 * \brief This function updates lim global structure, if CB parameters in the BSS
 *  have changed, and sends an indication to HAL also with the
 * updated HT Parameters.
 * This function does not detect the change in the primary channel, that is done as part
 * of channel Swtich IE processing.
 * If STA is configured with '20Mhz only' mode, then this function does not do anything
 * This function changes the CB mode, only if the self capability is set to '20 as well as 40Mhz'
 *
 *
 * \param pMac Pointer to global MAC structure
 *
 * \param pRcvdHTInfo Pointer to HT Info IE obtained from a  Beacon or
 * Probe Response
 *
 * \param bssIdx BSS Index of the Bss to which Station is associated.
 *
 *
 */

void limUpdateStaRunTimeHTSwitchChnlParams( tpAniSirGlobal   pMac,
                                  tDot11fIEHTInfo *pHTInfo,
                                  tANI_U8          bssIdx,
                                  tpPESession      psessionEntry)
{
    ePhyChanBondState secondaryChnlOffset = PHY_SINGLE_CHANNEL_CENTERED;
#if !defined WLAN_FEATURE_VOWIFI  
    tANI_U32 localPwrConstraint;
#endif
    
   //If self capability is set to '20Mhz only', then do not change the CB mode.
   if( !limGetHTCapability( pMac, eHT_SUPPORTED_CHANNEL_WIDTH_SET, psessionEntry ))
        return;

   if ((RF_CHAN_14 >= psessionEntry->currentOperChannel) &&
      psessionEntry->force_24ghz_in_ht20) {
        limLog(pMac, LOG1,
               FL("force_24_gh_in_ht20 is set and channel is 2.4 Ghz"));
        return;
   }

#if !defined WLAN_FEATURE_VOWIFI  
    if(wlan_cfgGetInt(pMac, WNI_CFG_LOCAL_POWER_CONSTRAINT, &localPwrConstraint) != eSIR_SUCCESS) {
        limLog( pMac, LOGP, FL( "Unable to get Local Power Constraint from cfg" ));
        return;
    }
#endif

    if (pMac->ft.ftPEContext.pFTPreAuthReq)
    {
        limLog( pMac, LOGE, FL( "FT PREAUTH channel change is in progress"));
        return;
    }

    /*
     * Do not try to switch channel if RoC is in progress. RoC code path uses
     * pMac->lim.gpLimRemainOnChanReq to notify the upper layers that the device
     * has started listening on the channel requested as part of RoC, if we set
     * pMac->lim.gpLimRemainOnChanReq to NULL as we do below then the
     * upper layers will think that the channel change is not successful and the
     * RoC from the upper layer perspective will never end...
     */
    if (pMac->lim.gpLimRemainOnChanReq)
    {
        limLog( pMac, LOGE, FL( "RoC is in progress"));
        return;
    }

    if ( psessionEntry->htSecondaryChannelOffset != ( tANI_U8 ) pHTInfo->secondaryChannelOffset ||
         psessionEntry->htRecommendedTxWidthSet  != ( tANI_U8 ) pHTInfo->recommendedTxWidthSet )
    {
        psessionEntry->htSecondaryChannelOffset = ( ePhyChanBondState ) pHTInfo->secondaryChannelOffset;
        psessionEntry->htRecommendedTxWidthSet  = ( tANI_U8 ) pHTInfo->recommendedTxWidthSet;
        if ( eHT_CHANNEL_WIDTH_40MHZ == psessionEntry->htRecommendedTxWidthSet )
            secondaryChnlOffset = (ePhyChanBondState)pHTInfo->secondaryChannelOffset;

        // Notify HAL
        limLog( pMac, LOGW,  FL( "Channel Information in HT IE change"
                                 "d; sending notification to HAL." ) );
        limLog( pMac, LOGW,  FL( "Primary Channel: %d, Secondary Chan"
                                 "nel Offset: %d, Channel Width: %d" ),
                pHTInfo->primaryChannel, secondaryChnlOffset,
                psessionEntry->htRecommendedTxWidthSet );
        psessionEntry->channelChangeReasonCode=LIM_SWITCH_CHANNEL_OPERATION;
        pMac->lim.gpchangeChannelCallback = NULL;
        pMac->lim.gpchangeChannelData = NULL;

#if defined WLAN_FEATURE_VOWIFI  
        limSendSwitchChnlParams( pMac, ( tANI_U8 ) pHTInfo->primaryChannel,
                                 secondaryChnlOffset, psessionEntry->maxTxPower, psessionEntry->peSessionId);
#else
        limSendSwitchChnlParams( pMac, ( tANI_U8 ) pHTInfo->primaryChannel,
                                 secondaryChnlOffset, (tPowerdBm)localPwrConstraint, psessionEntry->peSessionId);
#endif

        //In case of IBSS, if STA should update HT Info IE in its beacons.
       if (eLIM_STA_IN_IBSS_ROLE == psessionEntry->limSystemRole)
        {
            schSetFixedBeaconFields(pMac,psessionEntry);
        }

    }
} // End limUpdateStaRunTimeHTParams.

/**
 * \brief This function updates the lim global structure, if any of the
 * HT Capabilities have changed.
 *
 *
 * \param pMac Pointer to Global MAC structure
 *
 * \param pHTCapability Pointer to HT Capability Information Element
 * obtained from a Beacon or Probe Response
 *
 *
 *
 */

void limUpdateStaRunTimeHTCapability( tpAniSirGlobal   pMac,
                                      tDot11fIEHTCaps *pHTCaps )
{

    if ( pMac->lim.gHTLsigTXOPProtection != ( tANI_U8 ) pHTCaps->lsigTXOPProtection )
    {
        pMac->lim.gHTLsigTXOPProtection = ( tANI_U8 ) pHTCaps->lsigTXOPProtection;
       // Send change notification to HAL
    }

    if ( pMac->lim.gHTAMpduDensity != ( tANI_U8 ) pHTCaps->mpduDensity )
    {
       pMac->lim.gHTAMpduDensity = ( tANI_U8 ) pHTCaps->mpduDensity;
       // Send change notification to HAL
    }

    if ( pMac->lim.gHTMaxRxAMpduFactor != ( tANI_U8 ) pHTCaps->maxRxAMPDUFactor )
    {
       pMac->lim.gHTMaxRxAMpduFactor = ( tANI_U8 ) pHTCaps->maxRxAMPDUFactor;
       // Send change notification to HAL
    }


} // End limUpdateStaRunTimeHTCapability.

/**
 * \brief This function updates lim global structure, if any of the HT
 * Info Parameters have changed.
 *
 *
 * \param pMac Pointer to the global MAC structure
 *
 * \param pHTInfo Pointer to the HT Info IE obtained from a Beacon or
 * Probe Response
 *
 *
 */

void limUpdateStaRunTimeHTInfo( tpAniSirGlobal  pMac,
                                tDot11fIEHTInfo *pHTInfo, tpPESession psessionEntry)
{
    if ( psessionEntry->htRecommendedTxWidthSet != ( tANI_U8 )pHTInfo->recommendedTxWidthSet )
    {
        psessionEntry->htRecommendedTxWidthSet = ( tANI_U8 )pHTInfo->recommendedTxWidthSet;
        // Send change notification to HAL
    }

    if ( psessionEntry->beaconParams.fRIFSMode != ( tANI_U8 )pHTInfo->rifsMode )
    {
        psessionEntry->beaconParams.fRIFSMode = ( tANI_U8 )pHTInfo->rifsMode;
        // Send change notification to HAL
    }

    if ( pMac->lim.gHTServiceIntervalGranularity != ( tANI_U8 )pHTInfo->serviceIntervalGranularity )
    {
        pMac->lim.gHTServiceIntervalGranularity = ( tANI_U8 )pHTInfo->serviceIntervalGranularity;
        // Send change notification to HAL
    }

    if ( pMac->lim.gHTOperMode != ( tSirMacHTOperatingMode )pHTInfo->opMode )
    {
        pMac->lim.gHTOperMode = ( tSirMacHTOperatingMode )pHTInfo->opMode;
        // Send change notification to HAL
    }

    if ( psessionEntry->beaconParams.llnNonGFCoexist != pHTInfo->nonGFDevicesPresent )
    {
        psessionEntry->beaconParams.llnNonGFCoexist = ( tANI_U8 )pHTInfo->nonGFDevicesPresent;
    }

    if ( pMac->lim.gHTSTBCBasicMCS != ( tANI_U8 )pHTInfo->basicSTBCMCS )
    {
        pMac->lim.gHTSTBCBasicMCS = ( tANI_U8 )pHTInfo->basicSTBCMCS;
        // Send change notification to HAL
    }

    if ( pMac->lim.gHTDualCTSProtection != ( tANI_U8 )pHTInfo->dualCTSProtection )
    {
        pMac->lim.gHTDualCTSProtection = ( tANI_U8 )pHTInfo->dualCTSProtection;
        // Send change notification to HAL
    }

    if ( pMac->lim.gHTSecondaryBeacon != ( tANI_U8 )pHTInfo->secondaryBeacon )
    {
        pMac->lim.gHTSecondaryBeacon = ( tANI_U8 )pHTInfo->secondaryBeacon;
        // Send change notification to HAL
    }

    if ( psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport != ( tANI_U8 )pHTInfo->lsigTXOPProtectionFullSupport )
    {
        psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport = ( tANI_U8 )pHTInfo->lsigTXOPProtectionFullSupport;
        // Send change notification to HAL
    }

    if ( pMac->lim.gHTPCOActive != ( tANI_U8 )pHTInfo->pcoActive )
    {
        pMac->lim.gHTPCOActive = ( tANI_U8 )pHTInfo->pcoActive;
        // Send change notification to HAL
    }

    if ( pMac->lim.gHTPCOPhase != ( tANI_U8 )pHTInfo->pcoPhase )
    {
        pMac->lim.gHTPCOPhase = ( tANI_U8 )pHTInfo->pcoPhase;
        // Send change notification to HAL
    }

} // End limUpdateStaRunTimeHTInfo.


/** -------------------------------------------------------------
\fn limProcessHalIndMessages
\brief callback function for HAL indication
\param   tpAniSirGlobal pMac
\param    tANI_U32 mesgId
\param    void *mesgParam
\return tSirRetStatu - status
  -------------------------------------------------------------*/

tSirRetStatus limProcessHalIndMessages(tpAniSirGlobal pMac, tANI_U32 msgId, void *msgParam )
{
  //its PE's responsibility to free msgparam when its done extracting the message parameters.
  tSirMsgQ msg;

  switch(msgId)
  {
    case SIR_LIM_DEL_TS_IND:
    case SIR_LIM_ADD_BA_IND:    
    case SIR_LIM_DEL_BA_ALL_IND:
    case SIR_LIM_DELETE_STA_CONTEXT_IND:        
    case SIR_LIM_BEACON_GEN_IND:
    case SIR_LIM_DEL_BA_IND:
      msg.type = (tANI_U16) msgId;
      msg.bodyptr = msgParam;
      msg.bodyval = 0;
      break;

    default:
      vos_mem_free(msgParam);
      limLog(pMac, LOGP, FL("invalid message id = %d received"), msgId);
      return eSIR_FAILURE;
  }

  if (limPostMsgApi(pMac, &msg) != eSIR_SUCCESS)
  {
    vos_mem_free(msgParam);
    limLog(pMac, LOGP, FL("limPostMsgApi failed for msgid = %d"), msg.type);
    return eSIR_FAILURE;
  }
  return eSIR_SUCCESS;
}

/** -------------------------------------------------------------
\fn limValidateDeltsReq
\brief Validates DelTs req originated by SME or by HAL and also sends halMsg_DelTs to HAL
\param   tpAniSirGlobal pMac
\param     tpSirDeltsReq pDeltsReq
\param   tSirMacAddr peerMacAddr
\return eSirRetStatus - status
  -------------------------------------------------------------*/

tSirRetStatus
limValidateDeltsReq(tpAniSirGlobal pMac, tpSirDeltsReq pDeltsReq, tSirMacAddr peerMacAddr,tpPESession psessionEntry)
{
    tpDphHashNode pSta;
    tANI_U8            tsStatus;
    tSirMacTSInfo *tsinfo;
    tANI_U32 i;
    tANI_U8 tspecIdx;
    /* if sta
     *  - verify assoc state
     *  - del tspec locally
     * if ap,
     *  - verify sta is in assoc state
     *  - del sta tspec locally
     */
    if(pDeltsReq == NULL)
    {
      PELOGE(limLog(pMac, LOGE, FL("Delete TS request pointer is NULL"));)
      return eSIR_FAILURE;
    }

    if ((psessionEntry->limSystemRole == eLIM_STA_ROLE)||(psessionEntry->limSystemRole == eLIM_BT_AMP_STA_ROLE))
    {
        tANI_U32 val;

        // station always talks to the AP
        pSta = dphGetHashEntry(pMac, DPH_STA_HASH_INDEX_PEER, &psessionEntry->dph.dphHashTable);

        val = sizeof(tSirMacAddr);
        #if 0
        if (wlan_cfgGetStr(pMac, WNI_CFG_BSSID, peerMacAddr, &val) != eSIR_SUCCESS)
        {
            /// Could not get BSSID from CFG. Log error.
            limLog(pMac, LOGP, FL("could not retrieve BSSID"));
            return eSIR_FAILURE;
        }
       #endif// TO SUPPORT BT-AMP
       sirCopyMacAddr(peerMacAddr,psessionEntry->bssId);
       
    }
    else
    {
        tANI_U16 assocId;
        tANI_U8 *macaddr = (tANI_U8 *) peerMacAddr;

        assocId = pDeltsReq->aid;
        if (assocId != 0)
            pSta = dphGetHashEntry(pMac, assocId, &psessionEntry->dph.dphHashTable);
        else
            pSta = dphLookupHashEntry(pMac, pDeltsReq->macAddr, &assocId, &psessionEntry->dph.dphHashTable);

        if (pSta != NULL)
            // TBD: check sta assoc state as well
            for (i =0; i < sizeof(tSirMacAddr); i++)
                macaddr[i] = pSta->staAddr[i];
    }

    if (pSta == NULL)
    {
        PELOGE(limLog(pMac, LOGE, "Cannot find station context for delts req");)
        return eSIR_FAILURE;
    }

    if ((! pSta->valid) ||
        (pSta->mlmStaContext.mlmState != eLIM_MLM_LINK_ESTABLISHED_STATE))
    {
        PELOGE(limLog(pMac, LOGE, "Invalid Sta (or state) for DelTsReq");)
        return eSIR_FAILURE;
    }

    pDeltsReq->req.wsmTspecPresent = 0;
    pDeltsReq->req.wmeTspecPresent = 0;
    pDeltsReq->req.lleTspecPresent = 0;

    if ((pSta->wsmEnabled) &&
        (pDeltsReq->req.tspec.tsinfo.traffic.accessPolicy != SIR_MAC_ACCESSPOLICY_EDCA))
        pDeltsReq->req.wsmTspecPresent = 1;
    else if (pSta->wmeEnabled)
        pDeltsReq->req.wmeTspecPresent = 1;
    else if (pSta->lleEnabled)
        pDeltsReq->req.lleTspecPresent = 1;
    else
    {
        PELOGW(limLog(pMac, LOGW, FL("DELTS_REQ ignore - qos is disabled"));)
        return eSIR_FAILURE;
    }

    tsinfo = pDeltsReq->req.wmeTspecPresent ? &pDeltsReq->req.tspec.tsinfo
                                            : &pDeltsReq->req.tsinfo;
   limLog(pMac, LOG1,
           FL("received DELTS_REQ message (wmeTspecPresent = %d, lleTspecPresent = %d, wsmTspecPresent = %d, tsid %d,  up %d, direction = %d)"),
           pDeltsReq->req.wmeTspecPresent, pDeltsReq->req.lleTspecPresent, pDeltsReq->req.wsmTspecPresent,
           tsinfo->traffic.tsid, tsinfo->traffic.userPrio, tsinfo->traffic.direction);

       // if no Access Control, ignore the request

    if (limAdmitControlDeleteTS(pMac, pSta->assocId, tsinfo, &tsStatus, &tspecIdx)
        != eSIR_SUCCESS)
    {
       PELOGE(limLog(pMac, LOGE, "ERROR DELTS request for sta assocId %d (tsid %d, up %d)",
               pSta->assocId, tsinfo->traffic.tsid, tsinfo->traffic.userPrio);)
        return eSIR_FAILURE;
    }
    else if ((tsinfo->traffic.accessPolicy == SIR_MAC_ACCESSPOLICY_HCCA) ||
             (tsinfo->traffic.accessPolicy == SIR_MAC_ACCESSPOLICY_BOTH))
    {
      //edca only now.
    }
    else
    {
      if((tsinfo->traffic.accessPolicy == SIR_MAC_ACCESSPOLICY_EDCA) && 
           psessionEntry->gLimEdcaParams[upToAc(tsinfo->traffic.userPrio)].aci.acm)
      {
        //send message to HAL to delete TS
        if(eSIR_SUCCESS != limSendHalMsgDelTs(pMac,
                                              pSta->staIndex,
                                              tspecIdx,
                                              pDeltsReq->req,
                                              psessionEntry->peSessionId,
                                              psessionEntry->bssId))
        {
          limLog(pMac, LOGW, FL("DelTs with UP %d failed in limSendHalMsgDelTs - ignoring request"),
                           tsinfo->traffic.userPrio);
           return eSIR_FAILURE;
        }
      }
    }
    return eSIR_SUCCESS;
}

/** -------------------------------------------------------------
\fn limRegisterHalIndCallBack
\brief registers callback function to HAL for any indication.
\param   tpAniSirGlobal pMac
\return none.
  -------------------------------------------------------------*/
void
limRegisterHalIndCallBack(tpAniSirGlobal pMac)
{
    tSirMsgQ msg;
    tpHalIndCB pHalCB;

    pHalCB = vos_mem_malloc(sizeof(tHalIndCB));
    if ( NULL == pHalCB )
    {
       limLog(pMac, LOGP, FL("AllocateMemory() failed"));
       return;
    }

    pHalCB->pHalIndCB = limProcessHalIndMessages;

    msg.type = WDA_REGISTER_PE_CALLBACK;
    msg.bodyptr = pHalCB;
    msg.bodyval = 0;
    
    MTRACE(macTraceMsgTx(pMac, NO_SESSION, msg.type));
    if(eSIR_SUCCESS != wdaPostCtrlMsg(pMac, &msg))
    {
        vos_mem_free(pHalCB);
        limLog(pMac, LOGP, FL("wdaPostCtrlMsg() failed"));
    }
    
    return;
}


/** -------------------------------------------------------------
\fn limProcessAddBaInd

\brief handles the BA activity check timeout indication coming from HAL.
         Validates the request, posts request for sending addBaReq message for every candidate in the list.
\param   tpAniSirGlobal pMac
\param  tSirMsgQ limMsg
\return None
-------------------------------------------------------------*/
void
limProcessAddBaInd(tpAniSirGlobal pMac, tpSirMsgQ limMsg)
{
    tANI_U8             i;
    tANI_U8             tid;
    tANI_U16            assocId;
    tpDphHashNode       pSta;
    tpAddBaCandidate    pBaCandidate;
    tANI_U32            baCandidateCnt;
    tpBaActivityInd     pBaActivityInd;
    tpPESession         psessionEntry;
    tANI_U8             sessionId;
#ifdef FEATURE_WLAN_TDLS
    boolean             htCapable = FALSE;
#endif
    

    if (limMsg->bodyptr == NULL)
        return;
    
    pBaActivityInd = (tpBaActivityInd)limMsg->bodyptr;
    baCandidateCnt = pBaActivityInd->baCandidateCnt;

    if ((psessionEntry = peFindSessionByBssid(pMac,pBaActivityInd->bssId,&sessionId))== NULL)
    {
        limLog(pMac, LOGE,FL("session does not exist for given BSSId"));
        vos_mem_free(limMsg->bodyptr);
        limMsg->bodyptr = NULL;
        return;
    }
       
    //if we are not HT capable we don't need to handle BA timeout indication from HAL.
#ifdef FEATURE_WLAN_TDLS
    if ((baCandidateCnt  > pMac->lim.maxStation))
#else
    if ((baCandidateCnt  > pMac->lim.maxStation) || !psessionEntry->htCapability )
#endif
    {
        vos_mem_free(limMsg->bodyptr);
        limMsg->bodyptr = NULL;
        return;
    }

#ifdef FEATURE_WLAN_TDLS
    //if we have TDLS peers, we should look at peers HT capability, which can be different than
    //AP capability
    pBaCandidate =  (tpAddBaCandidate) (((tANI_U8*)pBaActivityInd) + sizeof(tBaActivityInd));

    for (i=0; i<baCandidateCnt; i++, pBaCandidate++)
    {
       pSta = dphLookupHashEntry(pMac, pBaCandidate->staAddr, &assocId, &psessionEntry->dph.dphHashTable);
       if ((NULL == pSta) || (!pSta->valid))
           continue;

       if (STA_ENTRY_TDLS_PEER == pSta->staType)
           htCapable = pSta->mlmStaContext.htCapability;
       else
           htCapable = psessionEntry->htCapability;

       if (htCapable)
           break;
    }
    if (!htCapable)
    {
        vos_mem_free(limMsg->bodyptr);
        limMsg->bodyptr = NULL;
        return;
    }
#endif
  
    //delete the complete dialoguetoken linked list
    limDeleteDialogueTokenList(pMac);
    pBaCandidate =  (tpAddBaCandidate) (((tANI_U8*)pBaActivityInd) + sizeof(tBaActivityInd));

    for (i=0; i<baCandidateCnt; i++, pBaCandidate++)
    {
       pSta = dphLookupHashEntry(pMac, pBaCandidate->staAddr, &assocId, &psessionEntry->dph.dphHashTable);
       if ((NULL == pSta) || (!pSta->valid))
           continue;

        for (tid=0; tid<STACFG_MAX_TC; tid++)
        {
            if((eBA_DISABLE == pSta->tcCfg[tid].fUseBATx) &&
                 (pBaCandidate->baInfo[tid].fBaEnable))
            {
                limLog(pMac, LOG1,
                        FL("BA setup for staId = %d, TID: %d, SSN: %d"),
                        pSta->staIndex, tid,
                        pBaCandidate->baInfo[tid].startingSeqNum);
                limPostMlmAddBAReq(pMac, pSta, tid,
                        pBaCandidate->baInfo[tid].startingSeqNum,psessionEntry);
            }
        }
    }
    vos_mem_free(limMsg->bodyptr);
    limMsg->bodyptr = NULL;
    return;
}


/** -------------------------------------------------------------
\fn      limDeleteBASessions
\brief   Deletes all the exisitng BA sessions for given session
         and BA direction.
\param   tpAniSirGlobal pMac
\param   tpPESession pSessionEntry
\param   tANI_U32 baDirection
\return  None
-------------------------------------------------------------*/

void 
limDeleteBASessions(tpAniSirGlobal pMac, tpPESession pSessionEntry,
                    tANI_U32 baDirection, tSirMacReasonCodes baReasonCode)
{
    tANI_U32 i;
    tANI_U8 tid;
    tpDphHashNode pSta;

    if (NULL == pSessionEntry)
    {
        limLog(pMac, LOGE, FL("Session does not exist"));
    }
    else
    {
        for(tid = 0; tid < STACFG_MAX_TC; tid++)
        {
            if ((eLIM_AP_ROLE == pSessionEntry->limSystemRole) ||
                (pSessionEntry->limSystemRole == eLIM_BT_AMP_AP_ROLE) ||
                (eLIM_STA_IN_IBSS_ROLE == pSessionEntry->limSystemRole) ||
                (pSessionEntry->limSystemRole == eLIM_P2P_DEVICE_GO))
            {
                for (i = 0; i < pMac->lim.maxStation; i++)
                {
                    pSta = pSessionEntry->dph.dphHashTable.pDphNodeArray + i;
                    if (pSta && pSta->added)
                    {
                        if ((eBA_ENABLE == pSta->tcCfg[tid].fUseBATx) &&
                                       (baDirection & BA_INITIATOR))
                        {
                            limPostMlmDelBAReq(pMac, pSta, eBA_INITIATOR, tid,
                                               baReasonCode,
                                               pSessionEntry);
                        }
                        if ((eBA_ENABLE == pSta->tcCfg[tid].fUseBARx) &&
                                        (baDirection & BA_RECIPIENT))
                        {
                            limPostMlmDelBAReq(pMac, pSta, eBA_RECIPIENT, tid,
                                               baReasonCode,
                                               pSessionEntry);
                        }
                    }
                }
            }
            else if ((eLIM_STA_ROLE == pSessionEntry->limSystemRole) ||
                     (eLIM_BT_AMP_STA_ROLE == pSessionEntry->limSystemRole) ||
                     (eLIM_P2P_DEVICE_ROLE == pSessionEntry->limSystemRole))
            {
                pSta = dphGetHashEntry(pMac, DPH_STA_HASH_INDEX_PEER,
                                       &pSessionEntry->dph.dphHashTable);
                if (pSta && pSta->added)
                {
                    if ((eBA_ENABLE == pSta->tcCfg[tid].fUseBATx) &&
                                    (baDirection & BA_INITIATOR))
                    {
                        limPostMlmDelBAReq(pMac, pSta, eBA_INITIATOR, tid,
                                           baReasonCode,
                                           pSessionEntry);
                    }
                    if ((eBA_ENABLE == pSta->tcCfg[tid].fUseBARx) &&
                                    (baDirection & BA_RECIPIENT))
                    {
                        limPostMlmDelBAReq(pMac, pSta, eBA_RECIPIENT, tid,
                                           baReasonCode,
                                           pSessionEntry);
                    }
                }
            }
        }
    }
}

/** -------------------------------------------------------------
\fn     limDelAllBASessions
\brief  Deletes all the exisitng BA sessions.
\param  tpAniSirGlobal pMac
\return None
-------------------------------------------------------------*/

void limDelAllBASessions(tpAniSirGlobal pMac)
{
    tANI_U32 i;
    tpPESession pSessionEntry;

    for (i = 0; i < pMac->lim.maxBssId; i++)
    {
        pSessionEntry = peFindSessionBySessionId(pMac, i);
        if (pSessionEntry)
        {
            limDeleteBASessions(pMac, pSessionEntry, BA_BOTH_DIRECTIONS,
                                eSIR_MAC_UNSPEC_FAILURE_REASON);
        }
    }
}

/** -------------------------------------------------------------
\fn     limDelAllBASessionsBtc
\brief  Deletes all the exisitng BA receipent sessions in 2.4GHz
        band.
\param  tpAniSirGlobal pMac
\return None
-------------------------------------------------------------*/

void limDelPerBssBASessionsBtc(tpAniSirGlobal pMac)
{
    tANI_U8 sessionId;
    tpPESession pSessionEntry;
    pSessionEntry = peFindSessionByBssid(pMac,pMac->btc.btcBssfordisableaggr,
                                                                &sessionId);
    if (pSessionEntry)
    {
        PELOGW(limLog(pMac, LOGW,
        "Deleting the BA for session %d as host got BTC event", sessionId);)
        limDeleteBASessions(pMac, pSessionEntry, BA_BOTH_DIRECTIONS,
                            eSIR_MAC_PEER_TIMEDOUT_REASON);
    }
}

/** -------------------------------------------------------------
\fn limProcessDelTsInd
\brief handles the DeleteTS indication coming from HAL or generated by PE itself in some error cases.
         Validates the request, sends the DelTs action frame to the Peer and sends DelTs indicatoin to HDD.
\param   tpAniSirGlobal pMac
\param  tSirMsgQ limMsg
\return None
-------------------------------------------------------------*/
void
limProcessDelTsInd(tpAniSirGlobal pMac, tpSirMsgQ limMsg)
{
  tpDphHashNode         pSta;
  tpDelTsParams         pDelTsParam = (tpDelTsParams) (limMsg->bodyptr);
  tpSirDeltsReq         pDelTsReq = NULL;
  tSirMacAddr           peerMacAddr;
  tpSirDeltsReqInfo     pDelTsReqInfo;
  tpLimTspecInfo        pTspecInfo;
  tpPESession           psessionEntry;
  tANI_U8               sessionId;  

if((psessionEntry = peFindSessionByBssid(pMac,pDelTsParam->bssId,&sessionId))== NULL)
    {
         limLog(pMac, LOGE,FL("session does not exist for given BssId"));
         vos_mem_free(limMsg->bodyptr);
         limMsg->bodyptr = NULL;
         return;
    }

  pTspecInfo = &(pMac->lim.tspecInfo[pDelTsParam->tspecIdx]);
  if(pTspecInfo->inuse == false)
  {
    PELOGE(limLog(pMac, LOGE, FL("tspec entry with index %d is not in use"), pDelTsParam->tspecIdx);)
    goto error1;
  }

  pSta = dphGetHashEntry(pMac, pTspecInfo->assocId, &psessionEntry->dph.dphHashTable);
  if(pSta == NULL)
  {
    limLog(pMac, LOGE, FL("Could not find entry in DPH table for assocId = %d"),
                pTspecInfo->assocId);
    goto error1;
  }

  pDelTsReq = vos_mem_malloc(sizeof(tSirDeltsReq));
  if ( NULL == pDelTsReq )
  {
     PELOGE(limLog(pMac, LOGE, FL("AllocateMemory() failed"));)
     goto error1;
  }

  vos_mem_set( (tANI_U8 *)pDelTsReq, sizeof(tSirDeltsReq), 0);

  if(pSta->wmeEnabled)
    vos_mem_copy( &(pDelTsReq->req.tspec), &(pTspecInfo->tspec), sizeof(tSirMacTspecIE));
  else
    vos_mem_copy( &(pDelTsReq->req.tsinfo), &(pTspecInfo->tspec.tsinfo), sizeof(tSirMacTSInfo));


  //validate the req
  if (eSIR_SUCCESS != limValidateDeltsReq(pMac, pDelTsReq, peerMacAddr,psessionEntry))
  {
    PELOGE(limLog(pMac, LOGE, FL("limValidateDeltsReq failed"));)
    goto error2;
  }
  limLog(pMac, LOG1, "Sent DELTS request to station with "
                "assocId = %d MacAddr = "MAC_ADDRESS_STR,
                pDelTsReq->aid, MAC_ADDR_ARRAY(peerMacAddr));

  limSendDeltsReqActionFrame(pMac, peerMacAddr, pDelTsReq->req.wmeTspecPresent, &pDelTsReq->req.tsinfo, &pDelTsReq->req.tspec,
          psessionEntry);

  // prepare and send an sme indication to HDD
  pDelTsReqInfo = vos_mem_malloc(sizeof(tSirDeltsReqInfo));
  if ( NULL == pDelTsReqInfo )
  {
     PELOGE(limLog(pMac, LOGE, FL("AllocateMemory() failed"));)
     goto error3;
  }
  vos_mem_set( (tANI_U8 *)pDelTsReqInfo, sizeof(tSirDeltsReqInfo), 0);

  if(pSta->wmeEnabled)
    vos_mem_copy( &(pDelTsReqInfo->tspec), &(pTspecInfo->tspec), sizeof(tSirMacTspecIE));
  else
    vos_mem_copy( &(pDelTsReqInfo->tsinfo), &(pTspecInfo->tspec.tsinfo), sizeof(tSirMacTSInfo));

  limSendSmeDeltsInd(pMac, pDelTsReqInfo, pDelTsReq->aid,psessionEntry);

error3:
  vos_mem_free(pDelTsReqInfo);
error2:
  vos_mem_free(pDelTsReq);
error1:
  vos_mem_free(limMsg->bodyptr);
  limMsg->bodyptr = NULL;
  return;
}

/**
 * \brief Setup an A-MPDU/BA session
 *
 * \sa limPostMlmAddBAReq
 *
 * \param pMac The global tpAniSirGlobal object
 *
 * \param pStaDs DPH Hash Node object of peer STA
 *
 * \param tid TID for which a BA is being setup.
 *            If this is set to 0xFFFF, then we retrieve
 *            the default TID from the CFG
 *
 * \return eSIR_SUCCESS if setup completes successfully
 *         eSIR_FAILURE is some problem is encountered
 */
tSirRetStatus limPostMlmAddBAReq( tpAniSirGlobal pMac,
    tpDphHashNode pStaDs,
    tANI_U8 tid, tANI_U16 startingSeqNum,tpPESession psessionEntry)
{
    tSirRetStatus status = eSIR_SUCCESS;
    tpLimMlmAddBAReq pMlmAddBAReq = NULL;
    tpDialogueToken dialogueTokenNode;
    tANI_U32        val = 0;

  // Check if the peer is a 11n capable STA
  // FIXME - Need a 11n peer indication in DPH.
  // For now, using the taurusPeer attribute
  //if( 0 == pStaDs->taurusPeer == )
    //return eSIR_SUCCESS;

  // Allocate for LIM_MLM_ADDBA_REQ
  pMlmAddBAReq = vos_mem_malloc(sizeof( tLimMlmAddBAReq ));
  if ( NULL == pMlmAddBAReq )
  {
    limLog( pMac, LOGP, FL("AllocateMemory failed"));
    status = eSIR_MEM_ALLOC_FAILED;
    goto returnFailure;
  }

  vos_mem_set( (void *) pMlmAddBAReq, sizeof( tLimMlmAddBAReq ), 0);

  // Copy the peer MAC
  vos_mem_copy(
      pMlmAddBAReq->peerMacAddr,
      pStaDs->staAddr,
      sizeof( tSirMacAddr ));

  // Update the TID
  pMlmAddBAReq->baTID = tid;

  // Determine the supported BA policy of local STA
  // for the TID of interest
  pMlmAddBAReq->baPolicy = (pStaDs->baPolicyFlag >> tid) & 0x1;

  // BA Buffer Size
  // Requesting the ADDBA recipient to populate the size.
  // If ADDBA is accepted, a non-zero buffer size should
  // be returned in the ADDBA Rsp
  if ((TRUE == psessionEntry->isCiscoVendorAP) &&
        (eHT_CHANNEL_WIDTH_80MHZ != pStaDs->htSupportedChannelWidthSet))
  {
      /* Cisco AP has issues in receiving more than 25 "mpdu in ampdu"
          causing very low throughput in HT40 case */
      limLog( pMac, LOGW,
          FL( "Requesting ADDBA with Cisco 1225 AP, window size 25"));
      pMlmAddBAReq->baBufferSize = MAX_BA_WINDOW_SIZE_FOR_CISCO;
  }
  else if (pMac->miracastVendorConfig)
  {
      if (wlan_cfgGetInt(pMac, WNI_CFG_NUM_BUFF_ADVERT , &val) != eSIR_SUCCESS)
      {
           limLog(pMac, LOGE, FL("Unable to get WNI_CFG_NUM_BUFF_ADVERT"));
           status = eSIR_FAILURE;
           goto returnFailure;
      }

      pMlmAddBAReq->baBufferSize = val;
  }
  else
      pMlmAddBAReq->baBufferSize = 0;

  limLog( pMac, LOGW,
      FL( "Requesting an ADDBA to setup a %s BA session with STA %d for TID %d buff = %d" ),
      (pMlmAddBAReq->baPolicy ? "Immediate": "Delayed"),
      pStaDs->staIndex,
      tid, pMlmAddBAReq->baBufferSize );

  // BA Timeout
  if (wlan_cfgGetInt(pMac, WNI_CFG_BA_TIMEOUT, &val) != eSIR_SUCCESS)
  {
     limLog(pMac, LOGE, FL("could not retrieve BA TIME OUT Param CFG"));
     status = eSIR_FAILURE;
     goto returnFailure;
  }
  pMlmAddBAReq->baTimeout = val; // In TU's

  // ADDBA Failure Timeout
  // FIXME_AMPDU - Need to retrieve this from CFG.
  //right now we are not checking for response timeout. so this field is dummy just to be compliant with the spec.
  pMlmAddBAReq->addBAFailureTimeout = 2000; // In TU's

  // BA Starting Sequence Number
  pMlmAddBAReq->baSSN = startingSeqNum;

  /* Update PE session Id*/
  pMlmAddBAReq->sessionId = psessionEntry->peSessionId;

  LIM_SET_STA_BA_STATE(pStaDs, tid, eLIM_BA_STATE_WT_ADD_RSP);

  dialogueTokenNode = limAssignDialogueToken(pMac);
  if (NULL == dialogueTokenNode)
  {
     limLog(pMac, LOGE, FL("could not assign dialogue token"));
     status = eSIR_FAILURE;
     goto returnFailure;
  }

  pMlmAddBAReq->baDialogToken = dialogueTokenNode->token;
  //set assocId and tid information in the lim linked list
  dialogueTokenNode->assocId = pStaDs->assocId;
  dialogueTokenNode->tid = tid;
  // Send ADDBA Req to MLME
  limPostMlmMessage( pMac,
      LIM_MLM_ADDBA_REQ,
      (tANI_U32 *) pMlmAddBAReq );
  return eSIR_SUCCESS;

returnFailure:
  vos_mem_free(pMlmAddBAReq);
  return status;
}

/**
 * \brief Post LIM_MLM_ADDBA_RSP to MLME. MLME
 * will then send an ADDBA Rsp to peer MAC entity
 * with the appropriate ADDBA status code
 *
 * \sa limPostMlmAddBARsp
 *
 * \param pMac The global tpAniSirGlobal object
 *
 * \param peerMacAddr MAC address of peer entity that will
 * be the recipient of this ADDBA Rsp
 *
 * \param baStatusCode ADDBA Rsp status code
 *
 * \param baDialogToken ADDBA Rsp dialog token
 *
 * \param baTID TID of interest
 *
 * \param baPolicy The BA policy
 *
 * \param baBufferSize The BA buffer size
 *
 * \param baTimeout BA timeout in TU's
 *
 * \return eSIR_SUCCESS if setup completes successfully
 *         eSIR_FAILURE is some problem is encountered
 */
tSirRetStatus limPostMlmAddBARsp( tpAniSirGlobal pMac,
    tSirMacAddr peerMacAddr,
    tSirMacStatusCodes baStatusCode,
    tANI_U8 baDialogToken,
    tANI_U8 baTID,
    tANI_U8 baPolicy,
    tANI_U16 baBufferSize,
    tANI_U16 baTimeout,
    tpPESession psessionEntry)
{
tSirRetStatus status = eSIR_SUCCESS;
tpLimMlmAddBARsp pMlmAddBARsp;

  // Allocate for LIM_MLM_ADDBA_RSP
  pMlmAddBARsp = vos_mem_malloc(sizeof( tLimMlmAddBARsp ));
  if ( NULL == pMlmAddBARsp )
  {
    limLog( pMac, LOGE,
        FL("AllocateMemory failed with error code %d"),
        status );

    status = eSIR_MEM_ALLOC_FAILED;
    goto returnFailure;
  }

  vos_mem_set( (void *) pMlmAddBARsp, sizeof( tLimMlmAddBARsp ), 0);

  // Copy the peer MAC
  vos_mem_copy(
      pMlmAddBARsp->peerMacAddr,
      peerMacAddr,
      sizeof( tSirMacAddr ));

  pMlmAddBARsp->baDialogToken = baDialogToken;
  pMlmAddBARsp->addBAResultCode = baStatusCode;
  pMlmAddBARsp->baTID = baTID;
  pMlmAddBARsp->baPolicy = baPolicy;
  pMlmAddBARsp->baBufferSize = baBufferSize;
  pMlmAddBARsp->baTimeout = baTimeout;

  /* UPdate PE session ID*/
  pMlmAddBARsp->sessionId = psessionEntry->peSessionId;

  // Send ADDBA Rsp to MLME
  limPostMlmMessage( pMac,
      LIM_MLM_ADDBA_RSP,
      (tANI_U32 *) pMlmAddBARsp );

returnFailure:

  return status;
}

/**
 * \brief Post LIM_MLM_DELBA_REQ to MLME. MLME
 * will then send an DELBA Ind to peer MAC entity
 * with the appropriate DELBA status code
 *
 * \sa limPostMlmDelBAReq
 *
 * \param pMac The global tpAniSirGlobal object
 *
 * \param pSta DPH Hash Node object of peer MAC entity
 * for which the BA session is being deleted
 *
 * \param baDirection DELBA direction
 *
 * \param baTID TID for which the BA session is being deleted
 *
 * \param baReasonCode DELBA Req reason code
 *
 * \return eSIR_SUCCESS if setup completes successfully
 *         eSIR_FAILURE is some problem is encountered
 */
tSirRetStatus limPostMlmDelBAReq( tpAniSirGlobal pMac,
    tpDphHashNode pSta,
    tANI_U8 baDirection,
    tANI_U8 baTID,
    tSirMacReasonCodes baReasonCode,
    tpPESession psessionEntry)
{
tSirRetStatus status = eSIR_SUCCESS;
tpLimMlmDelBAReq pMlmDelBAReq;
tLimBAState curBaState;

if(NULL == pSta)
    return eSIR_FAILURE;

LIM_GET_STA_BA_STATE(pSta, baTID, &curBaState);

  // Need to validate the current BA State.
  if( eLIM_BA_STATE_IDLE != curBaState)
  {
    limLog( pMac, LOGE,
        FL( "Received unexpected DELBA REQ when STA BA state for tid = %d is %d" ),
        baTID,
        curBaState);

    status = eSIR_FAILURE;
    goto returnFailure;
  }

  // Allocate for LIM_MLM_DELBA_REQ
  pMlmDelBAReq = vos_mem_malloc(sizeof( tLimMlmDelBAReq ));
  if ( NULL == pMlmDelBAReq )
  {
    limLog( pMac, LOGE,
        FL("AllocateMemory failed with error code %d"),
        status );

    status = eSIR_MEM_ALLOC_FAILED;
    goto returnFailure;
  }

  vos_mem_set( (void *) pMlmDelBAReq, sizeof( tLimMlmDelBAReq ), 0);

  // Copy the peer MAC
  vos_mem_copy(
      pMlmDelBAReq->peerMacAddr,
      pSta->staAddr,
      sizeof( tSirMacAddr ));

  pMlmDelBAReq->baDirection = baDirection;
  pMlmDelBAReq->baTID = baTID;
  pMlmDelBAReq->delBAReasonCode = baReasonCode;

  /* Update PE session ID*/
  pMlmDelBAReq->sessionId = psessionEntry->peSessionId;

  //we don't have valid BA session for the given direction. 
  // HDD wants to get the BA session deleted on PEER in this case. 
  // in this case we just need to send DelBA to the peer.
  if(((eBA_RECIPIENT == baDirection) && (eBA_DISABLE == pSta->tcCfg[baTID].fUseBARx)) ||
      ((eBA_INITIATOR == baDirection) && (eBA_DISABLE == pSta->tcCfg[baTID].fUseBATx)))
  {
        // Send DELBA Ind over the air
        if( eSIR_SUCCESS !=
            (status = limSendDelBAInd( pMac, pMlmDelBAReq,psessionEntry)))
          status = eSIR_FAILURE;
  
        vos_mem_free(pMlmDelBAReq);
        return status;
  }


  // Update the BA state in STA
  LIM_SET_STA_BA_STATE(pSta, pMlmDelBAReq->baTID, eLIM_BA_STATE_WT_DEL_RSP);

  // Send DELBA Req to MLME
  limPostMlmMessage( pMac,
      LIM_MLM_DELBA_REQ,
      (tANI_U32 *) pMlmDelBAReq );

returnFailure:

  return status;
}

/**
 * \brief Send WDA_ADDBA_REQ to HAL, in order
 * to setup a new BA session with a peer
 *
 * \sa limPostMsgAddBAReq
 *
 * \param pMac The global tpAniSirGlobal object
 *
 * \param pSta Runtime, STA-related configurReq to HSirh
 */
n the lishNode pSject
 *
 * \param pSDialogToken = e gltionFr ame(pialog token
 *
 * \param baTID TID ofr which the BA session is being detup a*
 * \param baPolicy Th sePicy
 *
 * \param baBufferSize The BAquest t BA poffer size
 *
 * \param baTimeout BA tiTeout. so0ndicatio not A timeout ineormac
 *
 * \param baReN = arting Sequence Number
 or the iBA session w*
 * \param baDirection DE serection D: 1 -nd itte or0); -nRipient o*
 * \return eSne.
*
 */

virRetStatus limPostMlmAddBAReq
 tpAniSirGlobal pMac,
    tpDphHashNode pSta,
    tANI_U8 baDireogToken,
    tANI_U8 baTID,
    tANI_U8 baPolicy,
    tANI_U16 baBufferSize,
    tANI_U16 baTimeout,
    tpPI_U16 baTiN =    tANI_U8 baDirection,
     tpPESession psessionEntry)
{
tSiddBaCArams   ddBaCArams   NULL;
  irRetStatus liturde = baIR_SUCCESS;
tpLrMsgQ msg;
Q;#ifdef FEAN_TDSOFTAP_VA_BAATURE_W // Se jun bely nod A ti ps"har ),A_Bs if ( N!(IS_HWA_IN_DX(ta->staIndex, )
  {
      turde = baIL, TATE_USAILURE;
    gomLog( pMac, LOGE,
        FL( "Rea RuIis not inHW a RuIi,eturn stde %d %d"  ),
  turde =)    goto returnFailure;
  }

 ndif
 / SAN_TDSOFTAP_VA_BAATURE_W  // Allocate for LIA_ADDBA_REQ t pMldBaCArams   NUs_mem_malloc(sizeof( tLidBaCArams   ;
  if ( NULL == pMldBaCArams   ; {
    limLog( pMac, LOGE,
        FL("AllocateMemory failed w"          s

    stturde = baIR_SUM_ALLOC_FAILED;
    goto returnFailure;
  }

  vos_mem_set( (void *) pMldBaCArams  sizeof( tLidBaCArams   ;0);

  // Copy the peer MAC
 ddress o vos_mem_copy(
      pMoid *) pMldBaCArams  peerMacAddr,
      pSoid *) pMla->staAddr,
      sizeof( tSirMacAddr ));

  pM CoPulate the siQ torameters.
 pMldBaCArams  staInddx pSta->staAddex, ; pMldBaCArams  stDialogToken = baDialogToken;
  pMldBaCArams  stDiD = baTID;
  pMldBaCArams  stDilicy = baPolicy;
  pMldBaCArams  stDifferSize = baBufferSize;
  pMldBaCArams  stDiDeout = baTimeout;

 pMldBaCArams  stDiN = stDiN =; pMldBaCArams  stDialction = baDirection;
  pMldBaCArams  stsponq
 d 1;
   /* UPdate PE session ID*//
    ldBaCArams  stasionId = psessionEntry->peSessionId;

  // Sest LIA_ADDBA_REQ to HAL,   ifg;
Qype = WDA_REDBA_REQ t  // S
// FIXME_AMPDU - // Alllobal tpuntered (alog token
 )d %dquesectto sekeep actik of // All th se<->AL ormmenunation i(s) // S
//g;
Qysentrd un0;
    g;
Qydyptr = pHadBaCArams      g;
Qydyptl = 0;

  //mLog( pMac, LOGW,
      FL( "ReSding adA_REDBA_REQ t...ufferize 25%d is,taId = %d,  ,imeout in%d,            ca"Ti= %d, TIDection = %d)" ,ilicy = ba, TIasionId = ps)" ,iDiN = st"  ),
            (ldBaCArams  stDifferSize =,MldBaCArams  staInddx            (ldBaCArams  stDimeout,
 MldBaCArams  stDiD =            (ldBaCArams  stDirection,
  ldBaCArams  stDilicy =            (ldBaCArams  stasionId =  ldBaCArams  stDiN =

  pM Cf Feany inoe sressage fotimele jut thsponse tick f  ifT_STM_P2PROSS;
ELBFDUM_SGpMac, pSlse)
 
  pMRACE(macTraceMsgTx(pMac, NOessionEntry->peSessionId,
 fg;
Qype =;

 fdef FEATURE_WLAN_TDDIAG_PPORT BTM_PM CATURE_WLAN_TDDIAG_PPORT B    limLoalogEnt",RertedMac, WNIN_TDPEDDIAG_L, TDBA_REQ t_EVT, tiessionEntry->0);0);

 ndif
 / SATURE_WLAN_TDDIAG_PPORT B 
    ( eSIR_SUCCESS !=
  (turde = baaPostCtrlMsg()  ac, &msg))Q )
      mLog( pMac, LOGE,
        FL("AlstCtg adA_REDBA_REQ to HAL forled w! asonCo %d"),
          turde = 
  else
    voturn stturde =
returnFailure:

  re Copan  -up...   ( eSLL ==!pMldBaCArams   ; {
vos_mem_free(pMMldBaCArams   ;;  return stturde =
re
/**
 * \brief Send WDA_ADLBA_REINDo HAL, in order
 * to selete th beist fi BA sessions.ith a er
 *
 * \sa limPostMsgAdlBAInd( *
 * \param pMac The global tpAniSirGlobal object
 *
 * \param pSta Runtime, STA-related configurReq to HSirh
 */
n the lishNode pSject
 *
 * \param pSDiD TID ofr which the BA session is being detup a*
 * \param baPoDection = Idtityfi nowhee sree DelBA Ind ovwa *
  nt = byhe BA sei itte or  resppient o*
 * \return eSne.
*
 */

virRetStatus limPostMlmAdlBAInd( pMAniSirGlobal pMac,
    tpDphHashNode pSta,
    tANI_U8 baDiD,
    tANI_U8 baPorection,
    tANESession psessionEntry)
{
tSidlBAInrams   dlBAInrams   NULL;
  irRetStatus liturde = baIR_SUCCESS;
tpLrMsgQ msg;
Q;#i// Allocate for LIR_SUL, TLBA_REIND
  dlBAInrams   NUs_mem_malloc(sizeof( tLilBAInrams   ;
  if ( NULL == pDelTsCArams   ; {
    limLog( pMac, LOGE,
        FL("AllocateMemory failed w"          

    stturde = baIR_SUM_ALLOC_FAILED;
    goto returnFailure;
  }

  vos_mem_set( (void *) pMllTsCArams  sizeof( tLilTsCArams   ;0);

  // CoPulate the siQ torameters.
 pMllTsCArams  staInddx pSta->staAddex, ; pMllTsCArams  stDiD = baTID;
  pMllTsCArams  stDialction = baDirection;
   /* Update PE session ID*//
      // CTBD-RAJESHpdate g deothe pession ID*// %dquesecdor LIR_SUL, TLBA_REIND????? // CllTsCArams  stasionId = psessionEntry->peSessionId;

  // Sest LIA_ADLBA_REINDo HAL,   ifg;
Qype = WDA_RELBA_REIND  // S
// FIXME_A: // Alllobal tpuntered (alog token
 )d %dquesectto sekeep actik of // All th se<->AL ormmenunation i(s) // S
//g;
Qysentrd un0;
    g;
Qydyptr = pHalTsCArams      g;
Qydyptl = 0;

  //mLog( pMac, LOGW,
      FL( "ReSding adR_SUL, TLBA_REIND...");

  pMRACE(macTraceMsgTx(pMac, NOessionEntry->peSessionId,
 fg;
Qype =;

 fdef FEATURE_WLAN_TDDIAG_PPORT BTM_PM CATURE_WLAN_TDDIAG_PPORT B    limLoalogEnt",RertedMac, WNIN_TDPEDDIAG_L, TLBA_REIND_EVT, tiessionEntry->0);0);

 ndif
 / SATURE_WLAN_TDDIAG_PPORT B    ( eSIR_SUCCESS !=
  (turde = baaPostCtrlMsg()  ac, &msg))Q )
      mLog( pMac, LOGE,
        FL("AlstCtg adA_RELBA_REINDo HAL, orled w! asonCo %d"),
          turde = 
  else
        li Update thM_P'snteresn tpuirh
...     ( eSI_INITIATOR == baDirection) &             ta->tcCfg[baTID].fUseBATx)) 0;
    
   ta->tcCfg[baTID].fUtxffeze = 0;

 
       else
    {
        ta->tcCfg[baTID].fUseBARx)) 0;
    
   ta->tcCfg[baTID].fUrxffeze = 0;

 
          voturn stturde =
r}

  turnFailure:

  re Copan  -up...   ( eSLL ==!pMllTsCArams   ; {
vos_mem_free(pMMllTsCArams   ;;  return stturde =
re
/**
 * \b@nction to:limPostMlSMate);date t() *
 * \r@ief  De:liTs fiection todate tthe DeL, od tiSoftcTr abt ine curhangin the liA-r'snSMPState i.*
 * \rrrrrrGE,IC:*
 * \rrrrrrASSUMPONS,
:*            NA*
 * \rrrrrrNOTE:*            NA*
 * \r@ram  tSac Th-estter is HAobal pMC
 dstrucrn e* \r@ram  tSmMsg->h-eLim ssage( dstrucrn ebject ofth the apMimoPSram  t thdypt* \r@turn None
--/

virRetStatus li
mPostMlSMate);date t(AniSirGlobal pMac, tp         NI_U16 startddx SirMacAdHTMIMOPowSizaveate custe)
      tSirRetStatus stttttttttttttturde = baIR_SUCCESS;
tp  tSirRegQ mssssssssssssssssssssg;
Q;#  tSipSetMIMOPSsssssssssssspMIMO_PSrams    
ssssg;
Qysentrd un0;
    ifg;
Qype = WDA_RET_STMIMOPSEQ t     li Uplocate for LIA_ADT_STMIMOPSEQ t
sssspMIMO_PSrams   vos_mem_malloc(sizeof(tSiretMIMOPS;
    if ( NULL == pHaMIMO_PSrams       {
        limLog(pMMac, LOGP, F("Al locateMemory failed"));
    st  voturn stIR_SUM_ALLOC_FAILED;
    go
    pHaMIMO_PSrams  htSuMIMOPSate cu=uste)
;   pHaMIMO_PSrams  htaInddx pSaInddx;   pHaMIMO_PSrams  htfnd Dp = votrue    ifg;
Qydyptr = pHaMIMO_PSrams      ifg;
Qydyptl = 0;

  //limLog(pMMac, LOGP,2,L( "ReSding adA_RET_STMIMOPSEQ t...");

  pMMTRACE(macTraceMsgTx(pMac, NO_SESSION, msg.tQype));
    ifturde = baaPostCtrlMsg()  ac, &msg))Q )    if ( NIR_SUCCESS !=
  turde =)   {
        limLog(pMMac, LOGP, FL("AlstCtg adA_RET_STMIMOPSEQ to HAL, orled w! asonCo %d"),
  turde = 
  ellllllls_mem_free(pMlmIMO_PSrams  
    st  voturn stturde =
r}
}
    
    return;
tturde =
r}void limDePktFe(p (#  tSipiSirGlobal pMMac,
    tpeame(ppe)
st  vofrmpe)
    tANI_U8 baaaaaaaaa*pRxPtikInt(fo, {
vos_idssssssssssss*pBypt      tSoid *)Mac,
;Soid *)Mfrmpe)
;Soid *)MpRxPtikInt(fo;Soid *)MpBypt}

/**
 * \bmDeGetBDom CRxPtikIn()*
 * \FUNIONS,:*   Ts fiection toicallbatto set thptter is HAPolari *
  ffer SiDescript codentai i adC
 dhead Si&noe srectrolDe*/
n tforom the CFdypteothe pessage fosts tto seM_P.*
 * \GE,IC:*
  NA*
 * \ASSUMPONS,
:*   NA*
 * \NOTE:*   NA*
 * \r@ram  tSdypteeee-nRipved unssage fodypt* \r@ram  tSaRxPtikInt(fo eeee-nstter is HAceived DEBD* \r@turn None
--/

void
limPrGetBDom CRxPtikIn(AniSirGlobal pMac, tpid *) dypt,ANI_U32   **pRxPtikInt(fo      tS*pRxPtikInt(fo (tpDI_U32 *) pMbypt}

* U** d WDmDeGetBDom CRxPtikIn() **

voooooid limDeRsagStacanannelWit(fopAniSirGlobal pMac)
{
    tAs_mem_set( (&ac->lim.tsscanannfo, sizeof(tSiLimScanannfo, ;0);

 }oooid limDeAddacanannelWit(fopAniSirGlobal pMac)
,ANI_U8 bacnnelWitd{
    tANI_U8 se
    tANI_U8BOOLEAN fFnteun0;eI_U8BOOLEAN_LSE;
#e   tAr(ti= 0; i < pMac->lim.mascanannfo, .numannfo, i++)
    {
        pS(pStc->lim.mascanannfo, .scanann[i].cnnelWitd= pHcnnelWitd{
      pS           liMac->lim.mascanannfo, .scanann[i].nummeouacan++           go fFnteun0;eI_U8BOOLEAN_UE =           go eak;
    }
}
}
    
     if ((eI_U8BOOLEAN_LSE;
= falFnteu    {
        pS(pStc->lim.mascanannfo, .numannfo,  STA_MAC_X_PPORT BEDHANNEL_WILIST{
      pS           liMac->lim.mascanannfo, .scanann[tc->lim.mascanannfo, .numannfo, ].cnnelWitd= HcnnelWitd;          liMac->lim.mascanannfo, .scanann[tc->lim.mascanannfo, .numannfo, ++].nummeouacan 1;
    }
}
}
    
     se
                     liMLOGW(limLog(pMac, LOGW,
 L("Al -- nber
 oothcnnelWis excd tocTr);)
     go }
    }
}

/***
 * \b@nction to:limPoIsannelWilidatForannelWiSth ch() *
 * \r@ief  De:liTs fiection toeckinsf the pecnnelWis HAich thAP*             s encectedi aduto gesth ch,s a 11lid BAcnnelWisr LIus *        GE,IC:*
 * \rrrrrrASSUMPONS,
:*            NA*
 * \rrrrrrNOTE:*            NA*
 * \r@ram  tSac Th-estter is HAobal pMC
 dstrucrn e* \r@ram  tScnnelWisNeedwecnnelWis HAich th are nopected DE HAmo
 *   @turn None
--/

viiSiBooli
mPoIsannelWilidatForannelWiSth ch(AniSirGlobal pMac)
,ANI_U8 bacnnelWi{
    tANI_U8    iex, ; pMtANI_U32      lid BannelWist(pL = baI_CFG_BAVALIDHANNEL_WILIST_LENtp  tSirRegacanneNum   lid BannelWist(p[I_CFG_BAVALIDHANNEL_WILIST_LEN]
    if (NUan_cfgGetInStrMac, WNI_CFG_BAVALIDHANNEL_WILIST            ANI_U8 *)pDlid BannelWist(p            ANI_U8  *) p&lid BannelWist(pL =!= eSIR_SUCCESS)
                LOGE(limLog(pMac, LOGE, FL("Aluld not retrieve BAlid BAcnnelWislt"));
         return;
 NIR_SULSE;
)    go
    pHr(ti=ex %d0; i <ex %d< lid BannelWist(pL =i <ex %)
    {
        pS(pSlid BannelWist(p[<ex %]= pHcnnelWi              turn;
 NIR_SUUE =)    go
    pH/*AcnnelWises not exbelo theoslt")oothlid BAcnnelWis/
      turn;
 NIR_SULSE;
)  
/**
 ----------------------------------------------------
\fn liiiii__mLoFillTxCtrolDerams  brief  DeFl then essage for evstoppi a/senung frtx.
param  tSac
\param  tSpTxClMsg()h-estter is HAtxectrolDeessage f\param  tpAe)
s- Wch th ayh arnts o statop/ senunertx.
aram  tpme = - Tstatop/senune * -----------------------------------------------------*/
voationc eHalatus s
__mLoFillTxCtrolDerams  pAniSirGlobal pMac, tpSiTxCtrolDerams  tSpTxClMsg()                                          iLimQuietTxMe = Ae)
, iLimCtrolDeTxpme ={
     li UTBD-RAJESHpHOW TO GET ssionEntry->????? //tANESession psessionEntry)
 &(ptc->lim.magessionEn[0];   
    re (NUme =  eLIM_P2STOP_TX          pTxClMsg()htaIop)) 0;;eI_U8BOOLEAN_UE =      se
          pTxClMsg()htaIop)) 0;;eI_U8BOOLEAN_LSE;
#e  
    resth ch ANe)
              pSse weIM_P2TXLLOC:             * --SIops/senunes actnsmiion psmpletes ly/
      li      pTxClMsg()htfClMsobal pM1;
    }
}
}




eak;
         pSse weIM_P2TXLS_ROBUT_BEACS,:*            * --SIops/senunes actnsmiion ps psaoramoncularSSId.-SIoppi BA SS,ses nnt               * tatopxbeac to ctnsmiion p.               *      li      pTxClMsg()htclMsBssM1;
    }
}
}




pTxClMsg()htbssBitmap



|(tp1 <<OessionEntry->pesId,&x)    }
}
}




eak;
         pSse weIM_P2TXLSTA:*            * --mory fai staIdon tobitmap
iall tateMed dynamillbain DPllbatrf this A               * taselede %doprierain srand sefl then ebitmap.onewot in letemend, a               * tfl the ughpu.               *      li  se weIM_P2TXLS_R:*            */Fl the uu...         fault T:*            LOGW(limLog(pMac, LOGW,
 L("AlInlid BAce w:onet Hdles d);
         reeeeeturn stIL, TATE_USAILURE;
    go}    stturn stIL, TATE_USACCESS;
tp
/**
 * \b@nction to:limPoame(ppctnsmiion pCtrolDe() *
 * \r@ief  De:liTs fiAPIoicallbattobyhe BAus is HAhalt/senuneny iname t*         actnsmiion psom the CFdevice.f ADstopp, a n thame tsill thb *            thqueuttoartingSesom thhar we n.he gnick f-pss on e* \rrrrrrrrrrrr beiuil o l then edriv.
  i       GE,IC:*
 * \rrrrrrASSUMPONS,
:*            NA*
 * \rrrrrrNOTE:*            NA*
 * \r@ram  tSac Th-estter is HAobal pMC
 dstrucrn e* \r@turn None
--/

vid limDeame(ppctnsmiion pCtrolDe(AniSirGlobal pMac, tpSLimQuietTxMe = Ae)
, iLimCtrolDeTxpme ={
     lieHalatus status = eSIL, TATE_USAILURE;
    goSiTxCtrolDerams  tpTxClMsg()tp  tSirRegQ mssssssssssg;
Q;#  tSiI_U8             nBy tth0; i / Neeof thby tthquesecttoi staIdon tobitmap.    pH/**plocate foly noquesecttonber
 oothby tthi staIdon tobitmap      --make i o stid gto HD4hby t bnteua fa *      nBy tth0;ANI_U8 *)L, MSNUM_BBYTESTATE_NS,_BITMAPStc->lim.maxStation) |       pTxClMsg()hvos_mem_malloc(sizeof(tS*pTxClMsg()+ sinBy tt
    if ( NULL == pHaTxClMsg()h    {
        limLog(pMac, LOGE, FL("AllocateMemory f failed"));)
       return;
    go}    sts_mem_set( (oid *) pMlTxClMsg()                 izeof(tS*pTxClMsg()+ sinBy tt
0);

     atus = eS__mLoFillTxCtrolDerams  pac, pTspxClMsg()  Ae)
, me =)    go ( Natus = !=tIL, TATE_USACCESS;
    {
        vos_mem_free(lipTxClMsg()+;       limLog(pMac, LOGE, FL("Al__mLoFillTxCtrolDerams  ailed"), atus = eS"),
  atus =)
       return;
    go}   go
ssssg;
Qydyptr = pHoid *) pMlTxClMsg()    ifg;
Qydyptl = 0;

   ifg;
Qysentrd un0;
    ifg;
Qype = WDA_RETRANSMIION, _CS,TROLEIND     ifRACE(macTraceMsgTx(pMac, NO_SESSION, msg.tQype));
    if(pSaPostCtrlMsg()  ac, &msg))Q!= eSIR_SUCCESS)
                s_mem_free(lipTxClMsg()+;       limLog(pMMac, LOGP, FL("AlstCtg adssage( d HAL, orled w);)
       return;
    go}    st (NUme =  eLIM_P2STOP_TX                     liMmLog(pMac, LOG1, "S("SesIoppi BAe tauctnsmiion ps f n thptikIns,ndicatio toaoftcTr);)
       re    else
    {
               liMmLog(pMac, LOG1, "S("SeRenung frt tauctnsmiion ps f n thptikIns,ndicatio toaoftcTr);)
       re    elturn;
}


/** -* \b@nction to:limPoRentorePreannelWiSth chate c() *
 * \r@ief  De:liTs fiAPIoicallbattobyhe BAus is HAteuony in* \rrrrrrrrrrrrec.
ifnc rhangison'tfolyhe CFdevice durg f* \rrrrrrrrrrrrcnnelWissth ch  i       GE,IC:*
 * \rrrrrrASSUMPONS,
:*            NA*
 * \rrrrrrNOTE:*            NA*
 * \r@ram  tSac Th-estter is HAobal pMC
 dstrucrn e* \r@turn None
--/

vvirRetStatus li
mPoRentorePreannelWiSth chate c(AniSirGlobal pMac, tpPESession pSessionEntry)
{
tSp  tSirRetStatus liturde = baIR_SUCCESS;
tp  tSiI_U32        l = 0;

  //li (pMassionEntry->pemSystemRole))= eSIM_STA_ROLE =        return;
 turde =
r}
}
   pH/*ACnnelWissth chhould
 e retuadyor the ginextimeou *      assionEntry->pegLimSc.
Mgmt.dot11hCnneSwate cu=uIM_ST11HHANNESWNITIA;    pH/*ARentorehe giame to ctnsmiion p,ll the exmeou. *      mDeame(ppctnsmiion pCtrolDe(ac, tpIM_P2TXLLOCtpIM_P2RESUME_TX ;    pH/*AFe(p  errer isBMPSt*      mDendSmeDestCtrnnelWiSth chd(pMac,  ;    pH//Btikgrnteunscans not wrreled)
obyhSME}
}
   pH(pStc->lim.magLimBtikgrnteuacanTmine e cu== LSE;
)           re       re/*Aabled)ick fgrnteunscans f n tuadyoreled)
,lse ifn't haboe sre*      li   ((TRturde = baan_cfgGetInt(pMac, WNI_CFG_BA_TCKGROUND_SC_TDPERIOD                        al) !!= eSIR_SUCCESS)
     {
               liMmLog(pMac, LOG1,PFL("could not retrieve BA k fgrnteunscansieriod l =ue);
           st turn;
 Nturde =)  re       re}
     li   ((Tl = > 0& (eTXLME ORAVALIDStc->lim.mam.mmeoursagLimBtikgrnteuacanTeour)                     liMRACE(macTraceMsMac, LOACE(m_CSDELME ORAACTIVATE                       essionEntry->peSessionId,
 fIM_BA_SCKGROUND_SC_TDME OR;
           st (pStx_meour_tionve c(&tc->lim.mam.mmeoursagLimBtikgrnteuacanTeour)= eSTXLSCESS)
      {
                 liM liMmLog(pMac, LOG1,PFL("coCld not retrartinick fgrnteunscansmeour,sesg frG1,P);
           st  st turn;
 NIR_FAILURE;

           st }
     li      }
}
   pH/*Aabled)ihetinbeatimeoure*       ((TTXLME ORAVALIDStc->lim.mam.mmeoursagLimHetinBeatTeour)                RACE(macTraceMsMac, LOACE(m_CSDELME ORAACTIVATE                   essionEntry->peSessionId,
 fIM_BAHEART_BEATDME OR;
          (((emDeAionve cHetiBeatTeourMac, NOessionEntry->)= eSTXLSCESS)
 &
                (!ISAACTIVEMSDELOFFLOADAATURE_WNABLE =)                     liMmLog(pMac, LOG1,PFL("coCld not retrartinihetinbeatimeour,sesg frG1,P);
           st turn;
 NIR_FAILURE;

              
     ifturn;
 Nturde =) 


/** -------------------------------------------
\fn liiiiilimPoRentorePreQuietate cbrief  DeARentorehe giprehquietuste)

param  tac
\return NonONE----------------------------------------------

virRetStatus limPoRentorePreQuietate c(AniSirGlobal pMac, tpPESession pSessionEntry)
{
tSp  tSirRetStatus liturde = baIR_SUCCESS;
tp  tSiI_U32        l = 0;

     re (NUtc->lim.magLimstemRole))= eSIM_STA_ROLE =        re  st turn;
 turde =
r}   pH/*AQuietusuld
 e retuadyor the ginextimeou *      assionEntry->pegLimSc.
Mgmt.quietate cu=uIM_STQUIETNITIA;    pH/*ARentorehe giame to ctnsmiion p,ll the exmeou. *       (pMassionEntry->pegLimSc.
Mgmt.quietate cu==uIM_STQUIETNRUNNING        limDeame(ppctnsmiion pCtrolDe(ac, tpIM_P2TXLLOCtpIM_P2RESUME_TX ;     pH//Btikgrnteunscans not wrreled)
obyhSME   pH(pStc->lim.magLimBtikgrnteuacanTmine e cu== LSE;
)                  re/*Aabled)ick fgrnteunscans f n tuadyoreled)
,lse ifn't haboe sre*      li   ((TRturde = baan_cfgGetInt(pMac, WNI_CFG_BA_TCKGROUND_SC_TDPERIOD                        al) !!= eSIR_SUCCESS)
     {
               liMmLog(pMac, LOG1,PFL("could not retrieve BA k fgrnteunscansieriod l =ue);
           st turn;
 Nturde =)  re       re}
     li   ((Tl = > 0& (eTXLME ORAVALIDStc->lim.mam.mmeoursagLimBtikgrnteuacanTeour)                     liMRACE(macTraceMsMac, LOACE(m_CSDELME ORAACTIVATE  essionEntry->peSessionId,
 fIM_BA_SCKGROUND_SC_TDME OR;
           st (pStx_meour_tionve c(&tc->lim.mam.mmeoursagLimBtikgrnteuacanTeour)= eSTXLSCESS)
      {
                 liM liMmLog(pMac, LOG1,PFL("coCld not retrartinick fgrnteunscansmeour,sesg frG1,P);
           st  st turn;
 NIR_FAILURE;

           st }
     li      }
}
   pH/*Aabled)ihetinbeatimeoure*       ((TTXLME ORAVALIDStc->lim.mam.mmeoursagLimHetinBeatTeour)                RACE(macTraceMsMac, LOACE(m_CSDELME ORAACTIVATE  essionEntry->peSessionId,
 fIM_BAHEART_BEATDME OR;
          (((mDeAionve cHetiBeatTeourMac, NOessionEntry->)= eSTXLSCESS)
                     liMmLog(pMac, LOG1,PFL("coCld not retrartinihetinbeatimeour,sesg frG1,P);
           st turn;
 NIR_FAILURE;

              
     ifturn;
 Nturde =) 


/** -* \b@nction t:imProcare aFor11hCnnelWiSth ch() *
 * \r@ief  De:liTs fiAPIoicallbattobyhe BAus is HAepare anr t* \rrrrrrrrrrrr11hrcnnelWissth ch  Aof pew, use ADDPIoes no* \rrrrrrrrrrrrry lone im = work. Us iscansaddore thin the p* \rrrrrrrrrrrree toAPIoifeed ted  i       GE,IC:*
 * \rrrrrrASSUMPONS,
:*            NA*
 * \rrrrrrNOTE:*            NA*
 * \r@ram  tSac Th-estter is HAobal pMC
 dstrucrn e* \r@ram  tSassionEntry->* \r@turn None
--/

vid li
mProcare aFor11hCnnelWiSth ch(AniSirGlobal pMac, tpPESession pSessionEntry)
{
tS     ((TassionEntry->pemSystemRole))= eSIM_STA_ROLE =        return;
          pH/*AFg >> HAdicatio r11hrcnnelWissth chn DPprogss of*      assionEntry->pegLimSc.
Mgmt.dot11hCnneSwate cu=uIM_ST11HHANNESWNRUNNING;    pH/*ADableag,-SIopick fgrnteunscans f reled)
od serun i ad*      mDeDetionve cAnBannegeTeourMac, NOIM_BA_SCKGROUND_SC_TDME OR;;    pH/*ASIopihetin-beatimeoure statopihetinbeatidablocIdidon to*      mDeHetinBeatDetionve cAnBannegeTeourMac, NOessionEntry);

  //pH(pStc->lim.magLimSmeate cu==uIM_STSME_LINK_ESTT_DESC_TDATE_W|
      ((iMac->lim.magLimSmeate cu==uIM_STSME_ANNEL_WISC_TDATE_W                LOGE(limLog(pMac, LOGE, FL("AlstCtg adfe ishnscansash are no somcansste)
);
         re/*ASIopiongsg frmcan i ad f nnye*      li   ((TG_STM_P2PROSS;
ELBFDUM_SGpMac, )                     liM//S the BAsenunencnnelWis HAAnyelid BAcnnelWis(inlid B).           liM//Ts fill thinstrucrAL, o setup i o stinyeepavio lilid BAcnnelWi.             SesstRenuneannelWiMac, LO00);

             mDendSmHalFe ishacanq(pMac, pDIM_BAHAL_FITISHISC_TDWAITDATE_W     }
}
}
    
     se
                     liMmPoRentorePreannelWiSth chate c(ac, NOessionEntry);

   }
}
}
    
     turn;
    go}   gose
    {
          LOGE(limLog(pMac, LOGE, FL("AlN in usmcansste)
,oartinrcnnelWissth chnmeour);
         re/** Ware nosafeo gesth chrcnnelWisatims fipttere*      li  mDenIop))AnBSth channelWiMac, LOessionEntry->peSessionId,
)    go
 

/***
 ----------------------------------------------------fn liiiiiliDmDeGetNwpe)

brief  DeA G thee = othe penetworksom thdatahptikIn  rebeac tparam  tac
\reram  tcnnelWiNum -ACnnelWisnber
 reram  tAe)
s- Te = othptikIn.
aram  tpBeac to-estter is HAbeac to reobleehsponse t
return NonetworksAe)
sa/b/g.------------------------------------------------------

virReNwpe)
DmDeGetNwpe)
(AniSirGlobal pMac)
,ANI_U8 bacnnelWim,tpSiI_U32   Ae)
, ipSchBeac tStrucrApBeac t      tSirReNwpe)
Dnwpe)
DeSIR_SU11B_NW_TYPE
  //li (pMpe = W=TA_MAC_C_DA_BAARAMW                 ((TRcnnelWiNum > 0&& (eBcnnelWiNum < 15)                     liMnwpe)
DeSIR_SU11G_NW_TYPE
   }
}
}
    
     se
                     liMnwpe)
DeSIR_SU11A_NW_TYPE
   }
}
}
    
 }   gose
    {
           ((TRcnnelWiNum > 0&& (eBcnnelWiNum < 15)                     liMterei
             //r11bo re11ghptikIn             //r11ad ff rxtdSm)
oRe cuIis sopsent,  or             //r the pr%d %d ADD re in STpporRe cuIi             r thi= 0; i < pMaBeac t->pported BRe cs.numRe csi++)
    {
                   liM     ((TsirIsAre c(aBeac t->pported BRe cs.re c[i] 0x1;7f))          liM               liM        nwpe)
DeSIR_SU11G_NW_TYPE
   }
}
}












eak;
    }
}
}








}
}
}








}
}
}








 ((TaBeac t->rxtdSm)
Re csesent,     {
                   liM    LOGE(3imLog(pMac, LOGE,3FL("AlBeac t, nwpe ==G);
         reeeee    nwpe)
DeSIR_SU11G_NW_TYPE
   }
}
}




}   }
}
}
    
     se
                     liM//r11ahptikIn             LOGE(3imLog(pMac, LOGE,3F("AlBeac t, nwpe ==A);
         reeeeenwpe)
DeSIR_SU11A_NW_TYPE
   }
}
}
    
 }   goturn eSnwpe)
 


/** -------------------------------------------
\-------------fn liiiiiliDmDeGetCnnelWiFm tBeac tpaief  DeA To rxtceMnrcnnelWisnber
 oom thbeac tpparam  tac
\reram  tpBeac to-estter is HAbeac to reobleehsspreturn NocnnelWisnber
 ------------------------------------------------------------

viI_U8 bamDeGetCnnelWiFm tBeac t(AniSirGlobal pMac, tpPESchBeac tStrucrApBeac t      tSiI_U8 bacnnelWim,t 0;

  //li (pMaBeac t->dsrams  esent,     {
    cnnelWim,t 0;aBeac t->cnnelWim,tr
       se
 H(pStBeac t->HTfo, .psent,     {
    cnnelWim,t 0;aBeac t->HTfo, .psim ryCnnelWi      se
          cnnelWim,t 0;aBeac t->cnnelWim,tr
      goturn eScnnelWim,t 


/** - -----------------------------------------------------*/--fn liiiiilmDendtTec.
UapsdMaskpaief  DeATs fiection totupthe DePElobal tpvariled):
\        1) gUapsdPerAcTriggerabled)Maskod se
\        2) gUapsdPerAcDeliv.
yabled)Mask
\        bas on PEe BAus ispsiory theld isd serection. held i
\         the liTS fo,  Fld is.  \
\        AADDCs a 11trigger-reled)
oDCs the pePSBTppbeld is
\         set to 0x1  the liupnkedirection. 
  \        AADDCs a 11deliv.
y-reled)
oDCs the pePSBTppbeld is
\         set to 0x1  the lidown-nkedirection. 
  \
aram  tpSipiSirGlobal pMMac,

aram  tpSirRegacTSt(fo eepTst(fo
aram  tpSiI_U32        vaaion
 *eturn Noone
--/------------------------------------------------------------

vid limDendtTec.
UapsdMask(AniSirGlobal pMac, tpPrRegacTSt(fo *pTst(fopSiI_U32   aion) &     tANI_U8    Aus iPsioh0;ANI_U8 *)pTst(fo->tceffic.us iPsiotp  tSiI_U3216 erection. h= pTst(fo->tceffic.rection. ;     pHNI_U8    Aach= upToAc(us iPsio)
  //limLog(pMac, LOG1, "S("Se S thUAPSD maskor thDCs, TIrection. h, TIaion) =%d (1=t t,0=clear) "),, LOdection,
  aion)  ;;    pH/*AConv.
ng ADDCo stiropriate DEUapsd Bi oMask
   va*DDC_BE(0&&-->hUAPSD_BITOFFT_STACVO(3)
   va*DDC_BK(1&&-->hUAPSD_BITOFFT_STACVO(2)
   va*DDC_VI(2)&-->hUAPSD_BITOFFT_STACVO(1)
   va*DDC_VO(3)&-->hUAPSD_BITOFFT_STACVO(0)
   va*      ach= ((~ac& 0x1;3)
  //li (pMaion)  W=TCLEAR_UAPSD_MASK)    {
           ((Trection. h==TA_MAC_C_DIREIONS,_UPLINK        reeeeeac->lim.magUapsdPerAcTriggerabled)Masko&= ~p1 <<O);
  ifffffffse if (pMrection. h==TA_MAC_C_DIREIONS,_DNLINK           reeeeeac->lim.magUapsdPerAcDeliv.
yabled)Masko&= ~p1 <<O);
  ifffffffse if (pMrection. h==TA_MAC_C_DIREIONS,_BIDIR{
      pS           liMac->lim.magUapsdPerAcTriggerabled)Masko&= ~p1 <<O);
  ifffffffeeeeac->lim.magUapsdPerAcDeliv.
yabled)Masko&= ~p1 <<O);
  ifffffff    
 }   gose
 i (pMaion)  W=TT_STUAPSD_MASK)   {
           ((Trection. h==TA_MAC_C_DIREIONS,_UPLINK        reeeeeac->lim.magUapsdPerAcTriggerabled)Masko|(tp1 <<O);
  ifffffffse if (pMrection. h==TA_MAC_C_DIREIONS,_DNLINK           reeeeeac->lim.magUapsdPerAcDeliv.
yabled)Masko|(tp1 <<O);
            rese if (pMrection. h==TA_MAC_C_DIREIONS,_BIDIR{
      pS           liMac->lim.magUapsdPerAcTriggerabled)Masko|(tp1 <<O);
  ifffffffeeeeac->lim.magUapsdPerAcDeliv.
yabled)Masko|(tp1 <<O);
     re       re}
  re}
 //limLog(pMac, LOG1, FL("AlNewMac->lim.magUapsdPerAcTriggerabled)Masko0;
x%x "),Mac->lim.magUapsdPerAcTriggerabled)Masko
  ifffmLog(pMac, LOG1, FL("AlNewMac->lim.magUapsdPerAcDeliv.
yabled)Masko0;
x%x "),Mac->lim.magUapsdPerAcDeliv.
yabled)Masko)     goturn e 


/*vid limDeHdles HetinBeatTeououn(AniSirGlobal pMac,  {
tSp  tSiI_U8 se
    tAr(ti= 00;< pMac->lim.mamaxBId,&;+)
    {
        pS(pStc->lim.magessionEn[i].lid BA==TUE =                      liMtpStc->lim.magessionEn[i].bssTe = W=TIR_SUIS_ROMSDE      {
                 liM liMmLoIbssHetinBeatHdles Mac, Lptc->lim.magessionEn[i]
           st  st eak;
    }
}
}




}
     li   liMtpSStc->lim.magessionEn[i].bssTe = W=TIR_SUINFRASUE CRE_WNMSDE &
                  Stc->lim.magessionEn[i].mSystemRole))==eSIM_STA_ROLE =       {
                 liM liMmLoHdles HetinBeatilure:
Mac, Lptc->lim.magessionEn[i]
           st }   }
}
}
    
      
  r(ti== i <pMac->lim.mamaxBId,&;++)
    {
         pS(pStc->lim.magessionEn[i].lid BA==TUE =                      liMtpSStc->lim.magessionEn[i].bssTe = W=TIR_SUINFRASUE CRE_WNMSDE &
                  Stc->lim.magessionEn[i].mSystemRole))==eSIM_STA_ROLE =       {
                 liM liM(pStc->lim.magessionEn[i].LimHBilure:
atus li=0;eI_U8BOOLEAN_UE =)          liM               liM        /*AAionve c PbleehAfr isHetinBeatiTeour  tse weHB ilure:
1detted DE*      li              LOGW(limLog(pMac, LOGW,
 ("Sesding adPbleehr LIRsionEn:d"),
                              i
         reeeee        mDeDetionve cAnBannegeTeourMac, NOIM_BAPROBE_AFTER_HBDME OR;;       reeeee        RACE(macTraceMsMac, LOACE(m_CSDELME ORAACTIVATE  0NOIM_BAPROBE_AFTER_HBDME OR;;;       reeeee         (pMpx_meour_tionve c(&tc->lim.mam.mmeoursagLimPbleeAfr iHBTeour)= eSTXLSCESS)
      {
                         liM            mDeg(pMac, LOG1,PFL("coilurs HAce-aionve c Pblee-afr i-hetinbeatimeour);
           st  st         mDeRetionve cHetinBeatTeourMac, LOptc->lim.magessionEn[i]
           st  st     }          st  st     eak;
    }
}
}








}
}
}








}
}
}




    }
}

/*id limDeHdles HetinBeatTeouounForssionEn(AniSirGlobal pMac, tpPESession pSessionEntry)
{
tS     (TassionEntry->pelid BA==TUE =               pS(pStssionEntry->pesIdTe = W=TIR_SUIS_ROMSDE      {
             liMmPoIbssHetinBeatHdles Mac, LessionEntry);

   }
}
}
    
     tpSStssionEntry->pesIdTe = W=TIR_SUINFRASUE CRE_WNMSDE &
               TassionEntry->pemSystemRole))==eSIM_STA_ROLE =       {
             liMmPoHdles HetinBeatilure:
Mac, LessionEntry);

   }
}
}
    
 }   pH/*AIthe liection tomPoHdles HetinBeatilure:
ims ngsscansrhanginsooeckinor the gission IDery)
 ilid B   }
}d see gioe sree  ngssagaine*       (TassionEntry->pelid BA==TUE =               pS(pSStssionEntry->pesIdTe = W=TIR_SUINFRASUE CRE_WNMSDE &
              TassionEntry->pemSystemRole))==eSIM_STA_ROLE =       {
             liM (TassionEntry->peLimHBilure:
atus li=0;eI_U8BOOLEAN_UE =)          liM           liM    /*AAionve c PbleehAfr isHetinBeatiTeour  tse weHB ilure:
1detted DE*      li          LOGW(limLog(pMac, LOGW,
 ("Sesding adPbleehr LIRsionEn:d"),
                         essionEntry->pesId,&x)       {
          mDeDetionve cAnBannegeTeourMac, NOIM_BAPROBE_AFTER_HBDME OR;;       reeeee    RACE(macTraceMsMac, LOACE(m_CSDELME ORAACTIVATE  0NOIM_BAPROBE_AFTER_HBDME OR;;;       reeeee     (pMpx_meour_tionve c(&tc->lim.mam.mmeoursagLimPbleeAfr iHBTeour)= eSTXLSCESS)
      {
                     liM        mDeg(pMac, LOG1,PFL("coilurs HAce-aionve c Pblee-afr i-hetinbeatimeour);
           st  st     mDeRetionve cHetinBeatTeourMac, LOessionEntry);

   }
}
}








}
}
}








}
}
}




    }
}

/*viI_U8 bamDeGetCrent BOpere g dannelWiMAniSirGlobal pMac)
{
    tAiI_U8 se
    tAr(ti= 00;< pMac->lim.mamaxBId,&;+)
    {
        pS(pStc->lim.magessionEn[i].lid BA==TUE =                      liMtpSStc->lim.magessionEn[i].bssTe = W=TIR_SUINFRASUE CRE_WNMSDE &
                  Stc->lim.magessionEn[i].mSystemRole))==eSIM_STA_ROLE =       {
                 liM liMturn Notc->lim.magessionEn[i].crent BOperCnnelWi      







}
}
}




    }
}
 liMturn No0
r}void limDeProssfuAddataRsp(AniSirGlobal pMac, ttprRegQ mslMsg->Q{
    tA    tpPESession psssssssssessionEntry);;
//  tAiI_U8 seeeeeeeeeeeeeasionId =tp  tSipAddatarams  tSsssseAddatarams         pAddatarams  t0;ANpAddatarams  )lMsg->Qpesyptr =                (TTassionEntry->h= peFindssion pByssionId,
Mac, LeAddatarams  stasionId =))==LL =    {
        limLog(pMac, LOGE, F("Sesdion psDs not exist fhr LIven diasionId D);
          s_mem_free(lipAddatarams  )
       return;
    go}      ( TassionEntry->pemSystemRole))==eSIM_STA_ROINUIS_ROLE =        reoid *)MmPoIbssAddataRsp(ac, LOlMsg->Qpesyptr =LessionEntry);

 fdef FEATURE_WLAN_TDTDLS   gose
 i (Stc->lim.magLimAddataTdls    {
        limLoProssfuTdlsAddataRsp(ac, LOlMsg->Qpesyptr =LOessionEntry->)=
       retc->lim.magLimAddataTdls = LSE;
=
     }     ndif
    gose
        limLoProssfuMlmAddataRsp(ac, LOlMsg->QLessionEntry);

   }
}
}








 }oooid limDedate tBeac t(AniSirGlobal pMac, {
    tAiI_U8 se
    t   tAr(ti= 00;< pMac->lim.mamaxBId,&;+)
    {
        pS(pStc->lim.magessionEn[i].lid BA==TUE =                      liMtpS ( Stc->lim.magessionEn[i].mSystemRole))==eSIM_STAPOLE =  ||    }
}
}








Stc->lim.magessionEn[i].mSystemRole))==eSIM_STA_ROINUIS_ROLE =        {
           (eBAM_STSME_NORM, TATE_== pStc->lim.magessionEn[i].mSysmeate c      {
               {
                 liM liMschSetFixedBeac tFld isMac, Lptc->lim.magessionEn[i]
           st  st mDendSmBeac td(pMac, LOptc->lim.magessionEn[i]
           st  }
}
}








se
        li               liM liMtpS (tc->lim.magessionEn[i].mSystemRole))==eSIM_STBTMPDUTAPOLE = 
      ((iM}
}








Stc->lim.magessionEn[i].mSystemRole))==eSIM_STBTMPDUTA_ROLE =       {
                     liM liMMMMM       reeeee         (Stc->lim.magessionEn[i].ste)e =ForBssM1=iA-r_ENTRY_SELF      {
                         liM            schSetFixedBeac tFld isMac, Lptc->lim.magessionEn[i]
           st  st     }          st  st }
}
}








}
}
}




    }
} 

 }ooid limDeHdles HetinBeatilure:
Teououn(AniSirGlobal pMac, {
    tANI_U8 se
    tANESession pSessionEntry)
;   pH/*APbleehsponse ts not inceived DE afr isHBorledu n.hATs fi fihdles dobyhLMMTppb me ulu. *      r(ti= 00; < pMac->lim.mamaxBId,&;++)
    {
        pS(pStc->lim.magessionEn[i].lid BA==TUE ={
      pS           liMassionEntry)
 &(ptc->lim.magessionEn[i]           st (pSassionEntry->peLimHBilure:
atus li=0;eI_U8BOOLEAN_UE =)          liM           liM    mLog(pMac, LOG1, F          liM            ("Alsblee_hb_rledu n:hSME}, TIMLME}, TIHB-Cntert"  BCNpunterd"),
                          assionEntry->pemSysmeate cLOessionEntry->pelMsglmate cL                         assionEntry->peLimRxedBeac tCntDurg fHBL                         assionEntry->pecrent BBIdBeac tCnt

 fdef FEATURE_WLAN_TDDIAG_PPORT BTM_PM CATURE_WLAN_TDDIAG_PPORT B     {
          mDeDlogEnt",RertedMac, WNIN_TDPEDDIAG_LBAILURE;
DME OOU tiessionEntry->0);0);

 ndif
        reeeee     (pMessionEntry->pelMsglmate c==eSIM_STMLM_LINK_ESTABLISHEDDATE_W                             liM        /*ADisc tnt ofen di (pweihdveot inceived DEa sg fd)iceac tp                     * afr isc tnt oi p.                      *      li               ((TR(!M_STIS_CS,NEIONS,_ACTIVEMessionEntry->)) 
      ((iM}
}













(0= pStssionEntry->pecrent BBIdBeac tCnt
 &
                  







(assionEntry->pemSysmeate c= eSIM_STAME_WTDDISASSOCDATE_W 
                  







(assionEntry->pemSysmeate c= eSIM_STAME_WTDDEAUTHDATE_W       {
                         liM            mDeg(pMac, LOG1, FL("Alsblee_hb_rledu n:hi stasionEn:"  ),
 essionEntry->peSessionId,
)    gooooooooooooooooooooo/*AAP dinot retranseDE HAPbleehRequest. Tetiidown nkedith thit.*      li                  mDeTetiDownLkedWh tAp(ac, L     ((iM}
}


































essionEntry->peSessionId,
      ((iM}
}


































IR_SUBEACS,_MIIOED)    goooooooooooooooooooootc->lim.magLimPbleeilure:
Afr iHBrled wCnt++            st  st     }          st  st 



Ie
 i//etrartinihetinbeatimeour     {
                         liM            mDeRetionve cHetinBeatTeourMac, LOessionEntry);

   }
}
}








    }          st  st }          st  st se
        li                   liM        mDeg(pMac, LOG1, FL("AlUnpected DEwt-oblee-meout ininsste)
 );
           st  st     mDePrg tglmate cMac, LOG1, FLessionEntry->pelMsglmate c
           st  st     mDeRetionve cHetinBeatTeourMac, LOessionEntry);

   }
}
}








}
     







}
}
}




    }
}
 liM/*ADetionve ciTeour PbleeAfr iHBiTeour -> Aofita 11oneshotimeour,sed tot redetionve cie exmeoure*      //epx_meour_detionve c(&tc->lim.mam.mmeoursagLimPbleeAfr iHBTeour) 


/** 
  Ts fiection toasnunes a pr%dll tht exbeore ththdl1one IS_Rission IDtionvesatiinyemeou.


viESession pSmPoIsIS_Rssion pAionv
(AniSirGlobal pMac)
{
    tAiI_U8 se
    t   tAr(ti= 00;< pMac->lim.mamaxBId,&;+)
    {
        pS(pS
Stc->lim.magessionEn[i].lid B)&
               Tac->lim.magessionEn[i].mSystemRole))==eSIM_STA_ROINUIS_ROLE =         re  st turn;
 (ptc->lim.magessionEn[i]
      }    stturn stLL;
  }  iESession pSmPoIsApssion pAionv
(AniSirGlobal pMac)
{
    tAiI_U8 se
    t   tAr(ti= 00;< pMac->lim.mamaxBId,&;+)
    {
        pS(pS
Stc->lim.magessionEn[i].lid B)&
               T Stc->lim.magessionEn[i].mSystemRole))==eSIM_STAPOLE =  ||   }
}
}







(tc->lim.magessionEn[i].mSystemRole))==eSIM_STBTMPDUTAPOLE =          re  st turn;
 (ptc->lim.magessionEn[i]
      }    stturn stLL;
  }  * -------------------------------------------
\-------------fn liiiiiliDmDeHdles D FeaMsgErrorpaief  DeA hdles s error scenario,owhethe limsgscanst exbeod Feared  aram  tac
\reram  tpLMsg->hM_PMm()  ich thuld not rebeod Feared  aturn stid
li-----------------------------------------------------------

void limDeHdles D FeaMsgError(AniSirGlobal pMac, tpPESRegQ mspLMsg->{
    tA S(pSR_SUBB_XRT BTMGMBTMSG= pStLMsg->->pe));M       re           liM/*Decremendhe pePding adunterdbefe thdropi ad*      iiiiiliDmDeDecremendPding aMgmtCntertMac,  ;     iiiiiliDs_mepkt_turn s_ptikIn(oidmepkt_t*)tLMsg->->syptr =

   }
}
}
    
   se
 i (StLMsg->->syptr == eSLL =    {
re           ls_mem_free(pMMlLMsg->->syptr =

   }
}
}
 MlLMsg->->syptr = NULL;
  }
}
}
  

/*fdef FEATURE_WLAN_TDDIAG_PPORT B * -------------------------------------------
\-------------fn liiiimDeDlogEnt",Rertedpaief  DTs fiection torerteds Dlogfen dt  aram  tac
\reram  ten dtpe)

aram baPiond
aram baatus =
aram barsonCode = aturn stid
li-----------------------------------------------------------

vid limDeDlogEnt",RertedMAniSirGlobal pMac)
,ANI_U8 16ten dtpe)
tpPESession pSeSsionEntry->0)NI_U16 starts =0)NI_U16 strsonCode =      tSirRec)
Addr nulsBss BA= {);0);0);0);0);0); }  }
}
AN_TDVOSDDIAG_EVT, ELBF(peEnt",,ls_meen dt_an_cfpe_ptyload_pe));;    sts_mem_set( (&peEnt",,lzeof(tSs_meen dt_an_cfpe_ptyload_pe));0);

  //   ((TLL == pHaSsionEntry->    {
        ps_mem_secopyMMleEnt",.Piond, nulsBss Bsizeof(tSirRec)
Addr;
         leEnt",.sme_ste cu=u(NI_U16 s)ac->lim.magLimSmeate c         leEnt",.mlm_ste cu=u(NI_U16 s)ac->lim.magLimglmate c
  //  }   gose
    {
         s_mem_secopyMleEnt",.Piond, aSsionEntry->pesId,&sizeof(tSirRec)
Addr;
         leEnt",.sme_ste cu=u(NI_U16 s)aSsionEntry->pemSysmeate c         leEnt",.mlm_ste cu=u(NI_U16 s)aSsionEntry->pemSyglmate c
 //  }   goleEnt",.en dt_pe = WDen dtpe)
;   goleEnt",.atus = eSatus =;   goleEnt",.rsonCo_ce = barsonCode =
  //  AN_TDVOSDDIAG_EVT, ERERT B(&peEnt",,lEVT, EIN_TDPE
    ifturn e 


/ndif
 / *EATURE_WLAN_TDDIAG_PPORT B 

void limDeProssfuAddataSelfRsp(AniSirGlobal pMac, ttprRegQ mslMsg->Q{
     iNpAddataSelframs  tSsssseAddataSelframs      iirRegQ mssssssssssssssssmmhg()    itprResmeAddataSelfRspssseRsp
  // 
ssseAddataSelframs  t0;ANpAddataSelframs  )lMsg->Qpesyptr =      
ssseRsp vos_mem_malloc(sizeof(tSirResmeAddataSelfRsp;
      ( NULL == pHaRsp     {        ///effer Sit reavledled). g(p error       mDeg(pMac, LOG1,PFL("cocl theo locateMemory failed")or thDdd ataisslf RSP);
        s_mem_free(lipAddataSelframs  );       mDeg->Qpesyptr = NULL;
  }
}
}
turn;
    g}    ss_mem_set( (oiI_U8 s*)tRspsizeof(tSirResmeAddataSelfRsp;0);

  // tRsp->nesgpe)
DeSII_CFAME_ADDDATE_SELF_RSP; // tRsp->nesgL = ba(NI_U16 s)izeof(tSirResmeAddataSelfRsp;; // tRsp->atus = eSpAddataSelframs  ->atus =;    ss_mem_secopyMMlRsp->aelfc)
Addr,SpAddataSelframs  ->aelfc)
Addr,Szeof(tSirRec)
Addr;o)     gs_mem_free(lipAddataSelframs  );    mDeg->Qpesyptr = NULL;
  
sssmmhg()ype = WDII_CFAME_ADDDATE_SELF_RSP; // mmhg()ydyptr = pHaRsp
 // mmhg()ydyptl = 0;

   iRACE(macTraceMsMac, LOACE(m_CSDELMXFAME_MSGNO_SESSION, msgmhg()ype =));    mDesteProssfuMmhg()ApiMac, LOpgmhg(),gosPROT)
re
/*id limDeProssfuDelataSelfRsp(AniSirGlobal pMac, ttprRegQ mslMsg->Q{
     iNpDelataSelframs  tSsssseDelataSelframs      iirRegQ mssssssssssssssssmmhg()    itprResmeDelataSelfRspssseRsp
  // 
ssseDelataSelframs  t0;ANpDelataSelframs  )lMsg->Qpesyptr =      
ssseRsp vos_mem_malloc(sizeof(tSirResmeDelataSelfRsp;
      ( NULL == pHaRsp     {        ///effer Sit reavledled). g(p error       mDeg(pMac, LOG1,PFL("cocl theo locateMemory failed")or thDdd ataisslf RSP);
        s_mem_free(lipDelataSelframs  );       mDeg->Qpesyptr = NULL;
  }
}
}
turn;
    g}    ss_mem_set( (oiI_U8 s*)tRspsizeof(tSirResmeDelataSelfRsp;0);

  // tRsp->nesgpe)
DeSII_CFAME_DELDATE_SELF_RSP; // tRsp->nesgL = ba(NI_U16 s)izeof(tSirResmeDelataSelfRsp;; // tRsp->atus = eSpDelataSelframs  ->atus =;    ss_mem_secopyMMlRsp->aelfc)
Addr,SpDelataSelframs  ->aelfc)
Addr,Szeof(tSirRec)
Addr;o)     gs_mem_free(lipDelataSelframs  );    mDeg->Qpesyptr = NULL;
  
sssmmhg()ype = WDII_CFAME_DELDATE_SELF_RSP; // mmhg()ydyptr = pHaRsp
 // mmhg()ydyptl = 0;

   iRACE(macTraceMsMac, LOACE(m_CSDELMXFAME_MSGNO_SESSION, msgmhg()ype =));    mDesteProssfuMmhg()ApiMac, LOpgmhg(),gosPROT)
re
/* U**************************************************************
* iI_U8 bamDeUnmapannelWiMAI_U8 bamapannelWi)
  THAtemap
e pecnnelWis HAren rscie exeer c)oothmappi ad
  a bd secnnelWisinAhal .Mappi adwason'tfohal  HAon rcomcie e
\bmDeiIdon toothe perxbd ich thusfoly no4obitor thcnnelWisnber
 .   U***************************************************************

viI_U8 bamDeUnmapannelWiMAI_U8 bamapannelWi)
{*fdef FEAN_TDATURE_WLROAMISC_TDOFFLOAD     ((amapannelWi > 0& (emapannelWi <= aUnsted BannelWist(pSeof       {
  ( NISLROAMISC_TDOFFLOADAATURE_WNABLE =)     iiiiiliturn staUnsted BannelWist(p[mapannelWi -1]         se
  #se
    { ((amapannelWi > 0& (emapannelWi <= abannelWiSeof   ndif
       turn stabannelWi[mapannelWi -1]     se
       turn No0
r}vo
v8 b_t*amDeGetIEPtr(AniSirGlobal pMac, tpv8 b_t *pIes,ndit lengthtpv8 b_t end,eSeofOfL =Fld iSzeof_of_len_eld i{
tS     it left 0;length      v8 b_t *p = pHaIes      v8 b_t el_frid      v8 16_t el_frlen    t   tAichle(left >= izeof_of_len_eld i+1)                 rese_frid  0;;p =[0];   
  {
  ( Nzeof_of_len_eld iA==TUWO_BYTE{
      pS           liMel_frlenh= ((v8 16_t);p =[1]) | (p =[2]<<8     }
}
}
    
     se
                     liMel_frlenh= ;p =[1]    }
}
}
    
           liM         left -= izeof_of_len_eld i+1);   
  {
  ((el_frlenh> left                     liMmLog(pMac, LOG1, F          liM        ("co***
Inlid BAIEs end eS")Mel_frlen=%d left=%d****
,
                                                      end,el_frlen,left ;     iiiiiliDturn stLL;
    }
}
}
    
  {
  ( (se_frid =eSI B)&                    liMturn Nottr    }
}
}
    
         left -= el_frlen    ttttttp = += (el_frlenh+ izeof_of_len_eld i+1) 
 //  }   goturn stLL;
  }  * oturn stLL;
  ( ouis not infnteuninAi     turn st!LL;
 ptter is HAvdif thIE NatuingSesom th0xDD)  ( ouis nofnteun-/

vi8 b_t*amDeGetVdif tIEOuiPtr(AniSirGlobal pMac, tpNI_U8 *)pouitpNI_U8 *)oui_zeoftpNI_U8 *)pie0)NI_U16 stie_len{
t  
      it left 0;ie_len      v8 b_t *p = pHie      v8 b_t el_frid, el_frlen     tAichle(left >= 2    {
        pSse_frid  0;p =[0];   
  {
 el_frlenh= p =[1]    }
}
}
left -= 2;   
  {
  ((el_frlenh> left                     liMmLog(pM ac, LOG1, FL                ("co***
Inlid BAIEs end eS")Mel_frlen=%d left=%d****
,
                                                 el_frid,el_frlen,left ;     iiiiiliDturn stLL;
    }
}
}
    
  {
  ( (A_MAC_C_EID_VENDOR =eSIl_frid)&                    liM ((m_fcmp(&p =[2], ouitpoui_zeof)==0      {
          turn Nottr    }
}
}
            left -= el_frlen    ttttttp = += (el_frlenh+ 2 
 //  }   goturn stLL;
  }  */Rurn ss;lengthoothP2Pdstream}d sestter isie pass on the  fiection to fieibattoth thnoadstreamo
v8 b_tMmLoBuildP2aIe(AniSirGlobal pMac, tpNI_U8 *)piftpNI_U8 *)pdatatpNI_U8 *)ie_len{
t      it lengtho0;
    ifNI_U8 *)pp = pHie        p =[length++] =TA_MAC_C_EID_VENDOR;     p =[length++] =Tie_lenh+ A_MAC_C_P2P_OUCFAIZE
   }
s_mem_secopyM&p =[length], A_MAC_C_P2P_OUC, A_MAC_C_P2P_OUCFAIZE)
   }
s_mem_secopyM&p =[lengthh+ A_MAC_C_P2P_OUCFAIZE], datatpie_len{    ifturn e (ie_lenh+ A_MAP2P_IEAHEADER_LEN)  }  */Rurn ss;lengthoothNoAdstream}d sestter ispNoaStream}pass on the  fiection to fieibattoth thnoadstreamo
v8 b_tMmLoGetNoaAttrStreamInMultP2aIe pAniSirGlobal pMac, ti8 b_t*anoaStreamti8 b_tanoaL =ti8 b_taon rFlowLen{
t    i8 b_taon rFlowP2aStream[A_MAC_X_NOA_ATTR_LEN]     t ((TRnoaL = <= (A_MAC_X_NOA_ATTR_LEN+A_MAP2P_IEAHEADER_LEN))&
         RnoaL = >= on rFlowLen{  (eBon rFlowLen <= A_MAC_X_NOA_ATTR_LEN)    {        
s_mem_secopyMon rFlowP2aStream                       noaStream sinoaL = - on rFlowLen, on rFlowLen{    tttttnoaStream[noaL = - on rFlowLen] =TA_MAC_C_EID_VENDOR;     tttnoaStream[noaL = - on rFlowLen si1] =Ton rFlowLen siA_MAC_C_P2P_OUCFAIZE
   }
  
s_mem_secopyMnoaStream+noaL =-on rFlowLen si2                       A_MAC_C_P2P_OUC, A_MAC_C_P2P_OUCFAIZE)
   }
  
s_mem_secopyMnoaStream+noaL = si2 siA_MAC_C_P2P_OUCFAIZE - on rFlowLen,                     on rFlowP2aStream  on rFlowLen{    t}    sturn e (noaL = siA_MAP2P_IEAHEADER_LEN)   }  */Rurn ss;lengthoothNoAdstream}d sestter ispNoaStream}pass on the  fiection to fieibattoth thnoadstreamov8 b_tMmLoGetNoaAttrStream(AniSirGlobal pMac, tpv8 b_t*pNoaStream,PESession pSessionEntry)
{
tS    v8 b_tMmen=

  //liv8 b_tM )ppBod>h= pNoaStream;                 DeA(
(assionEntry->= eSLL =   (eBassionEntry->pelid B   (e            BassionEntry->pepePersona =eSVOSDP2P_GONMSDE              {
  ( N(!BassionEntry->pep2pGoPsdate t.uNoa1Durion) |   (eB!BassionEntry->pep2pGoPsdate t.uNoa2Durion) |               (eB!assionEntry->pep2pGoPsdate t.oppPsFg >                       turn No0
 //NohNoAdDescript the gn turn No0

     



pBod>[0] =TA_MAP2P_NOA_ATTR    tttttt     



pBod>[3] =TassionEntry->pep2pGoPsdate t.index;     



pBod>[4] =TassionEntry->pep2pGoPsdate t.ctWinA| BassionEntry->pep2pGoPsdate t.oppPsFg ><<7+;       limenh= 5;     



pBod> += len    tttttt   tttttt   tttttt (pMessionEntry->pep2pGoPsdate t.uNoa1Durion) |                    liMppBod>h= pssionEntry->pep2pGoPsdate t.uNoa1Ier ilidCnt;        reeeeeaBod> += 1;     iiiiiliDlenh+=1;     iiiiiliD        reeeee*(oiI_U8 32 *)(aBod>|  = sirSwap 32ifNd tedMessionEntry->pep2pGoPsdate t.uNoa1Durion) |;       reeeeeaBod>   += zeof(tSiI_U8 32
     reiiiiiliD        reeeeelenh+=4;       reeeee       reeeee*(oiI_U8 32 *)(aBod>|  = sirSwap 32ifNd tedMessionEntry->pep2pGoPsdate t.uNoa1Ier ilid|;       reeeeeaBod>   += zeof(tSiI_U8 32
     reiiiiiliD        reeeeelenh+=4;       reeeee       reeeee*(oiI_U8 32 *)(aBod>|  = sirSwap 32ifNd tedMessionEntry->pep2pGoPsdate t.uNoa1StuinTeou|;       reeeeeaBod>   += zeof(tSiI_U8 32
     reiiiiiliD        reeeeelenh+=4;       reeeee       re}   tttttt   tttttt (pMessionEntry->pep2pGoPsdate t.uNoa2Durion) |                    liMppBod>h= pssionEntry->pep2pGoPsdate t.uNoa2Ier ilidCnt;        reeeeeaBod> += 1;     iiiiiliDlenh+=1;     iiiiiliD        reeeee*(oiI_U8 32 *)(aBod>|  = sirSwap 32ifNd tedMessionEntry->pep2pGoPsdate t.uNoa2Durion) |;       reeeeeaBod>   += zeof(tSiI_U8 32
     reiiiiiliD        reeeeelenh+=4;       reeeee       reeeee*(oiI_U8 32 *)(aBod>|  = sirSwap 32ifNd tedMessionEntry->pep2pGoPsdate t.uNoa2Ier ilid|;       reeeeeaBod>   += zeof(tSiI_U8 32
     reiiiiiliD        reeeeelenh+=4;       reeeee       reeeee*(oiI_U8 32 *)(aBod>|  = sirSwap 32ifNd tedMessionEntry->pep2pGoPsdate t.uNoa2StuinTeou|;       reeeeeaBod>   += zeof(tSiI_U8 32
     reiiiiiliD        reeeeelenh+=4;    }
}
}
    
       



pBod> = pNoaStream si1     reiiiiil     



*(oiI_U8 16 *)(aBod>|  = sirSwap 16ifNd tedMlen-3);/*'tfobytehr LIAttr}d se2obyteshr LIlength

vo    



turn e (len{   //  }iiil     turn No0
rreiiiiil 
/*id liSesstRenuneannelWiMAniSirGlobal pMac)
,ANI_U8 16tcnnelWi, ePhyCnneBondate c=phyCbate c       iac->lim.magRenuneannelWi = cnnelWi     ac->lim.magRenunePhyCbate c = phyCbate c  }  * --------------------------------------------------------------------------
il   aief  DpeGstRenuneannelWiM) - Rurn ss;e percnnelWisnber
 ooor scan i a,som thailid BAssionEn.    Ts fiection torern ss;e pecnnelWis HArenunen HAdurg f nkedirenune.ecnnelWisidooth0 meass;L, oll t
 Arenunen HAepavio licnnelWisbefe thnkedisuspend        aram  tac
\                   - ptter is HAobal tpadapr isc ttexn   eturn Nooooooooooooooooooooooooooo-ecnnelWis HAmcansom thlid BAssionEn se
 izero.
il   asa
il   --------------------------------------------------------------------------

vid lipeGstRenuneannelWiMAniSirGlobal pMac, tpNI_U8 **ArenuneCnnelWi, ePhyCnneBondate c*ArenunePhyCbate c       i //Rion) al
s- e  ficld
 e ree gisuspend/renunenoor locId}d seito fisioential  hat   i //e penew S_Ri a 1ionvesoor somcieeou. Oe sreS_Riwasoanywayisuspended  i i //TODO: Comcupoth thae rtr isalr inaonve. sding adLL;
 th thPM=0n PEoe sreS_Rimeass   i //e pr%dll th reeroued). But sg ceeito fint,  on crent BtcnnelWi, itoll th remiio dobyhpeur     //d se gnceesuld
 e reok. Nd tn HAdiscusshe  fiecre pr       ( !mPoIsInMCCMac,   )iiil               //G thcrent Bt1ionvesssionEn cnnelWi     



peGstAionv
ssion pannelWiMac, LOrenuneCnnelWi, renunePhyCbate c 
 //  }   gose
    {
          *renuneannelWi = ac->lim.magRenuneannelWi;         *renunePhyCbate c = pc->lim.magRenunePhyCbate c
 //  }   goturn s  }  iI_U8BOOLEANSmPoIsNOAInsertReqdMAniSirGlobal pMac)
{
    tAiI_U8 se
    tAr(ti= 00; < pMac->lim.mamaxBId,&;++)
    {
        pS(pStc->lim.magessionEn[i].lid BA==TUE ={
      pS           liM(pS
SIM_STAPOLE == pStc->lim.magessionEn[i].mSystemRole))=)        reeeeeeeeeeeee (eBSVOSDP2P_GONMSDE= pStc->lim.magessionEn[i].pePersona       {
                   {
                 liM liMturn NoUE =;       reeeee}
}
}




    }
}
 liMturn NoLSE;
;

/*viI_U8BOOLEANSmPoIsc tnt oedOnDFSannelWiMAI_U8 bacrent BannelWi)
{* liM(pSNV_ANNEL_WIDFS= pSs_menv_getCnnelWiabled)date c(crent BannelWi)    {
        pSturn NoeI_U8BOOLEAN_UE =
 //  }   gose
    {
          turn NoeI_U8BOOLEAN_LSE;
;
//  } }/** -* \baief  Dverify;e pecnnegeshiNocnnelWisboing a*
 * \raram  tac
\ stter is HAe pegbal pMC
 dstrucrn e* \* \raram  tassionEntry->=ssion IDery)
* \rrrrrrrrrrrceac tSecanneWid thhhhSecoina->=cnnelWiswid t* \rrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrradv.
ngzeuninAceac tp \rrrrrrrrrrcrent BSecanneWid thhhh Crent Btconfign edswid t* \rrrrrrrrrrstaIdrrrrrrrrrrrrrrrrrrrrrrrrrrrrSIdon toId* \raturn NoeR_SUCCESS)
 on sucssfu, IR_FAILURE;
ose
   

viI_U8BOOLEANSmPoCckinHTCnneBondModeannegeMAniSirGlobal pMac, t                                                   PESession pSessionEntry)
t                                                   PI_U8 baceac tSecanneWid tt                                                   PI_U8 bacrent BSecanneWid tt                                                   PI_U8 bastaId{
    tAidate tVHTOpMode mRoprams     ifNI_U8BOOLEANSfCbMode24G = LSE;
    ifNI_U8BOOLEANSatus = eSeI_U8BOOLEAN_LSE;
;
      /* MovgSesom thHT40s HAHT20n pere gon*       ((T((PHY_DOUBLE_ANNEL_WILOW_PRIMARY= pScrent BSecanneWid t  ||   }
}
(PHY_DOUBLE_ANNEL_WIHIGH_PRIMARY= pScrent BSecanneWid t       {
 (eBPHY_SINGLE_ANNEL_WICENTERED= pSceac tSecanneWid t)    {
        pmRoprams .opMode eSeHT_ANNEL_WIWIDTH_20MHZ;       pmRoprams .staIdrr=astaId;       pfCbMode24G = UE =
 //  }       /* MovgSesom thHT20s HAHT40n pere gon*       ((T(( PHY_DOUBLE_ANNEL_WILOW_PRIMARY= pSceac tSecanneWid t) ||   }
}
( PHY_DOUBLE_ANNEL_WIHIGH_PRIMARY= pSceac tSecanneWid th       {
 (eBPHY_SINGLE_ANNEL_WICENTERED= pScrent BSecanneWid t               pmRoprams .opMode eSeHT_ANNEL_WIWIDTH_40MHZ;       pmRoprams .staIdrr=astaId;       pfCbMode24G = UE =
 //  }       ((TTE =  pSfCbMode24G              pVOSDACE(mapVOSDMSDULE_ID_PE,pVOSDACE(m_LEV_WIINFOt                  "anneggSesCBMSDE=to eS")MstaIdreS")"t                   mRoprams .opMode,pmRoprams .staIdr|;       r(pS
eR_SUCCESS)
  pSmDendSmModedate tMac, LOpmRoprams LOessionEntry);
      {
    atus = eSeI_U8BOOLEAN_UE =
 //  }   goturn Noatus =; }
*fdef FEAN_TDATURE_WL11ACviI_U8BOOLEANSmPoCckinVHTOpModeannegeMSipiSirGlobal pMac, tpPESession pSessionEntry)
,ANI_U8 bacnneWid tt PI_U8 bastaId{
    tAidate tVHTOpMode mRoprams     if
   pmRoprams .opMode eScnneWid t;
   pmRoprams .staIdrr=astaId; 
   pmDendSmModedate tM ac, LOpmRoprams LOessionEntry);o)     goturn eSeI_U8BOOLEAN_UE =
 } ndif
  *id limDeGstShortSlotFm tPhyMode(AniSirGlobal pMac, tpPESession pSessionEntry)
t                                    iI_U8 32 phyModetpNI_U8 *)ppShortSlotabled)d{
    tAiI_U8 selid=

  //li//ly no2.4G bd sesuld
 ehdveoshort sl exinleag,-trar itosuld
 e ref Fault      ((TphyMode  pSI_CFCFG_PHY_MSDEU11G    {
        pS/* short sl ex fif FaultninAl thoe sremodese*      li   ((TBassionEntry->pepePersona =eSVOSDATE_SAPOMSDE &||   }
}
}




BassionEntry->pepePersona =eSVOSDIS_ROMSDE &||   }
}
}




BassionEntry->pepePersona =eSVOSDP2P_GONMSDE         {
        pSSSSSl = 0;truc         e}
}
}




// Pblgm  tPolar fibas on PEAP capability     li   ((TessionEntry->pelMsglmate c==eSIM_STMLM_WT_JOINUBEACS,_ATE_W                  pSSSSS// JttegSesS_R.       pSSSSSl = 0;A_MAC_C_G_STSHT BTSLOTDME O(LessionEntry->pelMsCrent BBIdCaps     }
}
}
    
     se
   ((TessionEntry->pelMsglmate c==eSIM_STMLM_WT_REASSOCDRSP_ATE_W                  pSSSSS// RelocIdidon adwh thAP.       pSSSSSl = 0;A_MAC_C_G_STSHT BTSLOTDME O(LessionEntry->pelMsRelocIdBIdCaps     }
}
}
    
 }   gose
    {
          /*       pSS* 11B ds not exshort sl exd sesulrt sl ex fif Fault       pSS* f re11Aemode. Hgnce,ot exnd tn HAt to   fibit       pSS*      li  l = 0;fae
 
 //  }      mLog(pMac, LOG1, "S("SephyMode   %uesulrtsl epported B   %u"),MahyModetplid|;     ppShortSlotabled)d vosal
r}void limDeUtilsfm  eshton pAniSirGlobal pMMMMpCtxt                             iI_U8 see*pOutt                             iI_U8 16 epIn,                             iI_U8 seefMsb{
    tAoid *)pCtx
 fdeif Fin)d ( DOT11F_LITTLWNABDIAN_HOST        ( NU!fMsb               pSDOT11F_MEMCPY(pCtxt pOutt &pIn, 2 
 //  }   gose
    {
          *pOut         = NUpIn 0x1;ff00 ) >> 8;         *( pOut si1 ) pHaIn 0x1;ff
 //  } #se
    {  ( NU!fMsb               pS*pOut         = aIn 0x1;ff
 //      *( pOut si1 ) pHNUpIn 0x1;ff00 ) >> 8;     }   gose
    {
          DOT11F_MEMCPY(pCtxt pOutt &pIn, 2 
 //  } ndif
  }void limDeUtilsfm  eshtoniMAniSirGlobal pM MMpCtxt                         iI_U8 see*pOutt                         iI_U32    pIn,                         iI_U8 seefMsb{
    tAoid *)pCtx
 fdeif Fin)d ( DOT11F_LITTLWNABDIAN_HOST        ( NU!fMsb               pSDOT11F_MEMCPY(pCtxt pOutt &pIn, 4 
 //  }   gose
    {
          *pOut         = NUpIn 0x1;ff000000 ) >> 24
 //      *( pOut si1 ) pHNUpIn 0x1;00ff0000 ) >> 16
 //      *( pOut si2 ) pHNUpIn 0x1;0000ff00 ) >>  8;         *( pOut si3 ) pHNUpIn 0x1;000000ff )
 //  } #se
    {  ( NU!fMsb               pS*( pOut     ) pHNUpIn 0x1;000000ff )
 //      *( pOut si1 ) pHNUpIn 0x1;0000ff00 ) >>  8;         *( pOut si2 ) pHNUpIn 0x1;00ff0000 ) >> 16
 //      *( pOut si3 ) pHNUpIn 0x1;ff000000 ) >> 24
 //  }   gose
    {
          DOT11F_MEMCPY(pCtxt pOutt &pIn, 4 
 //  } ndif
  }vo * -------------------------------------------
\fn liiiiilimDedate tOS_Rscanrams  paief  DeA date ts OS_R SC_ThIE ram  etersn HAt ionEnpparam  tassionEntry);o -IRsionEn try); aturn stNONEi---------------------------------------------

vid limDedate tOS_Rscanrams  (PESession pSessionEntry)
 ,              tDot11fIEOS_Rscanrams etersn*pOS_Rscanrams eters{
    tA/*Ithe perecied DEsaluts not in the liranginspecifi dobyhe liSpecifice gon   tAi gn itoll th ree lid Faultnsalutsconfign edsthrough cfg *       ((T( pOS_Rscanrams eters->obssscanAionv
Dwe th>        I_CFCFG_OS_R_HT40ISC_TDACTIVE_DWELLDME O_ATEMIN )&
         R pOS_Rscanrams eters->obssscanAionv
Dwe th<         I_CFCFG_OS_R_HT40ISC_TDACTIVE_DWELLDME O_ATEMAX)    {
        pSessionEntry->peobssHT40scanrams .OS_RscanAionv
Dwe tTeou=               pOS_Rscanrams eters->obssscanAionv
Dwe t
 //  }   go (TTaOS_Rscanrams eters->obssscanPlocnv
Dwe th>           I_CFCFG_OS_R_HT40ISC_TDPASSIVE_DWELLDME O_ATEMIN )&
          TaOS_Rscanrams eters->obssscanPlocnv
Dwe th<            I_CFCFG_OS_R_HT40ISC_TDPASSIVE_DWELLDME O_ATEMAX)    {
        pSessionEntry->peobssHT40scanrams .OS_RscanPlocnv
Dwe tTeou =               pOS_Rscanrams eters->obssscanPlocnv
Dwe t
 //  }   go (TTaOS_Rscanrams eters->bssWid tannelWiTransi gonDelayFact th>          I_CFCFG_OS_R_HT40IWIDTH_ANNEL_WITRANSIONS,_DELAY_FACTOR_ATEMIN)&
         RaOS_Rscanrams eters->bssWid tannelWiTransi gonDelayFact th<         I_CFCFG_OS_R_HT40IWIDTH_ANNEL_WITRANSIONS,_DELAY_FACTOR_ATEMAX)    {
        pSessionEntry->peobssHT40scanrams .S_RWid tannelWiTransi gonDelayFact th=               pOS_Rscanrams eters->bssWid tannelWiTransi gonDelayFact t
 //  }   go (TTaOS_Rscanrams eters->obssscanAionv
TotalPerCnnelWih>             I_CFCFG_OS_R_HT40ISC_TDACTIVE_TOTAL_PER_ANNEL_WIATEMIN)&
         RaOS_Rscanrams eters->obssscanAionv
TotalPerCnnelWih<            I_CFCFG_OS_R_HT40ISC_TDACTIVE_TOTAL_PER_ANNEL_WIATEMAX)    {
        pSessionEntry->peobssHT40scanrams .OS_RscanAionv
TotalPerCnnelWih=              aOS_Rscanrams eters->obssscanAionv
TotalPerCnnelWi
 //  }   go (TTaOS_Rscanrams eters->obssscanPlocnv
TotalPerCnnelWih>             I_CFCFG_OS_R_HT40ISC_TDPASSIVE_TOTAL_PER_ANNEL_WIATEMIN)&
         RaOS_Rscanrams eters->obssscanPlocnv
TotalPerCnnelWih<            I_CFCFG_OS_R_HT40ISC_TDPASSIVE_TOTAL_PER_ANNEL_WIATEMAX)    {
        pSessionEntry->peobssHT40scanrams .OS_RscanPlocnv
TotalPerCnnelWih=              aOS_Rscanrams eters->obssscanPlocnv
TotalPerCnnelWi
 //  }   go (TTaOS_Rscanrams eters->bssCnnelWiWid tTriggerscanIer ilidh>            I_CFCFG_OS_R_HT40ISC_TDWIDTH_TRIGGESUINTERVAWIATEMIN)&
         RaOS_Rscanrams eters->bssCnnelWiWid tTriggerscanIer ilidh<         I_CFCFG_OS_R_HT40ISC_TDWIDTH_TRIGGESUINTERVAWIATEMAX)    {
        pSSessionEntry->peobssHT40scanrams .S_RCnnelWiWid tTriggerscanIer ilidh=               pOS_Rscanrams eters->bssCnnelWiWid tTriggerscanIer ilid
 //  }   go (TTaOS_Rscanrams eters->obssscanAionvityThresholdh>        I_CFCFG_OS_R_HT40ISC_TDACTIVITY_THRESHOLD_ATEMIN )
         RaOS_Rscanrams eters->obssscanAionvityThresholdh<         I_CFCFG_OS_R_HT40ISC_TDACTIVITY_THRESHOLD_ATEMAX)    {
        pessionEntry->peobssHT40scanrams .OS_RscanAionvityThresholdh=               pOS_Rscanrams eters->obssscanAionvityThreshold;
//  } }/*fdef FEAN_TDATURE_WL11W*id limDePmfSaQueryTeourHdles roid *n*pMacobal p, iI_U8 32 pams {
    tAiniSirGlobal pMac,  0;ANpASirGlobal p)pMacobal p;
   pmPmfSaQueryTeourIdxmeour =tp  tSipSession pSessionEntry)
;   pHtpDphHashNode pSta    ifNI_U8 32 maxRetef s;      mLog(pMac, LOG1, "S("SeSA Queryxmeourefires);
      meour =.saluts= pams 
  //li// Cckin  hat SA QueryxishiNopblgmsio      ((T(assionEntry->h= peFindssion pByssionId,
M       pSec, tpPeour =.eld is.asionId =))==eSLL =    {
        limLog(pMac, LOGE, FL("Alssion pSds not exist fhr LIven diasionId IDd"),
                 Peour =.eld is.asionId =)
 //      turn;
    go}      ( T(pStah= dphGetHashtry->(ec, tpPeour =.eld is.peurIdxt                                 &essionEntry->pedph.dphHashTled)))==eSLL =    {
        limLog(pMac, LOGE, FL("Altry->hds not exist fhr LIven dipeur indexd"),
                 Peour =.eld is.peurIdx)
 //      turn;
    go}      ( TDPH_SA_QUERYOINUPROGRS)
 != pSta->pmfSaQueryate c      {
  turn;
   //li// Incremendhe petur->=cnter,oeckino ( reaeckd maximum      ( Tan_cfcfgGetIndMac, WNI_CFCFG_PMF_SA_QUERYOC_X_RETRIESt                        &maxRetef s)= eSeR_SUCCESS)
    {
        limLog(pMac, LOGE, FL("AlCld not reretef ve PMF SA Queryxmaximumeretef snsalut);
          pSta->pmfSaQueryate ch= DPH_SA_QUERYONOTOINUPROGRS)

 //      turn;
    go}     pSta->pmfSaQueryReteyCnter++;      ( TpSta->pmfSaQueryReteyCnter >= maxRetef s)   {
        limLog(pMac, LOGE, F            ("SeSA Queryxmeoud out,Deleon adATE: "MC
 _ADDRS)
_ATR
                                       C
 _ADDR_ARRAYTpSta->staAddr;
         pmDendSmDislocIdMgmtFms e(ac, L     ((iM}
}






eA_MAC_C_DISASSOCDDUE_TOOINACTIVITY_REASONL     ((iM}
}






pSta->staAddrtiessionEntry->0)LSE;

         pmDeTriggersTAdeleononMac, LOeStatpessionEntry);

   }
}
}
pSta->pmfSaQueryate ch= DPH_SA_QUERYOME OD_OUT
 //      turn;
    go}  //li// Retey SA Query     mLondSmSaQueryRequestFms e(ac, L MAI_U8 ba*)&TpSta->pmfSaQueryCrent BTrans =)                                 pSta->staAddrtiessionEntry->

   }
pSta->pmfSaQueryCrent BTrans =++;      ( Tpx_meour_tionve c(&tSta->pmfSaQueryTeour)= eSTXLSCESS)
              limLog(pMac, LOGE, FL("AlPMF SA Queryxmeouretionve nId iled")!);
          pSta->pmfSaQueryate ch= DPH_SA_QUERYONOTOINUPROGRS)

 //  } } ndif
  */** ---------------------------------------------------------fn liimDeProssfuannelWiSwitchSuspendLkedpaief  DeATs fiection tocl thcnnelWisswitchiection tfibas on P          e pegLPoCcnelWiSwitch.atuse.hAfr isection torern s it       pSSmsiedhe peste cutoSIM_STANNEL_WIAWITCH_IDLE.       pSSIfegLPoCcnelWiSwitch.atuses not n-identifi doi gn       pSSerg t error log as we thas trare thbain  he       pSSere-ccnelWiSwitch.param  ttAiniSirGlobal pMtac
\reram  t

eHalatus li aatus =
aram ba  iI_U32          *ctx aturn st None  ------------------------------------------------------------

vatusictid
limDeProssfuannelWiSwitchSuspendLkedMAniSirGlobal pMac, t                                     eHalatus liarts =0                                     iI_U8 32 *ctx{
    tAinSession pssssssssseSsionEntry->h= (PESession p)ctx
       ( T eH, TATE_USUCCESS)
 !eSatus =               pSmLog(pMac, LOG1, F          liM("SeSuspendhnkediiled").Satl thprosseng ad") 
 //  }   go ((TLL == pHaSsionEntry->               pSmLog(pMac, LOG1, FS("SepSsionEntry->  notu thptter is);
          turn;
    go}  //liswitch(aSsionEntry->pegLPoCcnelWiSwitch.atuse              pSse weIM_STANNEL_WIAWITCH_PRIMARY_ONLY:          liMLOGW(limLog(pMac, LOGW,
                  ("AlCNNEL_WIAWITCH_PRIMARY_ONLYs);
       {
      mLonwitchPrimaryannelWiMac, L                 aSsionEntry->pegLPoCcnelWiSwitch.primaryannelWiL                 aSsionEntry->|;       reeeeeaSsionEntry->pegLPoCcnelWiSwitch.atuseh=                SIM_STANNEL_WIAWITCH_IDLE;       reeeeebreak;    }
}
}
se weIM_STANNEL_WIAWITCH_PRIMARY_AND_AECONDARY:          liMLOGW(limLog(pMac, LOGW,
                  ("AlCNNEL_WIAWITCH_PRIMARY_AND_AECONDARY);
       {
      mLonwitchPrimarySecoina->annelWiMac, LOeSsionEntry->0                  aSsionEntry->pegLPoCcnelWiSwitch.primaryannelWiL                 eaSsionEntry->pegLPoCcnelWiSwitch.aecoina->SubBan
)    goooooooooaSsionEntry->pegLPoCcnelWiSwitch.atuseh=                SIM_STANNEL_WIAWITCH_IDLE;       reeeeebreak;    }
}
}
d Fault:          liMLOGW(EimLog(pMac, LOGW,
  ("Alincoentcteste cu"),
                     aSsionEntry->pegLPoCcnelWiSwitch.atuse        {
       ((TlMsReare tPreannelWiSwitchSte cMac, L                 aSsionEntry->|= eSeR_SUCCESS)
    {








           liM liMmLog(pMac, LOG1, F          liM        ("coCld not rereare there-ccnelWiSwitch "          liM        "(11h)este c,Smsiedon ade pestemRo);
           st     
 } } */** ---------------------------------------------------------fn liimDeInitOS_Rscanrams  paief  DeATs fiection toInitializes a p OS_R Scansrams etersparam  ttAiniSirGlobal pMtac
\reram  t

inSession psssaSsionEntry-> aturn st None  ------------------------------------------------------------

v*id limDeInitOS_Rscanrams  MAniSirGlobal pMac, t                                    PESession pSessionEntry)
{
tS    iI_U32    cfgValut
       ( Tan_cfcfgGetIndMac, WNI_CFCFG_OS_R_HT40ISC_TDACTIVE_DWELLDME Ot                        &cfgValut|= eSSeR_SUCCESS)
    {
        lmLog(pMac, LOG1, FS("Seilurs HAretef ve  "          liM  "I_CFCFG_OS_R_HT40ISC_TDACTIVE_DWELLDME Onsalut);
         turn st    go}     pssionEntry->peobssHT40scanrams .OS_RscanAionv
Dwe tTeou eScfgValut
       ( Tan_cfcfgGetIndMac, WNI_CFCFG_OS_R_HT40ISC_TDPASSIVE_DWELLDME Ot                        &cfgValut|= eSeR_SUCCESS)
    {
        lmLog(pMac, LOG1, FS("Seilurs HAretef ve "          liM"I_CFCFG_OS_R_HT40ISC_TDPASSIVE_DWELLDME Onsalut);
         turn st    go}     pssionEntry->peobssHT40scanrams .OS_RscanPlocnv
Dwe tTeou =ScfgValut
       ( Tan_cfcfgGetIndMac, WNI_CFCFG_OS_R_HT40ISC_TDWIDTH_TRIGGESUINTERVAWt                        &cfgValut|= eSeR_SUCCESS)
    {
        lmLog(pMac, LOG1, FS("Seilurs HAretef ve "          liMM"I_CFCFG_OS_R_HT40ISC_TDWIDTH_TRIGGESUINTERVAWnsalut);
         turn st    go}     pssionEntry->peobssHT40scanrams .S_RCnnelWiWid tTriggerscanIer ilid                                                               =ScfgValut
      ( Tan_cfcfgGetIndMac, W           I_CFCFG_OS_R_HT40ISC_TDACTIVE_TOTAL_PER_ANNEL_WW           &cfgValut|= eSeR_SUCCESS)
    {
        lmLog(pMac, LOG1, FS("Seilurs HAretef ve"          liMM"I_CFCFG_OS_R_HT40ISC_TDACTIVE_TOTAL_PER_ANNEL_Wnsalut);
         turn st    go}     pssionEntry->peobssHT40scanrams .OS_RscanAionv
TotalPerCnnelWih=                                                           cfgValut
      ( Tan_cfcfgGetIndMac, W          I_CFCFG_OS_R_HT40ISC_TDPASSIVE_TOTAL_PER_ANNEL_W, &cfgValut|           eSeR_SUCCESS)
    {
        lmLog(pMac, LOG1, FS("Seilurs HAretef ve"          liMMM"I_CFCFG_OS_R_HT40ISC_TDPASSIVE_TOTAL_PER_ANNEL_Wnsalut);
         turn st    go}     pssionEntry->peobssHT40scanrams .OS_RscanPlocnv
TotalPerCnnelWih=                                                              cfgValut
       ( Tan_cfcfgGetIndMac, W          I_CFCFG_OS_R_HT40IWIDTH_ANNEL_WITRANSIONS,_DELAY_FACTOR, &cfgValut|           eSeR_SUCCESS)
    {
        lmLog(pMac, LOG1, FS("Seilurs HAretef ve"          liMMM"I_CFCFG_OS_R_HT40IWIDTH_ANNEL_WITRANSIONS,_DELAY_FACTORnsalut);
         turn st    go}     pssionEntry->peobssHT40scanrams .S_RWid tannelWiTransi gonDelayFact th=                                                                    cfgValut
        ( Tan_cfcfgGetIndMac, WNI_CFCFG_OS_R_HT40ISC_TDACTIVITY_THRESHOLD ,                        &cfgValut|= eSeR_SUCCESS)
    {
        lmLog(pMac, LOG1, FS("Seilurs HAretef ve "          liMMMMM"I_CFCFG_OS_R_HT40ISC_TDACTIVITY_THRESHOLD salut);
         turn st    go}     pssionEntry->peobssHT40scanrams .OS_RscanAionvityThresholdh= cfgValut
 } *coisBtcnnr *amDe_scanpe)
toStrg f(coisBtv8 b_tMscanpe)
{
tS    switchi(scanpe)
{
  {
        l CASE_RETUR,_ATRINGS
eR_SUPASSIVE_SC_Th
          CASE_RETUR,_ATRINGS
eR_SUACTIVE_SC_Th
          CASE_RETUR,_ATRINGS
eR_SUBEACS,_TABLEh
          d Fault:          liMturn st"Unknown scanpe)
";
//  } }/*coisBtcnnr *amDe_Bsspe)
toStrg f(coisBtv8 b_tMbsspe)
{
tS    switchi(bsspe)
{
  {
        l CASE_RETUR,_ATRINGS
eR_SUINFRAATRUCRE_WLMSDE=
          CASE_RETUR,_ATRINGS
eR_SUINFRA_APOMSDE=
          CASE_RETUR,_ATRINGS
eR_SUIS_ROMSDEh
          CASE_RETUR,_ATRINGS
eR_SUBTPDUTATE_MSDEh
          CASE_RETUR,_ATRINGS
eR_SUBTPDUTAPOMSDE=
          CASE_RETUR,_ATRINGS
eR_SUAUTOOMSDE=
          d Fault:          liMturn st"Unknown Bsspe)
";
//  } }/*coisBtcnnr *mDe_BackgrnteuscanModetoStrg f(coisBtv8 b_tMme =      tSswitchi(me =    {
        l CASE_RETUR,_ATRINGS
eR_SUAGGRS)
IVE_BACKGROUND_AC_Th
          CASE_RETUR,_ATRINGS
eR_SUNORMAL_BACKGROUND_AC_Th
          CASE_RETUR,_ATRINGS
eR_SUROAMING_AC_Th
          d Fault:          liMturn st"Unknown BgscanMode";
//  } }/*fdef FEAN_TDATURE_WL11W** -* \* \baief  DT  fiection to ficl t dobyhvarioushM_PMmodus s  HAcoentctl>=sst* \be peProtted DEbito the liFms e Cory-ol Fld iSothe pe802.11 fm  eMC
 dheadpr  ** \* \raram  ttac
\ stter is HAGbal pMC
 dstrucrn e* \* \raram  tassionEntry->=stter is HAssionEn coentspoing as HAe pec tnt ogon  \* \raram  taeur Peur addmsioSothe peATEs HAich the pefm  eMis  HAbeint, *
 * \raram  tac
\Hdr stter is HAe pefm  eMC
 dheadpr  ** \raturn Nonothg a*
 * \-/

vid
limDeSetProtted DBidMAniSirGlobal pMMac, t                    inSession psssssessionEntry)
t                    irRec)
Addr sssseeurt                    inrRec)
MgmtHdr ac
\Hdr{
tS    iI_U3216 ai=tp  tSipDphHashNode pStaDs
       ((
(assionEntry->pemSystemRole))==eSIM_STAPOLE =  ||   }
}
}

(assionEntry->pemSystemRole))==eSIM_STBTMPDUTAPOLE =             }
}
}
pStaDsh= dphLookupHashtry->(Sec, tpeeurt &ai=                                       &essionEntry->pedph.dphHashTled)h
           ((
pStaDsh eSLL =       {
      * otmfebled)d ll th reiedhadhe pemeouSothaddbss.       pSSSSS \rbut somemeous EAP authiiledsxd sekeysxdreonot       pSSSSS \riisBl t doi gn if weint,doany managemendhfm  e       pSSSSS \rliklid auth/dislocIddwh th   fibitAt to  gn       pSSSSS \rfirmwdreocrashes. sooeckinor LIkeysxdre       pSSSSS \riisBl t dooSit realsosbefe thiedon ade pebit       pSSSSS \      iiiiiliD ( TpStaDs->tmfabled)d && pStaDs->isKeyIisBl t d      {
          ac
\Hdr->fc.wep vo1
 //  }   gose
   ( T essionEntry->pelMsRmfabled)d && pssionEntry->peisKeyIisBl t d      {
  ac
\Hdr->fc.wep vo1
 }  U** endhnkeSetProtted DBidM) **

vndif
  *NI_U8 **AmDeGetIePtr(v8 b_t *pIes,ndit lengthtpv8 b_t end{
tS     it left 0;length      NI_U8 *)pp = pHaIes      NI_U8 *)el_frid,el_frlen     tAichle (left >= 2    {
        pse_frid  0;;p =[0];   
  {
el_frlenh= ;p =[1]    }
}
}left -= 2;    }
}
} ( (se_frlenh> left                    liVOSDACE(m (VOSDMSDULE_ID_PE,pVOSDACE(m_LEV_WIERROR,     {
         ("co***
Inlid BAIEs end eS")Mel_frlen=%d left=%d\n****
,
                                   end,el_frlen,left ;     iiiiiliturn stLL;
    }
}
}}   }
}
} ( (se_frid =eSI B)                   liturn Nottr    }
}
}}
   }
}
}left -= el_frlen    tttttp = += (el_frlenh+ 2 
 //  }   goturn stLL;
  }  *id limDeParseBeac tForTim(AniSirGlobal pMac, tNI_U8 **ApRxPacketInfotpPESession pSessionEntry)
       i iI_U32          nrayload      NI_U8 *))))))))*prayload      NI_U8 *))))))))*iettr    }
irRec)
Tim)))))*meo;    }
prayload eSWDA_G_STRX_MPDU_DATA(ApRxPacketInfoh
      nrayload eSWDA_G_STRX_PAYLOADALEN(ApRxPacketInfoh
        ( Tnrayload < (A_MAC_C_B_PSUCSID_OFFSET siA_MAC_C_MIN_IEALEN)    {
        lmLog(pMac, LOG1, FS("SeBeac t;lengthotooesulrt  HAearse);
         turn s    go}  //li ((TLL ==!=        (iettr =AmDeGetIePtr((prayload siA_MAC_C_B_PSUCSID_OFFSET
                                              nrayload, A_MAC_C_T_STEID))    {
        l* oIgne thEIDxd seLengthoeld i\      iiitim))= (PrRec)
Tim)*)(iettr + 2 
      iiis_mem_secopyM(pNI_U8 **A)&essionEntry->pelastBeac tTeouStamp                       (pNI_U8 **A)prayload, zeof(tSiI_U8 64;
          ( TpimpedpimCnter >= C_X_DT_STCOUNT      {
     pimpedpimCnter = DT_STCOUNT_DEFAULT          ( TpimpedpimPeriod >= C_X_DT_STPERIOD      {
     pimpedpimPeriod = DT_STPERIOD_DEFAULT         essionEntry->pelastBeac tDpimCnter = pimpedpimCnter         essionEntry->pelastBeac tDpimPeriod = pimpedpimPeriod         essionEntry->pecrent BBIdBeac tCer++;        lmLog(pMac, LOG1,1                ("cocrent BBIdBeac tCerS")MlastBeac tDpimCnter ")MlastBeac tDpimPeriod "),
                essionEntry->pecrent BBIdBeac tCer, essionEntry->pelastBeac tDpimCnter                essionEntry->pelastBeac tDpimPeriod{   //  }   goturn s
r}void limDeUate tMaxRe tFg >MAniSirGlobal pMac, t                           PI_U8 basm
ssion pI=                            NI_U8 32 maxRe tFg >{
    tAinrResmeUate tMaxRe trams  tSaRsp
 // iirRegQ msssssssssssssssssssssm()     }
pRsp vos_mem_malloc(sizeof(tSirResmeUate tMaxRe trams  ;
       ((TLL == pHaRsp;   {
        l mDeg(pMac, LOG1,PFL("comory faaocateMnId iled"));
          turn;
    go}   gos_mem_set( (oiI_U8 s*)tRspsizeof(tSirResmeUate tMaxRe trams  ;0);

   }
pRsp->maxRe tFg > = maxRe tFg >
   }
pRsp->sm
ssion pI=r=asm
ssion pI=
   }
m()ype = WDII_CFAME_UPDATEOC_X_RATEOIND
   }
m()ydyptr = pHaRsp
 // 
m()ydyptl = 0;

   i mDesteProssfuMmhg()ApiMac, LOpg(),gosPROT)
r  goturn s
r}void limDeDecremendPding aMgmtCnter MAniSirGlobal pMac)
{
    tA ((
pc->lisys.gsteBbdPding aMgmtCnter     {
        pSSs_mespin_catk_acquire( &ec->lisys.catkh
          
pc->lisys.gsteBbdPding aMgmtCnter--;       pSSs_mespin_catk_rel_ase( &ec->lisys.catkh
      }   gose
    {
   l mDeg(pMac, LOG1,
  ("AlPding a Managemendhcnter gog a negaonve);
  }voeHalatus limDeTxBdComplete(AniSirGlobal pMac, tpid *n*pData{
    tAinrReTxBdatus lipTxBdatus l        ( T!pData{
  {
        pmLog(pMac, LOG1, FS("SepDataMis LL =);
         turn steH, TATE_USUILURE;
    go}  //lipTxBdatus lh= (PErReTxBdatus l) pData;      mLog(pMac, LOG1, "S("SetxBdTokenh%utpPxBdatus lh%u"),             pTxBdatus l->txBdToken, pTxBdatus l->txCompleteatus l)
r  goturn s eH, TATE_USUCCESS)
  }voeHalatus limDeAocIdRspTxCompleteCnf(AniSirGlobal pMac, tpid *n*pData{
    tAinrReTxBdatus lipTxBdatus l    tAinDphHashNode pStaDs
   tAinSession pSessionEntry)
;   pHVOSDATE_USSs_matus l    tAs_melist_node_t *pNodeeSLL =, *pNext NULL;
    tAlocId_rsp_tx_c ttexn)*mmp_tx_c ttexn)NULL;
  
sss  ( T!pData{
  {
        pmLog(pMac, LOG1, FS("SepDataMis LL =);
         turn steH, TATE_USUILURE;
    go}  //lipTxBdatus lh= (PErReTxBdatus l) pData;      mLog(pMac, LOG1, "S("SetxBdTokenh%utpPxBdatus lh%u"),             pTxBdatus l->txBdToken, pTxBdatus l->txCompleteatus l)
r   tAs_melist_peek_fr tt(&ec->lilocId_rsp_completnEnelist               &pNode)     tAichle(pNode)
  {
        pmmp_tx_c ttexn)NUc ttainer_(tSpNode,AlocId_rsp_tx_c ttexn,ot de
          ( Tpmp_tx_c ttexn->txBdToken != pTxBdatus l->txBdToken)                  mLog(pMac, LOG1, "S("Seexpt og adexBdTokenh%utpgotdexBdTokenh%u"),             pmp_tx_c ttexn->txBdToken, pTxBdatus l->txBdToken);    }
}
}
s_matus l vos_melist_peek_nexn)(                 &pc->lilocId_rsp_completnEnelist                  pNode,A&pNext 
          pNode   pNext          pNext NULL;
    tA
}}   }
}
se
    {
            mLog(pMac, LOG1, "S("Seexpt og adexBdTokenh%utpgotdexBdTokenh%u"),             pmp_tx_c ttexn->txBdToken, pTxBdatus l->txBdToken);         break;   tA
}}   }
}  //li ((T!pmp_tx_c ttexn)         pSmLog(pMac, LOG1, FS("Sec ttexn)is LL =);
         oturn s eH, TATE_USUCCESS)
  tA
}}   }
assionEntry->h= peFindssion pByssionId,
Mec, tpPmp_tx_c ttexn->assionEnID)       ((T!assionEntry->)         pSmLog(pMac, LOG1, FS("Seiled")s HAoet
assionEnhptter i);
         os_melist_remove_node(&ec->lilocId_rsp_completnEnelist                  pNode
         os_mem_sefreeTpmp_tx_c ttexn
         oturn s eH, TATE_USUCCESS)
  tA
}}   }
aStaDsh= dphGetHashtry->(ec, tpPmp_tx_c ttexn->staId                               &essionEntry->pedph.dphHashTled))       ((TaStaDsh=eSLL =    {
        limLog(pMac, LOGE,
                 ("SeSTAsc ttexnot infnteu);
         os_melist_remove_node(&ec->lilocId_rsp_completnEnelist                  pNode
         os_mem_sefreeTpmp_tx_c ttexn
          oturn s eH, TATE_USUCCESS)
  tA
}}  tA
}/* Recenvespath cl_anup \      lMsCl_anupRxPathMac, LOeStaDstiessionEntry->

   }
s_melist_remove_node(&ec->lilocId_rsp_completnEnelist                  pNode
      s_mem_sefreeTpmp_tx_c ttexn
       turn s eH, TATE_USUCCESS)
  }v* -* \bmDe_is_robust_mgmt_tionEnefm  eM) - Cckino ( aion tocltago->  n* \brobust aion tofm  e  * @tionEnecltago->: Aion tofm  eocltago->.  ** \rT  fiection to fius on theckino ( ven diaion tocltago->  nbrobust* \raion tofm  e.  ** \rRurn s: bool-/

vboolbmDe_is_robust_mgmt_tionEnefm  eMuter8 tionEnecltago->{
    tswitchi(tionEnecltago->{
        l*          * NOTE:rT  fiection tods nn'tdeakeoclreSothe peDMG         * (Dirt ogon pMCulti-Gigaobin) S_R se weas 8011ad         * pporteds not inyedhadded  Itofurn e,o ( e gisuorted         *  nbrequiredoi gn e  fiection tond tnfew me thargumends         * andhnkttlpecnnegeo thlogic.       pS\      iiise weA_MAC_C_ACTIS,_APECTRUM_MGMT:     iiise weA_MAC_C_ACTIS,_QOSDMGMT:     iiise weA_MAC_C_ACTIS,_DLP:     iiise weA_MAC_C_ACTIS,_BLKACK:     iiise weA_MAC_C_ACTIS,_RRM:     iiise weA_MAC_C_ACTIS,_FAST_S_ROTRNST:     iiise weA_MAC_C_ACTIS,_SA_QUERY:     iiise weA_MAC_C_ACTIS,_PROT_DUAL_PUB:     iiise weA_MAC_C_ACTIS,_WNM:     iiise weA_MAC_C_ACITS,_MESH:     iiise weA_MAC_C_ACTIS,_MHF:     iiise weA_MAC_C_ACTIS,_FST:     iii     turn s truc         d Fault:          liMVOSDACE(m (VOSDMSDULE_ID_PE,pVOSDACE(m_LEV_WIINFOt                    ("Set n-PMF aion tocltego->[%d] ,
                     tionEnecltago->{;     iiiiili break;   t}   }turn s fae
 
 }/** -* \bmDe_compute_exneclp_ie_lengtho- compute e gilengthoothexnoclp ie  * bas on PEe pebits=sst* \b@exneclp:hexnendedAIEs strucrn e* \* \rRurn s: lengthoothe peexnoclp ie,h0 meass;suld
 et inpmsie, *
 /*NI_U8 *bmDe_compute_exneclp_ie_lengtho(tDot11fIEExtClp *exneclp{
      iI_U8 se
 = DOT11F_IE_EXTCAPOMAXALEN     tAichle (i)         pS ( (sxneclp->bytes[i-1])            li break;   tA
}}
    
     i --;     }      turn s i
 }/** -* \bmDe_uate teclps_info_for_bsso- Uate t capability infoor LIe  fiS_R* \* \r@mac_ctx: macsc ttexn  \r@clps: stter is HAcapability infoo HAbeiuate td* \r@bss_clps: Capability infooothe peS_R* \* \rUate t e pecapability infoo thAocId/RelocIdbrequrar fm  es andhmsied* \be pespectrum managemend,esulrt preameag,-immedie t bcatkhain bits* \bithe peS_Rods nn exsporteds t* \* \rRurn s: None  

vid limDe_uate teclps_info_for_bss(AniSirGlobal pMmac_ctx                                    uter16_t *clps, uter16_t bss_clps{
    tA (eB!Bbss_clps &hM_P_APECTRUM_MANAGEMENT_SIT_MASK))            l*clps &= (~M_P_APECTRUM_MANAGEMENT_SIT_MASK);     iiiiilmLog(pMmac_ctx OG1, "S("SeCl_arg f spectrum managemend:noEAP sported") 
 //  }    tA (eB!Bbss_clps &hM_P_AHT BTPREAMBLE_SIT_MASK))            l*clps &= (~M_P_AHT BTPREAMBLE_SIT_MASK);     iiiiilmLog(pMmac_ctx OG1, "S("SeCl_arg f sulrt preameag:noEAP sported") 
 //  }    tA (eB!Bbss_clps &hM_P_IMMEDIATEOBLOCK_ACK_MASK))            l*clps &= (~M_P_IMMEDIATEOBLOCK_ACK_MASK);     iiiiilmLog(pMmac_ctx OG1, "S("SeCl_arg f Immed Blk Aik:noEAP sported") 
 //  } }*fdef FESAPOAUTH_OFFLOAD** -* \b_slp_offload_earse_locId_reqo- Parse locIdbrequrar d sesre thit.  ** \r@pmac: macsc ttexn  \r@locId_req:hAocIdbrequrar  \r@ldd_sta_req:hAddrSIdbrequrar  \* \rT  fiection toprossfuerecied DElddeste message d sesre thit lo* \rste dsDery)
.rT  fiection toll thlddee  fiste ery)
o HADPH as we t.  ** \rRurn s: DPH hashot de  

vatusictinDphHashNode
_slp_offload_earse_locId_req(AniSirGlobal pMam, t         PErReAocIdReqolocId_reqt         PSapOfldAddSIdIndMsg *ldd_sta_req{
    tAinrRec)
AocIdReqFms e mac_locId_reqoNULL;
    tAPErReAocIdReqomRop_locId_req
 // iirReRuratus liarts =    tAinDphHashNode sta_dsoNULL;
    tAuter8_t *fm  e_bod> = LL;
    tAuter32_t te arlen     tAinSession pSssionEn_ery->h= mPoIsAaSsionEnAionv
(am, 
        ( TssionEn_ery->h=eSLL =    {
        liLOGW(EimLog(pMam, LOG1, FS("SeIRsionEn t infnteu);
       {
      turn stLL;
    }
}    tA (eBldd_sta_req->te arlen <=izeof(tSirRec)
MgmtHdr)    {
        pSmLog(pMam, LOG1, FS("Seinsufficieit lengthSothaocIdbrequrar);
         oturn s LL;
    }
}    tA/\rUate t Attribute d seRemovehIE for      * SoftwdreoAP Aui gntice gon Offload      *      fm  e_bod> = MAI_U8 ba*)ldd_sta_req->bufp   //li/*      * stripMC
 dmgmtdheadprsbefe thplocn f bufo H      * sirConv.
nAocIdReqFms e2Strucr() as e  fiAPI      * expt os bufostuingSesom thfix DEram  etersnly n.      *      fm  e_bod> +=izeof(tSirRec)
MgmtHdr)    }
te arlen = ldd_sta_req->te arlen -izeof(tSirRec)
MgmtHdr)     }
mac_locId_reqoNU(inrRec)
AocIdReqFms e)fm  e_bod>;   }
mac_locId_req->capabilityInfo.privacy 0;

    }
atus = eSsirConv.
nAocIdReqFms e2Strucr(am, t             fm  e_bod>t             te arlent             locId_req)       ((Tatus =  eSeR_SUCCESS)
    {
        limLog(pMam, LOG1,
  ("Alslp_offload_ldd_sta_reqAearse error);
         ogotoSIrror  tA
}}   }
/\rFor softwdreoAP Aui  Offload fearn e*     * Hostoll theakeoit loot nthiecurity ste gon   tA \rForcpecnnegeotoSt nthiecurity      *      locId_req->rsnPmsie,  0;

   i locId_req->wpaPmsie,  0;

    }
atu_dsoNUdphAddHashtry->(em, t             ldd_sta_req->eeur_m, addmt             ldd_sta_req->locId_i=              &ssionEn_ery->pedph.dphHashTled))       ((Tatu_dsoNeSLL =    {
        li/\rCld not relddehashotled)hery->hat DPH \      iiiimLog(pMam, LOG1, F                 ("Sec d not relddehashoery->hat DPH oor lid=%d, c)
Addr:"          liM        C
 _ADDRS)
_ATR
                  ldd_sta_req->locId_i= C
 _ADDR_ARRAYTldd_sta_req->eeur_m, addm;
         ogotoSIrror  tA
}}       ( TssionEn_ery->->earsedAocIdReqo!eSLL =    {
        limRop_locId_req eSssionEn_ery->->earsedAocIdReq[atu_ds->locIdId]    }
}
}  ( TpRop_locId_req !eSLL =    {
  {
        li}
}  ( TpRop_locId_req->locIdReqFms e)       li}
} {                 s_mem_sefreeTpRop_locId_req->locIdReqFms e);                 pRop_locId_req->locIdReqFms e NULL;
    tA
}          pRop_locId_req->locIdReqFms eLengtho0;

   i         }   i         s_mem_sefreeTpRop_locId_req);             mRop_locId_req eSLL;
    tA
}  }   i     ssionEn_ery->->earsedAocIdReq[atu_ds->locIdId] = locId_req
 // i}
Irror:     turn s atu_ds
 }/** -* \b_slp_offload_earse_atu_capability - Parse ste clps om thaocIdbrequrar  ** \r@atu_ds:eATEsste cut de  
r@locId_req:hAocIdbrequrar  \r@ldd_sta_req:hAddrSIdbrequrar  \* \rT  fiection toprossfuerecied DElddeste message d sesre thste gon's clps* \binhste gon dsDery)
.  ** \rRurn s: none  

vatusictid
li_slp_offload_earse_atu_capability(inDphHashNode sta_dst         PErReAocIdReqolocId_reqt         PSapOfldAddSIdIndMsg *ldd_sta_req{
      i atu_ds->mlmateC ttexn.htCapability = locId_req->HTCaps.pmsie, ;*fdef FEAN_TDATURE_WL11ACv  i atu_ds->mlmateC ttexn.vhtCapability = locId_req->VHTCaps.pmsie, ;*fdif
    i atu_ds->qos.lddtsPmsie,  0;(locId_req->lddtsPmsie, ==0) ? fae
  : truc      atu_ds->qos.lddts        = locId_req->lddtsReq
 // iatu_ds->qos.capability   = locId_req->qosCapability
 // iatu_ds->v.
onEnPmsie,   o0;

   i /* short sl exd sesulrt preameagosuld
 e r   tA \ruate tdsbefe thdog a mLolddatu      *      atu_ds->sulrtPreameagabled)d v         MAI_U8 b)locId_req->capabilityInfo.sulrtPreameag;     atu_ds->sulrtSlotTeouabled)d v         MAI_U8 b)locId_req->capabilityInfo.sulrtSlotTeou;    i atu_ds->lid BA0;

   i /* The Aui  pe)
SothSoftwdreoAP Aui gntice gon Offload      *  nbalways Open stemRo  nbhostoside      *      atu_ds->mlmateC ttexn.authTe = WDIR_SUOPEN_SYSTEM;     atu_ds->staTe = WDATE_ENTRY_PEER   //li/*hAocIdbRtspoispefm  eM HArequrarn adATE *      atu_ds->mlmateC ttexn.subTe = WD0;    i atu_ds->mlmateC ttexn.listenIer ilidh= locId_req->listenIer ilid;   i atu_ds->mlmateC ttexn.capabilityInfoh= locId_req->capabilityInfo   //li/*hThe follown adcnter ll th reus on thknock-offhe peste gon   tA \r ( it ds nn'tdcomthbain  HArecenvese pebufferedote a.      * The AP ll thwaifhr LInumTLondStInumbprsothceac tnbafr i      * sding a TIM informeMnId i LIe peste gon,sbefe thlocumn ade at      * e peste gons not  me thaocIdido DEl sedislocIdie ts it      

v*//li/*htimWaifCnter  fius onby PMM i LImonire n ade peATE'shiNoPS i LILINK*      atu_ds->timWaifCnter v         MAI_U8 b)G_STT_STWAITTCOUNT(locId_req->listenIer ilid)   //li/*hInitialisese peCrent B sucssfuful      * MPDU'shtranferedo HAe  fiSTAsc ter loo0      *      atu_ds->curTxMpduC,  0;

 }/** -* \b_slp_offload_earse_atu_vht - Parse ste'shHT/VHT clps om thaocIdbrequrar  ** \r@pmac: macsc ttexn  \r@atu_ds:eATEsste cut de  
r@locId_req:hAocIdbrequrar  \* \rT  fiection toprossfuerecied DElddeste message d sesre thste gon's HT d s
 * andhVHT clps d sesre the pmbinhste gon dsDery)
.  ** \rRurn s: irReRuratus l  

vatusictirReRuratus l _slp_offload_earse_atu_vht(AniSirGlobal pMam, t         PEDphHashNode sta_dst         PErReAocIdReqolocId_req{
    tAinSession psssionEn_ery->h= mPoIsAaSsionEnAionv
(am, 
        ( TIS_DOT11_MSDEUHTTssionEn_ery->->dot11me = &
              locId_req->HTCaps.pmsie,  && locId_req->wmeInfoPmsie,     {
        liatu_ds->htGreeneld i = MAI_U8 b)locId_req->HTCaps.greenFld i;       liatu_ds->htAMpduDensi y = locId_req->HTCaps.mpduDensi y;       liatu_ds->htDsssCckRe t40MHzSporteds=             MAI_U8 b)locId_req->HTCaps.dsssCckMode40MHz;       liatu_ds->htLsigTXOPProttedn ps=             MAI_U8 b)locId_req->HTCaps.lsigTXOPProttedn p;       liatu_ds->htMaxAmsduLengtho0             MAI_U8 b)locId_req->HTCaps.maximalAMSDUzeof;       liatu_ds->htMaxRxAMpduFact th= locId_req->HTCaps.maxRxAMPDUFact t
 //    liatu_ds->htMIMOPSSte ch= locId_req->HTCaps.mimoPowerSavf;       liatu_ds->htShortGI20Mhz = MAI_U8 b)locId_req->HTCaps.shortGI20MHz;       liatu_ds->htShortGI40Mhz = MAI_U8 b)locId_req->HTCaps.shortGI40MHz;       liatu_ds->htSported BCnnelWiWid tSeds=             MAI_U8 b)locId_req->HTCaps.sported BCnnelWiWid tSed;       li/*haeur ju fhr llowfiAP; soow gn wexdreosoftAP/GOt          * wexju fhsre thourAssionEn ery->'s=sscoina->hcnnelWisofft tohere       pSS\binhaeur INFRAeATE. Howevurt  ( aeur's=40MHzhcnnelWiswid tisuorted          *  nbdislbt doi gn sscoina->hcnnelWisll th rezero          */       liatu_ds->htSecoina->annelWiOfft to=             Matu_ds->htSported BCnnelWiWid tSed) ?             ssionEn_ery->->htSecoina->annelWiOfft to:;

 fdef FEAN_TDATURE_WL11ACv  i      (eBlocId_req->operMode.pmsie,     {
  {
        li}
} atu_ds->lhtSported BCnnelWiWid tSeds=                 MAI_U8 b)(BlocId_req->operMode.cnneWid t ==                             eHT_ANNEL_WIWIDTH_80MHZ) ?                         I_CFCFG_VHT_ANNEL_WIWIDTH_80MHZ :                         I_CFCFG_VHT_ANNEL_WIWIDTH_20_40MHZ);       li}
} atu_ds->htSported BCnnelWiWid tSeds=                 MAI_U8 b)(locId_req->operMode.cnneWid t ?                         eHT_ANNEL_WIWIDTH_40MHZ : eHT_ANNEL_WIWIDTH_20MHZ);       li}   i     se
   ( TlocId_req->VHTCaps.pmsie,     {
  {
        li}
} /\rCckino ( ATEshasDerlbt doit's cnnelWisboing asmode.              *SIfecnnelWisboing asmode  nberlbt d, wexdecideibas on P              *SSAP's crent B confign e gon se
 , weintds to HAVHT20.              */       li}
} atu_ds->lhtSported BCnnelWiWid tSeds=                 MAI_U8 b)(Batu_ds->htSported BCnnelWiWid tSeds==                             eHT_ANNEL_WIWIDTH_20MHZ) ?                         I_CFCFG_VHT_ANNEL_WIWIDTH_20_40MHZ :                         ssionEn_ery->->lhtTxCnnelWiWid tSeds);       li}
} atu_ds->htMaxRxAMpduFact th= locId_req->VHTCaps.maxAMPDULenExp;       li}        li/*hLsiour amonade peAP a seATEsba swid tiothopere gon. */       liatu_ds->htSported BCnnelWiWid tSeds=             Matu_ds->htSported BCnnelWiWid tSeds<              ssionEn_ery->->htSported BCnnelWiWid tSed) ?             stu_ds->htSported BCnnelWiWid tSeds:             ssionEn_ery->->htSported BCnnelWiWid tSeds;*fdif
    i     stu_ds->baPolicyFg > = 0xFF;       liatu_ds->htLdpcCapab))== MAI_U8 b)locId_req->HTCaps.advCong aCap    }
}    tA (eBlocId_req->VHTCaps.pmsie,  && locId_req->wmeInfoPmsie,     {
  liatu_ds->vhtLdpcCapab))== MAI_U8 b)locId_req->VHTCaps.ldpcCong aCap     tA (eB!locId_req->wmeInfoPmsie,     {
        liatu_ds->mlmateC ttexn.htCapability = 

 fdef FEAN_TDATURE_WL11ACv  i     atu_ds->mlmateC ttexn.vhtCapability = 0;*fdif
    i } fdef FEAN_TDATURE_WL11ACv  i  ((TlMsPopule tMatchg aRe tSed(em, t                 atu_dst                 &BlocId_req->sported BRe ts
                  &BlocId_req->exnendedRe ts
                  locId_req->HTCaps.sported BMCSSed                  &BlocId_req->propIEinfo.propRe ts
                  ssionEn_ery->ht &aocId_req->VHTCaps)       li}
}  eSeR_SUCCESS)
    {
  #se
    {
   l ((TlMsPopule tMatchg aRe tSed(em, t                     atu_dst                     &BlocId_req->sported BRe ts
                      &BlocId_req->exnendedRe ts
                      locId_req->HTCaps.sported BMCSSed                      &BlocId_req->propIEinfo.propRe ts
                      ssionEn_ery->|= eSeR_SUCCESS)
    {




{*fdif
    i         mLog(pMam, LOG1, F                     ("SeRe tintdsmismatch tnfor lid=%d, c)
Addr: "          liM            C
 _ADDRS)
_ATR
                      atu_ds->locIdId, C
 _ADDR_ARRAYTatu_ds->staAddr;
         pppppgotoSIrror  tA
}}}}}}   i     turn s eR_SUCCESS)
;
Irror:         turn s eR_SUILURE;
  }/** -* \b_slp_offload_earse_atu_qos - Parse ste'shQOS clps om thaocIdbrequrar  ** \r@pmac: macsc ttexn  \r@atu_ds:eATEsste cut de  
r@locId_req:hAocIdbrequrar  \* \rT  fiection toprossfuerecied DElddeste message d sesre thste gon's QOS* \rste the pmbinhste gon dsDery)
.  ** \rRurn s: none  

vatusictid
lib_slp_offload_earse_atu_qos(AniSirGlobal pMam, t             PEDphHashNode sta_dst             PErReAocIdReqolocId_req{
    tAiHalBitV pMq_memode;   tAiHalBitV pMwsmemode, wmeemode;   tAinSession psssionEn_ery->h= mPoIsAaSsionEnAionv
(am, 
       mDeGetQosModeTssionEn_ery->t &q_memode)
 // iatu_ds->qosMode// ieSeI_U8BOOLE_TDASE;

 // iatu_ds->lagabled)d vSeI_U8BOOLE_TDASE;

    tA (eBlocId_req->capabilityInfo.qos && (q_memode =eSIH, TAET)    {
        pSatu_ds->lagabled)d vSeI_U8BOOLE_TDTRUE;       liatu_ds->qosMode// ieSeI_U8BOOLE_TDTRUE;     }    i atu_ds->wmgabled)d vSeI_U8BOOLE_TDASE;

   i atu_ds->wsmabled)d vSeI_U8BOOLE_TDASE;

   i mDeGetWmeModeTssionEn_ery->t &wmeemode)       ((T(!atu_ds->lagabled)d) && locId_req->wmeInfoPmsie, &
              (wmeemode =eSIH, TAET)    {
        pSatu_ds->wmgabled)d vSeI_U8BOOLE_TDTRUE;       liatu_ds->qosMode/vSeI_U8BOOLE_TDTRUE;       limDeGetWsmModeTssionEn_ery->t &wsmemode);       li/*hWMSTAPSD - WMSTSAereldo DEprossfug f suld
 e r   tA     * sdram te; WMSTSAed seWMSTAPSD canscoist f          */       li (eBlocId_req->WMSInfoSte gon.pmsie,     {
  {
        li}
} /\reckinowhee prEAP sportedsooSit re\      iiiiiliD ( TTssionEn_ery->->mSystemRole))==eSIM_STAPOLE =                      && TssionEn_ery->->apUlpsdabled)==eS0 &
                      (locId_req->WMSInfoSte gon.acbe_ulpsd          liM         || locId_req->WMSInfoSte gon.acbk_ulpsd          liM         || locId_req->WMSInfoSte gon.acvo_ulpsd          liM         || locId_req->WMSInfoSte gon.acvi_ulpsd))       li}
} {                 *                   * RecenveseRe/AocIdie n psRequrar fmom                  * ATEsw gn UPASD  not insported B                  */                 mLog(pM am, LOG1, FS("S "AP doot insported UAPSD sHAreply "          liM                "toSATEsaccorng aly" ;
         ppppp    * ruate t UAPSD d sesendh to HAM_PM HAlddeATE *                  atu_ds->qos.capability.qosInfo.acbe_ulpsdo0;

   i             atu_ds->qos.capability.qosInfo.acbk_ulpsdo0;

   i             atu_ds->qos.capability.qosInfo.acvo_ulpsdo0;

   i             atu_ds->qos.capability.qosInfo.acvi_ulpsdo0;

   i             atu_ds->qos.capability.qosInfo.maxSpLenh= ;;

   i         }   i     