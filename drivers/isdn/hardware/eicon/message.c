/*
 *
 Copyright (c) Eicon Networks, 2002.
 *
 This source file is supplied for the use with
 Eicon Networks range of DIVA Server Adapters.
 *
 Eicon File Revision :    2.1
 *
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.
 *
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
 implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 See the GNU General Public License for more details.
 *
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */





#include "platform.h"
#include "di_defs.h"
#include "pc.h"
#include "capi20.h"
#include "divacapi.h"
#include "mdm_msg.h"
#include "divasync.h"



#define FILE_ "MESSAGE.C"
#define dprintf









/*------------------------------------------------------------------*/
/* This is options supported for all adapters that are server by    */
/* XDI driver. Allo it is not necessary to ask it from every adapter*/
/* and it is not necessary to save it separate for every adapter    */
/* Macrose defined here have only local meaning                     */
/*------------------------------------------------------------------*/
static dword diva_xdi_extended_features = 0;

#define DIVA_CAPI_USE_CMA                 0x00000001
#define DIVA_CAPI_XDI_PROVIDES_SDRAM_BAR  0x00000002
#define DIVA_CAPI_XDI_PROVIDES_NO_CANCEL  0x00000004
#define DIVA_CAPI_XDI_PROVIDES_RX_DMA     0x00000008

/*
  CAPI can request to process all return codes self only if:
  protocol code supports this && xdi supports this
*/
#define DIVA_CAPI_SUPPORTS_NO_CANCEL(__a__)   (((__a__)->manufacturer_features & MANUFACTURER_FEATURE_XONOFF_FLOW_CONTROL) && ((__a__)->manufacturer_features & MANUFACTURER_FEATURE_OK_FC_LABEL) && (diva_xdi_extended_features & DIVA_CAPI_XDI_PROVIDES_NO_CANCEL))

/*------------------------------------------------------------------*/
/* local function prototypes                                        */
/*------------------------------------------------------------------*/

static void group_optimization(DIVA_CAPI_ADAPTER *a, PLCI *plci);
static void set_group_ind_mask(PLCI *plci);
static void clear_group_ind_mask_bit(PLCI *plci, word b);
static byte test_group_ind_mask_bit(PLCI *plci, word b);
void AutomaticLaw(DIVA_CAPI_ADAPTER *);
word CapiRelease(word);
word CapiRegister(word);
word api_put(APPL *, CAPI_MSG *);
static word api_parse(byte *, word, byte *, API_PARSE *);
static void api_save_msg(API_PARSE *in, byte *format, API_SAVE *out);
static void api_load_msg(API_SAVE *in, API_PARSE *out);

word api_remove_start(void);
void api_remove_complete(void);

static void plci_remove(PLCI *);
static void diva_get_extended_adapter_features(DIVA_CAPI_ADAPTER *a);
static void diva_ask_for_xdi_sdram_bar(DIVA_CAPI_ADAPTER *, IDI_SYNC_REQ *);

void callback(ENTITY *);

static void control_rc(PLCI *, byte, byte, byte, byte, byte);
static void data_rc(PLCI *, byte);
static void data_ack(PLCI *, byte);
static void sig_ind(PLCI *);
static void SendInfo(PLCI *, dword, byte **, byte);
static void SendSetupInfo(APPL *, PLCI *, dword, byte **, byte);
static void SendSSExtInd(APPL *, PLCI *plci, dword Id, byte **parms);

static void VSwitchReqInd(PLCI *plci, dword Id, byte **parms);

static void nl_ind(PLCI *);

static byte connect_req(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte connect_res(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte connect_a_res(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte disconnect_req(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte disconnect_res(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte listen_req(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte info_req(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte info_res(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte alert_req(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte facility_req(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte facility_res(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte connect_b3_req(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte connect_b3_res(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte connect_b3_a_res(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte disconnect_b3_req(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte disconnect_b3_res(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte data_b3_req(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte data_b3_res(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte reset_b3_req(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte reset_b3_res(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte connect_b3_t90_a_res(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte select_b_req(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte manufacturer_req(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
static byte manufacturer_res(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);

static word get_plci(DIVA_CAPI_ADAPTER *);
static void add_p(PLCI *, byte, byte *);
static void add_s(PLCI *plci, byte code, API_PARSE *p);
static void add_ss(PLCI *plci, byte code, API_PARSE *p);
static void add_ie(PLCI *plci, byte code, byte *p, word p_length);
static void add_d(PLCI *, word, byte *);
static void add_ai(PLCI *, API_PARSE *);
static word add_b1(PLCI *, API_PARSE *, word, word);
static word add_b23(PLCI *, API_PARSE *);
static word add_modem_b23(PLCI *plci, API_PARSE *bp_parms);
static void sig_req(PLCI *, byte, byte);
static void nl_req_ncci(PLCI *, byte, byte);
static void send_req(PLCI *);
static void send_data(PLCI *);
static word plci_remove_check(PLCI *);
static void listen_check(DIVA_CAPI_ADAPTER *);
static byte AddInfo(byte **, byte **, byte *, byte *);
static byte getChannel(API_PARSE *);
static void IndParse(PLCI *, word *, byte **, byte);
static byte ie_compare(byte *, byte *);
static word find_cip(DIVA_CAPI_ADAPTER *, byte *, byte *);
static word CPN_filter_ok(byte *cpn, DIVA_CAPI_ADAPTER *, word);

/*
  XON protocol helpers
*/
static void channel_flow_control_remove(PLCI *plci);
static void channel_x_off(PLCI *plci, byte ch, byte flag);
static void channel_x_on(PLCI *plci, byte ch);
static void channel_request_xon(PLCI *plci, byte ch);
static void channel_xmit_xon(PLCI *plci);
static int channel_can_xon(PLCI *plci, byte ch);
static void channel_xmit_extended_xon(PLCI *plci);

static byte SendMultiIE(PLCI *plci, dword Id, byte **parms, byte ie_type, dword info_mask, byte setupParse);
static word AdvCodecSupport(DIVA_CAPI_ADAPTER *, PLCI *, APPL *, byte);
static void CodecIdCheck(DIVA_CAPI_ADAPTER *, PLCI *);
static void SetVoiceChannel(PLCI *, byte *, DIVA_CAPI_ADAPTER *);
static void VoiceChannelOff(PLCI *plci);
static void adv_voice_write_coefs(PLCI *plci, word write_command);
static void adv_voice_clear_config(PLCI *plci);

static word get_b1_facilities(PLCI *plci, byte b1_resource);
static byte add_b1_facilities(PLCI *plci, byte b1_resource, word b1_facilities);
static void adjust_b1_facilities(PLCI *plci, byte new_b1_resource, word new_b1_facilities);
static word adjust_b_process(dword Id, PLCI *plci, byte Rc);
static void adjust_b1_resource(dword Id, PLCI *plci, API_SAVE *bp_msg, word b1_facilities, word internal_command);
static void adjust_b_restore(dword Id, PLCI *plci, byte Rc);
static void reset_b3_command(dword Id, PLCI *plci, byte Rc);
static void select_b_command(dword Id, PLCI *plci, byte Rc);
static void fax_connect_ack_command(dword Id, PLCI *plci, byte Rc);
static void fax_edata_ack_command(dword Id, PLCI *plci, byte Rc);
static void fax_connect_info_command(dword Id, PLCI *plci, byte Rc);
static void fax_adjust_b23_command(dword Id, PLCI *plci, byte Rc);
static void fax_disconnect_command(dword Id, PLCI *plci, byte Rc);
static void hold_save_command(dword Id, PLCI *plci, byte Rc);
static void retrieve_restore_command(dword Id, PLCI *plci, byte Rc);
static void init_b1_config(PLCI *plci);
static void clear_b1_config(PLCI *plci);

static void dtmf_command(dword Id, PLCI *plci, byte Rc);
static byte dtmf_request(dword Id, word Number, DIVA_CAPI_ADAPTER *a, PLCI *plci, APPL *appl, API_PARSE *msg);
static void dtmf_confirmation(dword Id, PLCI *plci);
static void dtmf_indication(dword Id, PLCI *plci, byte *msg, word length);
static void dtmf_parameter_write(PLCI *plci);


static void mixer_set_bchannel_id_esc(PLCI *plci, byte bchannel_id);
static void mixer_set_bchannel_id(PLCI *plci, byte *chi);
static void mixer_clear_config(PLCI *plci);
static void mixer_notify_update(PLCI *plci, byte others);
static void mixer_command(dword Id, PLCI *plci, byte Rc);
static byte mixer_request(dword Id, word Number, DIVA_CAPI_ADAPTER *a, PLCI *plci, APPL *appl, API_PARSE *msg);
static void mixer_indication_coefs_set(dword Id, PLCI *plci);
static void mixer_indication_xconnect_from(dword Id, PLCI *plci, byte *msg, word length);
static void mixer_indication_xconnect_to(dword Id, PLCI *plci, byte *msg, word length);
static void mixer_remove(PLCI *plci);


static void ec_command(dword Id, PLCI *plci, byte Rc);
static byte ec_request(dword Id, word Number, DIVA_CAPI_ADAPTER *a, PLCI *plci, APPL *appl, API_PARSE *msg);
static void ec_indication(dword Id, PLCI *plci, byte *msg, word length);


static void rtp_connect_b3_req_command(dword Id, PLCI *plci, byte Rc);
static void rtp_connect_b3_res_command(dword Id, PLCI *plci, byte Rc);


static int diva_get_dma_descriptor(PLCI *plci, dword *dma_magic);
static void diva_free_dma_descriptor(PLCI *plci, int nr);

/*------------------------------------------------------------------*/
/* external function prototypes                                     */
/*------------------------------------------------------------------*/

extern byte MapController(byte);
extern byte UnMapController(byte);
#define MapId(Id)(((Id) & 0xffffff00L) | MapController((byte)(Id)))
#define UnMapId(Id)(((Id) & 0xffffff00L) | UnMapController((byte)(Id)))

void sendf(APPL *, word, dword, word, byte *, ...);
void *TransmitBufferSet(APPL *appl, dword ref);
void *TransmitBufferGet(APPL *appl, void *p);
void TransmitBufferFree(APPL *appl, void *p);
void *ReceiveBufferGet(APPL *appl, int Num);

int fax_head_line_time(char *buffer);


/*------------------------------------------------------------------*/
/* Global data definitions                                          */
/*------------------------------------------------------------------*/
extern byte max_adapter;
extern byte max_appl;
extern DIVA_CAPI_ADAPTER *adapter;
extern APPL *application;







static byte remove_started = false;
static PLCI dummy_plci;


static struct _ftable {
	word command;
	byte *format;
	byte (*function)(dword, word, DIVA_CAPI_ADAPTER *, PLCI *, APPL *, API_PARSE *);
} ftable[] = {
	{_DATA_B3_R,                          "dwww",         data_b3_req},
	{_DATA_B3_I | RESPONSE,               "w",            data_b3_res},
	{_INFO_R,                             "ss",           info_req},
	{_INFO_I | RESPONSE,                  "",             info_res},
	{_CONNECT_R,                          "wsssssssss",   connect_req},
	{_CONNECT_I | RESPONSE,               "wsssss",       connect_res},
	{_CONNECT_ACTIVE_I | RESPONSE,        "",             connect_a_res},
	{_DISCONNECT_R,                       "s",            disconnect_req},
	{_DISCONNECT_I | RESPONSE,            "",             disconnect_res},
	{_LISTEN_R,                           "dddss",        listen_req},
	{_ALERT_R,                            "s",            alert_req},
	{_FACILITY_R,                         "ws",           facility_req},
	{_FACILITY_I | RESPONSE,              "ws",           facility_res},
	{_CONNECT_B3_R,                       "s",            connect_b3_req},
	{_CONNECT_B3_I | RESPONSE,            "ws",           connect_b3_res},
	{_CONNECT_B3_ACTIVE_I | RESPONSE,     "",             connect_b3_a_res},
	{_DISCONNECT_B3_R,                    "s",            disconnect_b3_req},
	{_DISCONNECT_B3_I | RESPONSE,         "",             disconnect_b3_res},
	{_RESET_B3_R,                         "s",            reset_b3_req},
	{_RESET_B3_I | RESPONSE,              "",             reset_b3_res},
	{_CONNECT_B3_T90_ACTIVE_I | RESPONSE, "ws",           connect_b3_t90_a_res},
	{_CONNECT_B3_T90_ACTIVE_I | RESPONSE, "",             connect_b3_t90_a_res},
	{_SELECT_B_REQ,                       "s",            select_b_req},
	{_MANUFACTURER_R,                     "dws",          manufacturer_req},
	{_MANUFACTURER_I | RESPONSE,          "dws",          manufacturer_res},
	{_MANUFACTURER_I | RESPONSE,          "",             manufacturer_res}
};

static byte *cip_bc[29][2] = {
	{ "",                     ""                     }, /* 0 */
	{ "\x03\x80\x90\xa3",     "\x03\x80\x90\xa2"     }, /* 1 */
	{ "\x02\x88\x90",         "\x02\x88\x90"         }, /* 2 */
	{ "\x02\x89\x90",         "\x02\x89\x90"         }, /* 3 */
	{ "\x03\x90\x90\xa3",     "\x03\x90\x90\xa2"     }, /* 4 */
	{ "\x03\x91\x90\xa5",     "\x03\x91\x90\xa5"     }, /* 5 */
	{ "\x02\x98\x90",         "\x02\x98\x90"         }, /* 6 */
	{ "\x04\x88\xc0\xc6\xe6", "\x04\x88\xc0\xc6\xe6" }, /* 7 */
	{ "\x04\x88\x90\x21\x8f", "\x04\x88\x90\x21\x8f" }, /* 8 */
	{ "\x03\x91\x90\xa5",     "\x03\x91\x90\xa5"     }, /* 9 */
	{ "",                     ""                     }, /* 10 */
	{ "",                     ""                     }, /* 11 */
	{ "",                     ""                     }, /* 12 */
	{ "",                     ""                     }, /* 13 */
	{ "",                     ""                     }, /* 14 */
	{ "",                     ""                     }, /* 15 */

	{ "\x03\x80\x90\xa3",     "\x03\x80\x90\xa2"     }, /* 16 */
	{ "\x03\x90\x90\xa3",     "\x03\x90\x90\xa2"     }, /* 17 */
	{ "\x02\x88\x90",         "\x02\x88\x90"         }, /* 18 */
	{ "\x02\x88\x90",         "\x02\x88\x90"         }, /* 19 */
	{ "\x02\x88\x90",         "\x02\x88\x90"         }, /* 20 */
	{ "\x02\x88\x90",         "\x02\x88\x90"         }, /* 21 */
	{ "\x02\x88\x90",         "\x02\x88\x90"         }, /* 22 */
	{ "\x02\x88\x90",         "\x02\x88\x90"         }, /* 23 */
	{ "\x02\x88\x90",         "\x02\x88\x90"         }, /* 24 */
	{ "\x02\x88\x90",         "\x02\x88\x90"         }, /* 25 */
	{ "\x03\x91\x90\xa5",     "\x03\x91\x90\xa5"     }, /* 26 */
	{ "\x03\x91\x90\xa5",     "\x03\x91\x90\xa5"     }, /* 27 */
	{ "\x02\x88\x90",         "\x02\x88\x90"         }  /* 28 */
};

static byte *cip_hlc[29] = {
	"",                           /* 0 */
	"",                           /* 1 */
	"",                           /* 2 */
	"",                           /* 3 */
	"",                           /* 4 */
	"",                           /* 5 */
	"",                           /* 6 */
	"",                           /* 7 */
	"",                           /* 8 */
	"",                           /* 9 */
	"",                           /* 10 */
	"",                           /* 11 */
	"",                           /* 12 */
	"",                           /* 13 */
	"",                           /* 14 */
	"",                           /* 15 */

	"\x02\x91\x81",               /* 16 */
	"\x02\x91\x84",               /* 17 */
	"\x02\x91\xa1",               /* 18 */
	"\x02\x91\xa4",               /* 19 */
	"\x02\x91\xa8",               /* 20 */
	"\x02\x91\xb1",               /* 21 */
	"\x02\x91\xb2",               /* 22 */
	"\x02\x91\xb5",               /* 23 */
	"\x02\x91\xb8",               /* 24 */
	"\x02\x91\xc1",               /* 25 */
	"\x02\x91\x81",               /* 26 */
	"\x03\x91\xe0\x01",           /* 27 */
	"\x03\x91\xe0\x02"            /* 28 */
};

/*------------------------------------------------------------------*/

#define V120_HEADER_LENGTH 1
#define V120_HEADER_EXTEND_BIT  0x80
#define V120_HEADER_BREAK_BIT   0x40
#define V120_HEADER_C1_BIT      0x04
#define V120_HEADER_C2_BIT      0x08
#define V120_HEADER_FLUSH_COND  (V120_HEADER_BREAK_BIT | V120_HEADER_C1_BIT | V120_HEADER_C2_BIT)

static byte v120_default_header[] =
{

	0x83                          /*  Ext, BR , res, res, C2 , C1 , B  , F   */

};

static byte v120_break_header[] =
{

	0xc3 | V120_HEADER_BREAK_BIT  /*  Ext, BR , res, res, C2 , C1 , B  , F   */

};


/*------------------------------------------------------------------*/
/* API_PUT function                                                 */
/*------------------------------------------------------------------*/

word api_put(APPL *appl, CAPI_MSG *msg)
{
	word i, j, k, l, n;
	word ret;
	byte c;
	byte controller;
	DIVA_CAPI_ADAPTER *a;
	PLCI *plci;
	NCCI *ncci_ptr;
	word ncci;
	CAPI_MSG *m;
	API_PARSE msg_parms[MAX_MSG_PARMS + 1];

	if (msg->header.length < sizeof(msg->header) ||
	    msg->header.length > MAX_MSG_SIZE) {
		dbug(1, dprintf("bad len"));
		return _BAD_MSG;
	}

	controller = (byte)((msg->header.controller & 0x7f) - 1);

	/* controller starts with 0 up to (max_adapter - 1) */
	if (controller >= max_adapter)
	{
		dbug(1, dprintf("invalid ctrl"));
		return _BAD_MSG;
	}

	a = &adapter[controller];
	plci = NULL;
	if ((msg->header.plci != 0) && (msg->header.plci <= a->max_plci) && !a->adapter_disabled)
	{
		dbug(1, dprintf("plci=%x", msg->header.plci));
		plci = &a->plci[msg->header.plci - 1];
		ncci = GET_WORD(&msg->header.ncci);
		if (plci->Id
		    && (plci->appl
			|| (plci->State == INC_CON_PENDING)
			|| (plci->State == INC_CON_ALERT)
			|| (msg->header.command == (_DISCONNECT_I | RESPONSE)))
		    && ((ncci == 0)
			|| (msg->header.command == (_DISCONNECT_B3_I | RESPONSE))
			|| ((ncci < MAX_NCCI + 1) && (a->ncci_plci[ncci] == plci->Id))))
		{
			i = plci->msg_in_read_pos;
			j = plci->msg_in_write_pos;
			if (j >= i)
			{
				if (j + msg->header.length + MSG_IN_OVERHEAD <= MSG_IN_QUEUE_SIZE)
					i += MSG_IN_QUEUE_SIZE - j;
				else
					j = 0;
			}
			else
			{

				n = (((CAPI_MSG *)(plci->msg_in_queue))->header.length + MSG_IN_OVERHEAD + 3) & 0xfffc;

				if (i > MSG_IN_QUEUE_SIZE - n)
					i = MSG_IN_QUEUE_SIZE - n + 1;
				i -= j;
			}

			if (i <= ((msg->header.length + MSG_IN_OVERHEAD + 3) & 0xfffc))

			{
				dbug(0, dprintf("Q-FULL1(msg) - len=%d write=%d read=%d wrap=%d free=%d",
						msg->header.length, plci->msg_in_write_pos,
						plci->msg_in_read_pos, plci->msg_in_wrap_pos, i));

				return _QUEUE_FULL;
			}
			c = false;
			if ((((byte *) msg) < ((byte *)(plci->msg_in_queue)))
			    || (((byte *) msg) >= ((byte *)(plci->msg_in_queue)) + sizeof(plci->msg_in_queue)))
			{
				if (plci->msg_in_write_pos != plci->msg_in_read_pos)
					c = true;
			}
			if (msg->header.command == _DATA_B3_R)
			{
				if (msg->header.length < 20)
				{
					dbug(1, dprintf("DATA_B3 REQ wrong length %d", msg->header.length));
					return _BAD_MSG;
				}
				ncci_ptr = &(a->ncci[ncci]);
				n = ncci_ptr->data_pending;
				l = ncci_ptr->data_ack_pending;
				k = plci->msg_in_read_pos;
				while (k != plci->msg_in_write_pos)
				{
					if (k == plci->msg_in_wrap_pos)
						k = 0;
					if ((((CAPI_MSG *)(&((byte *)(plci->msg_in_queue))[k]))->header.command == _DATA_B3_R)
					    && (((CAPI_MSG *)(&((byte *)(plci->msg_in_queue))[k]))->header.ncci == ncci))
					{
						n++;
						if (((CAPI_MSG *)(&((byte *)(plci->msg_in_queue))[k]))->info.data_b3_req.Flags & 0x0004)
							l++;
					}

					k += (((CAPI_MSG *)(&((byte *)(plci->msg_in_queue))[k]))->header.length +
					      MSG_IN_OVERHEAD + 3) & 0xfffc;

				}
				if ((n >= MAX_DATA_B3) || (l >= MAX_DATA_ACK))
				{
					dbug(0, dprintf("Q-FULL2(data) - pending=%d/%d ack_pending=%d/%d",
							ncci_ptr->data_pending, n, ncci_ptr->data_ack_pending, l));

					return _QUEUE_FULL;
				}
				if (plci->req_in || plci->internal_command)
				{
					if ((((byte *) msg) >= ((byte *)(plci->msg_in_queue)))
					    && (((byte *) msg) < ((byte *)(plci->msg_in_queue)) + sizeof(plci->msg_in_queue)))
					{
						dbug(0, dprintf("Q-FULL3(requeue)"));

						return _QUEUE_FULL;
					}
					c = true;
				}
			}
			else
			{
				if (plci->req_in || plci->internal_command)
					c = true;
				else
				{
					plci->command = msg->header.command;
					plci->number = msg->header.number;
				}
			}
			if (c)
			{
				dbug(1, dprintf("enqueue msg(0x%04x,0x%x,0x%x) - len=%d write=%d read=%d wrap=%d free=%d",
						msg->header.command, plci->req_in, plci->internal_command,
						msg->header.length, plci->msg_in_write_pos,
						plci->msg_in_read_pos, plci->msg_in_wrap_pos, i));
				if (j == 0)
					plci->msg_in_wrap_pos = plci->msg_in_write_pos;
				m = (CAPI_MSG *)(&((byte *)(plci->msg_in_queue))[j]);
				for (i = 0; i < msg->header.length; i++)
					((byte *)(plci->msg_in_queue))[j++] = ((byte *) msg)[i];
				if (m->header.command == _DATA_B3_R)
				{

					m->info.data_b3_req.Data = (dword)(long)(TransmitBufferSet(appl, m->info.data_b3_req.Data));

				}

				j = (j + 3) & 0xfffc;

				*((APPL **)(&((byte *)(plci->msg_in_queue))[j])) = appl;
				plci->msg_in_write_pos = j + MSG_IN_OVERHEAD;
				return 0;
			}
		}
		else
		{
			plci = NULL;
		}
	}
	dbug(1, dprintf("com=%x", msg->header.command));

	for (j = 0; j < MAX_MSG_PARMS + 1; j++) msg_parms[j].length = 0;
	for (i = 0, ret = _BAD_MSG; i < ARRAY_SIZE(ftable); i++) {

		if (ftable[i].command == msg->header.command) {
			/* break loop if the message is correct, otherwise continue scan  */
			/* (for example: CONNECT_B3_T90_ACT_RES has two specifications)   */
			if (!api_parse(msg->info.b, (word)(msg->header.length - 12), ftable[i].format, msg_parms)) {
				ret = 0;
				break;
			}
			for (j = 0; j < MAX_MSG_PARMS + 1; j++) msg_parms[j].length = 0;
		}
	}
	if (ret) {
		dbug(1, dprintf("BAD_MSG"));
		if (plci) plci->command = 0;
		return ret;
	}


	c = ftable[i].function(GET_DWORD(&msg->header.controller),
			       msg->header.number,
			       a,
			       plci,
			       appl,
			       msg_parms);

	channel_xmit_extended_xon(plci);

	if (c == 1) send_req(plci);
	if (c == 2 && plci) plci->req_in = plci->req_in_start = plci->req_out = 0;
	if (plci && !plci->req_in) plci->command = 0;
	return 0;
}


/*------------------------------------------------------------------*/
/* api_parse function, check the format of api messages             */
/*------------------------------------------------------------------*/

static word api_parse(byte *msg, word length, byte *format, API_PARSE *parms)
{
	word i;
	word p;

	for (i = 0, p = 0; format[i]; i++) {
		if (parms)
		{
			parms[i].info = &msg[p];
		}
		switch (format[i]) {
		case 'b':
			p += 1;
			break;
		case 'w':
			p += 2;
			break;
		case 'd':
			p += 4;
			break;
		case 's':
			if (msg[p] == 0xff) {
				parms[i].info += 2;
				parms[i].length = msg[p + 1] + (msg[p + 2] << 8);
				p += (parms[i].length + 3);
			}
			else {
				parms[i].length = msg[p];
				p += (parms[i].length + 1);
			}
			break;
		}

		if (p > length) return true;
	}
	if (parms) parms[i].info = NULL;
	return false;
}

static void api_save_msg(API_PARSE *in, byte *format, API_SAVE *out)
{
	word i, j, n = 0;
	byte *p;

	p = out->info;
	for (i = 0; format[i] != '\0'; i++)
	{
		out->parms[i].info = p;
		out->parms[i].length = in[i].length;
		switch (format[i])
		{
		case 'b':
			n = 1;
			break;
		case 'w':
			n = 2;
			break;
		case 'd':
			n = 4;
			break;
		case 's':
			n = in[i].length + 1;
			break;
		}
		for (j = 0; j < n; j++)
			*(p++) = in[i].info[j];
	}
	out->parms[i].info = NULL;
	out->parms[i].length = 0;
}

static void api_load_msg(API_SAVE *in, API_PARSE *out)
{
	word i;

	i = 0;
	do
	{
		out[i].info = in->parms[i].info;
		out[i].length = in->parms[i].length;
	} while (in->parms[i++].info);
}


/*------------------------------------------------------------------*/
/* CAPI remove function                                             */
/*------------------------------------------------------------------*/

word api_remove_start(void)
{
	word i;
	word j;

	if (!remove_started) {
		remove_started = true;
		for (i = 0; i < max_adapter; i++) {
			if (adapter[i].request) {
				for (j = 0; j < adapter[i].max_plci; j++) {
					if (adapter[i].plci[j].Sig.Id) plci_remove(&adapter[i].plci[j]);
				}
			}
		}
		return 1;
	}
	else {
		for (i = 0; i < max_adapter; i++) {
			if (adapter[i].request) {
				for (j = 0; j < adapter[i].max_plci; j++) {
					if (adapter[i].plci[j].Sig.Id) return 1;
				}
			}
		}
	}
	api_remove_complete();
	return 0;
}


/*------------------------------------------------------------------*/
/* internal command queue                                           */
/*------------------------------------------------------------------*/

static void init_internal_command_queue(PLCI *plci)
{
	word i;

	dbug(1, dprintf("%s,%d: init_internal_command_queue",
			(char *)(FILE_), __LINE__));

	plci->internal_command = 0;
	for (i = 0; i < MAX_INTERNAL_COMMAND_LEVELS; i++)
		plci->internal_command_queue[i] = NULL;
}


static void start_internal_command(dword Id, PLCI *plci, t_std_internal_command command_function)
{
	word i;

	dbug(1, dprintf("[%06lx] %s,%d: start_internal_command",
			UnMapId(Id), (char *)(FILE_), __LINE__));

	if (plci->internal_command == 0)
	{
		plci->internal_command_queue[0] = command_function;
		(*command_function)(Id, plci, OK);
	}
	else
	{
		i = 1;
		while (plci->internal_command_queue[i] != NULL)
			i++;
		plci->internal_command_queue[i] = command_function;
	}
}


static void next_internal_command(dword Id, PLCI *plci)
{
	word i;

	dbug(1, dprintf("[%06lx] %s,%d: next_internal_command",
			UnMapId(Id), (char *)(FILE_), __LINE__));

	plci->internal_command = 0;
	plci->internal_command_queue[0] = NULL;
	while (plci->internal_command_queue[1] != NULL)
	{
		for (i = 0; i < MAX_INTERNAL_COMMAND_LEVELS - 1; i++)
			plci->internal_command_queue[i] = plci->internal_command_queue[i + 1];
		plci->internal_command_queue[MAX_INTERNAL_COMMAND_LEVELS - 1] = NULL;
		(*(plci->internal_command_queue[0]))(Id, plci, OK);
		if (plci->internal_command != 0)
			return;
		plci->internal_command_queue[0] = NULL;
	}
}


/*------------------------------------------------------------------*/
/* NCCI allocate/remove function                                    */
/*------------------------------------------------------------------*/

static dword ncci_mapping_bug = 0;

static word get_ncci(PLCI *plci, byte ch, word force_ncci)
{
	DIVA_CAPI_ADAPTER *a;
	word ncci, i, j, k;

	a = plci->adapter;
	if (!ch || a->ch_ncci[ch])
	{
		ncci_mapping_bug++;
		dbug(1, dprintf("NCCI mapping exists %ld %02x %02x %02x-%02x",
				ncci_mapping_bug, ch, force_ncci, a->ncci_ch[a->ch_ncci[ch]], a->ch_ncci[ch]));
		ncci = ch;
	}
	else
	{
		if (force_ncci)
			ncci = force_ncci;
		else
		{
			if ((ch < MAX_NCCI + 1) && !a->ncci_ch[ch])
				ncci = ch;
			else
			{
				ncci = 1;
				while ((ncci < MAX_NCCI + 1) && a->ncci_ch[ncci])
					ncci++;
				if (ncci == MAX_NCCI + 1)
				{
					ncci_mapping_bug++;
					i = 1;
					do
					{
						j = 1;
						while ((j < MAX_NCCI + 1) && (a->ncci_ch[j] != i))
							j++;
						k = j;
						if (j < MAX_NCCI + 1)
						{
							do
							{
								j++;
							} while ((j < MAX_NCCI + 1) && (a->ncci_ch[j] != i));
						}
					} while ((i < MAX_NL_CHANNEL + 1) && (j < MAX_NCCI + 1));
					if (i < MAX_NL_CHANNEL + 1)
					{
						dbug(1, dprintf("NCCI mapping overflow %ld %02x %02x %02x-%02x-%02x",
								ncci_mapping_bug, ch, force_ncci, i, k, j));
					}
					else
					{
						dbug(1, dprintf("NCCI mapping overflow %ld %02x %02x",
								ncci_mapping_bug, ch, force_ncci));
					}
					ncci = ch;
				}
			}
			a->ncci_plci[ncci] = plci->Id;
			a->ncci_state[ncci] = IDLE;
			if (!plci->ncci_ring_list)
				plci->ncci_ring_list = ncci;
			else
				a->ncci_next[ncci] = a->ncci_next[plci->ncci_ring_list];
			a->ncci_next[plci->ncci_ring_list] = (byte) ncci;
		}
		a->ncci_ch[ncci] = ch;
		a->ch_ncci[ch] = (byte) ncci;
		dbug(1, dprintf("NCCI mapping established %ld %02x %02x %02x-%02x",
				ncci_mapping_bug, ch, force_ncci, ch, ncci));
	}
	return (ncci);
}


static void ncci_free_receive_buffers(PLCI *plci, word ncci)
{
	DIVA_CAPI_ADAPTER *a;
	APPL *appl;
	word i, ncci_code;
	dword Id;

	a = plci->adapter;
	Id = (((dword) ncci) << 16) | (((word)(plci->Id)) << 8) | a->Id;
	if (ncci)
	{
		if (a->ncci_plci[ncci] == plci->Id)
		{
			if (!plci->appl)
			{
				ncci_mapping_bug++;
				dbug(1, dprintf("NCCI mapping appl expected %ld %08lx",
						ncci_mapping_bug, Id));
			}
			else
			{
				appl = plci->appl;
				ncci_code = ncci | (((word) a->Id) << 8);
				for (i = 0; i < appl->MaxBuffer; i++)
				{
					if ((appl->DataNCCI[i] == ncci_code)
					    && (((byte)(appl->DataFlags[i] >> 8)) == plci->Id))
					{
						appl->DataNCCI[i] = 0;
					}
				}
			}
		}
	}
	else
	{
		for (ncci = 1; ncci < MAX_NCCI + 1; ncci++)
		{
			if (a->ncci_plci[ncci] == plci->Id)
			{
				if (!plci->appl)
				{
					ncci_mapping_bug++;
					dbug(1, dprintf("NCCI mapping no appl %ld %08lx",
							ncci_mapping_bug, Id));
				}
				else
				{
					appl = plci->appl;
					ncci_code = ncci | (((word) a->Id) << 8);
					for (i = 0; i < appl->MaxBuffer; i++)
					{
						if ((appl->DataNCCI[i] == ncci_code)
						    && (((byte)(appl->DataFlags[i] >> 8)) == plci->Id))
						{
							appl->DataNCCI[i] = 0;
						}
					}
				}
			}
		}
	}
}


static void cleanup_ncci_data(PLCI *plci, word ncci)
{
	NCCI *ncci_ptr;

	if (ncci && (plci->adapter->ncci_plci[ncci] == plci->Id))
	{
		ncci_ptr = &(plci->adapter->ncci[ncci]);
		if (plci->appl)
		{
			while (ncci_ptr->data_pending != 0)
			{
				if (!plci->data_sent || (ncci_ptr->DBuffer[ncci_ptr->data_out].P != plci->data_sent_ptr))
					TransmitBufferFree(plci->appl, ncci_ptr->DBuffer[ncci_ptr->data_out].P);
				(ncci_ptr->data_out)++;
				if (ncci_ptr->data_out == MAX_DATA_B3)
					ncci_ptr->data_out = 0;
				(ncci_ptr->data_pending)--;
			}
		}
		ncci_ptr->data_out = 0;
		ncci_ptr->data_pending = 0;
		ncci_ptr->data_ack_out = 0;
		ncci_ptr->data_ack_pending = 0;
	}
}


static void ncci_remove(PLCI *plci, word ncci, byte preserve_ncci)
{
	DIVA_CAPI_ADAPTER *a;
	dword Id;
	word i;

	a = plci->adapter;
	Id = (((dword) ncci) << 16) | (((word)(plci->Id)) << 8) | a->Id;
	if (!preserve_ncci)
		ncci_free_receive_buffers(plci, ncci);
	if (ncci)
	{
		if (a->ncci_plci[ncci] != plci->Id)
		{
			ncci_mapping_bug++;
			dbug(1, dprintf("NCCI mapping doesn't exist %ld %08lx %02x",
					ncci_mapping_bug, Id, preserve_ncci));
		}
		else
		{
			cleanup_ncci_data(plci, ncci);
			dbug(1, dprintf("NCCI mapping released %ld %08lx %02x %02x-%02x",
					ncci_mapping_bug, Id, preserve_ncci, a->ncci_ch[ncci], ncci));
			a->ch_ncci[a->ncci_ch[ncci]] = 0;
			if (!preserve_ncci)
			{
				a->ncci_ch[ncci] = 0;
				a->ncci_plci[ncci] = 0;
				a->ncci_state[ncci] = IDLE;
				i = plci->ncci_ring_list;
				while ((i != 0) && (a->ncci_next[i] != plci->ncci_ring_list) && (a->ncci_next[i] != ncci))
					i = a->ncci_next[i];
				if ((i != 0) && (a->ncci_next[i] == ncci))
				{
					if (i == ncci)
						plci->ncci_ring_list = 0;
					else if (plci->ncci_ring_list == ncci)
						plci->ncci_ring_list = i;
					a->ncci_next[i] = a->ncci_next[ncci];
				}
				a->ncci_next[ncci] = 0;
			}
		}
	}
	else
	{
		for (ncci = 1; ncci < MAX_NCCI + 1; ncci++)
		{
			if (a->ncci_plci[ncci] == plci->Id)
			{
				cleanup_ncci_data(plci, ncci);
				dbug(1, dprintf("NCCI mapping released %ld %08lx %02x %02x-%02x",
						ncci_mapping_bug, Id, preserve_ncci, a->ncci_ch[ncci], ncci));
				a->ch_ncci[a->ncci_ch[ncci]] = 0;
				if (!preserve_ncci)
				{
					a->ncci_ch[ncci] = 0;
					a->ncci_plci[ncci] = 0;
					a->ncci_state[ncci] = IDLE;
					a->ncci_next[ncci] = 0;
				}
			}
		}
		if (!preserve_ncci)
			plci->ncci_ring_list = 0;
	}
}


/*------------------------------------------------------------------*/
/* PLCI remove function                                             */
/*------------------------------------------------------------------*/

static void plci_free_msg_in_queue(PLCI *plci)
{
	word i;

	if (plci->appl)
	{
		i = plci->msg_in_read_pos;
		while (i != plci->msg_in_write_pos)
		{
			if (i == plci->msg_in_wrap_pos)
				i = 0;
			if (((CAPI_MSG *)(&((byte *)(plci->msg_in_queue))[i]))->header.command == _DATA_B3_R)
			{

				TransmitBufferFree(plci->appl,
						   (byte *)(long)(((CAPI_MSG *)(&((byte *)(plci->msg_in_queue))[i]))->info.data_b3_req.Data));

			}

			i += (((CAPI_MSG *)(&((byte *)(plci->msg_in_queue))[i]))->header.length +
			      MSG_IN_OVERHEAD + 3) & 0xfffc;

		}
	}
	plci->msg_in_write_pos = MSG_IN_QUEUE_SIZE;
	plci->msg_in_read_pos = MSG_IN_QUEUE_SIZE;
	plci->msg_in_wrap_pos = MSG_IN_QUEUE_SIZE;
}


static void plci_remove(PLCI *plci)
{

	if (!plci) {
		dbug(1, dprintf("plci_remove(no plci)"));
		return;
	}
	init_internal_command_queue(plci);
	dbug(1, dprintf("plci_remove(%x,tel=%x)", plci->Id, plci->tel));
	if (plci_remove_check(plci))
	{
		return;
	}
	if (plci->Sig.Id == 0xff)
	{
		dbug(1, dprintf("D-channel X.25 plci->NL.Id:%0x", plci->NL.Id));
		if (plci->NL.Id && !plci->nl_remove_id)
		{
			nl_req_ncci(plci, REMOVE, 0);
			send_req(plci);
		}
	}
	else
	{
		if (!plci->sig_remove_id
		    && (plci->Sig.Id
			|| (plci->req_in != plci->req_out)
			|| (plci->nl_req || plci->sig_req)))
		{
			sig_req(plci, HANGUP, 0);
			send_req(plci);
		}
	}
	ncci_remove(plci, 0, false);
	plci_free_msg_in_queue(plci);

	plci->channels = 0;
	plci->appl = NULL;
	if ((plci->State == INC_CON_PENDING) || (plci->State == INC_CON_ALERT))
		plci->State = OUTG_DIS_PENDING;
}

/*------------------------------------------------------------------*/
/* Application Group function helpers                               */
/*------------------------------------------------------------------*/

static void set_group_ind_mask(PLCI *plci)
{
	word i;

	for (i = 0; i < C_IND_MASK_DWORDS; i++)
		plci->group_optimization_mask_table[i] = 0xffffffffL;
}

static void clear_group_ind_mask_bit(PLCI *plci, word b)
{
	plci->group_optimization_mask_table[b >> 5] &= ~(1L << (b & 0x1f));
}

static byte test_group_ind_mask_bit(PLCI *plci, word b)
{
	return ((plci->group_optimization_mask_table[b >> 5] & (1L << (b & 0x1f))) != 0);
}

/*------------------------------------------------------------------*/
/* c_ind_mask operations for arbitrary MAX_APPL                     */
/*------------------------------------------------------------------*/

static void clear_c_ind_mask(PLCI *plci)
{
	word i;

	for (i = 0; i < C_IND_MASK_DWORDS; i++)
		plci->c_ind_mask_table[i] = 0;
}

static byte c_ind_mask_empty(PLCI *plci)
{
	word i;

	i = 0;
	while ((i < C_IND_MASK_DWORDS) && (plci->c_ind_mask_table[i] == 0))
		i++;
	return (i == C_IND_MASK_DWORDS);
}

static void set_c_ind_mask_bit(PLCI *plci, word b)
{
	plci->c_ind_mask_table[b >> 5] |= (1L << (b & 0x1f));
}

static void clear_c_ind_mask_bit(PLCI *plci, word b)
{
	plci->c_ind_mask_table[b >> 5] &= ~(1L << (b & 0x1f));
}

static byte test_c_ind_mask_bit(PLCI *plci, word b)
{
	return ((plci->c_ind_mask_table[b >> 5] & (1L << (b & 0x1f))) != 0);
}

static void dump_c_ind_mask(PLCI *plci)
{
	static char hex_digit_table[0x10] =
		{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
	word i, j, k;
	dword d;
	char *p;
	char buf[40];

	for (i = 0; i < C_IND_MASK_DWORDS; i += 4)
	{
		p = buf + 36;
		*p = '\0';
		for (j = 0; j < 4; j++)
		{
			if (i + j < C_IND_MASK_DWORDS)
			{
				d = plci->c_ind_mask_table[i + j];
				for (k = 0; k < 8; k++)
				{
					*(--p) = hex_digit_table[d & 0xf];
					d >>= 4;
				}
			}
			else if (i != 0)
			{
				for (k = 0; k < 8; k++)
					*(--p) = ' ';
			}
			*(--p) = ' ';
		}
		dbug(1, dprintf("c_ind_mask =%s", (char *) p));
	}
}





#define dump_plcis(a)



/*------------------------------------------------------------------*/
/* translation function for each message                            */
/*------------------------------------------------------------------*/

static byte connect_req(dword Id, word Number, DIVA_CAPI_ADAPTER *a,
			PLCI *plci, APPL *appl, API_PARSE *parms)
{
	word ch;
	word i;
	word Info;
	byte LinkLayer;
	API_PARSE *ai;
	API_PARSE *bp;
	API_PARSE ai_parms[5];
	word channel = 0;
	dword ch_mask;
	byte m;
	static byte esc_chi[35] = {0x02, 0x18, 0x01};
	static byte lli[2] = {0x01, 0x00};
	byte noCh = 0;
	word dir = 0;
	byte *p_chi = "";

	for (i = 0; i < 5; i++) ai_parms[i].length = 0;

	dbug(1, dprintf("connect_req(%d)", parms->length));
	Info = _WRONG_IDENTIFIER;
	if (a)
	{
		if (a->adapter_disabled)
		{
			dbug(1, dprintf("adapter disabled"));
			Id = ((word)1 << 8) | a->Id;
			sendf(appl, _CONNECT_R | CONFIRM, Id, Number, "w", 0);
			sendf(appl, _DISCONNECT_I, Id, 0, "w", _L1_ERROR);
			return false;
		}
		Info = _OUT_OF_PLCI;
		if ((i = get_plci(a)))
		{
			Info = 0;
			plci = &a->plci[i - 1];
			plci->appl = appl;
			plci->call_dir = CALL_DIR_OUT | CALL_DIR_ORIGINATE;
			/* check 'external controller' bit for codec support */
			if (Id & EXT_CONTROLLER)
			{
				if (AdvCodecSupport(a, plci, appl, 0))
				{
					plci->Id = 0;
					sendf(appl, _CONNECT_R | CONFIRM, Id, Number, "w", _WRONG_IDENTIFIER);
					return 2;
				}
			}
			ai = &parms[9];
			bp = &parms[5];
			ch = 0;
			if (bp->length)LinkLayer = bp->info[3];
			else LinkLayer = 0;
			if (ai->length)
			{
				ch = 0xffff;
				if (!api_parse(&ai->info[1], (word)ai->length, "ssss", ai_parms))
				{
					ch = 0;
					if (ai_parms[0].length)
					{
						ch = GET_WORD(ai_parms[0].info + 1);
						if (ch > 4) ch = 0; /* safety -> ignore ChannelID */
						if (ch == 4) /* explizit CHI in message */
						{
							/* check length of B-CH struct */
							if ((ai_parms[0].info)[3] >= 1)
							{
								if ((ai_parms[0].info)[4] == CHI)
								{
									p_chi = &((ai_parms[0].info)[5]);
								}
								else
								{
									p_chi = &((ai_parms[0].info)[3]);
								}
								if (p_chi[0] > 35) /* check length of channel ID */
								{
									Info = _WRONG_MESSAGE_FORMAT;
								}
							}
							else Info = _WRONG_MESSAGE_FORMAT;
						}

						if (ch == 3 && ai_parms[0].length >= 7 && ai_parms[0].length <= 36)
						{
							dir = GET_WORD(ai_parms[0].info + 3);
							ch_mask = 0;
							m = 0x3f;
							for (i = 0; i + 5 <= ai_parms[0].length; i++)
							{
								if (ai_parms[0].info[i + 5] != 0)
								{
									if ((ai_parms[0].info[i + 5] | m) != 0xff)
										Info = _WRONG_MESSAGE_FORMAT;
									else
									{
										if (ch_mask == 0)
											channel = i;
										ch_mask |= 1L << i;
									}
								}
								m = 0;
							}
							if (ch_mask == 0)
								Info = _WRONG_MESSAGE_FORMAT;
							if (!Info)
							{
								if ((ai_parms[0].length == 36) || (ch_mask != ((dword)(1L << channel))))
								{
									esc_chi[0] = (byte)(ai_parms[0].length - 2);
									for (i = 0; i + 5 <= ai_parms[0].length; i++)
										esc_chi[i + 3] = ai_parms[0].info[i + 5];
								}
								else
									esc_chi[0] = 2;
								esc_chi[2] = (byte)channel;
								plci->b_channel = (byte)channel; /* not correct for ETSI ch 17..31 */
								add_p(plci, LLI, lli);
								add_p(plci, ESC, esc_chi);
								plci->State = LOCAL_CONNECT;
								if (!dir) plci->call_dir |= CALL_DIR_FORCE_OUTG_NL;     /* dir 0=DTE, 1=DCE */
							}
						}
					}
				}
				else  Info = _WRONG_MESSAGE_FORMAT;
			}

			dbug(1, dprintf("ch=%x,dir=%x,p_ch=%d", ch, dir, channel));
			plci->command = _CONNECT_R;
			plci->number = Number;
			/* x.31 or D-ch free SAPI in LinkLayer? */
			if (ch == 1 && LinkLayer != 3 && LinkLayer != 12) noCh = true;
			if ((ch == 0 || ch == 2 || noCh || ch == 3 || ch == 4) && !Info)
			{
				/* B-channel used for B3 connections (ch==0), or no B channel    */
				/* is used (ch==2) or perm. connection (3) is used  do a CALL    */
				if (noCh) Info = add_b1(plci, &parms[5], 2, 0);    /* no resource    */
				else     Info = add_b1(plci, &parms[5], ch, 0);
				add_s(plci, OAD, &parms[2]);
				add_s(plci, OSA, &parms[4]);
				add_s(plci, BC, &parms[6]);
				add_s(plci, LLC, &parms[7]);
				add_s(plci, HLC, &parms[8]);
				if (a->Info_Mask[appl->Id - 1] & 0x200)
				{
					/* early B3 connect (CIP mask bit 9) no release after a disc */
					add_p(plci, LLI, "\x01\x01");
				}
				if (GET_WORD(parms[0].info) < 29) {
					add_p(plci, BC, cip_bc[GET_WORD(parms[0].info)][a->u_law]);
					add_p(plci, HLC, cip_hlc[GET_WORD(parms[0].info)]);
				}
				add_p(plci, UID, "\x06\x43\x61\x70\x69\x32\x30");
				sig_req(plci, ASSIGN, DSIG_ID);
			}
			else if (ch == 1) {

				/* D-Channel used for B3 connections */
				plci->Sig.Id = 0xff;
				Info = 0;
			}

			if (!Info && ch != 2 && !noCh) {
				Info = add_b23(plci, &parms[5]);
				if (!Info) {
					if (!(plci->tel && !plci->adv_nl))nl_req_ncci(plci, ASSIGN, 0);
				}
			}

			if (!Info)
			{
				if (ch == 0 || ch == 2 || ch == 3 || noCh || ch == 4)
				{
					if (plci->spoofed_msg == SPOOFING_REQUIRED)
					{
						api_save_msg(parms, "wsssssssss", &plci->saved_msg);
						plci->spoofed_msg = CALL_REQ;
						plci->internal_command = BLOCK_PLCI;
						plci->command = 0;
						dbug(1, dprintf("Spoof"));
						send_req(plci);
						return false;
					}
					if (ch == 4)add_p(plci, CHI, p_chi);
					add_s(plci, CPN, &parms[1]);
					add_s(plci, DSA, &parms[3]);
					if (noCh) add_p(plci, ESC, "\x02\x18\xfd");  /* D-channel, no B-L3 */
					add_ai(plci, &parms[9]);
					if (!dir)sig_req(plci, CALL_REQ, 0);
					else
					{
						plci->command = PERM_LIST_REQ;
						plci->appl = appl;
						sig_req(plci, LISTEN_REQ, 0);
						send_req(plci);
						return false;
					}
				}
				send_req(plci);
				return false;
			}
			plci->Id = 0;
		}
	}
	sendf(appl,
	      _CONNECT_R | CONFIRM,
	      Id,
	      Number,
	      "w", Info);
	return 2;
}

static byte connect_res(dword Id, word Number, DIVA_CAPI_ADAPTER *a,
			PLCI *plci, APPL *appl, API_PARSE *parms)
{
	word i, Info;
	word Reject;
	static byte cau_t[] = {0, 0, 0x90, 0x91, 0xac, 0x9d, 0x86, 0xd8, 0x9b};
	static byte esc_t[] = {0x03, 0x08, 0x00, 0x00};
	API_PARSE *ai;
	API_PARSE ai_parms[5];
	word ch = 0;

	if (!plci) {
		dbug(1, dprintf("connect_res(no plci)"));
		return 0;  /* no plci, no send */
	}

	dbug(1, dprintf("connect_res(State=0x%x)", plci->State));
	for (i = 0; i < 5; i++) ai_parms[i].length = 0;
	ai = &parms[5];
	dbug(1, dprintf("ai->length=%d", ai->length));

	if (ai->length)
	{
		if (!api_parse(&ai->info[1], (word)ai->length, "ssss", ai_parms))
		{
			dbug(1, dprintf("ai_parms[0].length=%d/0x%x", ai_parms[0].length, GET_WORD(ai_parms[0].info + 1)));
			ch = 0;
			if (ai_parms[0].length)
			{
				ch = GET_WORD(ai_parms[0].info + 1);
				dbug(1, dprintf("BCH-I=0x%x", ch));
			}
		}
	}

	if (plci->State == INC_CON_CONNECTED_ALERT)
	{
		dbug(1, dprintf("Connected Alert Call_Res"));
		if (a->Info_Mask[appl->Id - 1] & 0x200)
		{
			/* early B3 connect (CIP mask bit 9) no release after a disc */
			add_p(plci, LLI, "\x01\x01");
		}
		add_s(plci, CONN_NR, &parms[2]);
		add_s(plci, LLC, &parms[4]);
		add_ai(plci, &parms[5]);
		plci->State = INC_CON_ACCEPT;
		sig_req(plci, CALL_RES, 0);
		return 1;
	}
	else if (plci->State == INC_CON_PENDING || plci->State == INC_CON_ALERT) {
		clear_c_ind_mask_bit(plci, (word)(appl->Id - 1));
		dump_c_ind_mask(plci);
		Reject = GET_WORD(parms[0].info);
		dbug(1, dprintf("Reject=0x%x", Reject));
		if (Reject)
		{
			if (c_ind_mask_empty(plci))
			{
				if ((Reject & 0xff00) == 0x3400)
				{
					esc_t[2] = ((byte)(Reject & 0x00ff)) | 0x80;
					add_p(plci, ESC, esc_t);
					add_ai(plci, &parms[5]);
					sig_req(plci, REJECT, 0);
				}
				else if (Reject == 1 || Reject > 9)
				{
					add_ai(plci, &parms[5]);
					sig_req(plci, HANGUP, 0);
				}
				else
				{
					esc_t[2] = cau_t[(Reject&0x000f)];
					add_p(plci, ESC, esc_t);
					add_ai(plci, &parms[5]);
					sig_req(plci, REJECT, 0);
				}
				plci->appl = appl;
			}
			else
			{
				sendf(appl, _DISCONNECT_I, Id, 0, "w", _OTHER_APPL_CONNECTED);
			}
		}
		else {
			plci->appl = appl;
			if (Id & EXT_CONTROLLER) {
				if (AdvCodecSupport(a, plci, appl, 0)) {
					dbug(1, dprintf("connect_res(error from AdvCodecSupport)"));
					sig_req(plci, HANGUP, 0);
					return 1;
				}
				if (plci->tel == ADV_VOICE && a->AdvCodecPLCI)
				{
					Info = add_b23(plci, &parms[1]);
					if (Info)
					{
						dbug(1, dprintf("connect_res(error from add_b23)"));
						sig_req(plci, HANGUP, 0);
						return 1;
					}
					if (plci->adv_nl)
					{
						nl_req_ncci(plci, ASSIGN, 0);
					}
				}
			}
			else
			{
				plci->tel = 0;
				if (ch != 2)
				{
					Info = add_b23(plci, &parms[1]);
					if (Info)
					{
						dbug(1, dprintf("connect_res(error from add_b23 2)"));
						sig_req(plci, HANGUP, 0);
						return 1;
					}
				}
				nl_req_ncci(plci, ASSIGN, 0);
			}

			if (plci->spoofed_msg == SPOOFING_REQUIRED)
			{
				api_save_msg(parms, "wsssss", &plci->saved_msg);
				plci->spoofed_msg = CALL_RES;
				plci->internal_command = BLOCK_PLCI;
				plci->command = 0;
				dbug(1, dprintf("Spoof"));
			}
			else
			{
				add_b1(plci, &parms[1], ch, plci->B1_facilities);
				if (a->Info_Mask[appl->Id - 1] & 0x200)
				{
					/* early B3 connect (CIP mask bit 9) no release after a disc */
					add_p(plci, LLI, "\x01\x01");
				}
				add_s(plci, CONN_NR, &parms[2]);
				add_s(plci, LLC, &parms[4]);
				add_ai(plci, &parms[5]);
				plci->State = INC_CON_ACCEPT;
				sig_req(plci, CALL_RES, 0);
			}

			for (i = 0; i < max_appl; i++) {
				if (test_c_ind_mask_bit(plci, i)) {
					sendf(&application[i], _DISCONNECT_I, Id, 0, "w", _OTHER_APPL_CONNECTED);
				}
			}
		}
	}
	return 1;
}

static byte connect_a_res(dword Id, word Number, DIVA_CAPI_ADAPTER *a,
			  PLCI *plci, APPL *appl, API_PARSE *msg)
{
	dbug(1, dprintf("connect_a_res"));
	return false;
}

static byte disconnect_req(dword Id, word Number, DIVA_CAPI_ADAPTER *a,
			   PLCI *plci, APPL *appl, API_PARSE *msg)
{
	word Info;
	word i;

	dbug(1, dprintf("disconnect_req"));

	Info = _WRONG_IDENTIFIER;

	if (plci)
	{
		if (plci->State == INC_CON_PENDING || plci->State == INC_CON_ALERT)
		{
			clear_c_ind_mask_bit(plci, (word)(appl->Id - 1));
			plci->appl = appl;
			for (i = 0; i < max_appl; i++)
			{
				if (test_c_ind_mask_bit(plci, i))
					sendf(&application[i], _DISCONNECT_I, Id, 0, "w", 0);
			}
			plci->State = OUTG_DIS_PENDING;
		}
		if (plci->Sig.Id && plci->appl)
		{
			Info = 0;
			if (plci->Sig.Id != 0xff)
			{
				if (plci->State != INC_DIS_PENDING)
				{
					add_ai(plci, &msg[0]);
					sig_req(plci, HANGUP, 0);
					plci->State = OUTG_DIS_PENDING;
					return 1;
				}
			}
			else
			{
				if (plci->NL.Id && !plci->nl_remove_id)
				{
					mixer_remove(plci);
					nl_req_ncci(plci, REMOVE, 0);
					sendf(appl, _DISCONNECT_R | CONFIRM, Id, Number, "w", 0);
					sendf(appl, _DISCONNECT_I, Id, 0, "w", 0);
					plci->State = INC_DIS_PENDING;
				}
				return 1;
			}
		}
	}

	if (!appl)  return false;
	sendf(appl, _DISCONNECT_R | CONFIRM, Id, Number, "w", Info);
	return false;
}

static byte disconnect_res(dword Id, word Number, DIVA_CAPI_ADAPTER *a,
			   PLCI *plci, APPL *appl, API_PARSE *msg)
{
	dbug(1, dprintf("disconnect_res"));
	if (plci)
	{
		/* clear ind mask bit, just in case of collsion of          */
		/* DISCONNECT_IND and CONNECT_RES                           */
		clear_c_ind_mask_bit(plci, (word)(appl->Id - 1));
		ncci_free_receive_buffers(plci, 0);
		if (plci_remove_check(plci))
		{
			return 0;
		}
		if (plci->State == INC_DIS_PENDING
		    || plci->State == SUSPENDING) {
			if (c_ind_mask_empty(plci)) {
				if (plci->State != SUSPENDING) plci->State = IDLE;
				dbug(1, dprintf("chs=%d", plci->channels));
				if (!plci->channels) {
					plci_remove(plci);
				}
			}
		}
	}
	return 0;
}

static byte listen_req(dword Id, word Number, DIVA_CAPI_ADAPTER *a,
		       PLCI *plci, APPL *appl, API_PARSE *parms)
{
	word Info;
	byte i;

	dbug(1, dprintf("listen_req(Appl=0x%x)", appl->Id));

	Info = _WRONG_IDENTIFIER;
	if (a) {
		Info = 0;
		a->Info_Mask[appl->Id - 1] = GET_DWORD(parms[0].info);
		a->CIP_Mask[appl->Id - 1] = GET_DWORD(parms[1].info);
		dbug(1, dprintf("CIP_MASK=0x%lx", GET_DWORD(parms[1].info)));
		if (a->Info_Mask[appl->Id - 1] & 0x200) { /* early B3 connect provides */
			a->Info_Mask[appl->Id - 1] |=  0x10;   /* call progression infos    */
		}

		/* check if external controller listen and switch listen on or off*/
		if (Id&EXT_CONTROLLER && GET_DWORD(parms[1].info)) {
			if (a->profile.Global_Options & ON_BOARD_CODEC) {
				dummy_plci.State = IDLE;
				a->codec_listen[appl->Id - 1] = &dummy_plci;
				a->TelOAD[0] = (byte)(parms[3].length);
				for (i = 1; parms[3].length >= i && i < 22; i++) {
					a->TelOAD[i] = parms[3].info[i];
				}
				a->TelOAD[i] = 0;
				a->TelOSA[0] = (byte)(parms[4].length);
				for (i = 1; parms[4].length >= i && i < 22; i++) {
					a->TelOSA[i] = parms[4].info[i];
				}
				a->TelOSA[i] = 0;
			}
			else Info = 0x2002; /* wrong controller, codec not supported */
		}
		else{               /* clear listen */
			a->codec_listen[appl->Id - 1] = (PLCI *)0;
		}
	}
	sendf(appl,
	      _LISTEN_R | CONFIRM,
	      Id,
	      Number,
	      "w", Info);

	if (a) listen_check(a);
	return false;
}

static byte info_req(dword Id, word Number, DIVA_CAPI_ADAPTER *a,
		     PLCI *plci, APPL *appl, API_PARSE *msg)
{
	word i;
	API_PARSE *ai;
	PLCI *rc_plci = NULL;
	API_PARSE ai_parms[5];
	word Info = 0;

	dbug(1, dprintf("info_req"));
	for (i = 0; i < 5; i++) ai_parms[i].length = 0;

	ai = &msg[1];

	if (ai->length)
	{
		if (api_parse(&ai->info[1], (word)ai->length, "ssss", ai_parms))
		{
			dbug(1, dprintf("AddInfo wrong"));
			Info = _WRONG_MESSAGE_FORMAT;
		}
	}
	if (!a) Info = _WRONG_STATE;

	if (!Info && plci)
	{                /* no fac, with CPN, or KEY */
		rc_plci = plci;
		if (!ai_parms[3].length && plci->State && (msg[0].length || ai_parms[1].length))
		{
			/* overlap sending option */
			dbug(1, dprintf("OvlSnd"));
			add_s(plci, CPN, &msg[0]);
			add_s(plci, KEY, &ai_parms[1]);
			sig_req(plci, INFO_REQ, 0);
			send_req(plci);
			return false;
		}

		if (plci->State && ai_parms[2].length)
		{
			/* User_Info option */
			dbug(1, dprintf("UUI"));
			add_s(plci, UUI, &ai_parms[2]);
			sig_req(plci, USER_DATA, 0);
		}
		else if (plci->State && ai_parms[3].length)
		{
			/* Facility option */
			dbug(1, dprintf("FAC"));
			add_s(plci, CPN, &msg[0]);
			add_ai(plci, &msg[1]);
			sig_req(plci, FACILITY_REQ, 0);
		}
		else
		{
			Info = _WRONG_STATE;
		}
	}
	else if ((ai_parms[1].length || ai_parms[2].length || ai_parms[3].length) && !Info)
	{
		/* NCR_Facility option -> send UUI and Keypad too */
		dbug(1, dprintf("NCR_FAC"));
		if ((i = get_plci(a)))
		{
			rc_plci = &a->plci[i - 1];
			appl->NullCREnable = true;
			rc_plci->internal_command = C_NCR_FAC_REQ;
			rc_plci->appl = appl;
			add_p(rc_plci, CAI, "\x01\x80");
			add_p(rc_plci, UID, "\x06\x43\x61\x70\x69\x32\x30");
			sig_req(rc_plci, ASSIGN, DSIG_ID);
			send_req(rc_plci);
		}
		else
		{
			Info = _OUT_OF_PLCI;
		}

		if (!Info)
		{
			add_s(rc_plci, CPN, &msg[0]);
			add_ai(rc_plci, &msg[1]);
			sig_req(rc_plci, NCR_FACILITY, 0);
			send_req(rc_plci);
			return false;
			/* for application controlled supplementary services    */
		}
	}

	if (!rc_plci)
	{
		Info = _WRONG_MESSAGE_FORMAT;
	}

	if (!Info)
	{
		send_req(rc_plci);
	}
	else
	{  /* appl is not assigned to a PLCI or error condition */
		dbug(1, dprintf("localInfoCon"));
		sendf(appl,
		      _INFO_R | CONFIRM,
		      Id,
		      Number,
		      "w", Info);
	}
	return false;
}

static byte info_res(dword Id, word Number, DIVA_CAPI_ADAPTER *a,
		     PLCI *plci, APPL *appl, API_PARSE *msg)
{
	dbug(1, dprintf("info_res"));
	return false;
}

static byte alert_req(dword Id, word Number, DIVA_CAPI_ADAPTER *a,
		      PLCI *plci, APPL *appl, API_PARSE *msg)
{
	word Info;
	byte ret;

	dbug(1, dprintf("alert_req"));

	Info = _WRONG_IDENTIFIER;
	ret = false;
	if (plci) {
		Info = _ALERT_IGNORED;
		if (plci->State != INC_CON_ALERT) {
			Info = _WRONG_STATE;
			if (plci->State == INC_CON_PENDING) {
				Info = 0;
				plci->State = INC_CON_ALERT;
				add_ai(plci, &msg[0]);
				sig_req(plci, CALL_ALERT, 0);
				ret = 1;
			}
		}
	}
	sendf(appl,
	      _ALERT_R | CONFIRM,
	      Id,
	      Number,
	      "w", Info);
	return ret;
}

static byte facility_req(dword Id, word Number, DIVA_CAPI_ADAPTER *a,
			 PLCI *plci, APPL *appl, API_PARSE *msg)
{
	word Info = 0;
	word i    = 0;

	word selector;
	word SSreq;
	long relatedPLCIvalue;
	DIVA_CAPI_ADAPTER *relatedadapter;
	byte *SSparms  = "";
	byte RCparms[]  = "\x05\x00\x00\x02\x00\x00";
	byte SSstruct[] = "\x09\x00\x00\x06\x00\x00\x00\x00\x00\x00";
	API_PARSE *parms;
	API_PARSE ss_parms[11];
	PLCI *rplci;
	byte cai[15];
	dword d;
	API_PARSE dummy;

	dbug(1, dprintf("facility_req"));
	for (i = 0; i < 9; i++) ss_parms[i].length = 0;

	parms = &msg[1];

	if (!a)
	{
		dbug(1, dprintf("wrong Ctrl"));
		Info = _WRONG_IDENTIFIER;
	}

	selector = GET_WORD(msg[0].info);

	if (!Info)
	{
		switch (selector)
		{
		case SELECTOR_HANDSET:
			Info = AdvCodecSupport(a, plci, appl, HOOK_SUPPORT);
			break;

		case SELECTOR_SU_SERV:
			if (!msg[1].length)
			{
				Info = _WRONG_MESSAGE_FORMAT;
				break;
			}
			SSreq = GET_WORD(&(msg[1].info[1]));
			PUT_WORD(&RCparms[1], SSreq);
			SSparms = RCparms;
			switch (SSreq)
			{
			case S_GET_SUPPORTED_SERVICES:
				if ((i = get_plci(a)))
				{
					rplci = &a->plci[i - 1];
					rplci->appl = appl;
					add_p(rplci, CAI, "\x01\x80");
					add_p(rplci, UID, "\x06\x43\x61\x70\x69\x32\x30");
					sig_req(rplci, ASSIGN, DSIG_ID);
					send_req(rplci);
				}
				else
				{
					PUT_DWORD(&SSstruct[6], MASK_TERMINAL_PORTABILITY);
					SSparms = (byte *)SSstruct;
					break;
				}
				rplci->internal_command = GETSERV_REQ_PEND;
				rplci->number = Number;
				rplci->appl = appl;
				sig_req(rplci, S_SUPPORTED, 0);
				send_req(rplci);
				return false;
				break;

			case S_LISTEN:
				if (parms->length == 7)
				{
					if (api_parse(&parms->info[1], (word)parms->length, "wbd", ss_parms))
					{
						dbug(1, dprintf("format wrong"));
						Info = _WRONG_MESSAGE_FORMAT;
						break;
					}
				}
				else
				{
					Info = _WRONG_MESSAGE_FORMAT;
					break;
				}
				a->Notification_Mask[appl->Id - 1] = GET_DWORD(ss_parms[2].info);
				if (a->Notification_Mask[appl->Id - 1] & SMASK_MWI) /* MWI active? */
				{
					if ((i = get_plci(a)))
					{
						rplci = &a->plci[i - 1];
						rplci->appl = appl;
						add_p(rplci, CAI, "\x01\x80");
						add_p(rplci, UID, "\x06\x43\x61\x70\x69\x32\x30");
						sig_req(rplci, ASSIGN, DSIG_ID);
						send_req(rplci);
					}
					else
					{
						break;
					}
					rplci->internal_command = GET_MWI_STATE;
					rplci->number = Number;
					sig_req(rplci, MWI_POLL, 0);
					send_req(rplci);
				}
				break;

			case S_HOLD:
				api_parse(&parms->info[1], (word)parms->length, "ws", ss_parms);
				if (plci && plci->State && plci->SuppState == IDLE)
				{
					plci->SuppState = HOLD_REQUEST;
					plci->command = C_HOLD_REQ;
					add_s(plci, CAI, &ss_parms[1]);
					sig_req(plci, CALL_HOLD, 0);
					send_req(plci);
					return false;
				}
				else Info = 0x3010;                    /* wrong state           */
				break;
			case S_RETRIEVE:
				if (plci && plci->State && plci->SuppState == CALL_HELD)
				{
					if (Id & EXT_CONTROLLER)
					{
						if (AdvCodecSupport(a, plci, appl, 0))
						{
							Info = 0x3010;                    /* wrong state           */
							break;
						}
					}
					else plci->tel = 0;

					plci->SuppState = RETRIEVE_REQUEST;
					plci->command = C_RETRIEVE_REQ;
					if (plci->spoofed_msg == SPOOFING_REQUIRED)
					{
						plci->spoofed_msg = CALL_RETRIEVE;
						plci->internal_command = BLOCK_PLCI;
						plci->command = 0;
						dbug(1, dprintf("Spoof"));
						return false;
					}
					else
					{
						sig_req(plci, CALL_RETRIEVE, 0);
						send_req(plci);
						return false;
					}
				}
				else Info = 0x3010;                    /* wrong state           */
				break;
			case S_SUSPEND:
				if (parms->length)
				{
					if (api_parse(&parms->info[1], (word)parms->length, "wbs", ss_parms))
					{
						dbug(1, dprintf("format wrong"));
						Info = _WRONG_MESSAGE_FORMAT;
						break;
					}
				}
				if (plci && plci->State)
				{
					add_s(plci, CAI, &ss_parms[2]);
					plci->command = SUSPEND_REQ;
					sig_req(plci, SUSPEND, 0);
					plci->State = SUSPENDING;
					send_req(plci);
				}
				else Info = 0x3010;                    /* wrong state           */
				break;

			case S_RESUME:
				if (!(i = get_plci(a)))
				{
					Info = _OUT_OF_PLCI;
					break;
				}
				rplci = &a->plci[i - 1];
				rplci->appl = appl;
				rplci->number = Number;
				rplci->tel = 0;
				rplci->call_dir = CALL_DIR_OUT | CALL_DIR_ORIGINATE;
				/* check 'external controller' bit for codec support */
				if (Id & EXT_CONTROLLER)
				{
					if (AdvCodecSupport(a, rplci, appl, 0))
					{
						rplci->Id = 0;
						Info = 0x300A;
						break;
					}
				}
				if (parms->length)
				{
					if (api_parse(&parms->info[1], (word)parms->length, "wbs", ss_parms))
					{
						dbug(1, dprintf("format wrong"));
						rplci->Id = 0;
						Info = _WRONG_MESSAGE_FORMAT;
						break;
					}
				}
				dummy.length = 0;
				dummy.info = "\x00";
				add_b1(rplci, &dummy, 0, 0);
				if (a->Info_Mask[appl->Id - 1] & 0x200)
				{
					/* early B3 connect (CIP mask bit 9) no release after a disc */
					add_p(rplci, LLI, "\x01\x01");
				}
				add_p(rplci, UID, "\x06\x43\x61\x70\x69\x32\x30");
				sig_req(rplci, ASSIGN, DSIG_ID);
				send_req(rplci);
				add_s(rplci, CAI, &ss_parms[2]);
				rplci->command = RESUME_REQ;
				sig_req(rplci, RESUME, 0);
				rplci->State = RESUMING;
				send_req(rplci);
				break;

			case S_CONF_BEGIN: /* Request */
			case S_CONF_DROP:
			case S_CONF_ISOLATE:
			case S_CONF_REATTACH:
				if (api_parse(&parms->info[1], (word)parms->length, "wbd", ss_parms))
				{
					dbug(1, dprintf("format wrong"));
					Info = _WRONG_MESSAGE_FORMAT;
					break;
				}
				if (plci && plci->State && ((plci->SuppState == IDLE) || (plci->SuppState == CALL_HELD)))
				{
					d = GET_DWORD(ss_parms[2].info);
					if (d >= 0x80)
					{
						dbug(1, dprintf("format wrong"));
						Info = _WRONG_MESSAGE_FORMAT;
						break;
					}
					plci->ptyState = (byte)SSreq;
					plci->command = 0;
					cai[0] = 2;
					switch (SSreq)
					{
					case S_CONF_BEGIN:
						cai[1] = CONF_BEGIN;
						plci->internal_command = CONF_BEGIN_REQ_PEND;
						break;
					case S_CONF_DROP:
						cai[1] = CONF_DROP;
						plci->internal_command = CONF_DROP_REQ_PEND;
						break;
					case S_CONF_ISOLATE:
						cai[1] = CONF_ISOLATE;
						plci->internal_command = CONF_ISOLATE_REQ_PEND;
						break;
					case S_CONF_REATTACH:
						cai[1] = CONF_REATTACH;
						plci->internal_command = CONF_REATTACH_REQ_PEND;
						break;
					}
					cai[2] = (byte)d; /* Conference Size resp. PartyId */
					add_p(plci, CAI, cai);
					sig_req(plci, S_SERVICE, 0);
					send_req(plci);
					return false;
				}
				else Info = 0x3010;                    /* wrong state           */
				break;

			case S_ECT:
			case S_3PTY_BEGIN:
			case S_3PTY_END:
			case S_CONF_ADD:
				if (parms->length == 7)
				{
					if (api_parse(&parms->info[1], (word)parms->length, "wbd", ss_parms))
					{
						dbug(1, dprintf("format wrong"));
						Info = _WRONG_MESSAGE_FORMAT;
						break;
					}
				}
				else if (parms->length == 8) /* workaround for the T-View-S */
				{
					if (api_parse(&parms->info[1], (word)parms->length, "wbdb", ss_parms))
					{
						dbug(1, dprintf("format wrong"));
						Info = _WRONG_MESSAGE_FORMAT;
						break;
					}
				}
				else
				{
					Info = _WRONG_MESSAGE_FORMAT;
					break;
				}
				if (!msg[1].length)
				{
					Info = _WRONG_MESSAGE_FORMAT;
					break;
				}
				if (!plci)
				{
					Info = _WRONG_IDENTIFIER;
					break;
				}
				relatedPLCIvalue = GET_DWORD(ss_parms[2].info);
				relatedPLCIvalue &= 0x0000FFFF;
				dbug(1, dprintf("PTY/ECT/addCONF,relPLCI=%lx", relatedPLCIvalue));
				/* controller starts with 0 up to (max_adapter - 1) */
				if (((relatedPLCIvalue & 0x7f) == 0)
				    || (MapController((byte)(relatedPLCIvalue & 0x7f)) == 0)
				    || (MapController((byte)(relatedPLCIvalue & 0x7f)) > max_adapter))
				{
					if (SSreq == S_3PTY_END)
					{
						dbug(1, dprintf("wrong Controller use 2nd PLCI=PLCI"));
						rplci = plci;
					}
					else
					{
						Info = 0x3010;                    /* wrong state           */
						break;
					}
				}
				else
				{
					relatedadapter = &adapter[MapController((byte)(relatedPLCIvalue & 0x7f)) - 1];
					relatedPLCIvalue >>= 8;
					/* find PLCI PTR*/
					for (i = 0, rplci = NULL; i < relatedadapter->max_plci; i++)
					{
						if (relatedadapter->plci[i].Id == (byte)relatedPLCIvalue)
						{
							rplci = &relatedadapter->plci[i];
						}
					}
					if (!rplci || !relatedPLCIvalue)
					{
						if (SSreq == S_3PTY_END)
						{
							dbug(1, dprintf("use 2nd PLCI=PLCI"));
							rplci = plci;
						}
						else
						{
							Info = 0x3010;                    /* wrong state           */
							break;
						}
					}
				}
/*
  dbug(1, dprintf("rplci:%x", rplci));
  dbug(1, dprintf("plci:%x", plci));
  dbug(1, dprintf("rplci->ptyState:%x", rplci->ptyState));
  dbug(1, dprintf("plci->ptyState:%x", plci->ptyState));
  dbug(1, dprintf("SSreq:%x", SSreq));
  dbug(1, dprintf("rplci->internal_command:%x", rplci->internal_command));
  dbug(1, dprintf("rplci->appl:%x", rplci->appl));
  dbug(1, dprintf("rplci->Id:%x", rplci->Id));
*/
				/* send PTY/ECT req, cannot check all states because of US stuff */
				if (!rplci->internal_command && rplci->appl)
				{
					plci->command = 0;
					rplci->relatedPTYPLCI = plci;
					plci->relatedPTYPLCI = rplci;
					rplci->ptyState = (byte)SSreq;
					if (SSreq == S_ECT)
					{
						rplci->internal_command = ECT_REQ_PEND;
						cai[1] = ECT_EXECUTE;

						rplci->vswitchstate = 0;
						rplci->vsprot = 0;
						rplci->vsprotdialect = 0;
						plci->vswitchstate = 0;
						plci->vsprot = 0;
						plci->vsprotdialect = 0;

					}
					else if (SSreq == S_CONF_ADD)
					{
						rplci->internal_command = CONF_ADD_REQ_PEND;
						cai[1] = CONF_ADD;
					}
					else
					{
						rplci->internal_command = PTY_REQ_PEND;
						cai[1] = (byte)(SSreq - 3);
					}
					rplci->number = Number;
					if (plci != rplci) /* explicit invocation */
					{
						cai[0] = 2;
						cai[2] = plci->Sig.Id;
						dbug(1, dprintf("explicit invocation"));
					}
					else
					{
						dbug(1, dprintf("implicit invocation"));
						cai[0] = 1;
					}
					add_p(rplci, CAI, cai);
					sig_req(rplci, S_SERVICE, 0);
					send_req(rplci);
					return false;
				}
				else
				{
					dbug(0, dprintf("Wrong line"));
					Info = 0x3010;                    /* wrong state           */
					break;
				}
				break;

			case S_CALL_DEFLECTION:
				if (api_parse(&parms->info[1], (word)parms->length, "wbwss", ss_parms))
				{
					dbug(1, dprintf("format wrong"));
					Info = _WRONG_MESSAGE_FORMAT;
					break;
				}
				if (!plci)
				{
					Info = _WRONG_IDENTIFIER;
					break;
				}
				/* reuse unused screening indicator */
				ss_parms[3].info[3] = (byte)GET_WORD(&(ss_parms[2].info[0]));
				plci->command = 0;
				plci->internal_command = CD_REQ_PEND;
				appl->CDEnable = true;
				cai[0] = 1;
				cai[1] = CALL_DEFLECTION;
				add_p(plci, CAI, cai);
				add_p(plci, CPN, ss_parms[3].info);
				sig_req(plci, S_SERVICE, 0);
				send_req(plci);
				return false;
				break;

			case S_CALL_FORWARDING_START:
				if (api_parse(&parms->info[1], (word)parms->length, "wbdwwsss", ss_parms))
				{
					dbug(1, dprintf("format wrong"));
					Info = _WRONG_MESSAGE_FORMAT;
					break;
				}

				if ((i = get_plci(a)))
				{
					rplci = &a->plci[i - 1];
					rplci->appl = appl;
					add_p(rplci, CAI, "\x01\x80");
					add_p(rplci, UID, "\x06\x43\x61\x70\x69\x32\x30");
					sig_req(rplci, ASSIGN, DSIG_ID);
					send_req(rplci);
				}
				else
				{
					Info = _OUT_OF_PLCI;
					break;
				}

				/* reuse unused screening indicator */
				rplci->internal_command = CF_START_PEND;
				rplci->appl = appl;
				rplci->number = Number;
				appl->S_Handle = GET_DWORD(&(ss_parms[2].info[0]));
				cai[0] = 2;
				cai[1] = 0x70 | (byte)GET_WORD(&(ss_parms[3].info[0])); /* Function */
				cai[2] = (byte)GET_WORD(&(ss_parms[4].info[0])); /* Basic Service */
				add_p(rplci, CAI, cai);
				add_p(rplci, OAD, ss_parms[5].info);
				add_p(rplci, CPN, ss_parms[6].info);
				sig_req(rplci, S_SERVICE, 0);
				send_req(rplci);
				return false;
				break;

			case S_INTERROGATE_DIVERSION:
			case S_INTERROGATE_NUMBERS:
			case S_CALL_FORWARDING_STOP:
			case S_CCBS_REQUEST:
			case S_CCBS_DEACTIVATE:
			case S_CCBS_INTERROGATE:
				switch (SSreq)
				{
				case S_INTERROGATE_NUMBERS:
					if (api_parse(&parms->info[1], (word)parms->length, "wbd", ss_parms))
					{
						dbug(0, dprintf("format wrong"));
						Info = _WRONG_MESSAGE_FORMAT;
					}
					break;
				case S_CCBS_REQUEST:
				case S_CCBS_DEACTIVATE:
					if (api_parse(&parms->info[1], (word)parms->length, "wbdw", ss_parms))
					{
						dbug(0, dprintf("format wrong"));
						Info = _WRONG_MESSAGE_FORMAT;
					}
					break;
				case S_CCBS_INTERROGATE:
					if (api_parse(&parms->info[1], (word)parms->length, "wbdws", ss_parms))
					{
						dbug(0, dprintf("format wrong"));
						Info = _WRONG_MESSAGE_FORMAT;
					}
					break;
				default:
					if (api_parse(&parms->info[1], (word)parms->length, "wbdwws", ss_parms))
					{
						dbug(0, dprintf("format wrong"));
						Info = _WRONG_MESSAGE_FORMAT;
						break;
					}
					break;
				}

				if (Info) break;
				if ((i = get_plci(a)))
				{
					rplci = &a->plci[i - 1];
					switch (SSreq)
					{
					case S_INTERROGATE_DIVERSION: /* use cai with S_SERVICE below */
						cai[1] = 0x60 | (byte)GET_WORD(&(ss_parms[3].info[0])); /* Function */
						rplci->internal_command = INTERR_DIVERSION_REQ_PEND; /* move to rplci if assigned */
						break;
					case S_INTERROGATE_NUMBERS: /* use cai with S_SERVICE below */
						cai[1] = DIVERSION_INTERROGATE_NUM; /* Function */
						rplci->internal_command = INTERR_NUMBERS_REQ_PEND; /* move to rplci if assigned */
						break;
					case S_CALL_FORWARDING_STOP:
						rplci->internal_command = CF_STOP_PEND;
						cai[1] = 0x80 | (byte)GET_WORD(&(ss_parms[3].info[0])); /* Function */
						break;
					case S_CCBS_REQUEST:
						cai[1] = CCBS_REQUEST;
						rplci->internal_command = CCBS_REQUEST_REQ_PEND;
						break;
					case S_CCBS_DEACTIVATE:
						cai[1] = CCBS_DEACTIVATE;
						rplci->internal_command = CCBS_DEACTIVATE_REQ_PEND;
						break;
					case S_CCBS_INTERROGATE:
						cai[1] = CCBS_INTERROGATE;
						rplci->internal_command = CCBS_INTERROGATE_REQ_PEND;
						break;
					default:
						cai[1] = 0;
						break;
					}
					rplci->appl = appl;
					rplci->number = Number;
					add_p(rplci, CAI, "\x01\x80");
					add_p(rplci, UID, "\x06\x43\x61\x70\x69\x32\x30");
					sig_req(rplci, ASSIGN, DSIG_ID);
					send_req(rplci);
				}
				else
				{
					Info = _OUT_OF_PLCI;
					break;
				}

				appl->S_Handle = GET_DWORD(&(ss_parms[2].info[0]));
				switch (SSreq)
				{
				case S_INTERROGATE_NUMBERS:
					cai[0] = 1;
					add_p(rplci, CAI, cai);
					break;
				case S_CCBS_REQUEST:
				case S_CCBS_DEACTIVATE:
					cai[0] = 3;
					PUT_WORD(&cai[2], GET_WORD(&(ss_parms[3].info[0])));
					add_p(rplci, CAI, cai);
					break;
				case S_CCBS_INTERROGATE:
					cai[0] = 3;
					PUT_WORD(&cai[2], GET_WORD(&(ss_parms[3].info[0])));
					add_p(rplci, CAI, cai);
					add_p(rplci, OAD, ss_parms[4].info);
					break;
				default:
					cai[0] = 2;
					cai[2] = (byte)GET_WORD(&(ss_parms[4].info[0])); /* Basic Service */
					add_p(rplci, CAI, cai);
					add_p(rplci, OAD, ss_parms[5].info);
					break;
				}

				sig_req(rplci, S_SERVICE, 0);
				send_req(rplci);
				return false;
				break;

			case S_MWI_ACTIVATE:
				if (api_parse(&parms->info[1], (word)parms->length, "wbwdwwwssss", ss_parms))
				{
					dbug(1, dprintf("format wrong"));
					Info = _WRONG_MESSAGE_FORMAT;
					break;
				}
				if (!plci)
				{
					if ((i = get_plci(a)))
					{
						rplci = &a->plci[i - 1];
						rplci->appl = appl;
						rplci->cr_enquiry = true;
						add_p(rplci, CAI, "\x01\x80");
						add_p(rplci, UID, "\x06\x43\x61\x70\x69\x32\x30");
						sig_req(rplci, ASSIGN, DSIG_ID);
						send_req(rplci);
					}
					else
					{
						Info = _OUT_OF_PLCI;
						break;
					}
				}
				else
				{
					rplci = plci;
					rplci->cr_enquiry = false;
				}

				rplci->command = 0;
				rplci->internal_command = MWI_ACTIVATE_REQ_PEND;
				rplci->appl = appl;
				rplci->number = Number;

				cai[0] = 13;
				cai[1] = ACTIVATION_MWI; /* Function */
				PUT_WORD(&cai[2], GET_WORD(&(ss_parms[2].info[0]))); /* Basic Service */
				PUT_DWORD(&cai[4], GET_DWORD(&(ss_parms[3].info[0]))); /* Number of Messages */
				PUT_WORD(&cai[8], GET_WORD(&(ss_parms[4].info[0]))); /* Message Status */
				PUT_WORD(&cai[10], GET_WORD(&(ss_parms[5].info[0]))); /* Message Reference */
				PUT_WORD(&cai[12], GET_WORD(&(ss_parms[6].info[0]))); /* Invocation Mode */
				add_p(rplci, CAI, cai);
				add_p(rplci, CPN, ss_parms[7].info); /* Receiving User Number */
				add_p(rplci, OAD, ss_parms[8].info); /* Controlling User Number */
				add_p(rplci, OSA, ss_parms[9].info); /* Controlling User Provided Number */
				add_p(rplci, UID, ss_parms[10].info); /* Time */
				sig_req(rplci, S_SERVICE, 0);
				send_req(rplci);
				return false;

			case S_MWI_DEACTIVATE:
				if (api_parse(&parms->info[1], (word)parms->length, "wbwwss", ss_parms))
				{
					dbug(1, dprintf("format wrong"));
					Info = _WRONG_MESSAGE_FORMAT;
					break;
				}
				if (!plci)
				{
					if ((i = get_plci(a)))
					{
						rplci = &a->plci[i - 1];
						rplci->appl = appl;
						rplci->cr_enquiry = true;
						add_p(rplci, CAI, "\x01\x80");
						add_p(rplci, UID, "\x06\x43\x61\x70\x69\x32\x30");
						sig_req(rplci, ASSIGN, DSIG_ID);
						send_req(rplci);
					}
					else
					{
						Info = _OUT_OF_PLCI;
						break;
					}
				}
				else
				{
					rplci = plci;
					rplci->cr_enquiry = false;
				}

				rplci->command = 0;
				rplci->internal_command = MWI_DEACTIVATE_REQ_PEND;
				rplci->appl = appl;
				rplci->number = Number;

				cai[0] = 5;
				cai[1] = DEACTIVATION_MWI; /* Function */
				PUT_WORD(&cai[2], GET_WORD(&(ss_parms[2].info[0]))); /* Basic Service */
				PUT_WORD(&cai[4], GET_WORD(&(ss_parms[3].info[0]))); /* Invocation Mode */
				add_p(rplci, CAI, cai);
				add_p(rplci, CPN, ss_parms[4].info); /* Receiving User Number */
				add_p(rplci, OAD, ss_parms[5].info); /* Controlling User Number */
				sig_req(rplci, S_SERVICE, 0);
				send_req(rplci);
				return false;

			default:
				Info = 0x300E;  /* not supported */
				break;
			}
			break; /* case SELECTOR_SU_SERV: end */


		case SELECTOR_DTMF:
			return (dtmf_request(Id, Number, a, plci, appl, msg));



		case SELECTOR_LINE_INTERCONNECT:
			return (mixer_request(Id, Number, a, plci, appl, msg));



		case PRIV_SELECTOR_ECHO_CANCELLER:
			appl->appl_flags |= APPL_FLAG_PRIV_EC_SPEC;
			return (ec_request(Id, Number, a, plci, appl, msg));

		case SELECTOR_ECHO_CANCELLER:
			appl->appl_flags &= ~APPL_FLAG_PRIV_EC_SPEC;
			return (ec_request(Id, Number, a, plci, appl, msg));


		case SELECTOR_V42BIS:
		default:
			Info = _FACILITY_NOT_SUPPORTED;
			break;
		} /* end of switch (selector) */
	}

	dbug(1, dprintf("SendFacRc"));
	sendf(appl,
	      _FACILITY_R | CONFIRM,
	      Id,
	      Number,
	      "wws", Info, selector, SSparms);
	return false;
}

static byte facility_res(dword Id, word Number, DIVA_CAPI_ADAPTER *a,
			 PLCI *plci, APPL *appl, API_PARSE *msg)
{
	dbug(1, dprintf("facility_res"));
	return false;
}

static byte connect_b3_req(dword Id, word Number, DIVA_CAPI_ADAPTER *a,
			   PLCI *plci, APPL *appl, API_PARSE *parms)
{
	word Info = 0;
	byte req;
	byte len;
	word w;
	word fax_control_bits, fax_feature_bits, fax_info_change;
	API_PARSE *ncpi;
	byte pvc[2];

	API_PARSE fax_parms[9];
	word i;


	dbug(1, dprintf("connect_b3_req"));
	if (plci)
	{
		if ((plci->State == IDLE) || (plci->State == OUTG_DIS_PENDING)
		    || (plci->State == INC_DIS_PENDING) || (plci->SuppState != IDLE))
		{
			Info = _WRONG_STATE;
		}
		else
		{
			/* local reply if assign unsuccessful
			   or B3 protocol allows only one layer 3 connection
			   and already connected
			   or B2 protocol not any LAPD
			   and connect_b3_req contradicts originate/answer direction */
			if (!plci->NL.Id
			    || (((plci->B3_prot != B3_T90NL) && (plci->B3_prot != B3_ISO8208) && (plci->B3_prot != B3_X25_DCE))
				&& ((plci->channels != 0)
				    || (((plci->B2_prot != B2_SDLCDnels != 0)
				Oy_res(dword Id, w& i < 22}_FREE_S
	by ms((i = geB3_X25_DCE))= 0;
			& = CALL_DIANSWId &parm25_DCE))= 0;
			& = CALL_DI= _CE_ (plcB3______

				}
		].info + 1);
				d3nection
			   and =%dy cono */
		("lis, ori=%dysstf("conne",o = _OU5_DCE))
				&&lci->pty*/
		 dprintf(= 0;
		,&& plci->State &tf("form_CON_ALERT) {
			Info = ocalInfoCon")););
		sendSCONNECTplcl,
		      _INFNFO_R | CONFIRIRM,
		      Id,
	
		      Number,
		  end_req(rplci);
	ed */
		& plciC;
			red_	/* NCswordnci->vsprotbreak;->State =
	ed nge; .length =0 = &a-TRIEVE_REQO8208) & ||2DIS_PENDINO8208) & ||3_

				}
		&parmge;			else i> 0;
				if (ch R_ORIGINA PLCPVC /* Basic&parmge;		RD(&(2]DIS_mge;		RD(&(3]SPOOFING_REQUIRESE Numbemge;		RD(&(3];_REQUIRESE 1umbemge;		RD(&(2];_REQUIRer Ndnfo);
	2,ARSE				send_reeak;->RESEONG_MESSAGE_FORci);
					}
					e&parmge;		RD(&(_Mask1) break;->State =,
	N_D_BIT;_REQUIRer Ndnfo);
	d_mask_mge;			else i] = , &mge;		RD(&(lci, LLC,						break;elOSA[i] =TRIEVE_REQO8208) & ||5_

				}
		&pari->pty*/
				if (plci->NL.Id && !plci->nl_remov;
	word w;
	word k;
			}
			SS(T30df(ap *)(plci-;
	wordAPD
	eaturcci_fr)
			d w;
	word_or i, LLC,	ax_control_bits, k;
			}
			SS(T30df(ap *)(plci-;
	wordAPD
	eaturcci_fr)
	ontrol_bits,_or i, LLC,	se S_R;
	word w;
	word & T30d				if _BIT_MORE_DOCUMENTSSPOOFINannels !ax_control_bits, & T30dFEATURE_BIT_MORE_DOCUMENTSSSPOOFING_REQUIR	el k;offsetof(T30df(ap,l rivers			6				send_ax_feature_bitsrplci->cr_enqu		&parmge;			else i>= 4req == S_3PTY_ENDw k;
			}
			Smge;		RD(&(3]Sr_enqu		;
	if w = ((by01) !	{
	_mask_S(T30df(ap *)((plci-;
	wordAPD
	eaturcci_fr))ciC;solunate/= ((by01)(a, plci,S_3PTY_END	S(T30df(ap *)((plci-;
	wordAPD
	eaturcci_fr))ciC;solunate/=3PTY_END		;
					_S(T30df(ap *)((plci-;
	wordAPD
	eaturcci_fr))ciC;solunate/= ~T30dRESOLUcai[1R8_0770_;

ppl->|3PTY_END		* wrongf w = ((by01) ? T30dRESOLUcai[1R8_0770_;

ppl :t(a,r_enqu		;_ax_feature_bitsrplci->cr_enquilci = plcv;
	word w;
	word appl(T30d				if _BIT_l_commang_ree ==  T30d				if _BIT_MORE_DOCUMENTSSr_enqu		;
	ifw = ((by02)			InFax-p].info)r_CONF_BEGIN:  plcv;
	word w;
	word appT30d				if _BIT_l_commang_ree =r_enqu		;
	if w = ((by04)ase S_CONF_Bto->Id))/ p].iot othts oocucontBEGIN:  plcrongB3_Xa < rnudburol_rcontrol_, & MANUdf(TURERdFEATURE_FAX_MORE_DOCUMENTSSSPOOFIN,S_3PTY_END	;
	word w;
	word appT30d				if _BIT_MORE_DOCUMENTScr_enquilci = plcv&parmge;			else i>= 6SPOOFIN,S_3PTY_END	w k;
			}
			Smge;		RD(&(5]Sr_enqu		;apter -
				 w) !	{
	T30df(ap *)((plci-;
	wordAPD
	eaturcci_fr))cidata_dbug(1SPOOFIN,SS_3PTY_END		S(T30df(ap *)((plci-;
	wordAPD
	eaturcci_fr))cidata_dbug(1 = 2;
			
	bytenqu		;_ax_feature_bitsrplci->cr_enquiluiry = f		;apter a < rn_fo)) {
	1);ve &_	/* NCs & (1L << msg)and FAX_SUBy mP_PWDSSPOOFIN,ScrongB3_X
			}
			Smge;		RD(&(5]S = ((8by0))ase P);ve &  mP/SUB/PWD eND;
		EGIN:  plcv_3PTY_END		& plciC;
			red_	/* NCswordncapp(1L << msg)and FAX_SUBy mP_PWDScr_enquiluir = f		;apter a < rn_fo)) {
	1);ve &_	/* NCs & (1L << msg)and FAX_NON			NDARDSSPOOFIN,ScrongB3_X
			}
			Smge;		RD(&(5]S = ((4by0))ase P);ve & non-stfndalen;
, ch, pl eND;
		EGIN:  plcv_3PTY_END		& plciC;
			red_	/* NCswordncapp(1L << msg)and FAX_NON			NDARDScr_enquiluir = f		;a;
	word w;
	word appl(T30d				if _BIT_>State_SUBADDRESS=  T30d				if _BIT_>State_SELng_ree == 3PTY_END			* wronT30d				if _BIT_>State_PASS}
		Sr_enqu		;apter & plciC;
			red_	/* NCswordnca & plciC;
			red_	/* NCsca aciC;
			red_	/* NCswtD;
		if (a->Info_MSPOOFIN,ScrongBer 1L << msg)and FAX_SUBy mP_PWDS				1L << msg)and FAX_NON			NDARDSSSPOOFIN,SS_3PTY_END		_DEACTIVATE:
		mge;		RD(&(_M, mge;			else ord)wrms->len[2];

	APSSPOOFIN,SSat wrong"));
						Info = _WRONG_MESSA	lci;
						}
,SS_3PTY_END		apter & plciC;
			red_	/* NCswordnca & plciC;
			red_	/* NCsca aciC;
			red_	/* NCswtD;
		if (a->Info_MSPOOFIN,ScScrongBer1L << msg)and FAX_SUBy mP_PWDSSPOOFIN,ScSS_3PTY_END		a	;
	word w;
	word appT30d				if _BIT_>State_SUBADDRESS=  T30d				if _BIT_>State_PASS}
		;3PTY_END		a	pter;
	word w;
	word & T30d				if _BIT_>State_P_ree =SPOOFIN,ScSSa	;
	word w;
	word appT30d				if _BIT_>State_SELng_ree =;3PTY_END		a}3PTY_END		awrplcix_p(rplci, 	else ;3PTY_END		apterwi> 00SPOOFIN,ScSSawrpl20;3PTY_END		aS(T30df(ap *)((plci-;
	wordAPD
	eaturcci_fr))cireturon !p_	el k;2;
			
	bytenqu		;_facility_req"));
wadapter->maxEND		aS(T30df(ap *)((plci-;
	wordAPD
	eaturcci_fr))cireturon !p		}
		cix_p(rplci, RD(&(_ + tedadapterD		aS(T30df(ap *)((plci-;
	wordAPD
	eaturcci_fr))ciheap_	ine_	el k;0;3PTY_END		a	el k;offsetof(T30df(ap,lreturon !p) + T30dMAX_ST	cai[1ID_LENGTH;3PTY_END		awrplcix_p(rplc5, 	else ;3PTY_END		apterwi> 00SPOOFIN,ScSSawrpl20;3PTY_END		a(plci-;
	wordAPD
	eaturcci_fr[	el++] k;2;
			
	bytenqu		;_facility_req"));
wadapter->maxEND		a(plci-;
	wordAPD
	eaturcci_fr[	el++] k;cix_p(rplc5, RD(&(_ + tedadapterD		awrplcix_p(rplc6, 	else ;3PTY_END		apterwi> 00SPOOFIN,ScSSawrpl20;3PTY_END		a(plci-;
	wordAPD
	eaturcci_fr[	el++] k;2;
			
	bytenqu		;_facility_req"));
wadapter->maxEND		a(plci-;
	wordAPD
	eaturcci_fr[	el++] k;cix_p(rplc6, RD(&(_ + tedadapterD		apter & plciC;
			red_	/* NCswordnca & plciC;
			red_	/* NCsca aciC;
			red_	/* NCswtD;
		if (a->Info_MSPOOFIN,ScScrongBer1L << msg)and FAX_NON			NDARDSSPOOFIN,ScSS_3PTY_END		a	_DEACTIVATE:
		mge;		RD(&(_M, mge;			else ord)wrms->>len[2];

	APSSPOOFIN,SSatS_3PTY_END		a		].info + 1);
				non-stfndalen;
, ch, pl h = 0miss		/* r       dbug(1use 2nd PLCIND		a(plci-;
	wordAPD
	eaturcci_fr[	el++] k;0;3PTY_END		aa}3PTY_END		aci;
						}
,SStS_3PTY_END		a		pter cix_p(rplc7r (i = 1; pa3			Oy_cix_p(rplc7r RD(&(_Ma pa2SSPOOFIN,SSatS	a(plci-nsfword w;
	word k;
			}
			Scix_p(rplc7r RD(&(2]e 2nd PLCIND		a(plci-;
	wordAPD
	eaturcci_fr[	el++] k;;
					cix_p(rplc7r (i = 1e 2nd PLCIND		aacility_req"));
cix_p(rplc7r (i = 1adapter->maxEND		a	a(plci-;
	wordAPD
	eaturcci_fr[	el++] k;cix_p(rplc7, RD(&(_ + tedadapterD		aa}3PTY_END		a}3PTY_END		}3PTY_END	}3PTY_END	i;
						}
,S_3PTY_END			el k;offsetof(T30df(ap,l rivers			6				send_luir = f		;a;
	weature_bitsrplci->crr_enquilci = plcv&par;
	word w;
	word !k;
			}
			SS(T30df(ap *)(plci-;
	wordAPD
	eaturcci_fr)
			d w;
	word_or iSPOOFIN,S_3PTY_END	c Service S(T30df(ap *)(plci-;
	wordAPD
	eaturcci_fr)
			d w;
	word_or ,n;
	word w;
	word,r_enqu		;_ax_feature_bitsrplci->cr_enquilci = plcci = plcreak;
		& ||GOOSreq == S_3PTY_END(plci-;
	wordAPD
	eatur
				}
			byte = plcv&par;
	weature_bitsSPOOFIN,S_3PTY_END	&par;
	wontrol_bits, & T30dFEATURE_BIT_MORE_DOCUMENTSS					}
,S_3PTY_END					/*_ = 0;
				rplci-(		 dprin,n;
	wordAPD
	eatur rplci->dadapterD		req(plci);
						reND	}3PTY_END	i;
						}
,S_3PTY_END					/*_ = 0;
				rplci-(		 dprin,n;
	wadar i_b23r rplci->dadapterD		req(plci);
						reND	}3PTY_ENDci = plcci = plalect = 0;

;
		}
	}
	if (!a) Info= plalect  0;

;
		}
	}
	if (!a) Info= piry = [i] =TRIEVE_REQO8208) & ||plclTP_

				}
		ommand = 0;
			C;
rcci_fr[Numbemge;		
				}
+RS:
				ommand = 0;
			C;
rcci_fr[1umbeUg_real_commandTomma	    GURE:
				aciliwy_req"w);
mge;		
				}q"wpter->maxommand = 0;
			C;
rcci_fr[2
+Rwumbemge;		RD(&(1
+Rwufo = oc		/*_ = 0;
				rplci-(		 dprin,nrtpwordAPD
			   ar rplci->dadapt_req(rplci);
	ed */
ND	&parPLCI;
		}			}
		n		C;
rncc_ALERT;
	/* s0>dadapt_req(rpT, 0);
				ret =false;
				}			{
					Info = _Wtf("SendFacRc"));
	senSCONNECTplcl,
		      _INTY_R | CONFIRM,
	      Id,
	      Number,
	      "w"_res"));
	return false;
}

static b byte facility_res(dword Id, word Number, DIVA_CAPI_ADAPTER *a,
			   PLCI *plci, APPL *appl, API_PARSEncc_, fax_info_change;
	API_PA Infoe req;
	bypi;
	byte pvc[2];

	API_PARSE fax_pa = 0;
	byterms[9];
	word i;


	dbug(1, dprinsprintf(ncc_ k;;_mask_>In>> 16"connect_b3_		Oyncc_= INC_ummy, 0ncc__stf("[ncc_]		if (plci->State == INC_Cummy
			}
			Sngth =0 ORD(&(ss_ ((plci->c		}
		, 0ncc__stf("[ncc_]		| (plcREJ_plci->State =)
				&EC;
			r_xonALERT;
, 0ncc__ch[ncc_])tate =)
				&Exmir_xonALERT)tate =)leanuprncc__dataALERT;
ncc_=;	}
		n		C;
rncc_ALERT;
Nlci-C,;;
				ncc_=;	}
		_req(rpT, 0);
				, 0ncc__stf("[ncc_]		| (plACT_plci->Sta
	otbreak;->State =lACK
	ed nge; .length =1 = &a-TRIEEVE_REQO8208) & ||4= INC_DIS_PEO8208) & ||5_ INC_DIS_PEO8208) & ||7__

				
			apter & plciC;
			red_	/* NCswordnca & plciC;
			red_	/* NCsca aciC;
			red_	/* NCswtD;
		E_REQ_PENDa->Info_MSPOOFIrongBer1L << msg)and FAX_NON			NDARDSSPOOFI_3PTY_Epter -VE_REQO8208) & ||4= INC_DIS_PEO8208) & ||5_SPOOFINanne	Oy_res(dwnsfword w;
	word & T30dNSFd				if _BIT_ENABLEdNSFSPOOFINanne	Oy_res(dwnsfword w;
	word & T30dNSFd				if _BIT_NEGOTIand = SPSSPOOFING_REQUIR	el k;offsetof(T30df(ap,lreturon !p) + T30dMAX_ST	cai[1ID_LENGTH;3PTY_ENTRIEVE_REQ;
	wordAPD
	eatur
				}
<
	byreq == S_3PTY_ENDS(T30df(ap *)((plci-;
	wordAPD
	eaturcci_fr))cireturon !p_	el k;0;3PTY_ENDS(T30df(ap *)((plci-;
	wordAPD
	eaturcci_fr))ciheap_	ine_	el k;0;3PTY_ENci = plcreakCTIVATE:
		mge;		RD(&(_M, mge;			else ord)wrms->>len[2];

	APSSPOOFIN,_3PTY_END].info + 1);
				non-stfndalen;
, ch, pl h = 0miss		/* r       dbug(1use 2nd PLCci = plci;
						}
						elsTRIEVE_REQ;
	wordAPD
	eatur
				}
<=
	byreq == S	a(plci-;
	wordAPD
	eaturcci_fr[	el] k;0;3PTY_END	el += 1
+R(plci-;
	wordAPD
	eaturcci_fr[	el];					elsTRIEVE_REQ;
	wordAPD
	eatur
				}
<=
	byreq == S	a(plci-;
	wordAPD
	eaturcci_fr[	el] k;0;3PTY_END	el += 1
+R(plci-;
	wordAPD
	eaturcci_fr[	el];					elsTRIE cix_p(rplc7r (i = 1; pa3			Oy_cix_p(rplc7r RD(&(_Ma pa2SSPOOFIN,SS(plci-nsfword w;
	word k;
			}
			Scix_p(rplc7r RD(&(2]e 2nd PLCI(plci-;
	wordAPD
	eaturcci_fr[	el++] k;;
					cix_p(rplc7r (i = 1e 2nd PLCIacility_req"));
cix_p(rplc7r (i = 1adapter->maxEND(plci-;
	wordAPD
	eaturcci_fr[	el++] k;cix_p(rplc7, RD(&(_ + tedadapterci = plc(plci-;
	wordAPD
	eatur
				}
			byte = plcS(T30df(ap *)((plci-;
	wordAPD
	eaturcci_fr))citrol k;0;3PTY_EN			/*_ = 0;
				rplci-(		 dprin,n;
	wordAPD
	ackr rplci->dadapterreq(plci);
						return fal	}
		n		C;
rncc_ALERT;
	/* s;
				ncc_=;	}
		pter & plcimge;_stf(" & NC	byVALID_SCONNECTplcA	if (SSranne	Oy! & plcimge;_stf(" & NC	bySCONNECTplcA	i_S	InSSPOOFI_3PTY_EpterVE_REQO8208) & ||4=3PTY_EN	"SendFacRc ySCONNECTplcA	iIVE_I,acili0ord>len"1\x70\x69i;
						}
	"SendFacRc ySCONNECTplcA	iIVE_I,acili0ordSplci->ptymge;_cci_fr);r->maxommandmge;_stf(" |= NC	bySCONNECTplcA	i_S	Info= plalectiry = [i] =TRIEVE_REQO8208) & ||plclTP_

				}
		ommand = 0;
			C;
rcci_fr[Numbemge;		
				}
+RS:
				ommand = 0;
			C;
rcci_fr[1umbeUg_real_commandTomma	    GURE:
				aciliwy_req"w);
mge;		
				}q"wpter->maxommand = 0;
			C;
rcci_fr[2
+Rwumbemge;		RD(&(1+wufo = oc		/*_ = 0;
				rplci-(		 dprin,nrtpwordAPD
			   sr rplci->dadapt_req(rplci);
	ed */
ND	i;
							}
		&parmge;			else i> 0; _3PTY_Eptermge;		RD(&(_Mask1) break;->State =lACK,
	N_D_BIT;_REQUIer Ndnfo);
	d_mask_mge;			else i] = , &mge;		RD(&(lci, LLC,}	}
		n		C;
rncc_ALERT;
	/* s;
				ncc_=;	}
			"SendFacRc ySCONNECTplcA	iIVE_I,acili0ord>len"1\x70\x6TRIEVE_REQadar i_b   storlci && plci->SVE_REQadar i_b   storlrplci->cr_enqu	c		/*_ = 0;
				rplci-(		 dprin,nadar i_b   storlcfo= plalectir			_req(rpT, 0)}      "w", Info);
	}
	return falseordAPD
			 ac b byte facility_res(dword Id, word Number, DIVA_CAPI_A_ADAPTER *a,
			   PLCI *plci, APPL *appl, API_PARSEncc_, f(ncc_ k;;_mask_>In>> 16"con[9];
	word i;


	dbug(1, dprac b bncc_conne)";
ncc_=)WORD(msg_b3_		Oyncc_e	Oy_res(dwG) || (plci->Se	Oy_res(dwG) || (plc(plci->State ==
Nanne	Oy_res(dwG) || (pl (plci->State == req"));
	if, 0ncc__stf("[ncc_]		if (plACT_plci->S; _3PTY, 0ncc__stf("[ncc_]		|SCONNECILITY_NALERT_IGNORED;
		if (plci->SCONNECIL>State !NDING)
		    |SCONNECILITY_N)
				&EC;
			r_xonALERT;
, 0ncc__ch[ncc_])tate )
				&Exmir_xonALERT)tate}      "w", Info);
	}
	return false rel
}

static byte connect_b3_req(dword Id, word Number, DIVA_CAPI_AI_ADAPTER *a,
			   PLCI *plci, APPL *appl, API_PARSE *pa;I_PARSEncc_, fax_info_change;
	on[9];
	word i;


	 rel
}

static byprintf(;
				}			{
					Info = _W(ncc_ k;;_mask_>In>> 16"connect_b3_		Oyncc_=eq"));
		}
	}
	if (!a) Info= pter a <ncc__stf("[ncc_]		ifSCONNECIL OUTG_DIS_PEa <ncc__stf("[ncc_]		if (plcci->State ==OUTG_DIS_PEa <ncc__stf("[ncc_]		if (plci->State ==OUTG_DIS_PEa <ncc__stf("[ncc_]		if (plACT_plci->S;uppState , 0ncc__stf("[ncc_]		| (plci->State ==ITY_N)
				&EC;
			r_xonALERT;
, 0ncc__ch[ncc_])tate )
				&Exmir_xonALERT)taTY_NALER, 0ncc_[ncc_].data_p"Se		/_CAPI_AIB3_X25_DCE)O8208) & ||plcTRANSnfo	InSo= plINC_DIS_PEO8208) & ||plcT30So= plINC_DIS_PEO8208) & ||plcT30_WITH_EXTEN			cS___

				}
		DIS_PESERVI rele=s;
				ncc_:
				ommand			}

				rplci->creq(rplci);
	ed */
		i;
							}
		)leanuprncc__dataALERT;
ncc_=;	70\x6TRIEVE_REQO8208) & ||2DIS_PENDINO8208) & ||3_

		 plci->Snge; .length =0 = &a-		&parmge;			else i> 3SPOOFING_REQUIRer Ndnfo);
	d_mask_mge;			else i] = , ;
					S&(mge;		RD(&(lcii, LLC,						break;	n		C;
rncc_ALERT;
Nlci-C,;;
				ncc_=;	}
	ir			_req(rpT, 0)}     ("SendFacRc"));
	senci-CCONNECTplcl,
		      _INTY_R | CONFIRM,
	      Id,
	      Number,
	      "w"_res"));
	return false rel
}

static b byte facility_res(dword Id, word Number, DIVA_CAPI_A_AADAPTER *a,
			   PLCI *plci, APPL *appl, API_PARSEncc_, fE fax_par(ncc_ k;;_mask_>In>> 16"con[9];
	word i;


	 rel
}

static b bncc_conne";
ncc_=)WOnnect_b3_		Oyncc_= INC_& plciC;
			red_	/* NCswordnci->vslc(plci-;
	wordAPD
	eatur
				}
		>vslc(plci-mge;_stf(" elatedfo= pter L.Id
			    || (((plci->B3_prot != B3_T90NL) && (plci->B3_prot != B3_ISO8208) && (plci->B3_prot !I_AIB3_X25_DCE)OSDLCDnels != 0)
				Oy_res(dword Id, w& i < 22}_FREE_S
	by ms((uppState printf(= 0;
		 |= = CALL_DI= _CE_ (plcB3, 0)}  Iacility_req"));
MAX_CHAONNL>StaR			In			}
				iincI rerncc__tD;
		_]	!=s;
				ncc_:daptefo= pter));
MAX_CHAONNL>StaR			In= INC_Cummyprintf(
				&&)printf(
				&&--;	}
	aciliq"));
MAX_CHAONNL>StaR			In	-pT,dapte}
				iincI rerncc__tD;
		_]	=}
				iincI rerncc__tD;
		_
+RS = &a-
				iincI rerncc__tD;
		MAX_CHAONNL>StaR			In	-pT]ci->vsprotncc__freec bs[4]ercci_frsALERT;
ncc_=;	70\x
	if (plci)
	{
		if ((plDIS_PENDIN
	{
		if 			plci->S)		if (plci-(
				&&) {70\x6TRIEVE_REQ
	{
		if 			plci->S)	G_REQUI("SendE_REQ_PEND,POOFINannesendf(appl,
I,POOFINannesepport0xffffL,POOFINannese0,POOFINannesed)parm;_mask3	add_p3d_p4			d			du, ASSIGN, DSndE_REQ_PEND,enci-CCONNECTI,aciort0xffffL,i0ord Num0i, LLC,}	}
		E_REL.Id &&ALERT)tate =NDING)
		    |((pl, 0);
				reRONG_STATE;
	pter a < rnudburol_rcontrol_, & MANUdf(TURERdFEATURE_FAX_PAtaR	= _WROSS				I_AIB3_X25_DCE)O8208) & ||4= INC_DIS_PEO8208) & ||5_SPOOFrongB3_Xa <ncc__stf("[ncc_]		if (pli->State == req}			}
		ncc__freec bs[4]ercci_frsALERT;
ncc_=;	70\x	n		C;
rncc_ALERT;
NlEg_re,;;
				ncc_=;	ate =NDING)	rplci = ncc__stf("[ncc_]		| (pl, 0);N			/*_ = 0;
				rplci-(		 dprin,n;
	w rel
}

sta rplci->dadapt_req(rpT, 0);
				ret =    "w"_res"));
	return false ata_tic byte connect_b3_req(dword Id, word Number, DIVA_CAPDAPTER *a,
			   PLCI *plci, APPL *appl, API_NCPTERncc__pti->ng_reaO82DESC * ata;I_PARSE *pa;I_PARSEncc_, fE fax_par([9];
	word i;


	 ata_tic byprintf(;
				}			{
					Info = _W(ncc_ k;;_mask_>In>> 16"con].info + 1);
				ncc_conne dprinconne";
ncc_dprintf("ponnect_b3_		Oyncc_=eq"));
		}
	}
	if (!a) Info= pter a <ncc__stf("[ncc_]		ifSCONNECIL OUTG_DIS_PEa <ncc__stf("[ncc_]		if (plACT_plci->S;uppState /* 
		use ata	EGIN: ncc__pti .leR, 0ncc_[ncc_]>dadapimbemgc__pticidata_out
+Rmgc__pticidata_p"Se		/ITY_NALERia paMAX_g_reaO8SPOOFIi -paMAX_g_reaO8ITY_N ata	.leRmgc__pticiDBci_fr[_]>dadap ata->Npl;
					rplci->numpter L;
					S(ngth =0 ORD(&dPLC	{
	
					S(nDING)msg_ =_
		us)_SPOOFrongB3_XL;
					S(ngth =0 ORD(&dPL<{
	
					S(nDING)msg_ =_
		us)_
+Rsizeof(nDING)msg_ =_
		us)_SPOOF		
			a ata->Pe=s;
					S(long)(*(te conn	S(ngth =0 ORD(&dP=;	ate */
		i;
					a ata->Pe=sTransmirBci_frSetdFacRc *te conn	Sngth =0 ORD(&ddadap ata->L				}
		
			}
			ngth =1 ORD(&ddadap ata->		}

				appl}
			ngth =2 ORD(&ddadap ata->F:
					appl}
			ngth =i, CPN, ss_pa(mgc__pticidata_p"Se		/)++;	ate R_ORIGINA PLCdeliver
			 fiug(1ate/answer dir ata->F:
			= ((by04)						}
		&mbemgc__pticidata_ackrout
+Rmgc__pticidata_ackrp"Se		/ITY_NNALERia paMAX_g_reaACKSPOOFINi -paMAX_g_reaACK
	ed 	mgc__pticiDataAckatedNpl;
				 ata->Npl;
	
	ed 	mgc__pticiDataAckated		}

				 ata->		}

	
	ed 	(mgc__pticidata_ackrp"Se		/)++;		d */
ND	SERVI ataALERT ss_pacreq(rplci);
	ed	ret =reakCTmand "));
	ifLERT STATE;
	pter L;
					S(ngth =0 ORD(&dPLC	{
	
					S(nDING)msg_ =_
		us)_SPOOFrongB3_XL;
					S(ngth =0 ORD(&dPL<{
	
					S(nDING)msg_ =_
		us)_
+Rsizeof(nDING)msg_ =_
		us)_SPOOF		
			aTransmirBci_frFreedFacRc ;
					S(long)(*(te conn	S(ngth =0 ORD(&dP==;	ate */
	*/
	calInfoCon")););
	senc_reaO82l,
		      _INFTY_R | CONFRM,
		      Id,
	annesed) Numappl}
			ngth =2 ORD(&dumber,
	  t =    "w"_res"));
	return false ata_tic b byte facility_res(dword Id, word Number, DIVA_CAPDAPTER *a,
			   PLCI *plci, APPL *appl, API_PARSEn;I_PARSEncc_, fE faxNCPTtrolpar([9];
	word i;


	 ata_tic bsprintf(ncc_ k;;_mask_>In>> 16"connect_b3_		Oyncc_= INC_n			appl}
			ngth =0 ORD(&ddadaI_PARSE *msg)
{
	dreed%d)";
nii, LLNCPTtrolmbemgc_ s != _mask
, 0Isk
<< 8efo= pternL<{PENDa-MaxBci_fr		O,
	annePENDa-DataNCPT[n]		ifNCPTtrolm	O,
	anne;
					PENDa-DataF:
		[n]	>> 8PLCIvnDING)Isk
TE;
	rms))
				{
					dbunduse 2nd PENDa-DataNCPT[n]		->vsprotpter)
				&Ecan_xonALERT;
, 0ncc__ch[ncc_]))	G_REQU)
				&EC;
			r_xonALERT;
, 0ncc__ch[ncc_])tate }ate )
				&Exmir_xonALERT)taTY_NALER,ENDa-DataF:
		[n]	&|4= {eak;	n		C;
rncc_ALERT;
Nlc_reaACK,;;
				ncc_=;	}
		_req(rpT, 0);
				ret =    "w"_res"));
	return false bsetatic byte connect_b3_req(dword Id, word Number, DIVA_CAPIDAPTER *a,
			   PLCI *plci, APPL *appl, API_PARSE *pa;I_PARSEncc_, r([9];
	word i;


	 bsetatic byprintf(;
				}			{
					Info = _W(ncc_ k;;_mask_>In>> 16"connect_b3_		Oyncc_=eq"));
		}
	}
	if (!a) Info= k;
		} /DIS_PEO8208)  STATE;
appl,plci->B3_p:E;
appl,plci->B3_p:TY_NALER, <ncc__stf("[ncc_]		ifSCONNECIL OUT			}
		n		C;
rncc_ALERT;
->RESEO s;
				ncc_=;	}
			"SeICE, 0);
				sen
		}
	}GOOState }ate OT_SUPPORappl,plcTRANSnfo	In:TY_NALER, <ncc__stf("[ncc_]		ifSCONNECIL OUT			}
		c		/*_ = 0;
				rplci-(		 dprin,nrbsetatic rplci->dadapt
		}
	}GOOState }ate OT_SUPPOR	ret =reak;setati mustak;sul dpr anrbsetatic re/= rbsetaticILECTOR ("SendFacRc"));
	senRESEOTplcl,
		      _INTY_R | CONFIRM,
	      Id,
	      Number,
	      "w"_res"));
	return false bsetatic b byte facility_res(dword Id, word Number, DIVA_CAPI_ADAPTER *a,
			 PLCI *plci, APPL *appl, API_PARSEncc_, f([9];
	word i;


	 bsetatic bsprintf(ncc_ k;;_mask_>In>> 16"connect_b3_		Oyncc_= INC_k;
		} /DIS_PEO8208)  STATE;
appl,plci->B3_p:E;
appl,plci->B3_p:TY_NALER, <ncc__stf("[ncc_]		if (plRES>State ==OUTc		}
		, 0ncc__stf("[ncc_]		|SCONNECILITY_N	n		C;
rncc_ALERT;
->RESEOaACK,;;
				ncc_=;	}
		_req(rpci->cr_en}ate OT_SUPPOR	ret = "w", Info);
	}
	return falseordAPD
			 t90 ac b byte facility_res(dword Id, word Number, DIVA_CAPPI_ADAPTER *a,
			 PLCI *plci, APPL *appl, API_PARSEncc_, fax_info_change;
	API_PA Infoe [9];
	word i;


	dbug(1, dprt90 ac b printf(ncc_ k;;_mask_>In>> 16"connect_b3_		Oyncc_= INC_ummy, 0ncc__stf("[ncc_]		if (plACT_plci->S; _3PTY, 0ncc__stf("[ncc_]		|SCONNECILITY_	reRONG_ ummy, 0ncc__stf("[ncc_]		if (plci->State == INC_C, 0ncc__stf("[ncc_]		|SCONNECILITs_pacreak;->State =lACK
	ate R_Ongth =0 ==0A PLCord 3_req col m.info[0defini1ate!/answer dirngth =0 ORD(&d {eak;	nge; .length =1 = &a-	&parmge;			else i> 0; _3PTY_Eptermge;		RD(&(_Mask1) break;->State =lACK,
	N_D_BIT;_REQUIer Ndnfo);
	d_mask_mge;			else i] = , &mge;		RD(&(lci, LLC,}	}
	}	}
	n		C;
rncc_ALERT;
	/* s;
				ncc_=;	}
	_req(rpT, 0)}      "w", Info);
	}
		return false    "w_b   yte connect_b3_req(dword Id, word Number, DIVA_CAPIDAPTER *a,
			   PLCI *plci, APPL *appl, APPARSE *parms)
{
E fax_pa = 0;
tel, fax_info_chbp_p(rplc7,"ponnect (plc}
			appl,q"));
		}
	}
	if (!			Info = _W(t =fals,q"));[9];
	word i;


	    "w_b   y[%d],dprinonne Telnonne NLnonne LCI nonne sstf("conne",o = _app			else ornDING)IsornDING)tellci->pty*/
		 dprintfLCI *pNDING)
>State &tf("fo[9];
	word i;


	PDINStf("conne",_PENDIN
	{
	tf("foacility_req"));
7,dapte}bp_p(rplcir (i = 1;	->vsproR_ORIGINAnecno )
				& is op"S,cno B3			   and 3 protanswe
	if (plci)
	{
		if ((plci->State == IDLE) || (plci->State == IS_PENDING)
		    || (plci->State ==OUTG_DIS_PENDING)
>State & (plci->SeIS_PENDIN)
				&& IS_PENDIN>NL.Id && !plci-_3PTY!= IDLE))
		{
			Info = _WRR_ORIGINAm.info[0dbug(1 

		fill}bp_p(rpl po = 0;tansweONG_ ummyapp			else 		OyCTIVATE:
		app		pi_parse(&parmsapp			else or"wrms->lenbp_p(rplSuppState != IDLE))
		{
			Info = _WRONG_M	reRONG_STATE;
	pter NDING)
		    || (plci->State == S_PENDING)
		    || (plci->State )ase >Id))aler_Bton_ unb

		to the netpark,/answer{                                                                  D;
	.g. Qsi/* r RBS* r Cornet-N* r x.in msgcause of US ciortEXTd				if tat_

		 plci->S	"SendFacRc y msg))_Bal_c,
		      _ turn (ec_reqd Num0x2y02);          ord w;
lrolling Us  "w", I0, LLC,}	}
		E_REG)
		    |((plci->SCONNECIL>Statetate =NDING)PEND;
				rplci->)lear_c_ =d_mask	wornfo);
	d_mask_PENDa->Info_ii, LLC,dump_c_ =d_mask 0);
				senacility_req"));
m
	wa		rpdapte}    rel
}

st the othts a		r /* Messa{                            ord quasi anl
}

st        /* Basic&part		r_c_ =d_mask	wornfo);
	iSSPOOFIN,	"Send&a		rifo[0])cir,enci-CCONNECTI,aci,i0ord Num_OTHER_->appSCONNECIL , LLC,}	}
	}	2nd PE__sa&& appyappord>len&DIS_PESa&&d app , LLCtel	=}
				itel, fof US ciortEXTd				if tat_

		{70\x6TRIEtelif (plc 0;
		 ord w;
lrolpr GATEby this DAPTER/

		 plci->Summy, 0AdvSig
		AEND;	OyC 0AdvSig
		AEND;!
				rSPOOFING_REQUIR[9];
	word i;


	Ext_Ctrllpr GATE1ntf("format wrong"));
			a) Info= pl						break;
				 f (plc 0;
		 ord w;
lrolNOTlpr GATEby this DAPTE?ER/

		 plci->Summy, 0AdvSig
				In=POOFING_REQUIR[9];
	word i;


	Ext_Ctrllpr GATE2ntf("format wrong"));
			a) Info= pl						b
				  (pact;ve & the trolcplicit invocation[9];
	word i;


	Ext_Ctrllc		/*ntf("formatummyAdvCrolcS0x300E(uest(Id, Number0SSPOOFIN,_3PTY_END].info + 1);
				Erroolpr trolcp08)ceduprintf("fformat wrong"));
			a) Info= pl	Cci = plci;
	=TRIEVE_REQspoof&d app	if 	POOFse Sl_coIRIL      ait until trolcpis act;v		EGIN:  pl_3PTY_ENDVE_REQspoof&d app	i AWAITse S_msg))_B;3PTY_ENDVE_REQ = 0;
				rplci->inBLOCK			Inf 	}
		ek othts 	rplci-s	EGIN:  plcommand			}

				rplci->END].info + 1);
				ord inu	=TRItrolcploadedntf("fformatreq(plci);
						reN}o= pl						break;elOSA[i] = (plc 0;
		 ord w;
lrolworpis OFF/answer{70\x6TRIEtelif (plc 0;
		 ord w;
lrolpr GAT,cnee		to k;
		} offER/

		 plci->Summy, 0AdvSig
		AEND;=
				rSPOOFING_REQUIRCrolcIdCIGIN(uest(Idf("fforma
				itel			rplci->ENNDING)	rv_nl			rplci->EN[9];
	word i;


	Ext_Ctrll reD;
	ntf("fformAGE_FORci);
					}
					e[9];
	word i;


	Ext_Ctrll  orC;
			redntf("fformAGE_FOreak;elOSA&parPLCI;
		}			}
		ummyprintf(= 0;
			& = CALL_DI (per->maxommand(= 0;
			= = CALL_DI (p,
		 CALL_DI RIGIN Info= pli;
	=TRIEVE_REQ(= 0;
			& = CALL_DIINer->maxommand(= 0;
			= = CALL_DIIN,
		 CALL_DIANSWId;	}
					/*_ = 0;
				rplci-(		 dprin,n    "w_b  rplci->dadapt_req(rplci);
	ed */0)}     ("SendFacRc y msg))_Bal_c,
		      _ turn (ec_reqd Number,
	      "w"_res"));
	return false rnudburol_rc byte connect_b3_req(dword Id, word Number, DIVA_CAPI_AI__ADAPTER *a,
			 PLCI *plci, APPL *appl, API_PARSE rplci-
{
E fax_pa PARSEncc_, fax_info_cham, fax_info_chm_p(rplc5,;I_PARSE rolc;{
	word Info = 0;
chfo = 0;

		fo return falseohi[0] = {0x01, ((by}fo return falselli[0] = {0x01, ((by}fo return false rolc_cai[0] = {0x01, ((b1}fo return falsenu 0;app	i {y}fo return ax_info_chnu 0;p(rpl i {i0or&nu 0;app	}fo _ADAPTv					;APPARSE *parms)
{e [9];
	word i;


	 rnudburol_rc byntf("facility_req"));
5pdapte}m_p(rplcir (i = 1;	->vsprummy
			D}
			ngth =0 ORD(&d;!
	LL__MANUlci, "));
		}
	}
	if (!			Info = _WRONG_   			}

				
			}
			ngth =1 ORD(&ddadm .length =2];_R&parPLCI;
		INC_k;
		} / rplci-> TE;
appl,LL__		sig_			In:TY_NALER,TIVATE:
		a		pi_parse(&parmsao[1], (word)pb>lenm_p(rplSu _3PTY_ wrong"));
						Info = _WRONG_MESsupported */
		trolcp		
			}
			m_p(rplc0 ORD(&ddadapc1;	-m_p(rplc1 ORD(&(ssdadap
			= m_p(rplc2 ORD(&(ssdadapplci)
				{
					if (( _3PTY_(plc}			{
						rplci = &a->NDING)PEND;
				rplci->ommand			}

				_MANUdf(TURERdRplci->ommandm		rplci->in rplci-
{
i->ommandppl;
					rplci->numbE_REG)
		    |LOCAppSCONNECdadapt
->in!= _masknDING)Is
<< 8eca & plci	rplci = Is
| ((8bi, LLC,d9];
	word i;


	ManCMD,princonne";
Id==;	ate pplci)c1;	= 1
S_Pc1;	= 2			Oy_
			<pa2SS _3PTY_Eohi[1] k;;
					;
					c1e 2nd PLlli[fault:
						ommand(= 0;
			= = CALL_DI (p,
		 CALL_DI RIGIN Info= pl_k;
		} / rolcSPOOFING_REQUIappl,0eak;
			 wrong"er Nb1nfo);
	&m_p(rplc3],i0or0f("fforma_REQ_PEND;
						1eak;
			er Numfo);
					adrolc_caif("fforma_REQ_PEND;
	RR_O rnu		 'k;
	} on'	to the trolcp 0x300EMBERSout
sig
		info)EGIN:  plR_Ofirst '* localfo);'MBERS:this fIVATION, ther GATEEGIN:  p					2eak;
			ummyAdvCrolcS0x300E(uest(Id, Number0SS _3PTY_EY_ wrong")RESOU_CE_		caRfo= pl	Cci = plci;
	=_3PTY_EY_ wrong"er Nb1nfo);
	&nu 0;p(rpl,i0orB1ndf(appl,
LOCApf("fformatlli[fault:x10f 	}
		e		 o		i trolcp tREQm)EGIN:  pl}"fforma_REQ_PEND;
	iry = f	E_REG)
		    |LOCAppSCONNECdadapt>ommandmrnudburol_r plci->cr_enquommand			}

				_MANUdf(TURERdRplci->>ommandm		rplci->in rplci-
{
i->>ommandppl;
					rplci->lci->SummyPLCI;
		}	ING_REQUIRer Numfo);
	LL		allif("fformaer Numfo);
		H		adhif("fformaer Numfo);
					add_p(rplci, UID, "\x06\x43\x61\x70\x69\x32\x30"ER *a,
	sig_req(rplci, Aak;
			ummy! rolcSPOOFIN	_3PTY_EY_ wrong"er Nb23nfo);
	&m_p(rplc3]f("fformatummyPLCI;
		}	IN,S_3PTY_END	n		C;
rncc_ALERT;

	sig_re0f("fforma			"SeICE, 0);
				sen pl}"fformaci = plcreakPLCI;
		}	IN,_3PTY_END].info + 1);
				ori=onne spoofconne";

		,&& plcispoof&d app)f("fformatummyVE_REQspoof&d app	if 	POOFse Sl_coIRIL 		}	IN,S_3PTY_END	PE__sa&& appya;p(rpl,id)pb>len&DIS_PESa&&d app , LLC
i->>ommandspoof&d app	i AWAITse SMANUdpSCO, LLC
i->>ommand = 0;
				rplci->inBLOCK			Inf 	}
rej
st othts breameanwhi
		EGIN:  plcvommand			}

				rplci->END		"SeICE, 0);
				sen pl	req(plci);
						reNDci = plcv&par
			==k1) _3PTY_END	x32\x30"ER *a,	 CALl_cre0f("fforma	ci = plcvi;
	=TRIE!
		) _3PTY_END	x32\x30"ER *a,LISTENLl_cre0f("fforma	ci = plcv	"SeICE, 0);
				sen pci = plci;
						}
						elscalInfoCon")););	););
	senMANUdf(TURERdR,
		      _INFNF	NFTY_R | CONFRNF	NFTY_R |     Id,
	
		NFTY_R |"d) NumLL__MANUlci,n rplci-mber,
		  endpl	req(plc2			sen pci = pl					break;elOSA[i] =e;
				}		{
						Info = eturn falsappl,LL__IL__CTRL:TY_NALERk;
				}
	_3PTY_ wrong"));
						Info = _W(	ESsupported */
		ALER,TIVATE:
		a		pi_parse(&parmsao[1], (wordb>lenm_p(rplSu _3PTY_ wrong"));
						Info = _WRONG_MESsupported */
		break;m_p(rplc0 ORD(&(ssdadapommand			}

				_MANUdf(TURERdRplci-ommandm		rplci->in rplci-
{
i-ommandppl;
					rplci->numALERbreak=,	 CALl_c_

				}
		DIS_PEb_)
				& 			{
C
				&(&m_p(rplc1 		  endNECT:
setat)
				&Eid_esc"ER *a,DIS_PEb_)
				&\x70\x6TRIEVE_REQspoof&d app	if 	POOFse Sl_coIRIL 		}	I{{
i->>ommandspoof&d app	i 	 CALl_c,
	AWAITse SMANUdpSCO, LLC
iommand = 0;
				rplci->inBLOCK			Inf 	}
rej
st othts breameanwhi
		EGIN:  pommand			}

				rplci->EAGE_FORMAT;
				elOSA[i] =TRIEbreak=,LAWLl_c_

				}
		DIS_PEpl;
						rplci->cr_en
				,r NssALERT;
FTY
	&m_p(rplc1 		  enx32\x30"ER *a,	/* s0>dadap	"SeICE, 0);
				seTRIEbreak=,HANGUP
		}			}
		ummyprintf*/
				if (plci->NL.Id && !plci->nl_removummyprintf(
				&&)		}	ING_REQUIRacilincc_ k;1;emgc_ ;
MAX_NCPTE+;1;emgc_pter->maxE						elspter a <ncc__					ncc_]		ifnDING)Isk
B3_Xa <ncc__stf("[ncc_]		ifSCONNECIL  		}	IN,S_3PTY_END	P 0ncc__stf("[ncc_]		| (plci->State ==ITY_NNNNNN)leanuprncc__dataALERT;
ncc_=;	}
						n		C;
rncc_ALERT;
Nlci-C,;;
				ncc_=;	}
			pl}"fformaci = plci = plNECT:
		d &&ALERT)tate =	n		C;
rncc_ALERT;
REMOVq(rplci, S_v	"SeICE, 0);
				sen
				elOSAeturn falsappl,LL__(rplCTRL:TY_Nse >ig
		info)ord w;
A PLClooppact;ve ate/B-)
				& answer directio		}
	_3PTY_ wrong"));
						Info = _W(	ESsupported */
		ALERao[1], (w( _3PTY_(plcnd			}

				_MANUdf(TURERdRplci->ommandppl;
					rplci->numb,r NssALERT;
FTY
	m=;	}
			32\x30"ER *a,(rplCTRL(rplci, S_SERVICE, 0);
				seelOSA[i] = wrong"));
						Info = _WRONG_MEeturn falsappl,LL__RXTd	TRL:TY_Nse act;ve ate/ord w;
A PLC bs[4]er/transmir 0;tB-)
				& answer directio		}
	_3PTY_ wrong"));
						Info = _W(	ESsupported */
		ALERao[1], (w( _3PTY_(plcnd			}

				_MANUdf(TURERdRplci->ommandppl;
					rplci->numb,r NssALERT;
FTY
	m=;	}
			32\x30"ER *a,DSPlCTRL(rplci, S_SERVICE, 0);
				seelOSA[i] = wrong"));
						Info = _WRONG_MEeturn falsappl,LL__ADVpSCDEC:alsappl,LL__DSPlCTRL:TY_Nse TELlCTRL 	rplci-s	to k0x300EMnte/stfndalenadar iconts: answerse S		/* n/off, 		}
set micro volume,plc 0;
		 micro vol. answerse h	}
set+lc 0;
		 spurn
		volume,p bs[4]er+transm. gain,answerse h	}
sdree* n (hookh = 0off),n  t mixts 	rplci-         /* /
		ALER			}

			=,LL__ADVpSCDEC
		}			}
		ummy!, 0AdvCrolc		In= INC_Cat wrong"));
			a) Info= pl	AGE_FORMAT;
					v					ng"e 0AdvCrolc		In			seelOSA[i] 		}			}
		ummyprin	}
		    B3_Xm			else i>= 8SPOOFI    B3_Xm		RD(&(_Ma=lt:x1cSPOOFI    B3_Xm		RD(&(2Ma pa1SSPOOFI_3PTY_Epterm		RD(&(3Ma=ltDSPlCTRL_OLD_SEOaMIXERdCOEFFICIENTSS					}_3PTY_ENpter NDING)tel	!i ADVpVOIClci->State ;!
		 0AdvSig
				In=SPOOFIN	_3PTY_EY_ wrong"));
			a) Info= pl	Ca_REQ_PEND;
	R}"fformaaG)	rv_voice		refr
				}
		m		RD(&(2Ma-RS:
				>Summy, 0	rv_voice		refr
				}
> m			else i] = o= pl	CaaG)	rv_voice		refr
				}
		;
					m			else i] = :
				>Summy, 0	rv_voice		refr
				}
> ADVpVOICldCOEF_BUFFERdSIZE o= pl	CaaG)	rv_voice		refr
				}
		ADVpVOICldCOEF_BUFFERdSIZE:
				>Sacility_req"));
aG)	rv_voice		refr
				}adapter->maxENaG)	rv_voice		refrbci_fr[_]
		m		RD(&(4 + tedadapterTRIEVE_REQO1_;
, ch, pl &rB1ndf(appl,
VOIClcr->maxENarv_voice	write		refsALERT;
ADVpVOICldWRITE_UPD Inf("fforma_REQ_PEND;
						b
				 pterm		RD(&(3Ma=ltDSPlCTRL_SEOa
		cinfoAMETERSS					}_3PTY_ENpter! a < rnudburol_rcontrol_, & MANUdf(TURERdFEATURE_
		cinfoAMETERSSSPOOFIN	_3PTY_EY_ wrong")ault:
			Info = _FACILITY_Nl	Ca_REQ_PEND;
	R}"END;
	RVE_REQDTMF:p(rame 0;r
				}
		m		RD(&(2Ma-RS:
				>SummyVE_REQDTMF:p(rame 0;r
				}
> m			else i] = o= pl	CaVE_REQDTMF:p(rame 0;r
				}
		;
					m			else i] = :
				>SummyVE_REQDTMF:p(rame 0;r
				}
> 
		cinfoAMETER_BUFFERdSIZE o= pl	CaVE_REQDTMF:p(rame 0;r
				}
		
		cinfoAMETER_BUFFERdSIZE:
				>Sacility_req"));
VE_REQDTMF:p(rame 0;r
				}adapter->maxENVE_REQDTMF:p(rame 0;rbci_fr[_]
		m		RD(&(4 + tedadapterTRIEVE_REQO1_;
, ch, pl &rB1ndf(appl,

		cRer->maxENDTMF:p(rame 0;rwrite 0);
				sen peturn fals	pl					break;	v					ng"				;APd */
ND	&parPv							}
	_3PTY_ wrong"));
						Info = _W(	ESsupported */
		ALERao[1], (w( _3PTY_,r NssAv					;
FTY
	m=;	}
			32\x30"v					;
TELlCTRL(rplci, S_SERVICE, v									seelOSA[i] = wrong"));
						Info = _WRONGG_MEeturn falsappl,LL__OPcai[Sal_comma:TY_NALER,TIVATE:
		a		pi_parse(&parmsao[1], (worddlenm_p(rplSu _3PTY_ wrong"));
						Info = _WRONG_MESsupported */
		ummy
			D}
			m_p(rplc0 ORD(&d/= ~a < rn_fo)) {
	1);ve &_	/* NCs		}
	_3PTY_ wrong")ault:
			Info = _FACILITY_Nlsupported */
		aciC;
			red_	/* NCswtD;
		if (a->Info_Mp		
			D}
			m_p(rplc0 ORD(&dNG_MEeturn fafalsdefault:TY_N wrong"));
						Info = _WRONG_MEeturn f0)}    R ("SendFacRc"));
	senMANUdf(TURERdR,
		      _INTY_R | CONFTY_R |     Id,
TY_R |"d) NumLL__MANUlci,n rplci-mber,
		   "w", Info);
	}
		return false rnudburol_rc b byte facility_res(dword Id, word Number, DIVA_CAPI_A_ADAPTER *a,
			   PLCI *plci, APPL *appl, APPARSEindifo[0])cpi;
	byte pvcm_p(rplc3]; fax_info_change;
	A
	byte pvc[2];

	API_PARSE fax_pa = 0;
	byterms[9];
	word i;


	 rnudburol_rc b "f("ponnectyappc0 O
				}
	(plci-_A_A->Stappc1 O
				}
	(plci-_A_A->St
			D}
			mppc0 ORD(&d;!
	LL__MANUlci,
		INC_req(plci);
				t =rndifo[0])p		
			}
			mppc1 ORD(&dNG_k;
		} /rndifo[0])
		IN
sappl,LL__NEGOTIand B3:TY_ directio		}
	eturn f0)pter L.Id
			    || (((4rot != B3_ISO8208) && (5rot !I_AI
			 & plcimge;_stf(" & NC	byNEGOTIand B3_S	InSSPOO_3PTY[9];
	word i;


	      stf(" acilNEGOTIand B3 p(rame 0;intf("ffoeturn f0)}  NALER,TIVATE:
		appc2r RD(&(_Menmppc2r 1], (word)>lenm_p(rplSuPOO_3PTY[9];
	word i;


	      dbug(1 inlNEGOTIand B3 p(rame 0;intf("ffoeturn f0)}  Nnge; .lem_p(rplc1  f0)	el k;offsetof(T30df(ap,lreturon !p) + T30dMAX_ST	cai[1ID_LENGTH;3PTTRIEVE_REQ;
	wordAPD
	eatur
				}
<
	byreq _3PTYS(T30df(ap *)((plci-;
	wordAPD
	eaturcci_fr))cireturon !p_	el k;0;3PTYS(T30df(ap *)((plci-;
	wordAPD
	eaturcci_fr))ciheap_	ine_	el k;0;3PT}  NALER,TIVATE:
		mge;		RD(&(_M, mge;			else ord)wrms->>len[2];

	APSSPOO_3PTY[9];
	word i;


	non-stfndalen;
, ch, pl h = 0miss		/* r       dbug(1use 2nd	reRONG_STATE;
	pterVE_REQ;
	wordAPD
	eatur
				}
<=
	byreq ==(plci-;
	wordAPD
	eaturcci_fr[	el] k;0;3PTY	el += 1
+R(plci-;
	wordAPD
	eaturcci_fr[	el];				pterVE_REQ;
	wordAPD
	eatur
				}
<=
	byreq ==(plci-;
	wordAPD
	eaturcci_fr[	el] k;0;3PTY	el += 1
+R(plci-;
	wordAPD
	eaturcci_fr[	el];				pter cix_p(rplc7r (i = 1; pa3			Oy_cix_p(rplc7r RD(&(_Ma pa2SSPOOFI& plcimsfword w;
	word k;
			}
			Scix_p(rplc7r RD(&(2]e 2nd (plci-;
	wordAPD
	eaturcci_fr[	el++] k;;
					cix_p(rplc7r (i = 1e 2nd acility_req"));
cix_p(rplc7r (i = 1adapter->ma(plci-;
	wordAPD
	eaturcci_fr[	el++] k;cix_p(rplc7, RD(&(_ + tedada	reRVE_REQ;
	wordAPD
	eatur
				}
			byte =VE_REQ;
	wedata_ackr
				}
		VE_REQ;
	wordAPD
	eatur
				}te =			/*_ = 0;
				rplci-(		 dprin,n;
	wedata_ackr rplci->dadaeturn fal    "w", Info);
	}
	/*------------------------------------------------------------------*/	/* IDI o		ibaINA IVATION                                            */	/*------------------------------------------------------------------*/	
void o		ibaIN(	InfTY *el, APId, word Number, DIV
	A
	   PLCI ;
PDAPTER *a,;
Pord NMSGham, fE fax_, jpa = 0;
rc;{
	wordchfo = 0;
 Info = 0;
glob			C;
; =rnEMntEcanc	&ECc
{e [9];
	word i;


	%x:CB(%x:Req=%x,Rc=%x,Ind=ne)";3PTYSe->usfr[Num+k1) = ((7fff, eG)IsoreG)R/* seG)Rc, eG)Ind==;	ata	.leR	rplci [;
				e->usfr[Nu]e 2n(plc}			({
						e->usfr[1u]e 2nntEcanc	&ECc
		
d, word N = _FACS	InworNCEL(a=;	at/*i-_AIf newp08)tocoi trol 

		newpXDI is GATd ther ord 3shouldity_ki-_Afullydpr acc faanc	MBERS:IDI opec 

Clook* n o		ibaINA ielSEin	readi-_Aof RcA ielSE PLC bw", Itrols.
	answpter Lend			ple    ||0xff			OyntEcanc	&ECcci->i-_A_A(eG)Rc		if ntEcanc	&ECccu _3PTCc
		eG)Rcdadac1;	-eG)RcC}te =break;eG)R/*te =eG)Rc		->vsproptere->usfr[Num= ((8by0)STATE;
	/*i-	-_AIf REMOVq)r_CONF_Bwas >Idt ther we have	to  ait untili-	-_A bw", ItrolMBERS:Idn  t to zero ar);vls.
		-_AAll othts brw", Itrols3shouldibe ignored.
		-answer dirbreak=,REMOVq
		}			}
		ummyeG)IsSPOOFI_3PTY_E].info + 1);
				oanc	& RCdpr REMOVq)stf("ntf("fformbrw", ORMAT;
					)
				&Eflowword w;
			d &&ALERT)tate =acility_req"));
256adapter->ma_3PTY_Eptera->F:owCrd w;
IdTD;
		_]	==_PENDIN>NL.Id && !plci-	
		aciF:owCrd w;
IdTD;
		_]	=I0, LLC,}	}
		E_REG)>NL.Id && !p	=I0, LLC,pterVE_REQrx_dma_olscriptcil>plc INC_Catd;ve_freecdma_olscriptci"ER *a,DIS_PErx_dma_olscriptcilfo_i("fformDIS_PErx_dma_olscriptcil=I0, LLC,}	}
	*/
		ummyCc
	= OK_FC
		}			}
		aciF:owCrd w;
IdTD;
		ch]ak;eG)I-
{
i->aciF:owCrd w;
SkipTD;
		ch]ak;>vsprot>acichEflowword w;
	ch]a|= N_OK_FCState ==ITY_NNacichEfloww					ch]ak;nDING)Is;	}
		E_REG)>NL.Iql=I0, LLCelOSA[i] 		}			}
		/*i-	--_ACanc	& brw", Itrols3self, ummontrol_Bwas C;
			redi-	--ause of US ntEcanc	&ECc
B3_Xa <F:owCrd w;
IdTD;
		ch]akk;eG)I-			OyeG)Isk
TE;
			aciF:owCrd w;
IdTD;
		ch]ak;rplci->Epter Cc
	= OK);	OyC 0F:owCrd w;
SkipTD;
		ch]) _3PTY_EN].inf3 + 1);
				XDI ord : RCdcanc	&lefaci:0x02, Ch:%02e";
eG)Isorch)f("fformabrw", ORMAT;eturn fal	}
		ptera->chEflowword w;
	ch]a& N_OK_FCState ==er->ma_3PTY_Ea->chEflowword w;
	ch]a&= ~N_OK_FCState ==ITY_NNtpter)
akk;eG)R/*Chlci-	
		E_REG)>NL.Iql=I0, LLCbreak;
				
-	
		E_REG)>NL.Iql=I0, LLC*/
		ummyE_REG)>NL.Iqlci-	
ord w;
		c"ER *a,0,
rcorcha,0,
ci->f("ffo[i] 		}			}
		ummybreak=,N_XONer->ma_3PTY_E)
				&Ex_onALERT;
c1e 2nd PLummyE_REG) = 0;
				rplci-lci-	
		ord w;
		c"ER *a,	/* srcorcha,0,
ci->f("ffobreak;
				
-	
	{2nd PLummyE_REG)>NLglob			C;
S					}_3PTY_ENglob			C;
 =_PENDIN>NLglob			C;
; =-	
		E_REG)>NLglob			C;
 =_rplci->ENummyCc
!i A	sig_	OK);_3PTY_EY_eG)Is			rplci->ENDpterVE_REQrx_dma_olscriptcil>plc INC_Cat	atd;ve_freecdma_olscriptci"ER *a,DIS_PErx_dma_olscriptcilfo_i("fform	rmDIS_PErx_dma_olscriptcil=I0, LLC,		R}"fforma}"fforma)
				&Exmir_xonALERT)tate =	
ord w;
		c"ER *a,0,
rcorcha,glob			C;
,
ci->f("ffob						b
				 pterVE_REQData_>IdtS					}_3PTY_EN)
				&Exmir_xonALERT)tate =	
VE_REQData_>Idt plci->cr_enqu		printf*/
XNum k;1;3PTY_EN]ata_	c"ER *a,c1e 2nd PLLummyE_REG) = 0;
				rplci-lci-	
			ord w;
		c"ER *a,	/* srcorcha,0,
ci->f("ffobmAGE_FORci);
					}
					e)
				&Exmir_xonALERT)tate =	
ord w;
		c"ER *a,	/* srcorcha,0,
ci->f("ffobmAGE_FO}	}
	*/
		reRONG_STATE;
	/*i-	-_AIf REMOVq)r_CONF_Bwas >Idt ther we have	to  ait untili-	-_A bw", ItrolMBERS:Idn  t to zero ar);vls.
		-_AAll othts brw", Itrols3shouldibe ignored.
		-answer dirbreak=,REMOVq
		}			}
		ummyeG)IsSPOOFI_3PTY_E].info + 1);
				oanc	& RCdpr REMOVq)stf("ntf("fformbrw", ORMAT;
					ommands32\x3d && !p	=I0, LLC}
			ommands32\x3
 =_rplci-TRIEVE_REQs32\glob			C;
S				_3PTY_glob			C;
 =_PENDINs32\glob			C;
;					ommands32\glob			C;
 =_rplci->ummyCc
!i A	sig_	OK)
Y_EY_eG)Is			rplci->)
				&Exmir_xonALERT)tate =)rd w;
		c"ER *a,0,
rcorcha,glob			C;
,
ci->c				seelOSA[i] 				_3PTY_)
				&Exmir_xonALERT)tate =)rd w;
		c"ER *a,	/* srcorcha,0,
ci->c				seelOS _WRR_
	-_AAgain:dpr acc faanc	MBERS:IDI spec Rc 

		ILECoan'tibe delivereSEin the
	-_Asame o		ibaIN.AAlsoAnecnewpXDI 

		08)tocoi trol GATd ther jump
	-_Adir
st to finish.
		answe
	ifntEcanc	&ECcci_3PTY)
				&Exmir_xonALERT)tate g)to o	pi_o		ibaIN_suffix f0)}    R )
				&Exmir_xonALERT)taTYummyeG)Ii-> TE;
ptere->usfr[Num= ((8by0)i_3PTY= 0;
I
				eG)Ii-	= ((bf;3PTY= 0;
Ch			eG)Ii-C}te =wpter LI
			=,Nlci-Cci->StI
			=,Nlci-CaACKS)m	O,
	-_A_A(acichEfloww					Ch]		ifnDING)Isk) {70\x6TRIEa->chEflowword w;
	Ch]a& N_RX_FLOWd				if _MASK);_3PTY_E].inf3 + 1);
				XDI ord :  : p"Se		/ N-XON Ch:%02e";
Ch)f("ffor}prot>acichEflowword w;
	Ch]a&= ~N_RX_FLOWd				if _MASK			seelOSA>NLind 0);
				seTRIE(eG)RNR
!i 1)m	O,
	-_A_A(acichEfloww					Ch]		ifnDING)Iskm	O,
	-_A_A(acichEflowword w;
	Ch]a& N_RX_FLOWd				if _MASK)( _3PTY_,cichEflowword w;
	Ch]a&= ~N_RX_FLOWd				if _MASK			seE].inf3 + 1);
				XDI ord :  : x3d &&
cikTd N-XON Ch:%02e";
Ch)f("ffoelOS  i;
	=_3PTYs32\ind 0);
				s	reROG)Ii-			rplc  Ro	pi_o		ibaIN_suffix:oe rhi
		( (plci-C;
rin,
TY_R |		if (plci- = 0;
				rplci-,
TY_R |		if(nDING)msg_ =_write	pos
!i nDING)msg_ =_reap_pos,
		INC_j k;;nDING)msg_ =_reap_pos		ifnDING)msg_ =_wrap_pos, ? 0 : nDING)msg_ =_reap_posvsprop>in!= ord NMSGha)(&
	
					S(nDING)msg_ =_
		us)_[j]))ciheaper (i = 1;+a3			t0xfffc
{e dm .l ord NMSGha)(&
	
					S(nDING)msg_ =_
		us)_[j])			sPEND;
	*((
	   Pa)(&
	
					S(nDING)msg_ =_
		us)_[j + tetf("fo[9];
	word i;


	de
		useappy0x%04x)lfowrite=%d reap=%d wrap=%d",o = _aciheaper  rplci-mbnDING)msg_ =_write	pos, nDING)msg_ =_reap_posmbnDING)msg_ =_wrap_pos,);3PTTRIEVE_REQmsg_ =_reap_pos		ifnDING)msg_ =_wrap_pos,STATE;
	nDING)msg_ =_wrap_pos;
	MSG_IN_QUEUEdSIZE:
			VE_REQmsg_ =_reap_pos		 _
+RMSG_IN_OVqRHEALITY_	reRONG_STATE;
	nDING)msg_ =_reap_pos		 j + t
+RMSG_IN_OVqRHEALITY_	reRTRIEVE_REQmsg_ =_reap_pos		ifnDING)msg_ =_write	pos,STATE;
	nDING)msg_ =_write	pos

	MSG_IN_QUEUEdSIZE:
			VE_REQmsg_ =_reap_pos		 MSG_IN_QUEUEdSIZE:
			reRONG_ ummyVE_REQmsg_ =_reap_pos		ifnDING)msg_ =_wrap_pos,STATE;
	nDING)msg_ =_reap_pos		 MSG_IN_QUEUEdSIZE:
			nDING)msg_ =_wrap_pos;
	MSG_IN_QUEUEdSIZE:
		}prop>in,TIVAutdFacRc m);3PTTRIEi ((plci->{		seTRIEaciheaper  rplci-		=,LL_reaO82l)	
			aTransmirBci_frFreedFacRc ;
					S(long)(a		pi_p. ata_tic by.Data==;	ate ].info + 1);
				Errool0x%04x fromeappy0x%04x)";
*a,aciheaper  rplci-tf("ffoeturn f0)} reRTRIEVE_REQli_notify_upd{
	tSTATE;
	nDING)li_notify_upd{
	 plci->cr_enqNECT:
notify_upd{
	(prin,n;
->c				s}
     ("SeI ataALERT ss_SERVICE, 0);
			}
		return void ord w;
		c"DAPTER *a,
	= 0;
 In
	= 0;
 c, falseoh, falseglob			C;
,
NFTY_R | falsen&ECcc, APyte faci;APyte farI-
{
E fax	rplci->nPARSE *parms)
{
E fax_pa PARSEncc_, fId, word Number, DIV
	A
	   PLCI ;
PDAPTERr *a,;
PfalseSSp(rplc]ak;dd_p5			d			d			2			d			du;
PfalseSSsci-ctc]ak;dd_p9			d			d			6			d			d			d			d			d			d""ponnect (plc( _3PT].inf0 + 1);
				A: ord w;
		c,cno (plc}%02e:%02e:%02e:%02e:%02e"a,	/* srcorcha,glob			C;
,
n&ECcc)te =brw", ORM}f([9];
	word i;


	 bq0_ =/out=%d/%d", & plciC;
_ =, & plciC;
_out=)WOnnect_b3_ciC;
_ =
!i nDING)C;
_out=d "));
	ifn&ECci->Stglob			C;
 !i A	sig_ci->StCc
	= A	sig_	OK)SPOO_3PTY[9];
	word i;


	C;
_1brw", ntf("ffobrw", ORMA _WRR_ORanc	& outstfnd		/ r_CONF_Bon theADAPTEaf 0;tsig A	sig_n;
ilol_Bansw}
	_b3_ciC;
_ =
= & plciC;
_ =_			/* i nDING)C;
_outrms)
{
].info + 1);
				ord w;
		c"f("ponPEND;
	nDING)LCI ;
Pa;
	nDING)Lrplci ;f(ncc_ k;,cichEncc_[ch];_R&parCTmand "));
->in!= d_mask_mgc_ ?emgc_ :rch)f
<< 16) s !=_masknDING)Is
<< 8eca aG)Is;	}
nect_b3_citel			}
				i
>State & (pl	 CALHELD):Idn|=tEXTd				if tat;	}
Npl;
				E_REG)>rplci->nu].info + 1);
				Crd w_RC-Ip=%08lx,princ%x,telc%x, entityconne d rplci-conne d;
	_ rplci-conne"_ turnnDING)IsornDING)tellci->ptySig
		 dprintf rplci-mbnDING) = 0;
				rplci-lf("fo[9];
	word i;


	(
				&&conne",_PENDIN(
				&&));3PTTRIEVE_R\x3d && RIGIN 0);
	)"ffobrw", ORMA dirbreak=,REMOVq			}Cc
	= A	sig_	OK)POO_3PTY	32\x30"ER *a,HANGUP s0>dadap	32\x30"ER *a,REMOVq(rplci, SSERVICE, 0);
				s	reRTRIEVE_REQ	rplci-lci-_3PTY	;
		} /DIS_PE	rplci-lci-	_3PTY)ppl,C_HOLD_REQ:		seE].inf	word i;


	HoldRCconne",_Ccc)te =		SSp(rplc1] k;;
				S_HOLDplci->ummyCc
!i OK)
Y_EY_3PTY_E
				i
>State & 	| (pl, 0);NN wrong"0x2y01("ffor}prot>("SendFacRc yault:
			R,
		      _ turn (ec_reqd )>len wro, 3 +SSp(rpl)te =		eturn fals	)ppl,C_RETRIEVE_REQ:		seE].inf	word i;


	RetrieveRCconne",_Ccc)te =		SSp(rplc1] k;;
				S_RETRIEVEplci->ummyCc
!i OK)
Y_EY_3PTY_E
				i
>State & 	|	 CALHELD, 0);NN wrong"0x2y01("ffor}prot>("SendFacRc yault:
			R,
		      _ turn (ec_reqd )>len wro, 3 +SSp(rpl)te =		eturn fals	)ppl,df(ap_R:		seE].inf	word i;


	 wroRCconne",_Ccc)te =		ummyCc
!i OK)= wrong"));
			a) Info= pl("SendFacRc yf(ap_R,
		      _ turn (ec_reqd Number,
	  =		eturn fals	)ppl,dCCONNECTR:		seE].inf	word i;


	CrdAPD
	Rconne/onne/onne/onne"a,	/* srcorglob			C;
,
n&ECcc)te =eRTRIEVE_REQ
		    || (plci->State ==OUT	Ca_REQ_PEND;
TRIEVE_REQ
ig
		 ((plxff	
Y_EY_3PTY_Epter Lglob			C;
 =i A	sig_ci	if(Cc
!i A	sig_	OK)=OUT	Ca_A_A->St!n&ECc
B3_Xbreak=,	 CALl_c_i	if(Cc
!i OK)=S					}_3PTY_EN].inf	word i;


	No morlrIDs/C= 0;Rrea;
iledntf("fforma	"SendFacRc ySCONNECTR,
		      _ tu		t0xffLrn (ec_reqd Num		{
						In)tate =	
VE_R
		d &&ALERT)tate =	=NDING)
		    |((pl, 0);	ma_REQ_PEND;
						b
TRIEVE_REQ
		   ! |LOCAppSCONNEC !NDING)
		    | (plcci->State =PEND;
		"SendFacRc ySCONNECTR,
		      _ turn (ec_reqd Num0f("ffobreak;
				 	}
D-	} act;ve ate/R/

		 plci->SummyCc
!i A	sig_	OK)
Y_EY__3PTY_EN].inf	word i;


	No morlrIDs/X.25 C= 0;Rrea;
iledntf("fforma	"SendFacRc ySCONNECTR,
		      _ tu		t0xffLrn (ec_reqd Num		{
						In)tate =	
VE_R
		d &&ALERT)tate =	=NDING)
		    |((pl, 0);	ma_REQ_PEND;
						b
	"SendFacRc ySCONNECTR,
		      _ turn (ec_reqd Num0f("ffobN, DSndE_REQ_PEND,enState =lACiIVE_I,acili0ord>s>len"1en"1en"1i("fformDIS_PE
		    |((plACT_plci->S("ffobreak;
eturn fals	)ppl,dCCONNECTI,
	RESPONSp:TY_N
TRIEVE_REQ
		   ! | (plci->State ==OUT	CaDIS_PE
		    |((plci->SCCEPT	  =		eturn fals	)ppl,dci-CCONNECTR:TY_N
TRIEVE_REQ
		    || (plci->State ==OUT	Ca_REQ_PEND;
TRIEVE_REQ
ig
		 ((plxff	
Y_EY_3PTY_ENDING)
		    | (plci->State ==ITY_NNN	"SendFacRc yci-CCONNECTR,
		      _ turn (ec_reqd Num0f("ffobreak;
eturn fals	)ppl,			plci_REQ:		seEeturn fals	)ppl,RESUME_REQ:		seEeturn fals	)ppl,dCCONNECTO82l:TY_N
TRIECc
!i OK)
Y_EY_3PTY_E	"SendFacRc ySCONNECTplcl,
		      _ turn (ec_reqd Num));
						Info = i("fformAGE_FORMAT;
					ncc_ k;	{
	ncc_ALERT;
cha,0i("ffor
->in!ciort0xffff) s != d_maskyncc_= << 16)("fforPENDIN(
				&&++;		d 	ummybreak=,N_RESEOer->ma_3PTY_Ea->ncc__stf("[ncc_]		| (plACT_plci->S("ffobE	"SendFacRc ySCONNECTplcl,
		      _ turn (ec_reqd Num0f("ffobN, DSndFacRc ySCONNECTplcACiIVE_I,acili0ord>1en"1i("fforreak;
				
-	
	{2nd PLP 0ncc__stf("[ncc_]		| (plcci->State =PEND;
		"SendFacRc ySCONNECTplcl,
		      _ turn (ec_reqd Num0f("ffobreak;
eturn fals	)ppl,dCCONNECTplci,
	RESPONSp:TY_N
eturn fals	)ppl,dRESEOTplcl:	/*        	"SendFacRc yRESEOTplcl,
		      _ turn (ec_reqd Num0f(R/

		 eturn fals	)ppl,dci-CCONNECTO82l:TY_N
	"SendFacRc yci-CCONNECTplcl,
		      _ turn (ec_reqd Num0f("ffobeturn fals	)ppl,dMANUdf(TURERdR:TY_N
eturn fals	)ppl,PERM_LIST_REQ:		seETRIECc
!i OK)
Y_EY_3PTY_E wrong"));
						Info = _W(	ESN, DSndE_REQ_PEND,enState =lR,
		      _ turn (ec_reqd Number,
	  =		
VE_R
		d &&ALERT)tate =reak;
				
-	
		, DSndE_REQ_PEND,enState =lR,
		      _ turn (ec_reqd Number,
	  =		eturn fals	default:TY_Nlsupported */
		DIS_PE	rplci- k;0;3PT}  NONG_ ummyVE_REQ = 0;
				rplci-lci-_3PTY	;
		} /DIS_PE = 0;
				rplci-lci-	_3PTY)ppl,BLOCK			In:TY_Nlbrw", OR3PTY)ppl,
			MWI	a) In:		seETRIECc
	= OK);R_ORrplci- k0x300Eeilitait foolprdifo[0])pR/

		 plci->Sbrw", ORMAT;
					omma
		d &&ALERT)tate =eturn fals	pR_OGet S0x300Eei Services	EGIN: )ppl,
		SERV_REQ>Stat:		seETRIECc
	= OK);R_ORrplci- k0x300Eeilitait foolprdifo[0])pR/

		 plci->SAGE_FORMAT;
					PU		D}
			&SSsci-ctc6], MASK_TERMINApp_FACABt:
		lci, S_SERVndFacRc yault:
			R,
		      _ turn (ec_reqd )>len0, 3 +SSsci-ct)("fforPEND
		d &&ALERT)tate =eturn fals	)ppl,INTERRyciVERSai[1REQ>Stat:      /* I= 0;rog	   P(rame 0;i        */	ls	)ppl,INTERRyNUMBERS_REQ>Stat:		se)ppl,CF	a) RT>Stat:                  /* C= 0 Forward		/ S		/* p"Se		/ */	ls	)ppl,CF	a)OP>Stat:                   /* C= 0 Forward		/ S	op p"Se		/  */	ls	)ppl,CCBSal_comma_REQ>Stat:		se)ppl,CCBSaDEACiIVand REQ>Stat:		se)ppl,CCBSaINTERROGand REQ>Stat:		seY	;
		} /DIS_PE = 0;
				rplci-lci-		_3PTY_)ppl,INTERRyciVERSai[1REQ>Stat:lci->SSSp(rplc1] k;SaINTERROGand ciVERSai[("fformAGE_FORMAT;)ppl,INTERRyNUMBERS_REQ>Stat:		se>SSSp(rplc1] k;SaINTERROGand NUMBERS("fformAGE_FORMAT;)ppl,CF	a) RT>Stat:		se>SSSp(rplc1] k;Sa	 CALFORWARDI			a) tetate =mAGE_FORMAT;)ppl,CF	a)OP>Stat:		se>SSSp(rplc1] k;Sa	 CALFORWARDI			a)OPtate =mAGE_FORMAT;)ppl,CCBSal_comma_REQ>Stat:		se>SSSp(rplc1] k;Sa	CBSal_commatate =mAGE_FORMAT;)ppl,CCBSaDEACiIVand REQ>Stat:		se>SSSp(rplc1] k;Sa	CBSaDEACiIVandtate =mAGE_FORMAT;)ppl,CCBSaINTERROGand REQ>Stat:		seYSSSp(rplc1] k;Sa	CBSaINTERROGand("fformAGE_FORMAT;
					TRIEglob			C;
 =i A	sig_c

		 plci->S].inf	word i;


	A locaDiversron RCconne/onne"a,	/* srctf("fformbrw", ORMAT;
					nect (plcQ_PEND) _REQ_PEND;
TRIECc
	= ISDN_GUARD REJ)
Y_EY_3PTY_E wrong")ord NGUARD 		caRfo= plreak;
				 TRIECc
!i OK)
Y_EY_3PTY_E wrong") = _LEM	InARY_SERVICldInfo = _FACILITY_Nl}prot>("SendE_REQ_PEND,enault:
			R,
		      _ tu = ((7A_CAPPIIIIIIE_REG)>rplcieqd )>len wro, =_mask3 +SSp(rpl)te =		TRIEber,
 omma
		d &&ALERT)tate =eturn fals	pR_O3pt
			 ferenc	Mp"Se		/ */	ls	)ppl,P			REQ>Stat:		seETRIE (plci-C;l	  dP				In= _REQ_PEND;
r *a, i nDING)C;l	  dP				Inte =		SSp(rplc1] k;nDING)pt

		  PEND;
r
->in!=_maskrnDING)Is
<< 8eca rnDING)Lrplci G)Is;	}
		TRIECnDING)tel)arI-n|=tEXTd				if tat;	}
eETRIECc
!i OK)
Y_EY_3PTY_E wrong"0x300Ef 	}
  ork0x300Eei	EGIN:  pommandC;l	  dP				In				ULL("fformDIS_PEpt

		  l=I0, LLCbreak;
("SendrE_REQ_PEND,_CAPPIIIIIInault:
			R,
		      __CAPPIIIIIIr CONFRNFIIIIIIE_REG)>rplcieNFRNFIIIIIId )>len wro, =_mask3 +SSp(rpl)te =		eturn fals	pR_OEx	rifit C= 0 Trans_fr	p"Se		/ */	ls	)ppl,e =lREQ>Stat:		seE].info + 1);
				E =lRCconne/onne"a,	/* srctf("fforTRIE (plci-C;l	  dP				In= _REQ_PEND;
r *a, i nDING)C;l	  dP				Inte =		SSp(rplc1] k;S_NECdadaptr
->in!=_maskrnDING)Is
<< 8eca rnDING)Lrplci G)Is;	}
		TRIECnDING)tel)arI-n|=tEXTd				if tat;	}
eETRIECc
!i OK)
Y_EY_3PTY_E wrong"0x300Ef 	}
  ork0x300Eei	EGIN:  pommandC;l	  dP				In				ULL("fformDIS_PEpt

		  l=I0, LLCbreak;
("SendrE_REQ_PEND,_CAPPIIIIIInault:
			R,
		      __CAPPIIIIIIr CONFRNFIIIIIIE_REG)>rplcieNFRNFIIIIIId )>len wro, =_mask3 +SSp(rpl)te =		eturn fals	)ppl,dMANUdf(TURERdR:TY_N
].info + 1);
				_Mrnudburol_rcRconne/onne"a,	/* srctf("fforTRIELglob			C;
 =i A	sig_ci	if(Cc
!i A	sig_	OK)=OUT	Cplci->S].inf	word i;


	No morlrIDsntf("fformSERVndFacRc yMANUdf(TURERdR,
		      _ turn (ec_reqdd) NumLL__MANUlci,nyMANUdf(TURERdRum		{
						In)tate =	omma
		d &&ALERT)t  /* af 0;ttrolcpinit d;
	0;
		 orolcp	rplci-s	p"Se		/ */	ls	break;
eturn fals	)ppl,dCCONNECTR:TY_N
].info + 1);
				_CrdAPD
	Rconne/onne"a,	/* srctf("fforTRIELglob			C;
 =i A	sig_ci	if(Cc
!i A	sig_	OK)=OUT	Cplci->S].inf	word i;


	No morlrIDsntf("fformSERVndFacRc ySCONNECTR,
		      _ tu		t0xffLrn (ec_reqd Num		{
						In)tate =	omma
		d &&ALERT)t  /* af 0;ttrolcpinit d;
	0;
		 orolcp	rplci-s	p"Se		/ */	ls	break;
eturn fals	)ppl,PERM_COD_HOOK:                     /* finishei	BERS:HookcILECTOR 	Nlbrw", OR3PTY)ppl,PERM_COD_	 CA:TY_N
].info + 1);
				***Crolc CrdAPD
	P"Se		/ A, Rc		->nne",_Ccc)te =		DIS_PE = 0;
				rplci-		-PERM_COD_	CON>Stat;R 	Nlbrw", OR3PTY)ppl,PERM_COD_A	sig_:TY_N
].info + 1);
				***Crolc A loca A, Rc		->nne",_Ccc)te =		ummyCc
!i A	sig_	OK);_REQ_PEND;
x32\x30"ER *a,	 CALl_cre0f("ffor	"SeICE, 0);
				senDIS_PE = 0;
				rplci-		-PERM_COD_	ALL("fforbrw", OR3PTYpR_ONu 0 C= 0 Referenc	MR_CONF_Bp"Se		/ */	ls	)ppl,C_NCRnaul_REQ:		seE].inf	word i;


	NCRnaulconne/onne"a,	/* srctf("fforTRIEglob			C;
 =i A	sig_c

		 plci->STRIECc
	= A	sig_	OK)
Y_EY__3PTY_ENbrw", ORMAT;eturn fRci);
					}
					e("SendFacRc yf(ap_R,
		      _ tu		t0xfrn (ec_reqd Num));
			a) In)tate =	=if (a-Nu 0CREnD;
	 plci->cr_enqu		prin
		d &&ALERT)tate =						break;
				 ummybreak=,NCRnault:
		l

		 plci->STRIECc
	= OK)
Y_EY__3PTY_EN("SendFacRc yf(ap_R,
		      _ tu		t0xfrn (ec_reqd Num0)ORMAT;eturn fRci);
					}
					e("SendFacRc yf(ap_R,
		      _ tu		t0xfrn (ec_reqd Num));
			a) In)tate =	=if (a-Nu 0CREnD;
	 plci->cr_enqu	}ate =	omma
		d &&ALERT)t	ls	break;
eturn fals	)ppl,HOOK_i[1REQ:TY_N
TRIEVE_REQ(
				&&)		}	Iplci->STRIE, <ncc__stf("[ncc_]		ifSCONNECIL OUT	ING_REQUIRe 0ncc__stf("[ncc_]		| (plci->State ==ITY_NNNN)leanuprncc__dataALERT;
ncc_=;	}
				n		C;
rncc_ALERT;
Nlci-C,;;
				ncc_=;	}
			}ate =	AGE_FORMAT;
					eturn fals	)ppl,HOOK_iFF1REQ:TY_N
TRIEVE_REQ
		    || (plci->State ==OUT	Ca_REQ_PEND;
x32\x30"ER *a,	 CALl_cre0f("ffor	"SeICE, 0);
				senDIS_PE
		    | (plcci->State =PEND;
eturn fafls	)ppl,MWI	ACiIVand REQ>Stat:		se)ppl,MWI	DEACiIVand REQ>Stat:		se>TRIEglob			C;
 =i A	sig_			}Cc
	= A	sig_	OK)POO	Cplci->S].inf	word i;


	MWI	l_c,* locaedntf("fformbrw", ORMAT;
									 TRIECc
!i OK)
Y_EY_3PTY_ETRIECc
	= );
				E OUT	ING_REQUIR wrong"0x2y07f 	}
Illegol m.info[0p(rame 0; oronfo)EGIN:  pl].inf	word i;


	MWI	l_c,invalid0p(rame 0;ntf("fformAGE_FORci);
					}
					e wrong"0x300Bf 	}
  ork0x300Eei	EGIN:  pl].inf	word i;


	MWI	l_c,  ork0x300Eeintf("fformAGE_FOR	}
0x3010:MR_CONF_B  or= 0oweSEin this stf(" EGIN:  pPU		}
			&SSp(rplc4],i0x300E);    SS
  ork0x300Eei	EGIRMAT;
					ummyVE_REQ = 0;
				rplci-
	= MWI	ACiIVand REQ>Stat)
Y_EY_3PTY_EPU		}
			&SSp(rplc_M, S_MWI	ACiIVandf("ffobreak;
				 PU		}
			&SSp(rplc_M, S_MWI	DEACiIVand);	ate pplciVE_REQ(l;
						)
Y_EY_3PTY_E	"SendE_REQ_PEND,_CAPPPIIIIIInault:
			R,
		      __CAPPNTY_R | C		t0xfr_CAPPNTY_R |E_REG)>rplcieNFRNFFIIIIIId )>len wro, =_mask3 +SSp(rpl)te =			ummyCc
!i OK)=VE_R
		d &&ALERT)tate =reak;
				
-	
	_3PTY_E	"SendE_REQ_PEND,_CAPPPIIIIIInault:
			R,
		      __CAPPNTY_R | Cr_CAPPNTY_R |E_REG)>rplcieNFRNFFIIIIIId )>len wro, =_mask3 +SSp(rpl)te =		
					eturn fals	)ppl,	   _BEGIN REQ>Stat:		se)ppl,C   _ADD REQ>Stat:		se)ppl,C   _SP:
	 REQ>Stat:		se)ppl,C   _DROP REQ>Stat:		se)ppl,C   _ISOLand REQ>Stat:		se)ppl,C   _REATTACHlREQ>Stat:		seE].info + 1);
				C   _Rlconne/onne"a,	/* srctf("fforTRIEyVE_REQ = 0;
				rplci-
	= C   _ADD REQ>Statci	if( (plci-C;l	  dP				In== _REQ_PEND;
r *a, i nDINdadaptr
->inIs;	}
			;
		} /DIS_PE = 0;
				rplci-lci-		_3PTY_)ppl,	   _BEGIN REQ>Stat:		seYSSSp(rplc1] k;Sa	   _BEGINtate =mAGE_FORMAT;)ppl,C   _ADD REQ>Stat:		seYSSSp(rplc1] k;Sa	   _ADD("fformb *a, i nDING)C;l	  dP				Inte =		tr
->in!=_maskrnDING)Is
<< 8eca rnDING)Lrplci G)Is;	}
		mAGE_FORMAT;)ppl,C   _SP:
	 REQ>Stat:		seYSSSp(rplc1] k;Sa	   _SP:
	;	}
		mAGE_FORMAT;)ppl,C   _DROP REQ>Stat:		seYSSSp(rplc1] k;Sa	   _DROP;	}
		mAGE_FORMAT;)ppl,C   _ISOLand REQ>Stat:		seYSSSp(rplc1] k;Sa	   _ISOLand;	}
		mAGE_FORMAT;)ppl,C   _REATTACHlREQ>Stat:		seESSSp(rplc1] k;Sa	   _REATTACH("fformAGE_FORMAT;
	"fforTRIECc
!i OK)
Y_EY_3PTY_E wrong"0x300Ef 	}
  ork0x300Eei	EGIN:  pommandC;l	  dP				In				ULL("fformDIS_PEpt

		  l=I0, LLCbreak;
("SendrE_REQ_PEND,_CAPPIIIIIInault:
			R,
		      __CAPPIIIIIIr CONFRNFIIIIIIE_REG)>rplcieNFRNFIIIIIId )>len wro, =_mask3 +SSp(rpl)te				eturn fals	)ppl,VSWITCHlREQ>Stat:		seETRIECc
!i OK)
Y_EY_3PTY_ETRIE(plci-C;l	  dP				In=
					}
					e(plci-C;l	  dP				In->v	;
		}s		  l=I0, LLCb	e(plci-C;l	  dP				In->v	08) &=I0, LLCb	e(plci-C;l	  dP				In->v	08) dia  "w&=I0, LLCb	}"fformDIS_PEv	;
		}s		  l=I0, LLCb	DIS_PEv	08) &=I0, LLCb	DIS_PEv	08) dia  "w&=I0, LLCbreak;
				
-	
	{2nd PLummyE_REG)C;l	  dP				In		O,
	-NFIIIIDIS_PEv	;
		}s		  l== 1
	O,
	-NFIIIIDIS_PEC;l	  dP				In->v	;
		}s		  l=pa3			}
jopr tr	ple   EGIN:  plDIS_PEv	;
		}s		  l=I3te =		
					eturn fals		/* C= 0 Def  "w0])pR_CONF_Bp"Se		/ (SSEC !*/	ls	)ppl,CD REQ>Stat:		seYSSp(rplc1] k;Sa	 CALDEFsg))ai[("fforTRIECc
!i OK)
Y_EY_3PTY_E wrong"0x300Ef 	}
  ork0x300Eei	EGIN:  pommandif (a-CDEnD;
	 pl0, LLCbreak;
("SendE_REQ_PEND,enault:
			R,
		      _ tuA_CAPPIIIIIIE_REG)>rplcieqd )>len wro, =_mask3 +SSp(rpl)te =		eturn fals	)ppl,RTPySCONNECTplclEQ>COMMAND_2:		seETRIECc
	= OK)
Y_EY_3PTY_Encc_ k;	{
	ncc_ALERT;
cha,0i("fforr
->in!ciort0xffff) s != d_maskyncc_= << 16)("fforrPENDIN(
				&&++;		d 	LP 0ncc__stf("[ncc_]		| (plcci->State =PEND;
}fals	default:TY_NlummyVE_REQ = 0;
				rplci-_
		us[0])
Y_EY_3PTY_E(*yVE_REQ = 0;
				rplci-_
		us[0]))(		 dprin,nrct;2nd PLummyE_REG) = 0;
				rplci-lci-	
		brw", ORMAT;
					supported */
		nex*_ = 0;
				rplci-(		 dprin				s	re	re				  (paEND==0	EGIN_3PT
->in!=_masknDING)Is
<< 8eca nDING)Lrplci G)Is;	}
nect_b3_citel):Idn|=tEXTd				if tat;	
			;
		} /DIS_PE = 0;
				rplci-lci-_3PT)ppl,BLOCK			In:TY_Nbrw", OR3PT)ppl,	) RT>L1_(rpl		sig_		tat:		s)ppl,REM>L1_(rpl		sig_		tat:		srTRIEglob			C;
 =i A	sig_c

		{					supported */
		[i] 				_3PTY_].info + 1);
				***L1pR_C x3dADAPT"c)te =		DIS_PE = 0;
				rplci-		-0PEND;
x32\x30"ER *a,REMOVq(rplci, S_SERVICE, 0);
				seelOSAeturn fals	/* C= 0 Def  "w0])pR_CONF_Bp"Se		/, ar icno PEND;ptr,* locaed	EGIN:)ppl,CD REQ>Stat:		seSSp(rplc1] k;Sa	 CALDEFsg))ai[("ffoTRIECc
!i OK)
Y_E_3PTY_ wrong"0x300Ef 	}
  ork0x300Eei	EGIN: elOSAacility_req"));
max_LCI ;dapter->m{TY_NlummyLCI ifo[0])cir CDEnD;
	)
Y_EY_3PTY_ETRIE!LCI ifo[0])cir IskmLCI ifo[0])cir CDEnD;
	&=I0, LLCb	ci);
					}
					e("Send&LCI ifo[0])cir,enault:
			R,
		      _ tuA_CAPPPPIIIIIIE_REG)>rplcieqd )>len wro, =_mask3 +SSp(rpl)te =				TRIEber,
 LCI ifo[0])cir CDEnD;
	&=I0, LLCb						break;elOSADIS_PE = 0;
				rplci-		-0PEND;eturn falsappl,PERM_COD_HOOK:                   /* finishei	BERS:HookcILECTOR 	Nbrw", OR3PT)ppl,PERM_COD_	 CA:TY_NDIS_PE = 0;
				rplci-		-PERM_COD_	CON>Stat;R 	N].info + 1);
				***Crolc CrdAPD
	P"Se		/, Rc		->nne",_Ccc)te =	brw", OR3PT)ppl,PERM_COD_A	sig_:TY_N].info + 1);
				***Crolc A loca, Rc		->nne",_Ccc)te =	DIS_PE = 0;
				rplci-		-0PEND;ummyCc
!i A	sig_	OK);_REQ_PEND;DIS_PE = 0;
				rplci-		-PERM_COD_	ALL("ffox32\x30"ER *a,	 CALl_cre0f("ffoSERVICE, 0);
				sebrw", OR3PT)ppl,LISTENL(rpl		sig_		tat:		srTRIECc
	= A	sig_	OK)POO	{e =		DIS_PE = 0;
				rplci-		-0PEND;
].info + 1);
				ListenCIGIN,cnewp(rplci		->nne",_VE_REQ
ig
		c)te =		er Numfo);
	E-C,;"			2		18			d")t             /* k0x300EMo		i taitnfo)EGIN:  x32\x30"ER *a,IateCand REQ(rplci, S_SERVICE, 0);
				seelOSA[i] 				_3PTY_].info + 1);
				ListenCIGINa;
iledmyL locaRcconne)",_Ccc)te =		aG)listen_act;ve--te =		DIS_
		d &&ALERT)tate =DIS_PE
		    |((pl, 0);elOSAeturn falsappl,USELAWLl_c:		srTRIEglob			C;
 =i A	sig_c

		{					TRIECc
	= A	sig_	OK)
Y_EY_3PTY_E	32\x30"ER *a,LAWLl_c(rplci, S_v	"SeICE, 0);
				senS].inf	word i;


	Auto-Law,* locaedntf("fforreak;
				
-	
	{2nd PL].inf	word i;


	Auto-Law,* locaa;
iledntf("fformaG)	utomturn_law,=I3te =			DIS_PE = 0;
				rplci-		-0PEND;
maG)	utomturn_law		In				ULL("ffor
					supported */
		[i] =TRIEbreak=,LAWLl_c			}Cc
	= OK)POO	{e =		].inf	word i;


	Auto-Law,initiaEeintf("fforaG)	utomturn_law,=I2te =		DIS_PE = 0;
				rplci-		-0PEND;elOSA[i] 				_3PTY_].info + 1);
				Auto-Law,  ork0x300Eeintf("fforaG)	utomturn_law,=I3te =		DIS_PE = 0;
				rplci-		-0PEND;
x32\x30"ER *a,REMOVq(rplci, S_SERVICE, 0);
				semaG)	utomturn_law		In				ULL("ffoelOSAeturn fda	reRVE_R\x3d && RIGIN 0);
	;re	r}		return void ]ata_	c"DAPTER *a,
	= 0;
chc, APyte faci;APId, word Number, DIV
	ANCPTE*ncc__	ti->nL_reaO82DE-C *]ata;a PARSEncc_, Onnect_b3_ciCTmand "));TransmirBci_frFreedE_REQ_PEND,eE_REQ_Data_>Idt_	ti)			sP;
	nDING)Lrplci ;f((ncc_ k;,cichEncc_[ch];_R;
	ifncc_ B3_Xa <ncc__nDIN	ncc_]		ifnDING)Isklci-_3PTYncc__	ti 			({
	ncc_[ncc_]);R 	N].info + 1);
				Data_out=%d, ]ata_p"Se		/=%d", ncc__	tiQ_Data_out, ncc__	tiQ_Data_p"Se		/)				seTRIEncc__	tiQ_Data_p"Se		/)				_3PTY_]ata 			(ncc__	tiQ_DBci_fr[ncc__	tiQ_Data_out 		  endTRIE!(Data 0F:agl &r4);	OyC 0ncc__stf("[ncc_])
Y_EY_3PTY_E ->in!= d_maskncc_= << 16) s !=_masknDING)Is
<< 8eca aG)Is;	}
	}
nect_b3_citel):Idn|=tEXTd				if tat;	, S_v	"SendE_REQ_PEND,enL_reaO82l,
		      _ turnData 0NrplcieNFRNFFIIIIIId )"rnData 0		}
leum0f("ffobreak;
Encc__	tiQ_Data_out=++;		d 	ummyncc__	tiQ_Data_out
	= MAXnL_reaO8lci-	
	ncc__	tiQ_Data_out
	-0PEND;
Encc__	tiQ_Data_p"Se		/)--te =	elOS _W	r}		return void ]ata_aIN(DAPTER *a,
	= 0;
chc, APyte faci;APId, word Number, DIV
	ANCPTE*ncc__	ti->nPARSEncc_, Ona;
	nDING)Lrplci ;f(ncc_ k;,cichEncc_[ch];_Rncc__	ti 			({
	ncc_[ncc_]);R ummyncc__	tiQ_Data_aIN_p"Se		/)		{
>STRIE, <ncc__stf("[ncc_]	B3_Xa <ncc__nDIN	ncc_]		ifnDING)Isklci-_3PTY ->in!= d_maskncc_= << 16) s !=_masknDING)Is
<< 8eca aG)Is;	}
	nect_b3_citel):Idn|=tEXTd				if tat;	, S	"SendE_REQ_PEND,enL_reaO82l,
		      _ turnncc__	tiQ_DataAck[ncc__	tiQ_Data_aIN_out .NrplcieNFRNIIIIIId )"rnncc__	tiQ_DataAck[ncc__	tiQ_Data_aIN_out .		}
leum0f("ffelOSyncc__	tiQ_Data_aIN_out=++;		dummyncc__	tiQ_Data_aIN_out
	= MAXnL_reaACKS
	
	ncc__	tiQ_Data_aIN_out
	-0PENDyncc__	tiQ_Data_aIN_p"Se		/)--te 	r}		return void s32\ind DAPTER *a,c, APyte fax_ci;APyte faci;APyte farI-
{
E fax_pa PARSEcip;APyte facip_masn fd
					ie;APId, word Number, DIV
	A
	byte pvcsaved_p(rplcMAXnMSG_te MSm+k1];_#define MAXte MSIDS 31fd
					p(rplcMAXte MSIDS] fd
					er Ni[4] fd
					multi_;
,_p(rplcMAXnMULTI_IE] fd
					multi_TIVATEplcMAXnMULTI_IE] fd
					multi_ssex*_ATEplcMAXnMULTI_IE] fd
					multi_CiPN_ATEplcMAXnMULTI_IE] ffd
					multi_v	;
		}_ATEplcMAXnMULTI_IE] ffd
				ai_	byte 
					esc RI_ k;""te 
					esc law,=I""te 
					pty_ca_ k;""te 
					esc cr  k;""te 
					esc fo)) {
 k;""tee 
				;
, ch,y[256] fdDAPTERt *a, i 	ULL("f= 0;
chic]ak;dd_p2		18			1"te 
				voice		aic]aak;dd_p6		14			d			d			d			d			8"te 
				resume		auc]ak;dd_p5			5			d			2			d			du;
PR_OESCnMSGTYPE mr icbe theAla icbu_Bone m.info[, acnewpIE has	to b  EGINR_OincludedibeforlrtheAESCnMSGTYPE ci-	MAXte MSIDS has	to b  incx3dIdtei	EGIN   SMSGhis situaEei at the "Se b 	au] =Tts 0 (aciltr	pturb ch,y reasons	EGIN   (se = wro_Masn Bit 4, fir icIE. ther the m.info[0type)           EGINte faATEpl_idc]akci-_MAXte MSIDSa,	PN,t0xffa,DSA, OSA, BC,;LLC,;HLC,;ESCnCAUSEa,DSPa,DTa,	HA,
NFTUUI,		  G_RR,		  G_RNR,;ESCnCHI,	KEYa,	HI,		AU,;ESCnLAW,
NFTRDN,tRDX,		  N_NR,;RIN,tNI,		AI,;ESCnCR,
NFTCST,;ESCnPROFILE,t0xffa,ESCnMSGTYPE, SMSG};
PR_O14
FTY repl	= ;ESCnCHI	EGIN   18 PI  repl	= ;ESCnLAW	EGIN   x3d &&d OAD (
		gTd tot0xff foolfurol_Bu] , OAD is multiIE now EGINte famulti_;
,_idc]ak {1;
FTY};INte famulti_TIVidc]aak {1;
PI};INte famulti_CiPN_idc]aak {1;
OAD};INte famulti_ssex*_idc]aak {1;
ESCnSSEXT}tee te famulti_v	;
		}_idc]aak {1;
ESCnVSWITCH} ffd
						au->nPARSEncc_, PfalseSScILEc]ak;dd_p5			2			d			2			d			du; /* HoldcILECsci-ctEGIN= 0;
CFcILEc]ak;dd_p9			2			d			6			d			d			d			d			d			d""pY= 0;
I
 0;r_E;r_ILEc]ak;dd_pa			2			d			7			d			d			d			d			d			d			d			d			d			d""pY= 0;
	   _ILEc]ak;dd_p9		16			d			6			d			d	0		d	0		d	0		d	0		d""pY= 0;
fooce	m
	eatu plci->cr_e= 0;
dir;APyte fa-
{
E faxw, Ona;
	nDING)Lrplci ;f(
->in!=_masknDING)Is
<< 8eca aG)Is;	}PU		}
			&SS_ILEc4],i0x0by0), Onnect_b3_cis32\x3d && !p)		{
>SVE_REQ
ig
RNR
=I2t /* discalenEGIN:].info + 1);
				sig discalenrhi
		x3d &&
p"Se		/"c)te =brw", ORM}f(nect_b3_citel			}
				i
>State & (pl	 CALHELD):Idn|=tEXTd				if tat;	}].info + 1);
				sigILE-Ip=%08lx,princ%x,telc%x,stf("conne (
				&&c%d,Discflowcl=%d",o = turnnDING)IsornDING)tellci->ptyStf(",_PENDIN(
				&&,_PENDIN
		gupEflowwotrl_timfr));f(nect_b3_ci
ig
	i-
	= C CALHOLD_ACK			}
				i(
				&&)		{
>SVE_REQ
ig
RNR
=I1te =brw", ORM}f(nect_b3_ci
ig
	i-
	= HANGUP			}
				i(
				&&)		{
>SVE_REQ
ig
RNR
=I1te =PENDIN
		gupEflowwotrl_timfr++;		d   x3c &&r the netty_kAlay&r af 0;ttimfout
answe
	ifPENDIN
		gupEflowwotrl_timfrl== 1y0)STATE;
	].info + 1);
				Exce/* NCal disc"c)te =	DIS_PE
ig
RNR
=I0te =	DIS_PE
		gupEflowwotrl_timfrl=I0te =	acilincc_ k;1;emgc_ < MAXnNCPTE+;1;emgc_pter->m{TY_NlummyL <ncc__nDIN	ncc_]		ifnDING)Isk
Y_EY_3PTY_E)leanuprncc__dataALERT;
ncc_=;	}
			TRIEVE_REQ(
				&&)VE_REQ(
				&&--te =		nnect_b3_ciCTmand , S_v	"SendE_REQ_PEND,enLi-CCONNECTplcI,;!= d_maskyncc_= << 16)ca cili0ord)>len0, "1i("fforreak;*/
		ummyE_REG)CTmand , S	"SendE_REQ_PEND,enLi-CCONNECTI,acili0ord Num0)ORMATDIS_
		d &&ALERT)tate DIS_PE
		    |((pl, 0)}e =brw", ORM}fIN   do fir icATE:
 the eatu BERS:no OAD in, b 	au] =OAD will b  con&&rtei	EGIN   fir icthe multipl		;
, ch,ycIE, ther mult.	08)gresl h d.              EGIN   ther the p(rame 0;i foolthe eatu\indE+;ordA\indEEEEEEEEEEEEEEEEEEEEEEEEGIN	i-PTE:
	LERT;
multi_;
,_id;
multi_;
,_ATEpl, MAXnMULTI_IE)tat	i-PTE:
	LERT;
multi_TIVid;
multi_TIVATEpl, MAXnMULTI_IE)tat	i-PTE:
	LERT;
multi_ssex*_id;
multi_ssex*_ATEpl, MAXnMULTI_IE)taat	i-PTE:
	LERT;
multi_v	;
		}_id;
multi_v	;
		}_ATEpl, MAXnMULTI_IE)taat	i-PTE:
	LERT;
ATEpl_id;
ATEplum0)ORM	i-PTE:
	LERT;
multi_CiPN_id;
multi_CiPN_ATEpl, MAXnMULTI_IE)tatesc RI_ ;
	n(rplc14] fdesc law,;
	n(rplc18] fdpty_ca_ ;
	n(rplc24] fdesc cr  ;
	n(rplc25] fdesc fo)) {
 k;n(rplc27];_R&paresc cr[Num=	}
			)		{
>STRIEVE_REQ(l;
									}
				iCTmand ,{ate DIS_PE(l;
							plci->cr_enq   d = MANUlciEEEEEEEEEEEEEGINnq   w,=Im		rplci-	EEEEEEEEEEGINnq   b,=Itotal (i = 1;EEEEEEEGINnq   b,=Iprdifo[0])ptypeEEEEEGINnq   b,=I(i = 1;ofr= 0cIEsEEEGINnq   b,=IIE1EEEEEEEEEEEEEEEEEGINnq   S,=IIE1E(i = 1;+aord .EEGINnq   b,=IIE2EEEEEEEEEEEEEEEEEGINnq   S,=IIE2E(i = 1;+aord .EEGINnq	"SendE_REQ_PEND,_CAPEEEEEEyMANUdf(TURERdI,_CAPEEEEEEtuA_CAPEEEEEE0eNFRNIIIIIIddwbbbbSbSNumLL__MANUlci,nE_REQ_m		rplci-eNFRNIIIIII2E+;1E+;1E+;esc cr[Num+;1E+;1E+;esc law[Nu,_VE_REQ
ig
	i-mb1E+;1E+;esc cr[Num+;1E+;1E+;esc law[Nu,_E-C,;esc cr,_E-C,;esc law				s	re	reR_ORreat
 the addi* NCal eatu sci-ctol_BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBEGINer Ni[1] k;n(rplc15]  /* KEY;ofr=ddi* NCal eatu EGINer Ni[2] k;n(rplc11]  /* UUI;ofr=ddi* NCal eatu EGINei_	el k;Add wro(er Ni;
multi_;
,_ATEpl, esc RI_,	;
, ch,y);	at/*rtheAESCnLAW	prdifo[pl hf u-Law,oola-Law,is actuallydGATd = ;theAcalen EGINR_Oindifo[0])pbrw", s = ;theAcalenhf C;
			red = ;theA IVATION           EGINR_OAutomturnLaw() af 0;td);vlr,initBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB    EGINummyL <	utomturn_law,< 4)		{
>STRIEesc law[Nu( _3PTYTRIEesc law[2]) _3PTY_].inf0 + 1);
				u-Law,se  "weintf("fforaG)u law,=I1PEND;elOSA[i]  _3PTY_].inf0 + 1);
				a-Law,se  "weintf("fforaG)u law,=I0PEND;elOSAaG)	utomturn_law,=I4;	}
	nect_b3_		ifaG)	utomturn_law		In) _3PTY_DIS_PE = 0;
				rplci-		-0PEND;
x32\x30"ER *a,REMOVq(rplci, S_SERVICE, 0);
				semaG)	utomturn_law		In				ULL("ffoelOS}
>STRIEesc fo)) {
[0])
Y_TE;
	].info + 1);
				[%06x] CalePo)) {
: %lx %lx %lx %lx %lx",o = _	UnMapCrd w;
ler(aG)Is), 
			D}
			&esc fo)) {
[6]),o = _	
			D}
			&esc fo)) {
[10]), 
			D}
			&esc fo)) {
[14]),o = _	
			D}
			&esc fo)) {
[18]), 
			D}
			&esc fo)) {
[46])==;	ate {
		o)) {
.Glob			O/* NCsa&= 0x0by000ffL("ffo{
		o)) {
.B1_P8)tocoisa&= 0x0by003ffL("ffo{
		o)) {
.B2_P8)tocoisa&= 0x0by01fdfL("ffo{
		o)) {
.B3_P8)tocoisa&= 0x0by000b7L;	ate {
		o)) {
.Glob			O/* NCsa&= 
			D}
			&esc fo)) {
[6]) |o = _GL_BCHANNEL_OPER	cai[1 = _FACILITY_N{
		o)) {
.B1_P8)tocoisa&= 
			D}
			&esc fo)) {
[10])("ffo{
		o)) {
.B2_P8)tocoisa&= 
			D}
			&esc fo)) {
[14])("ffo{
		o)) {
.B3_P8)tocoisa&= 
			D}
			&esc fo)) {
[18])("ffo{
	mrnudburol_rcontrol_d k;
			D}
			&esc fo)) {
[46])("ffo{
	mrn fo)) {
.1);vo[p_o/* NCsak;>vsprotummyL <mrnudburol_rcontrol_d & MANUdf(TURERdFEATURE_ECHnworNCELLERer->m{TY_Nl{
	mrn fo)) {
.1);vo[p_o/* NCsa|=I1L << PRIVand ECHnworNCELLER			semaG)	o)) {
.Glob			O/* NCsa|=IGL_ECHnworNCELLER1 = _FACILITY_N}
sprotummyL <mrnudburol_rcontrol_d & MANUdf(TURERdFEATURE_RTP)TY_Nl{
	mrn fo)) {
.1);vo[p_o/* NCsa|=I1L << PRIVand RTP("ffo{
	mrn fo)) {
.rtp foimary_ATyloadd k;
			D}
			&esc fo)) {
[50])("ffo{
	mrn fo)) {
.rtp =ddi* NCal_ATyloadd k;
			D}
			&esc fo)) {
[54])("sprotummyL <mrnudburol_rcontrol_d & MANUdf(TURERdFEATURE_T38)TY_Nl{
	mrn fo)) {
.1);vo[p_o/* NCsa|=I1L << PRIVand T38("sprotummyL <mrnudburol_rcontrol_d & MANUdf(TURERdFEATURE_FAXnSUB_SEP_PWD)TY_Nl{
	mrn fo)) {
.1);vo[p_o/* NCsa|=I1L << PRIVand FAXnSUB_SEP_PWD("sprotummyL <mrnudburol_rcontrol_d & MANUdf(TURERdFEATURE_V18)TY_Nl{
	mrn fo)) {
.1);vo[p_o/* NCsa|=I1L << PRIVand V18("sprotummyL <mrnudburol_rcontrol_d & MANUdf(TURERdFEATURE_DTMF_TONE)TY_Nl{
	mrn fo)) {
.1);vo[p_o/* NCsa|=I1L << PRIVand DTMF_TONE("sprotummyL <mrnudburol_rcontrol_d & MANUdf(TURERdFEATURE_PIAFS)TY_Nl{
	mrn fo)) {
.1);vo[p_o/* NCsa|=I1L << PRIVand PIAFS("sprotummyL <mrnudburol_rcontrol_d & MANUdf(TURERdFEATURE_FAXnPAPERdFORMATS)TY_Nl{
	mrn fo)) {
.1);vo[p_o/* NCsa|=I1L << PRIVand FAXnPAPERdFORMATS("sprotummyL <mrnudburol_rcontrol_d & MANUdf(TURERdFEATURE_VOWN)TY_Nl{
	mrn fo)) {
.1);vo[p_o/* NCsa|=I1L << PRIVand VOWN("sprotummyL <mrnudburol_rcontrol_d & MANUdf(TURERdFEATURE_FAXnNONSTANDARD)TY_Nl{
	mrn fo)) {
.1);vo[p_o/* NCsa|=I1L << PRIVand FAXnNONSTANDARDvspro	reRONG_STATE;
	{
		o)) {
.Glob			O/* NCsa&= 0x0by0007fL("ffo{
		o)) {
.B1_P8)tocoisa&= 0x0by003dfL("ffo{
		o)) {
.B2_P8)tocoisa&= 0x0by01adfL("ffo{
		o)) {
.B3_P8)tocoisa&= 0x0by000b7L;	ffo{
	mrnudburol_rcontrol_d &= MANUdf(TURERdFEATURE_HARDDTMF, 0)}e =ummyL <mrnudburol_rcontrol_d & (MANUdf(TURERdFEATURE_HARDDTMF |o = _		MANUdf(TURERdFEATURE_SOFTDTMF_SEND | MANUdf(TURERdFEATURE_SOFTDTMF_RECEIVEklci-_3PTYaG)	o)) {
.Glob			O/* NCsa|=IGL_DTMF_S= _FACILITY_}e ={
	mrnudburol_rcontrol_d &= ~MANUdf(TURERdFEATURE_OOB_CHANNEL;IN:].info + 1);
				[%06x] Po)) {
: %lx %lx %lx %lx %lx",o = _UnMapCrd w;
ler(aG)Is), aG)	o)) {
.Glob			O/* NCs,		semaG)	o)) {
.B1_P8)tocois, aG)	o)) {
.B2_P8)tocois,		semaG)	o)) {
.B3_P8)tocois, aG)mrnudburol_rcontrol_d)	;re	reR_ORrolc _b3_	foolthe hci-set/hook stf(" k0x300EMis ar icand;
	0;
		 id  EGINummy_b3_	!ifaG)AdvCrolc		In=
	_3PTfooce	m
	eatu plSERVMultiIE"ER *a,Id;
multi_;
,_ATEpl, FTY,"0x2y(rplci, fooce	m
	eatu |plSERVMultiIE"ER *a,Id;
multi_TIVATEpl, PI,"0x21y(rplci, SERVSSExtInd(	ULL dprin,nId;
multi_ssex*_ATEpllci, SERV wro(prin,nId;
ATEpl, fooce	m
	eatu=;	ateVS;
		}ReqInd 0);
,nId;
multi_v	;
		}_ATEpl=;	at}fIN   	;
		} theAcrolc to theAb-(
				&BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB    EGINummyesc RI_[Num=	}
					if (plci-
>State &) _3PT(plci-b_(
				&B= esc RI_[esc RI_[Nu]&0x1f;3PTNECT:
set_b)
				&Eid_esc	LERT;
Aplci-b_(
				&);IN:].info + 1);
				storeC
				&conne",_PENDINb_(
				&));		dummy_b3_citel		= ADV VOICE			}
				iCTman _3PTYSetVoiceC
				&(aG)AdvCrolc		In, esc RI_,	a				s	re	rOnnect_b3_ciCTman}
				iCTma 0Nrplci++;	
		;
		} /DIS_PE
ig
	i-n _3PT   RespNCse	to Get_S0x300Eei_Services	r_CONF_BEGIN)ppl,	_S= _FACIL:IN:].info + 1);
				S_S0x300Eei"));		dummy (plcQ_PEND) _REQ_PENDnect_ty_ca_[Num	= 4lci-_3PTYPU		D}
			&CFcILEc6], 
			D}
			&_ty_ca_[1etf("fo	reRONG_STATE;
	PU		D}
			&CFcILEc6], MASK_TERMINApp_FACABt:
		 | MASK_HOLD_RETRIEVEf("fo	reRPU		}
			&CFcILEc_M, plci, PU		}
			&CFcILEc4M, plci, ("SendE_REQ_PEND,enault:
			R,
		      _ tu = ((7AIE_REG)>rplcieqd )>len0, 3 +CFcILE)te =PEND
		d &&ALERT)tateeturn fals   S0x3l3dIdtary Service	r_j "weiBEGIN)ppl,	_SERVICldREJ:IN:].info + 1);
				S_R_j "wconne",_Pty_ca_[5]));		dummy (ty_ca_[Nu) _REQ_PEND	;
		} /Dty_ca_[5])
i-_3PT)ppl,NECTEXECUIn:		s)ppl,THREd P			tat:		s)ppl,THREd P			BEGIN:		srTRIE (plci-C;l	  dP				In= _REQ_PEND;t *a, i (plci-C;l	  dP				InPEND;r
->in!=_masktnDING)Is
<< 8eca tnDING)Lrplci G)Is;	}
rTRIEt_b3_citel):rI-n|=tEXTd				if tat;	}
enect_ty_ca_[5um	= NECTEXECUIner->m{TY_NlPU		}
			&SS_ILEc_M, S_NEC  fals		_b3_civ	;
		}s		  l=I0, LLCb(plci-C;l	  dP				Inciv	;
		}s		  l=I0, END;elOSA[i] r->m{TY_NlPU		}
			&SS_ILEc_M, _ty_ca_[5um+a3	PEND;elOSAnect_ty_ca_[2] ((plxff	
Y_E{TY_NlPU		}
			&SS_ILEc4],i0x3600 s !_masknty_ca_[2]				seelOSA[i] 				_3PTY_PU		}
			&SS_ILEc4],i0x300E);		seelOSA(plci-C;l	  dP				In				ULL("ffo(plci-pt

		  l=I0, LLC("SendtE_REQ_PEND,enault:
			n, rcili0ord)>len3 +SScILE)te =Aeturn falsappl,	 CALDEFsg))ai[:lOSAnect_ty_ca_[2] ((plxff	
Y_E{TY_NlPU		}
			&SS_ILEc4],i0x3600 s !_masknty_ca_[2]				seelOSA[i] 				_3PTY_PU		}
			&SS_ILEc4],i0x300E);		seelOSAPU		}
			&SS_ILEc_M, _ty_ca_[5u)te =	acility_req"));
max_LCI ;dapter->m{TY_NlummyLCI ifo[0])cir CDEnD;
	)
Y_EY_3PTY_ETRIELCI ifo[0])cir Iskm("Send&LCI ifo[0])cir,enault:
			I,acili0ord >len3 +SScILE)te =A	=if (ifo[0])cir CDEnD;
	&=Ici->cr_enqureak;*/
		eturn falsappl,DEACiIVanai[1ciVERSai[:		s)ppl,ACiIVanai[1ciVERSai[:		s)ppl,ciVERSai[1INTERROGand CFU:		s)ppl,ciVERSai[1INTERROGand CFB:		s)ppl,ciVERSai[1INTERROGand CFNR:		s)ppl,ciVERSai[1INTERROGand NUM:		s)ppl,	CBSal_comma:		s)ppl,	CBSaDEACiIVand:		s)ppl,	CBSaINTERROGand:		srTRIE (plci-PEND) _REQ_PEND;nect_ty_ca_[2] ((plxff	
Y_E{TY_NlPU		}
			&I
 0;r_E;r_ILEc4],i0x3600 s !_masknty_ca_[2]				seelOSA[i] 				_3PTY_PU		}
			&I
 0;r_E;r_ILEc4],i0x300E);		seelOSA	;
		} /Dty_ca_[5])
i-	_3PTYappl,DEACiIVanai[1ciVERSai[:		sN:].info + 1);
				Debur_Divntf("fforI
 0;r_E;r_ILEcNum	i0x9("fforI
 0;r_E;r_ILEc3um	i0x6;3PTY_PU		}
			&I
 0;r_E;r_ILEc_M, S_	 CALFORWARDI			a)OP)te =		eturn f		s)ppl,ACiIVanai[1ciVERSai[:		sY_].info + 1);
				Aur_Divntf("fforI
 0;r_E;r_ILEcNum	i0x9("fforI
 0;r_E;r_ILEc3um	i0x6;3PTY_PU		}
			&I
 0;r_E;r_ILEc_M, S_	 CALFORWARDI			a)ART)te =		eturn f		s)ppl,ciVERSai[1INTERROGand CFU:		ss)ppl,ciVERSai[1INTERROGand CFB:		ss)ppl,ciVERSai[1INTERROGand CFNR:		seE].inf	word i;


	 w 0;r_Divntf("fforI
 0;r_E;r_ILEcNum	i0xa("fforI
 0;r_E;r_ILEc3um	i0x7;3PTY_PU		}
			&I
 0;r_E;r_ILEc_M, S_INTERROGand ciVERSai[)te =		eturn f		s)ppl,ciVERSai[1INTERROGand NUM:		seE].inf	word i;


	 w 0;r_Nrpntf("fforI
 0;r_E;r_ILEcNum	i0xa("fforI
 0;r_E;r_ILEc3um	i0x7;3PTY_PU		}
			&I
 0;r_E;r_ILEc_M, S_INTERROGand NUMBERS)te =		eturn f		s)ppl,	CBSal_comma:		seE].info + 1);
				CCBSpR_CONF_ntf("fforI
 0;r_E;r_ILEcNum	i0xd("fforI
 0;r_E;r_ILEc3um	i0xa;3PTY_PU		}
			&I
 0;r_E;r_ILEc_M, S_	CBSal_comma)te =		eturn f		s)ppl,	CBSaDEACiIVand:		seE].info + 1);
				CCBSpDebur;vo[pntf("fforI
 0;r_E;r_ILEcNum	i0x9("fforI
 0;r_E;r_ILEc3um	i0x6;3PTY_PU		}
			&I
 0;r_E;r_ILEc_M, S_	CBSaDEACiIVand)te =		eturn f		s)ppl,	CBSaINTERROGand:		srE].info + 1);
				CCBSpI= 0;rog	  ntf("fforI
 0;r_E;r_ILEcNum	i0xb("fforI
 0;r_E;r_ILEc3um	i0x8;3PTY_PU		}
			&I
 0;r_E;r_ILEc_M, S_	CBSaINTERROGand)te =		eturn f		selOSAPU		D}
			&I
 0;r_E;r_ILEc6u,_VE_REQCTma 0S_		}
lef("ffoSERVndE_REQ_PEND,enault:
			I_ tu = ((7AI0ord >len3 +I
 0;r_E;r_ILE)ORMATDIS_
		d &&ALERT)tate eturn f		)ppl,ACiIVanai[1MWI:		s)ppl,cEACiIVanai[1MWI:		senect_ty_ca_[5um	= ACiIVanai[1MWIer->m{TY_NlPU		}
			&SS_ILEc_M, S_MWI	ACiIVandf("ffoelOSA[i]  PU		}
			&SS_ILEc_M, S_MWI	DEACiIVand);	ate nect_ty_ca_[2] ((plxff	
Y_E{TY_NlPU		}
			&SS_ILEc4],i0x3600 s !_masknty_ca_[2]				seelOSA[i] 				_3PTY_PU		}
			&SS_ILEc4],i0x300E);		seelate nect_IS_PE(l;
							
Y_E{TY_NlSERVndE_REQ_PEND,enault:
			I_ tu = ((fli0ord >len3 +SScILE)te =A	DIS_
		d &&ALERT)tate elOSA[i] 				_3PTY_SERVndE_REQ_PEND,enault:
			I_ tuli0ord >len3 +SScILE)te =A}ate eturn f		)ppl,	   _ADD: /* 		caR	EGIN:)ppl,C   _BEGIN:IN:)ppl,C   _DROP:IN:)ppl,C   _ISOLand:IN:)ppl,C   _REATTACH:		se	   _ILEcNum	i9("ffo	   _ILEc3um	i6;3PTY	;
		} /Dty_ca_[5])
i-	_3PTYappl,C   _BEGIN:IN:, PU		}
			&C   _ILEc_M, S_	   _BEGIN)tate =DIS_PEpt

		  l=I0, LLCbeturn f		s)ppl,	   _DROP:IN:se	   _ILEcNum	i5;IN:se	   _ILEc3um	i2;3PTY_PU		}
			&C   _ILEc_M, S_	   _DROP)tate =DIS_PEpt

		  l=ISCONNECIL, LLCbeturn f		s)ppl,	   _ISOLand:IN:se	   _ILEcNum	i5;IN:se	   _ILEc3um	i2;3PTY_PU		}
			&C   _ILEc_M, S_	   _ISOLand)tate =DIS_PEpt

		  l=ISCONNECIL, LLCbeturn f		s)ppl,	   _REATTACH:		see	   _ILEcNum	i5;IN:se	   _ILEc3um	i2;3PTY_PU		}
			&C   _ILEc_M, S_	   _REATTACH)tate =DIS_PEpt

		  l=ISCONNECIL, LLCbeturn f		s)ppl,	   _ADD:3PTY_PU		}
			&C   _ILEc_M, S_	   _ADD)tate =DIS_PEC;l	  dP				In				ULL("ffort *a, i (plci-C;l	  dP				InPEND;rTRIEt_b3_) tnDING)pt

		  l=ISCONNECIL, LLCbDIS_PEpt

		  l=ISCONNECIL, LLCbeturn f		s}	ate nect_ty_ca_[2] ((plxff	
Y_E{TY_NlPU		}
			&C   _ILEc4],i0x3600 s !_masknty_ca_[2]				seelOSA[i] 				_3PTY_PU		}
			&C   _ILEc4],i0x3303);    Time-out: netty_kAdid,  orrespNCd3PTY_____  BERSir the r_COirTd time	EGIN: ellOSAPU		D}
			&C   _ILEc6],i0x0f("ffoSERVndE_REQ_PEND,enault:
			I_ tuli0ord >len3 +C   _ILE)tate eturn f		elOSeturn fals   S0x3l3dIdtary Service	prdifo[pl succesl EGIN)ppl,	_SERVICl:IN:].info + 1);
				Service_ILEntf("ffPU		}
			&CFcILEc4M, plci, (;
		} /Dty_ca_[5])
i-_3PT)ppl,THREd P			tat:		s)ppl,THREd P			BEGIN:		s)ppl,NECTEXECUIn:		srTRIE (plci-C;l	  dP				In= _REQ_PEND;t *a, i (plci-C;l	  dP				InPEND;r
->in!=_masktnDING)Is
<< 8eca tnDING)Lrplci G)Is;	}
rTRIEt_b3_citel):rI-n|=tEXTd				if tat;	}
enect_ty_ca_[5um	= NECTEXECUIner->m{TY_NlPU		}
			&SS_ILEc_M, S_NEC  fals		nect_IS_PEv	;
		}s		  l!pa3	als		{fals		bDIS_PEpt

		  l=I((pl, 0);	mDIS_PEC;l	  dP				In				ULL("ffor=DIS_PEpt

		  l=I0, "ffor
	"ffor].info + 1);
				E = OKntf("ffor("SendtE_REQ_PEND,enault:
			n, rcili0ord)>len3 +SScILE)te
 END;elOSA[i] r->m{TY_Nl	;
		} /DIS_PEpt

		  )
Y_EY_3PTY_)ppl,	_3P			BEGIN:		srCbDIS_PEpt

		  l=ISCONNECIL, LLCbr].info + 1);
				3P		 ONntf("fformeturn fals		)ppl,	_3P			tat:		seESDIS_PEpt

		  l=I((pl, 0);	mDIS_PEC;l	  dP				In				ULL("ffor=DIS_PEpt

		  l=I0, LLCbr].info + 1);
				3P		 OFFntf("fformeturn f	ND;elOSAlPU		}
			&SS_ILEc_M, _ty_ca_[5um+a3	PEND;C("SendtE_REQ_PEND,enault:
			n, rcili0ord)>len3 +SScILE)te =A*/
		eturn falsappl,	 CALDEFsg))ai[:lOSAPU		}
			&SS_ILEc_M, _ty_ca_[5u)te =	acility_req"));
max_LCI ;dapter->m{TY_NlummyLCI ifo[0])cir CDEnD;
	)
Y_EY_3PTY_ETRIELCI ifo[0])cir Iskm("Send&LCI ifo[0])cir,enault:
			I,acili0ord >len3 +SScILE)te =A	=if (ifo[0])cir CDEnD;
	&=Ici->cr_enqureak;*/
		eturn falsappl,DEACiIVanai[1ciVERSai[:		s)ppl,ACiIVanai[1ciVERSai[:		srTRIE (plci-PEND) _REQ_PEND;PU		}
			&CFcILEc_M, _ty_ca_[5um+a2)PEND;PU		D}
			&CFcILEc6], VE_REQCTma 0S_		}
lef("ffoSERVndE_REQ_PEND,enault:
			I_ tu = ((7AI0ord >len3 +CFcILE)te =TDIS_
		d &&ALERT)tate eturn f
	s)ppl,ciVERSai[1INTERROGand CFU:		s)ppl,ciVERSai[1INTERROGand CFB:		s)ppl,ciVERSai[1INTERROGand CFNR:		s)ppl,ciVERSai[1INTERROGand NUM:		s)ppl,	CBSal_comma:		s)ppl,	CBSaDEACiIVand:		s)ppl,	CBSaINTERROGand:		srTRIE (plci-PEND) _REQ_PEND;	;
		} /Dty_ca_[5])
i-	_3PTYappl,DiVERSai[1INTERROGand CFU:		ss)ppl,ciVERSai[1INTERROGand CFB:		ss)ppl,ciVERSai[1INTERROGand CFNR:		seE].inf	word i;


	 w 0;r_Divntf("fforPU		}
			&_ty_ca_[1e, S_INTERROGand ciVERSai[)te =		_ty_ca_[3um	i_ty_ca_[Num- 3;    S0x3l3dIdtary Service-specific0p(rame 0; 	el R/

		 eturn f	ss)ppl,ciVERSai[1INTERROGand NUM:		seE].inf	word i;


	 w 0;r_Nrpntf("fforPU		}
			&_ty_ca_[1e, S_INTERROGand NUMBERS)te =		_ty_ca_[3um	i_ty_ca_[Num- 3;    S0x3l3dIdtary Service-specific0p(rame 0; 	el R/

		 eturn f	ss)ppl,	CBSal_comma:		seE].info + 1);
				CCBSpR_CONF_ntf("fforPU		}
			&_ty_ca_[1e, S_	CBSal_comma)te =		_ty_ca_[3um	i_ty_ca_[Num- 3;    S0x3l3dIdtary Service-specific0p(rame 0; 	el R/

		 eturn f	ss)ppl,	CBSaDEACiIVand:		seE].info + 1);
				CCBSpDebur;vo[pntf("fforPU		}
			&_ty_ca_[1e, S_	CBSaDEACiIVand)te =		_ty_ca_[3um	i_ty_ca_[Num- 3;    S0x3l3dIdtary Service-specific0p(rame 0; 	el R/

		 eturn f	ss)ppl,	CBSaINTERROGand:		srE].info + 1);
				CCBSpI= 0;rog	  ntf("fforPU		}
			&_ty_ca_[1e, S_	CBSaINTERROGand)te =		_ty_ca_[3um	i_ty_ca_[Num- 3;    S0x3l3dIdtary Service-specific0p(rame 0; 	el R/

		 eturn f	sselOSAPU		}
			&_ty_ca_[4M, plc    S0x3l3dIdtary ServicepR_ason R/

		PU		D}
			&_ty_ca_[6], VE_REQCTma 0S_		}
lef("ffoSERVndE_REQ_PEND,enault:
			I_ tu = ((7AI0ord Slen3 +_ty_ca_)te =TDIS_
		d &&ALERT)tate eturn f
	s)ppl,ACiIVanai[1MWI:		s)ppl,cEACiIVanai[1MWI:		senect_ty_ca_[5um	= ACiIVanai[1MWIer->m{TY_NlPU		}
			&SS_ILEc_M, S_MWI	ACiIVandf("ffoelOSA[i]  PU		}
			&SS_ILEc_M, S_MWI	DEACiIVand);		e nect_IS_PE(l;
							
Y_E{TY_NlSERVndE_REQ_PEND,enault:
			I_ tu = ((fli0ord >len3 +SScILE)te =A	DIS_
		d &&ALERT)tate elOSA[i] 				_3PTY_SERVndE_REQ_PEND,enault:
			I_ tuli0ord >len3 +SScILE)te =A}ate eturn f		)ppl,MWI	IateCanai[:lOSAnect_ty_ca_[Num>(plx12er->m{TY_NlPU		}
			&_ty_ca_[3u, S_MWI	IateCand)te =		_ty_ca_[2um	i_ty_ca_[Num- 2t /* 	el P(rame 0; R/

		 _ty_ca_[5um	i_ty_ca_[Num- 5;    S0x3l3dIdtary Service-specific0p(rame 0; 	el R/

		 nect_b3_ciCTma	B3_Xa <Notifico[0])_Masn[VE_REQCTma 0tu - 1] = SMASK_MWIe)
Y_EY_3PTY_ETRIEDIS_PE = 0;
				rplci-		k;
			MWI	a) In)    x3sul_Bon M.info[0Waitnfo)Listen	EGIN:  p_3PTY_ElSERVndE_REQ_PEND,enault:
			I_ tu = ((fli0ord Slen3 +&pty_ca_[2]				se=A	DIS_
		d &&ALERT)tate 	ENbrw", ORMAT;eturn fRci); SERVndE_REQ_PEND,enault:
			I_ tuli0ord Slen3 +&pty_ca_[2]				se=A_ty_ca_[Num	I0, LLCbreak;
				
-	
	{2nd PLacility_req"));
max_LCI ;dapter->m p_3PTY_ElummyL <Notifico[0])_Masn[i]&SMASK_MWIe3PTY_El_3PTY_Ele("Send&LCI ifo[0])cir,enault:
			I_ tu = ((7AI0ord Slen3 +&pty_ca_[2]				se=A	A_ty_ca_[Num	I0, LLCb;eturn fR					bre

		 nect (ty_ca_[Nu)
-	
	{  (packnowledg  EGIN:  p;
, ch,y[2um	I0,    x3w", crol */	ls	break;
ci); ;
, ch,y[2um	I0xfftate elOSA[i] 				_3PTY_   x3j "w */	ls	b;
, ch,y[2um	I0xfft    x3w", crol */	ls	elOSAa
, ch,y[0um	i2;3PTYa
, ch,y[1] k;MWI	l_SPONSEf 	}
FIVATION */	ls	er Numfo);
		AI,;;
, ch,y);	ls	er Numfo);
	E-C,;multi_ssex*_ATEplc0])(    x3meplcied0p(rame 0; ->BonlyBone possi;
	&EGINnq	32\x30"ER *a,	_SERVIClre0f("ffoSERVICE, 0);
				se_IS_PE(rplci-		-0PEND;nex*_ = 0;
				rplci-(		 dprin				s eturn f		)ppl,	   _ADD: /* OK	EGIN:)ppl,C   _BEGIN:IN:)ppl,C   _DROP:IN:)ppl,C   _ISOLand:IN:)ppl,C   _REATTACH:		s)ppl,C   _PARTYLi-C:		se	   _ILEcNum	i9("ffo	   _ILEc3um	i6;3PTY	;
		} /Dty_ca_[5])
i-	_3PTYappl,C   _BEGIN:IN:, PU		}
			&C   _ILEc_M, S_	   _BEGIN)tate =nect_ty_ca_[Num	= 6)
Y_EY_3PTY_Edm	i_ty_ca_[6] fdN:, PU		D}
			&C   _ILEc6],id)(    P(rtyID */	ls	break;
ci);
Y_EY_3PTY_EPU		D}
			&C   _ILEc6],i0x0f("ffor
					supported )ppl,	   _ISOLand:IN:sePU		}
			&C   _ILEc_M, S_	   _ISOLand)tate =	   _ILEcNum	i5;IN:se	   _ILEc3um	i2;3PTY_eturn f		s)ppl,	   _REATTACH:		seePU		}
			&C   _ILEc_M, S_	   _REATTACH)tate =	   _ILEcNum	i5;IN:se	   _ILEc3um	i2;3PTY_eturn f		s)ppl,	   _DROP:IN:sePU		}
			&C   _ILEc_M, S_	   _DROP)tate =	   _ILEcNum	i5;IN:se	   _ILEc3um	i2;3PTY_eturn f		s)ppl,	   _ADD:3PTY_PU		}
			&C   _ILEc_M, S_	   _ADD)tate =dm	i_ty_ca_[6] fdN:,PU		D}
			&C   _ILEc6],id)(    P(rtyID */	ls	bt *a, i (plci-C;l	  dP				InPEND;rTRIEt_b3_) tnDING)pt

		  l=ISCONNECIL, LLCbeturn f		s)ppl,	   _PARTYLi-C:		se=	   _ILEcNum	i7;IN:se	   _ILEc3um	i4;3PTY_PU		}
			&C   _ILEc_M, S_	   _PARTYLi-C)tate =dm	i_ty_ca_[6] fdN:,PU		D}
			&C   _ILEc4],id)(    P(rtyID */	ls	beturn f	sselOSAnDING)pt

		  l=ISCONNECIL, LLCSERVndE_REQ_PEND,enault:
			I_ tuli0ord >len3 +C   _ILE)tate eturn f		)ppl,	CBSaINap_RETAIN:IN:)ppl,CCBSaERASE	 CALINKAGEID:IN:)ppl,CCBSaa)OP_ALERTING:		se	   _ILEcNum	i5("ffo	   _ILEc3um	i2;3PTY	;
		} /Dty_ca_[5])
i-	_3PTYappl,CCBSaINap_RETAIN:IN:Y_PU		}
			&C   _ILEc_M, S_	CBSaINap_RETAIN)te =		eturn f		s)ppl,	CBSaa)OP_ALERTING:		se_PU		}
			&C   _ILEc_M, S_	CBSaa)OP_ALERTING)te =		eturn f		s)ppl,	CBSaERASE	 CALINKAGEID:IN:e_PU		}
			&C   _ILEc_M, S_	CBSaERASE	 CALINKAGEID)tate =	   _ILEcNum	i7;IN:se	   _ILEc3um	i4;3PTY_C   _ILEc6]m	I0, LLCbC   _ILEc7]m	I0, LLCbeturn f	sselOSAwm	i_ty_ca_[6] fdN:PU		}
			&C   _ILEc4],iw)(    P(rtyID */	lOSAnect_b3_ciCTma	B3_Xa <Notifico[0])_Masn[VE_REQCTma 0tu - 1] = SMASK_	CBS)	
Y_E{TY_NlSERVndE_REQ_PEND,enault:
			I_ tuli0ord >len3 +C   _ILE)tate elOSA[i] 				_3PTY_acility_req"));
max_LCI ;dapter->m pummyL <Notifico[0])_Masn[i] = SMASK_	CBS)3PTY_ElSERVnd&LCI ifo[0])cir,enault:
			I_ tu = ((7AI0ord >len3 +C   _ILE)tate elOSAeturn f		elOSeturn fsappl,	 CALHOLD_REJ:IN:)pu k;n(rplc7];_R;
	if)pulci-_3PTYty_r_L3nCAUSE | 	auc2];		e nect	auc2]m	= 0) ty_rex3603 f		elOSONG_STATE;
	ty_rex3603 f		elOSPU		}
			&SS_ILEc_M, S_HOLDf("ffPU		}
			&SS_ILEc4],ii);		dummy_b3_ci
>State & 	= HOLD_REcomma)STATE;
	_b3_ci
>State & 	I((pl, 0);SERVndE_REQ_PEND,enault:
			I_ tuli0ord >len3 +SScILE)te =elOSeturn falappl,	 CALHOLD_ACK:		dummy_b3_ci
>State & 	= HOLD_REcomma)STATE;
	_b3_ci
>State & 	I	 CALHELD("ffo	rolcIdCIGIN(a dprin				s star*_ = 0;
				rplci-(		 dprin, holdcsave		rplci-lte =elOSeturn falappl,	 CALRETRIEVE_REJ:IN:)pu k;n(rplc7];_R;
	if)pulci-_3PTYty_r_L3nCAUSE | 	auc2];		e nect	auc2]m	= 0) ty_rex3603 f		elOSONG_STATE;
	ty_rex3603 f		elOSPU		}
			&SS_ILEc_M, S_RETRIEVEf("foPU		}
			&SS_ILEc4],ii);		dummy_b3_ci
>State & 	= RETRIEVE_REcomma)STATE;
	_b3_ci
>State & 	I	 CALHELD("ffo	rolcIdCIGIN(a dprin				s sERVndE_REQ_PEND,enault:
			I_ tuli0ord >len3 +SScILE)te =elOSeturn falappl,	 CALRETRIEVE_ACK:		dPU		}
			&SS_ILEc_M, S_RETRIEVEf("foummy_b3_ci
>State & 	= RETRIEVE_REcomma)STATE;
	_b3_ci
>State & 	I((pl, 0);_IS_PE(all_dirn|=t	 CALDIRdFORCE_ (plcNL("ffo(plci-b_(
				&B= esc RI_[esc RI_[Nu]&0x1f;3PT	nect_b3_citel)				_3PTY_NECT:
set_b)
				&Eid_esc	LERT;
Aplci-b_(
				&);IN:rE].info + 1);
				RetrC
				&conne",_PENDINb_(
				&));		dTYSetVoiceC
				&(aG)AdvCrolc		In, esc RI_,	a				sT	nect_b3_ciB2_08) &== B2_TRANSPARENT			}
				iB3_08) &== B3_TRANSPARENT)
Y_EY_3PTY_Ed.info + 1);
				Get B-chntf("fformstar*_ = 0;
				rplci-(		 dprin, x3wrieve	restore		rplci-lte =	break;
ci);
Y_EY_SERVndE_REQ_PEND,enault:
			I_ tuli0ord >len3 +SScILE)te =A}ate ci);
Y_EYstar*_ = 0;
				rplci-(		 dprin, x3wrieve	restore		rplci-lte =elOSeturn falappl,IateCand Iat:		sTRIEVE_REQ
		   !=,LISTENING) _3PTY	32\x30"ER *a,HANGUPre0f("ffoSERVICE, 0);
				seeturn f		elOScip&=IciRVIcip(a dp(rplc4],ip(rplc6])("ffcip_masn =I1L << cip;APEd.info + 1);
				cipc%d,cip_masn=%lx", cip,acip_masntf("ff)lear_c\ind_masn 0);
				snect x3d && star*ed		if aG)Lrplci _disD;
	-lci-_3PT	set_c\ind_masn_bit"ER *a,MAXnAPPL				segroup_o/* mizo[0])(a dprin				s acility_req"));
max_LCI ;dapte _3PTY_TRIELCI ifo[0])cir Is3PTY_    B3_Xa <CIP_Masn[i] = 1 |a aG)CIP_Masn[i] = cip_masnt3PTY_    B3_CPN_filci _on 0TEplc0],	a,ii)3PTY_    B3_test_group_ind_masn_bit"ER *a,i)e _3PTY_:].info + 1);
				storedcip_masn[%d]connlx", i, aG)CIP_Masn[i]tf("fformset_c\ind_masn_bit"ER *a,
				senS]ump_c\ind_masn 0);
				s;
	_b3_ci
te & 	I(NCcci->State =PEND;
;_IS_PE(all_dirn=ct_IS_PE(all_dirn& ~(	 CALDIRd (p,
		 CALDIRd RIGINand)) |o = _			 CALDIRdIN,
		 CALDIRdANSWat;	, S_vummyesc RI_[Nue _3PTY_:o(plci-b_(
				&B= esc RI_[esc RI_[Nu] = ((1f;3PT	TY_NECT:
set_b)
				&Eid_esc	LERT;
Aplci-b_(
				&);IN:rEeturn fRR_Oif a listenBon the "xtaord w;
lerMis done;
chGINaif hook stf("s EGIN:  p (parerk0x300Eei	cilif ar icaBon boalencrolc mr icbe bur;vo[pdB    EGIN>m pummyL <crolc_listen[_]	B3_!aG)AdvSig
				In) _3PTY_ pummyL <	o)) {
.Glob			O/* NCsa&,HANDSET)3PTY_El	_b3_citel = ADV VOICE;3PT	TY_[i] =TRIEL <	o)) {
.Glob			O/* NCsa&,i->BOARD_	CDEC)3PTY_El	_b3_citel = 	CDEC;3PTY_ pummy_b3_citel):Idn|=tEXTd				if tat;	, S_v	L <crolc_listen[_]	i nDINdadaptbre

		 lSERVnd&LCI ifo[0])cir,enCCONNECTI,acili0o

		 lBB    "wSSSSSSSbSSSSS", cip,a   /* CIPBBBBBBBBBBBBB    EGIN		 lBB    0TEplc0],	   /* CalledP(rtyNrplci   EGIN		 lBB    multi_CiPN_ATEplc0],	   /* CallingP(rtyNrplci  EGIN		 lBB    0TEplc2],	   /* CalledP(rtySubad    EGIN		 lBB    0TEplc3],	   /* CallingP(rtySubad   EGIN		 lBB    0TEplc4],	   /* BearerCapab ch,y    EGIN		 lBB    0TEplc5],	   /* LowLCBBBBBBBBBBB    EGIN		 lBB    0TEplc6],	   /* HighLCBBBBBBBBBBB   EGIN		 lBB    ei_	el,BB    	}
 		red sci-ct er Ni EGIN		 lBB    er Ni[0],	   /* B (
				&Beatu    EGIN		 lBB    er Ni[1],	   /* keypad ;
, ch,yc  EGIN		 lBB    er Ni[2],	   /* usci usci ]ata c  EGIN		 lBB    er Ni[3],	   /*  		red ;
, ch,yc  EGIN		 lBB    multi_CiPN_ATEplc1]	   /* seordd CiPN(SCR)c  EGIN		 l	);IN:rEeSERVSSExtInd(&LCI ifo[0])cir,IN		 l	B    0ERT;IN		 l	B    tuA_CAPPPPIIIIImulti_ssex*_ATEpllci, rEeSERVSetup wro(&LCI ifo[0])cir,IN		 l	B     0ERT;IN		 l	B     tuA_CAPPPPIIIIIIETEpl,_CAPPPPIIIIIISERVMultiIE"ER *a,Id;
multi_TIVATEpl, PI,"0x21y(rci-etf("fforreak;reak;)lear_c\ind_masn_bit"ER *a,MAXnAPPL				se]ump_c\ind_masn 0);
				s}e =ummyc\ind_masn_empty 0);
	) _3PTY	32\x30"ER *a,HANGUPre0f("ffoSERVICE, 0);
				se_b3_ci
te & 	I((pl, 0)}e =_b3_cinotifiedc= 0c	I0, LLaG)listen_act;ve--te =listen_RIGIN a				seturn falappl,	 CALStat_NOTIFY:e =_b3_cinotifiedc= 0c	I1te =listen_RIGIN a				seturn falappl,	 CALIat:		appl,	 CALCi[:lOSTRIEVE_REQ
		   	= ADVrNCED VOICEL(rp |a VE_REQ
		   	= ADVrNCED VOICELNO(rplci-_3PTYtRIEDIS_PE = 0;
				rplci-		k;PERM_COD_	CON>Stater->m{TY_NlummyVE_REQ
		   	= ADVrNCED VOICELNO(rplci-EY_3PTY_Ed.info + 1);
				***Crolc OKntf("fforpummyL <AdvSig
				In)"fforp_3PTY_ pt *a, i L <AdvSig
				In;3PTY_ pummytnDING)spoofed_msge3PTY_El_3PTY_Eled.info + 1);
				***Spoofed Msg(onne)",_tnDING)spoofed_msge				se=A	At_IS_PE(rplci-		-0PEND;=A	At_IS_PE = 0;
				rplci-		-0PEND;
m		x_ci>in!=_masktnDING)Is
<< 8eca tnDING)Lrplci G)Isca 0x80PEND;
m			;
		} /tnDING)spoofed_msge3PTY_Ell_3PTY_Eleappl,	 CALRES:3PTY_EleAt_IS_PE(rplci-		-nCCONNECTIca l_SPONSEf3PTY_EleAaTIVload_msg(&tnDING)saved_msg,csaved_p(rpl)f3PTY_EleAar Nb1/tnDIN, &saved_p(rplc_M, p,_tnDING)B1_;
, ch,iel)f3PTY_EleAummytnDING)Lrplci G)Iwro_Masn[tE_REQ_PEND 0tu - 1] = 0x2y0e3PTY_Elll_3PTY_Ele p (pearly B3;ordA "w (CIPBmasn bit 9):no releapl,af 0;ta disc EGIN		 l		s	er NumtnDIN, LLI,;"			1			1")f3PTY_EleA}3PTY_EleAar NsmtnDIN, 	  N_NR,;&saved_p(rplc2]				se=A	AAar NsmtnDIN, LLC,;&saved_p(rplc4]				se=A	AAar Nai/tnDIN, &saved_p(rplc5]				se=A	AAt_b3_ci
te & 	I(NCcci->ACCEPT			se=A	AA	32\x30"tnDIN, 	 CALRES(rplci, S_v	foSERVICE, tLERT)tate 	ENrmeturn fals		Eleappl,AWAITI			aELNECTp:3PTY_EleA].info + 1);
				Se  "w_Baord inu ntf("fforv	foStar*_ = 0;
				rplci-(x_ci,_tnDIN,,se  "w_b		rplci-lte =	bENrmeturn fals		Eleappl,AWAITI			MANUdLCi[: /* Get_P*a, p0;tMrnudburol_rcR_C to "xtaord w;
lerMEGIN		 l		snect t_b3_ci
ig
		c3PTY_Elll_3PTY_Ele p].info + 1);
				No sigID!ntf("fforv	foC("SendtE_REQ_PEND,enMANUdf(TURERdR,
		      _ x_ci,_tnDING)>rplcieqdd )"rnLL__MANUlci,nnMANUdf(TURERdR,nn (p_OF_		In)("fforv	foCDIS_
		d &&AtLERT)tate 	ENrmmeturn f	ND;EleA}3PTY_EleAt_IS_PE(rplci-		-nMANUdf(TURERdRf3PTY_EleAaTIVload_msg(&tnDING)saved_msg,csaved_p(rpl)f3PTY_EleAdirn=csaved_p(rplc2].eatu[0]f3PTY_EleAummydirn=	I1) _3PTY_ p	AA	32\x30"tnDIN, 	 CALREQ(rplci, S_v	fo}3PTY_EleA[i] =TRIE!dir) _3PTY_ p	AA	32\x30"tnDIN, LISTENLREQ(rplci, S_v	fo}3PTY_EleASERVICE, tLERT)tate 	ENrm("SendtE_REQ_PEND,enMANUdf(TURERdR,
		      _ x_ci,_tnDING)>rplcieqdd )"rnLL__MANUlci,nnMANUdf(TURERdR,n0lte =	bENrmeturn fals		Eleappl,(	 CALREQ,
	AWAITI			MANUdLCi[):		se=A	AA	32\x30"tnDIN, 	 CALREQ(rplci, S_v	foSERVICE, tLERT)tate 	ENrmeturn fals		Eleappl,	 CALREQ:IN		 l		snect t_b3_ci
ig
		c3PTY_Elll_3PTY_Ele p].info + 1);
				No sigID!ntf("fforv	foC("SendtE_REQ_PEND,enCCONNECTR,
		      _ tnDING)Lrplci G)Isli0ord Numn (p_OF_		In)("fforv	foCDIS_
		d &&AtLERT)tate 	ENrmmeturn f	ND;EleA}3PTY_EleAt_IS_PE(rplci-		-nCCONNECTRf3PTY_EleAaTIVload_msg(&tnDING)saved_msg,csaved_p(rpl)f3PTY_EleAar NsmtnDIN, 	PN,t&saved_p(rplc_M)f3PTY_EleAar NsmtnDIN, DSA, &saved_p(rplc3]				se=A	AAar Nai/tnDIN, &saved_p(rplc9]				se=A	AA	32\x30"tnDIN, 	 CALREQ(rplci, S_v	foSERVICE, tLERT)tate 	ENrmeturn fals		Eleappl,	 CALRETRIEVE:3PTY_EleAt_IS_PE(rplci-		-CLRETRIEVE_REQ			se=A	AA	32\x30"tnDIN, 	 CALRETRIEVE(rplci, S_v	foSERVICE, tLERT)tate 	ENrmeturn fND;EleA}3PTY_EletnDING)spoofed_msg		-0PEND;
m		ummytnDING) = 0;
				rplci-		k;0e3PTY_Elllnex*_ = 0;
				rplci-(x_ci,_tnDIN)tate 	ENturn fR					b;nex*_ = 0;
				rplci-(		 dprin				s rmeturn f	ND;elOSAld.info + 1);
				***Crolc Hook InitBR_Cntf("ffor_IS_PE = 0;
				rplci-		-PERM_COD_HOOK("fforer Numfo);
	FTY,""			1			9")fBBBBBBBBBB   /* Get Hook 
te &EGIN		 	32\x30"ER *a,TEALCTRL(rplci, S_SERVICE, 0);
				seelOS}
>S[i] =TRIE_IS_PE(rplci-	!	-nMANUdf(TURERdR  /* old scyl&
p"rlcient;ordA "w EGIN					}
				i
		   !=,(NCcf(T>State =lci-_3PTYNECT:
set_b)
				&Eid_esc	LERT;
Aplci-b_(
				&);IN:rummy_b3_citel		= ADV VOICE			}
				i
>State & 	= ((pl)    BERS:p"rlcient;orolc 	;
		} on immedie &ly EGIN		_3PTY_RI_[2]	i nDINi-b_(
				&;		dTYSetVoiceC
				&(aG)AdvCrolc		In, RI_,	a				sT}"ffoSERVndE_REQ_PEND,enCCONNECTACiIVE	I_ tuli0ordSs>lenp(rplc2_M, "", "1i("ffo_b3_ci
te & 	I(NCcf(T>State =te =elOSeturn falappl,TEALCTRL:lOST& 	Imulti_;
,_p(rplc0]  /* inspecicthe ;
, ch,ychook indifo[0])s
answe
	ifPENDIN
		   	= ADVrNCED VOICEL(rp 		}ie[Nue _3PTY	;
		} /ie[1] = 0x91) _3PTYappl,0x80:   /* hook off EGIN		appl,0x81:TY_NlummyVE_REQ = 0;
				rplci-		k;PERM_COD_HOOKlci-EY_3PTY_Ed.info + 1);
				init:hook_offntf("fforvVE_REQhook_s		  l=Iie[1];				b;nex*_ = 0;
				rplci-(		 dprin				s rmeturn f	ND;elOSAl[i] =/* ignorlrdou;
	-chook indifo[0])s
answeEY_3PTY_ETRIE((VE_REQhook_s		  ) = ((f0)		k;0x80)"fforp_3PTY_ pd.info + 1);
				ignorlrhookntf("fforv	eturn fND;El}"fforvVE_REQhook_s		  l=Iie[1]&0x91 f	ND;elOSAlR_ORhGINaacilin	rpnfo)c= 0cp"Se		/
answeEY (pand s32
		 '+'.ATma	mr icolciol */	ls	b   BERS:ordA "w	res=TRIc= 0cmr ic 
answeEY (pacce/*ei	cil  orrrrrrrrrrrrrrrrr*/	ls	b;cility_re,_tnDIN				ULL("));
max_LCI ;dapte _3PTY_pummyL <crolc_listen[_]3PTY_prrrrB3_Xa <crolc_listen[_]IN
		   	= (NCcci->State ="fforv	|a aG)crolc_listen[_]IN
		   	= (NCcci->ALERT)e _3PTY_:pt *a, i L <crolc_listen[_];3PTY_:pt *a,ciCTma	= &LCI ifo[0])cir fND;El}"fforelOSAlR_Ono in	rpnfo)c= 0, do outgonfo)c= 0cansweEY (pand s32
		 '+'=TRIoutg.,setuprrr*/	ls	bnect L <AdvSig
				In	B3_!t_b3_) _3PTY_ETRIE(, i get__b3_(a))e _3PTY_:pL <AdvSig
				In	= &L->_b3_[i - 1];3PTY_:pt *a, i L <AdvSig
				In;3PTY_ pt_b3_citel  = ADV VOICE;3PT	TY_PU		}
			&voice		aic5],	L <AdvSig
		AEND 0MaxDataLi = 1f("fforv	ummyL <Iwro_Masn[L <AdvSig
		AEND 0tu - 1] = 0x2y0e _3PTY_ p	 (pearly B3;ordA "w (CIPBmasn bit 9):no releapl,af 0;ta disc EGIN		 l		er NumtnDIN, LLI,;"			1			1")f3PTY_El}3PTY_Eler NumtnDIN, 	AI,;voice		ai)f3PTY_Eler NumtnDIN, OAD,	L <TelOAD)f3PTY_Eler NumtnDIN, OSA, L <TelOSA)f3PTY_Eler NumtnDIN, SHIFp,
	6,		ULL)f3PTY_Eler NumtnDIN, SIN,tdd_p2			1			0")f3PTY_Eler NumtnDIN, Uci,ndd_p6		43\x61		70\x69\x32		30")f3PTY_El	32\x30"tnDIN, AS(rpN, DSIG_ID)f3PTY_Ele <AdvSig
				InPE = 0;
				rplci-		-HOOK_OFF_REQ			se=A	e <AdvSig
				InPE	rplci-		-0PEND;
m	t *a,ciCTma	= L <AdvSig
		AENDPEND;
m	t *a,ci(all_dirn=c	 CALDIRd (p,
		 CALDIRd RIGINandf3PTY_El	ERVICE, tLERT)tate 	E} "ffor
	"ffornect t_b3_) _REQ_PEND;	ci>in!=_masktnDING)Is
<< 8eca aG)Is;	}
r	Idn|=tEXTd				if tat;	, S_("SendtE_REQ_PEND,
TY_prrrr enault:
			I_
TY_prrrr etuA_CAPPrrrr e0A_CAPPrrrr ed >len=_mask0,;"			1+")f3PTY_eturn fals	appl,0x90:   /* hook on  EGIN		appl,0x91:TY_NlummyVE_REQ = 0;
				rplci-		k;PERM_COD_HOOKlci-EY_3PTY_Ed.info + 1);
				init:hook_onntf("fforvVE_REQhook_s		  l=Iie[1] = 0x91;				b;nex*_ = 0;
				rplci-(		 dprin				s rmeturn f	ND;elOSAl[i] =/* ignorlrdou;
	-chook indifo[0])s
answeEY_3PTY_ETRIE((VE_REQhook_s		  ) = ((f0)		k;0x90) _REQ_PEND;	vVE_REQhook_s		  l=Iie[1] = 0x91;				belOSAlR_O
		gup the adv.;voice)c= 0cand s32
		 '-' to theACTma	*/	ls	bnectaG)AdvSig
				In) _3PTY_ ci>in!=_maske <AdvSig
				InPEIs
<< 8eca aG)Is;	}
r	pummy_b3_citel):Idn|=tEXTd				if tat;	, S_v("SendL <AdvSig
		AENDo

		 lBB    nault:
			I_
TY_p	B     tuA_CAPPPrrrr e0A_CAPPPrrrr ed >len=_mask0,;"			1-")f3PTY_Ee <AdvSig
				InPE = 0;
				rplci-		-HOOK_ON_REQ			se=Ae <AdvSig
				InPE	rplci-		-0PEND;
m	32\x30"e <AdvSig
				Ina,HANGUPre0f("ffoEl	ERVICE, aG)AdvSig
				In);				belOSAleturn f	sselOSelOSeturn falappl,RESUMd:IN:)lear_c\ind_masn_bit"ER *a,=_maskdE_REQ_PEND 0tu - 1tf("ffPU		}
			&x3sume		auc4],	GOOD)f3PTSERVndE_REQ_PEND,enault:
			I_ tuli0ord >len=_mask3, x3sume		au				seturn falappl,SUSPtat:		s)lear_c\ind_masn 0);
			swe
	ifPENDINNL.Id		if (plci-nl\x3d && !p) _3PTYNECT:
		d &&ALERT)tate nl\x3qrncc_"ER *a,REMOVq(rplci, }e =ummy (plci-s32\x3d && !p) _3PTY_IS_PE = 0;
				rplci-		-0PEND;x32\x30"ER *a,REMOVq(rplci, }
S_SERVICE, 0);
				summy (plci-(
				&&) _3PTY	ERVndE_REQ_PEND,enault:
			I_ tuli0ord >len=_mask3, "			5			4			0d_p2			0			0")f3PTY	"SendE_REQ_PEND,enLi-CCONNECTI,acili0ord Num0)ORMAelOSeturn falappl,SUSPtat_REJ:IN:eturn falappl,HANGUP:e =_b3_ci
		gupEflowwotrl_timfrl=I0te =
	ifPENDINmrnudburol_r			}
				i
		   	k;LO	 CnCCONNEC) _REQ_PEND)pu k;n(rplc7];_R;
	if)pul _3PTYTy_r_L3nCAUSE | 	auc2];		e nect	auc2]m	= 0) ty_re;		e [i] =TRIE	auc2]m	= 8) ty_r_L1_		caR;		e [i] =TRIE	auc2]m	= 9 |a 	auc2]m	= 10) ty_r_L2_		caR;		e [i] =TRIE	auc2]m	= 5) ty_r_CAPI_GUARD_		caR;		e}
>S[i] =_3PTYTy_r_L3n		caR;		e}

we
	ifPENDIN
		   	= (NCcci->State = |a VE_REQ
		   	= (NCcci->ALERT)
EY_3PTYacility_req"));
max_LCI ;dapter->m{TY_Nlummytest_c\ind_masn_bit"ER *a,
	)	, S_v("Send&LCI ifo[0])cir,enLi-CCONNECTI,acili0ord Num0)ORMATelOS}
>S[i] 
EY_3PTY)lear_c\ind_masn 0);
				s}e =ummy (plci-CTmand ,{ate 
	ifPENDIN
		   	= LISTENING)r->m{TY_Nl(plci-notifiedc= 0c	I0, LLLLaG)listen_act;ve--te =selOSAnDING)
te & 	I(NCcLi->State =PEND;ummyc\ind_masn_empty 0);
	)r->m{TY_Nl(plci-
		  l=I((pl, 0);	
	ifPENDINNL.Id		if (plci-nl\x3d && !p)sweEY_3PTY_ENECT:
		d &&ALERT)tate e nl\x3qrncc_"ER *a,REMOVq(rplci, 	belOSAlummy (plci-s32\x3d && !p)sweEY_3PTY_E_IS_PE = 0;
				rplci-		-0PEND;;
x32\x30"ER *a,REMOVq(rplci, S_}	, S_("SeICE, 0);
				seelOS}
>S[i] d ,{ate R_ORrllisION of Li-CCONNEC	cilCCONNECTRES BERS:HANGUP 	an EEEGINnq   x3sul_Binta seordd HANGUP! Don't gener	  lanothci   rrrrr*/	ls	   Li-CCONNEC																																															*/	ls	TRIEVE_REQ
		   !=,((pl			}
				i
		   !=,(NCcLi->State =er->m{TY_NlummyVE_REQ
		   	= RESUMe =er->mY_3PTY_EPU		}
			&x3sume		auc4],	if("ffoEl	ERVndE_REQ_PEND,enault:
			I_ tuli0ord >len=_mask3, x3sume		au				sS_}	, S_nDING)
te & 	I(NCcLi->State =PEND;Y	"SendE_REQ_PEND,enLi-CCONNECTI,acili0ord Num
				seelOS}
>Seturn falappl,SSEXTdIat:		sSERVSSExtInd(	ULL dprin,nId;
multi_ssex*_ATEpllci, eturn falappl,VSWITCHLREQ:IN	VS;
		}ReqInd 0);
,nId;
multi_v	;
		}_ATEpl=;	OSeturn fsappl,VSWITCHLIat:		sTRIEVE_REQC;l	  dP				In	&&
PPrrrrVE_REQv	;
		}s		  l== 3	&&
PPrrrrVE_REQC;l	  dP				Inciv	;
		}s		  l== 3	&&
PPrrrrV(rplcMAXPARMSIDS - 1][0])
Y_TE;
	er Numfo);EQC;l	  dP				In, SMSG,rV(rplcMAXPARMSIDS - 1])PEND;x32\x30"ER *EQC;l	  dP				In, VSWITCHLREQre0f("ffoSERVICE, 0);
i-C;l	  dP				In=;		e}
>S[i] =VS;
		}ReqInd 0);
,nId;
multi_v	;
		}_ATEpl=;	OSeturn fre	r}
sps		 ic;void SERVSetup wro(APPL *PEND,e		In	*0);
,nd_masnId;
by  l**ATEpl, by  lIwro_SERt_Flag)
TE;_masnNdadby  l*icr_e_masnIwro_Nrplcidadby  l*Iwro_El3dIdtr_e_masnIwro_Masn =I0, "f].info + 1);
				Setup wro"==;	atacility_req"));
MAXPARMSIDS;dapte _3PTT& 	IV(rplcir fNDIwro_Nrplci		-0PENDIwro_El3dIdtl=Iie;_R;
	ifie[Nue _3PTY	;
		} /i) _3PTYappl,0:		srE].info + 1);
				CPN ntf("fforI
ro_Nrplci		-0		070("fforI
ro_Masn =I0x80PEND;
Iwro_SERt_Flag =Ici-ef3PTY_eturn fPTYappl,8:  /* displayrrrrrr*/	ls	b].info + 1);
				display(%d)Num
	f("fforI
ro_Nrplci		-0		028("fforI
ro_Masn =I0x04PEND;
Iwro_SERt_Flag =Ici-ef3PTY_eturn fPTYappl,16: /* C
				&:Idn*/	ls	b].info + 1);
				CHIntf("fforI
ro_Nrplci		-0		018("fforI
ro_Masn =I0x100PEND;
Iwro_SERt_Flag =Ici-ef3PTY_NECT:
set_b)
				&Eid 0);
,nIwro_El3dIdt)f3PTY_eturn fPTYappl,19: /* Redir "weiBNrplci	*/	ls	b].info + 1);
				RDNntf("fforI
ro_Nrplci		-0		074("fforI
ro_Masn =I0x400PEND;
Iwro_SERt_Flag =Ici-ef3PTY_eturn fPTYappl,20: /* Redir "weiBNrplci	ex*ERVedn*/	ls	b].info + 1);
				RDXntf("fforI
ro_Nrplci		-0		073("fforI
ro_Masn =I0x400PEND;
Iwro_SERt_Flag =Ici-ef3PTY_eturn fPTYappl,22: /* Redir "nfo)Nrplci  EGIN		 ].info + 1);
				RINntf("fforI
ro_Nrplci		-0		076("fforI
ro_Masn =I0x400PEND;
Iwro_SERt_Flag =Ici-ef3PTY_eturn fPTYdefault:"fforI
ro_Nrplci		-0f3PTY_eturn fPTY}		e}

we
	ifil== MAXPARMSIDS - 2) _ /* to	prdifo[p theAm.info[0type 	Setup" EGIN		I
ro_Nrplci		-0	8000 s 5("ffoI
ro_Masn =I0x10("ffoI
ro_El3dIdtl=I"";		e}

we
	ifIwro_SERt_Flag 		}I
ro_Nrplcil _3PTYTect_b3_ciCrplci G)Iwro_Masn[PEND 0tu - 1] = Iwro_Masn) _3PTY_("SendLEND,enINap_I_ tuli0ord SlenI
ro_Nrplci,nIwro_El3dIdt)f3PTYelOS}
>	r}
sps		 ic;void SERV wro(		In	*0);
,nd_masnId;
by  l**ATEpl, by  liesIdt)
TE;_masnNdad_masnjdad_masnkdadby  l*icr_e_masnIwro_Nrplcidadby  l*Iwro_El3dIdtr_e_masnIwro_Masn =I0, 	s		 ic;by  l)
	rges[5um	i{4li0or0or0or0}, 	s		 ic;by  l)ause[um	i{0		2,-0	80,i0x00}, 	APPL *PEND, "f].info + 1);
				IwroParpl,"==;	atTectlOS (plci-CTmalOS	if (plci-
		  lOS	if_b3_ci
ig
	i-	!	-NCRnault:
		lOS)
	_3PT].info + 1);
				NoParpl,"==;	ENbrw", ORM}fsapuse[2]m	I0, Lacility_req"));
MAXPARMSIDS;dapte _3PTT& 	IV(rplcir fNDIwro_Nrplci		-0PENDIwro_El3dIdtl=Iie;_R;
	ifie[Nue _3PTY	;
		} /i) _3PTYappl,0:		srE].info + 1);
				CPN ntf("fforI
ro_Nrplci		-0		070("fforI
ro_Masn   =I0x80PEND;
eturn fPTYappl,7: /* 	SCnCAU EGIN		 ].info + 1);
				apu(onne)",_ie[2]tf("fforI
ro_Nrplci		-0		008("fforI
ro_Masn =I0x00PEND;
apuse[2]m	Iie[2]("fforI
ro_El3dIdtl=I	ULL("fforeturn fPTYappl,8:  /* displayrrrrrr*/	ls	b].info + 1);
				display(%d)Num
	f("fforI
ro_Nrplci		-0		028("fforI
ro_Masn =I0x04PEND;
eturn fPTYappl,9:  /* Do[p displayr*/	ls	b].info + 1);
				do[p(%d)Num
	f("fforI
ro_Nrplci		-0		029("fforI
ro_Masn =I0x02;3PTY_eturn f		s)ppl,10: /* )
	rgesr*/	ls	b;cilijy_req"j);
4q"jpte )
	rges[1 + j]m	I0, LLCb;cilijy_req"j);
ie[Nu		if /ie[1 + j]m&;0x80)q"jpte, LLCb;cilin =I1,"jptq"j);
ie[Nu		ifk <=
4q"jpt, kpte )
	rges[k]m	Iie[1 + j]("fforI
ro_Nrplci		-0	4000("fforI
ro_Masn =I0x40("fforI
ro_El3dIdtl=I)
	rges;3PTY_eturn f		s)ppl,11: /* usci usci eatu */	ls	b].info + 1);
				uuintf("fforI
ro_Nrplci		-0		07E("fforI
ro_Masn =I0x08;3PTY_eturn f		s)ppl,12: /* congesTION r "eivci	turdyr*/	ls	b].info + 1);
				clRDYntf("fforI
ro_Nrplci		-0		0B0("fforI
ro_Masn =I0x08("fforI
ro_El3dIdtl=I"";		eY_eturn f		s)ppl,13: /* congesTION r "eivci	  orrerdyr*/	ls	b].info + 1);
				clNRDYntf("fforI
ro_Nrplci		-0		0BF("fforI
ro_Masn =I0x08("fforI
ro_El3dIdtl=I"";		eY_eturn f		s)ppl,15: /* Keypad F
, ch,yc*/	ls	b].info + 1);
				KEYntf("fforI
ro_Nrplci		-0		02C("fforI
ro_Masn =I0x20PEND;
eturn fPTYappl,16: /* C
				&:Idn*/	ls	b].info + 1);
				CHIntf("fforI
ro_Nrplci		-0		018("fforI
ro_Masn =I0x100PEND;
NECT:
set_b)
				&Eid 0);
,nIwro_El3dIdt)f3PTY_eturn fPTYappl,17: /* 
	ino 1tr6l)ause, SERV fulll)ause, [i] =esc Rausen*/	ls	b].info + 1);
				q9apu(onne)",_ie[2]tf("fforummy apuse[2]m|a 	ause[2]m<;0x80) _REQ_P  /* eg. layci	1 0;roi	*/	ls	bI
ro_Nrplci		-0		008("fforI
ro_Masn =I0x01("fforummy	ause[2]m!	Iie[2]) I
ro_El3dIdtl=I)ausef3PTY_eturn fPTYappl,19: /* Redir "weiBNrplci	*/	ls	b].info + 1);
				RDNntf("fforI
ro_Nrplci		-0		074("fforI
ro_Masn =I0x400PEND;
eturn fPTYappl,22: /* Redir "nfo)Nrplci  EGIN		 ].info + 1);
				RINntf("fforI
ro_Nrplci		-0		076("fforI
ro_Masn =I0x400PEND;
eturn fPTYappl,23: /* Notifico[0]) I
difo[oi  EGIN		 ].info + 1);
				NIntf("fforI
ro_Nrplci		-=_maskNI("fforI
ro_Masn =I0x210PEND;
eturn fPTYappl,26: /* C= 0c
te & n*/	ls	b].info + 1);
				CSTntf("fforI
ro_Nrplci		-=_maskCST("fforI
ro_Masn =I0x01( /* do BERS:oauseni.e.aacilnow R/

		 eturn f	ss)ppl,MAXPARMSIDS - 2:  /* Escape M.info[0Type, mr icbe theAlast indifo[0])n*/	ls	b].info + 1);
					SC/MT[onne]",_ie[3]tf("fforI
ro_Nrplci		-0	8000 s ie[3]("fforummyiesIdt)nIwro_Masn =I0xfffftate S[i] = I
ro_Masn =I0x10("fforI
ro_El3dIdtl=I"";		eY_eturn f		sdefault:"fforI
ro_Nrplci	m	I0, LLCbI
ro_Masn   m	I0, LLCbI
ro_El3dIdtl=I"";		eY_eturn f		s}		e}

we
	if_b3_ci
ig
	i-	=	-NCRnault:
		)											R_ORhGINaord w;
lerMbroadcaF_BEGINY_3PTYacilijy_req"j);
max_LCI ;djpter->m{TY_NlCTma	= &LCI ifo[0])cj]("fforummyI
ro_Nrplci"fforrrrrB3_PEND 0tu"fforrrrrB3__b3_ciCrplci G)Iwro_Masn[PEND 0tu - 1] = Iwro_Masn)"ffor{TY_Nl ].info + 1);
				NCRnILEntf("ffforuesIdt =Ici-ef3PTY_v("Send&LCI ifo[0])cj],enINap_I_ tum&;0x0fli0ord SlenI
ro_Nrplci,nIwro_El3dIdt)f3PTYrreak;reak}
>S[i] =TRIE (plci-CTmand ,{ /* ovcilap r "eivnfo)broadcaF_BEGINYrummyI
ro_Nrplci	=	-CPN
forrrrr|a I
ro_Nrplci	=	-KEY
forrrrr|a I
ro_Nrplci	=	-NI
forrrrr|a I
ro_Nrplci	=	-DSP
forrrrr|a I
ro_Nrplci	=	-UUIer->m{TY_Nlacilijy_req"j);
max_LCI ;djpter->mY_3PTY_ETRIEtest_c\ind_masn_bit"ER *a,j	)	, S_v_3PTY_E ].info + 1);
				OvlnILEntf("ffforruesIdt =Ici-ef3PTY_vv("Send&LCI ifo[0])cj],enINap_I_ tuli0ord SlenI
ro_Nrplci,nIwro_El3dIdt)f3PTYrl}"fforelOSAreak}															 (pa 0cothci s32
		ling stf("s EGIN:[i] =TRIEI
ro_Nrplci"fforB3__b3_ciCrplci G)Iwro_Masn[E_REQ_PEND 0tu - 1] = Iwro_Masn)"ff_3PTY].info + 1);
				StdnILEntf("fffuesIdt =Ici-ef3PTY	"SendE_REQ_PEND,enINap_I_ tuli0ord SlenI
ro_Nrplci,nIwro_El3dIdt)f3PT}
>	r}
sps		 ic;by  lSERVMultiIE"		In	*0);
,nd_masnId;
by  l**ATEpl, by  lie_typeA_CAPd_masneatu_masn, by  lsetupParpl)
TE;_masnNdad_masnjdadby  l*icr_e_masnIwro_Nrplcidadby  l*Iwro_El3dIdtr_eAPPL *PEND, e_masnIwro_Masn =I0, 	by  liesIdt =I0, "fTectlOS (plci-CTmalOS	if (plci-
		  lOS	if_b3_ci
ig
	i-	!	-NCRnault:
		lOS	if setupParpl
OS)
	_3PT].info + 1);
				NoM-IEParpl,"==;	ENbrw", I0, 	}"f].info + 1);
				M-IEParpl,"==;	 Lacility_req"));
MAX_MULTI_IE;dapter-_3PTT& 	IV(rplcir fNDIwro_Nrplci		-0PENDIwro_El3dIdtl=Iie;_R;
	ifie[Nue"ff_3PTY].info + 1);
				[	i-onne]:IEconne",_PENDIN
ig
	i-,lie_typetf("fffI
ro_Nrplci		-=_maskie_type("fffI
ro_Masn =I=_maskiatu_masn;		e}

we
	if_b3_ci
ig
	i-	=	-NCRnault:
		)											R_ORhGINaord w;
lerMbroadcaF_BEGINY_3PTYacilijy_req"j);
max_LCI ;djpter->m{TY_NlCTma	= &LCI ifo[0])cj]("fforummyI
ro_Nrplci"fforrrrrB3_PEND 0tu"fforrrrrB3__b3_ciCrplci G)Iwro_Masn[PEND 0tu - 1] = Iwro_Masner->mY_3PTY_ETesIdt =Ici-ef3PTY_v].info + 1);
				Mlt_NCRnILEntf("fffor("Send&LCI ifo[0])cj],enINap_I_ tum&;0x0fli0ord SlenI
ro_Nrplci,nIwro_El3dIdt)f3PTYrreak;reak}
>S[i] =TRIE (plci-CTma 		}I
ro_Nrplcild ,{                                        /* ovcilap r "eivnfo)broadcaF_BEGINYracilijy_req"j);
max_LCI ;djpter->m{TY_NlTRIEtest_c\ind_masn_bit"ER *a,j	)	, S__3PTY_ETesIdt =Ici-ef3PTY_v].info + 1);
				Mlt_OvlnILEntf("fffor("Send&LCI ifo[0])cj] ,enINap_I_ tuli0ord SlenI
ro_Nrplci,nIwro_El3dIdt)f3PTYrelOSAreak}															                         /* a 0cothci s32
		ling stf("s EGIN:[i] =TRIEI
ro_Nrplci"fforB3__b3_ciCrplci G)Iwro_Masn[E_REQ_PEND 0tu - 1] = Iwro_Masn)"ff_3PTYTesIdt =Ici-ef3PTY].info + 1);
				Mlt_StdnILEntf("fff	"SendE_REQ_PEND,enINap_I_ tuli0ord SlenI
ro_Nrplci,nIwro_El3dIdt)f3PT}
>	rNbrw", ITesIdt;r}
ss		 ic;void SERVSSExtInd(APPL *PEND,e		In	*0);
,nd_masnId;
by  l**ATEpl)
TE;_masnNdad	}
Format of multi_ssex*_ATEplci][]:"f   0
by  lli = 1"f   1;by  lSSEXTIE"f   2;by  lSSEXTLREQ/SSEXTdIat"f   3
by  lli = 1"f   4 _masnSSExtCrplci-"f   6... P(rams
	EGINTectlOS0);
lOS	if_b3_ci
		  lOS	if_b3_ci
ig
	i-	!	-NCRnault:
		lOS)
	Lacility_req"));
MAX_MULTI_IE;dapter-,{ate 
	ifPTEplci][0]m<;6)aord inu ;ate 
	ifPTEplci][2]m	= SSEXTLREQ)aord inu ;aate 
	ifCTmand ,m{TY_Nl(TEplci][0]m	I0,    ki 0ciw */	ls	b("SendLEND,enMANUdf(TURERdI_
TY_prrrr etuA_CAPPrrrr e0A_CAPPrrrr edd Sle_CAPPrrrr eLL__MANUlci,_CAPPrrrr eLL__SSEXTLCTRL(_CAPPrrrr e&(TEplci][3]				seelOSA[i] =TRIE_IS_PECTmand ,m{TY_Nl(TEplci][0]m	I0,    ki 0ciw */	ls	b("SendE_REQ_PEND,enMANUdf(TURERdI_
TY_prrrr etuA_CAPPrrrr e0A_CAPPrrrr edd Sle_CAPPrrrr eLL__MANUlci,_CAPPrrrr eLL__SSEXTLCTRL(_CAPPrrrr e&(TEplci][3]				seelOS	r};
ss		 ic;void nl\ind"		In	*0);
)
TE;by  l)
, e_masnncc_, ed_masnId, eDIVA_CAPI_ADAPTER *P, e_masnNCCIcrolr_eAPPL *APPLptr, e_masncoudtr_e_masnNrpr_e_masn
,nncpi_s		  , 	by  l	el,Bncc__s		  , 	_masnmsgr_e_masn
atu 	-0PEN_masnfax_fearol__bits, 	by  lfax_SERVIe]ata_acn;		s		 ic;by  lv120_header_buffer[2m+a3];		s		 ic;_masnfax_
atu[um	i{lOS0,                     /* T30_SUCCESS                        */	lsnauX_NOnCCONNECION,    /* T30_ERR_NOnLi->RECEIVED            */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_TIME (p_NO	l_SPONSE        */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_RETRY_NO	l_SPONSE          */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_TOO_MANY	l_PEATS           */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_UNEXPNECIL_MESSAGE         */	lsnauX_REMOTE_ABORT,     /* T30_ERR_UNEXPNECIL_DCN             */	lsnauX_LO	 CnABORT,      /* T30_ERR_DTC_UNSUPPORTED            */	lsnauX_TRAININGn		caR,   /* T30_ERR_ CALRATESnauILED           */	lsnauX_TRAININGn		caR,   /* T30_ERR_TOO_MANY	TRAINS            */	lsnauX_PARAMETERn		caR,  /* T30_ERR_RECEIVEnCCRRUPTED          */	lsnauX_REMOTE_ABORT,     /* T30_ERR_UNEXPNECIL_DISC            */	lsnauX_LO	 CnABORT,      /* T30_ERR_APPLeCanai[_DISC           */	lsnauX_REMOTE_REJNEC,    /* T30_ERR_INCOMPanaBLd ciS           */	lsnauX_LO	 CnABORT,      /* T30_ERR_INCOMPanaBLd cCS           */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_TIME (p_NO	COMMAND         */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_RETRY_NO	COMMAND           */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_TIME (p_COMMAND_TOO_LONG   */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_TIME (p_l_SPONSE_TOO_LONG  */	lsnauX_NOnCCONNECION,    /* T30_ERR_NOTdIDENTIFIED             */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_SUPERVISORY_TIME (p        */	lsnauX_PARAMETERn		caR,  /* T30_ERR_TOO_LONG_SCAN_LINE         */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_RETRY_NO	PAGE_AFTERnMPS    */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_RETRY_NO	PAGE_AFTERnCFR    */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_RETRY_NO	cCS_AFTERnFTT     */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_RETRY_NO	cCS_AFTERnEOM     */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_RETRY_NO	cCS_AFTERnMPS     */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_RETRY_NO	cCN_AFTERnMCF     */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_RETRY_NO	cCN_AFTERnRTN     */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_RETRY_NO	CFR               */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_RETRY_NO	MCF_AFTERnEOP     */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_RETRY_NO	MCF_AFTERnEOM     */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_RETRY_NO	MCF_AFTERnMPS     */	ls0x331d,                /* T30_ERR_SUB_SEP_UNSUPPORTED        */	ls0x331e,                /* T30_ERR_PWD_UNSUPPORTED            */	ls0x331f,                /* T30_ERR_SUB_SEP_PWD_UNSUPPORTED    */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_INVALID_COMMAND_FRAME      */	lsnauX_PARAMETERn		caR,  /* T30_ERR_UNSUPPORTED	PAGE_COte =    */	lsnauX_PARAMETERn		caR,  /* T30_ERR_INVALID_PAGE_COte =        */	lsnauX_REMOTE_REJNEC,    /* T30_ERR_INCOMPanaBLd PAGE_CONFIG   */	lsnauX_LO	 CnABORT,      /* T30_ERR_TIME (p_FROM_APPLeCanai[   */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_V34auX_NOnREACiIi[_i[1MARK */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_V34auX_TRAININGnTIME (p    */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_V34auX_UNEXPNECIL_V21      */	lsnauX_PROTOCOLn		caR,   /* T30_ERR_V34auX_PRIMARYLCTS_ON      */	lsnauX_LO	 CnABORT,      /* T30_ERR_V34auX_TURNAROUND_POALING  */	lsnauX_LO	 CnABORT       /* T30_ERR_V34auX_V8_INCOMPanaBt:
		  */	l};
s	by  ldtmf_crol_buffer[CAPIDTMF_RECV ciGIT_BUFFERnSIZEm+a1];3
		s		 ic;_masnrtp_
atu[um	i{lOSGOOD,                  /* RTP_SUCCESS                       */	ls0x3600                 /* RTP_ERR_SSRC_OR_PAYLOAD_CHANGE    */	l};
s	s		 ic;d_masnu]ata_forware		/_tD;
	[0x100 / sizeof(d_mas)um	r-,{ate 0		020301e, 0		0000000, 0		0000000, 0		0000000,ate 0		0000000, 0		0000000, 0		0000000, 0		0000000lOS	 falah i nDINi-NL.IndC
, ea i nDINi-Crplci , encc_ i L <chrncc_[ch];		ci>in!=(d_mas)(ncc_ ?Bncc_ :l)
))
<< 16eca (!=_mask nDING)Is)
<< 8eca aG)Is;	}ummy_b3_citel):Idn|=tEXTd				if tat;	,APPLptr i nDINi-CEND, e].info + 1);
				NALIat-Id(NL:onne)conn08lx,nDIN=%x,tel=%x,s		  conne,chconne,chsc%d,Ind=ne",ate PENDINNL.Id_ tulinDING)IslinDING)tel,f_b3_ci
		  , RI,f_b3_ci(
				&&, nDINi-NL.Indm&;0x0f==;	 L/* in theA)ppl,
	ino ordA "w	act;venILE was sIdt to theACTma	we waitaacil*/	lOummy_b3_cinl\x3d && !p)sw{
e PENDINNL.RNR = 2t /* discalen*/	ls].info + 1);
				NA discalenwhile x3d &&cp"Se		/"==;	ENbrw", ORM}fsTRIE(nDINi-NL.Indm&;0x0f=	=	-NnCCONNEC)r-_3PTT	ifPENDIN
		   	= (NCcLi->State =
orrrrr|a PENDIN
		   	=  (plcLi->State =
orrrrr|a PENDIN
		   	= ((pl)r-,{ate PENDINNL.RNR = 2t /* discalen*/	lsb].info + 1);
				discalenn_ordA "wntf("fffbrw", ORMA}3PTT	ifPENDIN
		   <,(NCcf(T>State =lci-_3PTYPENDINNL.RNR = 1( /* flowaord w;
 EGIN		a
				&Ex_off"ER *a,RI,fN_XONnCCONNECTINDf("fffbrw", ORMA}3P}	lOummy!APPLptr)                         /* no LCI ifo[0])	cilinvalid ]ata EGIN{                                       Bhile x3loading theADSPBBBBBBBB*/	ls].info + 1);
				discale1ntf("ffPENDINNL.RNR = 2t	ENbrw", ORM}f
ETRIE((VE_REQNL.Indm&;0x0f=	=	-NnUDATA)
prrrrB3_X((VE_REQB2_08) &!= B2_SDLC)rB3_X(VE_REQB1	resourc  	= 17)r|a (VE_REQB1	resourc  	= 18)	)	, |a (VE_REQB2_08) &== 7)	, |a (VE_REQB3_08) &== 7)))sw{
e PENDINncpi_buffer[Num	I0, 
e ncpi_s		   i nDINi-ncpi_s		  , 		
	ifPENDINNL.comple   	= 1lci-_3PTYby  l*]ata = &PENDINNL.RBuffer->P[0]f3ate 
	if(PENDINNL.RBuffer->li = 1m>(p12er->mrrrrB3_X(*]ata =	-DSPnUDATA	IateCanai[	cCD_ON)	, S_|a (*]ata =	-DSPnUDATA	IateCanai[	CTS_ON)	)r->m{TY_Nl_masnconn_o/*,nncpi_o/* =I0x00PE        HexDump ("MDM-NnUDATA:", nDINi-NL.RBuffer->li = 1, ]ata); */	lOSA 
	if*]ata =	-DSPnUDATA	IateCanai[	cCD_ON)	, S_	nDINi-ncpi_s		  n|=tNCPI_MDM	cCD_ON>RECEIVED;lOSA 
	if*]ata =	-DSPnUDATA	IateCanai[	CTS_ON)	, S_	nDINi-ncpi_s		  n|=tNCPI_MDM	CTS_ON>RECEIVED;l	, S_]ataptq"      indifo[0])ncrol */	ls	b]ata += 2t /* timfs		mp */	ls	bnectf*]ata =	-DSPnSCONNECIL_NORM_V18)r|a (*]ata =	-DSPnSCONNECIL_NORM_VOWN	)	, S_vncpi_s		  n&= ~(NCPI_MDM	cCD_ON>RECEIVEDca NCPI_MDM	CTS_ON>RECEIVED)f3PTYr]ataptq"      ordA "weennorm */	ls	bconn_o/* =IGE		}
			]ata);	ls	b]ata += 2t /* ordA "weenop[0])s
ans	ls	bPU		}
			&(PENDINncpi_buffer[1])a,=_maskdGE		D}
			]ata)m&;0x0000FFFF==;	 Ls	bnectconn_o/* &-DSPnSCONNECIL_OPnai[	MASK_V42)	, S__3PTY_Encpi_o/* |=tMDM	NCPI_ECM_V42f3PTYrelOSA [i] =TRIE	onn_o/* &-DSPnSCONNECIL_OPnai[	MASK_MNP)	, S__3PTY_Encpi_o/* |=tMDM	NCPI_ECM_MNPf3PTYrelOSA [i] 	, S__3PTY_Encpi_o/* |=tMDM	NCPI_TRANSPARENTf3PTYrelOSA TRIE	onn_o/* &-DSPnSCONNECIL_OPnai[	MASK_COMPRESSION)	, S__3PTY_Encpi_o/* |=tMDM	NCPI_COMPRESSED;lOSA }	ls	bPU		}
			&(PENDINncpi_buffer[3])a,ncpi_o/*f("ffor_IS_PEncpi_buffer[Num	I4;	 Ls	bnDINi-ncpi_s		  n|=tNCPI_VALID_COONNECTB3_Iatca NCPI_VALID_COONNECTB3_f(Tca NCPI_VALID_DISCTB3_Iat			seelOS	r		
	ifPENDINB3_08) &== 7)
-,{ate 
	if( aG)ncc__s		  [ncc_] 	= (NCcf(T>State =lr|a (aG)ncc__s		  [ncc_] 	=  (plcci->State =)er->mrrrrB3_XnDINi-ncpi_s		  n& NCPI_VALID_COONNECTB3_f(Ter->mrrrrB3_!XnDINi-ncpi_s		  n& NCPI_COONNECTB3_f(T_SENT)er->m{TY_NlCG)ncc__s		  [ncc_] 	I(NCcf(T>State =te =foSERVndE_REQ_PEND,enCCONNECTB3_f(TIVE	I_ tuli0ordS", nDINi-ncpi_bufferf("ffor_IS_PEncpi_s		  n|=tNCPI_COONNECTB3_f(T_SENT f		s}		e}

we
	if!f(PENDINrequ		red_op[0])s_ordA | PENDINrequ		red_op[0])s | PENDINCrplci G)requ		red_op[0])s_tD;
	[E_REQ_PEND 0tu - 1])
PPrrrr e&if(1L << PRIVand V18)r| (1L << PRIVand VOWN	))
PPrrrr|a !(ncpi_s		  n& NCPI_MDM	cCD_ON>RECEIVED)
PPrrrr|a !(ncpi_s		  n& NCPI_MDM	CTS_ON>RECEIVED))
r-,{ate PENDINNL.RNR = 2t"fffbrw", ORMA}3P}	lOummyPENDINNL.comple   	= 2)r-_3PTT	if((VE_REQNL.Indm&;0x0f=	=	-NnUDATA)
pmrrrrB3_!Xu]ata_forware		/_tD;
	[VE_REQRData[Nu.P[0] >> 5] = (1L << (VE_REQRData[Nu.P[0] = ((1f)	))
PP{ate 	;
		} /DE_REQRData[Nu.P[0]er->m{TIN		appl,DTMF_UDATA	IateCanai[	auX_	 CAINGnTONE:TY_NlummyVE_REQdtmf_rec	act;ve &-DTMF_LISTENLf(TIVE	FLAG)	, S_v("SendE_REQ_PEND,enault:
			I_ tu = ((ffffLli0ord >lenaELNECOR_DTMF,;"			1X")f3PTY_eturn fN		appl,DTMF_UDATA	IateCanai[	ANSWatnTONE:TY_NlummyVE_REQdtmf_rec	act;ve &-DTMF_LISTENLf(TIVE	FLAG)	, S_v("SendE_REQ_PEND,enault:
			I_ tu = ((ffffLli0ord >lenaELNECOR_DTMF,;"			1Y")f3PTY_eturn fN		appl,DTMF_UDATA	IateCanai[	ciGIT->RECEIVED:		srE]tmf_indifo[0])(		 dprin, DE_REQRData[Nu.P, DE_REQRData[Nu.PLi = 1f("fforeturn fN		appl,DTMF_UDATA	IateCanai[	ciGIT->SENT:		srE]tmf_ordfirmat0])(		 dprin)f3PTY_eturn fafN		appl,UDATA	IateCanai[	MIXatnTAP_DATA:	ls	bcapidtmf_recv_08)cess_block	&(PENDINcapidtmf_s		  ), DE_REQRData[Nu.Pm+a1a,=_maskdE_REQ_RData[Nu.PLi = 1 - 1tf("ffTYTy_rcapidtmf_indifo[0])(&(PENDINcapidtmf_s		  ), dtmf_crol_bufferm+a1f("fforummyi&!= 0)"ffor{TY_Nl ]tmf_crol_buffer[Num	IDTMF_UDATA	IateCanai[	ciGIT->RECEIVED;TY_Nl ]tmf_indifo[0])(		 dprin, ]tmf_crol_buffera,=_maskdim+a1f);				belOSAleturn fafN		appl,UDATA	IateCanai[	MIXatnCOEF->SET:		srENECT:
indifo[0])_crefs
set(		 dprin)f3PTY_eturn fN		appl,UDATA	IateCanai[	XCOONNECTFROM:		srENECT:
indifo[0])_xordA "w	from(		 dprin, DE_REQRData[Nu.P, DE_REQRData[Nu.PLi = 1f("fforeturn fN		appl,UDATA	IateCanai[	XCOONNECTTO:		srENECT:
indifo[0])_xordA "w	to(		 dprin, DE_REQRData[Nu.P, DE_REQRData[Nu.PLi = 1f("fforeturn fafN		appl,LNE_UDATA	IateCanai[	ciSABLd cETNEC:		srEec_indifo[0])(		 dprin, DE_REQRData[Nu.P, DE_REQRData[Nu.PLi = 1f("fforeturn ffafN		default:"fforeturn f	sselOSelOSONG_STATE;
	t	if(PENDINRData[Nu.PLi = 1 != 0)"fforrrrB3_X(VE_REQB2_08) &== B2_V120_ASYNC)	, S_|a (VE_REQB2_08) &== B2_V120_ASYNC_V42BIS)	, S_|a (VE_REQB2_08) &== B2_V120_BIT_TRANSPARENT))er->m{TIN		Y	"SendE_REQ_PEND,enLATA	B3_I,acili0o

		 rrrr edd )wle_CAPPrrrr ePENDINRData[1u.P,_CAPPrrrr e(PENDINNL.RNum < 2) ? 0 :ePENDINRData[1u.PLi = 1e_CAPPrrrr ePENDINRNume_CAPPrrrr ePENDINRFlags=;	 Ls	}ate ci);
Y_E{TIN		Y	"SendE_REQ_PEND,enLATA	B3_I,acili0o

		 rrrr edd )wle_CAPPrrrr ePENDINRData[0u.P,_CAPPrrrr ePENDINRData[Nu.PLi = 1e_CAPPrrrr ePENDINRNume_CAPPrrrr ePENDINRFlags=;	 Ls	}ate}atebrw", ORM}f
Efax_fearol__bits 	-0PENTRIE(nDINi-NL.Indm&;0x0f=	=	-NnCCONNECr|a
prrrr(nDINi-NL.Indm&;0x0f=	=	-NnCCONNEC_ACKr|a
prrrr(nDINi-NL.Indm&;0x0f=	=	-NnDISC |a
prrrr(nDINi-NL.Indm&;0x0f=	=	-NnELATAr|a
prrrr(nDINi-NL.Indm&;0x0f=	=	-NnDISC_ACK)r-_3PTTatu 	-0PEN PENDINncpi_buffer[Num	I0, e 	;
		} /DE_REQB3_08) ) _3PTappl, 0: /*XPARENT*/	lsappl, 1: /*T.90 NL*/	ls	_REQ_P    /* no net_maNaord w;
 08) oco&Beatu - jfi	*/	lsappl, 2: /*ISO8202*/	lsappl, 3: /*X25 DCEEGINYracility_req"));
PENDINNL.RLi = 1;dapte PENDINncpi_buffer[4m+ai] i nDINi-NL.RBuffer->P[ir fND;PENDINncpi_buffer[Num	I(by  kdim+a3) fND;PENDINncpi_buffer[1um	I(by  kdnDINi-NL.Indm&;NnD_BIT ? 1 :e0) fND;PENDINncpi_buffer[2]m	I0, LLCPENDINncpi_buffer[3]m	I0, LLC_REQ_PEND)ppl, 4: /*T.30 - auX*/	lsappl, 5: /*T.30 - auX*/	lsOummyPENDINNL.RLi = 1m>(psizeof(T30_INap)er->m{TY_Nl].info + 1);
				Fax
		 us %04e",_((T30_INap *)nDINi-NL.RBuffer->P) <crolf);				blenm	I9("fforPU		}
			&(PENDINncpi_buffer[1])a,=(T30_INap *)nDINi-NL.RBuffer->P) <r	  _div_2400 * 2400e, LLCb;ax_fearol__bits 	-GE		}
			&=(T30_INap *)nDINi-NL.RBuffer->P) <fearol__bits_lowf("ffTYTy_r(=(T30_INap *)nDINi-NL.RBuffer->P) <resolu[0])n& T30_RESOLUnai[	R8_0770_OR_2y0e ?;0x0001 :e0x0000("fforummyPENDINB3_08) &== 5er->mY_3PTY_ETRIE!(;ax_fearol__bits & T30_FEATURE_BIT_ECM	)	, S_vYTy|	-0	8000t /* ThisMis   oran ECM ordA "w0])n*/	ls	bETRIE;ax_fearol__bits & T30_FEATURE_BIT_T6_COte =)	, S_vYTy|	-0	4000t /* ThisMis a ordA "w0])nBERS:MMR compress0])n*/	ls	bETRIE;ax_fearol__bits & T30_FEATURE_BIT_2D_COte =)	, S_vYTy|	-0	2000t /* ThisMis a ordA "w0])nBERS:MR compress0])n*/	ls	bETRIE;ax_fearol__bits & T30_FEATURE_BIT_MORE_DOCUMENTS)	, S_vYTy|	-0	0004t /* MorlrdocudIdtsn*/	ls	bETRIE;ax_fearol__bits & T30_FEATURE_BIT_POALING)	, S_vYTy|	-0	0002t /* Fax-po	ling indifo[0])n*/	ls	belOSAld.info + 1);
				auX O/* NCsa%04e %04e",_;ax_fearol__bitsum
	f("fforPU		}
			&(PENDINncpi_buffer[3])a,if("fforPU		}
			&(PENDINncpi_buffer[5])a,=(T30_INap *)nDINi-NL.RBuffer->P) <]ata_formatf("ffor_IS_PEncpi_buffer[7um	I((T30_INap *)nDINi-NL.RBuffer->P) <pages_low("ffor_IS_PEncpi_buffer[8um	I((T30_INap *)nDINi-NL.RBuffer->P) <pages_high("ffor_IS_PEncpi_buffer[len]m	I0, LLCbT	if((T30_INap *)nDINi-NL.RBuffer->P) <s		 i])_id_	eler->mY_3PTY_E_IS_PEncpi_buffer[len]m	I20PEND;
racility_req"));
T30_MAX_STanai[	ID_LENGTH;dapter->mY_E_IS_PEncpi_buffer[++len]m	I((T30_INap *)nDINi-NL.RBuffer->P) <s		 i])_idcir fND;E} LLCbT	if((nDINi-NL.Indm&;0x0f=	=	-NnDISClr|a ((nDINi-NL.Indm&;0x0f=	=	-NnDISC_ACK)er->mY_3PTY_ETRIE((T30_INap *)nDINi-NL.RBuffer->P) <crol);
ARRAYnSIZE(fax_
atu	)	, S_vYTatu 	-fax_
atu[((T30_INap *)nDINi-NL.RBuffer->P) <crol];3PTY_:[i] 	, S_vYTatu 	-nauX_PROTOCOLn		caR fND;E}  LLCbT	if(PENDINrequ		red_op[0])s_ordA | PENDINrequ		red_op[0])s | aG)requ		red_op[0])s_tD;
	[E_REQ_PEND 0tu - 1])
PPforrrrBif(1L << PRIVand auX_SUB_SEP_PWD)r| (1L << PRIVand auX_NONSTaNDARD))er->mY_3PTY_ET 	-offsetof(T30_INap, s		 i])_id)m+aT30_MAX_STanai[	ID_LENGTHm+a((T30_INap *)nDINi-NL.RBuffer->P) <head_line_	el;3PTY_:Bhile ());
PENDINNL.RBuffer->li = 1er->mY_E_IS_PEncpi_buffer[++len]m	InDINi-NL.RBuffer->P[i++] fND;E}  LLCbPENDINncpi_buffer[Num	I	el;3PTY_;ax_fearol__bits 	-GE		}
			&=(T30_INap *)nDINi-NL.RBuffer->P) <fearol__bits_lowf("ffTYPU		}
			&((T30_INap *)nDINi-;ax_ordA "w	iatu_bufferf <fearol__bits_low,_;ax_fearol__bits);	 Ls	bnDINi-ncpi_s		  n|=tNCPI_VALID_COONNECTB3_Iat; LLCbT	if((nDINi-NL.Indm&;0x0f=	=	-NnCCONNEC_ACK)
PPforrrr|a (((nDINi-NL.Indm&;0x0f=	=	-NnCCONNEC)r-----B3_X;ax_fearol__bits & T30_FEATURE_BIT_POALING))
PPforrrr|a (((nDINi-NL.Indm&;0x0f=	=	-NnEDATA)
pm---B3_XE((T30_INap *)nDINi-NL.RBuffer->P) <crol)=	-EDATA_T30_TRAIN_OKlci-EYorrrr|a (((T30_INap *)nDINi-NL.RBuffer->P) <crol)=	-EDATA_T30_DIS)	, S_orrrr|a (((T30_INap *)nDINi-NL.RBuffer->P) <crol)=	-EDATA_T30_DTC)))er->mY_3PTY_EnDINi-ncpi_s		  n|=tNCPI_VALID_COONNECTB3_ACTf3PTYrelOSA TRIE((nDINi-NL.Indm&;0x0f=	=	-NnDISCl
PPforrrr|a ((nDINi-NL.Indm&;0x0f=	=	-NnDISC_ACK)r-Pforrrr|a (((nDINi-NL.Indm&;0x0f=	=	-NnEDATA)
pm---B3_XE(T30_INap *)nDINi-NL.RBuffer->P) <crol)=	-EDATA_T30_EOP_CAPI))er->mY_3PTY_EnDINi-ncpi_s		  n|=tNCPI_VALID_COONNECTB3_ACTca NCPI_VALID_DISCTB3_Iat			serelOSAreak_eturn falsappl,B3_RTP:e =bT	if((nDINi-NL.Indm&;0x0f=	=	-NnDISClr|a ((nDINi-NL.Indm&;0x0f=	=	-NnDISC_ACK)er->m{"fforummyPENDINNL.RLi = 1m!= 0)"ffor{TY_Nl Tatu 	-rtp_
atu[PENDINNL.RBuffer->P[0]];3PTY_:PENDINncpi_buffer[Num	IPENDINNL.RLi = 1m- 1PEND;
racility_r1q"));
PENDINNL.RLi = 1;dapter->mY_E_IS_PEncpi_buffer[i] i nDINi-NL.RBuffer->P[ir fND;relOSAreak_eturn fals}
e PENDINNL.RNR = 2t"f}
e	;
		} /DE_REQNL.Indm&;0x0f=	{fsappl,NnEDATA:		sTRIEyPENDINB3_08) &== 4)r|a (VE_REQB3_08) &== 5e)
PP{ate ].info + 1);
					LATArncc_conne stf("=%dncrol=%02e",_ncc_, L <ncc__s		  [ncc_]A_CAPPP((T30_INap *)nDINi-NL.RBuffer->P) <crolf);				fax_SERVIe]ata_acny_r(=(T30_INap *)(nDINi-;ax_ordA "w	iatu_bufferf) <oper	 		/_mrol)=	-T30_OPERATI			MODE_CAPI_NEG)f3ate 
	if(PENDINnsf_ord w;
_bits & T30_NSFd				if _BIT_ENABLd NSFer->mrrrrB3_XnDINi-nsf_ord w;
_bits & (T30_NSFd				if _BIT_NEGOTIand Iatca T30_NSFd				if _BIT_NEGOTIand l_SP)er->mrrrrB3_X((T30_INap *)nDINi-NL.RBuffer->P) <crol)=	-EDATA_T30_DIS)	, SrrrrB3_XaG)ncc__s		  [ncc_] 	=  (plcci->State =)r->mrrrrB3_XnDINi-ncpi_s		  n& NCPI_VALID_COONNECTB3_f(Ter->mrrrrB3_!XnDINi-ncpi_s		  n& NCPI_NEGOTIand B3_SENT)er->m{TY_Nl=(T30_INap *)(nDINi-;ax_ordA "w	iatu_bufferf) <crol)=_((T30_INap *)nDINi-NL.RBuffer->P) <crolte =foSERVndE_REQ_PEND,enMANUdf(TURERdI_ tuli0orddwbS"rnLL__MANUlci,nnDI_NEGOTIand B3,_CAPPrrrr e(by  kdnDINi-ncpi_buffer[Num+a1f, nDINi-ncpi_bufferf("ffor_IS_PEncpi_s		  n|=tNCPI_NEGOTIand B3_SENT("fforummyPENDINnsf_ord w;
_bits & T30_NSFd				if _BIT_NEGOTIand l_SP)END;
raax_SERVIe]ata_acny_rfalsef3PTY}aate 
	ifCINmrnudburol_r_fearol_s & MANUdf(TURERdFEATURE_auX_PAPERdFORMATS)	, S{TY_Nl	;
		} /((T30_INap *)nDINi-NL.RBuffer->P) <crolf"ffor{TY_Nlappl,EDATA_T30_DIS:3PTY_ETRIE(aG)ncc__s		  [ncc_] 	=  (plcci->State =)r->m>mrrrrB3_!XGE		}
			&=(T30_INap *)nDINi-;ax_ordA "w	iatu_bufferf <ord w;
_bits_lowf & T30_				if _BIT_REQUEST_POALING)	, S_vrrrrB3_XnDINi-ncpi_s		  n& NCPI_VALID_COONNECTB3_f(Ter->m>mrrrrB3_!XnDINi-ncpi_s		  n& NCPI_COONNECTB3_f(T_SENT)er->m_v_3PTY_E CG)ncc__s		  [ncc_] 	I(NCcf(T>State =te =foorummyPENDINB3_08) &== 4er->mY_EoSERVndE_REQ_PEND,enCCONNECTB3_f(TIVE	I_ tuli0ords", "1i("ffo	_:[i] 	, S_vYoSERVndE_REQ_PEND,enCCONNECTB3_f(TIVE	I_ tuli0ordS", nDINi-ncpi_bufferf("fforor_IS_PEncpi_s		  n|=tNCPI_COONNECTB3_f(T_SENT f		sEl}3PTY_Eeturn fals		appl,EDATA_T30_TRAIN_OK:3PTY_ETRIE(aG)ncc__s		  [ncc_] 	= (NCcf(T>State =lci-S_vrrrrB3_XnDINi-ncpi_s		  n& NCPI_VALID_COONNECTB3_f(Ter->m>mrrrrB3_!XnDINi-ncpi_s		  n& NCPI_COONNECTB3_f(T_SENT)er->m_v_3PTY_E ummyPENDINB3_08) &== 4er->mY_EoSERVndE_REQ_PEND,enCCONNECTB3_f(TIVE	I_ tuli0ords", "1i("ffo	_:[i] 	, S_vYoSERVndE_REQ_PEND,enCCONNECTB3_f(TIVE	I_ tuli0ordS", nDINi-ncpi_bufferf("fforor_IS_PEncpi_s		  n|=tNCPI_COONNECTB3_f(T_SENT f		sEl}3PTY_Eeturn fals		appl-EDATA_T30_EOP_CAPI:3PTY_ETRIEaG)ncc__s		  [ncc_] 	= SCONNECILer->m_v_3PTY_E 	"SendE_REQ_PEND,enLi-CCONNECTB3_I,acili0ord SlenGOOD, nDINi-ncpi_bufferf("ffororCG)ncc__s		  [ncc_] 	I(NCcLi->State =PEND;Yor_IS_PEncpi_s		  n	-0PEND;
m	aax_SERVIe]ata_acny_rfalsef3PTYEl}3PTY_Eeturn fTYEl}3PTY}ate ci);
Y_E{TY_E 	;
		} /((T30_INap *)nDINi-NL.RBuffer->P) <crolf"ffor{TY_Nlappl,EDATA_T30_TRAIN_OK:3PTY_ETRIE(aG)ncc__s		  [ncc_] 	= (NCcf(T>State =lci-S_vrrrrB3_XnDINi-ncpi_s		  n& NCPI_VALID_COONNECTB3_f(Ter->m>mrrrrB3_!XnDINi-ncpi_s		  n& NCPI_COONNECTB3_f(T_SENT)er->m_v_3PTY_E ummyPENDINB3_08) &== 4er->mY_EoSERVndE_REQ_PEND,enCCONNECTB3_f(TIVE	I_ tuli0ords", "1i("ffo	_:[i] 	, S_vYoSERVndE_REQ_PEND,enCCONNECTB3_f(TIVE	I_ tuli0ordS", nDINi-ncpi_bufferf("fforor_IS_PEncpi_s		  n|=tNCPI_COONNECTB3_f(T_SENT f		sEl}3PTY_Eeturn fTYEl}3PTY}ate TRIE;ax_SERVIe]ata_acner->m{TY_Nl=(T30_INap *)(nDINi-;ax_ordA "w	iatu_bufferf) <crol)=_((T30_INap *)nDINi-NL.RBuffer->P) <crolte =fonDINi-;ax_e]ata_acn_li = 1m= 1PEND;
s		r*_ = 0;
				rplci-(		 dprin,_;ax_e]ata_acn_	rplci-				seelOS}
>S[i] d ,{ate ].info + 1);
					LATArncc_conne stf("=%d",_ncc_, L <ncc__s		  [ncc_]))ORMAelOSeturn fsappl,NnCOONNEC:		sTRIE!L <chrncc_[ch])
PP{ate ncc_ i get_ncc_"ER *a,RI,f0e, LLCci>in!tu = ((ffffeca (!=d_mas)_ncc_)
<< 16eORMAelOS].info + 1);
				NnCOONNEC:,RI=%dnstf("=%dnER *=%lxnER *_Id=%lxnER *_Stf("=%d",TY_Nlah, L <ncc__s		  [ncc_]A L <ncc__ER *[ncc_]A nDINi-IslinDING)Stf("==;	 Lsmsg =I_COONNECTB3_I;
_ETRIEaG)ncc__s		  [ncc_] 	= ((pl)r-,onDINi-(
				&&++;
SA[i] =TRIE_IS_PEB3_08) &== 1lci-smsg =I_COONNECTB3_T90_f(TIVE	I;	 LsCG)ncc__s		  [ncc_] 	I(NCcci->State =;
_ETRIEPENDINB3_08) &== 4er->mSERVndE_REQ_PEND,emsg_ tuli0ords", "1i("ff[i] 	, SSERVndE_REQ_PEND,emsg_ tuli0ordS", nDINi-ncpi_bufferf("ffeturn fsappl,NnCOONNEC_ACK:lOS].info + 1);
				NnordA "w	Ackntf("ffummyVE_REQ = 0;
				rplci-_queue[Nu
forrrrB3_X(VE_REQadjust_b_s		  n	= ADJUST_BnCOONNEC_2)	, S|a (VE_REQadjust_b_s		  n	= ADJUST_BnCOONNEC_3)	, S|a (VE_REQadjust_b_s		  n	= ADJUST_BnCOONNEC_4	))
PP{ate (*yVE_REQ = 0;
				rplci-_queue[Nu))(		 dprin,_0e, LLCTRIE (plci- = 0;
				rplci-er->mYnex*_ = 0;
				rplci-(		 dprin				s eturn fTY} Lsmsg =I_COONNECTB3_f(TIVE	I;	_ETRIEPENDINB3_08) &== 1lci-_3PTYTRIEaG)ncc__s		  [ncc_] !=  (plcci->State =)r->m>msg =I_COONNECTB3_T90_f(TIVE	I;	rorCG)ncc__s		  [ncc_] 	I(NCcf(T>State =te =fSERVndE_REQ_PEND,emsg_ tuli0ordS", nDINi-ncpi_bufferf("ff}
>S[i] =TRIEyPENDINB3_08) &== 4)r|a (VE_REQB3_08) &== 5er|a (VE_REQB3_08) &== 7)lci-_3PTYTRIEXaG)ncc__s		  [ncc_] 	=  (plcci->State =)r->mrrrrB3_XnDINi-ncpi_s		  n& NCPI_VALID_COONNECTB3_f(Ter->mrrrrB3_!XnDINi-ncpi_s		  n& NCPI_COONNECTB3_f(T_SENT)er->m{TY_NlCG)ncc__s		  [ncc_] 	I(NCcf(T>State =te =foummyPENDINB3_08) &== 4er->mY_SERVndE_REQ_PEND,emsg_ tuli0ords", "1i("ff_:[i] 	, S_vSERVndE_REQ_PEND,emsg_ tuli0ordS", nDINi-ncpi_bufferf("ffor_IS_PEncpi_s		  n|=tNCPI_COONNECTB3_f(T_SENT f		s}		e}
>S[i] d ,{ate CG)ncc__s		  [ncc_] 	I(NCcf(T>State =te =fSERVndE_REQ_PEND,emsg_ tuli0ordS", nDINi-ncpi_bufferf("ff}
>STect_b3_ciCrjust_b_restorelci-_3PTY_b3_ciCrjust_b_restorey_rfalsef3PTYs		r*_ = 0;
				rplci-(		 dprin,_Crjust_b_restorelORMAelOSeturn fsappl,NnLi-C:fsappl,NnLi-C_ACK:lOSummyVE_REQ = 0;
				rplci-_queue[Nu
forrrrB3_X(VE_REQ = 0;
				rplci-		= auXnLi-CCONNECTCOMMAND_1)	, S|a (VE_REQ = 0;
				rplci-		= auXnLi-CCONNECTCOMMAND_2)	, S|a (VE_REQ = 0;
				rplci-		= auXnLi-CCONNECTCOMMAND_3	))
PP{ate (*yVE_REQ = 0;
				rplci-_queue[Nu))(		 dprin,_0e, LLCTRIE (plci- = 0;
				rplci-er->mYnex*_ = 0;
				rplci-(		 dprin				selOSncc__s		   i L <ncc__s		  [ncc_];lOSncc__		d &&ALERT,_ncc_, false=;	 Ls   BERS:NnDISC or,NnLi-C_ACK theAIDI frees theArespect;ve BB*/	ls/* )
				&, so	we c			) &storeytheAs		   inBncc__s		  ! TheB*/	ls/* iaturmat0]) Bhi	} )
				&	we r "eivcd a:NnDISC is thus BB*/	ls/* stored in theAinc_dis_ncc__tD;
	 buffer.                 */	lsacility_req"(plci- =c_dis_ncc__tD;
	[ir daptePEN PENDIN =c_dis_ncc__tD;
	[irm	I(by  knncc_, 	ls/* necd a:ordA "w	b3_ind beforeya discrdA "w	b3_ind BERS:auX */	lsummy (plci-(
				&&
>mrrrrB3_XnDINi-B1	resourc  	= 16)
>mrrrrB3_XnDINi-
		   <= SCONNECILe)
PP{ate lenm	I9("ffoi)=_((T30_INap *)nDINi-;ax_ordA "w	iatu_bufferf <r	  _div_2400 * 2400("ffoPU		}
			&nDINi-ncpi_buffer[1],	if("ffoPU		}
			&nDINi-ncpi_buffer[3],_0e, LLCT)=_((T30_INap *)nDINi-;ax_ordA "w	iatu_bufferf <]ata_format("ffoPU		}
			&nDINi-ncpi_buffer[5],	if("ffoPU		}
			&nDINi-ncpi_buffer[7],_0e, LLCnDINi-ncpi_buffer[len]m	I0, LLCPENDINncpi_buffer[Num	I	el;3PTYummyPENDINB3_08) &== 4er->mYSERVndE_REQ_PEND,enCCONNECTB3_I_ tuli0ords", "1i("ffoci);
Y_E{TIN		YT	if(PENDINrequ		red_op[0])s_ordA | PENDINrequ		red_op[0])s | aG)requ		red_op[0])s_tD;
	[E_REQ_PEND 0tu - 1])
PPforrrrBif(1L << PRIVand auX_SUB_SEP_PWD)r| (1L << PRIVand auX_NONSTaNDARD))er->mY_3PTY_EPENDINncpi_buffer[++len]m	I0PEND;
mPENDINncpi_buffer[++len]m	I0PEND;
mPENDINncpi_buffer[++len]m	I0PEND;
mPENDINncpi_buffer[Num	I	el;3PTY_}TIN		Y	"SendE_REQ_PEND,enCCONNECTB3_I_ tuli0ordS", nDINi-ncpi_bufferf("ffo}e =fSERVndE_REQ_PEND,enLi-CCONNECTB3_I,acili0ord Sleniatu, nDINi-ncpi_bufferf("ffo_IS_PEncpi_s		  n	-0PEND;x32\x30"ER *a,HANGUPre0f("ffoSERVICE, 0);
f("ffo_IS_PE
te & 	I (plcLi->State =("ffo/* disc hereB*/	ls}
>S[i] =TRIEyCINmrnudburol_r_fearol_s & MANUdf(TURERdFEATURE_auX_PAPERdFORMATS)	, SrB3_X(VE_REQB3_08) &== 4)r|a (VE_REQB3_08) &== 5e)
PPSrB3_X(ncc__s		   i=,(NCcLi->State =er|a (ncc__s		   i=,((pl))lci-_3PTYTRIEncc__s		   i=,((pl)r->m{TY_NlummyPENDIN(
				&&)END;
mPENDIN(
				&&--;IN		YT	if(PENDIN
		   	= ((plr|a PENDIN
		   	= SUSState =er	if (plci-(
				&&) _3PTY_ETRIEPENDIN
		   	= SUSState =er_3PTY_E 	"SendE_REQ_PEND,3PTY_E       nault:
			I_3PTY_E       tu = ((ffffLl3PTY_E       0l3PTY_E       d >len=_mask3, "			3			4			0			01i("ffo	_:	"SendE_REQ_PEND,enLi-CCONNECTI,aci = ((ffffLli0ord "re0f("ffoEl}3PTY_EER *_		d &&ALERT)PEND;
mPENDIN
te & 	I((pl fTYEl}3PTY}ate}
>S[i] =TRIEPENDIN(
				&&)END{e =fSERVndE_REQ_PEND,enLi-CCONNECTB3_I,acili0ord Sleniatu, nDINi-ncpi_bufferf("ffo_IS_PEncpi_s		  n	-0PEND;T	if(ncc__s		   i=, (plcREJ>State =)r->mrrrrB3_X(VE_REQB3_08) &!= B3_T90NL)rB3_XnDINi-B3_08) &!= B3_ISO8208)rB3_XnDINi-B3_08) &!= B3_X25_DCE)	)r->m{TY_Nlx32\x30"ER *a,HANGUPre0f("ffooSERVICE, 0);
f("ffoo_IS_PE
te & 	I (plcLi->State =("ffoelOS}
>Seturn fsappl,NnRESET:		sCG)ncc__s		  [ncc_] 	I(NCcRES>State =("ffSERVndE_REQ_PEND,enRESETTB3_I_ tuli0ordS", nDINi-ncpi_bufferf("ffeturn fsappl,NnRESET_ACK:lOSCG)ncc__s		  [ncc_] 	ISCONNECIL("ffSERVndE_REQ_PEND,enRESETTB3_I_ tuli0ordS", nDINi-ncpi_bufferf("ffeturn ffsappl,NnUDATA:		sTRIE!Xu]ata_forware		/_tD;
	[VE_REQNL.RBuffer->P[0] >> 5] = (1L << (VE_REQNL.RBuffer->P[0] = ((1f)	))
PP{ate VE_REQRData[Nu.Pm="(plci- = 0;
			ind_bufferm+a(-(( = )(long)yVE_REQ = 0;
			ind_buffer)f & 3) fND;PENDINRData[Nu.PLi = 1 	I(NTERNAALIat_BUFFERnSIZE fND;PENDINNL.Rm="(plci-RData fND;PENDINNL.RNum = 1PEND;brw", ORMA}3Pappl,NnBDATA:		appl,NnLATA:		sTRIEyEaG)ncc__s		  [ncc_] != SCONNECILerB3_XnDINi-B2_08) &== 1l) /* transparIdt */	lsrrrr|a (aG)ncc__s		  [ncc_] 	= ((pl)r-,rrrr|a (aG)ncc__s		  [ncc_] 	= (NCcLi->State =e)
PP{ate VE_REQNL.RNR = 2t"fffeturn fTY} LsTRIEXaG)ncc__s		  [ncc_] != SCONNECILer->rrrrB3_XaG)ncc__s		  [ncc_] !	I (plcLi->State =er->rrrrB3_XaG)ncc__s		  [ncc_] !	I (plcREJ>State =))
PP{ate ].info + 1);
				flowaord w;
ntf("fffVE_REQNL.RNR = 1( /* flowaord w;
  EGIN		a
				&Ex_off"ER *a,RI,f0				s eturn fTY} fTYNCCIcrol = ncc_ a (!=_maskaG)Is)
<< 8e, 	ls/* coudt a 0cbuffers BERSin theAACI ifo[0])	po;
  BB*/	ls/* belonging to theAsamenNCCI. If thisMis below theA*/	ls/* nrplci	ofcbuffers availD;
	 pci	NCCI	we accept BB*/	ls/* thisMpacket,cothciwipl,we r j "wciw              */	lscoudt 	-0PENDNum = 0xfffftateacility_req"));
APPLptr->MaxBuffer;dapte _3PTsTRIENCCIcrol ==
APPLptr->DataNCCI[ir) coudt++;
SAOummy!APPLptr->DataNCCI[irrB3_Num == ((ffffecNum = i fTY} fTYTRIE	oudt >=
APPLptr->MaxNCCIDatar|a Num == ((ffffe
PP{ate ].inf3 + 1);
				alow-Crd w;
ntf("fffVE_REQNL.RNR = 1(
SAOummy++(APPLptr->NCCIDataalowCtrlTimcil >=r->mrrrr=_maskdyCINmrnudburol_r_fearol_s & MANUdf(TURERdFEATURE_OOB_CHANNELe ?;40 :e2000	)r->m{TY_NlVE_REQNL.RNR = 2t"fff ].inf3 + 1);
				DiscaleDatantf("fff} [i] ={TY_Nla
				&Ex_off"ER *a,RI,f0				s }
fffeturn fTY} Ls[i] d ,{ate APPLptr->NCCIDataalowCtrlTimci 	-0PEND} fTYVE_REQRData[Nu.Pm="R "eivcBufferGet(APPLptr, Numf("ffummy!VE_REQRData[Nu.Pe _3PTsVE_REQNL.RNR = 1(
SAOa
				&Ex_off"ER *a,RI,f0				s eturn fTY} fTYAPPLptr->DataNCCI[Num] 	INCCIcrolr_eYAPPLptr->DataFlags[Num] 	I(nDINi-Is
<< 8eca /DE_REQNL.Indm>> 4				s].inf3 + 1);
				Buffer(%d), Max 	I%d",_Nume
APPLptr->MaxBuffer==;	 LsVE_REQRNum = Nrpr_esVE_REQRFlags i nDINi-NL.Indm>> 4;fTYVE_REQRData[Nu.PLi = 1 	IAPPLptr->MaxDataLi = 1;
D;PENDINNL.Rm="(plci-RData fND
	if(PENDINNL.RLi = 1m!= 0)"ffrrrrB3_X(VE_REQB2_08) &== B2_V120_ASYNC)	, S|a (VE_REQB2_08) &== B2_V120_ASYNC_V42BIS)	, S|a (VE_REQB2_08) &== B2_V120_BIT_TRANSPARENT))er->{ate VE_REQRData[1u.Pm="(plci-RData[Nu.P fND;PENDINRData[1u.PLi = 1 	IVE_REQRData[Nu.PLi = 1 fND;PENDINRData[Nu.P 	Iv120_header_bufferm+a(-((uns32
ed long)v120_header_bufferf & 3) fND;
	if(PENDINNL.RBuffer->P[0] = V120_HEADERn	XTEND_BIT)r|a (VE_REQNL.RLi = 1m== 1l)TY_NlVE_REQRData[Nu.PLi = 1 	I1("ffoci);
Y_ElVE_REQRData[Nu.PLi = 1 	I2;3PTYummyPENDINNL.RBuffer->P[0] = V120_HEADERnBREAK_BIT)
Y_ElVE_REQRFlags |	-0	0010;3PTYummyPENDINNL.RBuffer->P[0] = (V120_HEADERnC1_BIT | V120_HEADERnC2_BIT))
Y_ElVE_REQRFlags |	-0	8000("ffoPENDINNL.RNum = 2 fTY} Ls[i] d ,{ate TRIE(nDINi-NL.Indm&;0x0f=	=	-NnUDATA)
pm--VE_REQRFlags |	-0	0010;3"ffoci);=TRIEyPENDINB3_08) &== B3_RTP)rB3_X(VE_REQNL.Indm&;0x0f=	=	-NnBDATA))
Y_ElVE_REQRFlags |	-0	0001;3"ffoPENDINNL.RNum = 1PENDelOSeturn fsappl,NnLATA_ACK:lOS]ata_acn"ER *a,RIf("ffeturn fsdefault:"ffVE_REQNL.RNR = 2t"ffeturn fs	r}
s/*------------------------------------------------------------------*/	/* find a freee		In	*/s/*------------------------------------------------------------------*/	ss		 ic;_masnget_VE_R(DIVA_CAPI_ADAPTER *P)
TE;_masnN,njdad		In	*0);
;3"fdump_VE_Rs(a);	lacility_req"));
CINmrx_VE_RrB3_P->VE_R[ir.Id daptePENummyi&==
CINmrx_VE_Re _3PT].info + 1);
				get_VE_R: out of 		Ins"==;	ENbrw", I0, 	}"fVE_Rr= &L->VE_R[ir;
	nDINi-Is
	I(by  kdim+a1=;	 L_IS_PE
ig
	d 	-0PENVE_REQNL.Id 	-0PENVE_REQx32\x30 	-0PENVE_REQnl\x3qm	I0, 
eE_REQ_PEND = NULLPENVE_REQrel	  dPTY		In	= NULLPENVE_REQ
te & 	I((pl fTVE_REQ
upp
te & 	I((pl fTVE_REQ(
				&& 	-0PENVE_REQtel 	-0PENVE_REQB1	resourc  	-0PENVE_REQB2_08) &=-0PENVE_REQB3_08) &=-0PEfTVE_REQ(rplci-		-0PENVE_REQm_(rplci-		-0PENini*_ = 0;
				rplci-_queue 0);
f("fVE_REQnrplci		-0PENPENDINreq_ =_s		r*		-0PENPENDINreq_ =		-0PENPENDINreq_out 	-0PENVE_REQmsg_ =_write_po& 	-MSGLIa_QUEUEnSIZE fNVE_REQmsg_ =_read_po& 	-MSGLIa_QUEUEnSIZE fNVE_REQmsg_ =_wrap_po& 	-MSGLIa_QUEUEnSIZE ffNVE_REQ]ata_sIdt =Ifalsef3PVE_REQxERVIdisc 	-0PENVE_REQx32\global\x30 	-0PENVE_REQx32\x3d && !p 	-0PENVE_REQnl\global\x30 	-0PENVE_REQnl\x3d && !p 	-0PENVE_REQadv_nl 	-0PENVE_REQmrnudburol_r =Ifalsef3PVE_REQca 0Idir =I	 CAcLiR_ (p |I	 CAcLiR_ RIGINATE fNVE_REQspo;fed_msg =I0PENVE_REQptyS		  n	-0PENVE_REQcr_enquiry =Ifalsef3PVE_REQ
		gup_flow_ctrl_timci 	-0PEENVE_REQncc__			/_list 	-0PENacilijy_req"j);
MAX_CHANNEL->StR_		In;djpte"(plci- =c_dis_ncc__tD;
	[j]m	I0PENclear_c\ind_masn 0);
f("fset_group\ind_masn 0);
f("f(plci-;ax_ordA "w	iatu_li = 1m= 0PENVE_REQnsf_ord w;
_bits = 0PENVE_REQncpi_s		  n	-0x00PEmPENDINncpi_buffer[Num	I0PEENVE_REQrequ		red_op[0])s_ordA 	-0PENPENDINrequ		red_op[0])s = 0PENVE_REQnotifiedca 0 = 0PENVE_REQv	;
		}s		  n	-0PENVE_REQvs08) &=-0PENVE_REQvs08) dial "wc	-0PENini*_b1_ordfig 0);
f("f].info + 1);
				get_VE_R(%x)"A nDINi-Is==;	Ebrw", ITm+a1;r}
s/*------------------------------------------------------------------*/	/* put aIV(rame 0; in theAV(rame 0; buffermmmmmmmmmmmmmmmmmmmmmmmmmm*/s/*------------------------------------------------------------------*/	ss		 ic;void add_p"		In	*0);
,nby  l)rol;
by  l*p)
TE;_masnp_li = 1PEENV_li = 1m= 0PENummyP) V_li = 1m= p[0]f3	add_i&ALERT,_)rol;
p, V_li = 1);r}
s/*------------------------------------------------------------------*/	/* put aIstruurol_ in theAV(rame 0; buffermmmmmmmmmmmmmmmmmmmmmmmmmm*/s/*------------------------------------------------------------------*/	s		 ic;void add_s"		In	*0);
,nby  l)rol;
API_PARSEl*p)
TE;ummyP) add_i&ALERT,_)rol;
pi- =tu, =_maskp->li = 1e;r}
s/*------------------------------------------------------------------*/	/* put multipleIstruurol_s in theAV(rame 0; buffermmmmmmmmmmmmmmmmmm*/s/*------------------------------------------------------------------*/	s		 ic;void add_ss"		In	*0);
,nby  l)rol;
API_PARSEl*p)
TE;by  li, "fTectpe _3PT].info + 1);
				add_ss"%x,li =%d)"A )rol;
pi-li = 1e)tateacility_r2q"));
(by  kpi-li = 1q"))+=
pi- =tu[irr+ 2) {ate ].info + 1);
				add_ss_i&A%x,li =%d)"A pi- =tu[i - 1]A pi- =tu[i]))ORMA	add_i&ALERT,_pi- =tu[i - 1]A (by  l*)&(pi- =tu[i]), =_maskp-> =tu[i])PENDelO	r}
s/*------------------------------------------------------------------*/	/* brw", ItheA)
				&	nrplci	sIdt by theACTmaifo[0])	in a esc_chimmm*/s/*------------------------------------------------------------------*/	s		 ic;by  lgetC
				&(API_PARSEl*p)
TE;by  li, "fTectpe _3PTacility_r2q"));
(by  kpi-li = 1q"))+=
pi- =tu[irr+ 2) {ate Tectpi- =tu[irr	= 2)={TY_NlTectpi- =tu[i - 1] =	-ESCrB3__i- =tu[i + 1] =	-CHI) brw", I(_i- =tu[i + 2]				seelOS	r>	rNbrw", I0;r}
ss/*------------------------------------------------------------------*/	/* put an iaturmat0]) el3dIdt in theAV(rame 0; buffermmmmmmmmmmmmmmm*/s/*------------------------------------------------------------------*/	ss		 ic;void add_i&A		In	*0);
,nby  l)rol;
by  l*p, _masnp_li = 1)
TE;_masnNdalOummy!(crol &-0	80er	if (_li = 1) brw", ORlOummyPENDINreq_ =		= PENDINreq_ =_s		r*)={TY_PENDINreq_ =	+= 2t"f}
e[i] ={TY_PENDINreq_ =--;IN}ENVE_REQRBuffer[PENDINreq_ =++um	Icrolr_"fTectpe _3PTVE_REQRBuffer[PENDINreq_ =++um	I(by  kp_li = 1PEYracility_req"));
P_li = 1;dapte PENDINRBuffer[PENDINreq_ =++um	Ip[1m+ai];RM}f
EPENDINRBuffer[PENDINreq_ =++um	I0;r}
s/*------------------------------------------------------------------*/	/* put aIunstruurol_d ]ata ;
	o theAbuffermmmmmmmmmmmmmmmmmmmmmmmmmm*/s/*------------------------------------------------------------------*/	ss		 ic;void add_dA		In	*0);
,n_masnli = 1, by  l*p)
TE;_masniORlOummyPENDINreq_ =		= PENDINreq_ =_s		r*)={TY_PENDINreq_ =	+= 2t"f}
e[i] ={TY_PENDINreq_ =--;IN}ENacility_req"));
li = 1;dapte PENDINRBuffer[PENDINreq_ =++um	Ip[i];R}
s/*------------------------------------------------------------------*/	/* put V(rame 0;s from theAAddit0])alnIwroIV(rame 0; in theAmmmmmmmm*/s/*AV(rame 0; buffermmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm*/s/*------------------------------------------------------------------*/	ss		 ic;void add_aiA		In	*0);
,nAPI_PARSEl*ai)
TE;_masniOR	API_PARSElai_(TEplc5]f3atacility_req"));
5;dapte ai_(TEplci].li = 1m= 0PElOummy!ai->li = 1er->brw", ORM
	ifCTi_(TEse(&aii- =tu[1]A (_maskai->li = 1, "ssss"A ai_(TEpl)er->brw", OR3	add_sALERT,_KEY, &ai_(TEplc1]				add_sALERT,_UUI, &ai_(TEplc2]				add_ssALERT,_FTY, &ai_(TEplc3]			}
s/*------------------------------------------------------------------*/	/* put V(rame 0;aacilb1 08) oco&Bea theAV(rame 0; buffermmmmmmmmmmmm*/s/*------------------------------------------------------------------*/	ss		 ic;_masnadd_b1A		In	*0);
,nAPI_PARSEl*bp, _masnb_a
				&E =tu,"ffrrr_masnb1_facilit0es)
TE;API_PARSElbp_(TEplc8]OR	API_PARSElmdm_cfg[9]OR	API_PARSElglobal\ordfigc2];E;by  l)ai[256];E;by  lresourc [um	i{5, 9, 13, 12, 16, 39, 9, 17, 17, 18};E;by  lvoice_)ai[um	i"			6\x14			0			0			0			0			8";E;_masniORlOAPI_PARSElmdm_cfg_v18[4];E;_masnj, n, _("f]_masnd fafNacility_req"));
8;dapte bp_(TEplci].li = 1m= 0PENacility_req"));
2;dapte global\ordfigci].li = 1m= 0PElO].info + 1);
				add_b1ntf("fCTi_save_msg(bp, ds", &nDINi-B_08) oco&)PElOummyb_a
				&E =tur	= 2)={TY_VE_REQB1	resourc  	-0PEN	Crjust_b1_facilit0esALERT,_pE_REQB1	resourc ,nb1_facilit0es)PEN	Crd_p"LERT,_CAI,;"			1			01i("ff].info + 1);
				Cai=1,0 (nolresourc )"==;	ENbrw", I0, 	}"	}ummy_b3_citel 	= SCDEC>StRMANENT) brw", I0, 	[i] =TRIEPENDINtel 	= SCDEC)={TY_VE_REQB1	resourc  	-1PEN	Crjust_b1_facilit0esALERT,_pE_REQB1	resourc ,nb1_facilit0es)PEN	Crd_p"LERT,_CAI,;"			1			11i("ff].info + 1);
				Cai=1,1 (Crolc)"==;	ENbrw", I0, 	}"	[i] =TRIEPENDINtel 	= ADV_VOICE)={TY_VE_REQB1	resourc  	-add_b1_facilit0esALERT,_9a,=_maskdb1_facilit0es | B1	ault:
			VOICE))PEN	Crjust_b1_facilit0esALERT,_pE_REQB1	resourc ,n=_maskdb1_facilit0es | B1	ault:
			VOICE))PEN	voice_)ai[1] i nDINi-B1	resourc PEN	PU		}
			&voice_)ai[5]A nDINi-PEND 0MaxDataLi = 1)PEN	Crd_p"LERT,_CAI,;voice_)aii("ff].info + 1);
				Cai=1,0nne (AdvVoice)"A voice_)ai[1]==;	ENbrw", I0, 	}"fVE_REQca 0Idir &= ~(	 CAcLiR_ RIGINATE |I	 CAcLiR_ANSWatePENummyVE_REQca 0Idir &I	 CAcLiR_ (p)TY_VE_REQca 0Idir |=I	 CAcLiR_ RIGINATE fN[i] =TRIEPENDIN(a 0Idir &I	 CAcLiR_IN)TY_VE_REQca 0Idir |=I	 CAcLiR_ANSWatPElOummy!bpi-li = 1e={TY_VE_REQB1	resourc  	-0x5PEN	Crjust_b1_facilit0esALERT,_pE_REQB1	resourc ,nb1_facilit0es)PEN	Crd_p"LERT,_CAI,;"			1			5"=;	ENbrw", I0, 	}"	}].info + 1);
				b_08) _li =%dlen=_maskbpi-li = 1e)tatummybp->li = 1m> 256) brw", I_WRONG_MESSAGE_FORMATORM
	ifCTi_(TEse(&bpi- =tu[1]A (_maskbp->li = 1ord wwsssblenbp_(TEpl))sw{
e bp_(TEplc6].li = 1m= 0PENM
	ifCTi_(TEse(&bpi- =tu[1]A (_maskbp->li = 1ord wwssslenbp_(TEpl))swP{ate ].info + 1);
				b-turm.!ntf("fffbrw", I_WRONG_MESSAGE_FORMATORMS	r>	rN[i] =TRIECTi_(TEse(&bpi- =tu[1]A (_maskbp->li = 1ord wwsssslenbp_(TEpl))sw_3PT].info + 1);
				b-turm.!ntf("ffbrw", I_WRONG_MESSAGE_FORMATORM}"	}ummybp_(TEplc6].li = 1)r-_3PTT	ifCTi_(TEse(&bp_(TEplc6]. =tu[1]A (_maskbp_(TEplc6].li = 1ord "reglobal\ordfig))swP{ate brw", I_WRONG_MESSAGE_FORMATORMS	r> 	;
		} /GE		}
			global\ordfigc0]. =tu))swP{ateappl,1:"ffoPENDINca 0Idir =IEPENDIN(a 0Idir &I~	 CAcLiR_ANSWate |I	 CAcLiR_ RIGINATE fNLC_REQ_PEND)ppl,2:"ffoPENDINca 0Idir =IEPENDIN(a 0Idir &I~	 CAcLiR_ RIGINATE) |I	 CAcLiR_ANSWat fNLC_REQ_PEND	r>	rN].info + 1);
				(a 0Idir=%04e",_PENDIN(a 0Idir==;	 ENTRIE(GE		}
			bp_(TEplc0]. =tu)&== B1_RTP)
mrrrrB3_XnDINi-Crplci G)man_08)file.1);v	  _op[0])s & (1L << PRIVand RTP)))sw{
e PENDINB1	resourc  	-add_b1_facilit0esALERT,_31,n=_maskdb1_facilit0es &I~B1	ault:
			VOICE))PEN	Crjust_b1_facilit0esALERT,_pE_REQB1	resourc ,n=_maskdb1_facilit0es &I~B1	ault:
			VOICE))PEN	)ai[1] i nDINi-B1	resourc PEN	)ai[2]m	I0PEND)ai[3]m	I0PEND)ai[4]m	I0PENDPU		}
			&)ai[5]A nDINi-PEND 0MaxDataLi = 1)PEN	acility_req"));
bp_(TEplc3].li = 1;dapter->m)ai[7m+ai] i bp_(TEplc3]. =tu[1m+ai];RMm)ai[Num	I6m+abp_(TEplc3].li = 1;EN	Crd_p"LERT,_CAI,;)aii("ffbrw", I0, 	}"	ENTRIE(GE		}
			bp_(TEplc0]. =tu)&== B1_PIAFS)
mrrrrB3_XnDINi-Crplci G)man_08)file.1);v	  _op[0])s & (1L << PRIVand PIAFS)))sw{
e PENDINB1	resourc  	-add_b1_facilit0esALERT,_35/* PIAFS HARDWARE ault:
		m*/,n=_maskdb1_facilit0es &I~B1	ault:
			VOICE))PEN	Crjust_b1_facilit0esALERT,_pE_REQB1	resourc ,n=_maskdb1_facilit0es &I~B1	ault:
			VOICE))PEN	)ai[1] i nDINi-B1	resourc PEN	)ai[2]m	I0PEND)ai[3]m	I0PEND)ai[4]m	I0PENDPU		}
			&)ai[5]A nDINi-PEND 0MaxDataLi = 1);RMm)ai[Num	I6;EN	Crd_p"LERT,_CAI,;)aii("ffbrw", I0, 	}"	ENTRIE(GE		}
			bp_(TEplc0]. =tu)&>= 32)
mrrrr|a (!f(1L << GE		}
			bp_(TEplc0]. =tu)f & nDINi-Crplci G)08)file.B1_P8) oco&&)ENDB3_X(GE		}
			bp_(TEplc0]. =tu)&!= 3)r-,rrrr|a !f(1L << B1_HDLC)rB nDINi-Crplci G)08)file.B1_P8) oco&&)ENDrrrr|a ((bp_(TEplc3].li = 1m!= 0)rB3_XGE		}
			&bp_(TEplc3]. =tu[1])m!= 0)rB3_XGE		}
			&bp_(TEplc3]. =tu[1])m!= 56000	))))sw{
e brw", I_B1_NOT_SUPPORCIL("f}"fVE_REQB1	resourc  	-add_b1_facilit0esALERT,_resourc [GE		}
			bp_(TEplc0]. =tu)]A_CAPPPmmmmmm=_maskdb1_facilit0es &I~B1	ault:
			VOICE))PENCrjust_b1_facilit0esALERT,_pE_REQB1	resourc ,n=_maskdb1_facilit0es &I~B1	ault:
			VOICE))PEN)ai[Num	I6;EN)ai[1] i nDINi-B1	resourc PENacility_r2q"));
sizeof()aii(dapte )ai[ium	I0PEENTRIE(GE		}
			bp_(TEplc0]. =tu)&== B1_MODEM_ CAcNEGOTIand)
mrrrr|a (GE		}
			bp_(TEplc0]. =tu)&== B1_MODEM_ SYNC)	,rrrr|a (GE		}
			bp_(TEplc0]. =tu)&== B1_MODEM_SYNC_HDLC))sw{ /* B1 - mrolm */	lsacility_req"));
7(dapte mdm_cfg[i].li = 1m= 0PElO}ummybp_(TEplc3].li = 1)r--_3PTYTRIEaTi_(TEse(&bp_(TEplc3]. =tu[1]A (_maskbp_(TEplc3].li = 1ord      ",_mdm_cfg	)r->m{TY_Nlbrw", I(_WRONG_MESSAGE_FORMAT				seelr->m)ai[2]m	I0P /* Bit r	  aacilCrplco[0])n*/	ate ].info + 1);
				MDM-Max Bit R	  :<%d>lenGE		}
			mdm_cfg[0]. =tu)f)f3ate PU		}
			&)ai[13],_0e,mmmmmmmmmmmmmmmmmmmmmmmmmm/* Min Tx specd EGIN		PU		}
			&)ai[15]A GE		}
			mdm_cfg[0]. =tu)ft /* Max Tx specd EGIN		PU		}
			&)ai[17],_0e,mmmmmmmmmmmmmmmmmmmmmmmmmm/* Min Rx specd EGIN		PU		}
			&)ai[19]A GE		}
			mdm_cfg[0]. =tu)ft /* Max Rx specd EGIr->m)ai[3]m	I0P /* Async framing V(rame 0;s EGIN			;
		} /GE		}
			mdm_cfg[2]. =tu))swPw{ mmmmmm/* Paritymmmmm*/s	teappl,1:m/* odd V(ritym*/	ls	bcai[3]m|=I(DSPnSAI_ASYNC_PAR
			ENABLd |IDSPnSAI_ASYNC_PAR
			ODD)f3PTYr].info + 1);
				MDM: odd V(rityntf("fffEeturn fals	)ppl,2:m/* even V(ritym*/	ls	bcai[3]m|=I(DSPnSAI_ASYNC_PAR
			ENABLd |IDSPnSAI_ASYNC_PAR
			EVEN)f3PTYr].info + 1);
				MDM: even V(rityntf("fffEeturn fals	default:"ffor].info + 1);
				MDM: no V(rityntf("fffEeturn f	seelr->m	;
		} /GE		}
			mdm_cfg[3]. =tu))swPw{ mmmmmm/* stop bits mm*/s	teappl,1:m/* 2 stop bits */	ls	bcai[3]m|=IDSPnSAI_ASYNC_TWO_STOP_BITSf3PTYr].info + 1);
				MDM: 2 stop bitsntf("fffEeturn fals	default:"ffor].info + 1);
				MDM: 1 stop bitntf("fffEeturn f	seelr->m	;
		} /GE		}
			mdm_cfg[1]. =tu))swPw{ mmmm/* )
	r
li = 1m*/s	teappl,5:	ls	bcai[3]m|=IDSPnSAI_ASYNC_CHAR_LENGTH_5f3PTYr].info + 1);
				MDM: 5 bitsntf("fffEeturn fals	appl,6:	ls	bcai[3]m|=IDSPnSAI_ASYNC_CHAR_LENGTH_6f3PTYr].info + 1);
				MDM: 6 bitsntf("fffEeturn fals	appl,7:	ls	bcai[3]m|=IDSPnSAI_ASYNC_CHAR_LENGTH_7f3PTYr].info + 1);
				MDM: 7 bitsntf("fffEeturn fals	default:"ffor].info + 1);
				MDM: 8 bitsntf("fffEeturn f	seelr->m)ai[7]m	I0P /* Line taking op[0])s */s	teapi[8um	I0t /* Modulo[0])nnegotio[0])nop[0])s */s	teapi[9um	I0t /* Modulo[0])nop[0])s */s3PTYTRIE(yVE_REQca 0Idir &I	 CAcLiR_ RIGINATE) != 0)r^ (yVE_REQca 0Idir &I	 CAcLiR_ UT) != 0))r->m{TY_Nlapi[9um|=IDSPnSAI_MODEM_REVERSEcLiRECnai[f3PTYr].info + 1);
				MDM: ReveEse dir "w0])ntf("fff}s3PTYTRIEGE		}
			mdm_cfg[4]. =tu)&& MDM	CAPI_ciSABLd RETRAIN)r->m{TY_Nlapi[9um|=IDSPnSAI_MODEM_ciSABLd RETRAINf3PTYr].info + 1);
				MDM: DisD;
	 brwrai)ntf("fff}s3PTYTRIEGE		}
			mdm_cfg[4]. =tu)&& MDM	CAPI_ciSABLd RINGnTONE)r->m{TY_Nlapi[7um|=IDSPnSAI_MODEM_ciSABLd 	 CAINGnTONE |IDSPnSAI_MODEM_ciSABLd ANSWatnTONEf3PTYr].info + 1);
				MDM: DisD;
	 bing tonentf("fff}s3PTYTRIEGE		}
			mdm_cfg[4]. =tu)&& MDM	CAPI_GUARD_1800)r->m{TY_Nlapi[8um|=IDSPnSAI_MODEM_GUARD_TONE_1800HZf3PTYr].info + 1);
				MDM: 1800 guard tonentf("fff}s	foci);=TRIEGE		}
			mdm_cfg[4]. =tu)&& MDM	CAPI_GUARD_550)r->m{TY_Nlapi[8um|=IDSPnSAI_MODEM_GUARD_TONE_550HZf3PTYr].info + 1);
				MDM: 550 guard tonentf("fff}s3PTYTRIE(GE		}
			mdm_cfg[5]. =tu)&& 0x00ff=	=	-MDM	CAPI_NEG_V100)r->m{TY_Nlapi[8um|=IDSPnSAI_MODEM_NEGOTIand V100f3PTYr].info + 1);
				MDM: V100ntf("fff}s	foci);=TRIE(GE		}
			mdm_cfg[5]. =tu)&& 0x00ff=	=	-MDM	CAPI_NEG_MOD_CLASS)r->m{TY_Nlapi[8um|=IDSPnSAI_MODEM_NEGOTIand IN_CLASSf3PTYr].info + 1);
				MDM: IN CLASSntf("fff}s	foci);=TRIE(GE		}
			mdm_cfg[5]. =tu)&& 0x00ff=	=	-MDM	CAPI_NEG_ciSABLdD)r->m{TY_Nlapi[8um|=IDSPnSAI_MODEM_NEGOTIand ciSABLdDf3PTYr].info + 1);
				MDM: DiSABLdDntf("fff}s	fo)ai[Num	I20f3ate 
	if(PENDINCrplci G)man_08)file.1);v	  _op[0])s & (1L << PRIVand V18)er->mrrrrB3_XGE		}
			mdm_cfg[5]. =tu)&& 0x8000	)m/* P);v	   V.18 enD;
	 */s	te{TY_NlPENDINrequ		red_op[0])s |=I1L << PRIVand V18("fff}s	foTRIEGE		}
			mdm_cfg[5]. =tu)&& 0x4000)m/* P);v	   VOWN enD;
	 */s	telPENDINrequ		red_op[0])s |=I1L << PRIVand VOWNf3ate 
	if(PENDINrequ		red_op[0])s_ordA | PENDINrequ		red_op[0])s | PENDINCrplci G)requ		red_op[0])s_tD;
	[E_REQ_PEND 0tu - 1])
PPfrrrrBif(1L << PRIVand V18)r| (1L << PRIVand VOWN)	)r->m{TY_Nlummy!aTi_(TEse(&bp_(TEplc3]. =tu[1]A (_maskbp_(TEplc3].li = 1ord      s",_mdm_cfg	)r->mY_3PTY_ET 	-27f3PTYrlummymdm_cfg[6].li = 1m>= 4er->mY__3PTY_E	p 	-GE		D}
			&mdm_cfg[6]. =tu[1])f3PTYrllapi[7um|=I(by  knd;mmmmmmmmmm/* line taking op[0])s */s	te_Nlapi[9um|=I(by  kddm>> 8)P    /* modulo[0])nop[0])s */s	te_Nlapi[++irm	I(by  kddm>> 16eO  /* vown modulo[0])nop[0])s */s	te_Nlapi[++irm	I(by  kddm>> 24)f3PTYrllummymdm_cfg[6].li = 1m>= 8er->mY___3PTY_E		p 	-GE		D}
			&mdm_cfg[6]. =tu[5])f3PTYrllN)ai[10um|=I(by  knd;mmmmmmmm/* disD;
	d modulo[0])s masn */s	te_NlN)ai[11um|=I(by  kddm>> 8)Ps	te_NlNummymdm_cfg[6].li = 1m>= 12er->mY____3PTY_E			p 	-GE		D}
			&mdm_cfg[6]. =tu[9])f3PTYrllNN)ai[12rm	I(by  knd;mmmmmmmmmm/* enD;
	d modulo[0])s masn */s	te_NlNlapi[++irm	I(by  kddm>> 8)P   /* vown enD;
	d modulo[0])s */s	te_NlNlapi[++irm	I(by  kddm>> 16eORMA	te_Nlapi[++irm	I(by  kddm>> 24)f3PTYrllNlapi[++irm	I0f3PTYrllNlummymdm_cfg[6].li = 1m>= 14er->mY_Eo__3PTY_E				w 	-GE		}
			&mdm_cfg[6]. =tu[13])f3PTYrllNNlummywm!= 0)"fforrrrrrrPU		}
			&)ai[13],_weO  /* mea tx specd EGIN		YrllNlummymdm_cfg[6].li = 1m>= 16)"fforrrrrr_3PTY_E					w 	-GE		}
			&mdm_cfg[6]. =tu[15])f3PTYrllNNllummywm!= 0)"fforrrrrrr	PU		}
			&)ai[15]A weO  /* max tx specd EGIN		YrllNllummymdm_cfg[6].li = 1m>= 18)"fforrrrrrr_3PTY_E						w 	-GE		}
			&mdm_cfg[6]. =tu[17])f3PTYrllNNlllummywm!= 0)"fforrrrrrr		PU		}
			&)ai[17],_weO  /* mea rx specd EGIN		YrllNlllummymdm_cfg[6].li = 1m>= 20)"fforrrrrrr	_3PTY_E							w 	-GE		}
			&mdm_cfg[6]. =tu[19])f3PTYrllNNllllummywm!= 0)"fforrrrrrr			PU		}
			&)ai[19]A weO  /* max rx specd EGIN		YrllNllllummymdm_cfg[6].li = 1m>= 22)"fforrrrrrr		_3PTY_E								w 	-GE		}
			&mdm_cfg[6]. =tu[21])f3PTYrllNNllllm)ai[23rm	I(by  kd-((shor*)=w)eO  /* transmit level EGIN		YrllNlllllummymdm_cfg[6].li = 1m>= 24)"fforrrrrrr			_3PTY_E									w 	-GE		}
			&mdm_cfg[6]. =tu[23])f3PTYrllNNllllmm)ai[22um|=I(by  knw;mmmmmmmm/*  =turop[0])s masn */s	te_NlNlllllmm)ai[21um|=I(by  kdwm>> 8)P  /* disD;
	d symbol r	  s */s	te_NlNlllllm}
te_NlNlllllm}
te_NlNlllll}
te_NlNllll}
te_NlNlll}
te_NlNll}
te_NlNl}
te_NlN}
te_Nl}
te_Nl)ai[27]m	Ii - 27f3PTYrlu++;
SAONlummy!aTi_(TEse(&bp_(TEplc3]. =tu[1]A (_maskbp_(TEplc3].li = 1ord      ss",_mdm_cfg	)r->mYv_3PTY_E ummy!aTi_(TEse(&mdm_cfg[7]. =tu[1]A (_maskmdm_cfg[7].li = 1, "sss",_mdm_cfg_v18)er->mY___3PTY_E		aciliA 	-0P n < 3P npter->mY_E	_3PTY_E			)ai[ium	I(by  kdmdm_cfg_v18[n].li = 1)f3PTYrllNNacilijy_r1q"j);
!=_mask()ai[ium+a1=);djpte
te_NlNlll)ai[im+aj]m	Imdm_cfg_v18[n]. =tu[j]f3PTYrllNlu	+= )ai[ium+a1;
te_NlNl}
te_NlN}
te_Nl}
te_Nl)ai[0um	I(by  kdi - 1)f3PTYr}"fff}s3PT	r>	rNTRIEGE		}
			bp_(TEplc0]. =tu)&== 2r|a mmmmmmmmmmmmmmmmmmmmmmmm/* V.110 async */s	mmmmGE		}
			bp_(TEplc0]. =tu)&== 3)mmmmmmmmmmmmmmmmmmmmmmmmmmm/* V.110 sync */s	_3PTT	ifbp_(TEplc3].li = 1) {ate ].info + 1);
				V.110,%dlenGE		}
			&bp_(TEplc3]. =tu[1])tf("fff	;
		} /GE		}
			&bp_(TEplc3]. =tu[1])t {mmmmmmmmmmmmmmmmm/* R	  m*/s	teappl,0:s	teappl,56000:TY_NlummyGE		}
			bp_(TEplc0]. =tu)&== 3)m{mmmmmmmmmmmmmmmmmm/* V.110 sync 56n */s	te_N].info + 1);
				56n sync HSCXntf("fffEN)ai[1] i 8("fffEN)ai[2rm	I0f3PTYrl)ai[3]m	I0PENDNl}
te_Nci);=TRIEGE		}
			bp_(TEplc0]. =tu)&== 2er_3PTY_E].info + 1);
				56n async DSPntf("fffEN)ai[2rm	I9PENDNl}
te_Neturn f	seappl,50:mmmmm)ai[2rm	I1q" eturn f	seappl,75:mmmmm)ai[2rm	I1q" eturn f	seappl,110:mmmm)ai[2rm	I1q" eturn f	seappl,150:mmmm)ai[2rm	I1q" eturn f	seappl,200:mmmm)ai[2rm	I1q" eturn f	seappl,300:mmmm)ai[2rm	I1q" eturn f	seappl,600:mmmm)ai[2rm	I1q" eturn f	seappl,1200:mmm)ai[2rm	I2q" eturn f	seappl,2400:mmm)ai[2rm	I3q" eturn f	seappl,4800:mmm)ai[2rm	I4q" eturn f	seappl,7200:mmm)ai[2rm	I10P eturn f	seappl,9600:mmm)ai[2rm	I5q" eturn f	seappl,12000:mm)ai[2rm	I13P eturn f	seappl,24000:mm)ai[2rm	I0q" eturn f	seappl,14400:mm)ai[2rm	I11P eturn f	seappl,19200:mm)ai[2rm	I6q" eturn f	seappl,28800:mm)ai[2rm	I12P eturn f	seappl,38400:mm)ai[2rm	I7q" eturn f	seappl,48000:mm)ai[2rm	I8q" eturn f	seappl,76:mmmmm)ai[2rm	I15P eturn mm/* 75/1200mmmmm*/s	teappl,1201:mmm)ai[2rm	I14P eturn mm/* 1200/75mmmmm*/s	teappl,56001:mm)ai[2rm	I9q" eturn mm/* V.110 56000n*/	ate ]efault:"fforbrw", I_B1_PARM_NOT_SUPPORCIL("fff}s	fo)ai[3]m	I0PENDNTRIE	ai[1] i	I13)mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm/* v.110 async */s	>m{TY_Nlummybp_(TEplc3].li = 1m>= 8er->mY_3PTY_E	;
		} /GE		}
			&bp_(TEplc3]. =tu[3]))r->mYv_mmmmmmm/* )
	r
li = 1m*/s	teteappl,5:	ls	b	bcai[3]m|=IDSPnSAI_ASYNC_CHAR_LENGTH_5f3PTYr_Eeturn fTYEl	appl,6:	ls	b	bcai[3]m|=IDSPnSAI_ASYNC_CHAR_LENGTH_6f3PTYr_Eeturn fTYEl	appl,7:	ls	b	bcai[3]m|=IDSPnSAI_ASYNC_CHAR_LENGTH_7f3PTYr_Eeturn fTYEl	}3PTY_E	;
		} /GE		}
			&bp_(TEplc3]. =tu[5]))r->mYv_mmmmmmm/* Paritymmmmm*/s	teteappl,1:m/* odd V(ritym*/	ls	b	bcai[3]m|=I(DSPnSAI_ASYNC_PAR
			ENABLd |IDSPnSAI_ASYNC_PAR
			ODD)f3PTYr_Eeturn fTYEl	appl,2:m/* even V(ritym*/	ls	b	bcai[3]m|=I(DSPnSAI_ASYNC_PAR
			ENABLd |IDSPnSAI_ASYNC_PAR
			EVEN)f3PTYr_Eeturn fTYEl	}3PTY_E	;
		} /GE		}
			&bp_(TEplc3]. =tu[7]))r->mYv_mmmmmmm/* stop bits mm*/s	teteappl,1:m/* 2 stop bits */	ls	b	bcai[3]m|=IDSPnSAI_ASYNC_TWO_STOP_BITSf3PTYr_Eeturn fTYEl	}3PTY_}3PTY}ate}
>S[i] =TRIE	ai[1] i	I8r|a GE		}
			bp_(TEplc0]. =tu)&== 3)m{ate ].info + 1);
				V.110 ]efault 56n syncntf("fff)ai[1] i 8("fff)ai[2rm	I0f3PTY)ai[3]m	I0PEND}
>S[i] ={ate ].info + 1);
				V.110 ]efault 9600 asyncntf("fff)ai[2rm	I5q3PT	r>	rNPU		}
			&)ai[5]A nDINi-PEND 0MaxDataLi = 1);RM].info + 1);
				CAI[%d]=%x,%x,%x,%x,%x,%x",;)aic0],;)aic1],;)aic2],;)aic3],;)aic4],;)aic5],;)aic6]==;	/* HexDump 		CAI",;sizeof()aii, &)aic0]);d*/	atCrd_p"LERT,_CAI,;)aii("fbrw", I0;r}
s/*------------------------------------------------------------------*/	/* put V(rame 0;aacilb2 ci-	B3  08) oco&Bea theAV(rame 0; buffermmmm*/s/*------------------------------------------------------------------*/	ss		 ic;_masnadd_b23A		In	*0);
,nAPI_PARSEl*bp)
TE;_masnN,n;ax_ord w;
_bits;E;by  lpos,I	el;3Pby  lSAPI 	-0x40P  /* default SAPI 16aacilx.31 */	lAPI_PARSElbp_(TEplc8]OR	API_PARSEl*b1_ordfigOR	API_PARSEl*b2_ordfigOR	API_PARSElb2_ordfig_(TEplc8]OR	API_PARSEl*b3_ordfigOR	API_PARSElb3_ordfig_(TEplc6]OR	API_PARSElglobal\ordfigc2];E
	s		 ic;by  lllc[3]m	I{2,0,0};E;s		 ic;by  ldlc[20um	I{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};E;s		 ic;by  lnlc[256];E;s		 ic;by  llli[12rm	I{1,1} ffsaonst;by  lllc2_out[rm	I{1,2,4,6,2,0,0,0, X75_V42BIS,V120_L2,V120_V42BIS,V120_L2,6};E;aonst;by  lllc2_in[rmm	I{1,3,4,6,3,0,0,0, X75_V42BIS,V120_L2,V120_V42BIS,V120_L2,6};EE;aonst;by  lllc3[rm	I{4,3,2,2,6,6,0};E;aonst;by  lheader[um	I{0,2,3,3,0,0,0};EfNacility_req"));
8;dapte bp_(TEplci].li = 1m= 0PENacility_req"));
6;dapte b2_ordfig_(TEplci].li = 1m= 0PENacility_req"));
5;dapte b3_ordfig_(TEplci].li = 1m= 0PElOlli[0] 	-1PENlli[1] 	-1PENTect_b3_ciCrplci G)manudburol_r_fearol_s & MANUdf(TURERdFEATURE_XONOFF_FLOW_				if )r->lli[1] |= 2t"fTect_b3_ciCrplci G)manudburol_r_fearol_s & MANUdf(TURERdFEATURE_OOB_CHANNELer->lli[1] |= 4PEENTRIE(lli[1] & 0x02)rB3_Xdiva_xdi_extended_fearol_s & DIVA_CAPI_USE_CMA))m{atelli[1] |= 0x10PENM
	ifPENDINrx_dma_descriptcil<= 0)r{ate PENDINrx_dma_descriptcil= diva_get_dma_descriptci"LERT,_&PENDINrx_dma_magic) fND;
	ifPENDINrx_dma_descriptcil>= 0)"fforPENDINrx_dma_descriptci++;
SA}ENM
	ifPENDINrx_dma_descriptcil> 0)r{ate lli[0] 	-6f3PTYlli[1] |= 0x40f3PTYlli[2rm	I(by  kfPENDINrx_dma_descriptcil- 1)f3PTYlli[3rm	I(by  kPENDINrx_dma_magicf3PTYlli[4rm	I(by  kfPENDINrx_dma_magicm>>  8)Ps	telli[5rm	I(by  kfPENDINrx_dma_magicm>> 16eORMA	lli[6rm	I(by  kfPENDINrx_dma_magicm>> 24)f3PT}RM}"	}ummyDIVA_CAPI_SUPPORCS_NO_CANCELt_b3_ciCrplci ))m{atelli[1] |= 0x20, 	}"	}].info + 1);
				add_b23ntf("fCTi_save_msg(bp, ds", &nDINi-B_08) oco&)PElOummy!bpi-li = 1rB3__ENDINtel)sw{
e PENDINadv_nl 	-tru PEN	].info + 1);
				Default adv.N
ntf("ffCrd_p"LERT,_LLI,;llif("ffnDINi-B2_08) &=-1 /*XPARENT*/("ffnDINi-B3_08) &=-0 /*XPARENT*/("ffllc[1rm	I2q"ffllc[2rm	I4q"ffCrd_p"LERT,_LLC,;llc) fNDdlc[0rm	I2q"ffPU		}
			&dlc[1rA nDINi-PEND 0MaxDataLi = 1)PEN	Crd_p"LERT,_DLC,;dlc) fNDbrw", I0, 	}"	}ummy!bpi-li = 1e=/*default*/s	_3PT].info + 1);
				brw defaultntf("ffCrd_p"LERT,_LLI,;llif("ffnDINi-B2_08) &=-0 /*X.75mmm*/("ffnDINi-B3_08) &=-0 /*XPARENT*/("ffllc[1rm	I1q"ffllc[2rm	I4q"ffCrd_p"LERT,_LLC,;llc) fNDdlc[0rm	I2q"ffPU		}
			&dlc[1rA nDINi-PEND 0MaxDataLi = 1)PEN	Crd_p"LERT,_DLC,;dlc) fNDbrw", I0, 	}"}].info + 1);
				b_08) _li =%dlen=_maskbpi-li = 1e)tatummy=_maskbpi-li = 1m> 256)    brw", I_WRONG_MESSAGE_FORMATORRM
	ifCTi_(TEse(&bpi- =tu[1]A (_maskbp->li = 1ord wwsssblenbp_(TEpl))sw{
e bp_(TEplc6].li = 1m= 0PENM
	ifCTi_(TEse(&bpi- =tu[1]A (_maskbp->li = 1ord wwssslenbp_(TEpl))swP{ate ].info + 1);
				b-turm.!ntf("fffbrw", I_WRONG_MESSAGE_FORMATORMS	r>	rN[i] =TRIECTi_(TEse(&bpi- =tu[1]A (_maskbp->li = 1ord wwsssslenbp_(TEpl))sw_3PT].info + 1);
				b-turm.!ntf("ffbrw", I_WRONG_MESSAGE_FORMATORM}"	}ummyPENDINtel 	= ADV_VOICE)=/* transparIdt B on advanced voice */s	_3PTT	ifGE		}
			bp_(TEplc1]. =tu)m!= 1
NDrrrr|a GE		}
			bp_(TEplc2]. =tu)m!= 0) brw", I_B2_NOT_SUPPORCIL("ffPENDINadv_nl 	-tru PEN}"	[i] =TRIEPENDINtel) brw", I_B2_NOT_SUPPORCIL("EENTRIE(GE		}
			bp_(TEplc1]. =tu)&== B2_RTP)
mrrrrB3_XGE		}
			bp_(TEplc2]. =tu)m== B3_RTP)
mrrrrB3_XnDINi-Crplci G)man_08)file.1);v	  _op[0])s & (1L << PRIVand RTP)))sw{
e Crd_p"LERT,_LLI,;llif("ffnDINi-B2_08) &=-(by  knGE		}
			bp_(TEplc1]. =tu)("ffnDINi-B3_08) &=-(by  knGE		}
			bp_(TEplc2]. =tu)("ffllc[1rm	IyVE_REQca 0Idir &I(	 CAcLiR_ RIGINATE |I	 CAcLiR_FORCE_ (plcNL)e ?;14 :I13P"ffllc[2rm	I4q"ffCrd_p"LERT,_LLC,;llc) fNDdlc[0rm	I2q"ffPU		}
			&dlc[1rA nDINi-PEND 0MaxDataLi = 1)PEN	dlc[3]m	I3P /* Addr A */	lsdlc[4rm	I1q"/* Addr B */	lsdlc[5rm	I7q"/* modulo mrol */	lsdlc[6rm	I7q"/* window;size */	lsdlc[7]m	I0P /* XIDI	el Lo  */	lsdlc[8]m	I0P /* XIDI	el Hi  */	lsacility_req"));
bp_(TEplc4].li = 1;dapter->mdlc[9m+ai] i bp_(TEplc4]. =tu[1m+ai];RMmdlc[0rm	I(by  kf8m+abp_(TEplc4].li = 1)f3PTCrd_p"LERT,_DLC,;dlc) fNDacility_req"));
bp_(TEplc5].li = 1;dapter->mnlc[1m+ai] i bp_(TEplc5]. =tu[1m+ai];RMmnlc[0rm	I(by  kfbp_(TEplc5].li = 1)f3PTCrd_p"LERT,_NLC,;nlc) fNDbrw", I0, 	}"	EENTRIE(GE		}
			bp_(TEplc1]. =tu)&>= 32)
mrrrr|a (!f(1L << GE		}
			bp_(TEplc1]. =tu))rB nDINi-Crplci G)08)file.B2_P8) oco&&)ENDB3_X(GE		}
			bp_(TEplc1]. =tu)m!= B2_PIAFS)
m,rrrr|a !fnDINi-Crplci G)man_08)file.1);v	  _op[0])s & (1L << PRIVand PIAFS)))))ssw{
e brw", I_B2_NOT_SUPPORCIL("f}ENTRIE(GE		}
			bp_(TEplc2]. =tu)&>= 32)
mrrrr|a !f(1L << GE		}
			bp_(TEplc2]. =tu))rB nDINi-Crplci G)08)file.B3_P8) oco&&))sw{
e brw", I_B3_NOT_SUPPORCIL("f}ENTRIE(GE		}
			bp_(TEplc1]. =tu)m!= B2_SDLC)
mrrrrB3_X(GE		}
			bp_(TEplc0]. =tu)&== B1_MODEM_ CAcNEGOTIand)
m	|a (GE		}
			bp_(TEplc0]. =tu)&== B1_MODEM_ SYNC)	,	|a (GE		}
			bp_(TEplc0]. =tu)&== B1_MODEM_SYNC_HDLC)))sw{
e brw", I(Crd_mrolm_b23ALERT,_bp_(TEpl))ORM}"	}Crd_p"LERT,_LLI,;llif("ENVE_REQB2_08) &=-(by  kGE		}
			bp_(TEplc1]. =tu)("fnDINi-B3_08) &=-(by  kGE		}
			bp_(TEplc2]. =tu)("fummyPENDINB2_08) &== 12)rSAPI 	-0; /* default SAPI D-)
				&	*/	atummybp_(TEplc6].li = 1)r-_3PTT	ifCTi_(TEse(&bp_(TEplc6]. =tu[1]A (_maskbp_(TEplc6].li = 1ord "reglobal\ordfig))swP{ate brw", I_WRONG_MESSAGE_FORMATORMS	r> 	;
		} /GE		}
			global\ordfigc0]. =tu))swP{ateappl,1:"ffoPENDINca 0Idir =IEPENDIN(a 0Idir &I~	 CAcLiR_ANSWate |I	 CAcLiR_ RIGINATE fNLC_REQ_PEND)ppl,2:"ffoPENDINca 0Idir =IEPENDIN(a 0Idir &I~	 CAcLiR_ RIGINATE) |I	 CAcLiR_ANSWat fNLC_REQ_PEND	r>	rN].info + 1);
				(a 0Idir=%04e",_PENDIN(a 0Idir==;	 ENTRIEPENDINB2_08) &== B2_PIAFS)
m,llc[1rm	IPIAFS_CRC;"	[i] 	/* IMPLEMENT_PIAFS */s	_3PTllc[1rm	IyVE_REQca 0Idir &I(	 CAcLiR_ RIGINATE |I	 CAcLiR_FORCE_ (plcNL)e ?RMA	llc2_out[GE		}
			bp_(TEplc1]. =tu)] :Illc2_in[GE		}
			bp_(TEplc1]. =tu)]("f}ENllc[2rm	Illc3[GE		}
			bp_(TEplc2]. =tu)];"	}Crd_p"LERT,_LLC,;llc) f
Ddlc[0rm	I2q"fPU		}
			&dlc[1rA nDINi-PEND 0MaxDataLi = 1 +
m,rheader[GE		}
			bp_(TEplc2]. =tu)]) f
Db1_ordfigm	I&bp_(TEplc3]q"fnlc[0rm	I0("fummyPENDINB3_08) &== 4
mrrrr|a PENDINB3_08) &== 5)r-_3PTacility_req"));
sizeof(T30_INapi(dapte nlc[irm	I0f3PTnlc[0rm	Isizeof(T30_INapi(ENM
	ifPENDINCrplci G)manudburol_r_fearol_s & MANUdf(TURERdFEATURE_auX_PAPERdFORMATS)	, S((T30_INap *)&nlc[1])->operat		/_mrol = T30_OPERATINGnMODE_CAPI(ENM((T30_INap *)&nlc[1])->r	  _div_2400 = ((ff(ENM
	ifb1_ordfigi-li = 1m>= 2eswP{ate ((T30_INap *)&nlc[1])->r	  _div_2400 = (by  kfGE		}
			&b1_ordfigi- =tu[1])m/,2400)f3PT}RM}"	b2_ordfigm	I&bp_(TEplc4];E ENTRIEllc[1rm		IPIAFS_CRC)r-_3PTT	ifPENDINB3_08) &!= B3_TRANSPARENT)swP{ate brw", I_B_STACK_NOT_SUPPORCIL("ff}ENM
	ifb2_ordfigi-li = 1rB3_CTi_(TEse(&b2_ordfigi- =tu[1]A (_maskb2_ordfigi-li = 1ordb   ",_b2_ordfig_(TEpl))m{ate brw", I_WRONG_MESSAGE_FORMATORMS	r> PU		}
			&dlc[1rA nDINi-PEND 0MaxDataLi = 1)PEN	dlc[3]m	I0P /* Addr A */	lsdlc[4rm	I0q"/* Addr B */	lsdlc[5rm	I0q"/* modulo mrol */	lsdlc[6rm	I0q"/* window;size */	ls
	ifb2_ordfigi-li = 1r>= 7)m{ate ]lc[7]m	I7f3PTYdlc[8]m	I0Pr->mdlc[9] i b2_ordfig_(TEplc0]. =tu[0]f /* PIAFS 08) oco&BSpecd ordfiguro[0])n*/	->mdlc[10] i b2_ordfig_(TEplc1]. =tu[0]f /* V.42bis P0n*/	->mdlc[11] i b2_ordfig_(TEplc1]. =tu[1]f /* V.42bis P0n*/	->mdlc[12] i b2_ordfig_(TEplc2]. =tu[0]f /* V.42bis P1n*/	->mdlc[13] i b2_ordfig_(TEplc2]. =tu[1]f /* V.42bis P1n*/	->mdlc[14] i b2_ordfig_(TEplc3]. =tu[0]f /* V.42bis P2n*/	->mdlc[15] i b2_ordfig_(TEplc3]. =tu[1]f /* V.42bis P2n*/	->mdlc[0rm	I15PfND;
	ifb2_ordfigi-li = 1r>= 8)m{m/* PIAFS ord w;
 abilit0es */	ls	b]lc[7]m	I10f3PTYr]lc[16rm	I2q"/* Li = 1rof 	IAFS extens0])n*/	->mr]lc[17rm	IPIAFS_ULATA_ABt:
	IESq"/* ord w;
 (UDATA) abilityn*/	->mr]lc[18] i b2_ordfig_(TEplc4]. =tu[0]f /* valuen*/	->mr]lc[0rm	I18("fff}s	f}
>S[i] =/* default values, 64K, variD;
	, no compl_ss0])n*/	->{ate ]lc[7]m	I7f3PTYdlc[8]m	I0Pr->mdlc[9] i 0x03f /* PIAFS 08) oco&BSpecd ordfiguro[0])n*/	->mdlc[10] i 0x03f /* V.42bis P0n*/	->mdlc[11] i 0P    /* V.42bis P0n*/	->mdlc[12] i 0P    /* V.42bis P1n*/	->mdlc[13] i 0P    /* V.42bis P1n*/	->mdlc[14] i 0P    /* V.42bis P2n*/	->mdlc[15] i 0P    /* V.42bis P2n*/	->mdlc[0rm	I15PfND}3PTCrd_p"LERT,_DLC,;dlc) fN}"	[i] ElO}ummyEllc[1rm		IV120_L2)r|a (llc[1rm		IV120_V42BIS))r--_3PTYTRIEPENDINB3_08) &!= B3_TRANSPARENT)swPe brw", I_B_STACK_NOT_SUPPORCIL("	->mdlc[0rm	I6f3PTYPU		}
			&dlc[1rA GE		}
			&dlc[1r)r+ 2)Pr->mdlc[3] i 0x08Pr->mdlc[4] i 0x01Pr->mdlc[5rm	I127f3PTYdlc[6]m	I7f3PTY
	ifb2_ordfigi-li = 1r!= 0)"ffo_3PTY}ummyEllc[1rm		IV120_V42BIS)rB3_CTi_(TEse(&b2_ordfigi- =tu[1]A (_maskb2_ordfigi-li = 1ordbbbb   ",_b2_ordfig_(TEpl))m{ate e brw", I_WRONG_MESSAGE_FORMATORMSl	}3PTY_dlc[3] i (by  kf(b2_ordfigi- =tu[2] << 3)ma ((b2_ordfigi- =tu[1] >> 5)&& 0x04tf("fffEdlc[4] i (by  kf(b2_ordfigi- =tu[1] << 1)ma 0x01f("fffE
	ifb2_ordfigi- =tu[3]m!= 128er->mY_3PTY_E].info + 1);
				1D-dlc= %x %x %x %x %x",;dlcc0],;dlc[1rA dlc[2rA dlc[3rA dlc[4]tf("fffENbrw", I_B2_PARM_NOT_SUPPORCIL("fff	}3PTY_dlc[5rm	I(by  kfb2_ordfigi- =tu[3]m- 1)f3PTYrdlc[6]m	Ib2_ordfigi- =tu[4]("fffE
	ifllc[1rm		IV120_V42BIS)r_3PTY_E
	ifb2_ordfigi-li = 1r>= 10)r_3PTY_E ]lc[7]m	I6f3PTYr_Edlc[8]m	I0Pr->m->mdlc[9] i b2_ordfig_(TEplc4]. =tu[0]fr->m->mdlc[10] i b2_ordfig_(TEplc4]. =tu[1]fr->m->mdlc[11] i b2_ordfig_(TEplc5]. =tu[0]fr->m->mdlc[12] i b2_ordfig_(TEplc5]. =tu[1]fr->m->mdlc[13] i b2_ordfig_(TEplc6]. =tu[0]fr->m->mdlc[14] i b2_ordfig_(TEplc6]. =tu[1]fr->m->mdlc[0rm	I14Pr->m->md.info + 1);
				b2_ordfig_(TEplc4]. =tu[0] [1]:  %x %x",;b2_ordfig_(TEplc4]. =tu[0],;b2_ordfig_(TEplc4]. =tu[1]tf("fffENmd.info + 1);
				b2_ordfig_(TEplc5]. =tu[0] [1]:  %x %x",;b2_ordfig_(TEplc5]. =tu[0],;b2_ordfig_(TEplc5]. =tu[1]tf("fffENmd.info + 1);
				b2_ordfig_(TEplc6]. =tu[0] [1]:  %x %x",;b2_ordfig_(TEplc6]. =tu[0],;b2_ordfig_(TEplc6]. =tu[1]tf("fffEN}3PTY_E[i] ={TY_NlYrdlc[6]m	I14Pr->m->}3PTY_}3PTY}ate}
>S[i] r--_3PTYTRIEb2_ordfigi-li = 1)"ffo_3PTY}d.info + 1);
				B2-Crdfigntf("fffE
	ifllc[1rm		IX75_V42BIS)r_3PTY_E
	ifCTi_(TEse(&b2_ordfigi- =tu[1]A (_maskb2_ordfigi-li = 1ordbbbb   ",_b2_ordfig_(TEpl))3PTY_E{TY_NlYrbrw", I_WRONG_MESSAGE_FORMATORMSl	>}3PTY_}3PTYE[i] ={TY_NlY
	ifCTi_(TEse(&b2_ordfigi- =tu[1]A (_maskb2_ordfigi-li = 1ordbbbbs",_b2_ordfig_(TEpl))3PTY_E{TY_NlYrbrw", I_WRONG_MESSAGE_FORMATORMSl	>}3PTY_}3PTYE/*  f B2 P8) oco&Bes LAPD,_b2_ordfigIstruurol_ is differIdt */	->mr
	ifllc[1rm		I6er->mY_3PTY_E]lc[0rm	I4Pr->m->
	ifb2_ordfigi-li = 1r>= 1) dlc[2rm	Ib2_ordfigi- =tu[1]f mmmmm/* TEI */	ls	b	[i] =dlc[2rm	I0x01Pr->mY}ummyEb2_ordfigi-li = 1r>= 2)rB3_XPENDINB2_08) &== 12))3PTY_E{TY_NlYrSAPI 	-b2_ordfigi- =tu[2]P    /* SAPI */	ls	b	}3PTY_E]lc[1] i SAPI(ENMmY}ummyEb2_ordfigi-li = 1r>= 3)rB3_Xb2_ordfigi- =tu[3]m== 128eer->mY__3PTY_E	plc[3] i 127f mmmmm/* Mrol */	lsfEN}3PTY_E[i] r->mY__3PTY_E	plc[3] i 7f mmmmmmm/* Mrol */	lsfEN}3r->m->
	ifb2_ordfigi-li = 1r>= 4) dlc[4] i b2_ordfigi- =tu[4](mmmmmm/* Window;*/	ls	b	[i] =dlc[4rm	I1q"ffTY}d.info + 1);
				D-dlc[%d]=%x,%x,%x,%x",;dlcc0],;dlc[1rA dlc[2rA dlc[3rA dlc[4]tf("fffEN
	ifb2_ordfigi-li = 1r> 5)&brw", I_B2_PARM_NOT_SUPPORCIL("fff	}3PTY_[i] r->mY_3PTY_E]lc[0rm	I(by  kfb2_ordfig_(TEplc4].li = 1r+ 6eORMA	teplc[3] i b2_ordfigi- =tu[1]fRMA	teplc[4] i b2_ordfigi- =tu[2]("fffEN
	ifb2_ordfigi- =tu[3]m!= 8rB3_b2_ordfigi- =tu[3]m!= 128e={TY_NlYrd.info + 1);
				1D-dlc= %x %x %x %x %x",;dlcc0],;dlc[1rA dlc[2rA dlc[3rA dlc[4]tf("fffENNbrw", I_B2_PARM_NOT_SUPPORCIL("fff	N}3r->m->dlc[5rm	I(by  kfb2_ordfigi- =tu[3]m- 1)f3PTYrrdlc[6]m	Ib2_ordfigi- =tu[4]("fffEN
	ifdlc[6]m> dlc[5re={TY_NlYrd.info + 1);
				2D-dlc= %x %x %x %x %x %x %x",;dlcc0],;dlc[1rA dlc[2rA dlc[3rA dlc[4], dlc[5r, dlc[6]tf("fffENNbrw", I_B2_PARM_NOT_SUPPORCIL("fff	N}3r->m->
	ifllc[1rm		IX75_V42BIS)r_3PTY_EE
	ifb2_ordfigi-li = 1r>= 10)r_3PTY_E  ]lc[7]m	I6f3PTYr_EEdlc[8]m	I0Pr->m->mmdlc[9] i b2_ordfig_(TEplc4]. =tu[0]fr->m->mmdlc[10] i b2_ordfig_(TEplc4]. =tu[1]fr->m->mmdlc[11] i b2_ordfig_(TEplc5]. =tu[0]fr->m->mmdlc[12] i b2_ordfig_(TEplc5]. =tu[1]fr->m->mmdlc[13] i b2_ordfig_(TEplc6]. =tu[0]fr->m->mmdlc[14] i b2_ordfig_(TEplc6]. =tu[1]fr->m->mmdlc[0rm	I14Pr->m->mmd.info + 1);
				b2_ordfig_(TEplc4]. =tu[0] [1]:  %x %x",;b2_ordfig_(TEplc4]. =tu[0],;b2_ordfig_(TEplc4]. =tu[1]tf("fffENmmd.info + 1);
				b2_ordfig_(TEplc5]. =tu[0] [1]:  %x %x",;b2_ordfig_(TEplc5]. =tu[0],;b2_ordfig_(TEplc5]. =tu[1]tf("fffENmmd.info + 1);
				b2_ordfig_(TEplc6]. =tu[0] [1]:  %x %x",;b2_ordfig_(TEplc6]. =tu[0],;b2_ordfig_(TEplc6]. =tu[1]tf("fffENl}
te_NlN[i] ={TY_NlYrrdlc[6]m	I14Pr->m->N}3r->m->}3PTY_E[i] ={TY_NlYrPU		}
			&dlc[7]A (_maskb2_ordfig_(TEplc4].li = 1)f3PTTTTTacility_req"));
b2_ordfig_(TEplc4].li = 1;dapter->m->mmdlc[11m+ai] i b2_ordfig_(TEplc4]. =tu[1m+ai];RMm	->}3PTY_}3PTY}ate}
>Crd_p"LERT,_DLC,;dlc) f
	b3_ordfigm	I&bp_(TEplc5]f3E
	ifb3_ordfigi-li = 1)r-_3PTT	ifPENDINB3_08) &== 4
mmrrrr|a PENDINB3_08) &== 5)r--_3PTYTRIEaTi_(TEse(&b3_ordfigi- =tu[1]A (_maskb3_ordfigi-li = 1ord wsslenb3_ordfig_(TEpl	)r->m{TY_Nlbrw", I_WRONG_MESSAGE_FORMATORMSl}s	foT 	-GE		}
			(by  l*)(b3_ordfig_(TEplc0]. =tu)fts	fo((T30_INap *)&nlc[1])->resolu[0])ni (by  kf(ity& 0x0001)ma|r->m->mmmrrrr(fPENDINB3_08) &== 4)rB3_X((by  kfGE		}
			(by  l*)b3_ordfig_(TEplc1]. =tu)))m!= 5))e ?;T30_RESOLUnai[_R8_0770_OR_200m: 0fts	fo((T30_INap *)&nlc[1])->]ata_turmat = (by  kfGE		}
			(by  l*)b3_ordfig_(TEplc1]. =tu))ts	fo;ax_ord w;
_bits = T30_				if _BIT_ CAcFEATURESf3PTY
	if(((T30_INap *)&nlc[1])->r	  _div_2400 != 0)rB3_X((T30_INap *)&nlc[1])->r	  _div_2400 <	I6eer->m-;ax_ord w;
_bits &= ~T30_				if _BIT_ENABLd_V34FAXf3PTY
	ifPENDINCrplci G)manudburol_r_fearol_s & MANUdf(TURERdFEATURE_auX_PAPERdFORMATS)	, S{
	->mr
	if(PENDINrequ		red_op[0])s_ordA | PENDINrequ		red_op[0])s | PENDINCrplci G)requ		red_op[0])s_tD;
	[E_REQ_PEND 0tu - 1])
PPffrrrrBif1L << PRIVand auX_PAPERdFORMATS)er->mY_3PTY_E((T30_INap *)&nlc[1])->resolu[0])n|=;T30_RESOLUnai[_R8_1540 |r->m->mT30_RESOLUnai[_R16_1540_OR_400 |;T30_RESOLUnai[_300_300 |r->m->mT30_RESOLUnai[_INCH_BASED |;T30_RESOLUnai[_METRIC_BASED("fff	}3
TY_E((T30_INap *)&nlc[1])->recmas		/_08)pert0es =
>m->mT30_REC
		INGnWIDTH_ISO_A3 |r->m->(T30_REC
		INGnLENGTH_UNLIMICIL << 2)r|r->m->(T30_MIN_SCANLINE_TIME_00_00_00 << 4f("fff}s	foT	ifPENDINB3_08) &== 5)"ffo_3PTY}ummyty& 0x0002)=/* Accept incoming ;ax-polling requ		rs */	ls	b	;ax_ord w;
_bits |= T30_				if _BIT_ CCEPT_POCAING;3PTY}ummyty& 0x2000)m/* Do n) &u] =MR compl_ss0])n*/	->>m-;ax_ord w;
_bits &= ~T30_				if _BIT_ENABLd_2D_		DING;3PTY}ummyty& 0x4000)m/* Do n) &u] =MMR compl_ss0])n*/	->>m-;ax_ord w;
_bits &= ~T30_				if _BIT_ENABLd_T6_		DING;3PTY}ummyty& 0x8000)m/* Do n) &u] =ECMn*/	->>m-;ax_ord w;
_bits &= ~T30_				if _BIT_ENABLd_ECM;3PTY}ummyPENDIN;ax_ordnectE =tu_li = 1r!= 0)"ffoY_3PTY_E((T30_INap *)&nlc[1])->resolu[0])n= ((T30_INap *)PENDIN;ax_ordnectE =tu_buffer)->resolu[0]);3PTY_E((T30_INap *)&nlc[1])->]ata_turmat = ((T30_INap *)PENDIN;ax_ordnectE =tu_buffer)->]ata_turmat;3PTY_E((T30_INap *)&nlc[1])->recmas		/_08)pert0es = ((T30_INap *)PENDIN;ax_ordnectE =tu_buffer)->recmas		/_08)pert0es;	ls	b	;ax_ord w;
_bits |= GE		}
			&((T30_INap *)PENDIN;ax_ordnectE =tu_buffer)->ord w;
_bits_low)rBr->m->m(T30_				if _BIT_REQUEST_POCAING |;T30_				if _BIT_MORE_DOCUMENTS)f3PTYr}"fff}s	YE/* copy sco[0])nid to_NLCn*/	->>acility_req"));
T30_MuX_STAnai[_IDnLENGTH;dapter->m_3PTY}ummyty< b3_ordfig_(TEplc2].li = 1)r--oY_3PTY_E((T30_INap *)&nlc[1])->sco[0])_id[ium	I((by  l*)b3_ordfig_(TEplc2]. =tu)[1m+ai];RMm	-}3PTY_[i] r->mY_3PTY_E((T30_INap *)&nlc[1])->sco[0])_id[ium	I' 'f3PTYr}"fff}s	YE((T30_INap *)&nlc[1])->sco[0])_id_li  = T30_MuX_STAnai[_IDnLENGTH;s	YE/* copy head line to_NLCn*/	->>
	ifb3_ordfig_(TEplc3].li = 1)r--S{
	->mrpos = (by  kf;ax_head_line_time	&(((T30_INap *)&nlc[1])->sco[0])_id[T30_MuX_STAnai[_IDnLENGTH])tf("fff}ummyPos != 0)"ffoY_3PTY_EummyCAPI_MuX_Dand TIME_LENGTHr+ 2m+ab3_ordfig_(TEplc3].li = 1 > CAPI_MuX_HEADnLINE_SPACEer->m->mpos = 0Pr->m->[i] r->mY__3PTY_E	nlc[1m+aoffsetof(T30_INap, sco[0])_id)r+ T30_MuX_STAnai[_IDnLENGTHr+ pos++um	I' 'f3PTYrE	nlc[1m+aoffsetof(T30_INap, sco[0])_id)r+ T30_MuX_STAnai[_IDnLENGTHr+ pos++um	I' 'f3PTYrE	li  = (by  kb3_ordfig_(TEplc2].li = 1f3PTYrE	
	ifli  > 20)"fforrrrli  = 20f3PTYrE	
	ifCAPI_MuX_Dand TIME_LENGTHr+ 2m+ali  + 2m+ab3_ordfig_(TEplc3].li = 1 <= CAPI_MuX_HEADnLINE_SPACEer->m->m{TY_NlYrracility_req"));
li ;dapter->m->mm	nlc[1m+aoffsetof(T30_INap, sco[0])_id)r+ T30_MuX_STAnai[_IDnLENGTHr+ pos++um	I((by  l*)b3_ordfig_(TEplc2]. =tu)[1m+ai];RMm	-rE	nlc[1m+aoffsetof(T30_INap, sco[0])_id)r+ T30_MuX_STAnai[_IDnLENGTHr+ pos++um	I' 'f3PTYrE		nlc[1m+aoffsetof(T30_INap, sco[0])_id)r+ T30_MuX_STAnai[_IDnLENGTHr+ pos++um	I' 'f3PTYrE	}
te_Nl}
te_N}3
TY_Eli  = (by  kb3_ordfig_(TEplc3].li = 1f3PTYr
	ifli  > CAPI_MuX_HEADnLINE_SPACE - poser->m->li  = (by  k(CAPI_MuX_HEADnLINE_SPACE - pose;
TY_E((T30_INap *)&nlc[1])->head_line_li  = (by  k(pos +ali e;
TY_Enlc[0rm+= (by  k(pos +ali e;
TY_Eacility_req"));
li ;dapter->m->nlc[1m+aoffsetof(T30_INap, sco[0])_id)r+ T30_MuX_STAnai[_IDnLENGTHr+ pos++um	II((by  l*)b3_ordfig_(TEplc3]. =tu)[1m+ai];RMm	} [i] r->mY((T30_INap *)&nlc[1])->head_line_li  = 0f3ate PENDINnsf_ord w;
_bits = 0PENDNTRIEPENDINB3_08) &== 5)"ffo_3PTY}ummyfnDINi-Crplci G)man_08)file.1);v	  _op[0])s & (1L << PRIVand auX_SUB_SEP_PWD))
PPffrrrrB3_XGE		}
			(by  l*)b3_ordfig_(TEplc1]. =tu)&& 0x8000	)m/* P);v	   SUB/SEP/PWD enD;
	 */s	tel{TY_NlYPENDINrequ		red_op[0])s |=I1L << PRIVand auX_SUB_SEP_PWD;RMm	-}3PTY_ummyfnDINi-Crplci G)man_08)file.1);v	  _op[0])s & (1L << PRIVand auX_NONSTANDARD))
PPffrrrrB3_XGE		}
			(by  l*)b3_ordfig_(TEplc1]. =tu)&& 0x4000	)m/* P);v	   non-scondard facilit0es enD;
	 */s	tel{TY_NlYPENDINrequ		red_op[0])s |=I1L << PRIVand auX_NONSTANDARD;RMm	-}3PTY_ummyfnDINi-requ		red_op[0])s_ordA | PENDINrequ		red_op[0])s | PENDINCrplci G)requ		red_op[0])s_tD;
	[E_REQ_PEND 0tu - 1])
PPffrrrrBif(1L << PRIVand auX_SUB_SEP_PWD)r| (1L << PRIVand auX_NONSTANDARD)))"ffoY_3PTY_EummyfnDINi-requ		red_op[0])s_ordA | PENDINrequ		red_op[0])s | PENDINCrplci G)requ		red_op[0])s_tD;
	[E_REQ_PEND 0tu - 1])
PPfffrrrrBif1L << PRIVand auX_SUB_SEP_PWD))
PPffm{TY_NlYr;ax_ord w;
_bits |= T30_				if _BIT_ CCEPT_SUBADDRESS |;T30_				if _BIT_ CCEPT_PASS}
		f3PTYrE	
	if;ax_ord w;
_bits & T30_				if _BIT_ CCEPT_POCAINGer->m->mm;ax_ord w;
_bits |= T30_				if _BIT_ CCEPT_SEL_POCAING;3PTY}	}
te_Nlli  = nlc[0r;3PTY}	pos = offsetof(T30_INap, sco[0])_id)r+ T30_MuX_STAnai[_IDnLENGTH("fffEN
	ifpos < PENDIN;ax_ordnectE =tu_li = 1)
PPffm{TY_NlYr;cility_r1m+aPENDIN;ax_ordnectE =tu_buffer[pos]q"))!_req")--er->m->mmnlc[++li um	IPENDIN;ax_ordnectE =tu_buffer[pos++u("fffEN}3PTY_E[i] 
>m->mmnlc[++li um	I0("fffEN
	ifpos < PENDIN;ax_ordnectE =tu_li = 1)
PPffm{TY_NlYr;cility_r1m+aPENDIN;ax_ordnectE =tu_buffer[pos]q"))!_req")--er->m->mmnlc[++li um	IPENDIN;ax_ordnectE =tu_buffer[pos++u("fffEN}3PTY_E[i] 
>m->mmnlc[++li um	I0("fffEN
	iffnDINi-requ		red_op[0])s_ordA | PENDINrequ		red_op[0])s | PENDINCrplci G)requ		red_op[0])s_tD;
	[E_REQ_PEND 0tu - 1])
PPfffrrrrBif1L << PRIVand auX_NONSTANDARD))
PPffv_3PTY_E ummyfpos < PENDIN;ax_ordnectE =tu_li = 1)rB3_XPENDIN;ax_ordnectE =tu_buffer[pos] != 0))r->mffv_3PTY_E N
	iffnDINi-;ax_ordnectE =tu_buffer[pos] >= 3)rB3_XnDINi-;ax_ordnectE =tu_buffer[posm+a1]r>= 2)er->m->mm	PENDINnsf_ord w;
_bits = GE		}
			&nDINi-;ax_ordnectE =tu_buffer[posm+a2])f3PTYrllN;cility_r1m+aPENDIN;ax_ordnectE =tu_buffer[pos]q"))!_req")--er->m->mmmnlc[++li um	IPENDIN;ax_ordnectE =tu_buffer[pos++u("fffENl}
te_NlN[i] r->mffv_3PTY_E N
	ifaTi_(TEse(&b3_ordfigi- =tu[1]A (_maskb3_ordfigi-li = 1ord wssslenb3_ordfig_(TEpl	)r->mmffv_3PTY_E Nmd.info + 1);
				non-scondard facilit0es  =tu missing or wrong ;urmat"tf("fffENmmmnlc[++li um	I0("fffENll}
te_NlNl[i] r->mffvv_3PTY_E NmummyEb3_ordfig_(TEplc4].li = 1m>= 3)rB3_Xb3_ordfig_(TEplc4]. =tu[1] >= 2)er->m->mm		PENDINnsf_ord w;
_bits = GE		}
			&b3_ordfig_(TEplc4]. =tu[2])f3PTYrllNmnlc[++li um	I(by  kfb3_ordfig_(TEplc4].li = 1)f3PTYrllNNacility_req"));
b3_ordfig_(TEplc4].li = 1;dapter->m->mm	mnlc[++li um	Ib3_ordfig_(TEplc4]. =tu[1m+ai];RMm	-rE	}
te_NlN}
te_Nl}
te_Nlnlc[0rm	I	el;3P_E N
	iffnDINi-nsf_ord w;
_bits & T30_NSF_				if _BIT_ENABLd_NSF)
PPfffrrrrB3_XnDINi-nsf_ord w;
_bits & T30_NSF_				if _BIT_NEGOTIand RESP))
PPffv_3PTY_E ((T30_INap *)&nlc[1])->operat		/_mrol = T30_OPERATINGnMODE_CAPI_NEG;RMm	->}3PTY_}3PTY}a3PTYPU		}
			&(((T30_INap *)&nlc[1])->ord w;
_bits_low),n;ax_ord w;
_bitseORMA	li  = offsetof(T30_INap, sco[0])_id)r+ T30_MuX_STAnai[_IDnLENGTH("fffacility_req"));
li ;dapter->m-PENDIN;ax_ordnectE =tu_buffer[ium	Inlc[1m+ai]ts	fo((T30_INap *)IPENDIN;ax_ordnectE =tu_buffer)->head_line_li  = 0f3E N
m+= ((T30_INap *)&nlc[1])->head_line_li f3E Nwhilemyty< nlc[0rer->m-PENDIN;ax_ordnectE =tu_buffer[li ++um	Inlc[++i]ts	foPENDIN;ax_ordnectE =tu_li = 1r	I	el;3P_}
>S[i] r--_3PTYnlc[0rm	I14Pr->m
	ifb3_ordfigi-li = 1m!= 16)swPe brw", I_B3_PARM_NOT_SUPPORCIL("fffacility_req"));
12P apte nlc[1m+ai] i b3_ordfigi- =tu[1m+ai]ts	foT	ifGE		}
			&b3_ordfigi- =tu[13])m!= 8rB3_GE		}
			&b3_ordfigi- =tu[13])m!= 128er->mYbrw", I_B3_PARM_NOT_SUPPORCIL("fffnlc[13] i b3_ordfigi- =tu[13]ts	foT	ifGE		}
			&b3_ordfigi- =tu[15re=>	Inlc[13]er->mYbrw", I_B3_PARM_NOT_SUPPORCIL("fffnlc[14] i b3_ordfigi- =tu[15]f3ES	r>	rN[i] r-_3PTT	ifPENDINB3_08) &== 4
mmrrrr|a PENDINB3_08) &== 5m/*T.30 - auX*/)&brw", I_B3_PARM_NOT_SUPPORCIL("f}
>Crd_p"LERT,_NLC,;nlc) fNbrw", I0;r}
s/*----------------------------------------------------------------*/	/*      make theAsame asnadd_b23enbut onlyaaciltheAmrolm rel	  dmm*/s/*      L2 ci-	L3 B-Chan 08) oco&.mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm*/s/*                                                                */s/*      EnD;
	d L2 ci-	L3 Crdfiguro[0])s:mmmmmmmmmmmmmmmmmmmmmmmmm*/s/*        If L1	=	-Mrolm allnnegotio[0])nmmmmmmmmmmmmmmmmmmmmmmmmm*/s/*          onlyaL2 =	-Mrolm wi 1mfullnnegotio[0])nis allow dmmmmm*/s/*        If L1	=	-Mrolm async cilsync                            */s/*          onlyaL2 =	-TransparIdt is allow dmmmmm                */s/*        L3 =	-Mrolm cilL3 =	-TransparIdt arI allow dmmmmm       */s/*      B2 Crdfiguro[0])aacilmrolm:mmmmmmmmmmmmmmmmmmmmmmmmm      */s/*          _masn: enD;
	/disD;
	 compl_ss0]), bitop[0])s m       */s/*      B3 Crdfiguro[0])aacilmrolm:mmmmmmmmmmmmmmmmmmmmmmmmm      */s/*          emptymmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm*/s/*----------------------------------------------------------------*/ss		 ic;_masnadd_mrolm_b23A		In	*0);
,nAPI_PARSEl*bp_(TEpl	
TE;s		 ic;by  llli[12rm	I{1,1} f	s		 ic;by  lllc[3]m	I{2,0,0};E;s		 ic;by  ldlc[16rm	I{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};E;API_PARSElmdm_crdfig[2]("f_masnN("f_masnb2_ordfigm	I0;EfNacility_req"));
2P apte mdm_crdfig[i].li = 1m= 0PENacility_req"));
sizeof(dlc)  apte dlc[irm	I0f3ENTRIE((GE		}
			bp_(TEplc0]. =tu)&== B1_MODEM_ CAcNEGOTIand)
mmmmmmB3_XGE		}
			bp_(TEplc1]. =tu)m!= B2_MODEM_EC_		MPRESSai[))
mrrrr|a ((GE		}
			bp_(TEplc0]. =tu)&!= B1_MODEM_ CAcNEGOTIand)
m	B3_XGE		}
			bp_(TEplc1]. =tu)m!= B2_TRANSPARENT)))sw{
e brw", I(_B_STACK_NOT_SUPPORCIL) fN}"	TRIE(GE		}
			bp_(TEplc2]. =tu)&!= B3_MODEM)
mrrrrB3_XGE		}
			bp_(TEplc2]. =tu)m!= B3_TRANSPARENT))sw{
e brw", I(_B_STACK_NOT_SUPPORCIL) fN}"ENVE_REQB2_08) &=-(by  knGE		}
			bp_(TEplc1]. =tu)("fnDINi-B3_08) &=-(by  knGE		}
			bp_(TEplc2]. =tu)("ENTRIE(GE		}
			bp_(TEplc1]. =tu)&== B2_MODEM_EC_		MPRESSai[)rB3_bp_(TEplc4].li = 1)r-_3PTT	ifCTi_(TEse(&bp_(TEplc4]. =tu[1],
fffrrrr  (_maskbp_(TEplc4].li = 1ord "r
fffrrrr  mdm_crdfig))swP{ate brw", I(_WRONG_MESSAGE_FORMAT)f3PT}RM	b2_ordfigm	IGE		}
			mdm_crdfig[0]. =tu)("f}"EN/* OK,aL2 is mrolm */	atlli[0] 	-1PENlli[1] 	-1PENTect_b3_ciCrplci G)manudburol_r_fearol_s & MANUdf(TURERdFEATURE_XONOFF_FLOW_				if )r->lli[1] |= 2t"fTect_b3_ciCrplci G)manudburol_r_fearol_s & MANUdf(TURERdFEATURE_OOB_CHANNELer->lli[1] |= 4PEENTRIE(lli[1] & 0x02)rB3_Xdiva_xdi_extended_fearol_s & DIVA_CAPI_USE_CMA))m{atelli[1] |= 0x10PENM
	ifPENDINrx_dma_descriptcil<= 0)r{ate PENDINrx_dma_descriptcil= diva_get_dma_descriptci"LERT,_&PENDINrx_dma_magic) fND;
	ifPENDINrx_dma_descriptcil>= 0)"fforPENDINrx_dma_descriptci++;
SA}ENM
	ifPENDINrx_dma_descriptcil> 0)r{ate lli[1] |= 0x40f3PTYlli[0] 	-6f3PTYlli[2rm	I(by  kfPENDINrx_dma_descriptcil- 1)f3PTYlli[3rm	I(by  kPENDINrx_dma_magicf3PTYlli[4rm	I(by  kfPENDINrx_dma_magicm>>  8)Ps	telli[5rm	I(by  kfPENDINrx_dma_magicm>> 16eORMA	lli[6rm	I(by  kfPENDINrx_dma_magicm>> 24)f3PT}RM}"	}ummyDIVA_CAPI_SUPPORCS_NO_CANCELt_b3_ciCrplci ))m{atelli[1] |= 0x20, 	}"	}llc[1rm	IyVE_REQca 0Idir &I(	 CAcLiR_ RIGINATE |I	 CAcLiR_FORCE_ (plcNL)e ?RMA/*V42*/ 10m: /*V42_IN*/ 9;
fllc[2rm	I4qmmmmmmmmmmmmmmmmmmmmmm/* passlL3 always transparIdt */s	Crd_p"LERT,_LLI,;llif("fCrd_p"LERT,_LLC,;llc) fNty_r-1PENPU		}
			&dlc[irA nDINi-PEND 0MaxDataLi = 1)PEN
m+= 2t"fTectGE		}
			bp_(TEplc1]. =tu)&== B2_MODEM_EC_		MPRESSai[)s	_3PTT	ifbp_(TEplc4].li = 1)r-P{ate ].info + 1);
				MDMnb2_ordfig=%02x",;b2_ordfig))Pr->mdlc[i++um	I3P /* Addr A */	lsmdlc[i++um	I1q"/* Addr B */	lsmdlc[i++um	I7q"/* modulo mrol */	lsmdlc[i++um	I7q"/* window;size */	lsmdlc[i++um	I0P /* XIDI	el Lo  */	lsmdlc[i++um	I0P /* XIDI	el Hi  */	r->m
	ifb2_ordfigm& MDM	B2_ciSABLd V42bis)"ffo_3PTY}dlc[irm|=IDLC_MODEMPROT_ciSABLd V42_V42BIS;RMm	}r->m
	ifb2_ordfigm& MDM	B2_ciSABLd MNP)"ffo_3PTY}dlc[irm|=IDLC_MODEMPROT_ciSABLd MNP MNP5;RMm	}r->m
	ifb2_ordfigm& MDM	B2_ciSABLd TRANS)"ffo_3PTY}dlc[irm|=IDLC_MODEMPROT_REQUIRE_PROTOCOL;RMm	}r->m
	ifb2_ordfigm& MDM	B2_ciSABLd V42)"ffo_3PTY}dlc[irm|=IDLC_MODEMPROT_ciSABLd V42_DETECT;RMm	}r->m
	ifb2_ordfigm& MDM	B2_ciSABLd 		MP)"ffo_3PTY}dlc[irm|=IDLC_MODEMPROT_ciSABLd 		MPRESSai[;RMm	}r->m
++;
SA}EN	rN[i] r-_3PTdlc[i++um	I3P /* Addr A */	lsdlc[i++um	I1q"/* Addr B */	lsdlc[i++um	I7q"/* modulo mrol */	lsdlc[i++um	I7q"/* window;size */	lsdlc[i++um	I0P /* XIDI	el Lo  */	lsdlc[i++um	I0P /* XIDI	el Hi  */	lsdlc[i++um	IDLC_MODEMPROT_ciSABLd V42_V42BISr|r->mDLC_MODEMPROT_ciSABLd MNP MNP5r|r->mDLC_MODEMPROT_ciSABLd V42_DETECTr|r->mDLC_MODEMPROT_ciSABLd 		MPRESSai[;RM}
Ddlc[0rm	I(by  kdi - 1)f3/* HexDump 		DLC",;sizeof(dlc),_&dlc[0r);d*/	TCrd_p"LERT,_DLC,;dlc) fNbrw", I(0fts}"	E/*------------------------------------------------------------------*/	/* send a requ		raaciltheAsignaling Idtitymmmmmmmmmmmmmmmmmmmmmmmmmm*/s/*------------------------------------------------------------------*/	ss		 ic;voidAsig_reqA		In	*0);
,nby  lreq,nby  lId	
TE;ummy!0);
)&brw", t"fTect_b3_ciCrplci G)Crplci _disD;
	d)&brw", t"f].info + 1);
				sig_reqA%x)",;reqe)tatummyreq&== REMOVd)
m	_b3_cisig_remove_idm	IPENDINSig.Idt"fTect_b3_cireq_in&== _b3_cireq_in_s		r*)={
lYPENDINreq_in&+= 2t"fYPENDINRBuffer[pENDINreq_in++um	I0Pr>	rNPU		}
			&PENDINRBuffer[pENDINreq_in_s		r*rA nDINi-req_in-pENDINreq_in_s		r* - 2)("fnDINi-RBuffer[pENDINreq_in++um	IIdP   /* sig/nl flagd*/	TnDINi-RBuffer[pENDINreq_in++um	IreqP  /* requ		ra*/	TnDINi-RBuffer[pENDINreq_in++um	I0P    /* )
				&	*/		pENDINreq_in_s		r* = _b3_cireq_in;r}
s/*------------------------------------------------------------------*/	/* send a requ		raaciltheAnet_mak layer Idtitymmmmmmmmmmmmmmmmmmmmmm*/s/*------------------------------------------------------------------*/	ss		 ic;voidAnl_req_ncciA		In	*0);
,nby  lreq,nby  lncci	
TE;ummy!0);
)&brw", t"fTect_b3_ciCrplci G)Crplci _disD;
	d)&brw", t"f].info + 1);
				nl_req %02x %02x %02x",_PENDINId,lreq,nncci	)tatummyreq&== REMOVd)
m{
lYPENDINnl_remove_idm	IPENDINNL.Idt"f	ncci_remove"LERT,_0,I(by  kdncci != 0))t"f	nccim	I0Pr>	rNTect_b3_cireq_in&== _b3_cireq_in_s		r*)={
lYPENDINreq_in&+= 2t"fYPENDINRBuffer[pENDINreq_in++um	I0Pr>	rNPU		}
			&PENDINRBuffer[pENDINreq_in_s		r*rA nDINi-req_in-pENDINreq_in_s		r* - 2)("fnDINi-RBuffer[pENDINreq_in++um	I1P    /* sig/nl flagd*/	TnDINi-RBuffer[pENDINreq_in++um	IreqP  /* requ		ra*/	TnDINi-RBuffer[pENDINreq_in++um	I_b3_ciCrplci G)ncci_ch[ncci];   /* )
				&	*/		pENDINreq_in_s		r* = _b3_cireq_in;r}
ss		 ic;voidAsend_reqA		In	*0);
	
TE;ENTITY *e("f_masnlf3/*  _masnN(	*/	atummy!0);
)&brw", t"fTect_b3_ciCrplci G)Crplci _disD;
	d)&brw", t"f)
				&_xmit_xont_b3_)("EN/*  f nothing to do,&brw", I*/		Tect_b3_cireq_in&== _b3_cireq_out)&brw", t"f].info + 1);
				send_reqAi =%d,out=%d)"A nDINi-req_in, _b3_cireq_out))("ENTRIEPENDINnl_reqr|a PENDINsig_req)&brw", t"
fl = GE		}
			&nDINi-RBuffer[pENDINreq_out])("fnDINi-req_out&+= 2t"fnDINi-XData[0].Pm	I&nDINi-RBuffer[pENDINreq_out]("fnDINi-req_out&+= lt"fTect_b3_ciRBuffer[pENDINreq_out]m== 1)
m{
lYem	I&nDINi-NL;
lYPENDINreq_out++;
SAeciReq&= PENDINnl_reqr= PENDINRBuffer[pENDINreq_out++u("ffeciReqChr= PENDINRBuffer[pENDINreq_out++u("ffummy!(e 0tu & 0x1f))swP{ate e 0tu = NL_IDts	foPENDINRBuffer[pENDINreq_out - 4rm	ICAIts	foPENDINRBuffer[pENDINreq_out - 3rm	I1q"ffTPENDINRBuffer[pENDINreq_out - 2rm	IyVE_REQSig.Idm== ((ffe ?;0m: PENDINSig.Idt"ffTPENDINRBuffer[pENDINreq_out - 1um	I0("fffl&+= 3t"ffTPENDINnl_global\req&= PENDINnl_req;
SA}ENM].info + 1);
				%x:NLREQA%x:%x:%x)"A nDINi-Crplci G)Id,leG)Id,leG)Req,neciReqCh)) fN}"	[i] Em{
lYem	I&nDINi-SigOR	fTect_b3_ciRBuffer[pENDINreq_out])ate e 0tu = nDINi-RBuffer[pENDINreq_out]("fYPENDINreq_out++;
SAeciReq&= PENDINsig_reqr= PENDINRBuffer[pENDINreq_out++u("ffeciReqChr= PENDINRBuffer[pENDINreq_out++u("ffummy!(e 0tu & 0x1f))swP	_b3_cisig_global\req&= PENDINsig_req;ENM].info + 1);
				%x:SIGREQA%x:%x:%x)"A nDINi-Crplci G)Id,leG)Id,leG)Req,neciReqCh)) fN}"	nDINi-XData[0].PLi = 1r	I	 fNeciX&= PENDINXData("fnDINi-Crplci G)requ		r(e);RM].info + 1);
				send_ok"tf("}
ss		 ic;voidAsend_]ataA		In	*0);
	
TE;DIVA_CAPI_ADAPTER *a("fLATA_B3_DESC *data("fNCIn	mm*ncci_ptr("f_masnncci;	atummy!0);
INnl_reqrB3__ENDINncci_r		/_list)
m{
lYam	I_b3_ciCrplci t"f	nccim	I_ENDINncci_r		/_list;ENM]or--_3PTYnccim	IaINncci_next[ncci];3PTYncci_ptrm	I&(aINncci[ncci]) fND;
	if!(aINncci_ch[ncci]
fffrrrr  B3_XaINch_flow_ord w;
[aINncci_ch[ncci]] & N_OK_FC_PENDING)	)r->m{TY_Nl
	ifncci_ptr->]ata_pend		/)"ffoY_3PTY_EummyfaINncci_s		 e[ncci]m== CONNECTED)
PPfffrrrr|a (aINncci_s		 e[ncci]m== INC_ACT_PENDING)
PPfffrrrr|a (PENDINsend_]iscm== ncci	)
PPffv_3PTY_E ]atam	I&(ncci_ptr->DBuffer[ncci_ptr->]ata_out])("fP_E N
	iffnDINi-B2_08) &== B2_V120_ SYNC)	,	Pfffrrrr|a (PENDINB2_08) &== B2_V120_ SYNC_V42BIS)	,	Pfffrrrr|a (PENDINB2_08) &== B2_V120_BIT_TRANSPARENT))swPPffv_3PTY_E fnDINi-NData[1].Pm	ITransmitBufferGett_b3_ciCppl + ata->P)f3PTYrllNnDINi-NData[1].PLi = 1r	I ata->Li = 1f3PTYrE	N
	ifdata->Flags & 0x10)"fforrrrNnDINi-NData[0].Pm	Iv120__REQ__headerf3PTYrE	N[i] r->mffvvNnDINi-NData[0].Pm	Iv120_default_headerf3PTYrE	NnDINi-NData[0].PLi = 1r	I1f3PTYrE	NnDINi-NL.XNum = 2t"fYYrE	NnDINi-NL.Req&= PENDINnl_reqr= (by  kf(data->Flags & 0x07) << 4 | N_DATA)t"fYYrE	}
te_NlN[i] r->mffv_3PTY_E NnDINi-NData[0].Pm	ITransmitBufferGett_b3_ciCppl + ata->P)f3PTYrllNnDINi-NData[0].PLi = 1r	I ata->Li = 1f3PTYrE	N
	ifdata->Flags & 0x10)"fforrrrNnDINi-NL.Req&= PENDINnl_reqr= (by  kN_ULATA;
3PTYrE	N[i]  
	iffnDINi-B3_08) &== B3_RTP)rB3_Xdata->Flags & 0x01)er->m->mm	PENDINNL.Req&= PENDINnl_reqr= (by  kN_BLATA;
3PTYrE	N[i] r->m->mm	PENDINNL.Req&= PENDINnl_reqr= (by  kf(data->Flags & 0x07) << 4 | N_DATA)t"fYYrE	}
te_NlNnDINi-NL.Xm	IPENDINNData("f->mm	PENDINNL.ReqChr= aINncci_ch[ncci]("f->mm	].info + 1);
				%x:DREQA%x:%x)"A aG)Id,lPENDINNL.Id,lPENDINNL.Reqe)tat->mm	PENDIN]ata_sIdt 	-tru PEN	>mm	PENDIN]ata_sIdt_ptrm	I ata->PPEN	>mm	aG)requ		r(&nDINi-NLf("fffEN}3PTY_E[i] ={TY_NlYrcleanup_ncci_]ataALERT,_ncci	("fffEN}3PTY_}3PTYE[i] =Tect_b3_cisend_]iscm== ncci	"ffoY_3PTY_E/* d1);
				N_ciSC");d*/	TrllNnDINi-NData[0].PLi = 1r	I0("fffENPENDINNL.ReqChr= aINncci_ch[ncci]("f->mmPENDINNL.Req&= PENDINnl_reqr= N_ciSC("f->mmaG)requ		r(&nDINi-NLf("fffENnDINi-commci-	= _ciSCONNECT_B3_R("fffENnDINi-send_]iscm=I0("fffE}3PTY}ate} whilemy!0);
INnl_reqrB3_dncci != _ENDINncci_r		/_list))("ffnDINi-ncci_r		/_list =nncci;	Y}a}
ss		 ic;voidAlisten_check(DIVA_CAPI_ADAPTER *a)
TE;_masnN,njPENP	In	*0);
;3Pby  lactivnotifiedca 0sm	I0f3EN].info + 1);
				listen_check(%d,%d)"A aG)listen_activeA aG)max_listen	)tatummy!remove_s		r*edrB3_!aG)Crplci _disD;
	d)r-_3PTacility_req"));
aG)max_0);
;dapter->{swP	_b3_m	I&(aIN_b3_[i]) fND;
	ifnDINi-notifiedca 0)lactivnotifiedca 0s++;
SA}ENM].info + 1);
				listen_check(%d)"A activnotifiedca 0s))("ENTacility_raG)listen_activeq"));
y=_mask(aG)max_listen + activnotifiedca 0s))( apte {fND;
	if(jy_rget_nDIN(a)))m{ate eaG)listen_active++;
SAP	_b3_m	I&aIN_b3_[j - 1u;
SAP	_b3_i-S		 em	ILISTENING;3ate eard_p"LERT,_OAD, "\x01\xfd");3ate eard_p"LERT,_KEY, "\x04\x43\x41\x32\x30");3ate eard_p"LERT,_CAI, "\x01\xc0");3te eard_p"LERT,_UID, "\x06\x43\x61\x70\x69\x32\x30");3te eard_p"LERT,_LLI,;"\x01\xc4");dmmmmmmmmmmmmmmmmm/* support Dummy CR FAC + MWI + SpoofNotifyn*/	->mrard_p"LERT,_SHIFT | 6, NULL);3te eard_p"LERT,_SIN,;"\x02\x00\x00");3te e_b3_i-;
	ernal\ormmci-	= LISTEN_SIG_ SSIGN_PEND;mmmmm/* do ind	c	  _reqr
	iOK  */	lsm	sig_reqALERT,_ SSIGN, DSIG_ID);3te esend_reqA_b3_)("PTY}ate}	Y}a}
s/*------------------------------------------------------------------*/	/* func[0])s acilallnparame	ers sIdt in INDsdmmmmmmmmmmmmmmmmmmmmmmm*/s/*------------------------------------------------------------------*/	ss		 ic;voidAIndPTEse(		In	*0);
,n_masn*(TEpl_id,nby  l**(TEpl,nby  lmultiIEsize)
TE;_masnploc;dmmmmmmmmmmm/* po;
	s to currIdt loco[0])awi 1in packera*/	Tby  lw;3Pby  lw	el;3Pby  lcodeset, lock;3Pby  l*in;rf_masnN("f_masncode("f_masnmIEindexm=I0("fplocm=I0("fcodesetm=I0("flockm	I0f3ENTnm	IPENDINSig.RBuffer->PPENacility_req"));
(TEpl_id[0]f apte mm/* multiIE
(TEpl_id ord ai)s just theA1	ra*/	T{mmmmmmmmmmmmmmmmmmmmmmmmmmmm/* elemIdt but (TEpl array is largermmmmmm*/s e_TEplcirm	I(by   *)"" fN}"	acility_req"));
multiIEsize;dapter-{s e_TEplcirm	I(by   *)"" fN}"
Nwhilemyplocm<IPENDINSig.RBuffer->li = 1m- 1) {
	->/* read  =turmat0])aelemIdt id ci-	li = 1mmmmmmmmmmmmmmmmmmm*/s		wm	Iin[ploc];"	};
	ifw&& 0x80) {
/*    w&&=0xfeq"removed,ncann) &de	ect ordg		r0])alev[i] */s/*    upperm4 bit maskedrwi 1mw==SHIFT nowmmmmmmmmmmmmmmm*/s			wli  = 0f3E }
>S[i] ={s			wli  = (by  kdin[plocm+a1]r+ 1)f3PT}	->/* checkr
	ili = 1mvalid (n) &exceeding Iddrof packer)mmmmmm*/s		
	iffnDocm+awli ) > 270)&brw", t"fr
	iflockm& 0x80) lockm&= ((7f(ENM[i] =codesetm=Ilock;3s		
	iffw&& 0xf0)&== SHIFT)m{ate codesetm=Iin[ploc];"	D;
	if!(codesetm& 0x08)) lockm= (by  kdcodesetm& 7)("PTYcodesetm&	I7f3PTYlockm|= 0x80f3E }
>S[i] ={s			
	ifw&== ESC B3_wli  >= 3)rcrol = in[plocm+a2]ma 0x800("fff[i] =code = w("PTYcodem|= dcodesetm<< 8);3ate ;cility_r1q"));
(TEpl_id[0]r+ 1rB3__TEpl_id[i] != code(dapte;	r->m
	if));
(TEpl_id[0]r+ 1)m{ate e
	if!multiIEsize)m{m/* wi 1mmultiIEs&u] =next field  =dex,mmmmmmmmmm*/s					mIEindexm=Ii - 1P    /* wi 1mnurmal IEs&u] =same indexmlike
(TEpl_id */s				}3
TY_E_TEplcmIEindexrm	I&in[plocm+a1];3te e].info + 1);
				mIE[%d]=0x%x",;*_TEplcmIEindexr,Iin[ploc]tf("fff}ummyPTEpl_id[i] == OAD"fff}    |a PTEpl_id[i] == CONN_NR"fff}    |a PTEpl_id[i] == CAD)r_3PTY_E
	ifin[plocm+a2]m& 0x80) {
Y_NlYrin[plocm+a0rm	I(by  kdin[plocm+a1]r+ 1)f3PTNlYrin[plocm+a1rm	I(by  kdin[plocm+a2]m& 0x7f)f3PTNlYrin[plocm+a2rm	I0x80("fffENl_TEplcmIEindexrm	I&in[ploc];RMm	->}3PTY_}3PTY	mIEindex++;mmmmmmm/* effec	s multiIEs&onlya*/s			}ate}	"ffnDocm+	I(wli  + 1) fN}"	brw", t"}
s/*------------------------------------------------------------------*/	/* try to match a cip from received BC ci-	HLCmmmmmmmmmmmmmmmmmmmmmm*/s/*------------------------------------------------------------------*/	ss		 ic;by   ie\ormparI(by   *ie1,nby  l*ie2)
TE;_masnNtatummy!ie1r|a !ie2)&brw", Ifai] tatummy!ie1[0r)&brw", Ifai] tatacility_req"));
=_mask(ie1[0r + 1)  apte 
	ifie1[i] != ie2[i])&brw", Ifai] tatbrw", Itru PE}
ss		 ic;_masnfind_cip(DIVA_CAPI_ADAPTER *a,nby  l*bc,nby  l*hlc)
TE;_masnNtat_masnj;EfNacility_r9q"))B3_!ie\ormparI(bc,ncip_bc[ir[aINu_lawr);d)--e;EfNacilijy_r16; j);
29)B3
ffrrrr (!ie\ormparI(bc,ncip_bc[jr[aINu_lawr)r|a !ie\ormparI(hlc,ncip_hlc[jr))( jpte;	tummyj == 29)&brw", Iitatbrw", Ijts}"	Es		 ic;by   AddI=tu(by   **ard_i,
ffrrrrby   **fty_i,
ffrrrrby   *esc_chi,
ffrrrrby   *facility)
TE;by   i;3Pby  lj;3Pby  lk;3Pby  lf	el;3Pby  lli  = 0f3E/* facility is a=ne	redIstruurol_ */s	/* FTYncan beAmrre than oncemmmmmm*/s	tummyesc_chi[0r B3_!yesc_chi[esc_chi[0r]m& 0x7f))
m{
lYard_i[0rm	I(by   *)"\x02\x02\x00"P /* u] =nei 1ermbmnur d )
				&	*/		}	"f[i] Em{
lYard_i[0rm	I(by   *)"" fN}"	ummy!fty_i[0r[0rer-{
lYard_i[3rm	I(by   *)"" fN}"	[i] Em{mmmm/* facility array found  */	lsacility_re, jy_r1q"));
MuX_MULTI_IE
B3_fty_i[i][0]f apte
-P{ate ].info + 1);
				AddIFac[%d]",;fty_i[i][0])eORMA	li  +	Ifty_i[i][0]fRMA	li  +	I2t"fYYfli  = fty_i[i][0]fRMA	facility[j++um	I0x1cq"/* orpy fac IEn*/	->>acilikm	I0f kl<= f	el; k++, jpte
foY_3PTY_facility[j] = fty_i[i][k];s/*      ].info + 1);
				%x ",facility[j]));d*/	Trl}ate}
>Sfacility[0rm	I	el;3P_ard_i[3rm	Ifacility fN}"/*  ].info + 1);
				FacArrLe =%d ",len	)td*/	Tli  = ard_i[0r[0r + ard_i[1r[0r + ard_i[2r[0r + ard_i[3][0]fRMli  +	I4qmmmmmmmmmmmmmmmmmmmmmmmmmm/* calcul	  ili = 1mof alln*/	Tbrw", I(li e;
}
s/*------------------------------------------------------------------*/	/* voicemci-	codec fearol_s                                         */s/*------------------------------------------------------------------*/	ss		 ic;voidASetVoiceC
				&A		In	*0);
,nby  l*chi, DIVA_CAPI_ADAPTER *a)
TE;by  lvoice_chi[rm	I"\x02\x18\x01";3Pby  lc
				&;EfN)
				&	= chi[chi[0r]m& 0x3t"f].info + 1);
				ExtDevON(Ch=0x%x)"A )
				&)eORMvoice_chi[2rm	Iy)
				&) ? )
				&	:I1f3Pard_p"LERT,_FTY,;"\x02\x01\x07");dmmmmmmmmmmmm/* B On, default ])a1d*/	TCrd_p"LERT,_ESC,lvoice_chi);dmmmmmmmmmmmmmmmmm/* C
				&	*/		sig_reqALERT,_TEL_CTRL, 0fts	send_reqA_b3_)("PT	ifC->AdvSignal		Iner-{
lYarv_voice_write\orefsfC->AdvSignal		In, ADV_VOICE_WRITE_ACTIVanai[);	Y}a}
ss		 ic;voidAVoiceC
				&OffA		In	*0);
	
TE;].info + 1);
				ExtDevOFF"tf("fard_p"LERT,_FTY,;"\x02\x01\x08");dmmmmmmmmmmmm/* B Off	*/		sig_reqALERT,_TEL_CTRL, 0fts	send_reqA_b3_)("PT	ifnDINi-Crplci G)AdvSignal		Iner-{
lYarv_voice_clear_ordfigfnDINi-Crplci G)AdvSignal		Ine;	Y}a}
sss		 ic;_masnAdvCodecSupport(DIVA_CAPI_ADAPTER *a,n		In	*0);
,nAPPL *appl 
	ffrrrrby   hook_listen	
TE;_masnjPENP	In	*s0);
;3
>/* checkr
	ihardwarI supportsihandsetmwi 1mhook sco[_s (arv.codec) */s	/* cil
	ijust a ])aboar-	codec is support dmmmmm                   */s	/* theAarvance-	codec _b3_mis just acil;
	ernal u] =               */ss	/* diva P8)mwi 1mon-boar-	codec:mmmmmmmmmmmmmmmmmmmmmmmmm          */s	T	ifC->08)file.Global\Op[0])s & HANDSETer-{
lY/* new callenbut hook sco[_s arI alreadyAsignal
	d */s		
	ifC->AdvCodecFLAGe
-P{ate T	ifC->AdvSignalAppl != applr|a C->AdvSignal		Iner-fo_3PTY}d.info + 1);
				AdvSigPb3_=0x%x",;C->AdvSignal		Ine)f3PTNlbrw", I0x2001q"/* ordec in u] =by anothermapplico[0])n*/	->m}s	foT	ifPEND != NULL)r-fo_3PTY}C->AdvSignal		Inm	IPEND;3te e_b3_i-t	&	= ADV_VOICE("PTY}ateNbrw", I0;mmmmmmmmmmmm          /* arv ordec still u] d */s		}ENM
	if(jy_rget_nDIN(a)))
-P{ate s_b3_m	I&aIN_b3_[j - 1u;
SAPs_b3_i-t	&	= CODEC_PERMANENT;RMm	/* hook_listen ind	c	  sl
	iaIfacility_reqrwi 1mhandset/hook support */	->m/* was sIdt. Otherwi] =Tecjust a call ])aan external devicemwas maol */	lsm/* theAordec will beAu] d but theAhook  =tu will beA]iscar- d (just  */	lsm/* theAexternal ord w;
lermis in u] =mmmmmmmmmmmmmmmmmmmmmm          */s	NM
	ifhook_listen	 s_b3_i-S		 em	IADVANCED_VOICE_SIG("fff[i] r-fo_3PTY}s_b3_i-S		 em	IADVANCED_VOICE_NOSIG("fffoT	ifPEND	"ffoY_3PTY_EnDINi-spoofed_msgm	ISPOOFINGnREQUIRED;RMm	-}3PTY_/*  nd	c	   D-ch ordnect=Tecn*/	->m}mmmmmmmmmmmmmmmmmmmmmmmmm               /* codec is ordnect d OK     */s	NM
	ifPEND != NULL)r-fo_3PTY}C->AdvSignal		Inm	IPEND;3te e_b3_i-t	&	= ADV_VOICE("PTY}ateNC->AdvSignalAppl = appl;ateNC->AdvCodecFLAG 	-tru PEN	>C->AdvCodec		Inm	IsPEND;3te ard_p"sLERT,_CAI, "\x01\x15");3te ard_p"sLERT,_LLI,;"\x01\x00");3te ard_p"sLERT,_ESC,l"\x02\x18\x00");3te ard_p"sLERT,_UID, "\x06\x43\x61\x70\x69\x32\x30");3te s_b3_i-;
	ernal\ormmci-	= PERM_		D_ SSIGN;ate ].info + 1);
				Codec Assign"tf("fffsig_reqAsLERT,_ SSIGN, DSIG_ID);3te send_reqAs_b3_)("PT}
>S[i] r--_3PTYbrw", I0x2001q"/* wrong sco[_, no mrre _b3_s */s		}EN}"	[i]  T	ifC->08)file.Global\Op[0])s & ON_BOARD_		DEC)s	_3PTT	ifhook_listen	 brw", I0x300B;mmmmmmmmmmmm   /* Facility n) &support dm*/s		/* noAhook wi 1mSCOM      */s	N
	ifPEND != NULL) _b3_i-t	&	= CODEC("PT].info + 1);
				S/SCOM codec"tf("ff/* first timemweAu] ltheAsorm-sAordec w lmust shut downltheA;
	ernal  m*/s		/* handsetmapplico[0])nof theAoar-. This oan beAdon =by an assign wi 1m*/s		/* a cai wi 1mtheA0x80 bit set. Assign brw", Icodemis 'out of resource'*/s	N
	if!aG)sorm_appl_disD;
	e {fND;
	if(jy_rget_nDIN(a)))m{ate es_b3_m	I&aIN_b3_[j - 1u;
SAP ard_p"sLERT,_CAI, "\x01\x80");3te eard_p"sLERT,_UID, "\x06\x43\x61\x70\x69\x32\x30");3te fsig_reqAsLERT,_ SSIGN, 0xC0)P  /* 0xc0mis theATEL_ID */	lsm	send_reqAs_b3_)("PT	>C->sorm_appl_disD;
	 	-tru PEN	>}"fff[i] {3PTNlbrw", I0x2001q"/* wrong sco[_, no mrre _b3_s */s		Y}ate}	Y}a	[i]  brw", I0x300B;mmmmmmmmmmmm   /* Facility n) &support dm*/sfNbrw", I0;r}
sss		 ic;voidACodecIdCheck(DIVA_CAPI_ADAPTER *a,n		In	*0);
	
TEE;].info + 1);
				CodecIdCheck"))("ENTRIEC->AdvSignal		Inm		IPEND)s	_3PT].info + 1);
						Inmowns codec"tf("ffVoiceC
				&OffAC->AdvCodec		In)t"fr
	ifC->AdvCodec		Ini-S		 em		IADVANCED_VOICE_NOSIGe
-P{ate ].info + 1);
				remove tempAordec 		In"tf("fffPEND_remove"C->AdvCodec		In)t"frNC->AdvCodecFLAG m=I0("fffC->AdvCodec		Inm = NULL("fffC->AdvSignalAppl = NULL("ff}ateC->AdvSignal		Inm	INULL("f}a}
s/* -------------------------------------------------------------------
   Ask acilphysicol ardl_ss of oar- ])aPInmbus
   ------------------------------------------------------------------- */ss		 ic;voidAdiva_ask_tur_xdi_sdram_bar(DIVA_CAPI_ADAPTER *a,"fff}       IDI_SYNC_REQ	*0req)&{atC->sdram_barm=I0("f
	ifdiva_xdi_extended_fearol_s & DIVA_CAPI_XDI_PROVIDES_SDRAM_BAR)m{ateENTITY *em	IyENTITY *)preq;E
ffeciu] r[0rm	Ia 0tu - 1("ffpreq->xdi_sdram_bar. =tu.barm  m=I0("ffpreq->xdi_sdram_bar.Req&&&&&&&&&=I0("ffpreq->xdi_sdram_bar.Rc           = IDI_SYNC_REQ_XDI_GE		ADAPTER_SDRAM_BAR;E
ff(*(aG)requ		r))(e);RateC->sdram_barm=Ipreq->xdi_sdram_bar. =tu.bar("PT].inf3 + 1);
				A(%d) SDRAM BARm=I%08x"A aG)Id,lC->sdram_bar))("f}a}
s/* -------------------------------------------------------------------
   Ask XDI about extended fearol_s
   ------------------------------------------------------------------- */ss		 ic;voidAdiva_get_extended_Crplci _fearol_s(DIVA_CAPI_ADAPTER *a)m{atIDI_SYNC_REQ	*0req;fN)
	r buffer[((sizeof(preq->xdi_extended_fearol_s)r+ 4) > sizeof(ENTITY)e ?;(sizeof(preq->xdi_extended_fearol_s)r+ 4) : sizeof(ENTITY)];EfN)
	r fearol_s[4]("fpreqr= (IDI_SYNC_REQ	*)&buffer[0];	atummy!diva_xdi_extended_fearol_s)m{ateENTITY *em	IyENTITY *)preq;EPT]iva_xdi_extended_fearol_s |= 0x80000000;E
ffeciu] r[0rm	Ia 0tu - 1("ffpreq->xdi_extended_fearol_s.Req&= 0("ffpreq->xdi_extended_fearol_s.Rc  = IDI_SYNC_REQ_XDI_GE		EXTENDED_FEATURESf3PTpreq->xdi_extended_fearol_s. =tu.buffer_li = 1_in_by  sm	Isizeof(fearol_s)f3PTpreq->xdi_extended_fearol_s. =tu.fearol_s 	I&fearol_s[0];	atf(*(aG)requ		r))(e);Rate
	if;earol_s[0] & DIVA_XDI_EXTENDED_FEATURES_VALID)r_3PTY/*
	ffrrCheck fearol_s loco[ed  = theAby   '0'
	ff*/s	NM
	if;earol_s[0] & DIVA_XDI_EXTENDED_FEATURE_CMA)m{ate e]iva_xdi_extended_fearol_s |= DIVA_CAPI_USE_CMAPEN	>}"fff
	if;earol_s[0] & DIVA_XDI_EXTENDED_FEATURE_RX_DMA)m{ate e]iva_xdi_extended_fearol_s |= DIVA_CAPI_XDI_PROVIDES_RX_DMA;3te e].info + 1);
				XDI 08)vid_s RxDMA"tf("fff}"fff
	if;earol_s[0] & DIVA_XDI_EXTENDED_FEATURE_SDRAM_BAR)m{ate e]iva_xdi_extended_fearol_s |= DIVA_CAPI_XDI_PROVIDES_SDRAM_BAR;Efff}"fff
	if;earol_s[0] & DIVA_XDI_EXTENDED_FEATURE_NO_CANCEL_RC)m{ate e]iva_xdi_extended_fearol_s |= DIVA_CAPI_XDI_PROVIDES_NO_CANCEL;3te e].inf3 + 1);
				XDI 08)vid_s NO_CANCEL_RC fearol_"tf("fff}"ate}	Y}a
	diva_ask_tur_xdi_sdram_bar(a,n0req);
}
s/*------------------------------------------------------------------*/	/* autom	 ic;lawmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm*/s/*------------------------------------------------------------------*/	/* cal
	d from OS specific;p	r* afterminit timemtorget theALawmmmmmmmmmmmmmm*/	/* a-lawm(Euro)mci-	u-lawm(us,japan	 u] ldifferIdt BCs  = theASrw"p m_ssagl */	voidAAutom	 icLaw(DIVA_CAPI_ADAPTER *a)
TE;_masnjPENP	In	*s0);
;3
>
	ifC->autom	 ic_law)m{atebrw", t"f}"	TRIE(jy_rget_nDIN(a)))m{atediva_get_extended_Crplci _fearol_s(af("ffs_b3_m	I&aIN_b3_[j - 1u;
SAC->autom	 ic_law		Inm	IsPEND;3teC->autom	 ic_lawr	I1f3PTard_p"sLERT,_CAI, "\x01\x80");3teard_p"sLERT,_UID, "\x06\x43\x61\x70\x69\x32\x30");3tes_b3_i-;
	ernal\ormmci-	= USELAW_REQ;3tes_b3_i-ormmci-	= 0;3tes_b3_i-number	= 0;3tesig_reqAsLERT,_ SSIGN, DSIG_ID);3tesend_reqAs_b3_)("P}a}
s/* cal
	d from OS specific;p	r* 
	ianmapplico[0])nsendsianmCapi20Releasl */	_masnCapiReleasl=_maslId	
TE;_masnN,nj,mappls_found;ENP	In	*0);
;3PAPPL mm*this;	lDIVA_CAPI_ADAPTER *a("atummy!Id)s	_3PT].inf0 + 1);
				A:nCapiReleasl=Id==0)"tf("ffbrw", I(_WRONG_APPL_ID);3t}a
	thism	I&applico[0])[tu - 1];mmmmmmmmmmmm   /* getmapplico[0])npo;
	er	*/sfNacility_re, appls_foundy_req"));
max_appl;dapter-{s eT	ifCTplico[0])[i].Id)mmmm   /* anmapplico[0])nhas been found        */s	N{ate appls_found++;
SA}EN	r"	acility_req"));
max_arplci t apte mmmmmmmmm   /* soan all arplci s...    */s	{
lYam	I&arplci [iu("ffummyaG)requ		r)s	N{ate a 0t=tu_Mask[tu - 1]m=I0("fffC->CIP_Mask[tu - 1]m=I0("fffC->Notifico[0])_Mask[tu - 1]m=I0("fffC->ordec_listen[tu - 1]m=INULL("fffC->requ		red_op[0])s_tD;
	[tu - 1]m=I0("fffacilijy_r0; j);
aG)max_0);
;djpte mmmmmmmmm /* and all P	Ins ordnect d */s	NM{mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm /* wi 1mthis applico[0])nmm*/s				_b3_m	I&aIN_b3_[j]("fffoT	ifPEND 0tu)mmmmmmmmmmmmmmmmmmmmmmmm /* T	i_b3_mowns nomapplico[0])n*/	->mM{mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm/* Tt may beAn) &jet ordnect d */s	NMfoT	ifPEND 0S		 em		IINC_CON_PENDINGs	NMfommmm|a PENDINS		 em		IINC_CON_ALERT)
PPffv_3PTY_E T	if  st_c_ind_mask_bitALERT,_=_mask(tu - 1)))swPPffv_3PTY_E fclear_o_ind_mask_bitALERT,_=_mask(tu - 1));3PTY_E fT	ifo_ind_mask_emptyfPEND	er->m->mm_3PTY_E f	sig_reqALERT,_HANGUP, 0fts	TY_E f	send_reqA_b3_)("PTY	te e_b3_i-S		 em	I (plcDIS_PENDING("PTY	te }
te_NlN}
te_Nl}
te_NlT	if  st_c_ind_mask_bitALERT,_=_mask(tu - 1)))swPPff_3PTY_E clear_o_ind_mask_bitALERT,_=_mask(tu - 1));3PTY_E T	ifo_ind_mask_emptyfPEND	er->m->m{3PTY_E fT	if!0);
INappler->m->mm_3PTY_E f	PEND_remove"_b3_)("PTY	te e_b3_i-S		 em	IIDLE("PTY	te }
te_NlN}
te_Nl}
te_NlT	if0);
INapplm		Ithis)swPPff_3PTY_E 0);
INapplm	INULL("fff f	PEND_remove"_b3_)("PTY	te_b3_i-S		 em	IIDLE("PTY	t}3PTY_}3PTY}aPTYlisten_check(ae;	r->m
	ifaINflag_dynamic_l1_down)r->m{TY_Nl
	ifappls_foundy_= 1)mmmmmmmmmmmm/* lastmapplico[0])ndo_s a capi releasl */	PPff_3PTY_E
	if(jy_rget_nDIN(a)))
-PPff_3PTY_E 0);
m	I&aIN_b3_[j - 1u;
SAP ENnDINi-commci-	= 0("fffENlard_p"LERT,_OAD, "\x01\xfd");3fffENlard_p"LERT,_CAI, "\x01\x80");3te e eard_p"LERT,_UID, "\x06\x43\x61\x70\x69\x32\x30");3te emrard_p"LERT,_SHIFT | 6, NULL);3te e eard_p"LERT,_SIN,;"\x02\x00\x00");3te e e_b3_i-;
	ernal\ormmci-	= REM_L1_SIG_ SSIGN_PEND;3te e esig_reqALERT,_ SSIGN, DSIG_ID);3te e	fard_p"LERT,_FTY,;"\x02\xff\x06");d/* l1 downl*/s	NMfoesig_reqALERT,_SIG_CTRL, 0fts	NMfoesend_reqA_b3_)("PTY	t}3PTY_}3PTY}aPTYT	ifC->AdvSignalAppl 		Ithis)swPP_3PTY_this->NullCREnD;
	m	Ifai] tatPTYT	ifC->AdvCodec		In)	PPff_3PTY_EPEND_remove"C->AdvCodec		In)t"frNffC->AdvCodec		Ini-t	&	= 0t"frNffC->AdvCodec		Ini-arv_n&	= 0t"frNf}3PTY_C->AdvSignalAppl = NULL("ffteC->AdvSignal		Inm	INULL("f	rNC->AdvCodecFLAG = 0t"frNfC->AdvCodec		Inm	INULL("f	r}ate}	Y}a
_this->Idm	I0f3ENbrw", IGOODPE}
ss		 ic;_masnPEND_remove_check(		In	*0);
	
TE;ummy!0);
)&brw", -tru PENT	if!0);
INNL.Id
B3_o_ind_mask_emptyfPEND	er-{s eT	ifPENDINSig.Idm== ((ffe
	te_b3_i-Sig.Idm= 0PENM
	if!_b3_i-Sig.Ide
-P{ate ].info + 1);
				PEND_remove_complete(%x)"A nDINi-Ide)Pr->md.info + 1);
				t	&=0x%x,Sig=0x%x",;_b3_i-t	&, _b3_i-Sig.Ide) fND;
	ifnDINi-Ide
-Pf_3PTY_CodecIdCheck(nDINi-Crplci , _b3_)t"frNfclear_b1_ordfigfnDIN)t"frNfncci_remove"LERT,_0,Ifai] );3te e_b3__free_msg_in_qu	ue"_b3_)("PTY	)
				&_flow_ord w;
_remove"_b3_)("PTY	nDINi-Id = 0t"frNf_b3_i-S		 em	IIDLE("PTY	nDINi-c
				&s = 0t"frNf_b3_i-applm	INULL("fff _b3_i-notifiedca 0 = 0t"frN}aPTYlisten_check(nDINi-Crplci )("PTYbrw", -tru PENe}	Y}a	brw", Ifai] ta}"	E/*------------------------------------------------------------------*/	Es		 ic;by   _b3__nl_busy(		In	*0);
	
TE;/* cnlyaapplico;
	macilnon-multiplex d 08) oco&sn*/	Tbrw", I(_b3_i-nl_reqENe|a (PENDINncci_r		/_list
ffrrrrB3_nDINi-Crplci G)ncci_ch[PENDINncci_r		/_list]
ffrrrrB3_XnDINi-Crplci G)ch_flow_ord w;
[nDINi-Crplci G)ncci_ch[PENDINncci_r		/_list]] & N_OK_FC_PENDING)	)ts}"	E/*------------------------------------------------------------------*/	/* DTMF facilit0es                                                  */s/*------------------------------------------------------------------*/	sEs		 ic;struur
TE;by  lsend_mask;3Pby  llisten_mask;3Pby  l)
	racci t"fby  lcodets} dtmf_digit_map[rm	
TE;{ 0x01, 0x01, 0x23enDTMFcDIGIT_TONE_		DE_HASHMARK },E;{ 0x01, 0x01, 0x2aenDTMFcDIGIT_TONE_		DE_STAR },E;{ 0x01, 0x01, 0x30enDTMFcDIGIT_TONE_		DE_0 },E;{ 0x01, 0x01, 0x31enDTMFcDIGIT_TONE_		DE_1 },E;{ 0x01, 0x01, 0x32enDTMFcDIGIT_TONE_		DE_2 },E;{ 0x01, 0x01, 0x33enDTMFcDIGIT_TONE_		DE_3 },E;{ 0x01, 0x01, 0x34enDTMFcDIGIT_TONE_		DE_4 },E;{ 0x01, 0x01, 0x35enDTMFcDIGIT_TONE_		DE_5 },E;{ 0x01, 0x01, 0x36enDTMFcDIGIT_TONE_		DE_6 },E;{ 0x01, 0x01, 0x37enDTMFcDIGIT_TONE_		DE_7 },E;{ 0x01, 0x01, 0x38enDTMFcDIGIT_TONE_		DE_8 },E;{ 0x01, 0x01, 0x39enDTMFcDIGIT_TONE_		DE_9 },E;{ 0x01, 0x01, 0x41enDTMFcDIGIT_TONE_		DE_A },E;{ 0x01, 0x01, 0x42enDTMFcDIGIT_TONE_		DE_B },E;{ 0x01, 0x01, 0x43enDTMFcDIGIT_TONE_		DE_C },E;{ 0x01, 0x01, 0x44enDTMFcDIGIT_TONE_		DE_D },E;{ 0x01, 0x00, 0x61enDTMFcDIGIT_TONE_		DE_A },E;{ 0x01, 0x00, 0x62enDTMFcDIGIT_TONE_		DE_B },E;{ 0x01, 0x00, 0x63enDTMFcDIGIT_TONE_		DE_C },E;{ 0x01, 0x00, 0x64enDTMFcDIGIT_TONE_		DE_D },EE;{ 0x04, 0x04, 0x80enDTMFcSIGNAL_NO_TONE },E;{ 0x00, 0x04, 0x81enDTMFcSIGNAL_UNIDENTIFIED_TONE },E;{ 0x04, 0x04, 0x82enDTMFcSIGNAL_DIAL_TONE },E;{ 0x04, 0x04, 0x83enDTMFcSIGNAL_PABX_INTERNAL_DIAL_TONE },E;{ 0x04, 0x04, 0x84enDTMFcSIGNAL_SPECIAL_DIAL_TONE },E;{ 0x04, 0x04, 0x85enDTMFcSIGNAL_SECOND_DIAL_TONE },E;{ 0x04, 0x04, 0x86enDTMFcSIGNAL_RINGINGnTONE },E;{ 0x04, 0x04, 0x87enDTMFcSIGNAL_SPECIAL_RINGINGnTONE },E;{ 0x04, 0x04, 0x88enDTMFcSIGNAL_BUSYnTONE },E;{ 0x04, 0x04, 0x89enDTMFcSIGNAL_CONGESnai[_TONE },E;{ 0x04, 0x04, 0x8aenDTMFcSIGNAL_SPECIAL_INapRMATai[_TONE },E;{ 0x04, 0x04, 0x8benDTMFcSIGNAL_COMapRT_TONE },E;{ 0x04, 0x04, 0x8cenDTMFcSIGNAL_HOLD_TONE },E;{ 0x04, 0x04, 0x8denDTMFcSIGNAL_RECORD_TONE },E;{ 0x04, 0x04, 0x8eenDTMFcSIGNAL_CALLER_WAITINGnTONE },E;{ 0x04, 0x04, 0x8fenDTMFcSIGNAL_CALL_WAITINGnTONE },E;{ 0x04, 0x04, 0x90enDTMFcSIGNAL_PAYnTONE },E;{ 0x04, 0x04, 0x91enDTMFcSIGNAL_POSITIVE_INDICATai[_TONE },E;{ 0x04, 0x04, 0x92enDTMFcSIGNAL_NEGATIVE_INDICATai[_TONE },E;{ 0x04, 0x04, 0x93enDTMFcSIGNAL_WARNINGnTONE },E;{ 0x04, 0x04, 0x94enDTMFcSIGNAL_INTRUSai[_TONE },E;{ 0x04, 0x04, 0x95enDTMFcSIGNAL_CALLINGnCARD_SERVICE_TONE },E;{ 0x04, 0x04, 0x96enDTMFcSIGNAL_PAYPHONE_RECOGNITai[_TONE },E;{ 0x04, 0x04, 0x97enDTMFcSIGNAL_CPE_ALERTINGnSIGNAL },E;{ 0x04, 0x04, 0x98enDTMFcSIGNAL_OFF_HOOK_WARNINGnTONE },E;{ 0x04, 0x04, 0xbfenDTMFcSIGNAL_INTERCEPT_TONE },E;{ 0x04, 0x04, 0xc0enDTMFcSIGNAL_MODEM_CALLINGnTONE },E;{ 0x04, 0x04, 0xc1enDTMFcSIGNAL_auX_CALLINGnTONE },E;{ 0x04, 0x04, 0xc2enDTMFcSIGNAL_ANSWER_TONE },E;{ 0x04, 0x04, 0xc3enDTMFcSIGNAL_REVERSED_ANSWER_TONE },E;{ 0x04, 0x04, 0xc4enDTMFcSIGNAL_ANSAM_TONE },E;{ 0x04, 0x04, 0xc5enDTMFcSIGNAL_REVERSED_ANSAM_TONE },E;{ 0x04, 0x04, 0xc6enDTMFcSIGNAL_BELL103_ANSWER_TONE },E;{ 0x04, 0x04, 0xc7enDTMFcSIGNAL_auX_FLAGS },E;{ 0x04, 0x04, 0xc8enDTMFcSIGNAL_G2_auX_GROUP_ID },E;{ 0x00, 0x04, 0xc9enDTMFcSIGNAL_HUMAN_SPEECH },E;{ 0x04, 0x04, 0xcaenDTMFcSIGNAL_ANSWERINGnMACHINE_390 },E;{ 0x02, 0x02, 0xf1enDTMFcMFcDIGIT_TONE_		DE_1 },E;{ 0x02, 0x02, 0xf2enDTMFcMFcDIGIT_TONE_		DE_2 },E;{ 0x02, 0x02, 0xf3enDTMFcMFcDIGIT_TONE_		DE_3 },E;{ 0x02, 0x02, 0xf4enDTMFcMFcDIGIT_TONE_		DE_4 },E;{ 0x02, 0x02, 0xf5enDTMFcMFcDIGIT_TONE_		DE_5 },E;{ 0x02, 0x02, 0xf6enDTMFcMFcDIGIT_TONE_		DE_6 },E;{ 0x02, 0x02, 0xf7enDTMFcMFcDIGIT_TONE_		DE_7 },E;{ 0x02, 0x02, 0xf8enDTMFcMFcDIGIT_TONE_		DE_8 },E;{ 0x02, 0x02, 0xf9enDTMFcMFcDIGIT_TONE_		DE_9 },E;{ 0x02, 0x02, 0xfaenDTMFcMFcDIGIT_TONE_		DE_0 },E;{ 0x02, 0x02, 0xfbenDTMFcMFcDIGIT_TONE_		DE_K1 },E;{ 0x02, 0x02, 0xfcenDTMFcMFcDIGIT_TONE_		DE_K2 },E;{ 0x02, 0x02, 0xfdenDTMFcMFcDIGIT_TONE_		DE_KP },E;{ 0x02, 0x02, 0xfeenDTMFcMFcDIGIT_TONE_		DE_S1 },E;{ 0x02, 0x02, 0xffenDTMFcMFcDIGIT_TONE_		DE_ST },EE};

#definenDTMFcDIGIT_MAP_ENTRIES ARRAYnSIZE(dtmf_digit_map)
sss		 ic;voidAdtmf_enD;
	_receiverA		In	*0);
,nby  lenD;
	_mask	
TE;_masnmin_digit_duro[0]),nmin_gap_duro[0])f3EN].info + 1);
				[%06lx] %s,%d:Adtmf_enD;
	_receiver %02x","PTY(d_mask(fnDINi-Idm<< 8) | UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTY()
	r *)(FILE_), __LINE__,lenD;
	_mask	)("ENTRIEenD;
	_mask != 0)r-{s emin_digit_duro[0]) 	IyVE_REQdtmf_rec_pulse_msm== (e ?;40m: PENDINdtmf_rec_pulse_ms;s emin_gap_duro[0]) 	IyVE_REQdtmf_rec_pause_msm== (e ?;40m: PENDINdtmf_rec_pause_msf3PTpb3_i-;
	ernal\req_buffer[0] 	IDTMFcULATAnREQUESn_ENABLd RECEIVER;EffPU		}
			&PENDIN;
	ernal\req_buffer[1],nmin_digit_duro[0]));EffPU		}
			&PENDIN;
	ernal\req_buffer[3],nmin_gap_duro[0]))("ffnDINi-NData[0].PLi = 1r	I5;	r->PU		}
			&PENDIN;
	ernal\req_buffer[5],nINTERNAL_IND_BUFFER_SIZE)("ffnDINi-NData[0].PLi = 1r+	I2t"fYcapidtmf_recv_enD;
	(&yVE_REQcapidtmf_sco[_),nmin_digit_duro[0]),nmin_gap_duro[0]))("fN}"	[i] Em{
lYpb3_i-;
	ernal\req_buffer[0] 	IDTMFcULATAnREQUESn_ciSABLd RECEIVER;EffnDINi-NData[0].PLi = 1r	I1;	r->capidtmf_recv_disD;
	(&yVE_REQcapidtmf_sco[_))("fN}"	nDINi-NData[0].Pm	IPENDIN;
	ernal\req_buffer;"	nDINi-NL.Xm	IPENDINNData("fnDINi-NL.ReqChr= 0("fnDINi-NL.Req = PENDINnl_reqr= (by  k N_ULATA;
fnDINi-Crplci G)requ		r(&nDINi-NLf("}
sss		 ic;voidAdtmf_send_]igitsA		In	*0);
,nby  l*digit_buffer,n_masndigit_count	
TE;_masnw, if3EN].info + 1);
				[%06lx] %s,%d:Adtmf_send_]igits %d","PTY(d_mask(fnDINi-Idm<< 8) | UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTY()
	r *)(FILE_), __LINE__,ldigit_count	)("fNpb3_i-;
	ernal\req_buffer[0] 	IDTMFcULATAnREQUESn_SEND_DIGITSf3Pw 	IyVE_REQdtmf_send_pulse_msm== (e ?;40m: PENDINdtmf_send_pulse_ms;rNPU		}
			&PENDIN;
	ernal\req_buffer[1],nw)f3Pw 	IyVE_REQdtmf_send_pause_msm== (e ?;40m: PENDINdtmf_send_pause_ms;rNPU		}
			&PENDIN;
	ernal\req_buffer[3],nw)f3Pacility_req"));
digit_count;dapter-{s ew = 0t"frwhilemy(w);
DTMFcDIGIT_MAP_ENTRIES)
f}       B3_Xdigit_buffer[i] != dtmf_digit_map[w].)
	racci ))
-P{ate w++;
SA}ENMPENDIN;
	ernal\req_buffer[5 + irm	I(w);
DTMFcDIGIT_MAP_ENTRIES) ?RMA	dtmf_digit_map[w].)odem:nDTMFcDIGIT_TONE_		DE_STAR;fN}"	nDINi-NData[0].PLi = 1r	I5 + digit_count;"	nDINi-NData[0].Pm	IPENDIN;
	ernal\req_buffer;"	nDINi-NL.Xm	IPENDINNData("fnDINi-NL.ReqChr= 0("fnDINi-NL.Req = PENDINnl_reqr= (by  k N_ULATA;
fnDINi-Crplci G)requ		r(&nDINi-NLf("}
sss		 ic;voidAdtmf_rec_clear_ordfigf		In	*0);
	
TEE;].info + 1);
				[%06lx] %s,%d:Adtmf_rec_clear_ordfig","PTY(d_mask(fnDINi-Idm<< 8) | UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTY()
	r *)(FILE_), __LINE__	)("fNpb3_i-dtmf_rec_activer= 0("fnDINi-dtmf_rec_pulse_msm= 0("fnDINi-dtmf_rec_pause_msm=I0f3ENcapidtmf_init(&yVE_REQcapidtmf_sco[_),nnDINi-Crplci G)u_law)("f}
sss		 ic;voidAdtmf_send_clear_ordfigf		In	*0);
	
TEE;].info + 1);
				[%06lx] %s,%d:Adtmf_send_clear_ordfig","PTY(d_mask(fnDINi-Idm<< 8) | UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTY()
	r *)(FILE_), __LINE__	)("fNpb3_i-dtmf_send_requ		rsm= 0("fnDINi-dtmf_send_pulse_msm= 0("fnDINi-dtmf_send_pause_msm=I0f3}
sss		 ic;voidAdtmf_preparI_switch(d_mas Id,l		In	*0);
	
TEE;].info + 1);
				[%06lx] %s,%d:Adtmf_preparI_switch","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__	)("fNwhilemypl3_i-dtmf_send_requ		rsm!= 0)r-	dtmf_ordfirmat0])(Id,lPENDf("}
sss		 ic;_masndtmf_save_codfigfd_mas Id,l		In	*0);
,nby  lRc	
TEE;].info + 1);
				[%06lx] %s,%d:Adtmf_save_codfig %02x %d","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc,nnDINi-Crjust_b_sco[_))("fNbrw", I(GOODf("}
sss		 ic;_masndtmf_r		rore_codfigfd_mas Id,l		In	*0);
,nby  lRc	
TE;_masnt=tuf3EN].info + 1);
				[%06lx] %s,%d:Adtmf_r		rore_codfig %02x %d","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc,nnDINi-Crjust_b_sco[_))("fNt=tum=IGOODPE;
	ifnDINi-B1_facilit0es & B1_FACILITY_DTMFRer-{s eswitch_XnDINi-Crjust_b_sco[_)
-P{atecasl ADJUSn_BnRESTORE_DTMF_1:
	te_b3_i-;
	ernal\ormmci-	= nDINi-Crjust_b_ormmci- fND;
	ifnDIN_nl_busy(PEND	er-Pf_3PTY_nDINi-Crjust_b_sco[_	= ADJUSn_BnRESTORE_DTMF_1("fff _REQ_t"frN}aPTYdtmf_enD;
	_receiverA0);
,npb3_i-dtmf_rec_activef("fffPENDi-Crjust_b_sco[_	= ADJUSn_BnRESTORE_DTMF_2t"fYY_REQ_t"frcasl ADJUSn_BnRESTORE_DTMF_2:fND;
	if(Rcm!= OK)rB3_XRcm!= OK_FC	er-Pf_3PTY_].info + 1);
				[%06lx] %s,%d:AReenD;
	 DTMF receiver fai
	d %02x","PTYPTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc));3PTY_t=tum=I_WRONG_STATE("fff _REQ_t"frN}aPTY_REQ_t"fr}	Y}a	brw", I(t=tuf("}
sss		 ic;voidAdtmf_ormmci-fd_mas Id,l		In	*0);
,nby  lRc	
TE;_masn;
	ernal\ormmci-,nt=tuf3fby  lmask;3Pby  lr		ult[4]("EN].info + 1);
				[%06lx] %s,%d:Adtmf_ormmci-	%02x %04x %04x %d %d %d %d","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc,nnDINi-;
	ernal\ormmci-,"fffPENDi-dtmf_omd,npb3_i-dtmf_rec_pulse_ms,npb3_i-dtmf_rec_pause_ms,"fffPENDi-dtmf_send_pulse_ms, PENDINdtmf_send_pause_ms))("fNt=tum=IGOODPE;r		ult[0] 	I2;rNPU		}
			&r		ult[1],nDTMFcSUCCESS)("PT
	ernal\ormmci-	= nDINi-T
	ernal\ormmci-;fNpb3_i-;
	ernal\commci-	= 0("fmask = ((01("fswitch_XnDINi-dtmf_omder-{s
rcasl DTMFcLISTEN_TONE_START:fNDmask <<	I1f3Pcasl DTMFcLISTEN_MFcSTART:fNDmask <<	I1f33Pcasl DTMFcLISTEN_START:fNDswitch_X;
	ernal\commci-)
-P{atedefault:fND;Crjust_b1_resource(Id,lPEND, NULL,_=_mask(nDINi-B1_facilit0es |"PTYPTYf}  B1_FACILITY_DTMFRe,nDTMFcCOMMAND_1)f3PTcasl DTMFcCOMMAND_1:fND;
	ifCrjust_b_process(Id,lPEND, Rc	m!= GOODfr-Pf_3PTY_].info + 1);
				[%06lx] %s,%d:ALoad DTMF fai
	d","PTYPTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTY_t=tum=I_FACILITY_NOn_SUPPORTED;RMm	-_REQ_t"frN}aPTY
	ifnDINi-;
	ernal\commci-)
-Ptebrw", t"fTcasl DTMFcCOMMAND_2:fND;
	ifnDIN_nl_busy(PEND	er-Pf_3PTY_nDINi-;
	ernal\commci-	= DTMFcCOMMAND_2;
-Ptebrw", t"fTN}aPTYnDINi-;
	ernal\commci-	= DTMFcCOMMAND_3t"ffTdtmf_enD;
	_receiverA0);
,n(by  kdpb3_i-dtmf_rec_activer| mask	)("Ptebrw", t"fTcasl DTMFcCOMMAND_3:fND;
	if(Rcm!= OK)rB3_XRcm!= OK_FC	er-Pf_3PTY_].info + 1);
				[%06lx] %s,%d:AEnD;
	 DTMF receiver fai
	d %02x","PTYPTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc));3PTY_t=tum=I_FACILITY_NOn_SUPPORTED;RMm	-_REQ_t"frN}aaPTYnDINi-tone_last_ nd	c	 0])_code = DTMFcSIGNAL_NO_TONE;aaPTYnDINi-dtmf_rec_activer|=lmask;3PTY_REQ_t"fr}	YY_REQ_t"s
rcasl DTMFcLISTEN_TONE_STOP:fNDmask <<	I1f3Pcasl DTMFcLISTEN_MFcSTOP:fNDmask <<	I1f33Pcasl DTMFcLISTEN_STOP:fNDswitch_X;
	ernal\commci-)
-P{atedefault:fND;nDINi-dtmf_rec_activer&= ~mask;3PTY
	ifnDINi-dtmf_rec_activefRMm	-_REQ_t"/*
  casl DTMFcCOMMAND_1:f  
	ifnDINi-dtmf_rec_activefR  {f  
	ifnDIN_nl_busy (PEND	er  {f  nDINi-;
	ernal\commci-	= DTMFcCOMMAND_1f3 &brw", t"  }f  nDINi-dtmf_rec_activer&= ~mask;3  nDINi-;
	ernal\commci-	= DTMFcCOMMAND_2;3  dtmf_enD;
	_receiver A0);
,nfai] );3 &brw", t"  }f  Rcm=IOK;
  casl DTMFcCOMMAND_2:f  
	if(Rcm!= OK)rB3_XRcm!= OK_FC	er  {f  ].in fo + 1);
				[%06lx] %s,%d:ADisD;
	 DTMF receiver fai
	d %02x","  UnMapId (Ide, ()
	r f	r *)(FILE_), __LINE__,lRc));3  t=tum=I_FACILITY_NOn_SUPPORTED;R  _REQ_t"  }f*/s	NMCrjust_b1_resource(Id,lPEND, NULL,_=_mask(nDINi-B1_facilit0es &"PTYPTYf}  ~(B1_FACILITY_DTMFXr| B1_FACILITY_DTMFRee,nDTMFcCOMMAND_3)f3PTcasl DTMFcCOMMAND_3:fND;
	ifCrjust_b_process(Id,lPEND, Rc	m!= GOODfr-Pf_3PTY_].info + 1);
				[%06lx] %s,%d:AUnload DTMF fai
	d","PTYPTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTY_t=tum=I_FACILITY_NOn_SUPPORTED;RMm	-_REQ_t"frN}aPTY
	ifnDINi-;
	ernal\commci-)
-Ptebrw", t"fTY_REQ_t"fr}	YY_REQ_t"s
rcasl DTMFcSEND_TONE:fNDmask <<	I1f3Pcasl DTMFcSEND_MF:fNDmask <<	I1f33Pcasl DTMFcDIGITScSEND:fNDswitch_X;
	ernal\commci-)
-P{atedefault:fND;Crjust_b1_resource(Id,lPEND, NULL,_=_mask(nDINi-B1_facilit0es |"PTYPTYf}  (fnDINi-dtmf_parame	er_li = 1m!= 0) ? B1_FACILITY_DTMFXr| B1_FACILITY_DTMFR : B1_FACILITY_DTMFXe),"PTYf}   DTMFcCOMMAND_1)f3PTcasl DTMFcCOMMAND_1:fND;
	ifCrjust_b_process(Id,lPEND, Rc	m!= GOODfr-Pf_3PTY_].info + 1);
				[%06lx] %s,%d:ALoad DTMF fai
	d","PTYPTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTY_t=tum=I_FACILITY_NOn_SUPPORTED;RMm	-_REQ_t"frN}aPTY
	ifnDINi-;
	ernal\commci-)
-Ptebrw", t"fTcasl DTMFcCOMMAND_2:fND;
	ifnDIN_nl_busy(PEND	er-Pf_3PTY_nDINi-;
	ernal\commci-	= DTMFcCOMMAND_2;
-Ptebrw", t"fTN}aPTYnDINi-dtmf_msg_number_qu	ue[ypl3_i-dtmf_send_requ		rs)++um	IPENDINnumber("fffPENDi-;
	ernal\commci-	= DTMFcCOMMAND_3t"ffTdtmf_send_]igitsAPEND, &nDINi-saved_msg._TEplc3]. =tu[1],nnDINi-saved_msg._TEplc3].li = 1)("Ptebrw", t"fTcasl DTMFcCOMMAND_3:fND;
	if(Rcm!= OK)rB3_XRcm!= OK_FC	er-Pf_3PTY_].info + 1);
				[%06lx] %s,%d:ASIddrDTMF ]igits fai
	d %02x","PTYPTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc));3PTY_
	ifnDINi-dtmf_send_requ		rsm!= 0)r-				ypl3_i-dtmf_send_requ		rs)--;3PTY_t=tum=I_FACILITY_NOn_SUPPORTED;RMm	-_REQ_t"frN}aPTYbrw", t"fT}	YY_REQ_t"T}	Ysendft_b3_ciCppl +_FACILITY_Rr| CONFIRM, tu & 0xffffL,IPENDINnumber,"P      "wws",nt=tu, SELECTOR_DTMF,lr		ult)ts}"	Es		 ic;by   dtmf_requ		r(d_mas Id,l_mas Number, DIVA_CAPI_ADAPTER *a,n		In	*0);
,nAPPL   *appl  API_PARSE *msg	
TE;_masnt=tuf3;_masnN,njPENby  lmask;3PAPI_PARSE dtmf_parplc5];3Pby  lr		ult[40]("EN].info + 1);
				[%06lx] %s,%d:Adtmf_requ		r","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__	)("fNt=tum=IGOODPE;r		ult[0] 	I2;rNPU		}
			&r		ult[1],nDTMFcSUCCESS)("PT	if!(aIN08)file.Global\Op[0])s & GL_DTMFcSUPPORTED	er-{s e].info + 1);
				[%06lx] %s,%d:AFacility n) &support d","PTYPUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTt=tum=I_FACILITY_NOn_SUPPORTED;RM}"	[i]  T	ifCpi_parse(&msg[1]. =tu[1],nmsg[1].li = 1, "w",ndtmf_parpl	er-{s e].info + 1);
				[%06lx] %s,%d:AWrong m_ssagl turmat","PTYPUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTt=tum=I_WRONG_MESSAGE_apRMAT;		}	"f[i]  
	if(GE		}
			dtmf_parplc0]. =tu)&== DTMFcGE		SUPPORTED_DETECT_		DES)
f} |a (GE		}
			dtmf_parplc0]. =tu)&== DTMFcGE		SUPPORTED_SEND_		DES)er-{s eT	if!((C->requ		red_op[0])s_tD;
	[appl 0tu - 1])
f}      3_X1Lm<< PRIVanE_DTMF_TONE)))
-P{ate ].info + 1);
				[%06lx] %s,%d:ADTMF unknownlrequ		r %04x","PTYPTUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lGE		}
			dtmf_parplc0]. =tu)));3PTYPU		}
			&r		ult[1],nDTMFcUNKNOWNnREQUESn)("PT}
>S[i] r--_3PTYacility_req"));
32f apte
-P	;r		ult[4 + irm	I0;3PTY
	ifGE		}
			dtmf_parplc0]. =tu)&== DTMFcGE		SUPPORTED_DETECT_		DES)
f}Y_3PTY_fcility_req"));
DTMFcDIGIT_MAP_ENTRIESf apte
-P	;_3PTY_f
	ifdtmf_digit_map[i].listen_maskm!= 0)r-				;r		ult[4 + fdtmf_digit_map[i].)
	racci  >> 3)]m|= d1m<< fdtmf_digit_map[i].)
	racci  & 0x7));3PTY_}3PTY}aPTY[i] r-fo_3PTY}fcility_req"));
DTMFcDIGIT_MAP_ENTRIESf apte
-P	;_3PTY_f
	ifdtmf_digit_map[i].send_maskm!= 0)r-				;r		ult[4 + fdtmf_digit_map[i].)
	racci  >> 3)]m|= d1m<< fdtmf_digit_map[i].)
	racci  & 0x7));3PTY_}3PTY}aPTYr		ult[0] 	I3 + 32;aPTYr		ult[3rm	I32;aPT}		}	"f[i]  
	if0);
&== NULL)r-{s e].info + 1);
				[%06lx] %s,%d:AWrong 		In","PTYPUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTt=tum=I_WRONG_IDENTIFIER;fN}"	[i] Em{
lY
	if!_b3_i-Sco[_
fommmm|a !0);
INNL.Id
|a PENDINn
_remove_ide
-P{ate ].info + 1);
				[%06lx] %s,%d:AWrong sco[_","PTYPTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYt=tum=I_WRONG_STATE("ff}
>S[i] r--_3PTYVE_REQcommci-	= 0("fffnDINi-dtmf_omdm=IGE		}
			dtmf_parplc0]. =tu)("fffmask = ((01("f eswitch_XnDINi-dtmf_omder-	-{s
r	rcasl DTMFcLISTEN_TONE_START:fNDrcasl DTMFcLISTEN_TONE_STOP:fNDNDmask <<	I1f3PDrcasl DTMFcLISTEN_MFcSTART:fNDPcasl DTMFcLISTEN_MFcSTOP:fNDNDmask <<	I1f3PDreT	if!((nDINi-requ		red_op[0])s_ordn a PENDINrequ		red_op[0])s a PENDINCrplci G)requ		red_op[0])s_tD;
	[appl 0tu - 1])
f}f}      3_X1Lm<< PRIVanE_DTMF_TONE)))
-P	;_3PTY_f].info + 1);
				[%06lx] %s,%d:ADTMF unknownlrequ		r %04x","PTYPTPTUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lGE		}
			dtmf_parplc0]. =tu)));3PTYTYPU		}
			&r		ult[1],nDTMFcUNKNOWNnREQUESn)("PTm	-_REQ_t"frNN}aaPTYcasl DTMFcLISTEN_START:fNDPcasl DTMFcLISTEN_STOP:fND	PT	if!(aINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_HARDDTMF)
f}f}    B3_!yaINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_SOFTDTMFcRECEIVE))
-P	;_3PTY_f].info + 1);
				[%06lx] %s,%d:AFacility n) &support d","PTYPYPTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYY_t=tum=I_FACILITY_NOn_SUPPORTED;RMm	--_REQ_t"frNN}aND	PT	ifmask & DTMFcLISTEN_ACTIVE_FLAGe
-P	;_3PTY_f
	ifCpi_parse(&msg[1]. =tu[1],nmsg[1].li = 1, "wwws",ndtmf_parpl	er--P	;_3PTY_ffnDINi-dtmf_rec_pulse_msm= 0("fTY_ffnDINi-dtmf_rec_pause_msm=I0f3	frNN}aND	PY[i] r-fo	;_3PTY_ffnDINi-dtmf_rec_pulse_msm= GE		}
			dtmf_parplc1]. =tu)("fff_ffnDINi-dtmf_rec_pause_msm=IGE		}
			dtmf_parplc2]. =tu)("fff_f}3PTY_}3PTY	scort_ n	ernal\commci-(Id,lPEND, dtmf_ormmci-)f3PTNlbrw", I(fai] );3aaPTYcasl DTMFcSEND_TONE:fNDNDmask <<	I1f3PDrcasl DTMFcSEND_MF:fNDNDmask <<	I1f3PDreT	if!((nDINi-requ		red_op[0])s_ordn a PENDINrequ		red_op[0])s a PENDINCrplci G)requ		red_op[0])s_tD;
	[appl 0tu - 1])
f}f}      3_X1Lm<< PRIVanE_DTMF_TONE)))
-P	;_3PTY_f].info + 1);
				[%06lx] %s,%d:ADTMF unknownlrequ		r %04x","PTYPTPTUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lGE		}
			dtmf_parplc0]. =tu)));3PTYTYPU		}
			&r		ult[1],nDTMFcUNKNOWNnREQUESn)("PTm	-_REQ_t"frNN}aaPTYcasl DTMFcDIGITScSEND:fND_f
	ifCpi_parse(&msg[1]. =tu[1],nmsg[1].li = 1, "wwws",ndtmf_parpl	er--P	_3PTY_f].info + 1);
				[%06lx] %s,%d:AWrong m_ssagl turmat","PTYPYPTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYY_t=tum=I_WRONG_MESSAGE_apRMAT;		m	--_REQ_t"frNN}aND	PT	ifmask & DTMFcLISTEN_ACTIVE_FLAGe
-P	;_3PTY_fnDINi-dtmf_send_pulse_msm= GE		}
			dtmf_parplc1]. =tu)("fff_fnDINi-dtmf_send_pause_msm=IGE		}
			dtmf_parplc2]. =tu)("fff_}aND	PTm=I0f3	frNjm=I0f3	frNwhilemy());
dtmf_parplc3].li = 1)rB3_Xj);
DTMFcDIGIT_MAP_ENTRIES)e
-P	;_3PTY_fjm=I0f3	frNNwhilemy(j);
DTMFcDIGIT_MAP_ENTRIES)3	frNN       B3_X(dtmf_parplc3]. =tu[im+a1rm!= dtmf_digit_map[j].)
	racci )"PTYPYPmmm|a (fdtmf_digit_map[j].send_maskm& mask	m== (e	er--P	;_3PTY_ffj++;
SArNN}aND	PYi++;
SArN}aND	PT	ifj&== DTMFcDIGIT_MAP_ENTRIES)3	frN_3PTY_f].info + 1);
				[%06lx] %s,%d:AIncorrect=DTMF ]igit %02x","PTYPTYTUnMapId(Ide, ()
	r *)(FILE_), __LINE__,ldtmf_parplc3]. =tu[i]));3PTYTYPU		}
			&r		ult[1],nDTMFcINCORRECT_DIGIT)("PTm	-_REQ_t"frNN}aND	PT	ifnDINi-dtmf_send_requ		rsm>= ARRAYnSIZE(nDINi-dtmf_msg_number_qu	ue))
-P	;_3PTY_f].info + 1);
				[%06lx] %s,%d:ADTMF requ		r overrun","PTYPYPTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYY_t=tum=I_WRONG_STATE("fff -_REQ_t"frNN}aND	PCpi_save_msg(dtmf_parpl, "wwws",n&nDINi-saved_msg);3te fscort_ n	ernal\commci-(Id,lPEND, dtmf_ormmci-)f3PTNlbrw", I(fai] );3a	tedefault:fND;f].info + 1);
				[%06lx] %s,%d:ADTMF unknownlrequ		r %04x","PTYPTPUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lnDINi-dtmf_omde)f3PTNlPU		}
			&r		ult[1],nDTMFcUNKNOWNnREQUESn)("PTm}ate}	Y}a	sendftCppl +_FACILITY_Rr| CONFIRM, tu & 0xffffL,INumber,"P      "wws",nt=tu, SELECTOR_DTMF,lr		ult)tslbrw", I(fai] );3}
sss		 ic;voidAdtmf_ordfirmat0])(d_mas Id,l		In	*0);
	
TE;_masnNtatby  lr		ult[4]("EN].info + 1);
				[%06lx] %s,%d:Adtmf_ordfirmat0])","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__	)("fNr		ult[0] 	I2;rNPU		}
			&r		ult[1],nDTMFcSUCCESS)("PT	ifnDINi-dtmf_send_requ		rsm!= 0)r-_3PTsendft_b3_ciCppl +_FACILITY_Rr| CONFIRM, tu & 0xffffL,IPENDINdtmf_msg_number_qu	ue[0],
ffrrrr  "wws",nGOOD, SELECTOR_DTMF,lr		ult)tsl	ypl3_i-dtmf_send_requ		rs)--;3PTfcility_req"));
pl3_i-dtmf_send_requ		rsf apte
-P	PENDINdtmf_msg_number_qu	ue[ium	IPENDINdtmf_msg_number_qu	ue[im+a1r;	Y}a}
sss		 ic;voidAdtmf_ nd	c	 0])fd_mas Id,l		In	*0);
,nby  l*msg,l_mas li = 1)
TE;_masnN,nj,m)f3EN].info + 1);
				[%06lx] %s,%d:Adtmf_ nd	c	 0])","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__	)("fN  = 0f3Efcility_r1q"));
li = 1;dapter-{s ejm=I0f3	fwhilemy(j);
DTMFcDIGIT_MAP_ENTRIES)3	f       B3_X(msg[i] != dtmf_digit_map[j].)odee
-P	mmm|a (fdtmf_digit_map[j].listen_maskm&npb3_i-dtmf_rec_activefm== (e	er--_3PTYj++;
SA}ENMT	ifj&;
DTMFcDIGIT_MAP_ENTRIES)3	f{	r->m
	iffdtmf_digit_map[j].listen_maskm&nDTMF_TONEcLISTEN_ACTIVE_FLAGe
-P	rrrrB3_XnDINi-tone_last_ nd	c	 0])_code == DTMFcSIGNAL_NO_TONEe
-P	rrrrB3_Xdtmf_digit_map[j].)
	racci  != DTMFcSIGNAL_UNIDENTIFIED_TONE	er-Pf_3PTY_
	ifnm+a1 == D	"ffoY_3PTY_Efcility_rli = 1;da > nm+a1;da--)"PTYPYPmsg[i] =nmsg[i - 1u;
SAP Eli = 1++;
SArNNi++;
SArN}aND	Pmsg[++n] 	IDTMFcSIGNAL_UNIDENTIFIED_TONEt"fTN}aPTYnDINi-tone_last_ nd	c	 0])_code = dtmf_digit_map[j].)
	racci ;3a	temsg[++n] 	Idtmf_digit_map[j].)
	racci ;3te}	Y}a	
	ifnm!= 0)r-{s emsg[0rm	I(by  )  t"fTsendft_b3_ciCppl +_FACILITY_I, tu & 0xffffL,I0, "wS", SELECTOR_DTMF,lmsg);3t}s}"	E/*------------------------------------------------------------------*/	/* DTMF parame	ers                                                  */s/*------------------------------------------------------------------*/	ss		 ic;voidAdtmf_parame	er_write(		In	*0);
	
TE;_masnNtatby  lparame	er_buffer[DTMFcPARAMETER_BUFFER_SIZEm+a2]("EN].info + 1);
				[%06lx] %s,%d:Adtmf_parame	er_write","PTY(d_mask(fnDINi-Idm<< 8) | UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTY()
	r *)(FILE_), __LINE__	)("fNparame	er_buffer[0um	IPENDINdtmf_parame	er_li = 1m+I1f3Pparame	er_buffer[1]m=IDSP_CTRLcSET_DTMFcPARAMETERSf3Efcility_req"));
pl3_i-dtmf_parame	er_li = 1f apte
-Pparame	er_buffer[2 + irm	Ipl3_i-dtmf_parame	er_buffer[i];"fard_p"LERT,_FTY,;parame	er_buffer);3tsig_reqALERT,_TEL_CTRL, 0fts	send_reqA_b3_)("}
sss		 ic;voidAdtmf_parame	er_clear_ordfigf		In	*0);
	
TEE;].info + 1);
				[%06lx] %s,%d:Adtmf_parame	er_clear_ordfig","PTY(d_mask(fnDINi-Idm<< 8) | UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTY()
	r *)(FILE_), __LINE__	)("fNpb3_i-dtmf_parame	er_li = 1m=I0f3}
sss		 ic;voidAdtmf_parame	er_preparI_switch(d_mas Id,l		In	*0);
	
TEE;].info + 1);
				[%06lx] %s,%d:Adtmf_parame	er_preparI_switch","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__	)("f}
sss		 ic;_masndtmf_parame	er_save_codfigfd_mas Id,l		In	*0);
,nby  lRc	
TEE;].info + 1);
				[%06lx] %s,%d:Adtmf_parame	er_save_codfig %02x %d","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc,nnDINi-Crjust_b_sco[_))("fNbrw", I(GOODf("}
sss		 ic;_masndtmf_parame	er_r		rore_codfigfd_mas Id,l		In	*0);
,nby  lRc	
TE;_masnt=tuf3EN].info + 1);
				[%06lx] %s,%d:Adtmf_parame	er_r		rore_codfig %02x %d","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc,nnDINi-Crjust_b_sco[_))("fNt=tum=IGOODPE;
	iffnDINi-B1_facilit0es & B1_FACILITY_DTMFRer-rrrrB3_XnDINi-dtmf_parame	er_li = 1m!= 0)er-{s eswitch_XnDINi-Crjust_b_sco[_)
-P{atecasl ADJUSn_BnRESTORE_DTMF_PARAMETER_1:
	te_b3_i-;
	ernal\ormmci-	= nDINi-Crjust_b_ormmci- fND;
	ifnDINi-sig_reqer-Pf_3PTY_nDINi-Crjust_b_sco[_	= ADJUSn_BnRESTORE_DTMF_PARAMETER_1;
SArN_REQ_t"frN}aPTYdtmf_parame	er_write(_b3_)("TY_nDINi-Crjust_b_sco[_	= ADJUSn_BnRESTORE_DTMF_PARAMETER_2t"fYY_REQ_t"frcasl ADJUSn_BnRESTORE_DTMF_PARAMETER_2:fND;
	if(Rcm!= OK)rB3_XRcm!= OK_FC	er-Pf_3PTY_].info + 1);
				[%06lx] %s,%d:ARe	rore DTMF parame	ers fai
	d %02x","PTYPTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc));3PTY_t=tum=I_WRONG_STATE("fff _REQ_t"frN}aPTY_REQ_t"fr}	Y}a	brw", I(t=tuf("}
ss/*------------------------------------------------------------------*/	/* Linen;
	erordnect=facilit0es                                     */s/*------------------------------------------------------------------*/	sELI_CONFIG   *li_codfig_tD;
	;	_masnli_total\o
				&s;
ss/*------------------------------------------------------------------*/	/* translo[_	a CHI  =turmat0]) elemIdt to a c
				& number	         */s/*&brw", s 0xff - any c
				&                                       */s/*         0xfe - chi wrong coding                                  */s/*         0xfu - D-ch				&                                         */s/*         0x00 - no c
				&                                        */s/*         [i]  c
				& number	/ PRI: timeslot                      */s/* 
	ic
				&s is 08)vid_d w laccept mrre t
		 on =c
				&.         */s/*------------------------------------------------------------------*/	ss		 ic;by   chi_to\o
				&(by  l*chi + _masn*po
				&map)
{"PT
	 p;"PT
	 Ntat _masnmaptatby  lexcltatby  lofs;3Pby  l)
("ENTRIEpo
				&map)n*po
				&map =I0("f
	if!chi[0u	 brw", I0xff("fexclm=I0f3ENT	ifohi[1] & 0x20)m{ateT	ifohi[0um	=a1 B3_ohi[1] == ((ac	 brw", I0xfd;d/* exclusiverd-ch				& */s	Nfcility_r1q"));
ohi[0umB3_!yohi[i] & 0x80)P apte;ateT	if
&== ohi[0um|a !yohi[i] & 0x80)	 brw", I0xfe;ateT	iffohi[1] | 0xc8	m!= 0xe9	 brw", I0xfe;ateT	ifohi[1] & 0x08	mexclm=I0x40;E
ff/* 
nt. id 08esIdt */s	N
	ifohi[1] & 0x40)m{ate	p =Iim+I1f3P	Nfcility_rpq"));
ohi[0umB3_!yohi[i] & 0x80)P apte;ateeT	if
&== ohi[0um|a !yohi[i] & 0x80)	 brw", I0xfe;ate}E
ff/* coding s		ndard,INumber/Map, C
				& Typl */	PPp =Iim+I1f3P	fcility_rpq"));
ohi[0umB3_!yohi[i] & 0x80)P apte;ateT	if
&== ohi[0um|a !yohi[i] & 0x80)	 brw", I0xfe;ateT	iffohi[p] | 0xd0	m!= 0xd3	 brw", I0xfe;a
ff/* Number/Map */s	N
	ifohi[p] & 0x10)m{aatee/* map */s	NeT	iffohi[0um- pfm== 4)lofsm=I0f3	fr[i]  
	if(ohi[0um- pfm== 3)lofsm=I1f3	fr[i]  brw", I0xfe;ate	chm=I0f3	frmap =I0("fPTfcility_req"));
4rB3_n);
ohi[0ut apte _3PTY_n++;
SArNc1r+	I8;
SArNmap <<	I8f3PDreT	ifohi[p]e _3PTY_Tfcilichm=I0f !yohi[p] & d1m<< ch)); chpte;ateerNmap |= ohi[p];
SArN}aND	}
ArNc1r+	Iofs;3PrNmap <<	Iofs;3Pr}
>S[i] m{aatee/* number	*/s	Nep =Iim+I1f3P	Nchm=Iohi[p] & 0x3f fND;
	ifno
				&map)n_3PTY_
	if(by  kdohi[0um- pfm> 30	 brw", I0xfe;atefrmap =I0("fPT	fcility_rpq"));=
ohi[0ut apte _3PTY_eT	iffohi[i] & 0x7ffm> 31	 brw", I0xfe;atefrNmap |= X1Lm<< fohi[i] & 0x7ff);3PTY_}3PTY}aPTY[i] n_3PTY_
	ifpm!= chi[0u	 brw", I0xfef3PDreT	ifohm> 31	 brw", I0xfe;atefrmap =IX1Lm<< ch)("PTm}ateN
	ifohi[p] & 0x40	 brw", I0xfe;ate}ENMT	ifpo
				&map)n*po
				&map =Imaptatr[i]  
	ifmap != ((d_mask(1Lm<< ch))	 brw", I0xfe;atebrw", I(by  kdexclm| ch)("P}"	[i]  {mm/* n) &PRI */s	Nfcility_r1q"));
ohi[0umB3_!yohi[i] & 0x80)P apte;ateT	if
&!= ohi[0um|a !yohi[i] & 0x80)	 brw", I0xfe;ateT	ifohi[1] & 0x08	mexclm=I0x40;E
ffswitch_Xohi[1] | 0x98e _3PTcasl 0x98: brw", I0;3PTcasl 0x99:fND;
	ifno
				&map)n*po
				&map =I2;aPTYr	w", Iexclm| 1;3PTcasl 0x9a:fND;
	ifno
				&map)n*po
				&map =I4;aPTYr	w", Iexclm| 2;3PTcasl 0x9b: brw", I0xff("fTcasl 0x9c: brw", I0xfd;d/* d-ch */s	Ndefault: brw", I0xfe;ate}EN}a}
sss		 ic;voidAmixer_set_b)
				&_id_escA		In	*0);
,nby  lb)
				&_id)
{"PDIVA_CAPI_ADAPTER *a("NP	In	*s0);
;3tby  lold_id;E
fa	= nDINi-Crplci t
	old_id	= nDINi-li_b)
				&_id;
>
	ifC->li_1);er-{s eT	if(old_id	!= 0) B3_Xli_codfig_tD;
	[C->li_basl + (old_id	- 1)].0);
&== PEND	er-Pfli_codfig_tD;
	[C->li_basl + (old_id	- 1)].0);
&=INULL("ffnDINi-li_b)
				&_idm	I(b)
				&_idm& 0x1f)m+I1f3P	T	ifli_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&== NULL)r-Pfli_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&= 0);
;3t}"	[i] Em{
lY
	if((b)
				&_idm& 0x03)y_= 1)m|a (fb)
				&_idm& 0x03)y_= 2	er--_3PTYT	if(old_id	!= 0) B3_Xli_codfig_tD;
	[C->li_basl + (old_id	- 1)].0);
&== PEND	er-Pffli_codfig_tD;
	[C->li_basl + (old_id	- 1)].0);
&=INULL("fffnDINi-li_b)
				&_idm	Ib)
				&_idm& 0x03 fND;
	if(C->AdvSignal		Inm!= NULL) B3_XC->AdvSignal		Inm!= PEND	 B3_XC->AdvSignal		Ini-t	&	== ADV_VOICE	er-Pf_3PTY_s_b3_m	IC->AdvSignal		Inf3PDreT	ifli_codfig_tD;
	[C->li_basl + (2m- pDINi-li_b)
				&_id)].0);
&== NULL)r-Pff_3PTY_E
	if(snDINi-li_b)
				&_idm!= 0)r-				    B3_Xli_codfig_tD;
	[C->li_basl + (snDINi-li_b)
				&_idm- 1)].0);
&== sPEND	er-Pff;_3PTY_ffli_codfig_tD;
	[C->li_basl + (snDINi-li_b)
				&_idm- 1)].0);
&=INULL("fff f}aND	PYsnDINi-li_b)
				&_idm	I3 - pDINi-li_b)
				&_id;
SAP Eli_codfig_tD;
	[C->li_basl + (2m- pDINi-li_b)
				&_id)].0);
&=IsPEND;3teTY_].info + 1);
				[%06lx] %s,%d:Aarv_voice_set_b)
				&_id_esc %d","PTYYYYY(d_mask(fsnDINi-Idm<< 8) | UnMapCrd w;
lerXsnDINi-Crplci G)Ide),"PTYYYYY()
	r *)(FILE_), __LINE__,lsnDINi-li_b)
				&_idf);3PTY_}3PTY}aPTYT	ifli_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&== NULL)r-Pffli_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&= 0);
;3te}	Y}a	
	if(old_id	== 0) B3_XnDINi-li_b)
				&_idm!= 0)r-    B3_Xli_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&== PEND	er-{s emixer_clear_ordfigf_b3_)("T}EN].info + 1);
				[%06lx] %s,%d:Amixer_set_b)
				&_id_esc %d %d","PTY(d_mask(fnDINi-Idm<< 8) | UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTY()
	r *)(FILE_), __LINE__,lb)
				&_id, pDINi-li_b)
				&_id))("}
sss		 ic;voidAmixer_set_b)
				&_idA		In	*0);
,nby  l*ohi)
{"PDIVA_CAPI_ADAPTER *a("NP	In	*s0);
;3tby  lch,lold_id;E
fa	= nDINi-Crplci t
	old_id	= nDINi-li_b)
				&_id;
>chm=Iohi_to\o
				&(chi +NULL);3tT	if!(chm& 0x80)	r-{s eT	ifC->li_1);er--_3PTYT	if(old_id	!= 0) B3_Xli_codfig_tD;
	[C->li_basl + (old_id	- 1)].0);
&== PEND	er-Pffli_codfig_tD;
	[C->li_basl + (old_id	- 1)].0);
&=INULL("fffnDINi-li_b)
				&_idm	I(chm& 0x1f)m+I1f3P	YT	ifli_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&== NULL)r-Pffli_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&= 0);
;3te}	YS[i] r--_3PTY
	if((chm& 0x1f)m_= 1)m|a (fchm& 0x1f)m_= 2	er-Pf_3PTY_
	if(old_id	!= 0) B3_Xli_codfig_tD;
	[C->li_basl + (old_id	- 1)].0);
&== PEND	er-Pfffli_codfig_tD;
	[C->li_basl + (old_id	- 1)].0);
&=INULL("ffffnDINi-li_b)
				&_idm	Ichm& 0x1ff3PDreT	if(C->AdvSignal		Inm!= NULL) B3_XC->AdvSignal		Inm!= PEND	 B3_XC->AdvSignal		Ini-t	&	== ADV_VOICE	er-Pf;_3PTY_fs_b3_m	IC->AdvSignal		Inf3PDreeT	ifli_codfig_tD;
	[C->li_basl + (2m- pDINi-li_b)
				&_id)].0);
&== NULL)r-Pffv_3PTY_E T	if(snDINi-li_b)
				&_idm!= 0)r-					    B3_Xli_codfig_tD;
	[C->li_basl + (snDINi-li_b)
				&_idm- 1)].0);
&== sPEND	er-Pff;m_3PTY_E fli_codfig_tD;
	[C->li_basl + (snDINi-li_b)
				&_idm- 1)].0);
&=INULL("fff f }
te_NlNsnDINi-li_b)
				&_idm	I3 - pDINi-li_b)
				&_id;
SAP EEli_codfig_tD;
	[C->li_basl + (2m- pDINi-li_b)
				&_id)].0);
&=IsPEND;3teTY__].info + 1);
				[%06lx] %s,%d:Aarv_voice_set_b)
				&_id %d","PTYYYYYY(d_mask(fsnDINi-Idm<< 8) | UnMapCrd w;
lerXsnDINi-Crplci G)Ide),"PTYYYYYY()
	r *)(FILE_), __LINE__,lsnDINi-li_b)
				&_idf);3PTY_f}3PTY_}3PTY	T	ifli_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&== NULL)r-Pfffli_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&= 0);
;3tem}ate}	Y}a	
	if(old_id	== 0) B3_XnDINi-li_b)
				&_idm!= 0)r-    B3_Xli_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&== PEND	er-{s emixer_clear_ordfigf_b3_)("T}EN].info + 1);
				[%06lx] %s,%d:Amixer_set_b)
				&_id %02x %d","PTY(d_mask(fnDINi-Idm<< 8) | UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTY()
	r *)(FILE_), __LINE__,lch,lpDINi-li_b)
				&_id))("}
ss#definenMIXER_MAX_DUMP_CHANNELS 34sss		 ic;voidAmixer_calculo[__orefs(DIVA_CAPI_ADAPTER *a)
{"Ps		 ic;)
	r hex_digit_tD;
	[0x10rm	I{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};E;_masnn,nN,njPEN)
	r *pPEN)
	r hex_line[2 *nMIXER_MAX_DUMP_CHANNELS +nMIXER_MAX_DUMP_CHANNELS / 8 +n4]("EN].info + 1);
				[%06lx] %s,%d:Amixer_calculo[__orefs","PTY(d_mask(UnMapCrd w;
lerXaG)Ide), ()
	r *)(FILE_), __LINE__	)("fNfcility_req"));
li_total\o
				&s;dapter-{s eli_codfig_tD;
	[i].)
				& &= LI_CHANNEL_ADDRESSEScSETf3P	T	ifli_codfig_tD;
	[i].)
flagsm!= 0)r-		li_codfig_tD;
	[i].)
				& |= LI_CHANNEL_INVOLVED;RMm[i] r--_3PTYacilijm=I0f j);
li_total\o
				&s;djpte
-P	_3PTY_
	if(fli_codfig_tD;
	[i].flag_tD;
	[j])m!= 0)r-			    |a (fli_codfig_tD;
	[j].flag_tD;
	[i])m!= 0)er-Pf;_3PTY_fli_codfig_tD;
	[i].)
				& |= LI_CHANNEL_INVOLVED;RMmY_}3PTY	T	if(fli_codfig_tD;
	[i].flag_tD;
	[j]m& LI_FLAG_CONFERENCE	m!= 0)r-			    |a (fli_codfig_tD;
	[j].flag_tD;
	[i]m& LI_FLAG_CONFERENCE	m!= 0)er-Pf;_3PTY_fli_codfig_tD;
	[i].)
				& |= LI_CHANNEL_CONFERENCE;3PTY_}3PTY}aPT}	Y}a	fcility_req"));
li_total\o
				&s;dapter-{s eacilijm=I0f j);
li_total\o
				&s;djpte
-P{r-		li_codfig_tD;
	[i].)ref_tD;
	[j]m&= ~(LI_COEF_CH_CH | LI_COEF_CH_PC | LI_COEF_PC_CH | LI_COEF_PC_PC)f3P	YT	ifli_codfig_tD;
	[i].flag_tD;
	[j]m& LI_FLAG_CONFERENCE	r-Pffli_codfig_tD;
	[i].)ref_tD;
	[j]m|= LI_COEF_CH_CH;aPT}	Y}a	fcili  = 0f n);
li_total\o
				&s;dnpter-{s eT	ifli_codfig_tD;
	[n].)
				& & LI_CHANNEL_CONFERENCE)r--_3PTYacility_req"));
li_total\o
				&s;dapter-P	_3PTY_
	ifli_codfig_tD;
	[i].)
				& & LI_CHANNEL_CONFERENCE)r--oY_3PTY_Efcilijm=I0f j);
li_total\o
				&s;djpte
-P	f;_3PTY_ffli_codfig_tD;
	[i].)ref_tD;
	[j]m|=3PTY_E fli_codfig_tD;
	[i].)ref_tD;
	[n]m& li_codfig_tD;
	[n].)ref_tD;
	[j];3PTY_f}3PTY_}3PTY}aPT}	Y}a	fcility_req"));
li_total\o
				&s;dapter-{s e
	ifli_codfig_tD;
	[i].)
				& & LI_CHANNEL_INVOLVEDe
-P{r-		li_codfig_tD;
	[i].)ref_tD;
	[i]m&= ~LI_COEF_CH_CH;aPTYacilijm=I0f j);
li_total\o
				&s;djpte
-P	_3PTY_
	ifli_codfig_tD;
	[i].)ref_tD;
	[j]m& LI_COEF_CH_CH)r-Pfffli_codfig_tD;
	[i].flag_tD;
	[j]m|= LI_FLAG_CONFERENCE("PTm}ateN
	ifli_codfig_tD;
	[i].flag_tD;
	[i]m& LI_FLAG_CONFERENCE	r-Pffli_codfig_tD;
	[i].)ref_tD;
	[i]m|= LI_COEF_CH_CH;aPT}	Y}a	fcility_req"));
li_total\o
				&s;dapter-{s e
	ifli_codfig_tD;
	[i].)
				& & LI_CHANNEL_INVOLVEDe
-P{r-		acilijm=I0f j);
li_total\o
				&s;djpte
-P	_3PTY_
	ifli_codfig_tD;
	[i].flag_tD;
	[j]m& LI_FLAG_INTERCONNECT)r-Pfffli_codfig_tD;
	[i].)ref_tD;
	[j]m|= LI_COEF_CH_CH;aPTY_
	ifli_codfig_tD;
	[i].flag_tD;
	[j]m& LI_FLAG_MONITOR)r-Pfffli_codfig_tD;
	[i].)ref_tD;
	[j]m|= LI_COEF_CH_PC;aPTY_
	ifli_codfig_tD;
	[i].flag_tD;
	[j]m& LI_FLAG_MIX)r-Pfffli_codfig_tD;
	[i].)ref_tD;
	[j]m|= LI_COEF_PC_CH;aPTY_
	ifli_codfig_tD;
	[i].flag_tD;
	[j]m& LI_FLAG_PCCONNECT)r-Pfffli_codfig_tD;
	[i].)ref_tD;
	[j]m|= LI_COEF_PC_PC("PTm}ateN
	ifli_codfig_tD;
	[i].)
flagsm& LI_CHFLAG_MONITOR)r-Pf_3PTY_fcilijm=I0f j);
li_total\o
				&s;djpte
-P	f_3PTY_E
	ifli_codfig_tD;
	[i].flag_tD;
	[j]m& LI_FLAG_INTERCONNECT)r-Pfff_3PTY_ffli_codfig_tD;
	[i].)ref_tD;
	[j]m|= LI_COEF_CH_PC;aPTY__E
	ifli_codfig_tD;
	[j].)
flagsm& LI_CHFLAG_MIX)r-Pfffffli_codfig_tD;
	[i].)ref_tD;
	[j]m|= LI_COEF_PC_CH | LI_COEF_PC_PC;3PTY_f}3PTY_}3PTY}aPTN
	ifli_codfig_tD;
	[i].)
flagsm& LI_CHFLAG_MIX)r-Pf_3PTY_fcilijm=I0f j);
li_total\o
				&s;djpte
-P	f_3PTY_E
	ifli_codfig_tD;
	[j].flag_tD;
	[i]m& LI_FLAG_INTERCONNECT)r-Pffffli_codfig_tD;
	[j].)ref_tD;
	[i]m|= LI_COEF_PC_CH;aPTY_}3PTY}aPTN
	ifli_codfig_tD;
	[i].)
flagsm& LI_CHFLAG_LOOP)r-Pf_3PTY_fcilijm=I0f j);
li_total\o
				&s;djpte
-P	f_3PTY_E
	ifli_codfig_tD;
	[i].flag_tD;
	[j]m& LI_FLAG_INTERCONNECT)r-Pfff_3PTY_fffcili  = 0f n);
li_total\o
				&s;dnpter--Pfff_3PTY_ffeT	ifli_codfig_tD;
	[n].flag_tD;
	[i]m& LI_FLAG_INTERCONNECT)r-Pfffff_3PTY_ffefli_codfig_tD;
	[n].)ref_tD;
	[j]m|= LI_COEF_CH_CH;aPTY_Y__E
	ifli_codfig_tD;
	[j].)
flagsm& LI_CHFLAG_MIX)r-Pffffff_3PTY_ffeffli_codfig_tD;
	[n].)ref_tD;
	[j]m|= LI_COEF_PC_CH;aPTY_Y_ffeT	ifli_codfig_tD;
	[n].)
flagsm& LI_CHFLAG_MONITOR)r-PfTY_ffefli_codfig_tD;
	[n].)ref_tD;
	[j]m|= LI_COEF_CH_PC | LI_COEF_PC_PC;aPTY__Ef }
te_NlNtr[i]  
	ifli_codfig_tD;
	[n].)
flagsm& LI_CHFLAG_MONITOR)r-PfTY_ffeli_codfig_tD;
	[n].)ref_tD;
	[j]m|= LI_COEF_CH_PC;aPTY__Ef}
te_NlN}3PTY_f}3PTY_}3PTY}aPT}	Y}a	fcility_req"));
li_total\o
				&s;dapter-{s e
	ifli_codfig_tD;
	[i].)
				& & LI_CHANNEL_INVOLVEDe
-P{r-		
	ifli_codfig_tD;
	[i].)
flagsm& (LI_CHFLAG_MONITOR | LI_CHFLAG_MIX | LI_CHFLAG_LOOP)	r-Pffli_codfig_tD;
	[i].)
				& |= LI_CHANNEL_ACTIVEf3P	YT	ifli_codfig_tD;
	[i].)
flagsm& LI_CHFLAG_MONITOR)r-PfTli_codfig_tD;
	[i].)
				& |= LI_CHANNEL_RX_LATA;
fTN
	ifli_codfig_tD;
	[i].)
flagsm& LI_CHFLAG_MIX)r-PfTli_codfig_tD;
	[i].)
				& |= LI_CHANNEL_TX_LATA;
fTNacilijm=I0f j);
li_total\o
				&s;djpte
-P	_3PTY_
	if(li_codfig_tD;
	[i].flag_tD;
	[j]m&r-			     (LI_FLAG_INTERCONNECT | LI_FLAG_PCCONNECT | LI_FLAG_CONFERENCE | LI_FLAG_MONITOR))r-			    |a (li_codfig_tD;
	[j].flag_tD;
	[i]m&3PTY_f(LI_FLAG_INTERCONNECT | LI_FLAG_PCCONNECT | LI_FLAG_CONFERENCE | LI_FLAG_ANNOUNCEMENT | LI_FLAG_MIX))er-Pf;_3PTY_fli_codfig_tD;
	[i].)
				& |= LI_CHANNEL_ACTIVEf3P	Y_}3PTY	T	ifli_codfig_tD;
	[i].flag_tD;
	[j]m& (LI_FLAG_PCCONNECT | LI_FLAG_MONITOR))r-			Tli_codfig_tD;
	[i].)
				& |= LI_CHANNEL_RX_LATA;
fTNE
	ifli_codfig_tD;
	[j].flag_tD;
	[i]m& (LI_FLAG_PCCONNECT | LI_FLAG_ANNOUNCEMENT | LI_FLAG_MIX))r-			Tli_codfig_tD;
	[i].)
				& |= LI_CHANNEL_TX_LATA;
fTN}aPTN
	if!fli_codfig_tD;
	[i].)
				& & LI_CHANNEL_ACTIVE	er-Pf_3PTY_li_codfig_tD;
	[i].)ref_tD;
	[i]m|= LI_COEF_PC_CH | LI_COEF_CH_PC;aPTY_li_codfig_tD;
	[i].)
				& |= LI_CHANNEL_TX_LATA | LI_CHANNEL_RX_LATA;
fTN}aPT}	Y}a	fcility_req"));
li_total\o
				&s;dapter-{s e
	ifli_codfig_tD;
	[i].)
				& & LI_CHANNEL_INVOLVEDe
-P{r-		jm=I0f3	frwhilemy(j);
li_total\o
				&s)mB3_!yli_codfig_tD;
	[i].flag_tD;
	[j]m& LI_FLAG_ANNOUNCEMENT))r-			j++;
SArT	ifj&;
li_total\o
				&s)r-Pf_3PTY_fcilijm=I0f j);
li_total\o
				&s;djpte
-P	f_3PTY_Eli_codfig_tD;
	[i].)ref_tD;
	[j]m&= ~(LI_COEF_CH_CH | LI_COEF_PC_CH);3PTY_f
	ifli_codfig_tD;
	[i].flag_tD;
	[j]m& LI_FLAG_ANNOUNCEMENT)3PTY_ffli_codfig_tD;
	[i].)ref_tD;
	[j]m|= LI_COEF_PC_CH;aPTY_}3PTY}aPT}	Y}a	  = li_total\o
				&s;a	
	ifnm>nMIXER_MAX_DUMP_CHANNELS)3PT  = MIXER_MAX_DUMP_CHANNELS;a	p =Ihex_line;
_fcilijm=I0f j);
n;djpte
-{
lY
	if(j & 0x7)	== 0)3PTY*(ppte = ' ';aPT*(ppte = hex_digit_tD;
	[li_codfig_tD;
	[j].)urchnl >> 4];aPT*(ppte = hex_digit_tD;
	[li_codfig_tD;
	[j].)urchnl & 0xfr;	Y}a	*p =I'\0';aP].info + 1);
				[%06lx] CURRENT %s","PTY(d_mask(UnMapCrd w;
lerXaG)Ide), ()
	r *)hex_linef);3Pp =Ihex_line;
_fcilijm=I0f j);
n;djpte
-{
lY
	if(j & 0x7)	== 0)3PTY*(ppte = ' ';aPT*(ppte = hex_digit_tD;
	[li_codfig_tD;
	[j].)
				& >> 4];aPT*(ppte = hex_digit_tD;
	[li_codfig_tD;
	[j].)
				& & 0xfr;	Y}a	*p =I'\0';aP].info + 1);
				[%06lx] CHANNEL %s","PTY(d_mask(UnMapCrd w;
lerXaG)Ide), ()
	r *)hex_linef);3Pp =Ihex_line;
_fcilijm=I0f j);
n;djpte
-{
lY
	if(j & 0x7)	== 0)3PTY*(ppte = ' ';aPT*(ppte = hex_digit_tD;
	[li_codfig_tD;
	[j].)
flagsm>> 4];aPT*(ppte = hex_digit_tD;
	[li_codfig_tD;
	[j].)
flagsm& 0xfr;	Y}a	*p =I'\0';aP].info + 1);
				[%06lx] CHFLAG  %s","PTY(d_mask(UnMapCrd w;
lerXaG)Ide), ()
	r *)hex_linef);3Pfcility_req"));
n;dapter-{s ep =Ihex_line;
__fcilijm=I0f j);
n;djpte
--_3PTY
	if(j & 0x7)	== 0)3PTYY*(ppte = ' ';aPTT*(ppte = hex_digit_tD;
	[li_codfig_tD;
	[i].flag_tD;
	[j]m>> 4];aPTT*(ppte = hex_digit_tD;
	[li_codfig_tD;
	[i].flag_tD;
	[j]m& 0xfr;	YY}aPT*p =I'\0';aPP].info + 1);
				[%06lx] FLAG[%02x]%s","PTYY(d_mask(UnMapCrd w;
lerXaG)Ide), i, ()
	r *)hex_linef);3P}a	fcility_req"));
n;dapter-{s ep =Ihex_line;
__fcilijm=I0f j);
n;djpte
--_3PTY
	if(j & 0x7)	== 0)3PTYY*(ppte = ' ';aPTT*(ppte = hex_digit_tD;
	[li_codfig_tD;
	[i].)ref_tD;
	[j]m>> 4];aPTT*(ppte = hex_digit_tD;
	[li_codfig_tD;
	[i].)ref_tD;
	[j]m& 0xfr;	YY}aPT*p =I'\0';aPP].info + 1);
				[%06lx] COEF[%02x]%s","PTYY(d_mask(UnMapCrd w;
lerXaG)Ide), i, ()
	r *)hex_linef);3P}a}
sss		 ic;struct
{"Pby  lmask;3Pby  lline_flags;
}Amixer_write_prog_1);[] =
{"P{ LI_COEF_CH_CH, 0 },"P{ LI_COEF_CH_PC, MIXER_COEF_LINE_TO_PC_FLAG },"P{ LI_COEF_PC_CH, MIXER_COEF_LINE_FROM_PC_FLAG },"P{ LI_COEF_PC_PC, MIXER_COEF_LINE_TO_PC_FLAG | MIXER_COEF_LINE_FROM_PC_FLAG }
};sss		 ic;struct
{"Pby  lfrom_)
("Pby  lto\o
;"Pby  lmask;3Pby  lxordnect_override;
}Amixer_write_prog_b);[] =
{"P{ 0,I0, LI_COEF_CH_CH, 0x01 },mm/* B      to B      */s	{ 1,I0, LI_COEF_CH_CH, 0x01 },mm/* Alt B  to B      */s	{ 0,I0, LI_COEF_PC_CH, 0x80 },mm/* PC     to B      */s	{ 1,I0, LI_COEF_PC_CH, 0x01 },mm/* Alt PC to B      */s	{ 2,I0, LI_COEF_CH_CH, 0x00 },mm/* IC     to B      */s	{ 3,I0, LI_COEF_CH_CH, 0x00 },mm/* Alt IC to B      */s	{ 0,I0, LI_COEF_CH_PC, 0x80 },mm/* B      to PC     */s	{ 1,I0, LI_COEF_CH_PC, 0x01 },mm/* Alt B  to PC     */s	{ 0,I0, LI_COEF_PC_PC, 0x01 },mm/* PC     to PC     */s	{ 1,I0, LI_COEF_PC_PC, 0x01 },mm/* Alt PC to PC     */s	{ 2,I0, LI_COEF_CH_PC, 0x00 },mm/* IC     to PC     */s	{ 3,I0, LI_COEF_CH_PC, 0x00 },mm/* Alt IC to PC     */s	{ 0,I2, LI_COEF_CH_CH, 0x00 },mm/* B      to IC     */s	{ 1,I2, LI_COEF_CH_CH, 0x00 },mm/* Alt B  to IC     */s	{ 0,I2, LI_COEF_PC_CH, 0x00 },mm/* PC     to IC     */s	{ 1,I2, LI_COEF_PC_CH, 0x00 },mm/* Alt PC to IC     */s	{ 2,I2, LI_COEF_CH_CH, 0x00 },mm/* IC     to IC     */s	{ 3,I2, LI_COEF_CH_CH, 0x00 },mm/* Alt IC to IC     */s	{ 1,I1, LI_COEF_CH_CH, 0x01 },mm/* Alt B  to Alt B  */s	{ 0,I1, LI_COEF_CH_CH, 0x01 },mm/* B      to Alt B  */s	{ 1,I1, LI_COEF_PC_CH, 0x80 },mm/* Alt PC to Alt B  */s	{ 0,I1, LI_COEF_PC_CH, 0x01 },mm/* PC     to Alt B  */s	{ 3,I1, LI_COEF_CH_CH, 0x00 },mm/* Alt IC to Alt B  */s	{ 2,I1, LI_COEF_CH_CH, 0x00 },mm/* IC     to Alt B  */s	{ 1,I1, LI_COEF_CH_PC, 0x80 },mm/* Alt B  to Alt PC */s	{ 0,I1, LI_COEF_CH_PC, 0x01 },mm/* B      to Alt PC */s	{ 1,I1, LI_COEF_PC_PC, 0x01 },mm/* Alt PC to Alt PC */s	{ 0,I1, LI_COEF_PC_PC, 0x01 },mm/* PC     to Alt PC */s	{ 3,I1, LI_COEF_CH_PC, 0x00 },mm/* Alt IC to Alt PC */s	{ 2,I1, LI_COEF_CH_PC, 0x00 },mm/* IC     to Alt PC */s	{ 1,I3, LI_COEF_CH_CH, 0x00 },mm/* Alt B  to Alt IC */s	{ 0,I3, LI_COEF_CH_CH, 0x00 },mm/* B      to Alt IC */s	{ 1,I3, LI_COEF_PC_CH, 0x00 },mm/* Alt PC to Alt IC */s	{ 0,I3, LI_COEF_PC_CH, 0x00 },mm/* PC     to Alt IC */s	{ 3,I3, LI_COEF_CH_CH, 0x00 },mm/* Alt IC to Alt IC */s	{ 2,I3, LI_COEF_CH_CH, 0x00 } mm/* IC     to Alt IC */s};sss		 ic;by  lmixer_swapped_ ndex_b);[] =
{"P18,mm/* B      to B      */s	19,mm/* Alt B  to B      */s	20,mm/* PC     to B      */s	21,mm/* Alt PC to B      */s	22,mm/* IC     to B      */s	23,mm/* Alt IC to B      */s	24,mm/* B      to PC     */s	25,mm/* Alt B  to PC     */s	26,mm/* PC     to PC     */s	27,mm/* Alt PC to PC     */s	28,mm/* IC     to PC     */s	29,mm/* Alt IC to PC     */s	30,mm/* B      to IC     */s	31,mm/* Alt B  to IC     */s	32,mm/* PC     to IC     */s	33,mm/* Alt PC to IC     */s	34,mm/* IC     to IC     */s	35,mm/* Alt IC to IC     */s	0,mmm/* Alt B  to Alt B  */s	1,mmm/* B      to Alt B  */s	2,mmm/* Alt PC to Alt B  */s	3,mmm/* PC     to Alt B  */s	4,mmm/* Alt IC to Alt B  */s	5, mm/* IC     to Alt B  */s	6,mmm/* Alt B  to Alt PC */s	7,mmm/* B      to Alt PC */s	8,mmm/* Alt PC to Alt PC */s	9,mmm/* PC     to Alt PC */s	10,mm/* Alt IC to Alt PC */s	11,mm/* IC     to Alt PC */s	12,mm/* Alt B  to Alt IC */s	13,mm/* B      to Alt IC */s	14,mm/* Alt PC to Alt IC */s	15,mm/* PC     to Alt IC */s	16,mm/* Alt IC to Alt IC */s	17 mm/* IC     to Alt IC */s};sss		 ic;struct
{"Pby  lmask;3Pby  lfrom_pc("Pby  lto\pc("}lxordnect_write_prog[] =
{"P{ LI_COEF_CH_CH, fai] , fai]  },"P{ LI_COEF_CH_PC, fai] , tru  },"P{ LI_COEF_PC_CH, tru , fai]  },"P{ LI_COEF_PC_PC, tru , tru  }s};ssss		 ic;voidAxordnect_query_addresses(		In	*0);
	
TE;DIVA_CAPI_ADAPTER *a("N_masnw, o
;"Pby  l*pPEEN].info + 1);
				[%06lx] %s,%d:Axordnect_query_addresses","PTY(d_mask(fnDINi-Idm<< 8) | UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTY()
	r *)(FILE_), __LINE__	)("fNa	= nDINi-Crplci t
	T	ifC->li_1); B3_X(nDINi-li_b)
				&_idm	= 0)3PTY  |a (li_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&!= PEND		er-{s e].info + 1);
				[%06x] %s,%d:AC
				& idmwiped out","PTYY(d_mask(fnDINi-Idm<< 8) | UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTYY()
	r *)(FILE_), __LINE__	)("TYbrw", t"f}3Pp =I_b3_i-;
	ernal\req_buffer;
>chm=IfC->li_1);e ?lpDINi-li_b)
				&_idm- 1 :I0f3	*(ppte = ULATAnREQUESn_XCONNECT_FROM("N_m	Ichf3	*(ppte = (by  ) wf3	*(ppte = (by  )(wm>> 8)("N_m	Ich | XCONNECT_CHANNEL_PORT_PC;aP*(ppte = (by  ) wf3	*(ppte = (by  )(wm>> 8)("NpDINi-NDatac0].P =I_b3_i-;
	ernal\req_buffer;
>pDINi-NDatac0].PLi = 1m=Ipm- pDINi-;
	ernal\req_buffer;
>pDINi-NL.X =I_b3_i-NData;
>pDINi-NL.ReqChm=I0f3	pDINi-NL.Req =I_b3_i-n
_req = (by  ) N_ULATA;
fnDINi-Crplci G)requ		r(&pDINi-NL)("}
sss		 ic;voidAxordnect_write_orefs(		In	*0);
,n_masnN
	ernal\commci-)
TEE;].info + 1);
				[%06lx] %s,%d:Axordnect_write_orefs %04x","PTY(d_mask(fnDINi-Idm<< 8) | UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTY()
	r *)(FILE_), __LINE__,lN
	ernal\commci-))("fNpb3_i-li_write_ormmci-	= N
	ernal\commci-;fNpb3_i-li_write_o
				& =I0f3}
sss		 ic;by  lxordnect_write_orefs_process(d_mas Id,l		In	*0);
,nby  lRc	
TE;DIVA_CAPI_ADAPTER *a("N_masnw, n,nN,nj, r, s,lto\o
;"Pd_mas d;"Pby  l*pPE	structlxordnect_transfer_address_s   *transfer_address;3tby  lch_map[MIXER_CHANNELS_BRI]("EN].info + 1);
				[%06x] %s,%d:Axordnect_write_orefs_process %02x %d","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc,nnDINi-li_write_o
				&	)("fNa	= nDINi-Crplci t
	T	if(nDINi-li_b)
				&_idm	= 0)3P    |a (li_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&!= PEND		r-{s e].info + 1);
				[%06x] %s,%d:AC
				& idmwiped out","PTYYUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTbrw", I(tru );3P}a	_m	IC->li_basl + (nDINi-li_b)
				&_idm- 1);3Pj	= nDINi-li_write_o
				&;3Pp =I_b3_i-;
	ernal\req_buffer;
>T	ifj&!= 0)r-{s e
	if(Rcm!= OK)rB3_XRcm!= OK_FC	er-P_3PTY].info + 1);
				[%06lx] %s,%d:ALI write orefs fai
	d %02x","PTYPTUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc));3PTYbrw", I(fai] );3te}	Y}a	
	ifli_codfig_tD;
	[i].Crplci G)mciufaurol_r_fearol_s & MANUFACTURER_FEATURE_XCONNECT)r-_3PTrm=I0f3	fsm=I0f3	fT	ifj&;
li_total\o
				&s)r-P{r-		
	ifli_codfig_tD;
	[i].)
				& & LI_CHANNEL_ADDRESSEScSET)r-Pf_3PTY_sm=If(li_codfig_tD;
	[i].send_b.card_address.low | li_codfig_tD;
	[i].send_b.card_address.highe ?r-			     (LI_COEF_CH_CH | LI_COEF_CH_PC | LI_COEF_PC_CH | LI_COEF_PC_PC) :I(LI_COEF_CH_PC | LI_COEF_PC_PC))m&3PTY_f((li_codfig_tD;
	[i].send_pc.card_address.low | li_codfig_tD;
	[i].send_pc.card_address.highe ?r-				 (LI_COEF_CH_CH | LI_COEF_CH_PC | LI_COEF_PC_CH | LI_COEF_PC_PC) :I(LI_COEF_CH_CH | LI_COEF_PC_CH))("PTm}ateNrm=If(li_codfig_tD;
	[i].)ref_tD;
	[j]m& 0xf) ^ifli_codfig_tD;
	[i].)ref_tD;
	[j]m>> 4))("PTmwhilemy(j);
li_total\o
				&s)
		f       B3_X(r	== 0)3PTYY   |a (!yli_codfig_tD;
	[j].)
				& & LI_CHANNEL_ADDRESSEScSET))3PTYY   |a (!li_codfig_tD;
	[j].Crplci G)li_1);r-			      rB3_Xj)>= li_codfig_tD;
	[j].Crplci G)li_basl + MIXER_BCHANNELS_BRI))3PTYY   |a ((fli_codfig_tD;
	[j].send_b.card_address.low != li_codfig_tD;
	[i].send_b.card_address.low)3PTY_f|a (li_codfig_tD;
	[j].send_b.card_address.high != li_codfig_tD;
	[i].send_b.card_address.highe)r-			      rB3_X!yaINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_DMACONNECT)r-Pfff   |a !yli_codfig_tD;
	[j].Crplci G)mciufaurol_r_fearol_s & MANUFACTURER_FEATURE_DMACONNECT)))3PTYY   |a ((li_codfig_tD;
	[j].Crplci G)li_basl !	IC->li_basl)r-			      rB3_!(  & sm&3PTY_f    (fli_codfig_tD;
	[j].send_b.card_address.low | li_codfig_tD;
	[j].send_b.card_address.highe ?r-				     (LI_COEF_CH_CH | LI_COEF_CH_PC | LI_COEF_PC_CH | LI_COEF_PC_PC) :I(LI_COEF_PC_CH | LI_COEF_PC_PC))m&3PTY_f    (fli_codfig_tD;
	[j].send_pc.card_address.low | li_codfig_tD;
	[j].send_pc.card_address.highe ?r-				     (LI_COEF_CH_CH | LI_COEF_CH_PC | LI_COEF_PC_CH | LI_COEF_PC_PC) :I(LI_COEF_CH_CH | LI_COEF_CH_PC))))))r-Pf_3PTY_j++;
SArNT	ifj&;
li_total\o
				&s)r-PfeNrm=If(li_codfig_tD;
	[i].)ref_tD;
	[j]m& 0xf) ^ifli_codfig_tD;
	[i].)ref_tD;
	[j]m>> 4))("PTm}aPT}	YfT	ifj&;
li_total\o
				&s)r-P{r-		_b3_i-;
	ernal\ormmci-	= nDINi-li_write_ormmci-;
SArT	ifnDIN_n
_busy(PEND	er-Pffbrw", I(tru );3P		to\o
m=IfC->li_1);e ?lpDINi-li_b)
				&_idm- 1 :I0f3			*(ppte = ULATAnREQUESn_XCONNECT_TOf3			dor-Pf_3PTY_
	ifli_codfig_tD;
	[j].Crplci G)li_basl !	IC->li_basl)r-			{r-PfeNrm&= sm&3PTY_ff((li_codfig_tD;
	[j].send_b.card_address.low | li_codfig_tD;
	[j].send_b.card_address.highe ?r-					 (LI_COEF_CH_CH | LI_COEF_CH_PC | LI_COEF_PC_CH | LI_COEF_PC_PC) :I(LI_COEF_PC_CH | LI_COEF_PC_PC))m&3PTY_ff((li_codfig_tD;
	[j].send_pc.card_address.low | li_codfig_tD;
	[j].send_pc.card_address.highe ?r-					 (LI_COEF_CH_CH | LI_COEF_CH_PC | LI_COEF_PC_CH | LI_COEF_PC_PC) :I(LI_COEF_CH_CH | LI_COEF_CH_PC))f3P	Y_}3PTY	  = 0f3E			dor-Pff_3PTY_E
	if  & xordnect_write_prog[n].mask	3PTY_E_3PTY_EE
	ifxordnect_write_prog[n].from_pc)r-PfTY_ftransfer_address = &(li_codfig_tD;
	[j].send_pc)("fff_ff[i] r-fo	;_ftransfer_address = &(li_codfig_tD;
	[j].send_b)("fff_ff-	= transfer_address->card_address.low("fff_ff*(ppte = (by  ) d("fff_ff*(ppte = (by  )(dm>> 8)("Nff_ff*(ppte = (by  )(dm>> 16)("Nff_ff*(ppte = (by  )(dm>> 24)("fff_ff-	= transfer_address->card_address.high("fff_ff*(ppte = (by  ) d("fff_ff*(ppte = (by  )(dm>> 8)("Nff_ff*(ppte = (by  )(dm>> 16)("Nff_ff*(ppte = (by  )(dm>> 24)("fff_ff-	= transfer_address->offset("fff_ff*(ppte = (by  ) d("fff_ff*(ppte = (by  )(dm>> 8)("Nff_ff*(ppte = (by  )(dm>> 16)("Nff_ff*(ppte = (by  )(dm>> 24)("fff_ff_m	Ixordnect_write_prog[n].to\pc ?lto\o
m| XCONNECT_CHANNEL_PORT_PC :Ito\o
;"Pff_ff*(ppte = (by  ) w("fff_ff*(ppte = (by  )(wm>> 8)("Nff_ff_m	If(li_codfig_tD;
	[i].)ref_tD;
	[j]m& xordnect_write_prog[n].mask		== 0) ? 0x01 :r-fo	;_ffli_codfig_tD;
	[i].Crplci G)u_law ?r-						ifli_codfig_tD;
	[j].Crplci G)u_law ? 0x80 : 0x86) :r-fo	;_fifli_codfig_tD;
	[j].Crplci G)u_law ? 0x7a : 0x80))("Nff_ff*(ppte = (by  ) w("fff_ff*(ppte = (by  ) 0("fTY_ffli_codfig_tD;
	[i].)ref_tD;
	[j]m^	Ixordnect_write_prog[n].mask <<I4;aPTYlN}3PTY_fn++;
SArN} whilemy(n);
ARRAYnSIZE(xordnect_write_prog))r-			T B3_X(nm- pDINi-;
	ernal\req_buffer)m+I16);
INTERNAL_REQ_BUFFER_SIZE))("Nff_
	ifnm==
ARRAYnSIZE(xordnect_write_prog))r-			_3PTY_Edor-Pff;_3PTY_ffj++;
SArNNNT	ifj&;
li_total\o
				&s)r-PfeNeNrm=If(li_codfig_tD;
	[i].)ref_tD;
	[j]m& 0xf) ^ifli_codfig_tD;
	[i].)ref_tD;
	[j]m>> 4))("PTmrN} whilemy(j&;
li_total\o
				&s)r-PfeNe B3_X(r	== 0)3PTYY		     |a (!yli_codfig_tD;
	[j].)
				& & LI_CHANNEL_ADDRESSEScSET))3PTYY		     |a (!li_codfig_tD;
	[j].Crplci G)li_1);r-			eNe B3_Xj)>= li_codfig_tD;
	[j].Crplci G)li_basl + MIXER_BCHANNELS_BRI))3PTYY		     |a ((fli_codfig_tD;
	[j].send_b.card_address.low != li_codfig_tD;
	[i].send_b.card_address.low)3PTY_fTY  |a (li_codfig_tD;
	[j].send_b.card_address.high != li_codfig_tD;
	[i].send_b.card_address.highe)r-			eNe B3_X!yaINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_DMACONNECT)r-Pfff		     |a !yli_codfig_tD;
	[j].Crplci G)mciufaurol_r_fearol_s & MANUFACTURER_FEATURE_DMACONNECT)))3PTYY		     |a ((li_codfig_tD;
	[j].Crplci G)li_basl !	IC->li_basl)r-			eNe B3_!(  & sm&3PTY_f		      ((li_codfig_tD;
	[j].send_b.card_address.low | li_codfig_tD;
	[j].send_b.card_address.highe ?r-						      r(LI_COEF_CH_CH | LI_COEF_CH_PC | LI_COEF_PC_CH | LI_COEF_PC_PC) :I(LI_COEF_PC_CH | LI_COEF_PC_PC))m&3PTY_ff	      ((li_codfig_tD;
	[j].send_pc.card_address.low | li_codfig_tD;
	[j].send_pc.card_address.highe ?r-						      r(LI_COEF_CH_CH | LI_COEF_CH_PC | LI_COEF_PC_CH | LI_COEF_PC_PC) :I(LI_COEF_CH_CH | LI_COEF_CH_PC))))))f3P	Y_}3PTY} whilemy(j&;
li_total\o
				&s)r-Pfe B3_X(nm- pDINi-;
	ernal\req_buffer)m+I16);
INTERNAL_REQ_BUFFER_SIZE))("Nf}
>S[i] mT	ifj&== li_total\o
				&s)r-P{r-		_b3_i-;
	ernal\ormmci-	= nDINi-li_write_ormmci-;
SArT	ifnDIN_n
_busy(PEND	er-Pffbrw", I(tru );3P		T	ifC->li_1);er--;_3PTY_*(ppte = ULATAnREQUESn_SET_MIXER_COEFS_PRI_SYNC;aPTY_w = 0f3E			
	ifli_codfig_tD;
	[i].)
				& & LI_CHANNEL_TX_LATA)r-			ewm|= MIXER_FEATURE_ENABLE_TX_LATA;
fTN	
	ifli_codfig_tD;
	[i].)
				& & LI_CHANNEL_RX_LATA)r-			ewm|= MIXER_FEATURE_ENABLE_RX_LATA;
fTNE*(ppte = (by  ) w("fff_*(ppte = (by  )(wm>> 8)("Nff}aPTY[i] r--;_3PTY_*(ppte = ULATAnREQUESn_SET_MIXER_COEFS_BRI;aPTY_w = 0f3E			
	ifXnDINi-t	&	== ADV_VOICE	 B3_XnDIN	== C->AdvSignal		In)r-			    B3_XADV_VOICE_NEW_COEF_BASEm+asizeof(_mask);=
ai-Crv_voice_)ref_li = 1))r-			_3PTY_Ew = GE		}
			ai-Crv_voice_)ref_bufferm+aADV_VOICE_NEW_COEF_BASE)f3P	Y_}3PTY	
	ifli_codfig_tD;
	[i].)
				& & LI_CHANNEL_TX_LATA)r-			ewm|= MIXER_FEATURE_ENABLE_TX_LATA;
fTN	
	ifli_codfig_tD;
	[i].)
				& & LI_CHANNEL_RX_LATA)r-			ewm|= MIXER_FEATURE_ENABLE_RX_LATA;
fTNE*(ppte = (by  ) w("fff_*(ppte = (by  )(wm>> 8)("Nff_fcilijm=I0f j);
sizeof(ch_map)f j)+= 2	r-Pff_3PTY_E
	ifnDINi-li_b)
				&_idm	= 2	3PTY_E_3PTY_EEch_map[jrm	I(by  )ijm+ 1);3PTY_EEch_map[jm+a1r = (by  ) j;aPTYlN}3PTY_f[i] r-fo	;_3PTY_EEch_map[jrm	I(by  ) j;aPTYlNEch_map[jm+a1r = (by  )ijm+ 1);3PTY_E}3PTY_}3PTY	fcili  = 0f n);
ARRAYnSIZE(mixer_write_prog_b););dnpter--Pf_3PTY_E
m	IC->li_basl + ch_map[mixer_write_prog_b);[n].to\ch];3PTY_fjm	IC->li_basl + ch_map[mixer_write_prog_b);[n].from_)
];3PTY_f
	ifli_codfig_tD;
	[i].)
				& & li_codfig_tD;
	[j].)
				& & LI_CHANNEL_INVOLVEDe
-Po	;_3PTY_EE*p =I(mixer_write_prog_b);[n].xordnect_override	!= 0) ?r-						mixer_write_prog_b);[n].xordnect_override	:r-fo	;_ff(li_codfig_tD;
	[i].)ref_tD;
	[j]m& mixer_write_prog_b);[n].mask		? 0x80 : 0x01);3PTY_EE
	ifXi)>= aG)li_basl + MIXER_BCHANNELS_BRI) |a (j)>= aG)li_basl + MIXER_BCHANNELS_BRI))r-			eN_3PTY_EEf_m	If(li_codfig_tD;
	[i].)ref_tD;
	[j]m& 0xf) ^ifli_codfig_tD;
	[i].)ref_tD;
	[j]m>> 4))("PTmrNffli_codfig_tD;
	[i].)ref_tD;
	[j]m^	I(wm& mixer_write_prog_b);[n].mask		<<I4;aPTYlNN}3PTY_f}3PTY_f[i] r-fo	;_3PTY_EE*p =I0x00;3PTY_EE
	ifXC->AdvSignal		Inm!= NULL) B3_XC->AdvSignal		Ini-t	&	== ADV_VOICE	er-Pf;eN_3PTY_EEf_m	IfnDIN	== C->AdvSignal		In)	? n :lmixer_swapped_ ndex_b);[n]("PTmrNff
	ifADV_VOICE_NEW_COEF_BASEm+asizeof(_mask)+ w);
ai-Crv_voice_)ref_li = 1)"PTmrNffE*p =Iai-Crv_voice_)ref_buffer[ADV_VOICE_NEW_COEF_BASEm+asizeof(_mask)+ w];aPTYlNN}3PTY_f}3PTY_fn++;
SArN}3PTY}aPTNj = li_total\o
				&sm+I1f3P	}3t}"	[i] Em{
lY
	ifj);=
li_total\o
				&s)r-P{r-		_b3_i-;
	ernal\ormmci-	= nDINi-li_write_ormmci-;
SArT	ifnDIN_n
_busy(PEND	er-Pffbrw", I(tru );3P		T	ifj);
ai-li_basl)r-			jm	IC->li_basl;3P		T	ifC->li_1);er--;_3PTY_*(ppte = ULATAnREQUESn_SET_MIXER_COEFS_PRI_SYNC;aPTY_w = 0f3E			
	ifli_codfig_tD;
	[i].)
				& & LI_CHANNEL_TX_LATA)r-			ewm|= MIXER_FEATURE_ENABLE_TX_LATA;
fTN	
	ifli_codfig_tD;
	[i].)
				& & LI_CHANNEL_RX_LATA)r-			ewm|= MIXER_FEATURE_ENABLE_RX_LATA;
fTNE*(ppte = (by  ) w("fff_*(ppte = (by  )(wm>> 8)("Nff_fcili  = 0f n);
ARRAYnSIZE(mixer_write_prog_p););dnpter--Pf_3PTY_E*(ppte = (by  )((nDINi-li_b)
				&_idm- 1) | mixer_write_prog_1);[n].line_flags)("PTmrNfcilijm=IC->li_basl; j);
ai-li_basl + MIXER_CHANNELS_PRI;djpte
-P	f;_3PTY_ff_m	If(li_codfig_tD;
	[i].)ref_tD;
	[j]m& 0xf) ^ifli_codfig_tD;
	[i].)ref_tD;
	[j]m>> 4))("PTmrNf
	ifwm& mixer_write_prog_p);[n].mask	r-Pf;eN_3PTY_EEf*(ppte = (li_codfig_tD;
	[i].)ref_tD;
	[j]m& mixer_write_prog_p);[n].mask		? 0x80 : 0x01("PTmrNffli_codfig_tD;
	[i].)ref_tD;
	[j]m^	Imixer_write_prog_p);[n].mask	<<I4;aPTYlNN}3PTY_ff[i] r-fo	;_f*(ppte = 0x00;3PTY_E}3PTY_E*(ppte = (by  )((nDINi-li_b)
				&_idm- 1) | MIXER_COEF_LINE_ROW_FLAG | mixer_write_prog_1);[n].line_flags)("PTmrNfcilijm=IC->li_basl; j);
ai-li_basl + MIXER_CHANNELS_PRI;djpte
-P	f;_3PTY_ff_m	If(li_codfig_tD;
	[j].)ref_tD;
	[i]m& 0xf) ^ifli_codfig_tD;
	[j].)ref_tD;
	[i]m>> 4))("PTmrNf
	ifwm& mixer_write_prog_p);[n].mask	r-Pf;eN_3PTY_EEf*(ppte = (li_codfig_tD;
	[j].)ref_tD;
	[i]m& mixer_write_prog_p);[n].mask		? 0x80 : 0x01("PTmrNffli_codfig_tD;
	[j].)ref_tD;
	[i]m^	Imixer_write_prog_p);[n].mask	<<I4;aPTYlNN}3PTY_ff[i] r-fo	;_f*(ppte = 0x00;3PTY_E}3PTY_}3PTY}aPTY[i] r--;_3PTY_*(ppte = ULATAnREQUESn_SET_MIXER_COEFS_BRI;aPTY_w = 0f3E			
	ifXnDINi-t	&	== ADV_VOICE	 B3_XnDIN	== C->AdvSignal		In)r-			    B3_XADV_VOICE_NEW_COEF_BASEm+asizeof(_mask);=
ai-Crv_voice_)ref_li = 1))r-			_3PTY_Ew = GE		}
			ai-Crv_voice_)ref_bufferm+aADV_VOICE_NEW_COEF_BASE)f3P	Y_}3PTY	
	ifli_codfig_tD;
	[i].)
				& & LI_CHANNEL_TX_LATA)r-			ewm|= MIXER_FEATURE_ENABLE_TX_LATA;
fTN	
	ifli_codfig_tD;
	[i].)
				& & LI_CHANNEL_RX_LATA)r-			ewm|= MIXER_FEATURE_ENABLE_RX_LATA;
fTNE*(ppte = (by  ) w("fff_*(ppte = (by  )(wm>> 8)("Nff_fcilijm=I0f j);
sizeof(ch_map)f j)+= 2	r-Pff_3PTY_E
	ifnDINi-li_b)
				&_idm	= 2	3PTY_E_3PTY_EEch_map[jrm	I(by  )ijm+ 1);3PTY_EEch_map[jm+a1r = (by  ) j;aPTYlN}3PTY_f[i] r-fo	;_3PTY_EEch_map[jrm	I(by  ) j;aPTYlNEch_map[jm+a1r = (by  )ijm+ 1);3PTY_E}3PTY_}3PTY	fcili  = 0f n);
ARRAYnSIZE(mixer_write_prog_b););dnpter--Pf_3PTY_E
m	IC->li_basl + ch_map[mixer_write_prog_b);[n].to\ch];3PTY_fjm	IC->li_basl + ch_map[mixer_write_prog_b);[n].from_)
];3PTY_f
	ifli_codfig_tD;
	[i].)
				& & li_codfig_tD;
	[j].)
				& & LI_CHANNEL_INVOLVEDe
-Po	;_3PTY_EE*p =I((li_codfig_tD;
	[i].)ref_tD;
	[j]m& mixer_write_prog_b);[n].mask		? 0x80 : 0x01);3PTY_EE_m	If(li_codfig_tD;
	[i].)ref_tD;
	[j]m& 0xf) ^ifli_codfig_tD;
	[i].)ref_tD;
	[j]m>> 4))("PTmrNfli_codfig_tD;
	[i].)ref_tD;
	[j]m^	I(wm& mixer_write_prog_b);[n].mask		<<I4;aPTYlN}3PTY_f[i] r-fo	;_3PTY_EE*p =I0x00;3PTY_EE
	ifXC->AdvSignal		Inm!= NULL) B3_XC->AdvSignal		Ini-t	&	== ADV_VOICE	er-Pf;eN_3PTY_EEf_m	IfnDIN	== C->AdvSignal		In)	? n :lmixer_swapped_ ndex_b);[n]("PTmrNff
	ifADV_VOICE_NEW_COEF_BASEm+asizeof(_mask)+ w);
ai-Crv_voice_)ref_li = 1)"PTmrNffE*p =Iai-Crv_voice_)ref_buffer[ADV_VOICE_NEW_COEF_BASEm+asizeof(_mask)+ w];aPTYlNN}3PTY_f}3PTY_fn++;
SArN}3PTY}aPTNj = li_total\o
				&sm+I1f3P	}3t}"	pb3_i-li_write_o
				& =Ij;aP
	ifpm!= pDINi-;
	ernal\req_buffer)r-{s epDINi-NDatac0].P =I_b3_i-;
	ernal\req_buffer;
>>pDINi-NDatac0].PLi = 1m=Ipm- pDINi-;
	ernal\req_buffer;
>>pDINi-NL.X =I_b3_i-NData;
>>pDINi-NL.ReqChm=I0f3		pDINi-NL.Req =I_b3_i-n
_req = (by  ) N_ULATA;
ffnDINi-Crplci G)requ		r(&pDINi-NL)("t}"	brw", I(tru );3}
sss		 ic;voidAmixer_notify_updateA		In	*0);
,nby  lothers	
TE;DIVA_CAPI_ADAPTER *a("N_masn
,n_;"NP	In	*notify_0);
;3tby  lmsg[sizeof(CAPI_MSG_HEADERk)+ 6]("EN].info + 1);
				[%06lx] %s,%d:Amixer_notify_update %d","PTY(d_mask(fnDINi-Idm<< 8) | UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTY()
	r *)(FILE_), __LINE__,lothers	)("fNa	= nDINi-Crplci t
	T	ifC->profile.Global\Options & GL_LINE_INTERCONNECT_SUPPORTEDe
-{
lY
	ifothers	
	ffnDINi-li_notify_update = tru ;
ffim=I0f3		dor-P_3PTYnotify_0);
 =INULL("fff
	ifothers	
	ff_3PTY_whilemy());
li_total\o
				&s) B3_Xli_codfig_tD;
	[i].0);
&== NULL))"PTmrNi++;
SArNT	if));
li_total\o
				&s)"PTmrNnotify_0);
 =Ili_codfig_tD;
	[i++].0);
("Nff}aPTY[i] r--;_3PTY_T	if(nDINi-li_b)
				&_idm!= 0)r-			    B3_Xli_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&== PEND	er-;eN_3PTY_Enotify_0);
 =IPEND;3teTY}3PTY}aPTN
	if(notify_0);
 != NULL)r-Pf   rB3_!notify_0);
i-li_notify_updater-Pf   rB3_(notify_0);
i-appl != NULL)r-Pf   rB3_(notify_0);
i-S		 e)r-Pf   rB3_notify_0);
i-NL.IdmB3_!notify_0);
i-n
_remove_id)
;eN_3PTY_notify_0);
i-li_notify_update = tru ;
ff_ff(CAPI_MSG *)lmsg)->header.li = 1m=I18;
ff_ff(CAPI_MSG *)lmsg)->header.appl_idm	Inotify_0);
i-applG)Id;
ff_ff(CAPI_MSG *)lmsg)->header.ormmci-	= _FACILITY_R;
ff_ff(CAPI_MSG *)lmsg)->header.numberm=I0f3	f_ff(CAPI_MSG *)lmsg)->header.ord w;
lerm	Inotify_0);
i-arplci G)Idf3	f_ff(CAPI_MSG *)lmsg)->header.0);
 =Inotify_0);
i-Idf3	f_ff(CAPI_MSG *)lmsg)->header.nccim=I0f3	f_ff(CAPI_MSG *)lmsg)->info.facility_req.Selectorm	ISELECTOR_LINE_INTERCONNECTf3	f_ff(CAPI_MSG *)lmsg)->info.facility_req.structs[0rm	I3 fND;	PU		}
			&(f(CAPI_MSG *)lmsg)->info.facility_req.structs[1]), LI_REQ_SILENT_UPLATE)f3P	Y_f(CAPI_MSG *)lmsg)->info.facility_req.structs[3]m=I0f3	f_f_m	Iapi_put(notify_0);
i-appl, (CAPI_MSG *)lmsg);
SArNT	ifw != _QUEUE_FULL)r-Pff_3PTY_E
	ifwm!= 0)r-				_3PTY_EE].info + 1);
				[%06lx] %s,%d:AI
	erordnectInotify fai
	d %06x %d","PTYYYYYY(d_mask(fnDINi-Idm<< 8) | UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTYY	PTY()
	r *)(FILE_), __LINE__,"PTYYYYYY(d_mask(fnotify_0);
i-Idm<< 8) | UnMapCrd w;
lerXnotify_0);
i-arplci G)Ide), wf);3PTY_f}3PTY__notify_0);
i-li_notify_update = fai] ;3teTY}3PTY}aPT} whilemyothersrB3_(notify_0);
 != NULL));3PT
	ifothers	
	ffnDINi-li_notify_update = fai] ;3t}3}
sss		 ic;voidAmixer_clear_ordfigf		In	*0);
	
TE;DIVA_CAPI_ADAPTER *a("N_masnN,njPEEN].info + 1);
				[%06lx] %s,%d:Amixer_clear_ordfig","PTY(d_mask(fnDINi-Idm<< 8) | UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTY()
	r *)(FILE_), __LINE__	)("fNnDINi-li_notify_update = fai] ;3tnDINi-li_nDIN_b_write_posm=I0f3	nDINi-li_nDIN_b_read_posm=I0f3	nDINi-li_nDIN_b_req_posm=I0f3	a	= nDINi-Crplci t
	T	if(nDINi-li_b)
				&_idm!= 0)r-    B3_Xli_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&== PEND	er-{s e_m	IC->li_basl + (nDINi-li_b)
				&_idm- 1);3Pfli_codfig_tD;
	[i].)urchnl =I0f3	fli_codfig_tD;
	[i].)
				& =I0f3	fli_codfig_tD;
	[i].)
flagsm=I0f3	facilijm=I0f j);
li_total\o
				&s;djpte
-P{r-		li_codfig_tD;
	[j].flag_tD;
	[i]m=I0f3	f_li_codfig_tD;
	[i].flag_tD;
	[j]m=I0f3	f_li_codfig_tD;
	[i].)ref_tD;
	[j]m=I0f3	f_li_codfig_tD;
	[j].)ref_tD;
	[i]m=I0f3	f}	YfT	if!C->li_1);er--_3PTYli_codfig_tD;
	[i].)ref_tD;
	[i]m|= LI_COEF_CH_PC_SET | LI_COEF_PC_CHcSETf3P		
	ifXnDINi-t	&	== ADV_VOICE	 B3_XnDIN	== C->AdvSignal		In))
;eN_3PTY__m	IC->li_basl + MIXER_IC_CHANNEL_BASEm+a(nDINi-li_b)
				&_idm- 1);3PfPfli_codfig_tD;
	[i].)urchnl =I0f3	f	fli_codfig_tD;
	[i].)
				& =I0f3	f	fli_codfig_tD;
	[i].)
flagsm=I0f3	fY_fcilijm=I0f j);
li_total\o
				&s;djpte
-P	f_3PTY_Eli_codfig_tD;
	[i].flag_tD;
	[j]m=I0f3	f_		li_codfig_tD;
	[j].flag_tD;
	[i]m=I0f3	f_f_li_codfig_tD;
	[i].)ref_tD;
	[j]m=I0f3	f_f_li_codfig_tD;
	[j].)ref_tD;
	[i]m=I0f3	fY_}3PTY	
	ifaINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_SLAVE_CODECer--Pf_3PTY_E
m	IC->li_basl + MIXER_IC_CHANNEL_BASEm+a(2m- pDINi-li_b)
				&_id)f3	f_f_li_codfig_tD;
	[i].)urchnl =I0f3	f	ffli_codfig_tD;
	[i].)
				& =I0f3	f	ffli_codfig_tD;
	[i].)
flagsm=I0f3	fY_Efcilijm=I0f j);
li_total\o
				&s;djpte
-P	f;_3PTY_ffli_codfig_tD;
	[i].flag_tD;
	[j]m=I0f3	f_			li_codfig_tD;
	[j].flag_tD;
	[i]m=I0f3	f_f__li_codfig_tD;
	[i].)ref_tD;
	[j]m=I0f3	f_f__li_codfig_tD;
	[j].)ref_tD;
	[i]m=I0f3	fY_f}3PTY_}3PTY}aPT}	Y}a}
sss		 ic;voidAmixer_prepare_switch(d_mas Id,l		In	*0);
)
TEE;].info + 1);
				[%06lx] %s,%d:Amixer_prepare_switch","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__	)("fNdor-_3PTmixer_indication_orefs_set(Id,l0);
);3t} whilemynDINi-li_nDIN_b_read_posm!= nDINi-li_nDIN_b_req_pos);3}
sss		 ic;_mas mixer_save_ordfigfd_mas Id,l		In	*0);
,nby  lRc	
TE;DIVA_CAPI_ADAPTER *a("N_masnN,njPEEN].info + 1);
				[%06lx] %s,%d:Amixer_save_ordfig %02x %d","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc,nnDINi-adjust_b_s		 e))("fNa	= nDINi-Crplci t
	T	if(nDINi-li_b)
				&_idm!= 0)r-    B3_Xli_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&== PEND	er-{s e_m	IC->li_basl + (nDINi-li_b)
				&_idm- 1);3Pfacilijm=I0f j);
li_total\o
				&s;djpte
-P{r-		li_codfig_tD;
	[i].)ref_tD;
	[j]m&= 0xff3	f_li_codfig_tD;
	[j].)ref_tD;
	[i]m&= 0xff3	f}	YfT	if!C->li_1);er--Yli_codfig_tD;
	[i].)ref_tD;
	[i]m|= LI_COEF_CH_PC_SET | LI_COEF_PC_CHcSETf3P}"	brw", I(GOOD);3}
sss		 ic;_mas mixer_l_store_ordfigfd_mas Id,l		In	*0);
,nby  lRc	
TE;DIVA_CAPI_ADAPTER *a("N_masnInfoPEEN].info + 1);
				[%06lx] %s,%d:Amixer_l_store_ordfig %02x %d","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc,nnDINi-adjust_b_s		 e))("fNInfo = GOODf3	a	= nDINi-Crplci t
	T	if(nDINi-B1_faciliti_s & B1_FACILITY_MIXER)r-    B3_XnDINi-li_b)
				&_idm!= 0)r-    B3_Xli_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&== PEND	er-{s eswitch (nDINi-adjust_b_s		 e)
-P{r-	casl ADJUST_B_RESTORE_MIXER_1:3P		
	ifaINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_XCONNECT)r-Pf_3PTY__b3_i-;
	ernal\ormmci-	= nDINi-adjust_b_ormmci-;
SArrT	ifnDIN_n
_busy(PEND	er-Pff_3PTY_fnDINi-adjust_b_s		 e = ADJUST_B_RESTORE_MIXER_1f3	fY_fbreakf3	fY_}3PTY	xordnect_query_addresses(0);
);3tY_fnDINi-adjust_b_s		 e = ADJUST_B_RESTORE_MIXER_2;3tY_fbreakf3	fY}
Y_fnDINi-adjust_b_s		 e = ADJUST_B_RESTORE_MIXER_5f3	fYRc = OK;r-	casl ADJUST_B_RESTORE_MIXER_2:r-	casl ADJUST_B_RESTORE_MIXER_3:r-	casl ADJUST_B_RESTORE_MIXER_4:3P		
	if(Rcm!= OK)rB3_XRcm!= OK_FC	rB3_XRcm!= 0))
;eN_3PTY_].info + 1);
				[%06lx] %s,%d:AAdjust B query addresses fai
	d %02x","PTYPTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc));3PTYNInfo = _WRONG_STATE;3tY_fbreakf3	fY}
Y_f
	ifRc == OK)r--;_3PTY_T	ifnDINi-adjust_b_s		 e == ADJUST_B_RESTORE_MIXER_2)3PTY_fnDINi-adjust_b_s		 e = ADJUST_B_RESTORE_MIXER_3 fND;	[i] mT	ifnDINi-adjust_b_s		 e == ADJUST_B_RESTORE_MIXER_4)3PTY_fnDINi-adjust_b_s		 e = ADJUST_B_RESTORE_MIXER_5("Nff}aPTY[i]  
	ifRc == 0)r--;_3PTY_T	ifnDINi-adjust_b_s		 e == ADJUST_B_RESTORE_MIXER_2)3PTY_fnDINi-adjust_b_s		 e = ADJUST_B_RESTORE_MIXER_4 fND;	[i] mT	ifnDINi-adjust_b_s		 e == ADJUST_B_RESTORE_MIXER_3)3PTY_fnDINi-adjust_b_s		 e = ADJUST_B_RESTORE_MIXER_5("Nff}aPTYT	ifnDINi-adjust_b_s		 e != ADJUST_B_RESTORE_MIXER_5)r-Pf_3PTY__b3_i-;
	ernal\ormmci-	= nDINi-adjust_b_ormmci-;
SArrbreakf3	fY}
Y_casl ADJUST_B_RESTORE_MIXER_5:3P		xordnect_write_orefs(0);
,nnDINi-adjust_b_ormmci-);3PTYnDINi-adjust_b_s		 e = ADJUST_B_RESTORE_MIXER_6f3	fYRc = OK;r-	casl ADJUST_B_RESTORE_MIXER_6:3P		
	if!xordnect_write_orefs_process(Id,l0);
,lRc))
;eN_3PTY_].info + 1);
				[%06lx] %s,%d:AWrite mixer orefs fai
	d","PTYPTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNInfo = _FACILITY_NOT_SUPPORTED;3tY_fbreakf3	fY}
Y_f
	if_b3_i-;
	ernal\ormmci-)3tY_fbreakf3	fYnDINi-adjust_b_s		 e = ADJUST_B_RESTORE_MIXER_7;r-	casl ADJUST_B_RESTORE_MIXER_7:3P		breakf3	f}3P}"	brw", I(Info);3}
sss		 ic;voidAmixer_ormmci-fd_mas Id,l		In	*0);
,nby  lRc	
TE;DIVA_CAPI_ADAPTER *a("N_masnN,nN
	ernal\commci-;fEN].info + 1);
				[%06lx] %s,%d:Amixer_crmmci-	%02x %04x %04x","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc,nnDINi-N
	ernal\commci-,
	ffnDINi-li_cmd))("fNa	= nDINi-Crplci t
	T
	ernal\ormmci-	= nDINi-N
	ernal\commci-;fNpb3_i-T
	ernal\ormmci-	= 0f3	switch (nDINi-li_cmd)r-{s casl LI_REQ_CONNECT:s casl LI_REQ_DISCONNECT:s casl LI_REQ_SILENT_UPLATE:s eswitch (;
	ernal\ormmci-)3tY_3PTdefault:
Y_f
	if_b3_i-li_c
				&_bits & LI_CHANNEL_INVOLVEDe
-Po_3PTY_adjust_b1_l_source(Id,l0);
,lNULL, (_mask(nDINi-B1_faciliti_s |"PTYPTT	Pf  B1_FACILITY_MIXER), MIXER_COMMAND_1)f3	fY}
Y_casl MIXER_COMMAND_1:
Y_f
	if_b3_i-li_c
				&_bits & LI_CHANNEL_INVOLVEDe
-Po_3PTY_
	ifadjust_b_process(Id,l0);
,lRc) != GOOD)r-Pff_3PTY_f].info + 1);
				[%06lx] %s,%d:ALoadAmixer fai
	d","PTYPTTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNfbreakf3	fY_}3PTY	
	if_b3_i-;
	ernal\ormmci-)3tY_fYbrw", t"ffY}
Y_fnDINi-li_nDIN_b_req_posm=InDINi-li_nDIN_b_write_posf3P		
	ifXnDINi-li_c
				&_bits & LI_CHANNEL_INVOLVEDe
-Po    |a ((get_b1_faciliti_s(0);
,nnDINi-B1_l_source) & B1_FACILITY_MIXER)r----B3_XCdd_b1_faciliti_s(0);
,nnDINi-B1_l_source, (_mask(nDINi-B1_faciliti_s &"PTYPTT	Pf	      ~B1_FACILITY_MIXER))&== PENDi-B1_l_source)))
;eN_3PTY_xordnect_write_orefs(0);
,nMIXER_COMMAND_2)("Nff}aPTY[i] r--;_3PTY_dor-Pff_3PTY_Emixer_indication_orefs_set(Id,l0);
);3t		t} whilemynDINi-li_nDIN_b_read_posm!= nDINi-li_nDIN_b_req_pos);3	fY}
Y_casl MIXER_COMMAND_2:3P		
	ifXnDINi-li_c
				&_bits & LI_CHANNEL_INVOLVEDe
-Po    |a ((get_b1_faciliti_s(0);
,nnDINi-B1_l_source) & B1_FACILITY_MIXER)r----B3_XCdd_b1_faciliti_s(0);
,nnDINi-B1_l_source, (_mask(nDINi-B1_faciliti_s &"PTYPTT	Pf	      ~B1_FACILITY_MIXER))&== PENDi-B1_l_source)))
;eN_3PTY_
	if!xordnect_write_orefs_process(Id,l0);
,lRc))
;eNf_3PTY_f].info + 1);
				[%06lx] %s,%d:AWrite mixer orefs fai
	d","PTYPTTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNf
	if_b3_i-li_nDIN_b_write_posm!= nDINi-li_nDIN_b_req_pos)3PTYNf_3PTY_EE]or-Pff;N_3PTY_EEfnDINi-li_nDIN_b_write_posm=If_b3_i-li_nDIN_b_write_posm== 0) ?r-							LI_		In_B_QUEUE_ENTRIESm- 1 :I_b3_i-li_nDIN_b_write_posm- 1("PTmrNffim=If_b3_i-li_nDIN_b_write_posm== 0) ?r-							LI_		In_B_QUEUE_ENTRIESm- 1 :I_b3_i-li_nDIN_b_write_posm- 1("PTmrNf} whilemy(_b3_i-li_nDIN_b_write_posm!= nDINi-li_nDIN_b_req_pos)3PTYNfNe B3_!(nDINi-li_nDIN_b_queu	[i]m& LI_		In_B_LAST_FLAGf);3PTY_f}3PTY__breakf3	fY_}3PTY	
	if_b3_i-;
	ernal\ormmci-)3tY_fYbrw", t"ffY}
Y_f
	if!fnDINi-li_c
				&_bits & LI_CHANNEL_INVOLVEDee
-Po_3PTY_adjust_b1_l_source(Id,l0);
,lNULL, (_mask(nDINi-B1_faciliti_s &"PTYPTT	Pf  ~B1_FACILITY_MIXER),nMIXER_COMMAND_3);3	fY}
Y_casl MIXER_COMMAND_3:
Y_f
	if!fnDINi-li_c
				&_bits & LI_CHANNEL_INVOLVEDee
-Po_3PTY_
	ifadjust_b_process(Id,l0);
,lRc) != GOOD)r-Pff_3PTY_f].info + 1);
				[%06lx] %s,%d:AUnloadAmixer fai
	d","PTYPTTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNfbreakf3	fY_}3PTY	
	if_b3_i-;
	ernal\ormmci-)3tY_fYbrw", t"ffY}
Y_fbreakf3	f}3Pfbreakf3	}
	T	if(nDINi-li_b)
				&_idm	= 0)3P    |a (li_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&!= PEND		r-{s e].info + 1);
				[%06x] %s,%d:AC
				& idmwiped out %d","PTYYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,l(;
	)(nDINi-li_b)
				&_id)f);3P}a	[i] Em{
lY
m	IC->li_basl + (nDINi-li_b)
				&_idm- 1);3Pfli_codfig_tD;
	[i].)urchnl =InDINi-li_c
				&_bits;3PfT	if!C->li_1); B3_XnDINi-t	&	== ADV_VOICE	 B3_XnDIN	== C->AdvSignal		In))
;e_3PTY_m	IC->li_basl + MIXER_IC_CHANNEL_BASEm+a(nDINi-li_b)
				&_idm- 1);3PfPli_codfig_tD;
	[i].)urchnl =InDINi-li_c
				&_bits;3Pf	
	ifaINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_SLAVE_CODECer--P_3PTY__m	IC->li_basl + MIXER_IC_CHANNEL_BASEm+a(2m- pDINi-li_b)
				&_id)f3	f_fli_codfig_tD;
	[i].)urchnl =InDINi-li_c
				&_bits;3Pf	}aPT}	Y}a}
sss		 ic;voidAli_update_ordnectfd_mas Id,lDIVA_CAPI_ADAPTER *a,l		In	*0);
,
-Po      d_mas nDIN_b_id,nby  lordnect, d_mas li_flags)
TE;_masnN,nch_a,nch_a_v,nch_a_s,nch_b,nch_b_v,nch_b_s;3P		In	*0);
_b;E;DIVA_CAPI_ADAPTER *a_b;EfNa_b = &(Crplci [MapCrd w;
lerX(by  )inDIN_b_id & 0x7f))&- 1])("NpDIN_b = &(C_b->pDIN[(inDIN_b_id >> 8)m& 0xff)&- 1])("Nch_am	IC->li_basl + (nDINi-li_b)
				&_idm- 1);3PT	if!C->li_1); B3_XnDINi-t	&	== ADV_VOICE	r-    B3_XnDIN	== C->AdvSignal		In)	B3_XIdmB EXT_CONTROLLER))Em{
lYch_a_v =nch_a + MIXER_IC_CHANNEL_BASE;
lYch_a_sm=IfaINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_SLAVE_CODECe ?r-		C->li_basl + MIXER_IC_CHANNEL_BASEm+a(2m- pDINi-li_b)
				&_id) :Ich_a_v;3P}a	[i] Em{
lYch_a_v =nch_a;
lYch_a_sm=Ich_a;
l}"Nch_b = C_b->li_basl + (nDIN_b->li_b)
				&_idm- 1);3PT	if!C_b->li_1); B3_XnDIN_b->t	&	== ADV_VOICE	r-    B3_XnDIN_b == C_b->AdvSignal		In)	B3_XnDIN_b_id & EXT_CONTROLLER))Em{
lYch_b_v =nch_b + MIXER_IC_CHANNEL_BASE;
lYch_b_sm=Ifa_b->mciufaurol_r_fearol_s & MANUFACTURER_FEATURE_SLAVE_CODECe ?r-		C_b->li_basl + MIXER_IC_CHANNEL_BASEm+a(2m- pDIN_b->li_b)
				&_id) :Ich_b_v;3P}a	[i] Em{
lYch_b_v =nch_b;
lYch_b_sm=Ich_b;
l}
	T	ifordnect)Em{
lYli_codfig_tD;
	[ch_a].flag_tD;
	[ch_a_v]m&= ~LI_FLAG_MONITOR;3Pfli_codfig_tD;
	[ch_a].flag_tD;
	[ch_a_s]m&= ~LI_FLAG_MONITOR;3Pfli_codfig_tD;
	[ch_a_v].flag_tD;
	[ch_a]m&= ~(LI_FLAG_ANNOUNCEMENT | LI_FLAG_MIX);3Pfli_codfig_tD;
	[ch_a_s].flag_tD;
	[ch_a]m&= ~(LI_FLAG_ANNOUNCEMENT | LI_FLAG_MIX);3P}
	li_codfig_tD;
	[ch_a].flag_tD;
	[ch_b_v]m&= ~LI_FLAG_MONITOR;3Pli_codfig_tD;
	[ch_a].flag_tD;
	[ch_b_s]m&= ~LI_FLAG_MONITOR;3Pli_codfig_tD;
	[ch_b_v].flag_tD;
	[ch_a]m&= ~(LI_FLAG_ANNOUNCEMENT | LI_FLAG_MIX);3Pli_codfig_tD;
	[ch_b_s].flag_tD;
	[ch_a]m&= ~(LI_FLAG_ANNOUNCEMENT | LI_FLAG_MIX);3PT	ifoh_a_v ==Ich_b_v)Em{
lYli_codfig_tD;
	[ch_a_v].flag_tD;
	[ch_b_v]m&= ~LI_FLAG_CONFERENCE;3Pfli_codfig_tD;
	[ch_a_s].flag_tD;
	[ch_b_s]m&= ~LI_FLAG_CONFERENCE;3P}"	[i] Em{
lY
	ifli_codfig_tD;
	[ch_a_v].flag_tD;
	[ch_b_v]m& LI_FLAG_CONFERENCE)
;e_3PTYfcility_req"));
li_total\o
				&s;dipte
-P	_3PTY_
	if
&!= ch_a_v)3tY_fYli_codfig_tD;
	[ch_a_v].flag_tD;
	[i]m&= ~LI_FLAG_CONFERENCE;3Pfm}aPT}	YfT	ifli_codfig_tD;
	[ch_a_s].flag_tD;
	[ch_b_v]m& LI_FLAG_CONFERENCE)
;e_3PTYfcility_req"));
li_total\o
				&s;dipte
-P	_3PTY_
	if
&!= ch_a_s)3tY_fYli_codfig_tD;
	[ch_a_s].flag_tD;
	[i]m&= ~LI_FLAG_CONFERENCE;3Pfm}aPT}	YfT	ifli_codfig_tD;
	[ch_b_v].flag_tD;
	[ch_a_v]m& LI_FLAG_CONFERENCE)
;e_3PTYfcility_req"));
li_total\o
				&s;dipte
-P	_3PTY_
	if
&!= ch_a_v)3tY_fYli_codfig_tD;
	[i].flag_tD;
	[ch_a_v]m&= ~LI_FLAG_CONFERENCE;3Pfm}aPT}	YfT	ifli_codfig_tD;
	[ch_b_v].flag_tD;
	[ch_a_s]m& LI_FLAG_CONFERENCE)
;e_3PTYfcility_req"));
li_total\o
				&s;dipte
-P	_3PTY_
	if
&!= ch_a_s)3tY_fYli_codfig_tD;
	[i].flag_tD;
	[ch_a_s]m&= ~LI_FLAG_CONFERENCE;3Pf	}aPT}	Y}afT	ifli_flagsm& LI_FLAG_CONFERENCE_A_B)Em{
lYli_codfig_tD;
	[ch_b_v].flag_tD;
	[ch_a_v]m|= LI_FLAG_CONFERENCE;3Pfli_codfig_tD;
	[ch_b_s].flag_tD;
	[ch_a_v]m|= LI_FLAG_CONFERENCE;3Pfli_codfig_tD;
	[ch_b_v].flag_tD;
	[ch_a_s]m|= LI_FLAG_CONFERENCE;3Pfli_codfig_tD;
	[ch_b_s].flag_tD;
	[ch_a_s]m|= LI_FLAG_CONFERENCE;3P}afT	ifli_flagsm& LI_FLAG_CONFERENCE_B_A)Em{
lYli_codfig_tD;
	[ch_a_v].flag_tD;
	[ch_b_v]m|= LI_FLAG_CONFERENCE;3Pfli_codfig_tD;
	[ch_a_v].flag_tD;
	[ch_b_s]m|= LI_FLAG_CONFERENCE;3Pfli_codfig_tD;
	[ch_a_s].flag_tD;
	[ch_b_v]m|= LI_FLAG_CONFERENCE;3Pfli_codfig_tD;
	[ch_a_s].flag_tD;
	[ch_b_s]m|= LI_FLAG_CONFERENCE;3P}afT	ifli_flagsm& LI_FLAG_MONITOR_A)Em{
lYli_codfig_tD;
	[ch_a].flag_tD;
	[ch_a_v]m|= LI_FLAG_MONITOR;3Pfli_codfig_tD;
	[ch_a].flag_tD;
	[ch_a_s]m|= LI_FLAG_MONITOR;3P}afT	ifli_flagsm& LI_FLAG_MONITOR_B)Em{
lYli_codfig_tD;
	[ch_a].flag_tD;
	[ch_b_v]m|= LI_FLAG_MONITOR;3Pfli_codfig_tD;
	[ch_a].flag_tD;
	[ch_b_s]m|= LI_FLAG_MONITOR;3P}afT	ifli_flagsm& LI_FLAG_ANNOUNCEMENT_A)Em{
lYli_codfig_tD;
	[ch_a_v].flag_tD;
	[ch_a]m|= LI_FLAG_ANNOUNCEMENT;3Pfli_codfig_tD;
	[ch_a_s].flag_tD;
	[ch_a]m|= LI_FLAG_ANNOUNCEMENT;3P}afT	ifli_flagsm& LI_FLAG_ANNOUNCEMENT_B)Em{
lYli_codfig_tD;
	[ch_b_v].flag_tD;
	[ch_a]m|= LI_FLAG_ANNOUNCEMENT;3Pfli_codfig_tD;
	[ch_b_s].flag_tD;
	[ch_a]m|= LI_FLAG_ANNOUNCEMENT;3P}afT	ifli_flagsm& LI_FLAG_MIX_A)Em{
lYli_codfig_tD;
	[ch_a_v].flag_tD;
	[ch_a]m|= LI_FLAG_MIX;3Pfli_codfig_tD;
	[ch_a_s].flag_tD;
	[ch_a]m|= LI_FLAG_MIX;3P}afT	ifli_flagsm& LI_FLAG_MIX_B)Em{
lYli_codfig_tD;
	[ch_b_v].flag_tD;
	[ch_a]m|= LI_FLAG_MIX;3Pfli_codfig_tD;
	[ch_b_s].flag_tD;
	[ch_a]m|= LI_FLAG_MIX;3P}afT	ifoh_a_v != ch_a_s)3t{
lYli_codfig_tD;
	[ch_a_v].flag_tD;
	[ch_a_s]m|= LI_FLAG_CONFERENCE;3Pfli_codfig_tD;
	[ch_a_s].flag_tD;
	[ch_a_v]m|= LI_FLAG_CONFERENCE;3P}afT	ifoh_b_v != ch_b_s)3t{
lYli_codfig_tD;
	[ch_b_v].flag_tD;
	[ch_b_s]m|= LI_FLAG_CONFERENCE;3Pfli_codfig_tD;
	[ch_b_s].flag_tD;
	[ch_b_v]m|= LI_FLAG_CONFERENCE;3P}a}
sss		 ic;voidAli2_update_ordnectfd_mas Id,lDIVA_CAPI_ADAPTER *a,l		In	*0);
,
-Po       d_mas nDIN_b_id,nby  lordnect, d_mas li_flags)
TE;_masnch_a,nch_a_v,nch_a_s,nch_b,nch_b_v,nch_b_s;3P		In	*0);
_b;E;DIVA_CAPI_ADAPTER *a_b;EfNa_b = &(Crplci [MapCrd w;
lerX(by  )inDIN_b_id & 0x7f))&- 1])("NpDIN_b = &(C_b->pDIN[(inDIN_b_id >> 8)m& 0xff)&- 1])("Nch_am	IC->li_basl + (nDINi-li_b)
				&_idm- 1);3PT	if!C->li_1); B3_XnDINi-t	&	== ADV_VOICE	r-    B3_XnDIN	== C->AdvSignal		In)	B3_XIdmB EXT_CONTROLLER))Em{
lYch_a_v =nch_a + MIXER_IC_CHANNEL_BASE;
lYch_a_sm=IfaINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_SLAVE_CODECe ?r-		C->li_basl + MIXER_IC_CHANNEL_BASEm+a(2m- pDINi-li_b)
				&_id) :Ich_a_v;3P}a	[i] Em{
lYch_a_v =nch_a;
lYch_a_sm=Ich_a;
l}"Nch_b = C_b->li_basl + (nDIN_b->li_b)
				&_idm- 1);3PT	if!C_b->li_1); B3_XnDIN_b->t	&	== ADV_VOICE	r-    B3_XnDIN_b == C_b->AdvSignal		In)	B3_XnDIN_b_id & EXT_CONTROLLER))Em{
lYch_b_v =nch_b + MIXER_IC_CHANNEL_BASE;
lYch_b_sm=Ifa_b->mciufaurol_r_fearol_s & MANUFACTURER_FEATURE_SLAVE_CODECe ?r-		C_b->li_basl + MIXER_IC_CHANNEL_BASEm+a(2m- pDIN_b->li_b)
				&_id) :Ich_b_v;3P}a	[i] Em{
lYch_b_v =nch_b;
lYch_b_sm=Ich_b;
l}
	T	ifordnect)Em{
lYli_codfig_tD;
	[ch_b].flag_tD;
	[ch_b_v]m&= ~LI_FLAG_MONITOR;3PYli_codfig_tD;
	[ch_b].flag_tD;
	[ch_b_s]m&= ~LI_FLAG_MONITOR;3Pfli_codfig_tD;
	[ch_b_v].flag_tD;
	[ch_b]m&= ~LI_FLAG_MIX;3Pfli_codfig_tD;
	[ch_b_s].flag_tD;
	[ch_b]m&= ~LI_FLAG_MIX;3Pfli_codfig_tD;
	[ch_b].flag_tD;
	[ch_b]m&= ~LI_FLAG_PCCONNECTf3	fli_codfig_tD;
	[ch_b].)
flagsm&= ~(LI_CHFLAG_MONITOR | LI_CHFLAG_MIX | LI_CHFLAG_LOOP);3P}
	li_codfig_tD;
	[ch_b_v].flag_tD;
	[ch_a_v]m&= ~(LI_FLAG_INTERCONNECT | LI_FLAG_CONFERENCE);3Pli_codfig_tD;
	[ch_b_s].flag_tD;
	[ch_a_v]m&= ~(LI_FLAG_INTERCONNECT | LI_FLAG_CONFERENCE);3Pli_codfig_tD;
	[ch_b_v].flag_tD;
	[ch_a_s]m&= ~(LI_FLAG_INTERCONNECT | LI_FLAG_CONFERENCE);3Pli_codfig_tD;
	[ch_b_s].flag_tD;
	[ch_a_s]m&= ~(LI_FLAG_INTERCONNECT | LI_FLAG_CONFERENCE);3Pli_codfig_tD;
	[ch_a_v].flag_tD;
	[ch_b_v]m&= ~(LI_FLAG_INTERCONNECT | LI_FLAG_CONFERENCE);3Pli_codfig_tD;
	[ch_a_v].flag_tD;
	[ch_b_s]m&= ~(LI_FLAG_INTERCONNECT | LI_FLAG_CONFERENCE);3Pli_codfig_tD;
	[ch_a_s].flag_tD;
	[ch_b_v]m&= ~(LI_FLAG_INTERCONNECT | LI_FLAG_CONFERENCE);3Pli_codfig_tD;
	[ch_a_s].flag_tD;
	[ch_b_s]m&= ~(LI_FLAG_INTERCONNECT | LI_FLAG_CONFERENCE);3PT	ifli_flagsm& LI2_FLAG_INTERCONNECT_A_B)Em{
lYli_codfig_tD;
	[ch_b_v].flag_tD;
	[ch_a_v]m|= LI_FLAG_INTERCONNECTf3	fli_codfig_tD;
	[ch_b_s].flag_tD;
	[ch_a_v]m|= LI_FLAG_INTERCONNECTf3	fli_codfig_tD;
	[ch_b_v].flag_tD;
	[ch_a_s]m|= LI_FLAG_INTERCONNECTf3	fli_codfig_tD;
	[ch_b_s].flag_tD;
	[ch_a_s]m|= LI_FLAG_INTERCONNECTf3	}afT	ifli_flagsm& LI2_FLAG_INTERCONNECT_B_A)Em{
lYli_codfig_tD;
	[ch_a_v].flag_tD;
	[ch_b_v]m|= LI_FLAG_INTERCONNECTf3	fli_codfig_tD;
	[ch_a_v].flag_tD;
	[ch_b_s]m|= LI_FLAG_INTERCONNECTf3	fli_codfig_tD;
	[ch_a_s].flag_tD;
	[ch_b_v]m|= LI_FLAG_INTERCONNECTf3	fli_codfig_tD;
	[ch_a_s].flag_tD;
	[ch_b_s]m|= LI_FLAG_INTERCONNECTf3	}afT	ifli_flagsm& LI2_FLAG_MONITOR_B)Em{
lYli_codfig_tD;
	[ch_b].flag_tD;
	[ch_b_v]m|= LI_FLAG_MONITOR;3Pfli_codfig_tD;
	[ch_b].flag_tD;
	[ch_b_s]m|= LI_FLAG_MONITOR;3P}afT	ifli_flagsm& LI2_FLAG_MIX_B)Em{
lYli_codfig_tD;
	[ch_b_v].flag_tD;
	[ch_b]m|= LI_FLAG_MIX;3Pfli_codfig_tD;
	[ch_b_s].flag_tD;
	[ch_b]m|= LI_FLAG_MIX;3P}afT	ifli_flagsm& LI2_FLAG_MONITOR_X)3	fli_codfig_tD;
	[ch_b].)
flagsm|= LI_CHFLAG_MONITOR;3PT	ifli_flagsm& LI2_FLAG_MIX_X)3	fli_codfig_tD;
	[ch_b].)
flagsm|= LI_CHFLAG_MIX;3PT	ifli_flagsm& LI2_FLAG_LOOP_B)Em{
lYli_codfig_tD;
	[ch_b_v].flag_tD;
	[ch_b_v]m|= LI_FLAG_INTERCONNECTf3	fli_codfig_tD;
	[ch_b_s].flag_tD;
	[ch_b_v]m|= LI_FLAG_INTERCONNECTf3	fli_codfig_tD;
	[ch_b_v].flag_tD;
	[ch_b_s]m|= LI_FLAG_INTERCONNECTf3	fli_codfig_tD;
	[ch_b_s].flag_tD;
	[ch_b_s]m|= LI_FLAG_INTERCONNECTf3	}afT	ifli_flagsm& LI2_FLAG_LOOP_PC)3	fli_codfig_tD;
	[ch_b].flag_tD;
	[ch_b]m|= LI_FLAG_PCCONNECTf3	T	ifli_flagsm& LI2_FLAG_LOOP_X)3	fli_codfig_tD;
	[ch_b].)
flagsm|= LI_CHFLAG_LOOPf3	T	ifli_flagsm& LI2_FLAG_PCCONNECT_A_B)Emfli_codfig_tD;
	[ch_b_s].flag_tD;
	[ch_a_s]m|= LI_FLAG_PCCONNECTf3	T	ifli_flagsm& LI2_FLAG_PCCONNECT_B_A)Emfli_codfig_tD;
	[ch_a_s].flag_tD;
	[ch_b_s]m|= LI_FLAG_PCCONNECTf3	T	ifoh_a_v != ch_a_s)3t{
lYli_codfig_tD;
	[ch_a_v].flag_tD;
	[ch_a_s]m|= LI_FLAG_CONFERENCE;3Pfli_codfig_tD;
	[ch_a_s].flag_tD;
	[ch_a_v]m|= LI_FLAG_CONFERENCE;3P}afT	ifoh_b_v != ch_b_s)3t{
lYli_codfig_tD;
	[ch_b_v].flag_tD;
	[ch_b_s]m|= LI_FLAG_CONFERENCE;3Pfli_codfig_tD;
	[ch_b_s].flag_tD;
	[ch_b_v]m|= LI_FLAG_CONFERENCE;3P}a}
sss		 ic;_mas li_check_main_nDIN(d_mas Id,l		In	*0);
)
TE	
	if_b3_&== NULL)r-{s e].info + 1);
				[%06lx] %s,%d:AWrongl		In","PTYYUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTbrw", I(_WRONG_IDENTIFIER);3P}afT	if!pDINi-S		 e3P    |a !pDINi-NL.Idm|a pDINi-n
_remove_id3P    |a (nDINi-li_b)
				&_idm	= 0))r-{s e].info + 1);
				[%06lx] %s,%d:AWrongls		 e","PTYYUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTbrw", I(_WRONG_STATE);3P}
	li_codfig_tD;
	[nDINi-Crplci ->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&=IPEND;3tbrw", I(GOOD);3}
sss		 ic;		In	*li_check_pDIN_bfd_mas Id,l		In	*0);
,
-Po     d_mas nDIN_b_id,n_mas nDIN_b_write_pos,nby  l*p_l_sult)
TE	by  lotlr_b;
l		In	*0);
_b;E
	T	if(ynDINi-li_nDIN_b_read_posm> nDIN_b_write_pose ?lpDINi-li_nDIN_b_read_posm:3P     LI_		In_B_QUEUE_ENTRIESm+lpDINi-li_nDIN_b_read_pos)m- pDIN_b_write_posm- 1 < 2	3P{s e].info + 1);
				[%06lx] %s,%d:ALI requ		r overrun","PTYYUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTPU		}
			p_l_sult, _REQUESn_NOT_ALLOWED_IN_THIS_STATE);3Ptbrw", I(NULL);
l}"Nctlr_b	= 0f3	T	if(nDIN_b_id & 0x7f)m!= 0)r-{
lYctlr_b	= MapCrd w;
lerX(by  )inDIN_b_id & 0x7f));	YfT	if(ctlr_b	> max_Crplci ) |a ((ctlr_b	!= 0) B3_XCdplci [ctlr_b	- 1].requ		r == NULL)))3tY_ctlr_b	= 0f3	}
	T	if(ctlr_b	== 0)3P    |a ((inDIN_b_id >> 8)m& 0xff)&== 0)3P    |a ((inDIN_b_id >> 8)m& 0xff)&> Cdplci [ctlr_b	- 1].max_PEND		r-{s e].info + 1);
				[%06lx] %s,%d:ALI invalid secoddl		In	%08lx","PTYYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lnDIN_b_id));3PTPU		}
			p_l_sult, _WRONG_IDENTIFIER);3Ptbrw", I(NULL);
l}"NpDIN_b = &(Cdplci [ctlr_b	- 1].pDIN[(inDIN_b_id >> 8)m& 0xff)&- 1])("NT	if!pDIN_b->S		 e3P    |a !pDIN_b->NL.Idm|a pDIN_b->n
_remove_id3P    |a (nDIN_b->li_b)
				&_idm	= 0))r-{s e].info + 1);
				[%06lx] %s,%d:ALI peer in wrongls		 e	%08lx","PTYYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lnDIN_b_id));3PTPU		}
			p_l_sult, _REQUESn_NOT_ALLOWED_IN_THIS_STATE);3Ptbrw", I(NULL);
l}"Nli_codfig_tD;
	[nDIN_b->Crplci ->li_basl + (nDIN_b->li_b)
				&_idm- 1)].0);
&=IPEND_b;
lT	if(yby  )inDIN_b_id & ~EXT_CONTROLLER))	!=3P    (yby  )iUnMapCrd w;
lerXnDINi-Crplci G)Ide & ~EXT_CONTROLLER))r-    B3_X!XnDINi-Crplci G)mciufaurol_r_fearol_s & MANUFACTURER_FEATURE_XCONNECT)r-P|a !ynDIN_b->Crplci ->mciufaurol_r_fearol_s & MANUFACTURER_FEATURE_XCONNECT)))r-{s e].info + 1);
				[%06lx] %s,%d:ALI nor on sam lotrl	%08lx","PTYYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lnDIN_b_id));3PTPU		}
			p_l_sult, _WRONG_IDENTIFIER);3Ptbrw", I(NULL);
l}"N
	if!fget_b1_faciliti_s(0);
_b,nCdd_b1_faciliti_s(0);
_b,nnDIN_b->B1_l_source,
YPTT	Pf  (_mask(nDIN_b->B1_faciliti_s | B1_FACILITY_MIXER))) & B1_FACILITY_MIXER))r-{s e].info + 1);
				[%06lx] %s,%d:AI
	erordnectIpeer c			or mix %d","PTYYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lnDIN_b->B1_l_source));3PTPU		}
			p_l_sult, _REQUESn_NOT_ALLOWED_IN_THIS_STATE);3Ptbrw", I(NULL);
l}"Nbrw", I(nDIN_b);3}
sss		 ic;		In	*li2_check_pDIN_bfd_mas Id,l		In	*0);
,
-Po      d_mas nDIN_b_id,n_mas nDIN_b_write_pos,nby  l*p_l_sult)
TE	by  lotlr_b;
l		In	*0);
_b;E
	T	if(ynDINi-li_nDIN_b_read_posm> nDIN_b_write_pose ?lpDINi-li_nDIN_b_read_posm:3P     LI_		In_B_QUEUE_ENTRIESm+lpDINi-li_nDIN_b_read_pos)m- pDIN_b_write_posm- 1 < 2	3P{s e].info + 1);
				[%06lx] %s,%d:ALI requ		r overrun","PTYYUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTPU		}
			p_l_sult, _WRONG_STATE);3Ptbrw", I(NULL);
l}"Nctlr_b	= 0f3	T	if(nDIN_b_id & 0x7f)m!= 0)r-{
lYctlr_b	= MapCrd w;
lerX(by  )inDIN_b_id & 0x7f));	YfT	if(ctlr_b	> max_Crplci ) |a ((ctlr_b	!= 0) B3_XCdplci [ctlr_b	- 1].requ		r == NULL)))3tY_ctlr_b	= 0f3	}
	T	if(ctlr_b	== 0)3P    |a ((inDIN_b_id >> 8)m& 0xff)&== 0)3P    |a ((inDIN_b_id >> 8)m& 0xff)&> Cdplci [ctlr_b	- 1].max_PEND		r-{s e].info + 1);
				[%06lx] %s,%d:ALI invalid secoddl		In	%08lx","PTYYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lnDIN_b_id));3PTPU		}
			p_l_sult, _WRONG_IDENTIFIER);3Ptbrw", I(NULL);
l}"NpDIN_b = &(Cdplci [ctlr_b	- 1].pDIN[(inDIN_b_id >> 8)m& 0xff)&- 1])("NT	if!pDIN_b->S		 e3P    |a !pDIN_b->NL.Idm|a pDIN_b->n
_remove_id3P    |a (nDIN_b->li_b)
				&_idm	= 0)3P    |a (li_codfig_tD;
	[nDIN_b->Crplci ->li_basl + (nDIN_b->li_b)
				&_idm- 1)].0);
&!=IPEND_b))r-{s e].info + 1);
				[%06lx] %s,%d:ALI peer in wrongls		 e	%08lx","PTYYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lnDIN_b_id));3PTPU		}
			p_l_sult, _WRONG_STATE);3Ptbrw", I(NULL);
l}"NT	if(yby  )inDIN_b_id & ~EXT_CONTROLLER))	!=3P    (yby  )iUnMapCrd w;
lerXnDINi-Crplci G)Ide & ~EXT_CONTROLLER))r-    B3_X!XnDINi-Crplci G)mciufaurol_r_fearol_s & MANUFACTURER_FEATURE_XCONNECT)r-P|a !ynDIN_b->Crplci ->mciufaurol_r_fearol_s & MANUFACTURER_FEATURE_XCONNECT)))r-{s e].info + 1);
				[%06lx] %s,%d:ALI nor on sam lotrl	%08lx","PTYYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lnDIN_b_id));3PTPU		}
			p_l_sult, _WRONG_IDENTIFIER);3Ptbrw", I(NULL);
l}"N
	if!fget_b1_faciliti_s(0);
_b,nCdd_b1_faciliti_s(0);
_b,nnDIN_b->B1_l_source,
YPTT	Pf  (_mask(nDIN_b->B1_faciliti_s | B1_FACILITY_MIXER))) & B1_FACILITY_MIXER))r-{s e].info + 1);
				[%06lx] %s,%d:AI
	erordnectIpeer c			or mix %d","PTYYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lnDIN_b->B1_l_source));3PTPU		}
			p_l_sult, _WRONG_STATE);3Ptbrw", I(NULL);
l}"Nbrw", I(nDIN_b);3}
sss		 ic;by  lmixer_l_qu		r(d_mas Id,l_mas Number,lDIVA_CAPI_ADAPTER *a,l		In	*0);
, APPL   *appl, API_PARSE *msg)
TE;_masnInfoPE;_masnNPE;d_mas d,lli_flags,lnDIN_b_id;
l		In	*0);
_b;E	API_PARSE li_narms[3];E	API_PARSE li_req_parms[3];E	API_PARSE li_par icipant_struct[2];E	API_PARSE li_par icipant_parms[3];E	_mas nar icipant_parms_posf3Pby  ll_sult_buffer[32];E	by  l*l_sult;E	_mas l_sult_posf3P_mas nDIN_b_write_posPEEN].info + 1);
				[%06lx] %s,%d:Amixer_l_qu		r","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__	)("fNInfo = GOODf3	l_sult = l_sult_bufferf3	l_sult_buffer[0]	= 0f3	T	if!fC->profile.Global\Options & GL_LINE_INTERCONNECT_SUPPORTEDe)r-{s e].info + 1);
				[%06lx] %s,%d:AFacility nor supported","PTYYUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTInfo = _FACILITY_NOT_SUPPORTED;3t}a	[i] mT	ifapi_parse(&msg[1].info[1], msg[1].li = 1, "ws", li_narms))r-{s e].info + 1);
				[%06lx] %s,%d:AWronglmessage format","PTYYUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTInfo = _WRONG_MESSAGE_FORMAT;3P}a	[i] Em{
lYl_sult_buffer[0]	= 3;3PTPU		}
			&l_sult_buffer[1], GE		}
			li_narms[0].info));3PTbrsult_buffer[3]m=I0f3	fswitch (GE		}
			li_narms[0].info))
-P{r-	casl LI_GE		SUPPORTED_SERVICES:3P		
	ifapplG)appl_flagsm& APPL_FLAG_OLD_LI_SPECer--P_3PTY_l_sult_buffer[0]	= 17;r-	PTbrsult_buffer[3]m=I14 fND;	PU		}
			&l_sult_buffer[4], GOOD);3TY_f]m=I0f3	fY_
	ifaINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_MIXER_CH_CH)3tY_fYdm|= LI_CONFERENCING_SUPPORTED;3tY_f
	ifaINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_MIXER_CH_PC)3tY_fYdm|= LI_MONITORING_SUPPORTED;3tY_f
	ifaINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_MIXER_PC_CH)3tY_fYdm|= LI_ANNOUNCEMENTS_SUPPORTED | LI_MIXING_SUPPORTED;3tY_f
	ifaINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_XCONNECT)r-PffYdm|= LI_CROSS_CONTROLLER_SUPPORTED;3tY_fPU		D}
			&l_sult_buffer[6], d)f3	f_f
	ifaINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_XCONNECT)r-Pff{r-PffYdm=I0f3	fY_Efcility_req"));
li_total\o
				&s;dipte
-P	;N_3PTY_EET	if(li_codfig_tD;
	[i].Crplci G)mciufaurol_r_fearol_s & MANUFACTURER_FEATURE_XCONNECT)r-P				    B3_Xli_codfig_tD;
	[i].Crplci G)li_1);
YPTT	Pf|a ());
li_codfig_tD;
	[i].Crplci G)li_basl + MIXER_BCHANNELS_BRI)	er-Pf;eN_3PTY_EEfd++;
SArNNN}3PTY_f}3PTY_}3PTY_[i] r-fo	{r-PffYdm=IC->li_1); ?IC->li_o
				&sm: MIXER_BCHANNELS_BRI;3PTY_}3PTY_PU		D}
			&l_sult_buffer[10], d / 2)("Nff_PU		D}
			&l_sult_buffer[14], d)f3	f_}aPTY[i] r--;_3PTY_l_sult_buffer[0]	= 25;r-	PTbrsult_buffer[3]m=I22 fND;	PU		}
			&l_sult_buffer[4], GOOD);3TY_f]m=ILI2_ASYMMETRIC_SUPPORTED | LI2_B_LOOPING_SUPPORTED | LI2_X_LOOPING_SUPPORTED;3tY_f
	ifaINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_MIXER_CH_PC)3tY_fYdm|= LI2_MONITORING_SUPPORTED | LI2_REMOTE_MONITORING_SUPPORTED;3tY_f
	ifaINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_MIXER_PC_CH)3tY_fYdm|= LI2_MIXING_SUPPORTED | LI2_REMOTE_MIXING_SUPPORTED;3tY_f
	ifaINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_MIXER_PC_PC)3tY_fYdm|= LI2_PC_LOOPING_SUPPORTED;3tY_f
	ifaINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_XCONNECT)r-PffYdm|= LI2_CROSS_CONTROLLER_SUPPORTED;3tY_fPU		D}
			&l_sult_buffer[6], d)f3	f_fdm=IC->li_1); ?IC->li_o
				&sm: MIXER_BCHANNELS_BRI;3PTY_PU		D}
			&l_sult_buffer[10], d / 2)("Nff_PU		D}
			&l_sult_buffer[14], dm- 1);3PfPf
	ifaINmciufaurol_r_fearol_s & MANUFACTURER_FEATURE_XCONNECT)r-Pff{r-PffYdm=I0f3	fY_Efcility_req"));
li_total\o
				&s;dipte
-P	;N_3PTY_EET	if(li_codfig_tD;
	[i].Crplci G)mciufaurol_r_fearol_s & MANUFACTURER_FEATURE_XCONNECT)r-P				    B3_Xli_codfig_tD;
	[i].Crplci G)li_1);
YPTT	Pf|a ());
li_codfig_tD;
	[i].Crplci G)li_basl + MIXER_BCHANNELS_BRI)	er-Pf;eN_3PTY_EEfd++;
SArNNN}3PTY_f}3PTY_}3PTY_PU		D}
			&l_sult_buffer[18], d / 2)("Nff_PU		D}
			&l_sult_buffer[22], dm- 1);3PfP}
Y_fbreakf3r-	casl LI_REQ_CONNECT:s YfT	ifli_narms[1].li = 1m	= 8er--P_3PTY_applG)appl_flagsm|= APPL_FLAG_OLD_LI_SPEC;3PfPf
	ifapi_parse(&li_narms[1].info[1], li_narms[1].li = 1, "dd", li_req_parms))
;eNf_3PTY_f].info + 1);
				[%06lx] %s,%d:AWronglmessage format","PTYY	TTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNfInfo = _WRONG_MESSAGE_FORMAT;3P		Nfbreakf3	fY_}3PTY	nDIN_b_id = GE		D}
			li_req_parms[0].info)m& 0xfffff3	f_	li_flagsm= GE		D}
			li_req_parms[1].info);3PTYNInfo = li_check_main_nDIN(Id,l0);
);3t		tl_sult_buffer[0]	= 9;r-	PTbrsult_buffer[3]m=I6("Nff_PU		D}
			&l_sult_buffer[4], nDIN_b_id) fND;	PU		}
			&l_sult_buffer[8], GOOD);3TY_f
	ifInfo != GOOD)r-Pfffbreakf3	fY_l_sult = nDINi-saved_msg.infof3	fY_fcility_req"));= l_sult_buffer[0];dipte
-P	;Nl_sult[i]m=Il_sult_buffer[i];aPTYlpDIN_b_write_posm=InDINi-li_nDIN_b_write_posf3P		NpDIN_b = li_check_pDIN_bfId,l0);
,lnDIN_b_id,nnDIN_b_write_pos,n&l_sult[8]);3TY_f
	ifnDIN_b == NULL)r-Pfffbreakf3	fY_li_update_ordnectfId,la,l0);
,lnDIN_b_id,ntru ,lli_flags);3tY_fnDINi-li_nDIN_b_queu	[nDIN_b_write_pos]&=IPEND_b_id | LI_		In_B_LAST_FLAG;aPTYlpDIN_b_write_posm=I(pDIN_b_write_posm== LI_		In_B_QUEUE_ENTRIESm- 1)	? 0 :I_b3__b_write_posm+ 1("PTmrnDINi-li_nDIN_b_write_pos&=IPEND_b_write_posf3P		}aPTY[i] r--;_3PTY_applG)appl_flagsm&= ~APPL_FLAG_OLD_LI_SPEC;3PfPf
	ifapi_parse(&li_narms[1].info[1], li_narms[1].li = 1, "ds", li_req_parms))
;eNf_3PTY_f].info + 1);
				[%06lx] %s,%d:AWronglmessage format","PTYY	TTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNfInfo = _WRONG_MESSAGE_FORMAT;3P		Nfbreakf3	fY_}3PTY	li_flagsm= GE		D}
			li_req_parms[0].info)m& ~(LI2_FLAG_INTERCONNECT_A_B | LI2_FLAG_INTERCONNECT_B_A);3PTYNInfo = li_check_main_nDIN(Id,l0);
);3t		tl_sult_buffer[0]	= 7;r-	PTbrsult_buffer[3]m=I4 fND;	PU		}
			&l_sult_buffer[4], Info);3PTYNl_sult_buffer[6]m=I0f3	fY_
	ifInfo != GOOD)r-Pfffbreakf3	fY_l_sult = nDINi-saved_msg.infof3	fY_fcility_req"));= l_sult_buffer[0];dipte
-P	;Nl_sult[i]m=Il_sult_buffer[i];aPTYlpDIN_b_write_posm=InDINi-li_nDIN_b_write_posf3P		Npar icipant_parms_posm=I0f3	fY_l_sult_pos	= 7;r-	PTli2_update_ordnectfId,la,l0);
,lUnMapId(Ide, tru ,lli_flags);3tY_fwhilemynar icipant_parms_posm< li_req_parms[1].li = 1)"PTmr{
-P	;Nl_sult[l_sult_pos]m=I6("Nff__l_sult_pos	+= 7;r-	PT_PU		D}
			&l_sult[l_sult_posm- 6], 0);r-	PT_PU		}
			&l_sult[l_sult_posm- 2], GOOD);3TY_ff
	ifapi_parse(&li_req_parms[1].info[1m+lpar icipant_parms_pos],"PTYY	T      (_mask(li_narms[1].li = 1m-lpar icipant_parms_pose, "s", li_nar icipant_struct)e
-P	;N_3PTY_EE].info + 1);
				[%06lx] %s,%d:AWronglmessage format","PTYY	TTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNf_PU		}
			&l_sult[l_sult_posm- 2], _WRONG_MESSAGE_FORMAT);3PTYNf_breakf3	fY_N}3PTY_f
	ifapi_parse(&li_nar icipant_struct[0].info[1],"PTYY	T      li_nar icipant_struct[0].li = 1, "dd", li_par icipant_parms)e
-P	;N_3PTY_EE].info + 1);
				[%06lx] %s,%d:AWronglmessage format","PTYY	TTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNf_PU		}
			&l_sult[l_sult_posm- 2], _WRONG_MESSAGE_FORMAT);3PTYNf_breakf3	fY_N}3PTY_fnDIN_b_id = GE		D}
			li_par icipant_parms[0].info)m& 0xfffff3	f_		li_flagsm= GE		D}
			li_par icipant_parms[1].info);3PTYN_PU		D}
			&l_sult[l_sult_posm- 6], nDIN_b_id) fND;	f
	ifsizeof(l_sult)m- l_sult_posm< 7e
-P	;N_3PTY_EE].info + 1);
				[%06lx] %s,%d:ALI result overrun","PTYY	TTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNf_PU		}
			&l_sult[l_sult_posm- 2], _WRONG_STATE);3PtYNf_breakf3	fY_N}3PTY_fnDIN_b = li2_check_pDIN_bfId,l0);
,lnDIN_b_id,nnDIN_b_write_pos,n&l_sult[l_sult_posm- 2]);3PTYNf
	if_b3__b	!= NULL)r-Pfff_3PTY_EEli2_update_ordnectfId,la,l0);
,lnDIN_b_id,ntru ,lli_flags);3tY_f_fnDINi-li_nDIN_b_queu	[nDIN_b_write_pos]&=IPEND_b_id |"PTYY	TT(fli_flagsm& (LI2_FLAG_INTERCONNECT_A_B | LI2_FLAG_INTERCONNECT_B_A |"PTYPTT	P      LI2_FLAG_PCCONNECT_A_B | LI2_FLAG_PCCONNECT_B_A))	? 0 :ILI_		In_B_DISC_FLAGf;3tY_f_fnDIN_b_write_posm=I(pDIN_b_write_posm== LI_		In_B_QUEUE_ENTRIESm- 1)	? 0 :I_b3__b_write_posm+ 1("PTmrN}3PTY_fnar icipant_parms_posm=I(_mask((&li_nar icipant_struct[0].info[1m+ li_nar icipant_struct[0].li = 1])m-"PTYPTT	P       (&li_req_parms[1].info[1]));3PTYN}3	fY_l_sult[0]	= yby  )il_sult_posm- 1);3PTYNl_sult[3]m=Iyby  )il_sult_posm- 4);3PTYNl_sult[6]m=Iyby  )il_sult_posm- 7);3TY_f
m=I(pDIN_b_write_posm== 0) ? LI_		In_B_QUEUE_ENTRIESm- 1 :I_b3__b_write_posm- 1("PTmrT	if(nDIN_b_write_posm== pDINi-li_nDIN_b_read_pos)
TT	P    |a (nDINi-li_nDIN_b_queu	[i]m& LI_		In_B_LAST_FLAGf)"PTmr{
-P	;NnDINi-li_nDIN_b_queu	[nDIN_b_write_pos]&=ILI_		In_B_SKIP_FLAG | LI_		In_B_LAST_FLAG;aPTYlfnDIN_b_write_posm=I(pDIN_b_write_posm== LI_		In_B_QUEUE_ENTRIESm- 1)	? 0 :I_b3__b_write_posm+ 1("PTmr}3PTY_[i] r-fo	NnDINi-li_nDIN_b_queu	[i]m|= LI_		In_B_LAST_FLAG;aPTYlpDINi-li_nDIN_b_write_pos&=IPEND_b_write_posf3P		}aPTYmixer_calculate_orefs(a);3PTYnDINi-li_o
				&_bits =
li_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].)
				&;aPTYmixer_notify_update(0);
,ntru );3PTYsendf(appl, _FACILITY_R | CONFIRM, Idm& 0xffffL, Number,
T	P      "wwS", Info, SELECTOR_LINE_INTERCONNECT, result);3PTYnDINi-ormmci-	= 0f3	ffnDINi-li_cmdm= GE		}
			li_narms[0].info);3PTYstar _;
	ernal\ormmci-fId,l0);
,lmixer_crmmci-);3PTYbrw", I(fai] )f3r-	casl LI_REQ_DISCONNECT:s YfT	ifli_narms[1].li = 1m	= 4er--P_3PTY_applG)appl_flagsm|= APPL_FLAG_OLD_LI_SPEC;3PfPf
	ifapi_parse(&li_narms[1].info[1], li_narms[1].li = 1, "d", li_req_parms))
;eNf_3PTY_f].info + 1);
				[%06lx] %s,%d:AWronglmessage format","PTYY	TTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNfInfo = _WRONG_MESSAGE_FORMAT;3P		Nfbreakf3	fY_}3PTY	nDIN_b_id = GE		D}
			li_req_parms[0].info)m& 0xfffff3	f_	Info = li_check_main_nDIN(Id,l0);
);3t		tl_sult_buffer[0]	= 9;r-	PTbrsult_buffer[3]m=I6("Nff_PU		D}
			&l_sult_buffer[4], GE		D}
			li_req_parms[0].info)) fND;	PU		}
			&l_sult_buffer[8], GOOD);3TY_f
	ifInfo != GOOD)r-Pfffbreakf3	fY_l_sult = nDINi-saved_msg.infof3	fY_fcility_req"));= l_sult_buffer[0];dipte
-P	;Nl_sult[i]m=Il_sult_buffer[i];aPTYlpDIN_b_write_posm=InDINi-li_nDIN_b_write_posf3P		NpDIN_b = li_check_pDIN_bfId,l0);
,lnDIN_b_id,nnDIN_b_write_pos,n&l_sult[8]);3TY_f
	ifnDIN_b == NULL)r-Pfffbreakf3	fY_li_update_ordnectfId,la,l0);
,lnDIN_b_id,nfai] , 0);r-	PTnDINi-li_nDIN_b_queu	[nDIN_b_write_pos]&=IPEND_b_id | LI_		In_B_DISC_FLAG | LI_		In_B_LAST_FLAG;aPTYlpDIN_b_write_posm=I(pDIN_b_write_posm== LI_		In_B_QUEUE_ENTRIESm- 1)	? 0 :I_b3__b_write_posm+ 1("PTmrnDINi-li_nDIN_b_write_pos&=IPEND_b_write_posf3P		}aPTY[i] r--;_3PTY_applG)appl_flagsm&= ~APPL_FLAG_OLD_LI_SPEC;3PfPf
	ifapi_parse(&li_narms[1].info[1], li_narms[1].li = 1, "s", li_req_parms))
;eNf_3PTY_f].info + 1);
				[%06lx] %s,%d:AWronglmessage format","PTYY	TTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNfInfo = _WRONG_MESSAGE_FORMAT;3P		Nfbreakf3	fY_}3PTY	Info = li_check_main_nDIN(Id,l0);
);3t		tl_sult_buffer[0]	= 7;r-	PTbrsult_buffer[3]m=I4 fND;	PU		}
			&l_sult_buffer[4], Info);3PTYNl_sult_buffer[6]m=I0f3	fY_
	ifInfo != GOOD)r-Pfffbreakf3	fY_l_sult = nDINi-saved_msg.infof3	fY_fcility_req"));= l_sult_buffer[0];dipte
-P	;Nl_sult[i]m=Il_sult_buffer[i];aPTYlpDIN_b_write_posm=InDINi-li_nDIN_b_write_posf3P		Npar icipant_parms_posm=I0f3	fY_l_sult_pos	= 7;r-	PTwhilemynar icipant_parms_posm< li_req_parms[0].li = 1)"PTmr{
-P	;Nl_sult[l_sult_pos]m=I6("Nff__l_sult_pos	+= 7;r-	PT_PU		D}
			&l_sult[l_sult_posm- 6], 0);r-	PT_PU		}
			&l_sult[l_sult_posm- 2], GOOD);3TY_ff
	ifapi_parse(&li_req_parms[0].info[1m+lpar icipant_parms_pos],"PTYY	T      (_mask(li_narms[1].li = 1m-lpar icipant_parms_pose, "s", li_nar icipant_struct)e
-P	;N_3PTY_EE].info + 1);
				[%06lx] %s,%d:AWronglmessage format","PTYY	TTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNf_PU		}
			&l_sult[l_sult_posm- 2], _WRONG_MESSAGE_FORMAT);3PTYNf_breakf3	fY_N}3PTY_f
	ifapi_parse(&li_nar icipant_struct[0].info[1],"PTYY	T      li_nar icipant_struct[0].li = 1, "d", li_par icipant_parms)e
-P	;N_3PTY_EE].info + 1);
				[%06lx] %s,%d:AWronglmessage format","PTYY	TTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNf_PU		}
			&l_sult[l_sult_posm- 2], _WRONG_MESSAGE_FORMAT);3PTYNf_breakf3	fY_N}3PTY_fnDIN_b_id = GE		D}
			li_par icipant_parms[0].info)m& 0xfffff3	f_		PU		D}
			&l_sult[l_sult_posm- 6], nDIN_b_id) fND;	f
	ifsizeof(l_sult)m- l_sult_posm< 7e
-P	;N_3PTY_EE].info + 1);
				[%06lx] %s,%d:ALI result overrun","PTYY	TTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNf_PU		}
			&l_sult[l_sult_posm- 2], _WRONG_STATE);3PtYNf_breakf3	fY_N}3PTY_fnDIN_b = li2_check_pDIN_bfId,l0);
,lnDIN_b_id,nnDIN_b_write_pos,n&l_sult[l_sult_posm- 2]);3PTYNf
	if_b3__b	!= NULL)r-Pfff_3PTY_EEli2_update_ordnectfId,la,l0);
,lnDIN_b_id,nfai] , 0);r-	PTPTnDINi-li_nDIN_b_queu	[nDIN_b_write_pos]&=IPEND_b_id | LI_		In_B_DISC_FLAG;3tY_f_fnDIN_b_write_posm=I(pDIN_b_write_posm== LI_		In_B_QUEUE_ENTRIESm- 1)	? 0 :I_b3__b_write_posm+ 1("PTmrN}3PTY_fnar icipant_parms_posm=I(_mask((&li_nar icipant_struct[0].info[1m+ li_nar icipant_struct[0].li = 1])m-"PTYPTT	P       (&li_req_parms[0].info[1]));3PTYN}3	fY_l_sult[0]	= yby  )il_sult_posm- 1);3PTYNl_sult[3]m=Iyby  )il_sult_posm- 4);3PTYNl_sult[6]m=Iyby  )il_sult_posm- 7);3TY_f
m=I(pDIN_b_write_posm== 0) ? LI_		In_B_QUEUE_ENTRIESm- 1 :I_b3__b_write_posm- 1("PTmrT	if(nDIN_b_write_posm== pDINi-li_nDIN_b_read_pos)
TT	P    |a (nDINi-li_nDIN_b_queu	[i]m& LI_		In_B_LAST_FLAGf)"PTmr{
-P	;NnDINi-li_nDIN_b_queu	[nDIN_b_write_pos]&=ILI_		In_B_SKIP_FLAG | LI_		In_B_LAST_FLAG;aPTYlfnDIN_b_write_posm=I(pDIN_b_write_posm== LI_		In_B_QUEUE_ENTRIESm- 1)	? 0 :I_b3__b_write_posm+ 1("PTmr}3PTY_[i] r-fo	NnDINi-li_nDIN_b_queu	[i]m|= LI_		In_B_LAST_FLAG;aPTYlpDINi-li_nDIN_b_write_pos&=IPEND_b_write_posf3P		}aPTYmixer_calculate_orefs(a);3PTYnDINi-li_o
				&_bits =
li_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].)
				&;aPTYmixer_notify_update(0);
,ntru );3PTYsendf(appl, _FACILITY_R | CONFIRM, Idm& 0xffffL, Number,
T	P      "wwS", Info, SELECTOR_LINE_INTERCONNECT, result);3PTYnDINi-ormmci-	= 0f3	ffnDINi-li_cmdm= GE		}
			li_narms[0].info);3PTYstar _;
	ernal\ormmci-fId,l0);
,lmixer_crmmci-);3PTYbrw", I(fai] )f3r-	casl LI_REQ_SILENT_UPLATE:s eNT	if!pDIN |a !pDINi-S		 e3P	P    |a !pDINi-NL.Idm|a pDINi-n
_remove_id3P	P    |a (nDINi-li_b)
				&_idm	= 0)3P	P    |a (li_codfig_tD;
	[nDINi-Crplci ->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&!= PEND		r--;_3PTY_d.info + 1);
				[%06lx] %s,%d:AWrongls		 e","PTYYTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNbrw", I(fai] )f3P		}aPTYpDIN_b_write_posm=InDINi-li_nDIN_b_write_posf3P		T	if(ynDINi-li_nDIN_b_read_posm> nDIN_b_write_pose ?lpDINi-li_nDIN_b_read_posm:3P	P     LI_		In_B_QUEUE_ENTRIESm+lpDINi-li_nDIN_b_read_pos)m- pDIN_b_write_posm- 1 < 2	3P-;_3PTY_d.info + 1);
				[%06lx] %s,%d:ALI requ		r overrun","PTYYTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNbrw", I(fai] )f3P		}aPTY
m=I(pDIN_b_write_posm== 0) ? LI_		In_B_QUEUE_ENTRIESm- 1 :I_b3__b_write_posm- 1("PTmT	if(nDIN_b_write_posm== pDINi-li_nDIN_b_read_pos)
TT	    |a (nDINi-li_nDIN_b_queu	[i]m& LI_		In_B_LAST_FLAGf)"PTm{r-	PTnDINi-li_nDIN_b_queu	[nDIN_b_write_pos]&=ILI_		In_B_SKIP_FLAG | LI_		In_B_LAST_FLAG;aPTYlnDIN_b_write_posm=I(pDIN_b_write_posm== LI_		In_B_QUEUE_ENTRIESm- 1)	? 0 :I_b3__b_write_posm+ 1("PTm}aPTY[i] r--;NnDINi-li_nDIN_b_queu	[i]m|= LI_		In_B_LAST_FLAG;aPTYpDINi-li_nDIN_b_write_pos&=IPEND_b_write_posf3P		nDINi-li_o
				&_bits =
li_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].)
				&;aPTYnDINi-ormmci-	= 0f3	ffnDINi-li_cmdm= GE		}
			li_narms[0].info);3PTYstar _;
	ernal\ormmci-fId,l0);
,lmixer_crmmci-);3PTYbrw", I(fai] )f3r-	default:3P	Pd.info + 1);
				[%06lx] %s,%d:ALI unknown requ		r %04x","PTYYYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lGE		}
			li_narms[0].info)));3PTYInfo = _FACILITY_NOT_SUPPORTED;3tT}	Y}afsendf(appl, _FACILITY_R | CONFIRM, Idm& 0xffffL, Number,
T      "wwS", Info, SELECTOR_LINE_INTERCONNECT, result);3Pbrw", I(fai] )f3}
sss		 ic;voidAmixer_indication_orefs_set(d_mas Id,l		In	*0);
)
TE	d_mas d;3Pby  ll_sult[12];EEN].info + 1);
				[%06lx] %s,%d:Amixer_indication_orefs_set","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__	)("fN
	if_b3_i-li_nDIN_b_read_posm!= nDINi-li_nDIN_b_req_pos)3P{r-	]or-P{r-	Pdm=InDINi-li_nDIN_b_queu	[nDINi-li_nDIN_b_read_pos]("PTmT	if!(dm& LI_		In_B_SKIP_FLAGee
-Po_3PTY_
	ifnDINi-CpplG)appl_flagsm& APPL_FLAG_OLD_LI_SPECer--Pr{
-P	;N
	ifdm& LI_		In_B_DISC_FLAGfr-Pfff_3PTY_EEl_sult[0]	= 5;3PTYNf_PU		}
			&l_sult[1], LI_IND_DISCONNECT);r-	PTPTl_sult[3]m=I2;3PTYNf_PU		}
			&l_sult[4], _LI_USER_INITIATEDe("PTmrN}3PTY_f[i] r-fo	N_3PTY_EEl_sult[0]	= 7;3PTYNf_PU		}
			&l_sult[1], LI_IND_CONNECT_ACTIVE);r-	PTPTl_sult[3]m=I4;3PTYNf_PU		D}
			&l_sult[4], d & ~LI_		In_B_FLAG_MASKe("PTmrN}3PTY_}3PTY_[i] r-fo	{r-PffY
	ifdm& LI_		In_B_DISC_FLAGfr-Pfff_3PTY_EEl_sult[0]	= 9;3PTYNf_PU		}
			&l_sult[1], LI_IND_DISCONNECT);r-	PTPTl_sult[3]m=I6;3PTYNf_PU		D}
			&l_sult[4], d & ~LI_		In_B_FLAG_MASKe("PTmrN_PU		}
			&l_sult[8], _LI_USER_INITIATEDe("PTmrN}3PTY_f[i] r-fo	N_3PTY_EEl_sult[0]	= 7;3PTYNf_PU		}
			&l_sult[1], LI_IND_CONNECT_ACTIVE);r-	PTPTl_sult[3]m=I4;3PTYNf_PU		D}
			&l_sult[4], d & ~LI_		In_B_FLAG_MASKe("PTmrN}3PTY_}3PTY_sendf(nDINi-Cppl, _FACILITY_I, Idm& 0xffffL, 0,
TT	P      "ws", SELECTOR_LINE_INTERCONNECT, result);3PTY}aPTYpDINi-li_nDIN_b_read_posm=if_b3_i-li_nDIN_b_read_posm== LI_		In_B_QUEUE_ENTRIESm- 1)	?
TT	P0 :I_b3_i-li_nDIN_b_read_posm+ 1("PT} whilemy!(dm& LI_		In_B_LAST_FLAGf B3_XnDINi-li_nDIN_b_read_posm!= nDINi-li_nDIN_b_req_pos));
l}"}
sss		 ic;voidAmixer_indication_xordnect_fromfd_mas Id,l		In	*0);
,nby  l*msg,l_mas li = 1)"{E;_masnN,nj,nch;
lstruct xordnect_transfer_address_s s,   *p;E;DIVA_CAPI_ADAPTER *a;EEN].info + 1);
				[%06lx] %s,%d:Amixer_indication_xordnect_from %d","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,l(;
	)li = 1))("fNam=InDINi-Crplci ;E;
m=I1("Pfcility_r1q"));
li = 1q"))+_r16)3P{r-	s.card_address.lowy_rmsg[i]m| (msg[im+ 1] << 8)ma ((id_mask(msg[im+ 2])) << 16)ma ((id_mask(msg[im+ 3])) << 24);3PTs.card_address.highy_rmsg[im+ 4]m| (msg[im+ 5] << 8)ma ((id_mask(msg[im+ 6])) << 16)ma ((id_mask(msg[im+ 7])) << 24);3PTs.offsety_rmsg[im+ 8]m| (msg[im+ 9] << 8)ma ((id_mask(msg[im+ 10])) << 16)ma ((id_mask(msg[im+ 11])) << 24);3PTchy_rmsg[im+ 12]m| (msg[im+ 13] << 8);3PTj = chm& XCONNECT_CHANNEL_NUMBER_MASK;3PTT	if!C->li_1); B3_XnDINi-li_b)
				&_idm	= 2ee
-Poj = 1m- j;3PTj +	IC->li_basl;3PTT	ifchm& XCONNECT_CHANNEL_PORT_PC)3tY_p = &(li_codfig_tD;
	[j].send_pc);3PT[i] r-fop = &(li_codfig_tD;
	[j].send_b);3PTpi-oard_address.lowy_rs.card_address.low;3PTpi-oard_address.highy_rs.card_address.high;3PTpi-offsety_rs.offset;3PTli_codfig_tD;
	[j].)
				&m|= LI_CHANNEL_ADDRESSES_SETf3	}afT	ifnDINi-;
	ernal\ormmci-_queu	[0]r-    B3_XXnDINi-Crjust_b_s		 e	== ADJUST_B_RESTORE_MIXER_2)r-P|a XnDINi-Crjust_b_s		 e	== ADJUST_B_RESTORE_MIXER_3)r-P|a XnDINi-Crjust_b_s		 e	== ADJUST_B_RESTORE_MIXER_4)))r-{s e(*fnDINi-;
	ernal\ormmci-_queu	[0]))fId,l0);
,l0);	YfT	if!nDINi-;
	ernal\ormmci-)3tY_nex _;
	ernal\ormmci-fId,l0);
);
l}"Nmixer_notify_update(0);
,ntru );3}
sss		 ic;voidAmixer_indication_xordnect_tofd_mas Id,l		In	*0);
,nby  l*msg,l_mas li = 1)"{EEN].info + 1);
				[%06lx] %s,%d:Amixer_indication_xordnect_to %d","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,l(;
	) li = 1))("f}
sss		 ic;by  lmixer_notify_source_removed(		In	*0);
,nd_mas nDIN_b_id)"{E;_masnPEND_b_write_posf3
YpDIN_b_write_posm=InDINi-li_nDIN_b_write_posf3PT	if(ynDINi-li_nDIN_b_read_posm> nDIN_b_write_pose ?lpDINi-li_nDIN_b_read_posm:3P     LI_		In_B_QUEUE_ENTRIESm+lpDINi-li_nDIN_b_read_pos)m- pDIN_b_write_posm- 1 < 1)r-{s e].info + 1);
				[%06lx] %s,%d:ALI requ		r overrun","PTYYid_mask(ynDINi-Idm<< 8)ma UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTYYi)
	r *)(FILE_), __LINE__));3PTbrw", I(fai] )f3P}"NpDINi-li_nDIN_b_queu	[nDIN_b_write_pos]&=IPEND_b_id | LI_		In_B_DISC_FLAG;3tnDIN_b_write_posm=I(pDIN_b_write_posm== LI_		In_B_QUEUE_ENTRIESm- 1)	? 0 :I_b3__b_write_posm+ 1("PpDINi-li_nDIN_b_write_pos&=IPEND_b_write_posf3Pbrw", I(tru );3}
sss		 ic;voidAmixer_remove(		In	*0);
)
TE	DIVA_CAPI_ADAPTER *a;El		In	*notify_0);
;E	d_mas PEND_b_id;E;_masnN,njPEEN].info + 1);
				[%06lx] %s,%d:Amixer_l_move","PTYid_mask(ynDINi-Idm<< 8)ma UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTY()
	r *)(FILE_), __LINE__	)("fNam=InDINi-Crplci ;E;PEND_b_id =if_b3_i-Idm<< 8)ma UnMapCrd w;
lerXnDINi-Crplci G)Idef3PT	ifC->profile.Global\Options & GL_LINE_INTERCONNECT_SUPPORTEDer-{s eT	if(nDINi-li_b)
				&_idm!= 0)r-	    B3_Xli_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&== PEND		r--{aPTY
m=IC->li_basl + (nDINi-li_b)
				&_idm- 1);3PEET	if(li_codfig_tD;
	[i].curchn&m|
li_codfig_tD;
	[i].)
				&)m& LI_CHANNEL_INVOLVEDe
-Po_3PTY_fcilijy_req"j);
li_total\o
				&s;djpte
-P	;{r-PffY
	if(li_codfig_tD;
	[i].flag_tD;
	[j]m& LI_FLAG_INTERCONNECTfr-Pfff    |a (li_codfig_tD;
	[j].flag_tD;
	[i]m& LI_FLAG_INTERCONNECTffr-Pfff_3PTY_EEnotify_0);
 =
li_codfig_tD;
	[j].0);
;3PTYNf_
	if(notify_0);
 != NULL)r-Pfff	    B3_Xnotify_0);
 != 0);
)
-Pfff	    B3_Xnotify_0);
i-Cppl != NULL)r-Pfff	    B3_!Xnotify_0);
i-CpplG)appl_flagsm& APPL_FLAG_OLD_LI_SPECer--Prf	    B3_Xnotify_0);
i-S		 eer--Prf	    B3_notify_0);
i-NL.IdmB3_!notify_0);
i-n
_remove_ider-Pf;eN_3PTY_EEfmixer_notify_source_removed(notify_0);
, nDIN_b_id) fND;	fN}3PTY_f}3PTY_}3PTY_mixer_clear_codfig(0);
);
l	TYmixer_calculate_orefs(a);3PTYYmixer_notify_update(0);
,ntru );3PTY}3PTYli_codfig_tD;
	[i].0);
 =
NULLf3	ffnDINi-li_b)
				&_idm	 0f3	f}
l}"}
ss/*------------------------------------------------------------------*/s/* Echo c		ce
ler faciliti_s                                        */s/*------------------------------------------------------------------*/ssss		 ic;voidAec_write_parameci s(		In	*0);
)
TE	_masnw;3Pby  lparameci _buffer[6]PEEN].info + 1);
				[%06lx] %s,%d:Aec_write_parameci s","PTYid_mask(ynDINi-Idm<< 8)ma UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTY()
	r *)(FILE_), __LINE__	)("fNparameci _buffer[0]	= 5;3Pparameci _buffer[1]	= DSP_CTRL_SET_LEC_PARAMETERS;El	U		}
			&parameci _buffer[2], nDIN->ec_idi_options)("PpDINi-ec_idi_optionsm&= ~LEC_RESET_COEFFICIENTS;E;_ =if_b3_i-ec_tail_li = 1m	= 0) ? 128 :I_b3_i-ec_tail_li = 1;El	U		}
			&parameci _buffer[4], w)("PCdd_p(0);
,nFTY,lparameci _buffer)("Psig_req(0);
,nTEL_CTRL,l0);	Ysend_req(0);
);3}
sss		 ic;voidAec_clear_codfig(		In	*0);
)
TEEN].info + 1);
				[%06lx] %s,%d:Aec_clear_codfig","PTYid_mask(ynDINi-Idm<< 8)ma UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTY()
	r *)(FILE_), __LINE__	)("fNpDINi-ec_idi_optionsm= LEC_ENABLE_ECHO_CANCELLER |"PTLEC_MANUAL_DISABLE | LEC_ENABLE_NONLINEAR_PROCESSING("PpDINi-ec_tail_li = 1m	 0f3}
sss		 ic;voidAec_prepare_switch(d_mas Id,l		In	*0);
)
TEEN].info + 1);
				[%06lx] %s,%d:Aec_prepare_switch","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__	)("f}
sss		 ic;_mas ec_save_codfig(d_mas Id,l		In	*0);
,nby  lRc)
TEEN].info + 1);
				[%06lx] %s,%d:Aec_save_codfig %02x %d","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc,InDINi-Crjust_b_s		 e	)("fNbrw", I(GOOD);3}
sss		 ic;_mas ec_r		rore_codfig(d_mas Id,l		In	*0);
,nby  lRc)
TE;_masnInfoPEEN].info + 1);
				[%06lx] %s,%d:Aec_r		rore_codfig %02x %d","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc,InDINi-Crjust_b_s		 e	)("fNInfo = GOODf3	T	ifnDINi-B1_faciliti_s & B1_FACILITY_ECer-{r-	switch (nDINi-Crjust_b_s		 e	
-P{r-	casl ADJUST_B_RESTORE_EC_1:3	ffnDINi-;
	ernal\ormmci-m=InDINi-Crjust_b_ormmci-;3PEET	ifnDINi-sig_req)"PTm{r-	PTnDINi-Crjust_b_s		 e	= ADJUST_B_RESTORE_EC_1;3PTYYbreakf3	fY}aPTY[c_write_parameci s(0);
);
l	TnDINi-Crjust_b_s		 e	= ADJUST_B_RESTORE_EC_2;3PTYbreakf3	fcasl ADJUST_B_RESTORE_EC_2:s eNT	if(Rc != OKf B3_XRc != OK_FC		r--;_3PTY_d.info + 1);
				[%06lx] %s,%d:AR		rore EC failed %02x","PTYYTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc));3PTYNInfo = _WRONG_STATE;3PTYYbreakf3	fY}aPTYbreakf3	f}
l}"Nbrw", I(Info);3}
sss		 ic;voidAec_crmmci-fd_mas Id,l		In	*0);
,nby  lRc)
TE;_masn;
	ernal\ormmci-,nInfoPE;by  ll_sult[8]PEEN].info + 1);
				[%06lx] %s,%d:Aec_ormmci-m%02x %04x %04x %04x %d","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc,InDINi-;
	ernal\ormmci-,
l	TnDINi-ec_omd, nDIN->ec_idi_options,I_b3_i-ec_tail_li = 1	)("fNInfo = GOODf3	T	ifnDINi-CpplG)appl_flagsm& APPL_FLAG_PRIV_EC_SPECer-{
EEl_sult[0]	= 2;3PTPU		}
			&l_sult[1], EC_SUCCESS)f3P}"N[i] Em{
lYl_sult[0]	= 5;3PTPU		}
			&l_sult[1], nDINi-ec_omd);3PTbrsult[3]m=I2;3PTPU		}
			&l_sult[4], GOOD);3T}afT
	ernal\ormmci-m=InDINi-T
	ernal\ormmci-("PpDINi-T
	ernal\ormmci-m=I0f3	switch (nDINi-ec_omd)Em{
lcasl EC_ENABLE_OPERATION:
lcasl EC_FREEZE_COEFFICIENTS:
lcasl EC_RESUME_COEFFICIENT_UPLATE:s casl EC_RESET_COEFFICIENTS:r-	switch (;
	ernal\ormmci-)3tY{s e]efault:3P	PCrjust_b1_l_sourcefId,l0);
,lNULL, (_mask(nDIN->B1_faciliti_s |3PTY_EEf	  B1_FACILITY_ECe, EC_COMMAND_1);3PEcasl EC_COMMAND_1:3P		
	ifarjust_b_processfId,l0);
,lRc) != GOOD)r-Pf_3PTY_d.info + 1);
				[%06lx] %s,%d:ALoad EC failed","PTYYTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNInfo = _FACILITY_NOT_SUPPORTED;3tTYYbreakf3	fY}aPTYT	ifnDINi-;
	ernal\ormmci-er-Pf;brw", ;3PEcasl EC_COMMAND_2:s eNT	ifnDINi-sig_req)"PTm{r-	PTnDINi-T
	ernal\ormmci-m=IEC_COMMAND_2;r-Pf;brw", ;3PEY}aPTYpDINi-T
	ernal\ormmci-m=IEC_COMMAND_3;3PTY[c_write_parameci s(0);
);
l	Tbrw", ;3PEcasl EC_COMMAND_3:s eNT	if(Rc != OKf B3_XRc != OK_FC		r--;_3PTY_d.info + 1);
				[%06lx] %s,%d:AEnD;
	 EC failed %02x","PTYYTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc));3PTYNInfo = _FACILITY_NOT_SUPPORTED;3tTYYbreakf3	fY}aPTYbreakf3	f}
lfbreakf3r-casl EC_DISABLE_OPERATION:
l	switch (;
	ernal\ormmci-)3tY{s e]efault:3P	casl EC_COMMAND_1:3P		
	ifnDINi-B1_faciliti_s & B1_FACILITY_ECer-Po_3PTY_
	ifnDINi-sig_req)"PTmr{
-P	;NnDINi-T
	ernal\ormmci-m=IEC_COMMAND_1("PTmrNbrw", ;3PEY_}3PTY	nDINi-T
	ernal\ormmci-m=IEC_COMMAND_2;r-Pf;[c_write_parameci s(0);
);
l	T;brw", ;3PEY}aPTYRc = OK;3PEcasl EC_COMMAND_2:s eNT	if(Rc != OKf B3_XRc != OK_FC		r--;_3PTY_d.info + 1);
				[%06lx] %s,%d:ADisD;
	 EC failed %02x","PTYYTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lRc));3PTYNInfo = _FACILITY_NOT_SUPPORTED;3tTYYbreakf3	fY}aPTYCrjust_b1_l_sourcefId,l0);
,lNULL, (_mask(nDIN->B1_faciliti_s &3PTY_EEf	  ~B1_FACILITY_ECe, EC_COMMAND_3);3PEcasl EC_COMMAND_3:3P		
	ifarjust_b_processfId,l0);
,lRc) != GOOD)r-Pf_3PTY_d.info + 1);
				[%06lx] %s,%d:AUnload EC failed","PTYYTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNInfo = _FACILITY_NOT_SUPPORTED;3tTYYbreakf3	fY}aPTYT	ifnDINi-;
	ernal\ormmci-er-Pf;brw", ;3PEYbreakf3	f}
lfbreakf3Y}afsendf(nDINi-Cppl, _FACILITY_R | CONFIRM, Idm& 0xffffL, 0);
i-number,
T      "wws", Info, fnDINi-CpplG)appl_flagsm& APPL_FLAG_PRIV_EC_SPECe	?
T      PRIV_SELECTOR_ECHO_CANCELLER : SELECTOR_ECHO_CANCELLER, result);3}
sss		 ic;by  lec_r	qu		r(d_mas Id,l_mas Number,lDIVA_CAPI_ADAPTER *a,l		In	*0);
, APPL   *appl, API_PARSE *msg)
TE;_masnInfoPE;_masnopt;E	API_PARSE ec_parms[3];E	by  ll_sult[16]PEEN].info + 1);
				[%06lx] %s,%d:Aec_l_qu		r","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__	)("fNInfo = GOODf3	l_sult[0]	= 0f3	T	if!fC->mci_profile.1);vate_optionsm& (1Lm<< PRIVATE_ECHO_CANCELLER)))r-{s e].info + 1);
				[%06lx] %s,%d:AFacility nor supported","PTYYUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTInfo = _FACILITY_NOT_SUPPORTED;3t}a	[i] r-{s eT	ifCpplG)appl_flagsm& APPL_FLAG_PRIV_EC_SPECer--{aPTY
	ifapi_parse(&msg[1].info[1], msg[1].li = 1, "w", ec_parms		r--;_3PTY_d.info + 1);
				[%06lx] %s,%d:AWronglmessage format","PTYY	TUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNInfo = _WRONG_MESSAGE_FORMAT;3P		}aPTY[i] r--;_3PTY_
	if_b3_&== NULL)r-Tmr{
-P	;N].info + 1);
				[%06lx] %s,%d:AWrongl		In","PTYYY	TUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNNInfo = _WRONG_IDENTIFIER;3PEY_}3PTY	[i] mT	if!pDINi-S		 e |a !pDINi-NL.Idm|a pDINi-n
_remove_id)r-Tmr{
-P	;N].info + 1);
				[%06lx] %s,%d:AWrongls		 e","PTYYTTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNNInfo = _WRONG_STATE;3PTYY}3PTY_[i] r-fo	{r-PffYpDINi-ormmci-	= 0f3	ff	TnDINi-ec_omdm= GE		}
			ec_parms[0].info);3PTYN_pDINi-ec_idi_optionsm&= ~(LEC_MANUAL_DISABLE | LEC_RESET_COEFFICIENTS);3PTYN_l_sult[0]	= 2;3PT	PTPU		}
			&l_sult[1], EC_SUCCESS)f3PPTY_
	ifmsg[1].li = 1 >= 4er--PeN_3PTY_EEoptm= GE		}
			&ec_parms[0].info[2]);3PTYNf_pDINi-ec_idi_optionsm&= ~(LEC_ENABLE_NONLINEAR_PROCESSING |3PTY_EEf	T  LEC_ENABLE_2100HZ_DETECTOR | LEC_REQUIRE_2100HZ_REVERSALS)f3PPTY_	T	if!foptm& EC_DISABLE_NON_LINEAR_PROCESSING	er-Pf;eN_pDINi-ec_idi_optionsm|= LEC_ENABLE_NONLINEAR_PROCESSING("PPTY_	T	ifoptm& EC_DETECT_DISABLE_TONEer-Pf;eN_pDINi-ec_idi_optionsm|= LEC_ENABLE_2100HZ_DETECTORf3PPTY_	T	if!foptm& EC_DO_NOT_REQUIRE_REVERSALS)er-Pf;eN_pDINi-ec_idi_optionsm|= LEC_REQUIRE_2100HZ_REVERSALSf3PPTY_	T	ifmsg[1].li = 1 >= 6er-Pf;eN_3PTY_EEfpDINi-ec_tail_li = 1m	 GE		}
			&ec_parms[0].info[4]);3PTYNf_}3PTY_f}3PTY_	switch (nDINi-ec_omd)Emf;eN_3PTY_Ecasl EC_ENABLE_OPERATION:
lTYNf_pDINi-ec_idi_optionsm&= ~LEC_FREEZE_COEFFICIENTS;3PTYNf_star _;
	ernal\ormmci-fId,l0);
,lec_ormmci-);r-	PTPTl_w", I(fai] )f3r-	Y_Ecasl EC_DISABLE_OPERATION:
l	YNf_pDINi-ec_idi_optionsm= LEC_ENABLE_ECHO_CANCELLER |"PTTTTTTLEC_MANUAL_DISABLE | LEC_ENABLE_NONLINEAR_PROCESSING |"PTTTTTTLEC_RESET_COEFFICIENTS;E;TYNf_star _;
	ernal\ormmci-fId,l0);
,lec_ormmci-);r-	PTPTl_w", I(fai] )f3r-	Y_Ecasl EC_FREEZE_COEFFICIENTS:
lf;eN_pDINi-ec_idi_optionsm|= LEC_FREEZE_COEFFICIENTS;3PTYNf_star _;
	ernal\ormmci-fId,l0);
,lec_ormmci-);r-	PTPTl_w", I(fai] )f3r-	Y_Ecasl EC_RESUME_COEFFICIENT_UPLATE:s TYNf_pDINi-ec_idi_optionsm&= ~LEC_FREEZE_COEFFICIENTS;3PTYNf_star _;
	ernal\ormmci-fId,l0);
,lec_ormmci-);r-	PTPTl_w", I(fai] )f3r-	Y_Ecasl EC_RESET_COEFFICIENTS:r-	;eN_pDINi-ec_idi_optionsm|= LEC_RESET_COEFFICIENTS;E;TYNf_star _;
	ernal\ormmci-fId,l0);
,lec_ormmci-);r-	PTPTl_w", I(fai] )f3r-	Y_E]efault:3P	P	Y_d.info + 1);
				[%06lx] %s,%d:AEC unknown requ		r %04x","PTYYY	YYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lnDINi-ec_omd)e("PTmrN_PU		}
			&l_sult[1], EC_UNSUPPORTED_OPERATIONe("PTmrN}3PTY_}3PTY}3	f}
lf[i] r-f{aPTY
	ifapi_parse(&msg[1].info[1], msg[1].li = 1, "ws", ec_parms		r--;_3PTY_d.info + 1);
				[%06lx] %s,%d:AWronglmessage format","PTYY	TUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNInfo = _WRONG_MESSAGE_FORMAT;3P		}aPTY[i] r--;_3PTY_
	ifGE		}
			ec_parms[0].info)&== EC_GE		SUPPORTED_SERVICES)"PTmr{
-P	;Nl_sult[0]	= 11;3PT	PTPU		}
			&l_sult[1], EC_GE		SUPPORTED_SERVICES);3PTYN_l_sult[3]m=I8;3PT	PTPU		}
			&l_sult[4], GOOD);3TY_fTPU		}
			&l_sult[6], 0x0007);3TY_fTPU		}
			&l_sult[8], LEC_MAX	SUPPORTED_TAIL_LENGTH);3TY_fTPU		}
			&l_sult[10], 0);r-	PT}3PTY	[i] mT	if_b3_&== NULL)r-Tmr{
-P	;N].info + 1);
				[%06lx] %s,%d:AWrongl		In","PTYYY	TUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNNInfo = _WRONG_IDENTIFIER;3PEY_}3PTY	[i] mT	if!pDINi-S		 e |a !pDINi-NL.Idm|a pDINi-n
_remove_id)r-Tmr{
-P	;N].info + 1);
				[%06lx] %s,%d:AWrongls		 e","PTYYTTTUnMapId(Ide, ()
	r *)(FILE_), __LINE__));3PTYNNInfo = _WRONG_STATE;3PTYY}3PTY_[i] r-fo	{r-PffYpDINi-ormmci-	= 0f3	ff	TnDINi-ec_omdm= GE		}
			ec_parms[0].info);3PTYN_pDINi-ec_idi_optionsm&= ~(LEC_MANUAL_DISABLE | LEC_RESET_COEFFICIENTS);3PTYN_l_sult[0]	= 5;3PT	PTPU		}
			&l_sult[1], nDINi-ec_omd);3PT	PTl_sult[3]m=I2;3PTYNfPU		}
			&l_sult[4], GOOD);3TY_fTpDINi-ec_idi_optionsm&= ~(LEC_ENABLE_NONLINEAR_PROCESSING |3PTY_EEf	  LEC_ENABLE_2100HZ_DETECTOR | LEC_REQUIRE_2100HZ_REVERSALS)f3PPTY_pDINi-ec_tail_li = 1m	 0f3	PTY_
	ifec_parms[1].li = 1 >= 2er--PeN_3PTY_EEoptm= GE		}
			&ec_parms[1].info[1])("PPTY_	T	ifoptm& EC_ENABLE_NON_LINEAR_PROCESSING	r-Pf;eN_pDINi-ec_idi_optionsm|= LEC_ENABLE_NONLINEAR_PROCESSING("PPTY_	T	ifoptm& EC_DETECT_DISABLE_TONEer-Pf;eN_pDINi-ec_idi_optionsm|= LEC_ENABLE_2100HZ_DETECTORf3PPTY_	T	if!foptm& EC_DO_NOT_REQUIRE_REVERSALS)er-Pf;eN_pDINi-ec_idi_optionsm|= LEC_REQUIRE_2100HZ_REVERSALSf3PPTY_	T	ifec_parms[1].li = 1 >= 4er-Pf;eN_3PTY_EEfpDINi-ec_tail_li = 1m	 GE		}
			&ec_parms[1].info[3]);3PTYNf_}3PTY_f}3PTY_	switch (nDINi-ec_omd)Emf;eN_3PTY_Ecasl EC_ENABLE_OPERATION:
lTYNf_pDINi-ec_idi_optionsm&= ~LEC_FREEZE_COEFFICIENTS;3PTYNf_star _;
	ernal\ormmci-fId,l0);
,lec_ormmci-);r-	PTPTl_w", I(fai] )f3r-	Y_Ecasl EC_DISABLE_OPERATION:
l	YNf_pDINi-ec_idi_optionsm= LEC_ENABLE_ECHO_CANCELLER |"PTTTTTTLEC_MANUAL_DISABLE | LEC_ENABLE_NONLINEAR_PROCESSING |"PTTTTTTLEC_RESET_COEFFICIENTS;E;TYNf_star _;
	ernal\ormmci-fId,l0);
,lec_ormmci-);r-	PTPTl_w", I(fai] )f3r-	Y_E]efault:3P	P	Y_d.info + 1);
				[%06lx] %s,%d:AEC unknown requ		r %04x","PTYYY	YYUnMapId(Ide, ()
	r *)(FILE_), __LINE__,lnDINi-ec_omd)e("PTmrN_PU		}
			&l_sult[4], _FACILITY_SPECIFIC_FUNCTION_NOT_SUPPe("PTmrN}3PTY_}3PTY}3	f}
l}afsendf(appl, _FACILITY_R | CONFIRM, Idm& 0xffffL, Number,
T      "wws", Info, fCpplG)appl_flagsm& APPL_FLAG_PRIV_EC_SPECe	?
T      PRIV_SELECTOR_ECHO_CANCELLER : SELECTOR_ECHO_CANCELLER, result);3Pbrw", I(fai] )f3}
sss		 ic;voidAec_indicationfd_mas Id,l		In	*0);
,nby  l*msg,l_mas li = 1)"{E;by  ll_sult[8]PEEN].info + 1);
				[%06lx] %s,%d:Aec_indication","PTYUnMapId(Ide, ()
	r *)(FILE_), __LINE__	)("fN
	if!(pDINi-ec_idi_optionsm& LEC_MANUAL_DISABLE))r-{s eT	ifnDINi-CpplG)appl_flagsm& APPL_FLAG_PRIV_EC_SPECer-N_3PTYl_sult[0]	= 2;3PT	PU		}
			&l_sult[1], 0);r-	Pswitch (msg[1]	r--;_3PTYcasl LEC_DISABLE_TYPE_CONTIGNUOUS_2100HZ:3P	P	PU		}
			&l_sult[1], EC_BYPASS_DUE_TO_CONTINUOUS_2100HZ);3tTYYbreakf3	fYcasl LEC_DISABLE_TYPE_REVERSED_2100HZ:3P	P	PU		}
			&l_sult[1], EC_BYPASS_DUE_TO_REVERSED_2100HZ);3tTYYbreakf3	fYcasl LEC_DISABLE_RELEASED:3P	P	PU		}
			&l_sult[1], EC_BYPASS_RELEASED);3tTYYbreakf3	fY}3	f}
lf[i] r-f{aPTYl_sult[0]	= 5;3PT	PU		}
			&l_sult[1], EC_BYPASS_INDICATIONe("PTml_sult[3]m=I2;3PTYPU		}
			&l_sult[4], 0);r-	Pswitch (msg[1]	r--;_3PTYcasl LEC_DISABLE_TYPE_CONTIGNUOUS_2100HZ:3P	P	PU		}
			&l_sult[4], EC_BYPASS_DUE_TO_CONTINUOUS_2100HZ);3tTYYbreakf3	fYcasl LEC_DISABLE_TYPE_REVERSED_2100HZ:3P	P	PU		}
			&l_sult[4], EC_BYPASS_DUE_TO_REVERSED_2100HZ);3tTYYbreakf3	fYcasl LEC_DISABLE_RELEASED:3P	P	PU		}
			&l_sult[4], EC_BYPASS_RELEASED);3tTYYbreakf3	fY}3	f}
lfsendf(nDINi-Cppl, _FACILITY_I, Idm& 0xffffL, 0, "ws", fnDINi-CpplG)appl_flagsm& APPL_FLAG_PRIV_EC_SPECe	?
TT      PRIV_SELECTOR_ECHO_CANCELLER : SELECTOR_ECHO_CANCELLER, result);3P}"}
sss/*------------------------------------------------------------------*/s/* Adv		ced;voice                                                   */s/*------------------------------------------------------------------*/sss		 ic;voidAadv_voice_write_orefs(		In	*0);
,n_masnwrite_ormmci-er{E	DIVA_CAPI_ADAPTER *a;El_masn;;E	by  l*p;EE	_masnw, n,nj,nk;E	by  lch_map[MIXER_CHANNELS_BRI]PEENby  lcref_buffer[ADV_VOICE_COEF_BUFFER_SIZEm+ 2]PEEN].info + 1);
				[%06lx] %s,%d:Aadv_voice_write_orefs %d","PTYid_mask(ynDINi-Idm<< 8)ma UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTY()
	r *)(FILE_), __LINE__,nwrite_ormmci-e)("fNam=InDINi-Crplci ;E;P = cref_bufferm+ 1("P*(p++)	= DSP_CTRL_OLD_SET_MIXER_COEFFICIENTS;E;im	 0f3	whilemyim+ sizeof(_mask);= ai-Crv_voice_cref_li = 1)"P_3PTPU		}
			p,lGE		}
			ai-Crv_voice_cref_bufferm+ i)e("PTp +	I2;3PTi +	I2;3P}3	whilemyim< ADV_VOICE_OLD_COEF_COUNT * sizeof(_mask)"P_3PTPU		}
			p,l0x8000e("PTp +	I2;3PTi +	I2;3P}3
TT	if!C->li_1); B3_XnDINi-li_b)
				&_idm	= 0))r-{s eT	ifXli_codfig_tD;
	[C->li_basl].0);
&== NULL) B3_Xli_codfig_tD;
	[C->li_basl + 1].0);
&!= NULL)er-N_3PTYnDINi-li_b)
				&_idm	 1;3PT	li_codfig_tD;
	[C->li_basl].0);
&=InDIN;3PT	].info + 1);
				[%06lx] %s,%d:Aadv_voice_set_b)
				&_idm%d","PTYYTid_mask(ynDINi-Idm<< 8)ma UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTYYY()
	r *)(FILE_), __LINE__,nnDINi-li_b)
				&_id)e("PT}
lf[i]  T	ifXli_codfig_tD;
	[C->li_basl].0);
&!= NULL) B3_Xli_codfig_tD;
	[C->li_basl + 1].0);
&== NULL)er-N_3PTYnDINi-li_b)
				&_idm	 2;3PT	li_codfig_tD;
	[C->li_basl + 1].0);
&=InDIN;3PT	].info + 1);
				[%06lx] %s,%d:Aadv_voice_set_b)
				&_idm%d","PTYYTid_mask(ynDINi-Idm<< 8)ma UnMapCrd w;
lerXnDINi-Crplci G)Ide),"PTYYY()
	r *)(FILE_), __LINE__,nnDINi-li_b)
				&_id)e("PT}
l}
TT	if!C->li_1); B3_XnDINi-li_b)
				&_idm!= 0)r-    B3_Xli_codfig_tD;
	[C->li_basl + (nDINi-li_b)
				&_idm- 1)].0);
&== PEND		r-{s eTm=IC->li_basl + (nDINi-li_b)
				&_idm- 1);3PEswitch (write_ormmci-er-P{r-	casl ADV_VOICE_WRITE_ACTIVATION:
l	Yjm=IC->li_basl + MIXER_IC_CHANNEL_BASE + (nDINi-li_b)
				&_idm- 1);3PEEkm=IC->li_basl + MIXER_IC_CHANNEL_BASE + (2m- pDINi-li_b)
				&_id);3PEE
	if!(pDINi-B1_faciliti_s & B1_FACILITY_MIXER		r--;_3PTY_li_codfig_tD;
	[j].flag_tD;
	[i]m|= LI_FLAG_CONFERENCE | LI_FLAG_MIX;3PTY_li_codfig_tD;
	[i].flag_tD;
	[j]m|= LI_FLAG_CONFERENCE | LI_FLAG_MONITORf3PPT}aPTYT	ifC->mciufacw",er_feaw",es & MANUFACTURER_FEATURE_SLAVE_CODECer-Po_3PTY_li_codfig_tD;
	[k].flag_tD;
	[i]m|= LI_FLAG_CONFERENCE | LI_FLAG_MIX;3PTY_li_codfig_tD;
	[i].flag_tD;
	[k]m|= LI_FLAG_CONFERENCE | LI_FLAG_MONITORf3PPT_li_codfig_tD;
	[k].flag_tD;
	[j]m|= LI_FLAG_CONFERENCEf3PPT_li_codfig_tD;
	[j].flag_tD;
	[k]m|= LI_FLAG_CONFERENCEf3PPT}aPTYmixer_calculate_orefs(a);3PTYli_codfig_tD;
	[i].curchn&m=
li_codfig_tD;
	[i].)
				&;aPTYli_codfig_tD;
	[j].)urchn&m=
li_codfig_tD;
	[j].)
				&;aPTYT	ifC->mciufacw",er_feaw",es & MANUFACTURER_FEATURE_SLAVE_CODECer-Po_li_codfig_tD;
	[k].)urchn&m=
li_codfig_tD;
	[k].)
				&;aPTYbreakf3r-	casl ADV_VOICE_WRITE_DEACTIVATION:
l	Yfcilijy_req"j);
li_total\o
				&s;djpte
-P	_3PTY_li_codfig_tD;
	[i].flag_tD;
	[j]m	 0f3	PTYli_codfig_tD;
	[j].flag_tD;
	[i]m	 0f3	PT}3PEEkm=IC->li_basl + MIXER_IC_CHANNEL_BASE + (nDINi-li_b)
				&_idm- 1);3PEEfcilijy_req"j);
li_total\o
				&s;djpte
-P	_3PTY_li_codfig_tD;
	[k].flag_tD;
	[j]m	 0f3	PTYli_codfig_tD;
	[j].flag_tD;
	[k]m	 0f3	PT}3PEET	ifC->mciufacw",er_feaw",es & MANUFACTURER_FEATURE_SLAVE_CODECer-Po_3PTY_km=IC->li_basl + MIXER_IC_CHANNEL_BASE + (2m- pDINi-li_b)
				&_id);3PEE_fcilijy_req"j);
li_total\o
				&s;djpte
-P	;{r-PffYli_codfig_tD;
	[k].flag_tD;
	[j]m	 0f3	PTYYli_codfig_tD;
	[j].flag_tD;
	[k]m	 0f3	PT_}3PTY}3	fYmixer_calculate_orefs(a);3PTYbreakf3	f}
lf
	ifnDINi-B1_faciliti_s & B1_FACILITY_MIXER	r-N_3PTYwm	 0f3	PT
	ifADV_VOICE_NEW_COEF_BASE + sizeof(_mask);= ai-Crv_voice_cref_li = 1)"PPTYwm	 GE		}
			ai-Crv_voice_cref_bufferm+ ADV_VOICE_NEW_COEF_BASE);3PEE
	ifli_codfig_tD;
	[i].)
				&m& LI_CHANNEL_TX_DATA)"PPTYwm|= MIXER_FEATURE_ENABLE_TX_DATA;3PEE
	ifli_codfig_tD;
	[i].)
				&m& LI_CHANNEL_RX_DATA)"PPTYwm|= MIXER_FEATURE_ENABLE_RX_DATA;3PEE*(p++)	= yby  )nw;3PEE*(p++)	= yby  )(w >> 8);3PT_fcilijy_req"j);
sizeof(ch_map)q"j)+= 2	3P-;_3PTY_ch_map[j]m=Iyby  )ij + (nDINi-li_b)
				&_idm- 1));3PEE_ch_map[jm+ 1] =Iyby  )ij + (2m- pDINi-li_b)
				&_id));3PTY}3PTYfciliny_req"nm< ARRAY_SIZE(mixer_write_prog_bri)q"npte
-P	_3PTY_Tm=IC->li_basl + ch_map[mixer_write_prog_bri[n].to\o
];aPTYljm=IC->li_basl + ch_map[mixer_write_prog_bri[n].from\o
];aPTYl
	ifli_codfig_tD;
	[i].)
				&m& li_codfig_tD;
	[j].)
				&m& LI_CHANNEL_INVOLVEDe
-Po;{r-PffY*(p++)	= yfli_codfig_tD;
	[i].)ref_tD;
	[j]m& mixer_write_prog_bri[n].mask)	? 0x80 : 0x01);3PTYN;_ =iffli_codfig_tD;
	[i].)ref_tD;
	[