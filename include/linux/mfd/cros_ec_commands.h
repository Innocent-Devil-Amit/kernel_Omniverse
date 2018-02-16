/*
 * Host communication command constants for ChromeOS EC
 *
 * Copyright (C) 2012 Google, Inc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * The ChromeOS EC multi function device is used to mux all the requests
 * to the EC device for its multiple features: keyboard controller,
 * battery charging and regulator control, firmware update.
 *
 * NOTE: This file is copied verbatim from the ChromeOS EC Open Source
 * project in an attempt to make future updates easy to make.
 */

#ifndef __CROS_EC_COMMANDS_H
#define __CROS_EC_COMMANDS_H

/*
 * Current version of this protocol
 *
 * TODO(crosbug.com/p/11223): This is effectively useless; protocol is
 * determined in other ways.  Remove this once the kernel code no longer
 * depends on it.
 */
#define EC_PROTO_VERSION          0x00000002

/* Command version mask */
#define EC_VER_MASK(version) (1UL << (version))

/* I/O addresses for ACPI commands */
#define EC_LPC_ADDR_ACPI_DATA  0x62
#define EC_LPC_ADDR_ACPI_CMD   0x66

/* I/O addresses for host command */
#define EC_LPC_ADDR_HOST_DATA  0x200
#define EC_LPC_ADDR_HOST_CMD   0x204

/* I/O addresses for host command args and params */
/* Protocol version 2 */
#define EC_LPC_ADDR_HOST_ARGS    0x800  /* And 0x801, 0x802, 0x803 */
#define EC_LPC_ADDR_HOST_PARAM   0x804  /* For version 2 params; size is
					 * EC_PROTO2_MAX_PARAM_SIZE */
/* Protocol version 3 */
#define EC_LPC_ADDR_HOST_PACKET  0x800  /* Offset of version 3 packet */
#define EC_LPC_HOST_PACKET_SIZE  0x100  /* Max size of version 3 packet */

/* The actual block is 0x800-0x8ff, but some BIOSes think it's 0x880-0x8ff
 * and they tell the kernel that so we have to think of it as two parts. */
#define EC_HOST_CMD_REGION0    0x800
#define EC_HOST_CMD_REGION1    0x880
#define EC_HOST_CMD_REGION_SIZE 0x80

/* EC command register bit functions */
#define EC_LPC_CMDR_DATA	(1 << 0)  /* Data ready for host to read */
#define EC_LPC_CMDR_PENDING	(1 << 1)  /* Write pending to EC */
#define EC_LPC_CMDR_BUSY	(1 << 2)  /* EC is busy processing a command */
#define EC_LPC_CMDR_CMD		(1 << 3)  /* Last host write was a command */
#define EC_LPC_CMDR_ACPI_BRST	(1 << 4)  /* Burst mode (not used) */
#define EC_LPC_CMDR_SCI		(1 << 5)  /* SCI event is pending */
#define EC_LPC_CMDR_SMI		(1 << 6)  /* SMI event is pending */

#define EC_LPC_ADDR_MEMMAP       0x900
#define EC_MEMMAP_SIZE         255 /* ACPI IO buffer max is 255 bytes */
#define EC_MEMMAP_TEXT_MAX     8   /* Size of a string in the memory map */

/* The offset address of each type of data in mapped memory. */
#define EC_MEMMAP_TEMP_SENSOR      0x00 /* Temp sensors 0x00 - 0x0f */
#define EC_MEMMAP_FAN              0x10 /* Fan speeds 0x10 - 0x17 */
#define EC_MEMMAP_TEMP_SENSOR_B    0x18 /* More temp sensors 0x18 - 0x1f */
#define EC_MEMMAP_ID               0x20 /* 0x20 == 'E', 0x21 == 'C' */
#define EC_MEMMAP_ID_VERSION       0x22 /* Version of data in 0x20 - 0x2f */
#define EC_MEMMAP_THERMAL_VERSION  0x23 /* Version of data in 0x00 - 0x1f */
#define EC_MEMMAP_BATTERY_VERSION  0x24 /* Version of data in 0x40 - 0x7f */
#define EC_MEMMAP_SWITCHES_VERSION 0x25 /* Version of data in 0x30 - 0x33 */
#define EC_MEMMAP_EVENTS_VERSION   0x26 /* Version of data in 0x34 - 0x3f */
#define EC_MEMMAP_HOST_CMD_FLAGS   0x27 /* Host cmd interface flags (8 bits) */
/* Unused 0x28 - 0x2f */
#define EC_MEMMAP_SWITCHES         0x30	/* 8 bits */
/* Unused 0x31 - 0x33 */
#define EC_MEMMAP_HOST_EVENTS      0x34 /* 32 bits */
/* Reserve 0x38 - 0x3f for additional host event-related stuff */
/* Battery values are all 32 bits */
#define EC_MEMMAP_BATT_VOLT        0x40 /* Battery Present Voltage */
#define EC_MEMMAP_BATT_RATE        0x44 /* Battery Present Rate */
#define EC_MEMMAP_BATT_CAP         0x48 /* Battery Remaining Capacity */
#define EC_MEMMAP_BATT_FLAG        0x4c /* Battery State, defined below */
#define EC_MEMMAP_BATT_DCAP        0x50 /* Battery Design Capacity */
#define EC_MEMMAP_BATT_DVLT        0x54 /* Battery Design Voltage */
#define EC_MEMMAP_BATT_LFCC        0x58 /* Battery Last Full Charge Capacity */
#define EC_MEMMAP_BATT_CCNT        0x5c /* Battery Cycle Count */
/* Strings are all 8 bytes (EC_MEMMAP_TEXT_MAX) */
#define EC_MEMMAP_BATT_MFGR        0x60 /* Battery Manufacturer String */
#define EC_MEMMAP_BATT_MODEL       0x68 /* Battery Model Number String */
#define EC_MEMMAP_BATT_SERIAL      0x70 /* Battery Serial Number String */
#define EC_MEMMAP_BATT_TYPE        0x78 /* Battery Type String */
#define EC_MEMMAP_ALS              0x80 /* ALS readings in lux (2 X 16 bits) */
/* Unused 0x84 - 0x8f */
#define EC_MEMMAP_ACC_STATUS       0x90 /* Accelerometer status (8 bits )*/
/* Unused 0x91 */
#define EC_MEMMAP_ACC_DATA         0x92 /* Accelerometer data 0x92 - 0x9f */
#define EC_MEMMAP_GYRO_DATA        0xa0 /* Gyroscope data 0xa0 - 0xa5 */
/* Unused 0xa6 - 0xfe (remember, 0xff is NOT part of the memmap region) */


/* Define the format of the accelerometer mapped memory status byte. */
#define EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK  0x0f
#define EC_MEMMAP_ACC_STATUS_BUSY_BIT        (1 << 4)
#define EC_MEMMAP_ACC_STATUS_PRESENCE_BIT    (1 << 7)

/* Number of temp sensors at EC_MEMMAP_TEMP_SENSOR */
#define EC_TEMP_SENSOR_ENTRIES     16
/*
 * Number of temp sensors at EC_MEMMAP_TEMP_SENSOR_B.
 *
 * Valid only if EC_MEMMAP_THERMAL_VERSION returns >= 2.
 */
#define EC_TEMP_SENSOR_B_ENTRIES      8

/* Special values for mapped temperature sensors */
#define EC_TEMP_SENSOR_NOT_PRESENT    0xff
#define EC_TEMP_SENSOR_ERROR          0xfe
#define EC_TEMP_SENSOR_NOT_POWERED    0xfd
#define EC_TEMP_SENSOR_NOT_CALIBRATED 0xfc
/*
 * The offset of temperature value stored in mapped memory.  This allows
 * reporting a temperature range of 200K to 454K = -73C to 181C.
 */
#define EC_TEMP_SENSOR_OFFSET      200

/*
 * Number of ALS readings at EC_MEMMAP_ALS
 */
#define EC_ALS_ENTRIES             2

/*
 * The default value a temperature sensor will return when it is present but
 * has not been read this boot.  This is a reasonable number to avoid
 * triggering alarms on the host.
 */
#define EC_TEMP_SENSOR_DEFAULT     (296 - EC_TEMP_SENSOR_OFFSET)

#define EC_FAN_SPEED_ENTRIES       4       /* Number of fans at EC_MEMMAP_FAN */
#define EC_FAN_SPEED_NOT_PRESENT   0xffff  /* Entry not present */
#define EC_FAN_SPEED_STALLED       0xfffe  /* Fan stalled */

/* Battery bit flags at EC_MEMMAP_BATT_FLAG. */
#define EC_BATT_FLAG_AC_PRESENT   0x01
#define EC_BATT_FLAG_BATT_PRESENT 0x02
#define EC_BATT_FLAG_DISCHARGING  0x04
#define EC_BATT_FLAG_CHARGING     0x08
#define EC_BATT_FLAG_LEVEL_CRITICAL 0x10

/* Switch flags at EC_MEMMAP_SWITCHES */
#define EC_SWITCH_LID_OPEN               0x01
#define EC_SWITCH_POWER_BUTTON_PRESSED   0x02
#define EC_SWITCH_WRITE_PROTECT_DISABLED 0x04
/* Was recovery requested via keyboard; now unused. */
#define EC_SWITCH_IGNORE1		 0x08
/* Recovery requested via dedicated signal (from servo board) */
#define EC_SWITCH_DEDICATED_RECOVERY     0x10
/* Was fake developer mode switch; now unused.  Remove in next refactor. */
#define EC_SWITCH_IGNORE0                0x20

/* Host command interface flags */
/* Host command interface supports LPC args (LPC interface only) */
#define EC_HOST_CMD_FLAG_LPC_ARGS_SUPPORTED  0x01
/* Host command interface supports version 3 protocol */
#define EC_HOST_CMD_FLAG_VERSION_3   0x02

/* Wireless switch flags */
#define EC_WIRELESS_SWITCH_ALL       ~0x00  /* All flags */
#define EC_WIRELESS_SWITCH_WLAN       0x01  /* WLAN radio */
#define EC_WIRELESS_SWITCH_BLUETOOTH  0x02  /* Bluetooth radio */
#define EC_WIRELESS_SWITCH_WWAN       0x04  /* WWAN power */
#define EC_WIRELESS_SWITCH_WLAN_POWER 0x08  /* WLAN power */

/*
 * This header file is used in coreboot both in C and ACPI code.  The ACPI code
 * is pre-processed to handle constants but the ASL compiler is unable to
 * handle actual C code so keep it separate.
 */
#ifndef __ACPI__

/*
 * Define __packed if someone hasn't beat us to it.  Linux kernel style
 * checking prefers __packed over __attribute__((packed)).
 */
#ifndef __packed
#define __packed __attribute__((packed))
#endif

/* LPC command status byte masks */
/* EC has written a byte in the data register and host hasn't read it yet */
#define EC_LPC_STATUS_TO_HOST     0x01
/* Host has written a command/data byte and the EC hasn't read it yet */
#define EC_LPC_STATUS_FROM_HOST   0x02
/* EC is processing a command */
#define EC_LPC_STATUS_PROCESSING  0x04
/* Last write to EC was a command, not data */
#define EC_LPC_STATUS_LAST_CMD    0x08
/* EC is in burst mode.  Unsupported by Chrome EC, so this bit is never set */
#define EC_LPC_STATUS_BURST_MODE  0x10
/* SCI event is pending (requesting SCI query) */
#define EC_LPC_STATUS_SCI_PENDING 0x20
/* SMI event is pending (requesting SMI query) */
#define EC_LPC_STATUS_SMI_PENDING 0x40
/* (reserved) */
#define EC_LPC_STATUS_RESERVED    0x80

/*
 * EC is busy.  This covers both the EC processing a command, and the host has
 * written a new command but the EC hasn't picked it up yet.
 */
#define EC_LPC_STATUS_BUSY_MASK \
	(EC_LPC_STATUS_FROM_HOST | EC_LPC_STATUS_PROCESSING)

/* Host command response codes */
enum ec_status {
	EC_RES_SUCCESS = 0,
	EC_RES_INVALID_COMMAND = 1,
	EC_RES_ERROR = 2,
	EC_RES_INVALID_PARAM = 3,
	EC_RES_ACCESS_DENIED = 4,
	EC_RES_INVALID_RESPONSE = 5,
	EC_RES_INVALID_VERSION = 6,
	EC_RES_INVALID_CHECKSUM = 7,
	EC_RES_IN_PROGRESS = 8,		/* Accepted, command in progress */
	EC_RES_UNAVAILABLE = 9,		/* No response available */
	EC_RES_TIMEOUT = 10,		/* We got a timeout */
	EC_RES_OVERFLOW = 11,		/* Table / data overflow */
	EC_RES_INVALID_HEADER = 12,     /* Header contains invalid data */
	EC_RES_REQUEST_TRUNCATED = 13,  /* Didn't get the entire request */
	EC_RES_RESPONSE_TOO_BIG = 14    /* Response was too big to handle */
};

/*
 * Host event codes.  Note these are 1-based, not 0-based, because ACPI query
 * EC command uses code 0 to mean "no event pending".  We explicitly specify
 * each value in the enum listing so they won't change if we delete/insert an
 * item or rearrange the list (it needs to be stable across platforms, not
 * just within a single compiled instance).
 */
enum host_event_code {
	EC_HOST_EVENT_LID_CLOSED = 1,
	EC_HOST_EVENT_LID_OPEN = 2,
	EC_HOST_EVENT_POWER_BUTTON = 3,
	EC_HOST_EVENT_AC_CONNECTED = 4,
	EC_HOST_EVENT_AC_DISCONNECTED = 5,
	EC_HOST_EVENT_BATTERY_LOW = 6,
	EC_HOST_EVENT_BATTERY_CRITICAL = 7,
	EC_HOST_EVENT_BATTERY = 8,
	EC_HOST_EVENT_THERMAL_THRESHOLD = 9,
	EC_HOST_EVENT_THERMAL_OVERLOAD = 10,
	EC_HOST_EVENT_THERMAL = 11,
	EC_HOST_EVENT_USB_CHARGER = 12,
	EC_HOST_EVENT_KEY_PRESSED = 13,
	/*
	 * EC has finished initializing the host interface.  The host can check
	 * for this event following sending a EC_CMD_REBOOT_EC command to
	 * determine when the EC is ready to accept subsequent commands.
	 */
	EC_HOST_EVENT_INTERFACE_READY = 14,
	/* Keyboard recovery combo has been pressed */
	EC_HOST_EVENT_KEYBOARD_RECOVERY = 15,

	/* Shutdown due to thermal overload */
	EC_HOST_EVENT_THERMAL_SHUTDOWN = 16,
	/* Shutdown due to battery level too low */
	EC_HOST_EVENT_BATTERY_SHUTDOWN = 17,

	/* Suggest that the AP throttle itself */
	EC_HOST_EVENT_THROTTLE_START = 18,
	/* Suggest that the AP resume normal speed */
	EC_HOST_EVENT_THROTTLE_STOP = 19,

	/* Hang detect logic detected a hang and host event timeout expired */
	EC_HOST_EVENT_HANG_DETECT = 20,
	/* Hang detect logic detected a hang and warm rebooted the AP */
	EC_HOST_EVENT_HANG_REBOOT = 21,

	/*
	 * The high bit of the event mask is not used as a host event code.  If
	 * it reads back as set, then the entire event mask should be
	 * considered invalid by the host.  This can happen when reading the
	 * raw event status via EC_MEMMAP_HOST_EVENTS but the LPC interface is
	 * not initialized on the EC, or improperly configured on the host.
	 */
	EC_HOST_EVENT_INVALID = 32
};
/* Host event mask */
#define EC_HOST_EVENT_MASK(event_code) (1UL << ((event_code) - 1))

/* Arguments at EC_LPC_ADDR_HOST_ARGS */
struct ec_lpc_host_args {
	uint8_t flags;
	uint8_t command_version;
	uint8_t data_size;
	/*
	 * Checksum; sum of command + flags + command_version + data_size +
	 * all params/response data bytes.
	 */
	uint8_t checksum;
} __packed;

/* Flags for ec_lpc_host_args.flags */
/*
 * Args are from host.  Data area at EC_LPC_ADDR_HOST_PARAM contains command
 * params.
 *
 * If EC gets a command and this flag is not set, this is an old-style command.
 * Command version is 0 and params from host are at EC_LPC_ADDR_OLD_PARAM with
 * unknown length.  EC must respond with an old-style response (that is,
 * withouth setting EC_HOST_ARGS_FLAG_TO_HOST).
 */
#define EC_HOST_ARGS_FLAG_FROM_HOST 0x01
/*
 * Args are from EC.  Data area at EC_LPC_ADDR_HOST_PARAM contains response.
 *
 * If EC responds to a command and this flag is not set, this is an old-style
 * response.  Command version is 0 and response data from EC is at
 * EC_LPC_ADDR_OLD_PARAM with unknown length.
 */
#define EC_HOST_ARGS_FLAG_TO_HOST   0x02

/*****************************************************************************/
/*
 * Byte codes returned by EC over SPI interface.
 *
 * These can be used by the AP to debug the EC interface, and to determine
 * when the EC is not in a state where it will ever get around to responding
 * to the AP.
 *
 * Example of sequence of bytes read from EC for a current good transfer:
 *   1. -                  - AP asserts chip select (CS#)
 *   2. EC_SPI_OLD_READY   - AP sends first byte(s) of request
 *   3. -                  - EC starts handling CS# interrupt
 *   4. EC_SPI_RECEIVING   - AP sends remaining byte(s) of request
 *   5. EC_SPI_PROCESSING  - EC starts processing request; AP is clocking in
 *                           bytes looking for EC_SPI_FRAME_START
 *   6. -                  - EC finishes processing and sets up response
 *   7. EC_SPI_FRAME_START - AP reads frame byte
 *   8. (response packet)  - AP reads response packet
 *   9. EC_SPI_PAST_END    - Any       0x30	_ ArgsR1UL << , _EN 5. EC_SPST_EN EC_SPI_PAST_ine EC_MEMMAP_HFSTED = 5,
	EC_HOST_EVEN respoDDR_Hgn - AP r          0x0100  /* ISRC2_DEC1_ENA */
#define ARIZONA_ISRC2_DEC1_ENA_MASK    0  /1SPST_EN EC_S*I2sponds bug thein
waitPI_FRAME_START
 *   6. -  NA */
#2 bin all 32  Removwit     f the ing a EC EC happed        EADY   - AP send      EADY   RXP send      EADY   R AP send      EADY   - EC start can be usnot in a fondiny tDECy cons
 * to the rly cwread thst to read
 * to the  tell thlC_LP is code
 * P * consigivsy t
waitPI_FRAME_START
 *   6. -   chargquery
 t in a sta handle arnel tis not inthein
deat _CMD_REBnuseto the ef __CRodes rF   a EC EC hwhich	EC_ce by acket
 *   9. EC_romeOS EC ea atAf8 bitCMD_REBO.  Com the rlt inthet aroc0-0x8fn<< , _Eumasl.
 */
s 0x8e *   a EC EC ntire  pref0-0x8fn<
 * tot
 *   9. EC_e EC_HOST_ARGS_FLART
 *   6. -  N ****ecCRodes rP Arg EC EC shwhich	  Daf0-0xst.
nteaf8 bivent md in thtot
 *   9. EC_e EC_HOST_ARGS_FLART
- Any        ****eds busy.  This ept subseC_ceivserminessedigned mevent EC hhas nodebug thespondEC_T_EEC devesume normat arotCMD th host.9. EC_ris used(ssingg ECown lengE

/*
 * CPARAM_COL_3)8fn<
 * SPST_defiuint8_EC_HOST_ARGS_FLART
RXP send     ****f8s busy.  ThssedssingedeC_ceiv interfaEC_RES_RomeOS EC APIOSes d it upssingedin an asets up efine 8_EC_HOST_ARGS_FLART
R AP sends r ****f9
ritten a byC_ceivsdrequest */
	EC_RES_RomeOS EC APrmineg a command */erneC_HOST_ARGS_FLART
- EC starts  ****fas busy.  ThC_ceivsdrbadC is at
 *  EC APIOsuch	 y ac9. EC_ris usedstyle rehe hostsy. must respondt aroigned respo is aumasl.(CS#)
 *   2FSTED = 58_EC_HOST_ARGS_FLART
RXPBAD 0xa0 /* **fbs busy.  ThC_ceivsdr is at
 *  EC APrgqfover getas ept scode
thouthS EC APrmED = edin a(CS#)
 *   2yte masngede *       	 */
	qfovert in a tas ept subseC_ceivsfine ECpondt aroigned respo is aumasl.(CS#)
 *   2FSTED = 58_EC_HOST_ARGS_FLART
  0x send     *he of busy.  Thtas ept subseC_ceivsfa	EC_RES_RomeOS EC APesponda byteptt mevent EC hhas chargyS EC APrmE memmap ra	EC_RES_R9. EC_rly c(RAMEsponse (thECt
 g a command * tell fullyhC_ceivsdr9. EC_rSes iead thst to g
 * to thine 8_EC_HOST_ARGS_FLART
- AP sends f ne EC_T*******************************************************************/
/*
 * Byte cRodes rPion 2 */
#define ERAMEI2ode.  e.
 tCMD thEC_RES_Rld-stway: can b	0	T_EC co
	EC_RE0 + (con is 0 and re_SPI	1	ion is 0oid
 *SPI	2	Lust rap rhost ar= NSPI	3..N+2	Post a,e/inin SPI	N+3	8-vent__packedad from EC0..N+2hromeOS EC cor* to the Ahtot
 *   is: can b	0	RestempOST_E(NSE_TOO*_SPI	1	Lust rap rhost ar= MSPI	2..M+1	Post a,e/inin SPI	M+2	8-vent__packedad from EC0..M+1 EC_PROTO_VERSION    2NCATED =      /_BYTES 3PROTO_VERSION    2NCATED = TRAIL /_BYTES 1PROTO_VERSION    2NCATED = 
	/*    E(NSEN    2NCATED =      /_BYTES +	\PROTO    EADN    2NCATED = TRAIL /_BYTESC_FAN_SPEED_ENN    2NCAIG = 14     /_BYTES 2FAN_SPEED_ENN    2NCAIG = 14TRAIL /_BYTES 1PROTO_VERSION    2NCAIG = 14
	/*    E(NSEN    2NCAIG = 14     /_BYTES +	\PROTO    D_ENN    2NCAIG = 14TRAIL /_BYTESnts atPost memormust rhtas limit to debug e is
	 * not iC_PROTO_VERSION    2NZE */
/* Protoche of bu verimumhEC_RES_R data from EC9. EC_rll p_host_pion 2 */
#define EC_LPC_ADDR_HOSN    2NZE *CATED = rotoc(NSEN    2NCATED = 
	/*    E+	\PROTO    EADN    2NZE */
/* Proto)LPC_ADDR_HOSN    2NZE *CAIG = 14rotoc(NSEN    2NCAIG = 14
	/*    E+	\PROTO    D_ENN    2NZE */
/* Proto)L*******************************************************************/
/*
 * Byte cRodes rVl 32 w commang
 legacy this flarome /ackedixt EC hx kenal (fr
 * TODO(cro 3+ c_hostC.  Dabee Ahe in neUsne Eis cov-t.
 */as 8_EC_HOST_ARGS_FL/*
 * CPARAM_COL_3cheda
HOST_ARGS_FLAG_TOCATED = 
	EC_RES3f bu Vtocol */
EC_RES_RomeOSEC_LP_lpc_host_argags *EC_RES_R{ that _host_
#define(=3)_t cfinishedwhen it is pID_HEADER = 12,     /*ifck as ceivsnt cos usedstyle finis and resp 2Foeit up.
 *SECwhx khos EC t checksum;
} c_hostint8_t datcksum; sum of com *   5. EC_R dat is mand + flesporom ECincluhe Ah__packedm; su* consitota
	ECC0._t checksum;
} __packed;
 thation is 0OST_Echecksum16ersion;
	u;
 thation is 0 and reschecksum;
} _on;
	uint8_t dat that- 0xfe data reg transfepion 2 */
#defin;ine EECC0schecksum;
} 
#defineat thatLust rap r is awhich	e ing sRld-stos usedchecksum16ers	 * amus;

/* Flags forOST_ARGS_FLAG_TOCAIG = 14
	EC_RES3f bu Vtocol */
ECfrom ECa curren_lpc_host_argags *ECfrom EC{ that _host_
#define(=3) checksum;
} c_hostint8_t datcksum; sum of com *   5able */
dat is mand + flesporom ECincluhe Ah__packedm; su* consitota
	ECC0._t checksum;
} __packed;
 thatRestempOST_E(NSE_TOO*_dchecksum16ersrestemat thatLust rap r is awhich	e ing sRld-stos usedchecksum16ers	 * amus;
 that- 0xfe datas reg transfepion 2 */
#defin;ine EECC0schecksum16ersresefineat
/* Flags for e****************************************************************/
/*
 * Byte c of ALSotas onstants fs: can b E therogress *le
 *8-vturnedn is 0 l 32ersion is shwhich	tr mohost arONA_ISit is pa from EC is aach val c_hostC.read
 aLP is codIf* dec_host_ined iach vauted,thrnedn is 0Foeiate whepes AMEsutpes  is ,pa frless; pr.es rP st memota bytes.
must rh-stymcify
 8fn<
 * c_hostC* GNhis termslyd * teln command conspion 2 *s (I2o, e.
)ed, dadd
must rhfirm of com os uses,not been
 *
 *sed uenceLPC_d con-t.
 */as /
dat#defi */
#de eveef __CRod****************************************************************/
/*
 * Byte c olic Licen/ teesponse cos __CRodes rGefepion 2 */
#defin, all the FSTldstylenon- thewer,
 *mpd cndle * TODO(cro  delet58_EC_HOST_ARGS_FLC coN          0x0 fla
pc_host_arga bytes._ * TOata_size lags;
	32_t_
#definat
/* Flags for ent coeing reasonable nsuence	 * determi teespady to accepbytesivle actua*/
	EC_HOSEC_HOST_ARGS_FLC coHELLOArgs apc_host_arghost a_heing lags;
	32_t_in_ is ma  atPos bin e coge eve* Hos/* Flags forc_host_arga bytes._heing lags;
	32_t_sut_ is ma  atOutpes ul,
 * bin_ is  + rgs 020304* Hos/* Flags for/ rGefeta_size oid
 * C_HOST_ARGS_FLC coGET      0x0 fl2
tus {
	EC transf_imne EVENT_LIMAGE_UNKN/* SugEVENT_TIMAGE_ROVENT_TIMAGE_RWost ec_host_arga bytes._getata_size lag fansll-hen thtt meta_size sll 8 byreadROV RWschec regeta_size_sll 8 _ro[32];ec regeta_size_sll 8 _rw[32];ec regeresefine[32];     **developckeviously RW-Bhe memorchecksum32_t_ transf_imne ma  atOnead f	EC transf_imne E Hos/* Flags for/ rRt.  TES_RESPOST_ARGS_FLC co sen_TES Args3apc_host_arghost a_st t_tES_R{ ts;
	32_t_sperat; **devSsingg ECll 32 f the ldis 255 bchecksum32_t_* Che   **devring define Efn<< , _E Hos/* Flags forc_host_arga bytes._st t_tES_R{ ts;
	32_t_ is [32];e
/* Flags for ent cGC_rSeil Efne acceize can b Rot
 *   is oill-hen thtt mee memoOSEC_HOST_ARGS_FLC coGET BUILDDERFOt writr/ rGefe(CS#)fne EC_HOST_ARGS_FLC coGET CHIPDERFOt wr5 ec_host_arga bytes._geta(CS#_fne Elag fansll-hen thtt mesll 8 bychec regetandor[32];ec regent m[32];ec regerevisize[32];  bu vesk0 and resches/* Flags for/ rGefery comHW0 and rescheOST_ARGS_FLC coGET BY = 1     0x0 fl6 ec_host_arga bytes._ry coata_size lags;
	16ersry coata_size;  bu A mo#deo#dusly increLPC_g ECoid
 *.sches/* Flags for/ n b Roahis allo-y.  Thi is c
rogram is d*le
 *alhenhtt s
	 * not ieventallo-y.  Thi is  f thcovspion 2 *s EC isich	dif we Chrome dis ct-y status byte.- I2o, e.
, etc. can b Rot
 *   is * If ECll pa< , _Ep r is OSEC_HOST_ARGS_FLC co sen_ 0x900
 fl7apc_host_arghost a_st t_n) */

lags;
	uintsperat; **devrsion 3mory) */

EXT_MAX) */*) checksum;
} c Che   **devring define Efn<< , _E Hos/* Flags for/ rRt.  ta_sizes  Chrome ECt good efine EC_LPC_STATUS_PC coGET C co
	EC_RESis in pc_host_arghost a_geta(moata_size flags;
	uint(mo;    **devC* determi m of E Hos/* Flags forc_host_arga bytes._geta(moata_size flagsum; suvesk0of bChrome ECta_size ; allUS_Psion) (1U)rmi m*mpdve*style finisdesHOST_ta_size._t checksum32_t_
#defin_ine ;e
/* Flags for ent cm of uses codunnd consits )*/
/cove).m is d*lestabst.
	 i2c/spirSes within 
	 lpciledcer gesseditC. */
sut-of- tC hassykenal (for. can b lpci with aad<
 * c_ )*/
omeOS EC ster bit functio. Ake fut interkernen b lpciul,
 erfawas a ceterrgs/post memorspot iis 0OSEC1_ENitC. is OSEC_HOST_ARGS_FLC coGET COMMSESSING)		s i9
rittAgeri al int	EC_RES_SUisich	is f the  is papped ttatus {
	ECstersC_RES_SUCCESS COMMSESSING)
- EC start	= Data r,g faPcommand */e fl Host ec_host_arga bytes._geta(tersC_RES_SUCCEksum32_t_8_t co datavesk0of us {
	ECstersC_RES_SU Hos/* Flags for/ rFr moth hrieCHANTpa from Es,npur proread
uery) *purp*
 e EC_HOST_CMD_REGC coTES PARAM_COL		s iaset adel that hedwesumeo tCMD  theneo ue EC_Hc_host_arghost a_
uer_pion 2 */CCEksum32_t_arga btematEksum32_t_e  amus;
gs;
	uints 2[32];e
/* Flags for ecoever geste e . EC_Hc_host_arga bytes._
uer_pion 2 */CCEksumuints 2[32];e
/* Flags for ecGefepiocetermine acceizeEC_HOST_ARGS_FLC coGET ARAM_COL_ERFO	s ibor ec_lpc_host_arga bytes._getapion 2 *_fne 
 * Args arepID_HEADER8,		/* Accd, distEC over S/inierogress *lesne EC_MEMMAP_BATT_ARAM_COL_ERFODER8,		/* Ac0x01
/* Hos Data re ec_host_arga bytes._getapion 2 *_fne Elag faFiel shwhich	exs to/init
mu EC pion 2 */
#define3 bChrome EC__CRg faBitmesk0of pion 2 */
#defins  Chrome EC Data nent pprotocol *n)checksum32_t_pion 2 *_ta_size ;CRg faverimumhEC_RES_R9. EC_rll p,Efn<< , _E Hogs;
	16ersmax*EC_RES_ Flagst * ChecRg faverimumhECfrom EC9. EC_rll p,Efn<< , _E Hogs;
	16ersmax*ECbytes._ lagst * ChecRg fa_lpc_;ineBATT_ARAM_COL_ERFOD* checksum32_t_8_t com
/* Flags forRod****************************************************************/
/*
 * Byte c olict/SC_rmiscel aneoMMAPpped ttatet addreChrionsturnof 
 * Argnel sdwesumeo do*/
#d cogent ppr"get" EC_LPC_STATUS_RGSV_

/*
 * Nine EC______tet addrel * Thth Fou< , _Ep r
 * Argi/as vaut 0x8e post memo,e/in
 aLPen a  ent pPI_FRAME rehedivido keCommand EC_LPC_STATUS_RGSV_/
/* PLPC_S flaffffff pc_host_arghost a_getasetatl 32 CCEksum32_t_8_t coecksum32_t_
l 32;os/* Flags forc_host_arga bytes._getasetatl 32 CCEksum32_t_8_t coecksum32_t_
l 32;os/* Flags for/sensors h
 * nC ster bitby try
 t is* c_hostCmeo get/on 3host emoe EC_HOST_CMD_REGC coGSV_/
USEDER8S5	s icorRod****************************************************************/
/*
 * Byte c ol_lpshponse cos __CRodcGefeflpshpfne EC_HOST_ARGS_FLC coFLASHDERFOt wh flagsVtocol *02.
 */
#dt is* fiel shC_Hc_host_arga bytes._flpsh_fne Elag faUsandleflpshpll p,Efn<< , _E Hogs;
	32_t_8_tsh_* Checksum; sug to E00-0x8* Chitly to Ess of eate ming  withbmothtures: km; suocol
 *._t checksum32_t_was a_00-0x_* Checksum; suEras E00-0x8* ChitlEras Ess of eate ming  withbmothtures: km; suocol
 *._t checksum32_t_eras _00-0x_* Checksum; suPione is us00-0x8* ChitlPione is usss of eate ming  withbmotm; sutures: keocol
 *._t checksum32_t_pione i_00-0x_* Chec
/* Flags for ec_lpc_host_otocol *1+eflpshpfne E efine EC_Litten flpshperas s UnuseECC0s*/
eaad<oco EC_MEMMAP_ACC_DFLASHDERFO_ERA 14  _0s Data re eodes rVtocol *12.
 */
#dt i s  8.on the  fiel shaprotocol *0,*style ArgsR1UL es rfiel she ing a E. can b gcceatonymoMMAc_hostCmdif we eemmeo get*alo ECown dt i ttribute_dis ctivhecbackfchangedri we'dfi */
#dt i otocol *0ec_host_ y acsub-c_host_ocol
 *
 nCOSEC_Hc_host_arga bytes._flpsh_fne _1Elag faVtocol *02fiel s;ineBAabrefaost_descripeizeEC_Hgs;
	32_t_8_tsh_* Checkksum32_t_was a_00-0x_* Checkksum32_t_eras _00-0x_* Checkksum32_t_pione i_00-0x_* Checag faVtocol *1e Ar#dt is* fiel s:EC_Hgsum; suIFSTlds to E	 * ECn<< , _itly to s ul,
 * bf ECe to/in	 * EC_PRe inxac * el
 *
amd inion 3msothtures: keocol
 *.  2 paequenceermiCC_m; sut, dk of ads to Es 255 bwhich	by tdresslf-pne Eosor wfins i34 - 0x3sams/resignfied undaesne 5 bword-at-a-red ds to Eporte_t checksum32_t_was a_iFSTl * ChecRg fa_lpc_;ineBATT_FLASHDERFO_* checksum32_t_8_t com
/* Flags for/ n b Roahi8_tsh can b Rot
 *   is * If ECll pa< , _Ep r is OSEC_HOST_ARGS_FLC coFLASHD sent wh apc_host_arghost a_8_tsh_ aad<{ ts;
	32_t_sperat; **devByo Ess of edefine EC_Lcksum32_t_* Che   **devring define Efn<< , _E Hos/* Flags for/sug to EflpshpC_HOST_ARGS_FLC coFLASHDT_DISt wh2FAN_SPEED_ENsionFLASHDT_DISt1flagsVtocol *02 mask iflpshponse co  Chrome ECEMMAP64a< , _Ep r is EC_MEMMAP_ACC_DFLASHDT_DISAsio04rotoc64apc_host_arghost a_8_tsh_s to E{ ts;
	32_t_sperat; **devByo Ess of edefs to EC_Lcksum32_t_* Che   **devring defs to Efn<< , _E Hog fa_ ing  to de is Edefs to EC_Ls/* Flags for/suEras EflpshpC_HOST_ARGS_FLC coFLASHDERA 1t wh3apc_host_arghost a_8_tsh_eras E{ ts;
	32_t_sperat; **devByo Ess of edeferas EC_Lcksum32_t_* Che   **devring deferas Efn<< , _E Hos/* Flags for/snt cGC_/on 3flpshppione is uC responds mesk!=0,*
#de/cmu ad
 * to the  to itC. f_8_t c at euesting 
	 */
es rfie.
 *
 s to Epione i GPIOd, beca#define Eul,
 tr mouseles immedianelyecbacthinkfine Eto t*/
	acommands.
	 h comboof edeftr mouselesersi of u*/
es rEC over Sfine EUnuseECCneBAwesume so kly readinedC responds mesk=0,*
uency2.
 */
#dt i  transfefine Eit wiOSEC_HOST_ARGS_FLC coFLASHDBLED 0xt wh5FAN_SPEED_ENsionFLASHDBLED 0xt1**devC* deterotocol *1 __CRodc_lpc_host_flpshppione is uEC_LittROiflpshpond Epione iof
 usnot in a P */ EC_WIRELESS_SWIFLASHDBLED 0x_RO_AT BYO/*
 * Nins Data re / n b ROiflpshpond Epione iof
nowcodIf*never set */ an oat- C an_RES_SUby within istrdeletdOSEC_HOST_ARGS_FLFLASHDBLED 0x_RO_NOWMASK    0  /1 Write pr/suEt */
	flpshpond Epione iof
now,aumasl. AP */ EC_HOST_CMD_REGFLASHDBLED 0x_ALL_NOWMASK    0  / EC is bc ol_lpshps to Epione i GPIO3msotED = ed  RemC_HOST_CMD_REGFLASHDBLED 0x_GPIO_ASSE* Host0  / EC is3pr/suEECy c-nit
mu EC  nC batwo paflpshpf Eituf u0-0xsted undby witthe An0-0xst.C_HOST_CMD_REGFLASHDBLED 0x_EC_RE_STUCK    0  / EC is4e / n b EECy c-nflpshppione is uEmode. innvalie  a EC_MEhitlAt
mu EC  nC batwo pes rflpshpshich	* consideEpione iof
iead thpione iof neUso kly fix to des rEC-MI query) *isteresHOST_fine rly c dea h comboof e/in
 aLPfThe Chro_HOST_CMD_REGFLASHDBLED 0x_EC_RE_INCONSISTfine SCI ever/suEt *l
	flpshpond Epione iof
 usnot in a P */ EC_WIRELESS_SWIFLASHDBLED 0x_ALL_AT BYO/*
 * Nin SMI eveapc_host_arghost a_8_tsh_pione i { ts;
	32_t_ine ; **devBnusee.  * Argno eadly checksum32_t_8_t coof fanew  * Argno eadly ches/* Flags forc_host_arga bytes._8_tsh_pione i { t/rsion of thl 32  fnflpshppione iefine EC_WEksum32_t_8_t coecsum; su_lpc_hwhich	  Dav EC_MEMol
 *
 t
 * juws
 * reportindt i   Batrm; sueo dey wonuishpbetw*/
	flpc_hwhich	  Df we e thc aflpc_hwhich	by 'tm; surossaturMol
 *
 t
 * juw_t checksum32_t_
 EC__8_t coecsuu_lpc_hwhich	by the rdeletdigivsndt i  transfepione is uEit wilchecksum32_t_was andl_8_t coes/* Flags for/snt cSota:ponse cos  wh#defin19 otocol *0eweveroldponse cos eo get/on 38_tsh caps to Epione icode
 seponse cos d, distECall town dotocol *>C0._ __CRodcGefe
 * togs usss of /ll paC_HOST_ARGS_FLC coFLASHD s80

/ERFOt wh6FAN_SPEED_ENsionFLASHD s80

/ERFOt1
tus {
	EC8_tsh_ agize lag faRagize which	holds ept -EMMAPto amne E Ho	SWIFLASHD s80

/ROiugEVEN faRagize which	holds epwas andlPto amne E Ho	SWIFLASHD s80

/RW has finisRagize which	* consideEs to -pione iof
indt i definey (acomsorraturefinisheIFLASHD s80

/RO)ST_EVENT_IFLASHD s80

/WP_ROVEN/fans at EC_M agizes_EVENT_IFLASHD s80

/COUNT,ost ec_host_arghost a_8_tsh_ agize_fne Elagksum32_t_e gize;  bu us {
	EC8_tsh_ agize ches/* Flags forc_host_arga bytes._8_tsh_ agize_fne Elagksum32_t_sperat;Lcksum32_t_* Cheos/* Flags for/ rRt. /s to EVbNvContPST_C_HOST_ARGS_FLC coVBNV 4,

#define FAN_SPEED_ENsionVBNV 4,

#def1PROTO_VERSIOVBNV BLOCK4rotoch6Ftus {
	ECvbnvcontPST_opUCCESS VBNV 4,

#de_OPD sen,CESS VBNV 4,

#de_OPDT_DIS,ost ec_host_arghost a_vbnvcontPSTElagksum32_t_sp;
gs;
	uints0-0x[SIOVBNV BLOCK4roto];e
/* Flags forc_host_arga bytes._vbnvcontPSTElagksumuints0-0x[SIOVBNV BLOCK4roto];e
/* Flags forod****************************************************************/
/*
 * Byte c olPWMponse cos __CRodcGefefy tty */t RPMEC_HOST_ARGS_FLC coNWMoGET ALLET	EC_x_RPMEt commc_host_arga bytes._ wm_getafy _rpmElagksum32_t_epd;

/* Flags for ecSC_rty */t fy tRPMEC_HOST_ARGS_FLC coNWMoSET ALLET	EC_x_RPMEt c apc_host_arghost a_ wm_setafy _ty */t_rpmElagksum32_t_epd;

/* Flags for ecGefetroller,
 thel2 GooC_HOST_ARGS_FLC coNWMoGET OVERY = 1BACKLIGHTsion mmc_host_arga bytes._ wm_getatroller,_ thel2 Goolagksumuintsorcnsf;
gs;
	uintehandleat
/* Flags for ecSC_rtroller,
 thel2 GooC_HOST_ARGS_FLC coNWMoSET OVERY = 1BACKLIGHTsion3apc_host_arghost a_ wm_setatroller,_ thel2 Goolagksumuintsorcnsf;

/* Flags for ecSC_rty */t fy tPWMpduty c/
/* C_HOST_ARGS_FLC coNWMoSET ALLEDUTYsion apc_host_arghost a_ wm_setafy _duty lagksum32_t_sorcnsf;

/* Flags for e****************************************************************/
/*
 * Byte c o capL2 Gobara*/
	EC_HO
 * reEC_Ssbworsrs h
 *set *. Sedcerwdefinetry
  nC AG_Tctua*/
	EC_eECCn, d"talk *
 * Exl2 Gobar",rwdepnterfac"EC_eEel tiumeo do*X" memmcbackn and ommCommand EWe'l tupdat/
#ifnde c_hostC.readommCommandsCown lengdi255 ng (repes rrgst is neaLPwde.
 *SECwhmuch	defeC_T_EOSEC_HOST_ARGS_FLC coLIGHTBA 3)   */
#apc_host_rgb_ flags;
	uintr, g, b;ost eOST_ARGSLB = 6,
	EC_L 0xS ite tos to masweakandlPpost memo*. ile isI8ff
ttribute_sor gesy the sng (reBO.  C args and pa, interfacesignmng (reqt i s  8. ager,flag. Kate.
 *neaLPwayOSEC_Hc_host_l2 Gobarghost a lag faTimemorchecsum32_t_g*
 * _st p_up;
gsum32_t_g*
 * _st p_batt;
gsum32_t_s3s0_st p_up;
gsum32_t_s0_tick_delay[2]o dataAC=0/1rchecsum32_t_s0a_tick_delay[2]o dataAC=0/1rchecsum32_t_s0s3_st p_batt;
gsum32_t_s3_slate_rea;
gsum32_t_s3_st p_up;
gsum32_t_s3_st p_batt;
ag faOscillaeizeEC_Hgs;
	uintnew_s0;
gs;
	uintosc_mem[2]o ddataAC=0/1rchecs;
	uintosc_max[2]o ddataAC=0/1rchecs;
	uintw_ofs[2]o ddataAC=0/1rcheRg faB12 GonES_Ulimitt, tsst.
	 */
	 thel2 Gooe.  Th.rchecs;
	uintb12 Go_bl_off_fix t[2]o dataAC=0/1rchecs;
	uintb12 Go_bl_on_mem[2]o dataAC=0/1rchecs;
	uintb12 Go_bl_on_max[2]o dataAC=0/1rcheRg faB too low */
	Eha bholds checs;
	uintb too l_Eha bhold[LB = 6,
	EC_L 0xS - 1]ecRg favep [AC][b too l_w */
]rmi m*ly condex checksum;
} c0_idx[2][LB = 6,
	EC_L 0xS]o ataAhein
runnemorchecksum8_t_s3_idx[2][LB = 6,
	EC_L 0xS]o ataAhein
slateine EC_Lthatioly cpaletwilchecc_host_rgb_ fm*ly [8]o ddata0-3	  Da *
 * fm*ly _E Hos/* Flags forc_host_arghost a_l2 Gobarflags;
	uint(mo;TO    D*devC* deter(neBA so the Gobarg and pa) checknize lagcc_host_lagcdatanonterfachec	}pdump,_spe,_sn,.on t, getaseq, getapost a,ent8_t datckcc_host_so tlagcds;
	uintned;
c	}pb12 GonES_,*
#qlow moatckcc_host_ agtlagcds;
	uintctrl,pa g, 
l 32;oc	}pa gatckcc_host_ gbtlagcds;
	uintlstedrstedgreen,.b 32;oc	}pagbatckcc_host_l2 Gobarghost a setapost a;oc};e
/* Flags forc_host_arga bytes._l2 Gobarflagsnize lagcc_host_dumptlagcdc_host_lagcdcksum;
} 
#g;agcdcksum;
} ic0;agcdcksum;
} ic1;agcd} 
l s[23];ec	}pdumpatckcc_host_getaseqtlagcds;
	uintned;
c	}pgetaseqatckcc_host_l2 Gobarghost a getapost a;ockcc_host_ta_size lag	gksum32_t_ned;
c	Eksum32_t_8_t coecd} 
t8_t datckcc_host_lagcdatanone  is p Protocol cd} spe,_sn,.on t, b12 GonES_,*
#qloa g, agblow mo, setapost a;oc};e
/* Flags for/apL2 Gobara*/
	EC_Httatus {
he Gobarg and pa_lagLIGHTBA 3)  EDUMPiugEVENLIGHTBA 3)  EOFFST_EVENLIGHTBA 3)  EOOST_EVENLIGHTBA 3)  EINITST_EVENLIGHTBA 3)  EBRIGHTN Accep4VENLIGHTBA 3)  ESEQST_EVENLIGHTBA 3)  E s8ST_EVENLIGHTBA 3)  E GBST_EVENLIGHTBA 3)  EGET SEQST_8VENLIGHTBA 3)  EDEMOST_EVENLIGHTBA 3)  EGET /
/* SOST_EVENLIGHTBA 3)  ESET /
/* SOST_EVENLIGHTBA 3)  E
	EC_RES_I1EVENLIGHTBA 3NUM3)  Sost eve****************************************************************/
/*
 * Byte c o Was controlponse cos __CROST_ARGS_FLC coLED 4,

ROwitc29Ftus {
	EClst_id lag faWas x kenal (fr
l too loit wil flaty */
EVENT_ILED ID = 6,
	EC_LDiugEVEN ffinisWas x kenal (fr
sysange
 * Thit wil( uss conssusuest).m; suve distonsp * Thintt uss c usC-panelOST_EVENT_ILED ID  = 3,
LED,ag faWas onsp * Thadapemors cog relug_EVENT_ILED ID ADAPT3,
LED,aENT_ILED ID COUNTost eve Was controlpfine EC_WIRELESS_SWILED  /* H_QU/* S Data reave Qul Chaas capabilne EfinetC_WIRELESS_SWILED  /* H_AUTO/1 Write p*devr*/
#deaas  theneo autocceic controlp__CRus {
	EClst_m*ly _E{ENT_ILED COLOR_RLDiugEVENT_ILED COLOR_GREENVENT_ILED COLOR_x02 VENT_ILED COLOR_YELLOWVENT_ILED COLOR_WHDIS,oENT_ILED COLOR_COUNTost ec_host_arghost a_lst_m*ntrolplags;
	uintlst_ide   **devWhich	Was x kcontrolp__Cgs;
	uint8_t co    D*devC*ntrolpfine EC_Wecs;
	uintb12 GonES_[T_ILED COLOR_COUNT];e
/* Flags forc_host_arga bytes._lst_m*ntrolplag ffinisA	EC_RES_Tb12 GonES_thl 32 st (i._t cfinisRt (it0ent pprm*ly crdel che*/
#define .finisRt (it1ent ppron/spekcontrol.finisORemovPpped tnt pprbug eas th thntrolpbytPWM._t checksum;
} b12 GonES__st (i[T_ILED COLOR_COUNT];e
/* Flags forve****************************************************************/
/*
 * Byte c o Vervaute P */ponse cos __CRodes rSota:ponse coean "noc29 otocol *0ewas Vand t)   onsLink EVT;.
 *d, dises rECall tRAMEsRemovpurp*
 etown dotocol *>C0._ __CRodcVervaute P */phpshponse co C_HOST_ARGS_FLC coVBnd tHASHnoc2A ec_host_arghost a_vb */_hpshplags;
	uint(mo;    **     **devus {
	ECvb */_hpsh_e fl Hogs;
	uinthpsh_ in ;     **devus {
	ECvb */_hpsh_ in mC_Hgs;
	uintnonce_* Che   **f fanonce_* Che d, dist0schecksum;
} 
#define0;     **devResefineassatu0schecksum32_t_sperat; **    **devrsion 3morflpshp
};

shpC_Hcksum32_t_* Che   **   **f fans at EC_M< , _E
};

shpC_Hcksumuintnonce_ is [64];  bu nonce_ is maigned meiftnonce_* Ch=0 ches/* Flags forc_host_arga bytes._vb */_hpshplags;
	uint_RES_S;  **   **f faus {
	ECvb */_hpsh__RES_SU Hogs;
	uinthpsh_ in ;     **devus {
	ECvb */_hpsh_ in mC_Hgs;
	uintdie AP_* Che   **devring C_Mhpshpdie APEfn<< , _E Hogs;
	;
} 
#define0;     **devIgned ; ul,
 * b0schecksum32_t_sperat; **    **devrsion 3morflpshpwhich	was hpshe EC_Lcksum32_t_* Che   **   **f fans at EC_M< , _Ehpshe EC_Lcksumuinthpsh_die AP[64];  ecopshpdie APE is EC_Ms/* Flags forus {
	ECvb */_hpsh_e flCCESS VBnd tHASHEGETiugEV*   **f faGefe(transfehpshp_RES_SU HogSS VBnd tHASHEABO Suggeder contaAbome calculaei */etransfehpshp HogSS VBnd tHASHE	/* Suggader contaSsing m*mpurature but hpshp HogSS VBnd tHASHERECALCST_EVr contaSynchro#dusly m*mpurere but hpshp Ho}forus {
	ECvb */_hpsh_ in mCCESS VBnd tHASHE0x78_SHA256iugEV*ntaSHA-256i Ho}forus {
	ECvb */_hpsh__RES_SUCCESS VBnd tHASHE	/*NG)
NONEiugEV*ntaN};

shp(noupssingedrly cabome E)p HogSS VBnd tHASHE	/*NG)
DONEiug1V*ntaFializingm*mpurature hpshp HogSS VBnd tHASHE	/*K \
	(ECuggade faBssykm*mpurature hpshp Host event ces for mapped tempess of eRAME_STVBnd tHASHE	/* Su co SS VBnd tHASHERECALC.sponds onkeocol
e   is ach vauted,thrnhedwhen autocceic kly updao Ess of eatecbacting deftEC cor* ctmapped tempet i sch vaute amne E(ROly cRWne EC_HOST_ARGS_FLVBnd tHASHEfine E/ROintry n staHOST_ARGS_FLVBnd tHASHEfine E/RWintry n st_T*******************************************************************/
/*
 * Byte cvent cMois uEienseponse cos EWe'l tupdat/
#ifnde c_hostC.readomm-CommandsCown lengdi255 ng (repes rrgst is neaLPwde.
 *SECwhmuch	defeC_T_EOSEC_HOST_ARGS_FLC coMOT0

/T)

E3)   */
Bor/sensis uEienseponse costtatus {
msis uienseg and pa_lag ffinisDumptster bit f */
#da#demsis uEienso- 0x9f incluhe Ahmsis uEienskm; sumodulnkfine Eess */divido keienso- 8_t c _t checMOT0

T)

E3)  EDUMPiugEVEHgsum; suIne E efine E f */
#d0x9f describy) *isteretThe ap ra	givsndienso-,s backncluhe Ahus {
msis uiensor_ in ,hus {
msis uiensor_lond coned unPRe ins {
msis uiensor_(CS# _t checMOT0

T)

E3)  EERFOtT_EVEhas finishedRaurnedn is 0ble nsetwir/getwirnedn is 0empet i ocesuence Ahtaurm; suocoa#demsis uEienso-see. mhenisecoC_HOST_EVENMOT0

T)

E3)  EID_HATEST_EVEhas finisSenso- ODRnedn is 0ble nsetwir/getwirnedn is 0empet i sutpes  is nt statwil fl aach vaic msis uEienso- e. mheniemotzOST_EVENMOT0

T)

E3)  ET)

#defDRST_EVEhas finisSenso- st (itedn is 0ble nsetwir/getwirnedn is 0empet i K to 454ams/re sch vaute msis uEienso- e. +/-G'arON +/-ereg/HOST_EVENMOT0

T)

E3)  ET)

#deRANGEcep4VEhas finisSetwir/getwirnedn is 0empet i troller,
wr motd in EWusnot inostsms/red insblegeptt ovwit ol
 *
apped, troller,
wr moin
disandlf
indS3,sms/redf
 usnot inEC_Med insgoeiaflagswit ol
 *
apped, troller,
wr moinPRe insandlf.rSota,ot inEC_Med insnt sureLPC_d*le
 *approxamnta,PRe iun-c kibrtt metpped, hs reat inwr motd ind*lf wenxac OST_EVENMOT0

T)

E3)  EKB_WAKE_ANGLES_INVAEN/fans at EC_Mmsis uiensedomm-Commands._EVENMOT0

T)

E3NUM3)  Sost ens {
msis uiensor_iflCCESS MOT0

/T)

#deD = L_BASEiugEVENT_IMOT0

/T)

#deD = L_/* Hos1VENT_IMOT0

/T)

#deGYROiugEVEhas finisSota,oi_Mmsll returnC.  Daadbst.g is not counwe deletthS EC p Arg Es backn_arga bytes._msis u_ienseddumptster bit withbmomodvauteOST_EVENT_IMOT0

/T)

#deCOUNTST_Eost eve Ws to mamsis uEienso-  in s._EVEus {
msis uiensor_ in lCCEMOT0

T)

E30x78_D = LiugEVENMOT0

T)

E30x78_GYROiug1,ost eve Ws to mamsis uEienso- lond cons._EVEus {
msis uiensor_lond conlCCEMOT0

T)

E3LOC_BASEiugEVENMOT0

T)

E3LOC_/* Hos1VEst eve Ws to mamsis uEienso- (CS#s._EVEus {
msis uiensor_(CS#)CCEMOT0

T)

E3CHIPDKXCJ9iugEVE}for/sensdulnkfine EC hasall tRAMEisterumptomm-Command EC_HOST_CMD_MOT0

T)

E3MODULET   0xACTIVE (1<<re eodsSenso- fine EC hasall tRAMEisterumptomm-Command EC_HOST_CMD_MOT0

T)

E3T)

#de   0x,
	/fine S<<re eod
nisSendol
 *
appedtRAMEisterx9f eceLPC_	deffinetper* ju thECad Eds youcbactCMD tnyEsRemovtpped, thrnhedwhen 
	 * prn 3mt_ y  is Edefof eate whenA_ISit is prface so keappedtse 8_EC_HOST_ARGS_FLMOT0

/T)

E3NO_VALUE - apc_host_arghost a_msis u_iensedlags;
	uint(mo;agsnize lagc faUsl tRAMEMOT0

T)

E3)  EDUMP._EVENcc_host_lagcdatanonterfachec	}pdumpatckcs fi bacUsl tRAMEMOT0

T)

E3)  EID_HATES unPR; suvOT0

T)

E3)  EKB_WAKE_ANGLE.PR; sVENcc_host_lagcdataDis Edefof eAME_STMOT0

/T)

E3NO_VALUE define ._EVENc	sum16ers	 * oecd} arganta, kb_wr m_td inatckcs cUsl tRAMEMOT0

T)

E3)  EERFO._EVENcc_host_lagcdataS consideEeceLPC_	of us {
msis uiensor_if._EVENc	s;
	uint_ensor_ned;
c	}pfne atckcs fi bacUsl tRAMEMOT0

T)

E3)  ET)

#defDRS unPR; suvOT0

T)

E3)  ET)

#deRANGE.PR; sVENcc_host_lagcdataS consideEeceLPC_	of us {
msis uiensor_if._EVENc	s;
	uint_ensor_ned;
agcdataRondiPI_FRine, tr32 f theondi-up, falsfaost_down._EVENc	s;
	uinteondiupatckccksum16ersresefineatagcdataDis Edefof eAME_STMOT0

/T)

E3NO_VALUE define ._EVENc	sum32_t_ is ;
c	}p_ensor_odr,p_ensor_K to ;oc};e
/* Flags forc_host_arga bytes._msis u_iensedlagsnize lagc faUsl tRAMEMOT0

T)

E3)  EDUMP._EVENcc_host_lagcdataFine Etodefine y) *istemsis uEienso- moduln._EVENc	s;
	uintmoduln_8_t coeagcdataFine Eost_a theienso- e. us {
msis uiensor_if._EVENc	s;
	uint_ensor_8_t c[T_IMOT0

/T)

#deCOUNT]oeagcdataArrayuocoa#deienso- 0x9f. E theienso- es 3-axis._EVENc	sum16ers	 * [3*T_IMOT0

/T)

#deCOUNT]oec	}pdumpatckcs cUsl tRAMEMOT0

T)

E3)  EERFO._EVENcc_host_lagcdataS consideEeceLPC_	of us {
msis uiensor_ in ._EVENc	s;
	uint in ;
agcdataS consideEeceLPC_	of us {
msis uiensor_lond con._EVENc	s;
	uintlond con;
agcdataS consideEeceLPC_	of us {
msis uiensor_(CS# _EVENc	s;
	uint(CS#;
c	}pfne atckcs fi bacUsl tRAMEMOT0

T)

E3)  EID_HATE,EMOT0

T)

E3)  ET)

#defDR,PR; suvOT0

T)

E3)  ET)

#deRANGE,S unPR; suvOT0

T)

E3)  EKB_WAKE_ANGLE.PR; sVENcc_host_lagcdataion of thl 32  fnx8e post memo commuteO_EVENc	sum32_t_rat;Lcd} arganta, _ensor_odr,p_ensor_K to , kb_wr m_td inatc};e
/* Flags for/a****************************************************************/
/*
 * Byte cve USBlaty *i */eontrolponse cos __CR ecSC_rUSBlrome aty *i */mST_EcheOST_ARGS_FLC co= 12,
	EC_ESET MOD1t w30apc_host_arghost a_usb_aty */_setamST_EVENs;
	uintusb_rome_ide
	s;
	uintmodheos/* Flags for/ ****************************************************************/
/*
 * Byte cve Ptocoe  a EC_orne ERAMEEC_LP_lp bu verimumh< , _E
eaLPsy the rt. /s tommancompiled instane co C_HOST_ARGS_FLPSTOR14rotoNZE c64ap faGefeptocoe  a EC_orne Efne EC_HOST_ARGS_FLC coPSTOR14ERFOt w4ommc_host_arga bytes._ C_ore_fne Elag faPtocoe  a EC_orne Ell p,Efn<< , _E Hogs;
	32_t_ C_ore_* Checksuommanags* Che rt. /s to Ess of eate ming  withbmothtures: k_ocol
 *
 Hogs;
	32_t_amanag_* Chec
/* Flags for en b Roahiptocoe  a EC_orne  can b Rot
 *   is * If ECll pa< , _Ep r is OSEC_HOST_ARGS_FLC coPSTOR14 sent w4 apc_host_arghost a_ C_ore_ aad<{ ts;
	32_t_sperat; **devByo Ess of edefine EC_Lcksum32_t_* Che   **devring define Efn<< , _E Hos/* Flags for/sug to Eptocoe  a EC_orne EC_HOST_ARGS_FLC coPSTOR14T_DISt w42apc_host_arghost a_ C_ore_s to E{ ts;
	32_t_sperat; **devByo Ess of edefs to EC_Lcksum32_t_* Che   **devring defs to Efn<< , _E Hog_size;
	/*
	[_FLPSTOR14rotoNZE ];e
/* Flags forve****************************************************************/
/*
 * Byte c o Roal-red df0-0x8_lp bu RTCp Protoc data from ECc_hostureshC_Hc_host_arghost a_stcE{ ts;
	32_t_red ;e
/* Flags forc_host_arga bytes._stcE{ ts;
	32_t_red ;e
/* Flags for/be used ry
 arga bytes._stcEC_HOST_ARGS_FLC coRTCoGET  ALUE  w44HOST_ARGS_FLC coRTCoGET ALARMEt 45or/be used a#dery
 arghost a_stcEC_HOST_ARGS_FLC coRTCoSET  ALUE  w46HOST_ARGS_FLC coRTCoSET ALARMEt 47or/ ****************************************************************/
/*
 * Byte cve Pome80ted _amanag __CRodcGefel EC pome80tan "nomeOSckevious P */pC_HOST_ARGS_FLC coPORT80_L AnyBYO/*t 48mmc_host_arga bytes._ ome80_l EC_P */plags;
	16erscodheos/* Flags for/ ****************************************************************/
/*
 * Byte cve Toad */
engineponse cos ESotaevesume nr *sed two uenceLPC_d cons EWe'l es rECallS EC ster bitoid
 *, interfac is EtC haehaviorEmode. *mpd cndl.es rVtocol *0ein
wesumorigin kly sCS#pst.
	 Link.es rVtocol *1t/
#ifndeprbug CPU load */
limitt,omeOS EC fy tcontrol.f __CROST_ARGS_FLC coDOWN = 16ETOP =ESHOLnt w50ROST_ARGS_FLC coDOWN = 1GETOP =ESHOLnt w51tet addreotocol *0ec_hostC.  Daopaq32erYoudk of def.
 *Swesume ny.  DatONA_IS EC get/on 3onse cos eo mr motdyEiensk._ __CRodcVercol *0e-fof eC_Hc_host_arghost a_load */_setaEha bholdplags;
	uint_ensor_ in e
	s;
	uintEha bhold_ide
	s;
	16ers
l 32;os/* Flags for/seVercol *0e-fgf eC_Hc_host_arghost a_load */_getaEha bholdplags;
	uint_ensor_ in e
	s;
	uintEha bhold_ide

/* Flags forc_host_arga bytes._load */_getaEha bholdplags;
	16ers
l 32;os/* Flags foret addreotocol *1ec_hostC.  Davisibln._EVEus {
	ECe fu_Eha bholds CCESS TEMPOP =ESH_WAR SugEVENT_TTEMPOP =ESH_HIGHVENT_TTEMPOP =ESH_HALT,oENT_ITEMPOP =ESH_COUNTost eve Toad */
n the hod conlempes
#dt fuor wull return. T fuC.  Dafn<degrees K.es rZeromapped tul,
 * bsilPC_ly igned me debug load */
taskOSEC_Hc_host_	ECeoad */_n the E{ ts;
	32_t_r fu_EC_L[T_ITEMPOP =ESH_COUNT];  ecw */
_Ep rhoonES_tC_Lcksum32_t_r fu_fy _ss ;cdatanontctivh
n oce Ahstabst.C_Lcksum32_t_r fu_fy _max;cdatamaxntctivh
n oce Ahstabst.C_Ls/* Flags for/seVercol *1e-fgf en the Eempes
#dreturn. C_Hc_host_arghost a_load */_getaEha bhold_v1E{ ts;
	32_t__ensor_ned;
s/* Flags fo/am is d f */
#da c_host_	ECeoad */_n the E__CRodcVercol *1e-fof en the Eempes
#dreturn.
bacUsl ept -modvay-s to Ef thcesrsrestems!eC_Hc_host_arghost a_load */_setaEha bhold_v1E{ ts;
	32_t__ensor_ned;
	c_host_	ECeoad */_n the Ecfg;
s/* Flags fo/am is d f */
#dnon is EC_Mr/ ****************************************************************/
/*
 * Bytatet adog insautocceic fy tcontrolpC_HOST_ARGS_FLC coDOWN = 1AUTO ALLECTRwitc52CRodcGefeTMP006 c kibrttol * is EC_MEMMAP_ACC_DC coDMP006oGET CALIBRAT 0x0 f53apc_host_arghost a_tmp006ogeta( kibrttol *lags;
	uintondexe

/* Flags forc_host_arga bytes._lmp006ogeta( kibrttol *lagfloa} c0;agfloa} b0;agfloa} b1;agfloa} b2;

/* Flags for ecSC_rTMP006 c kibrttol * is EC_MEMMAP_ACC_DC coDMP006oSET CALIBRAT 0x0 f5 apc_host_arghost a_lmp006oseta( kibrttol *lags;
	uintondexe
gs;
	;
} 
#define[3];  bu Resefineassatu0schecfloa} c0;agfloa} b0;agfloa} b1;agfloa} b2;

/* Flags for ecRoahirawrTMP006  is EC_MEMMAP_ACC_DC coDMP006oGET RAWint55apc_host_arghost a_tmp006ogetarawrlags;
	uintondexe

/* Flags forc_host_arga bytes._lmp006ogetarawrlagsum32_t_r;  bu I *1/100 Kschecsum32_t_
;  bu I *nVp Host eve ****************************************************************/
/*
 * Bytatbu vKBPe-fMatrix KeyBler,
Pion 2 */__CRodes rRoahikeloit wi can b Ro */
#drawr is  f thtroller,
2 *s;ineBAarga bytes._mkb#_fne .2 *s tONA_ISeC_T_Eeata from ECc ChiSEC_HOST_ARGS_FLC coMKBPE	/*KE  w6 flagsPiovidemine acceizeEabonterfacmatrix : oid
 * C_M tind coeanlumn EC_WIRELESS_SWIC coMKBPEERFOt w61orc_host_arga bytes._mkb#_fne Elagksum32_t_etin;Lcksum32_t_2 *s;ags;
	uint_*/
#decoes/* Flags for/svrimulaei tro#defi EC_WIRELESS_SWIC coMKBPESIMUL*KE OVEt w62apc_host_arghost a_mkb#_simulaeiatrodlags;
	uint(ole
gs;
	;
} 
owe
gs;
	;
} defi leat
/* Flags for ecC the hoi troller,
sby wine EC_IRELESS_SWIC coMKBPESET CONFIGt w64HOST_ARGS_FLC coMKBPEGET CONFIGt w65or/befine EC_Wus {
mkb#_n the _8_t clCCESS MKBPE /* H_ENABLEiug1VdataEhandletroller,
sby wine EC_st ens {
mkb#_n the _v EC_MCCESS MKBPE = 12,SCLLEPERIOD		= Data r,CESS MKBPE = 12,POLL_TIMEOUT		= Data 1,CESS MKBPE = 12,MER8,G_TOSCLLEDELAY	= Data 3,CESS MKBPE = 12,OUTPUT SETTLE		= Data 4,CESS MKBPE = 12,DEBOUNCE_DOWN		= Data 5,CESS MKBPE = 12,DEBOUNCE_UP		= Data 6,CESS MKBPE = 12,FIFONZE *DEPTH		= Data 7VE}for/seC the hod conlempesurikeloiby wine algorithmeC_Hc_host_argmkb#_n the  lags;
	32_t_
 EC__ine ;cdatav EC_Mfiel shC_Hgs;
	uint8_t cocdatathinkfine E(us {
mkb#_n the _8_t c) checksum;
} 
 EC__8_t cocdatawhich	eine Ee Dav EC_Mchecksum16ersiby _uoriod_uso atauoriodpbetw*/
	ssing0of bcan_E Hog far */rt x kenteEC1_ENmST_Eaf8 binontctivne ERAMEis reECmorchecksum32_t_poll_red sut_ucoecsum; suminimumhpC_L-bcanar laxnred . Odcerwdefializda ccanawe m of m; sueug liis teasl.w *sed duf defssing0
 * SPST_ nCOdIf*neverliis inPRe ibhort ovwiis fiel ,rwdeallS Emode.
eaad._t checksum16ersmi _ugs *iby _delay_ucoecsu delaypbetw*/
	set y) *up sutpes ate waitPI_FRAMEiumeo tCttl_Echecksum16erssutpes_tCttl__ucoecksum16ersdebonnce_ own_uso ataliis ost_debonnce.
	 kelo ownEchecksum16ersdebonnce_up_uso ataliis ost_debonnce.
	 keloup  Hog famerimumhdepth	defportihost_fie E(0iugnonkelccanasutpes) checksum;
} fie _max_depthe

/* Flags forc_host_arghost a_mkb#_seta(tthe  lagc_host_argmkb#_n the  n the ;e
/* Flags forc_host_arga bytes._mkb#_geta(tthe  lagc_host_argmkb#_n the  n the ;e
/* Flags for ecRunet i tro ccanaemulaeizeEC_HOST_ARGS_FLC coOVESCLLESEQECTRwitc66Ftus {
	ECkelccanaseq_e flCCESS OVESCLLESEQE	/*K \SugEV	odcGefe_RES_SUine acceizeEC_HESS OVESCLLESEQECLEARiug1VdataCmu adands.
ceEC_HESS OVESCLLESEQEADDiugEVcdataAddEiuemmeo ands.
ceEC_HESS OVESCLLESEQE	/* Sugg3,	ntaSsing runnemorands.
ceEC_HESS OVESCLLESEQECOLL 0xtep4Vthatioll ctmands.
ceEsummaryE is EC_Msforus {
	ECcoll ct_8_t clCCEsum; suInal (frsRld-stccanawa a comman me debug EC. Duf deftimemo,athinPRe ibcan_Ed, distskS#pstOST_EVENT_IOVESCLLESEQE   0xDONE	= Data r,Cst ec_host_argcoll ct_iuemm{Hgs;
	uint8_t cocdatathinkfine E(us {
	ECcoll ct_8_t c)EC_Msforc_host_arghost a_kelccanaseq_etrlplags;
	uint(mo;T/evC* determi tCMD (us {
	ECkelccanaseq_e f) checknize lagcc_host_lagcds;
	uinttctivhocdatatthen activh
EVENc	s;
	uints {_iuemso ataoid
 * C_Miuems
EVENc	ataion of tiuemmbee Ahdefine st.C_Lcc	s;
	uint(ur_iuem;
c	}p_RES_S;ENcc_host_lagcdatagcdnisAbsnlutealiis ost_ld-stccan,snt sured,omeOS ECagcdnisssing0of t i snds.
ce.agcdni_Lcc	s;
	32_t_red _ucoecc	s;
	uint_can[0]o atakelccana is EC_Mc	}padd;ENcc_host_lagcds;
	uint_RErt_iuem;g faFirstEiuemmeo it is pEVENc	s;
	uints {_iuemso ataNid
 * C_Miuems
eo it is pEVENc} coll ct;oc};e
/* Flags forc_host_arga bult_kelccanaseq_etrlplagsnize lagcc_host_lagcds;
	uints {_iuemso ataNid
 * C_Miuems
EVENc	ataDis  f tha theiuemmEVENc	c_host_argcoll ct_iuemmiuem[0]oENc} coll ct;oc};e
/* Flags for/ ****************************************************************/
/*
 * Byte cve T fuor wull returnponse cos __CR ecRt.  TEfuor wull returnpfne EC_HOST_ARGS_FLC coTEMPOT)

#deGET ERFOt w70apc_host_arghost a_r fu__ensor_getafne Elagksumuintode

/* Flags forc_host_arga bytes._l fu__ensor_getafne Elag rege_ensor_nt m[32];ecs;
	uint_ensor_ in e

/* Flags for/ ****************************************************************/
/*
 * Byte cRodes rSota:p args and pasne ECdefin87*sed 
#define	defpgeri n thlicttown dACPIctua*/
	EC_H_amai/as  kly sPC_	deft inwrCmori	 * not * GNelS EC ACPI sPceize camberti._ __CRod****************************************************************/
/*
 * Byte c ecoargs */n/ponse cos __CRodes roargs */n/pmesk0 Protoc data from ECc_hostures,atreg me dea#deof t i  argA_ISe*/n/ponse cos berti._ __Cc_host_arghost a_hgs *e*/n/_ine  { ts;
	32_t_ine ;os/* Flags forc_host_arga bytes._hgs *e*/n/_ine  { ts;
	32_t_ine ;os/* Flags for/be used a#dery
 arga bytes._hgs *e*/n/_ine  C_HOST_ARGS_FLC coAG_TOEVENToGET B **   **fin87HOST_ARGS_FLC coAG_TOEVENToGET SMIPLPC_Sfin88HOST_ARGS_FLC coAG_TOEVENToGET SCIPLPC_Sfin89HOST_ARGS_FLC coAG_TOEVENToGET WAKE_LPC_S f8dor/be used a#dery
 arghost a_hgs *e*/n/_ine  C_HOST_ARGS_FLC coAG_TOEVENToSET SMIPLPC_Sfin8aHOST_ARGS_FLC coAG_TOEVENToSET SCIPLPC_Sfin8bHOST_ARGS_FLC coAG_TOEVENToCLEARi**   **fin8cHOST_ARGS_FLC coAG_TOEVENToSET WAKE_LPC_S f8eHOST_ARGS_FLC coAG_TOEVENToCLEAR B **   * f8fCRod****************************************************************/
/*
 * Byte c ecr*/
#deonse cos __CR ecEhandl/disandl LCD
 thel2 GooC_HOST_ARGS_FLC coSWITCH_ENABLE_BKLIGHTsio90apc_host_arghost a__*/
#d_nsandl_ thel2 Goolagksumuintehandleat
/* Flags for ecEhandl/disandl WLAN/BluetoothpC_HOST_ARGS_FLC coSWITCH_ENABLE_WIREL Accio91PROTO_VERSIOVERoSWITCH_ENABLE_WIREL Acc1flagsVtocol *02post a;anone from EC__Cc_host_arghost a__*/
#d_nsandl_wir lnag_v0olagksumuintehandleat
/* Flags for ecVercol *1e Protocol c_host_arghost a__*/
#d_nsandl_wir lnag_v1Elag faF* Argno nsandl  RemC_Hcksumuintnow_8_t coeag/evWhich	 * Argno copy,omeOSnow_8_t cmC_Hcksumuintnow_ine ;oecsum; su_lpc_hno mu vetehandle
indS3,ckfchang're.
	 sume n S0->S3m; sueran_i con._ (ORemovfine Eul,
 istdisandlf
 debug S0->S3m; sueran_i con.)_t checksumuint_usuest_8_t coeag/evWhich	 * Argno copy,omeOS_usuest_8_t c checksumuint_usuest_ine ;os/* Flags for/beVtocol *12.
from EC__Cc_host_arga bytes.__*/
#d_nsandl_wir lnag_v1Elag faF* Argno nsandl  RemC_Hcksumuintnow_8_t coeag/ev_lpc_hno mu vetehandle
indS3 checksumuint_usuest_8_t coe
/* Flags for/ ****************************************************************/
/*
 * Byte cve GPIO3onse cos EOineta	EC_RES_TonPto afps to Epione i ssedb*/
	disandlf. __CR ecSC_rGPIO3sutpes hl 32 C_HOST_ARGS_FLC coGPIO_

/*io92apc_host_arghost a_gpio_setElag regent m[32];ecs;
	uinthl ;

/* Flags for ecGefeGPIO3hl 32 C_HOST_ARGS_FLC coGPIO_G
/*io93apc_host_arghost a_gpio_getElag regent m[32];e
/* Flags foc_host_arga bytes._gpio_getElags;
	uinthl ;

/* Flags for e****************************************************************/
/*
 * Byte cve I2C3onse cos EOineta	EC_RES_T usnoflpshps to Epione i -stAn0-0xst. __CRodes rTODO(crosbug.ons/p/23570):de
 seponse cos sed dodef (frted undul,
 ises rECmovte_soon._ UsGS_FLC coI2C_XFERde.
eaad._ __CR ecRt.  I2C3bu EC_WIRELESS_SWIC coI2C_ sent w9 apc_host_arghost a_i2c_ aad<{ ts;
	16ersaddr;  ec8- setaddrfi E(7- setshif st.ite p*checksumuintst t_* Che  ecEiRemov8eAME16.rchecs;
	uint ome;ecs;
	uintsperat;L
/* Flags foc_host_arga bytes._i2c_ aad<{ ts;
	16ers is ;
s/* Flags for/sug to EI2C3bu EC_WIRELESS_SWIC coI2C_T_DISt w95apc_host_arghost a_i2c_s to E{ ts;
	16ers	 * oecs;
	16ersaddr;  ec8- setaddrfi E(7- setshif st.ite p*checksumuintwas a_* Che  ecEiRemov8eAME16.rchecs;
	uint ome;ecs;
	uintsperat;L
/* Flags for e****************************************************************/
/*
 * Byte cve Cty */
it wilonse cos EOineta	EC_RES_T usnoflpshps to Epione i An0-0xst. __CRod 2 pcelaty */
it wilm th/
#dtefssop aty *i */*/
	 ttoo lost_f pceliumeolengdisaty */
*/
	 ttoo liSEC_HOST_ARGS_FLC co,
	EC_E4,

ROwitc96FAN_SPEED_ENsion,
	EC_E4,

ROwi1
tus {
	ECaty */_controlamST_EVEN,
	EC_E4,

ROw_NON = SugEVEN,
	EC_E4,

ROw_IDLEVEN,
	EC_E4,

ROw_DIS,
	EC_,ost ec_host_arghost a_aty */_control { ts;
	32_t_iodhe**devus {
aty */_controlamST_EC_Ls/* Flags for/s****************************************************************/
/*
 * Byte cve Ctesolilonse cos EOineta	EC_RES_T usnoflpshps to Epione i -stAn0-0xst. __CRod Snapsh*/ponesolilsutpes s 255 bf thry
 byS_FLC co,ONSOL14 sen EC_HOST_CMD_REGC co,ONSOL14SNAPSHO/*t 97CRodes rRoahiSPST_chutwo pa is  fmeOS_avte_snapsh*/. can b Rot
 *   is null-oo mintt mec_ha E.  Emptyec_ha E,ckfchanr  is noMmslles rECmainemorsutpesiSEC_HOST_ARGS_FLC co,ONSOL14 sen*t 98or/ ****************************************************************/
/*
 * Byte cRodes rCuntspe	 ttoo lop * Thsutpes kfchan
l too loiChromes. can b 2 paun Chrome ECl too l, jwithdif weuenceLPC_s not co detereterletsCC_m_ISit is pED_HEADER = 12,COMMANDiSEC_HOST_ARGS_FLC co= 6,
	ECCUTEOFFSt 99or/a****************************************************************/
/*
 * Byte cve USBlrome muxtcontrol. __CRodes rr*/
#deUSBlmuxt thet is pro autocceic _*/
#da E. caheOST_ARGS_FLC co= 12MUXSt 9aapc_host_arghost a_usb_muxtlags;
	uintmux;L
/* Flags for e****************************************************************/
/*
 * Byte cve LDOs / FETstcontrol. __CRus {
	ECldo_it wil{ENT_ILDOE	/*KEEOFFST_EV	odcbug eDO / FET is ahes  ownEchecT_ILDOE	/*KEEONiug1Vdatabug eDO / FET is ONi/EpioviiPI_Fp * Th*Host event ce*/
#deon/speka eDO. caheOST_ARGS_FLC coLDOE	
/*io9b ec_host_arghost a_ldo_setElags;
	uintondexe
gs;
	;
} it wiec
/* Flags for en b GefeeDO it wiOSEC_HOST_ARGS_FLC coLDOEG
/*io9c ec_host_arghost a_ldo_getElags;
	uintondexe

/* Flags forc_host_arga bytes._ldo_getElags;
	uintit wiec
/* Flags for e****************************************************************/
/*
 * Byte cve P * Thfne . __CRodes rGefep * Thfne .SEC_HOST_ARGS_FLC coP= 3,
ERFOt w9dmmc_host_arga bytes._ o* T_fne Elagksum32_t_usb_dev_ in e
	s;
	16ers
olta*/_ace
	s;
	16ers
olta*/_sysange
	s;
	16ersetransf_sysange
	s;
	16ersusb_atransf_limit;

/* Flags for e****************************************************************/
/*
 * Byte cve I2C3passthrustane co C_HWIRELESS_SWIC coI2C_PASSTHRUt w9eCR ecRt.   is maife*/
#define ,sntssne Efs ads to EC_HOST_ARGS_FLI2C_   0x sen	 Write 5)p bu veskFRAME ddrfi EC_HOST_ARGS_FLI2C_ADDR_LPC_	0x3ff pOST_ARGS_FLI2C_	/*NG)
NAK	 Data reave Tran_55 bwaead thlagnowdlfgst.C_HOST_CMD_REGI2C_	/*NG)
TIMEOUT	 Write p*devTed sut duri */*ran_55 b__CRod Any eECy cC_HOST_CMD_REGI2C_	/*NG)
EC_RE	(_FLI2C_	/*NG)
NAK |_REGI2C_	/*NG)
TIMEOUTeapc_host_arghost a_i2c_passthru_msg<{ ts;
	16ersaddr_8_t cocve I2C3sl of addrfi E(7eAME10EUnus)reter8_t c checksum16erslenocdatans at EC_M< , _E
}; aad<AMEs to EC_Ls/* Flags forc_host_arghost a_i2c_passthruolagksumuintsome;	cve I2C3rome oid
 * C_Hcksumuintnum_msgso ataNid
 * C_Mntssne c checc_host_arghost a_i2c_passthru_msg<msg[]oecsu Dis Edefs to ERAME #demtssne c th thn (frntt meanr  C_Ls/* Flags forc_host_arga bytes._i2c_passthruolagksumuinti2c__RES_S;	ntaSsiS_SUfine E(REGI2C_	/*NG)
...) C_Hcksumuintnum_msgso ataNid
 * C_Mntssne c  comman meC_Hcksumuint/*
	[]o dataDis E aad<bySntssne c thn (frntt meanr  C_Ls/* Flags for e****************************************************************/
/*
 * Byte cve P * Thintt usdeleerete i C_HWIRELESS_SWIC coHANG_DED 0xt w9fCR ecRt.szes_defssing0deleerete iol *red * C_Hve P * Thintt usdefi lecC_HOST_CMD_REGHANG_	/* SEONoP= 3,
,
	/S s Data re eod Lri nlo lecC_HOST_CMD_REGHANG_	/* SEONo 12,CLOSE   * Write pr
 od Lri odinedcC_HOST_CMD_REGHANG_	/* SEONo 12,OPEN    * Write2e eodsSsing0of AP S3->S0ueran_i con (P */emorsrsrestmemoromeOS_usuest)cC_HOST_CMD_REGHANG_	/* SEONo
	/UME **   * EC is3prR ecRt.szes_defcancel0deleerete iol *__CRod P * Thintt usr lna lecC_HOST_CMD_REGHANG_	/OPEONoP= 3,
REL ASEi EC is8)CRod Any  args and paromeOSAP receivlecC_HOST_CMD_REGHANG_	/OPEONoAG_TOCOMMAND * EC is9e eodsSsop o. usd0of AP S0->S3ueran_i con (_usuestemorsrsahes/emor own)cC_HOST_CMD_REGHANG_	/OPEONoSUSPEND **   * EC is1re eod
nisIf*neverfine  */ an oa,
 t i sRemovfiel sh  Dafgned m,.g is ne0deleerete iA_IS ed * f Eitingedws
 * repioviieprbug AP aPway defssing0
 * delee ed * capsithsut ren the hoine anyeof t i sRemovdeleerete i set y) c atSotaevesu capyou  withckeviousl dk of n the hoedueug liissut Chro_HOST_CMD_REGHANG_	/* SENOWMASK    0  /1 Write3re eod
nisIf*neverfine  */ an oa,
 t i sRemovfiel sh  Dafgned m (kncluhe A
nisheIHANG_	/* SENOW)ws
 * repioviieprbug AP aPway defssop 
 * delee ed * capsithsut ren the hoine anyeof t i sRemovdeleerete i set y) c hro_HOST_CMD_REGHANG_	/OPENOWMASK    0  /11 Write31eapc_host_arghost a_dele_rete i lag faF* Ar;ineBAREGHANG_*rchecksum32_t_8_t coeag/evTed sut e. msec befsll gineraei */hargs */n/,ckfcehandle
checksum16ershgs *e*/n/_red sut_msecoeag/evTed sut e. msec befsll gineraei */warm. AP */,ckfcehandle
checksum16erswarm_ AP */_red sut_msecoes/* Flags for/s****************************************************************/
/*
 * Byte cve Ctse cos f thc too loaty *i */__CRodes rTEmodeeqt i sed insta
#d-a,
  args and pardefeC deleta is   ager,i */*/
ctua*ty */
it wilm th/
#d(v2.g isupne EC_HOST_ARGS_FLC co,
	EC_E	/*KE  wa flagsSmmCommandsCost_ld-st args and parC_Wus {
aty */_st wig and pa_lag,
	EC_E	/*KE3)  EGET S/*KE,ag,
	EC_E	/*KE3)  EGET /
/* ,ag,
	EC_E	/*KE3)  ESET /
/* ,ag,
	EC_E	/*KE3NUM3)  Sost evectuaKnownEhost  oid
 *s sed do_ARGmeanr .sRt (is*sed 
#define	f thcler,-ach vaicctuapost a,ewhich	  Dadelddlf
 debug memmicular uenceLPC_d cons 
rC_Wus {
aty */_st wighost a lagCS /
/* o,
G_VOLTAGE,O    D*devaty */rs
olta*/
limit
checCS /
/* o,
G_CURRENT,O    D*devaty */rsetransfelimit
checCS /
/* o,
G_INPUT CURRENT, D*devaty */rsrepes etransfelimit
checCS /
/* o,
G_	/*NG),O    D*devaty */r-ach vaicp_RES_SU HogCS /
/* o,
G_OPT 0x,O    D*devaty */r-ach vaicpoptizes_EVEN ecoawlm dyEio far?U HogCS NUM3BASE /
/* SVAEN/faRt (itf thCONFIGo,
	EC_,
,
OFILE_OVEC_IDEp Protocol cCS /
/* o,U	/OM
,
OFILE_MI SugEx10000, cCS /
/* o,U	/OM
,
OFILE_MAXSugEx1n stVAEN/faORemovcwiteOScost  rt (is*goeanr ...EC_Msforc_host_arghost a_aty */_st wiflags;
	uint(mo;TO dataus {
aty */_st wig and pa_checknize lagcc_host_lagcd/tanonterfachec	}pgetast wiecagcc_host_lagcds;
	32_t_post ; dataus {
aty */_st wigcost  chec	}pgetapost ;cagcc_host_lagcds;
	32_t_post ; datacost  defof ei_Lcc	s;
	32_t_
l 32;cdatav Euf defsf ei_Lcc} setapost ;oc};e
/* Flags forc_host_arga bytes._aty */_st wiflagsnize lagcc_host_lagcd;
	 ace
	cd;
	 chg_
olta*/e
	cd;
	 chg_etransfe
	cd;
	 chg_repes_etransfe
	cd;
	 c to_st wigof_aty */;ec	}pgetast wiecagcc_host_lagcds;
	32_t_
l 32;oc	}pgetapost ;ckcc_host_lagcdatanone  is papped ti_Lcc} setapost ;oc};e
/* Flags foreod
nisSetamerimumhc too loaty *i */etransfiSEC_HOST_ARGS_FLC co,
	EC_E4URRENTo 1MIxt wa apc_host_arghost a_atransf_limitElagksum32_t_limit;*deve. mA C_Ls/* Flags for e
nisSetamerimumhexoo nalep * ThetransfiSEC_HOST_ARGS_FLC co#de_P= 3,
4URRENTo 1MIxt wa2apc_host_arghost a_PST_ o* T_atransf_limitElagksum32_t_limit;*deve. mA C_Ls/* Flags for e****************************************************************/
/*
 * Byte cve Sming0 ttoo lopass-through*__CRod Gefe/sSeta16- setsming0 ttoo lo agisemo*pC_HOST_ARGS_FLC coSBx sen_WORD  * fb0ROST_ARGS_FLC coSBxT_DIS_WORD   fb1CRod Gefe/sSetac_ha Etsming0 ttoo lopost memo*
nise accet meas SMBUS "s0-0x"iSEC_HOST_ARGS_FLC coSBx sen_BLOCK   fb2ROST_ARGS_FLC coSBxT_DIS_BLOCK  fb3apc_host_arghost a_sb_rdplags;
	uint
#g;a
/* Flags forc_host_arga bytes._sb_rd_wordplags;
	16ers
l 32;os/* Flags forc_host_arghost a_sb_wr_wordplags;
	;
} 
#g;ags;
	16ers
l 32;os/* Flags forc_host_arga bytes._sb_rd_s0-0xplags;
	;
} 	 * [32];e
/* Flags forc_host_arghost a_sb_wr_s0-0xplags;
	;
} 
#g;ags;
	16ers	 * [32];e
/* Flags for e****************************************************************/
/*
 * Byte cve Sysangeonse cos __CRodes rTODO(crosbug.ons/p/23747):de
modeeqa n thuse Ahst m, sedceliumdoelf wes rnemmanarily. AP */ebug EC.  Rest m def"amne "rsrsao meha Etsimilar?SEC_HOST_ARGS_FLC coREBnd tEC  fd2
cve Ctse corC_Wus {
arga P */_e flCCESS REBnd tCAN= LiugEV**   **f faCancel0a uestemor AP */echecT_IREBnd tJUMP_ROiug1,*   **f faJumptdefROisithsut reP */emorchecT_IREBnd tJUMP_RWuggader co*f faJumptdefRWisithsut reP */emorchec fa( and pa_3bwaeajumptdefRW-B)rchecT_IREBnd tCOLntep4V  **   **f faCold- AP */echecT_IREBnd tDISABLE_JUMPiug5,*f faDisandl jumptteasl.SPST_ AP */echecT_IREBnd tHIBERNATEST_6   **f faHibo natrnhed*Host eveaFine Eost_arghost a_s P */_ec.s P */_fine EC_WIRELESS_SWIREBnd t   0x sSERVED00  /11 Write0)**devWas ren vo lo aquesooC_HOST_ARGS_FLREBnd t   0xON_AP_SHUTDOWN1 Write p* bu ReP */eaf8 biAPsahes ownEcheHc_host_arghost a_s P */_ecplags;
	uint(mo;    **     /evus {
	ECa P */_e flC_Hcksumuint8_t co    D* **devreGS_FLREBnd t   0x* C_Ls/* Flags for e
nisGn 3moe acceizeEzeEl EC EC3panic. can b Ro */
#dvariandl-length plate ac-deuestnsfepanic3moe acceize* GNelSpanic.h
nise aeretThe iSEC_HOST_ARGS_FLC coGET /
NIC
ERFOt wd3T*******************************************************************/
/*
 * Byte cvent cACPI onse cos can b  used a Dav EC_MONLY.
	 */
	ACPI onse co/ is  rome._ __CR ent cACPI Rt.  Ed
 dbst.C*ntroll * caes rTEmod aadt,omeOSACPI memo loiFlae.
	 */
	EC3(REGACPI_MEM_*). can b UllS EC foortiemorands.
ce: can b    -ug to E_FLC coACPI_ sen*defT_ILPC_ADDR_ACPI_C cn b    -ugai eRAME_STLPC_C c,
,ENDINGEUnu_defcmu an b    -ug to Eaddrfi EdefT_ILPC_ADDR_ACPI_DATAn b    -ugai eRAME_STLPC_C c,
DATAEUnu_defsetn b    -uRt.  appedtRmeOST_ILPC_ADDR_ACPI_DATAn b_HOST_ARGS_FLC coACPI_ sen*e ECCR ent cACPI g to E_d
 dbst.C*ntroll * caes rTEmod aadt,omeOSACPI memo loiFlae.
	 */
	EC3(REGACPI_MEM_*). can b UllS EC foortiemorands.
ce: can b    -ug to E_FLC coACPI_T_DIStdefT_ILPC_ADDR_ACPI_C cn b    -ugai eRAME_STLPC_C c,
,ENDINGEUnu_defcmu an b    -ug to Eaddrfi EdefT_ILPC_ADDR_ACPI_DATAn b    -ugai eRAME_STLPC_C c,
,ENDINGEUnu_defcmu an b    -ug to Ev Euf defT_ILPC_ADDR_ACPI_DATAn b_HOST_ARGS_FLC coACPI_T_DISt w81CR ent cACPI Qul Ch_d
 dbst.C*ntroll * caes rTEmodcmu aeqt i rtieso-ord Thiit e.  EC stransfnetpestemorhargs */n/s,eatecbactetsCt i K bulttan "ndeft in1- tsst.ondex of t i iit (e*/n/p0x00000001Hos1VE_ISe*/n/pe ECCCCCCCugg32)rly c0aife*/Se*/n/pwa a estemoiSEC_HOST_ARGS_FLC coACPI_QU/* OEVENTpe E4ap faV EC_Maddrfi esee. ACPI memo loiFlae, f thet. /s to Eonse cos __CR ecMemo loiFlae.
t8_t da of edefREGACPI_MEM_
	EC_RE
4URRENTEC_HOST_ARGS_FLACPI_MEM_
	EC_RESK    0  /110x00Rodes rTesoolond con;fs toemorv Euf anr  updao sCtergs anplim/n/p< , edef(ntry -es rv Eufne EC_HOST_ARGS_FLACPI_MEM_TESTMASK    0  /1110x01cve T rgs anplim/n/;fs to _Ehnr *sed fgned m EC_HOST_CMD_REGACPI_MEM_TESTOCOMP 1MENTE1110x02
cve Kroller,
 thel2 Goob12 GonES_tperc/n/p(Cdef100)cC_HOST_CMD_REGACPI_MEM_KEYBOARco= CKLIGHTsio03cve DPTF Ty */t Fan Dutyp(C-100,intryERAME uto/none)cC_HOST_CMD_REGACPI_MEM_ALLEDUTY    0  /1110x04CR ent cDPTF l futEha bholds. Any of t i EC's l futreturnC.canak of uptdeftwolengondeuestnsfeEha bholds cetachne	deft im.addrecon of thl 32  fnx8e IDm_ISitgisemoerete mind tuhich	ienso- es affT_Eeat debug P =ESHOLnt pa_COMMITm_ISitgisemos.addreP =ESHOLntitgisemoeuseprbug st m T_ITEMPOT)

#define E schnm
ctuaaprbug memo l-ma#pst.returnC.addreCOMMITtitgisemoea#plieprbuose set y) c hron b  us sch mdoelad the coatrn dyEway def aad<btheneug loa bholdpset y) cA_IS ECmselves,ainte usnoa loa bholdpmodcrossedueug APsstabs aPway defrete mind capshich	ien