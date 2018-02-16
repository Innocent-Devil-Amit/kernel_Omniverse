/*
 * Author(s)......: Horst  Hummel    <Horst.Hummel@de.ibm.com>
 *		    Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * Copyright IBM Corp. 2000, 2001
 *
 */

#define KMSG_COMPONENT "dasd-eckd"

#include <linux/timer.h>
#include <asm/idals.h>

#define PRINTK_HEADER "dasd_erp(3990): "

#include "dasd_int.h"
#include "dasd_eckd.h"


struct DCTL_data {
	unsigned char subcommand;  /* e.g Inhibit Write, Enable Write,... */
	unsigned char modifier;	   /* Subcommand modifier */
	unsigned short res;	   /* reserved */
} __attribute__ ((packed));

/*
 *****************************************************************************
 * SECTION ERP HANDLING
 *****************************************************************************
 */
/*
 *****************************************************************************
 * 24 and 32 byte sense ERP functions
 *****************************************************************************
 */

/*
 * DASD_3990_ERP_CLEANUP
 *
 * DESCRIPTION
 *   Removes the already build but not necessary ERP request and sets
 *   the status of the original cqr / erp to the given (final) status
 *
 *  PARAMETER
 *   erp		request to be blocked
 *   final_status	either DASD_CQR_DONE or DASD_CQR_FAILED
 *
 * RETURN VALUES
 *   cqr		original cqr
 */
static struct dasd_ccw_req *
dasd_3990_erp_cleanup(struct dasd_ccw_req * erp, char final_status)
{
	struct dasd_ccw_req *cqr = erp->refers;

	dasd_free_erp_request(erp, erp->memdev);
	cqr->status = final_status;
	return cqr;

}				/* end dasd_3990_erp_cleanup */

/*
 * DASD_3990_ERP_BLOCK_QUEUE
 *
 * DESCRIPTION
 *   Block the given device request queue to prevent from further
 *   processing until the started timer has expired or an related
 *   interrupt was received.
 */
static void dasd_3990_erp_block_queue(struct dasd_ccw_req *erp, int expires)
{

	struct dasd_device *device = erp->startdev;
	unsigned long flags;

	DBF_DEV_EVENT(DBF_INFO, device,
		    "blocking request queue for %is", expires/HZ);

	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	dasd_device_set_stop_bits(device, DASD_STOPPED_PENDING);
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	erp->status = DASD_CQR_FILLED;
	if (erp->block)
		dasd_block_set_timer(erp->block, expires);
	else
		dasd_device_set_timer(device, expires);
}

/*
 * DASD_3990_ERP_INT_REQ
 *
 * DESCRIPTION
 *   Handles 'Intervention Required' error.
 *   This means either device offline or not installed.
 *
 * PARAMETER
 *   erp		current erp
 * RETURN VALUES
 *   erp		modified erp
 */
static struct dasd_ccw_req *
dasd_3990_erp_int_req(struct dasd_ccw_req * erp)
{

	struct dasd_device *device = erp->startdev;

	/* first time set initial retry counter and erp_function */
	/* and retry once without blocking queue		 */
	/* (this enables easier enqueing of the cqr)		 */
	if (erp->function != dasd_3990_erp_int_req) {

		erp->retries = 256;
		erp->function = dasd_3990_erp_int_req;

	} else {

		/* issue a message and wait for 'device ready' interrupt */
		dev_err(&device->cdev->dev,
			    "is offline or not installed - "
			    "INTERVENTION REQUIRED!!\n");

		dasd_3990_erp_block_queue(erp, 60*HZ);
	}

	return erp;

}				/* end dasd_3990_erp_int_req */

/*
 * DASD_3990_ERP_ALTERNATE_PATH
 *
 * DESCRIPTION
 *   Repeat the operation on a different channel path.
 *   If all alternate paths have been tried, the request is posted with a
 *   permanent error.
 *
 *  PARAMETER
 *   erp		pointer to the current ERP
 *
 * RETURN VALUES
 *   erp		modified pointer to the ERP
 */
static void
dasd_3990_erp_alternate_path(struct dasd_ccw_req * erp)
{
	struct dasd_device *device = erp->startdev;
	__u8 opm;
	unsigned long flags;

	/* try alternate valid path */
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	opm = ccw_device_get_path_mask(device->cdev);
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	if (erp->lpm == 0)
		erp->lpm = device->path_data.opm &
			~(erp->irb.esw.esw0.sublog.lpum);
	else
		erp->lpm &= ~(erp->irb.esw.esw0.sublog.lpum);

	if ((erp->lpm & opm) != 0x00) {

		DBF_DEV_EVENT(DBF_WARNING, device,
			    "try alternate lpm=%x (lpum=%x / opm=%x)",
			    erp->lpm, erp->irb.esw.esw0.sublog.lpum, opm);

		/* reset status to submit the request again... */
		erp->status = DASD_CQR_FILLED;
		erp->retries = 10;
	} else {
		dev_err(&device->cdev->dev,
			"The DASD cannot be reached on any path (lpum=%x"
			"/opm=%x)\n", erp->irb.esw.esw0.sublog.lpum, opm);

		/* post request with permanent error */
		erp->status = DASD_CQR_FAILED;
	}
}				/* end dasd_3990_erp_alternate_path */

/*
 * DASD_3990_ERP_DCTL
 *
 * DESCRIPTION
 *   Setup cqr to do the Diagnostic Control (DCTL) command with an
 *   Inhibit Write subcommand (0x20) and the given modifier.
 *
 *  PARAMETER
 *   erp		pointer to the current (failed) ERP
 *   modifier		subcommand modifier
 *
 * RETURN VALUES
 *   dctl_cqr		pointer to NEW dctl_cqr
 *
 */
static struct dasd_ccw_req *
dasd_3990_erp_DCTL(struct dasd_ccw_req * erp, char modifier)
{

	struct dasd_device *device = erp->startdev;
	struct DCTL_data *DCTL_data;
	struct ccw1 *ccw;
	struct dasd_ccw_req *dctl_cqr;

	dctl_cqr = dasd_alloc_erp_request((char *) &erp->magic, 1,
					  sizeof(struct DCTL_data),
					  device);
	if (IS_ERR(dctl_cqr)) {
		dev_err(&device->cdev->dev,
			    "Unable to allocate DCTL-CQR\n");
		erp->status = DASD_CQR_FAILED;
		return erp;
	}

	DCTL_data = dctl_cqr->data;

	DCTL_data->subcommand = 0x02;	/* Inhibit Write */
	DCTL_data->modifier = modifier;

	ccw = dctl_cqr->cpaddr;
	memset(ccw, 0, sizeof(struct ccw1));
	ccw->cmd_code = CCW_CMD_DCTL;
	ccw->count = 4;
	ccw->cda = (__u32)(addr_t) DCTL_data;
	dctl_cqr->flags = erp->flags;
	dctl_cqr->function = dasd_3990_erp_DCTL;
	dctl_cqr->refers = erp;
	dctl_cqr->startdev = device;
	dctl_cqr->memdev = device;
	dctl_cqr->magic = erp->magic;
	dctl_cqr->expires = 5 * 60 * HZ;
	dctl_cqr->retries = 2;

	dctl_cqr->buildclk = get_tod_clock();

	dctl_cqr->status = DASD_CQR_FILLED;

	return dctl_cqr;

}				/* end dasd_3990_erp_DCTL */

/*
 * DASD_3990_ERP_ACTION_1
 *
 * DESCRIPTION
 *   Setup ERP to do the ERP action 1 (see Reference manual).
 *   Repeat the operation on a different channel path.
 *   As deviation from the recommended recovery action, we reset the path mask
 *   after we have tried each path and go through all paths a second time.
 *   This will cover situations where only one path at a time is actually down,
 *   but all paths fail and recover just with the same sequence and timing as
 *   we try to use them (flapping links).
 *   If all alternate paths have been tried twice, the request is posted with
 *   a permanent error.
 *
 *  PARAMETER
 *   erp		pointer to the current ERP
 *
 * RETURN VALUES
 *   erp		pointer to the ERP
 *
 */
static struct dasd_ccw_req *dasd_3990_erp_action_1_sec(struct dasd_ccw_req *erp)
{
	erp->function = dasd_3990_erp_action_1_sec;
	dasd_3990_erp_alternate_path(erp);
	return erp;
}

static struct dasd_ccw_req *dasd_3990_erp_action_1(struct dasd_ccw_req *erp)
{
	erp->function = dasd_3990_erp_action_1;
	dasd_3990_erp_alternate_path(erp);
	if (erp->status == DASD_CQR_FAILED &&
	    !test_bit(DASD_CQR_VERIFY_PATH, &erp->flags)) {
		erp->status = DASD_CQR_FILLED;
		erp->retries = 10;
		erp->lpm = erp->startdev->path_data.opm;
		erp->function = dasd_3990_erp_action_1_sec;
	}
	return erp;
}				/* end dasd_3990_erp_action_1(b) */

/*
 * DASD_3990_ERP_ACTION_4
 *
 * DESCRIPTION
 *   Setup ERP to do the ERP action 4 (see Reference manual).
 *   Set the current request to PENDING to block the CQR queue for that device
 *   until the state change interrupt appears.
 *   Use a timer (20 seconds) to retry the cqr if the interrupt is still
 *   missing.
 *
 *  PARAMETER
 *   sense		sense data of the actual error
 *   erp		pointer to the current ERP
 *
 * RETURN VALUES
 *   erp		pointer to the ERP
 *
 */
static struct dasd_ccw_req *
dasd_3990_erp_action_4(struct dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->startdev;

	/* first time set initial retry counter and erp_function    */
	/* and retry once without waiting for state change pending  */
	/* interrupt (this enables easier enqueing of the cqr)	    */
	if (erp->function != dasd_3990_erp_action_4) {

		DBF_DEV_EVENT(DBF_INFO, device, "%s",
			    "dasd_3990_erp_action_4: first time retry");

		erp->retries = 256;
		erp->function = dasd_3990_erp_action_4;

	} else {
		if (sense && (sense[25] == 0x1D)) { /* state change pending */

			DBF_DEV_EVENT(DBF_INFO, device,
				    "waiting for state change pending "
				    "interrupt, %d retries left",
				    erp->retries);

			dasd_3990_erp_block_queue(erp, 30*HZ);

		} else if (sense && (sense[25] == 0x1E)) {	/* busy */
			DBF_DEV_EVENT(DBF_INFO, device,
				    "busy - redriving request later, "
				    "%d retries left",
				    erp->retries);
                        dasd_3990_erp_block_queue(erp, HZ);
		} else {
			/* no state change pending - retry */
			DBF_DEV_EVENT(DBF_INFO, device,
				     "redriving request immediately, "
				     "%d retries left",
				     erp->retries);
			erp->status = DASD_CQR_FILLED;
		}
	}

	return erp;

}				/* end dasd_3990_erp_action_4 */

/*
 *****************************************************************************
 * 24 byte sense ERP functions (only)
 *****************************************************************************
 */

/*
 * DASD_3990_ERP_ACTION_5
 *
 * DESCRIPTION
 *   Setup ERP to do the ERP action 5 (see Reference manual).
 *   NOTE: Further handling is done in xxx_further_erp after the retries.
 *
 *  PARAMETER
 *   erp		pointer to the current ERP
 *
 * RETURN VALUES
 *   erp		pointer to the ERP
 *
 */
static struct dasd_ccw_req *
dasd_3990_erp_action_5(struct dasd_ccw_req * erp)
{

	/* first of all retry */
	erp->retries = 10;
	erp->function = dasd_3990_erp_action_5;

	return erp;

}				/* end dasd_3990_erp_action_5 */

/*
 * DASD_3990_HANDLE_ENV_DATA
 *
 * DESCRIPTION
 *   Handles 24 byte 'Environmental data present'.
 *   Does a analysis of the sense data (message Format)
 *   and prints the error messages.
 *
 * PARAMETER
 *   sense		current sense data
 *
 * RETURN VALUES
 *   void
 */
static void
dasd_3990_handle_env_data(struct dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->startdev;
	char msg_format = (sense[7] & 0xF0);
	char msg_no = (sense[7] & 0x0F);
	char errorstring[ERRORLENGTH];

	switch (msg_format) {
	case 0x00:		/* Format 0 - Program or System Checks */

		if (sense[1] & 0x10) {	/* check message to operator bit */

			switch (msg_no) {
			case 0x00:	/* No Message */
				break;
			case 0x01:
				dev_warn(&device->cdev->dev,
					    "FORMAT 0 - Invalid Command\n");
				break;
			case 0x02:
				dev_warn(&device->cdev->dev,
					    "FORMAT 0 - Invalid Command "
					    "Sequence\n");
				break;
			case 0x03:
				dev_warn(&device->cdev->dev,
					    "FORMAT 0 - CCW Count less than "
					    "required\n");
				break;
			case 0x04:
				dev_warn(&device->cdev->dev,
					    "FORMAT 0 - Invalid Parameter\n");
				break;
			case 0x05:
				dev_warn(&device->cdev->dev,
					    "FORMAT 0 - Diagnostic of Special"
					    " Command Violates File Mask\n");
				break;
			case 0x07:
				dev_warn(&device->cdev->dev,
					    "FORMAT 0 - Channel Returned with "
					    "Incorrect retry CCW\n");
				break;
			case 0x08:
				dev_warn(&device->cdev->dev,
					    "FORMAT 0 - Reset Notification\n");
				break;
			case 0x09:
				dev_warn(&device->cdev->dev,
					 "FORMAT 0 - Storage Path Restart\n");
				break;
			case 0x0A:
				dev_warn(&device->cdev->dev,
					    "FORMAT 0 - Channel requested "
					    "... %02x\n", sense[8]);
				break;
			case 0x0B:
				dev_warn(&device->cdev->dev,
					    "FORMAT 0 - Invalid Defective/"
					    "Alternate Track Pointer\n");
				break;
			case 0x0C:
				dev_warn(&device->cdev->dev,
					    "FORMAT 0 - DPS Installation "
					    "Check\n");
				break;
			case 0x0E:
				dev_warn(&device->cdev->dev,
					    "FORMAT 0 - Command Invalid on "
					    "Secondary Address\n");
				break;
			case 0x0F:
				dev_warn(&device->cdev->dev,
					    "FORMAT 0 - Status Not As "
					    "Required: reason %02x\n",
					 sense[8]);
				break;
			default:
				dev_warn(&device->cdev->dev,
					    "FORMAT 0 - Reserved\n");
			}
		} else {
			switch (msg_no) {
			case 0x00:	/* No Message */
				break;
			case 0x01:
				dev_warn(&device->cdev->dev,
					 "FORMAT 0 - Device Error "
					 "Source\n");
				break;
			case 0x02:
				dev_warn(&device->cdev->dev,
					    "FORMAT 0 - Reserved\n");
				break;
			case 0x03:
				dev_warn(&device->cdev->dev,
					    "FORMAT 0 - Device Fenced - "
					    "device = %02x\n", sense[4]);
				break;
			case 0x04:
				dev_warn(&device->cdev->dev,
					    "FORMAT 0 - Data Pinned for "
					    "Device\n");
				break;
			default:
				dev_warn(&device->cdev->dev,
					    "FORMAT 0 - Reserved\n");
			}
		}
		break;

	case 0x10:		/* Format 1 - Device Equipment Checks */
		switch (msg_no) {
		case 0x00:	/* No Message */
			break;
		case 0x01:
			dev_warn(&device->cdev->dev,
				    "FORMAT 1 - Device Status 1 not as "
				    "expected\n");
			break;
		case 0x03:
			dev_warn(&device->cdev->dev,
				    "FORMAT 1 - Index missing\n");
			break;
		case 0x04:
			dev_warn(&device->cdev->dev,
				 "FORMAT 1 - Interruption cannot be "
				 "reset\n");
			break;
		case 0x05:
			dev_warn(&device->cdev->dev,
				    "FORMAT 1 - Device did not respond to "
				    "selection\n");
			break;
		case 0x06:
			dev_warn(&device->cdev->dev,
				    "FORMAT 1 - Device check-2 error or Set "
				    "Sector is not complete\n");
			break;
		case 0x07_warn(&device->cdev->dev,
				    "FORMAT 1 - Device c
		ca Setd a\n");
 danalmple    "Sectorte\nar
			break;
		case 0x07_warn8ev_warn(&device->cdev->dev,
				    "FORMAT 1 - Device check-s1 not as "
	on "
			break;
		case 0x07_warn9ev_warn(&device->cdev->dev,
				    "FORMAT 1 - Device check-spond ady			break;
		case 0x07_warnAev_warn(&device->cdev->dev,
				    "FORMAT 1 - DevicePointephf tca pa\n");
 dide    "Sectormplete\nar
			break;
		case 0x07_warnBev_warn(&device->cdev->dev,
				    "FORMAT 1 - DeviceM\n");
e offlina\n");
 

				break;
		case 0x07_warnCev_warn(&device->cdev->dev,
				    "FORMAT 1 - Device r			 mo not (msg_nline o			break;
		case 0x07_warnDev_warn(&device->cdev->dev,
				    "FORMAT 1 - DeviceSeek inte\n");
			break;
		case 0x07_warnEev_warn(&device->cdev->dev,
				    "FORMAT 1 - DeviceCyliissrpa\n");
 didemple    "Sectorte\nar
			break;
		case 0x07_warnFev_warn(&device->cdev->dev,
				    "FORMAT 1 - DeviceOfftifia"
			t be "
				 "resetquired);
			break;
		case 0t:
				dev_wrn(&device->cdev->dev,
				    "FORMAT 1 - Deviceed\n");
			}
		}eak;

	case 0x10:		/2 Format 1 - De2iceandlment Checks */
		switch (msg_no) {
		case 0x00:	/* 8ev_warn(&device->cdev->dev,
				    "FORMAT 1 - De2iceandlm2 error or Se			break;
		case 0x07_warnEev_warn(&device->cdev->dev,
				    "FORMAT 1 - De2iceSupto.. facilitystring[			break;
		case 0x07_warnFev_warn(&device->cdev->dev,
				    "FOT 1 - De2iceMicro CCW_d);
");
 or Set "reset\",
					 sens[8]);
				break
		case 0t:
				dev_wrn(&device->cdev->dev,
				    "FORMAT 1 - De2iceed\n");
			}
		}eak;

	case 0x10:		/3 Format 1 - De3iceandlml (DCTL) */
		switch (msg_no) {
		case 0x00:	/* Fev_warn(&device->cdev->dev,
				    "FORMAT 1 - De3iceAllegianualtiomirack
			break;
		case 0t:
				dev_wrn(&device->cdev->dev,
				    "FORMAT 1 - De3iceed\n");
			}
		}eak;

	case 0x10:		/4 Format 1 - De4a Pinned */
		switch (msg_no) {
		case 0x00:	/* Noev_wrn(&device->cdev->dev,
				    "FORMAT 1 - De4a PHomina\n");
 ar
a or Se			break;
		case 0x07_warndev_warn(&device->cdev->dev,
				    "FORMAT 1 - De4mand ss tar
a or Se			break;
		case 0x07_warn2ev_warn(&device->cdev->dev,
				    "FORMAT 1 - De4manKeytar
a or Se			break;
		case 0x07_warndev_warn(&device->cdev->dev,
				    "FORMAT 1 - In4a Pinnedar
a or Se			break;
		case 0x07_warndev_warn(&device->cdev->dev,
				 "FORMARMAT 1 - In4a Psagsync'Envirin homina\n");
  "resetquirar
a			break;
		case 0x05:
			dev_warn(&device->cdev->dev,
				    "FORMAT 1 - De4a Psagsync'Envirin = 4;
	a\n");
  "resetquirar
a			break;
		case 0x05:
			dev_warn(&device->cdev->dev,
				    "FORMAT 1 - De4a Psagsync'Envirin keytar
a			break;
		case 0x07_warn(&device->cdev->dev,
				    "FORMAT 1 - Device c
	4a Psagsync'Envirin dnnedar
a			break;
		case 0x07_warn8ev_warn(&device->cdev->dev,
				    "FORMAT 1 - De4a PHomina\n");
 ar
a or Se;  "resetquirofftifia"
						break;
		case 0x07_warn9ev_warn(&device->cdev->dev,
				    "FORMAT 1 - De4mand ss tar
a or Se; offtifi "resetquira"
						break;
		case 0x07_warnAev_warn(&device->cdev->dev,
				    "FORMAT 1 - De4manKeytar
a or Se; offtifi "resetquira"
						break;
		case 0x07_warnBev_warn(&device->cdev->dev,
				    "FORMAT 1 - De4a Pinnedar
a or Se;  "resetquirofftifia"
						break;
		case 0x07_warnCev_warn(&device->cdev->dev,
				    "FORMAT 1 - De4a Psagsync'Envirin homin "resetquira\n");
 ar
a; offtifia"
						break;
		case 0x07_warnDev_warn(&device->cdev->dev,
				    "FORMAT 1 - De4a Psagsyn'Envirin = 4;
	 "resetquira\n");
 ar
a; offtifia"
						break;
		case 0x07_warnEev_warn(&device->cdev->dev,
				    "FORMAT 1 - De4a Psagsync'Envirin keytar
a;  "resetquirofftifia"
						break;
		case 0x07_warnFev_warn(&device->cdev->dev,
				    "FORMAT 1 - De4a Psagsyn'Envirin dnnedar
a;  "resetquirofftifia"
						break;
		case 0t:
				dev_wrn(&device->cdev->dev,
				    "FORMAT 1 - De4iceed\n");
			}
		}eak;

	case 0x10:		/50:eserv 1 - De5a Pinned */
	"
				displacehecksin) {
	c
	/* andh (msg_no) {
		case 0x00:	/* Noev_wrn(&device->cdev->dev,
				    "FORMAT 1 - De5a Pinned */
	"in ror  "resetquirhomina\n");
 ar
a			break;
		case 0x07_warndev_warn(&device->cdev->dev,
				    "FOT 1 - De5a Pinned */
	"in ror = 4;
	 "resetrar
a			break;
		case 0x05:
			2ev_warn(&device->cdev->dev,
				    "FORMAT 1 - De5a Pinned */
	"in ror keytar
a			break;
		case 0x07_warndev_warn(&device->cdev->dev,
				    "FOT 1 - De5a Pinned */
	"in ror dnned "resetrar
a			break;
		case 0x05:
			8ev_warn(&device->cdev->dev,
				    "FORMAT 1 - De5a Pinned */
	"in ror  "resetquirhomina\n");
 ar
a; offtifia"
						break;
		case 0x07_warn9ev_warn(&device->cdev->dev,
				    "FORMAT 1 - De5a Pinned */
	"in ror = 4;
	ar
a;  "resetquirofftifia"
						break;
		case 0x07_warnAev_warn(&device->cdev->dev,
				    "FORMAT 1 - De5a Pinned */
	"in ror keytar
a;  "resetquirofftifia"
						break;
		case 0x07_warnBev_warn(&device->cdev->dev,
				    "FORMAT 1 - De5a Pinned */
	"in ror dnnedar
a;  "resetquirofftifia"
						break;
		case 0t:
				dev_wrn(&device->cdev->dev,
				    "FORMAT 1 - De5iceed\n");
			}
		}eak;

	case 0x10:		/60:eserv 1 - De6iceU/
			 1 nif Sps/Ovion n "
				switch (msg_no) {
		case 0x00:	/* Noev_wrn(&device->cdev->dev,
				    "FORMAT 1 - De6iceOvion n not  requesA			break;
		case 0x07_warndev_warn(&device->cdev->dev,
				    "FORMAT 1 - De6iceOvion n not  requesB			break;
		case 0x05:
			2ev_warn(&device->cdev->dev,
				    "FORMAT 1 - De6iceOvion n not  requesC			break;
		case 0x07_warndev_warn(&device->cdev->dev,
				    "FORMAT 1 - In6iceOvion n not  requesD			break;
		case 0x07_warndev_warn(&device->cdev->dev,
				 "FORMARMAT 1 - In6iceOvion n not  requesE			break;
		case 0x05:
			dev_warn(&device->cdev->dev,
				    "FORMAT 1 - De6iceOvion n not  requesF			break;
		case 0x05:
			dev_warn(&device->cdev->dev,
				    "FORMAT 1 - De6iceOvion n not  requesG			break;
		case 0x07_warn(&device->cdev->dev,
				    "FORMAT 1 - Device c
	6iceOvion n not  requesH			break;
		case 0t:
				dev_wrn(&device->cdev->dev,
				    "FORMAT 1 - De6iceed\n");
			}
		}eak;

	case 0x10:		/70:eserv 1 - De7ice check-Coequ= dasdl (DCTL) */
		switch (msg_no) {
		case 0x00:	/* Noev_wrn(&device->cdev->dev,
				    "FORMAT 1 - De7iceRCCal retr);
 byta = equ= dasd    "Sectort*/
	"aler				break;
		case 0x07_warndev_warn(&device->cdev->dev,
				    "FORMAT 1 - De7iceRCCa1nce and timple    "Sectorsucg unful			break;
		case 0x05:
			2ev_warn(&device->cdev->dev,
				    "FORMAT 1 - De7iceRCCa1nvaliRCCa2nce and talmple    "Sectorsucg unful			break;
		case 0x05:
			dev_warn(&device->cdev->dev,
				    "FORMAT 1 - In7alid Defecttag-in dur				    "interrion\n");
nce and t			break;
		case 0x07_warndev_warn(&device->cdev->dev,
				 "FORMARMAT 1 - In7aliextraiRCCaed\n");
				break
		case 0x05:
			dev_warn(&device->cdev->dev,
				    "FORMAT 1 - De7alid DefectDCCaion\n");
n "resetquired)pota
 t deimeou				break;
		case 0x07_warndev_warn(&device->cdev->dev,
				    "FORMAT 1 - De7iceM\n");
esd_3ion on a ;e offlin "resetquirtransferete\n");
			break;
		case 0x07_warn(&device->cdev->dev,
				    "FORMAT 1 - Device c
	7iceM\n");
esd_3ion on a ;e offlin "resetquirtransfereinte\n");
			break;
		case 0x07_warn8ev_warn(&device->cdev->dev,
				    "FORMAT 1 - De7alid Defecttag-in frelate    "interrutely, "
nd with ace and t			break;
		case 0x07_warn9ev_warn(&device->cdev->dev,
				    "FORMAT 1 - De7alid Defecttag-in frelate    "interrextrecoved with ace and t			break;
		case 0x07_warnAev_warn(&device->cdev->dev,
				    "FORMAT 1 - De7iceandlmmicro CCW_etry"itinghete    "interrits(inks)ion\n");
			break;
		case 0x06:
			Bev_warn(&device->cdev->dev,
				    "FORMAT 1 - De7a Psaged)pota
 mit on\n");
n "resetquirthe rea pollrupt (this);
			break;
		case 0x06:
			Cev_warn(&device->cdev->dev,
				    "FORMAT 1 - De7a PPnt error.t a tor SetCQR_Ve    "Sectorte(DCTLlers "
		vaileasi)			break;
		case 0x07_warnDev_warn(&device->cdev->dev,
				    "FORMAT 1 - De7ice nnot e(DCTLlers "
		vaileasi    "Sector notdis= equ= oved with a  ri
			break;
		case 0t:
				dev_wrn(&device->cdev->dev,
				    "FORMAT 1 - De7iceed\n");
			}
		}eak;

	case 0x10:		/80:eserv 1 - De8iceAddis);
ale Equipment Checks */
		switch (msg_no) {
		case 0x00:	/* No Message */
			breakx07_warndev_warn(&device->cdev->dev,
				    "FORMAT 1 - De8ice"
				t retrynnot CCW_ "resetquirharddeve 
							break;
		case 0x05:
			dev_warn(&device->cdev->dev,
				    "FORMAT 1 - In8iceUned\n");
esd_3ion on a n "resetquired)pota
  CCW			break;
		case 0x07_warndev_warn(&device->cdev->dev,
				 "FORMARMAT 1 - In8ice"d_3ion on a nhe samransfere    "Sectorte4;
	 "
	zero				break
		case 0x05:
			dev_warn(&device->cdev->dev,
				    "FORMAT 1 - De8ice"d_3ion on a nhe samransfere    "Sectorte4;
	zero				break
		case 0x05:
			dev_warn(&device->cdev->dev,
				    "FORMAT 1 - De8 Instalc*/
		sthe rea sChecks "resetquired);
 t d on\n")vt the p			break;
		case 0x07_warn(&device->cdev->dev,
				    "FORMAT 1 - Device c
	8 Instalcbe "
				fi "
				break;
		case 0x07_warn8ev_warn(&device->cdev->dev,
				    "FORMAT 1 - De8 InSes;	  redretry-itindur				    "interr offlinion\n");
			break;
		case 0x06:
			9ev_warn(&device->cdev->dev,
				    "FORMAT 1 - De8ice nnot e(DCTLlers) ERP
				    "selectio
 t dthe path mlags; redrlasg_			break;
		case 0x07_warnAev_warn(&device->cdev->dev,
				    "FORMAT 1 - De8a Psagupt (this);
the re offlin "resetquirdur				aed with a  ri
			break;
		case 0t:
				dev_wrn(&device->cdev->dev,
				    "FORMAT 1 - De8iceed\n");
			}
		}eak;

	case 0x10:		/90:eserv 1 - De9ice check-Read,, Enableth aSeek  */
		switch (msg_no) {
		case 0x00:	/* Noev_w

	cas Message */
			breakx07_warndev_warn(&device->cdev->dev,
				    "FORMAT 1 - De9ice check-2 error or Se			break;
		case 0x07_warn(&device->cdev->dev,
				    "FORMAT 1 -T 1 - De9iceSetd a\n");
 didemple    "Serte\nar
			break;
		case 0x07_warnAev_warn(&device->cdev->dev,
				    "FORMAT 1 - De9icePointephf tca pa\n");
 dide    "Sectormplete\nar
nghsk\norieck
				break;
		case 0x07_warnEev_warn(&device->cdev->dev,
				    "FORMAT 1 - De9iceCyliissrpa\n");
 didemple    "Sectorte\nar
			break;
		case 0t:
				dev_wrn(&device->cdev->dev,
				    "FORMAT 1 - De9iceed\n");
			}
		}eak;

	case 0x10:		/F Format 1 - DeFiceCon age Path R */
		switch (msg_no) {
		case 0x00:	/* Noev_wrn(&device->cdev->dev,
				    "FORMAT 1 - DeFiceOon on a nTiomirack
			break;
		case 0x07_warndev_warn(&device->cdev->dev,
				    "FORMAT 1 - DeFiceSubsChecksPing until Er Se			break;
		case 0x07_warn2ev_warn(&device->cdev->dev,
				    "FORMAT 1 - DeFiceCon ag instnv Filsk\ns Path R    "interrent Checks) ERur
			break;
		case 0x07_warndev_warn(&device->cdev->dev,
				 "FORMARMAT 1 - InFiceCon til tiomirack
			break;
		case 0x07_warndev_warn(&device->cdev->dev,
				    "FORMAT 1 - DeFiceCon agfah th/
	DCacg unemple    "Sectoras)...izk
			break;
		case 0x07_warn(&device->cdev->dev,
				    "FORMAT 1 - Device c
	FicePointe = (senict retry			break;
		case 0x06:
			9ev_warn(&device->cdev->dev,
				    "FORMAT 1 - DeFiceCon til rel retr);
			break;
		case 0x07_warnAev_warn(&device->cdev->dev,
				    "FORMAT 1 - DeFa Psanv Filsk\ns Path R    "interrtiomirack
			break;
		case 0x07_warnBev_warn(&device->cdev->dev,
				    "FORMAT 1 - DeFa PVoluactualsusprecoveduplex			break;ck mtry extrecoveor Sets.to..til (EER)	break;ckd.h"er_h/
	De, expiresfers;

	da     erp->r 3990_HEER_PPRCSUSG tobreak;
		case 0x06:
			Cev_warn(&device->cdev->dev,
				    "FORMAT 1 - DeFiceSubsCheckss1 not  be "
				 "resetquird);
omirk
			break;
		case 0x07_warnDev_warn(&device->cdev->dev,
				    "FORMAT 1 - DeFiceCon til s1 not the pato	 "resetquird)
							break;
		case 0x05:
			Eev_warn(&device->cdev->dev,
				    "FORMAT 1 - DeFice nnotFah t*/
	DCi Writek
			break;
		case 0t:
				dev_wrn(&device->cdev->dev,
				    "FORMAT 1 - DeDiceed\n");
			}
		}eak;

	case 0t:
				d;ck unknownge to ope = (sen- resuldemplehs.
 n
 "FORMupt (nor
 *    03iceunknownge to ope = (senwitch n the f(tring[ERROR, ENGTH];

	s, "03i%rn2",ormat = (se)se 0t:&device->cdev->dev,
			    "Un"An or Set ct send"in ror  nnot offling re				    "ired02x\=%srp->irbing[ERROR)se 0
		case }	nd dasd_3 (msg_ne to ope = (senwit/* end dasd_3990_erp_ac_env_data(struc*
 * DASD_3990_ERP_CLEANUPOM_REJ DESCRIPTION
 *   Handles 24 byte 'Envirod InvaliRejtryr.
 *   ThiRAMETER
 *   erp		current erp
 * RE_hetdsense		current sense data
 *
 * RETURN VALUES
 *   void
 urren'new'* RE_hetdicer to the ERnew*/
static void
d dasd_ccw_req *
dasd_3990_erp_cleanupom
dajt dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->startdev;

	/* firunction = dasd_3990_erp_actionpom
daj first ta(present'.
 *  (_5
 *
 1servCCW\n"resuldework)f (erp->f25] == 10) SNS2ATA
 *
 *_PRESENT	DBF_DEV_EVENT(DBF_WARNING, device,
			  
			    "dasd_3d InvaliRejtryaliemental data present'.
 * 	dasd_3990_erp_bl_env_data(structhar *
{

	srp->retries = 256;
	5else {
		ifnse[1] & 0x10) SNS1_WRITE_INHIBITEDdev_err(&device->cdev->dev,
			  n"An I/Ost with peeivedjn");
/opm=%			caem (h/
	 done ii Writek
			breakter _3990_erp_actionp(structhar *QR_FAILED &&
	 brea {
			switcst oator
 *    -  atus to submit
 *
 * "FORMupt (nor
 *    09mand InvaliRejtryaev_err(&device->cdev->dev,
			  n"An or Set ct send"in ror  nnot/opm=% offling re				ed02x\=%srp->i"09	erp->retr _3990_erp_actionp(structhar *QR_FAILED &&
	 brea urn erp;

}				/* end dasd_3990_erp_actionpom
daj*
 * DASD_3990_ERP_BLOCK_QUS_OUT DESCRIPTION
 *   Handles 24 byte 'EnviroBsubOuteteritys */
	r.
 *   ThiRAMETER
 *   erp		current erp
 * RE_hetdsens VALUES
 *   void
 urrennew* RE_hetdicer to the ERnew*/
static void
d dasd_ccw_req *
dasd_3990_erp_cleanubus_outt dasd_ccw_req * erp)
{

	struct dasd_device *device = erp->startdev;

	/* first time set initial retry counter and erp_function */
	/* and retry once without blocking queue		 */
	/* (this enables easier enqueing of the cqr)		 */
	if (erp->function != dasd_3990_erp_cleanubus_outerp->statuss = 256;
		erp->function = dasd_3990_erp_actionbus_outelse {

		/* issue a message and wait for 'device ready' interrupt */
		dev_erV_EVENT(DBF_WARNING, device,
			  
			    "dasd_3bthe ur.t ritystringg inBOPCsted "
				by    "INTERV  reque	dasd_3990_erp_block_queue(erp, 60*HZ);
	}

	rea urn erp;

}				/* end dasd_3990_erp_actionbus_out*
 * DASD_3990_ERP_BLOCK_D!!\NUPHECK DESCRIPTION
 *   Handles 24 byte 'Environnt Checks */
	r.
 *   ThiRAMETER
 *   erp		current erp
 * RE_hetdsens VALUES
 *   void
 urrennew* RE_hetdicer to the ERnew*/
static void
d dasd_ccw_req *
dasd_3990_erp_cleanuent C_2 errt dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->startdev;

	/* firunction = dasd_3990_erp_actionent C_2 err ((erp->1] & 0x10) SNS1_WRITE_INHIBITEDdev_err(&din) ce->cdev->dev,
			    "Unable*/
	DCi Writek
.t a tonr and ek
			breissue vdrest a te or no "FORMupt (nor
 *    04iceestarresuldebd pawice,e o-r no.ev_err(&device->cdev->dev,
			  n"An or Set ct send"in ror  nnot/opm=% offling re				ed02x\=%srp->i"04	erp->retr _3990_erp_action_1(b) */
	if (ese {
		ifnse[1] & 0 10) SNS2ATA
 *
 *_PRESENT	DBF_DEV_EVENT(DBF_WARNING, device,
			  
			    "dasd_3nnt Checks */
					 "emental data present'.
 * 	dasd_3990_erp_bl_env_data(structhar *
{

	srp->retrd_3990_erp_action_4;

	} thar *
{

	srp-> {
		ifnse[1] & 0x10) SNS1_PERMctl_	DBF_DEV_EVENT(DBF_WARNING, device,
			  
			    "dasd_3nnt Checks */
				once wexhaemtan rel   "INTERVundesireasi erp->retr _3990_erp_action_1(b) */
	if (ese {
		ifwitcst try oandlient Checksc*/
		s- A5 (see Rev_eret statne in xxxghetes = 256;
= 0dev_erV_EVENT(DBF_WARNING, device,
			  
			    "dasd_3Ent Checksc*/
	 relsing until or Se erp->retr _3990_erp_action_1(b) *5
	if (er}rn erp;

}				/* end dasd_3990_erp_actionent C_2 err*
 * DASD_3990_ERP_DCTL
 *
 *_PHECK DESCRIPTION
 *   Handles 24 byte 'Enviroinned */
	r.
 *   ThiRAMETER
 *   erp		current erp
 * RE_hetdsens VALUES
 *   void
 urrennew* RE_hetdicer to the ERnew*/
static void
d dasd_ccw_req *
dasd_3990_erp_cleanutruc_2 errt dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->startdev;

	/* firunction = dasd_3990_erp_actiontruc_2 err ((erp->1] & 0 10) SNS2ACORRECTABLE check m retryo allresen2 err*
 * ssue a messe to operh operatresenpireried m retry} __aterr(&demergce->cdev->dev,
			    "Unableinnedr just ovedurtil rece whe saPCIl   "INTERVfesg_neCCW_a"
						bre
o statetd wisi alloca_env_d nableions wher"in 90@de__aterpanic("Noforse thin) {
rs.
ln\n");
 about*th mawisi ay    "p->r 3"ict retrytrese" (ese {
		ifnse[1] & 0 10) SNS2ATA
 *
 *_PRESENT	DBF_DEV_EVENT(DBF_WARNING, device,
			  
			    "dasd_3Unm retryo allresen2 err*r just ovesry Addres   "INTERVa\n"e cqduplexst ie erp->retr _3990_erp_action_1(b) * thar *
{

	srp-> {
		ifnse[1] & 0x10) SNS1_PERMctl_	DBF_DEV_EVENT(DBF_WARNING, device,
			  
			    "dasd_3Unm retryo allresen2 err*he saupt (nor
   "INTERVonce wexhaemtan erp->retr _3990_erp_action_1(b) */
	if (ese {
		ifwitcst try oandliresen2 errsdev_erV_EVENT(DBF_WARNING, device,
			  
			    "dasd_3Unm retryo allresen2 err*he sacounter and
   "INTERVexhaemtan... erp->retr _3990_erp_action_1(b) *5
	if (er}rrn erp;

}				/* end dasd_3990_erp_actiontruc_2 err*
 * DASD_3990_ERP_DCTL
 OVtl_UN DESCRIPTION
 *   Handles 24 byte 'EnviroOvion nr.
 *   ThiRAMETER
 *   erp		current erp
 * RE_hetdsens VALUES
 *   void
 urrennew* RE_hetdicer to the ERnew*/
static void
d dasd_ccw_req *
dasd_3990_erp_cleanuovion nt dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->startdev;

	/* firunction = dasd_3990_erp_actionovion nF_DEV_EVENT(DBF_INFO, G, device,
			  
			    "NTERVOvion n - \n")erp-ovion n nr-ovion n   "p->r"eor Sets.d "
				by   reque	dasd_etr _3990_erp_action_1(b) *5
	if (ern erp;

}				/* end dasd_3990_erp_actionovion n 
 * DASD_3990_ERP_DCTL
 IA
  1 - D DESCRIPTION
 *   Handles 24 byte 'Envirod DefectPointe 1 - Dr.
 *   ThiRAMETER
 *   erp		current erp
 * RE_hetdsens VALUES
 *   void
 urrennew* RE_hetdicer to the ERnew*/
static void
d dasd_ccw_req *
dasd_3990_erp_cleanuia(s = (set dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->startdev;

	/* firunction = dasd_3990_erp_actionia(s = (se ((erp->1] & 0 10) SNS2ATA
 *
 *_PRESENT	DBF_DEV_EVENT(DBF_WARNING, device,
			  
			    "dasd_3Pointe = (senor Setgheted\n")g the el   "INTERVn")g therese" (ese3990_erp_bl_env_data(structhar *
{

	srp->retrd_3990_erp_action_4;

	} thar *
{

	srp-> {
		ifwitcst upt (nor
 *    06iceSD ctointe = (seniss "
	on "
ev_err(&device->cdev->dev,
			    "i"An or Set ct send"in ror  nnot offling re				    ""ed02x\=%srp->i"06	erp->retr _3990_erp_actionp(structhar *QR_FAILED &&
	 brea urn erp;

}				/* end dasd_3990_erp_actionia(s = (se*
 * DASD_3990_ERP_BLOCK_DOC DESCRIPTION
 *   Handles 24 byte 'Environmd-of-Cyliissrr.
 *   ThiRAMETER
 *   erp		curreny build a\noved:
				* RETURN VALUES
 *   erp		modifir to the ERal cqr
 *d) ERP
 *cqr.tatic void
d dasd_ccw_req *
dasd_3990_erp_cleanuDOCt dasd_ccw_req * erp, cd:
				_har *sense)
{

	struct dasd_device *device = erp->sd:
				_hartdev;

	/* firr(&device->cdev->dev,
			    "ASD ccyliissrpresenfrelacg until ror  nnote ii y Asi
		nt			bre
ost u\n")hecks 5 (see7iceBUGdev_e dctl_cq90_erp_actionp(structd:
				_har *QR_FAILED &&
	 bre/* end dasd_3990_erp_actionDOC*
 * DASD_3990_ERP_BLOCK_DA
 *
 * DESCRIPTION
 *   Handles 24 byte 'Environmental data -inned '.
 *  .
 *   ThiRAMETER
 *   erp		current erp
 * RE_hetdsens VALUES
 *   void
 urrennew* RE_hetdicer to the ERnew*/
static void
d dasd_ccw_req *
dasd_3990_erp_cleanuta(struct dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->startdev;
	char irunction = dasd_3990_erp_actionea(strucF_DEV_EVENT(DBF_INFO, G, device,
			  
			   "nmental data present'.
 * 	bre
o990_erp_bl_env_data(structhar *
{

	srp-> dadon't once wittdiso ald upt (facef (erp->f25] ==7]00) {

Ferp->statd_3990_erp_action_4;

	} thar *
{

	srp> {
		ifwitceatus = DASD_CQR_FILLED;
		}
	}
 urn erp;

}				/* end dasd_3990_erp_actionta(struc*
 * DASD_3990_ERP_CLEANUNO_REC DESCRIPTION
 *   Handles 24 byte 'EnviroNofRry rde 1undr.
 *   ThiRAMETER
 *   erp		curreny build a\noved:
				*
 * RETURN VALUES
 *   erp		pointenew* RE_hetdicer to the ERnew*/
static void
d dasd_ccw_req *
dasd_3990_erp_cleanuno
dact dasd_ccw_req *erp)
{cd:
				_har *sense)
{

	struct dasd_device *device = erp->sd:
				_hartdev;

	/* firr(&device->cdev->dev,
			    "asd_3Pse d"
		ointerry rdeeeiv "
	f1und			bre
o dctl_cq90_erp_actionp(structd:
				_har *QR_FAILED &&
	 bre/* end dasd_3990_erp_actionno
dac*
 * DASD_3990_ERP_CLEANU;
	E_PROD DESCRIPTION
 *   Handles 24 byte 'Enviroask\nPro;
");
r.
 *   This mNo;
:aSeek relr);
 ry actioniss "
	u\n")heck			b	caem Thi"asdweeadon't em (fla*
{ek d with ayet ThiRAMETER
 *   erp		current erp
 * RE_hetdsens VALUES
 *   void
 urrennew* RE_hetdicer to the ERnew*/
static void
d dasd_ccw_req *
dasd_3990_erp_cleanufsk\_pro;t dasd_ccw_req * erp)
{

	struct dasd_device *device = erp->startdev;

	/* firr(&device->cdev->dev,
			  n"Acg until ror  nnot) ERP
			caem (of	    "enpirddeve or Se			bre
o dctl_cq90_erp_actionp(structhar *QR_FAILED &&
	 bre/* end dasd_3990_erp_actionfsk\_pro; 
 * DASD_3990_ERP_DCTL
 IASPECT_ALIAS DESCRIPTION
 *   Handle */
		s interral cqr
 *t with peeivev;

any path n "ase,
			  If all alyes, itier;

	csnterral cqr
 *e given tatdt with psoerh oIf allven tatdt with pcth bevev;

any pat b05:
,
			  If RAMETER
 *   erp		currenr to the current ERP
 ly  buitoved:
				*
 * RETURN VALUES
 *   erp		pointer to the currened pointe
 *,e elNULLtaticc struct dasd_ccw_req *dasd_3990_erp_actionind"
	t_n "as(   "F	t dasd_ccw_req *dctl_ce	struct dasd_deviceq *dctl_cdasd_aefers;

	da; errors)
{

	 ((erp->uildclhe CQ  !test_>uildclhe Cdcl05:
0) tartdev = dev)	DBF_DEdata
 _3990_ed_cldata
(&efers;

	daesw.ebreakDAS	if  dynamuctpav maybeen t pendid b05:
n "asem links
	if (ererp->bit(DASD_CQR_VEFLAG_OFFLINE, &tartdev = devs)) {
		ensedata
  "asd_nse[25] ==0x1E)) {	0)_nse[25] ==7x1E)) {
Fe  "asd_nse[25] ==8x1E)) {67	erp->skDAS	iif  remactt offlinhe ren "aseng is don cut'.vp
 *newS	iif  rewith snhe rebf thesn anulany parreS	iif  wrags;n "ase,
			 S	iif reak;ckd.hn "as_remacte *devi(tartdev = dev)dasd_3te cn anulaeworkthe cureloadt offlin reak;ckd.hreloade *devi(tartdev = dev)da	
 urnerp->uildcev = devs))uitu5 * &3990_EFEAALUECTL
LOGerp->skV_EVENT(DBF_INFO, ERR, tartdev = dev    "FORMATtionoh n "ase,
			 nfrelt with p%p,    "Sector r just woh b05:
,
			  		   tar    "FORMAr(&dnam
(&uildclhe Cdcl05:->dev,
			 )}
		}eak;ckd.h"ck.hresdev_lo_to_l05:_io>uil);itceatus = = deviceuildclhe Cdcl05:p->function = dasd_3990_erp_actionind"
	t_n "asp->f erp;
	}

	DCT 	erp->l erp;
	NULLtatic DASD_3990_ERP_DCTL
 IASPECT_2* DESCRIPTION
 *   Setup  analysdet ERP
	ind"
	t a diinterre sense ERP fu*
 * RE printssdes up a relr);
 or Sets. action, we r* PARAMETER
 *   sense		currendata of the actual error
 *   erp		pointer to the current ERP
 ly  buitoved:
				*
 * RETURN VALUES
 *   erp		pointer to the current) Dtis);
al)*/
static void
d dasd_ccw_req *
dasd_3990_erp_cleanuiad"
	t_2ct dasd_ccw_req * erp, char *sense)
{

	struct dasd_deviceq *dctl_ce	s_fi "
	d_3NULLta-> da */
		data ofrel....FORM (this od InvaliRejtryrFORM (thrp->(e	s_fi "
	d__3NULL)_nse[25] ==0x1) SNS0CTL;
REJECT	erp->stat_fi "
	d_3990_erp_cleanupom
dajthar *
{

	srp> this ouptiovp
  a ded: reas'M (thrp->(e	s_fi "
	d__3NULL)_nse[25] ==0x1) SNS0CIN   BF_I *
 REQ	erp->stat_fi "
	d_3990_erp_cleanuto dctl
	if (er}rnis oBsubOuteteritys */
	r.M (thrp->(e	s_fi "
	d__3NULL)_nse[25] ==0x1) SNS0CQUS_OUT_PHECK	erp->stat_fi "
	d_3990_erp_cleanubus_outt	if (er}rnis onnt Checks */
	rFORM (thrp->(e	s_fi "
	d__3NULL)_nse[25] ==0x1) SNS0CD!!\NMF_I_PHECK	erp->stat_fi "
	d_3990_erp_cleanuent C_2 errthar *
{

	srp> this oinned */
	r	FORM (thrp->(e	s_fi "
	d__3NULL)_nse[25] ==0x1) SNS0C*
 *_PHECK	erp->stat_fi "
	d_3990_erp_cleanutruc_2 errthar *
{

	srp> this oOvion nr	FORM (thrp->(e	s_fi "
	d__3NULL)_nse[25] ==0x1) SNS0COVtl_UN	erp->stat_fi "
	d_3990_erp_cleanuovion nthar *
{

	srp> this oupDefectPointe 1 - Dr.M (thrp->(e	s_fi "
	d__3NULL)_nse[25] ==x10) SNS1_IA
 TRACK  1 - D	erp->stat_fi "
	d_3990_erp_cleanuto(s = (sethar *
{

	srp> this onmd-of-CyliissrrFORM (thrp->(e	s_fi "
	d__3NULL)_nse[25] ==x10) SNS1_EOC	erp->stat_fi "
	d_3990_erp_cleanuDOCthar *
{

	srp> this onmental data pinnerFORM (thrp->(e	s_fi "
	d__3NULL)_nse[25] == 10) SNS2ATA
 *
 *_PRESENT	erp->stat_fi "
	d_3990_erp_cleanuea(structhar *
{

	srp> this oNofRry rde 1undrFORM (thrp->(e	s_fi "
	d__3NULL)_nse[25] ==x10) SNS1_NO_REC  1UND	erp->stat_fi "
	d_3990_erp_cleanuno
dacthar *
{

	srp> this oask\nPro;
");
rFORM (thrp->(e	s_fi "
	d__3NULL)_nse[25] ==x10) SNS1_;
	E_PRODECTED	erp->stat_fi "
	d_3990_erp_cleanufsk\_pro;t	if (er}rnis oandli(unknown)
 *    - doed:
				*
 *f (erp->func_fi "
	d__3NULL)_{
->stat_fi "
	d_3}

	DCTurn erp;

}		_fi "
	re/* end daEND3990_erp_actionind"
	t_2/*
 *****************************************************************************
 * 24 byt32sense ERP functions (only)
 *****************************************************************************
 */

/*
 * DASD_3990_ERP_ACTIO_5
 *
 10_32 DESCRIPTION
 *   Handles 24 byt32sense 'A5 (see10'ecial thk\nPror SysA= dasdl de  Use a Jth tonce wvalip->once wdanan't work		edrp;

he sa
 *   ThiRAMETER
 *   erp		current erp
 * RE_hetdsense		current sense data
 *
 * REN VALUES
 *   erp		pointeed pointe RE_hetdsenic struct dasd_ccw_req *
dasd_3990_erp_action_5(stru10_32t dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->startdev;
	char irunctis = 256;
		erp->unction = dasd_3990_erp_action_5(stru10_32F_DEV_EVENT(DBF_INFO, G, device,
			  
			   "Per) {
rloggquest immedan erp-> erp;

}				/* end dasd_3990_erp_action_5 */

10_32*
 * DASD_3990_ERP_ACTION_5
 *
 1B_32 DESCRIPTION
 *   Handles 24 byt32sense 'A5 (see1B'ecial thk\nPror SysA= dasdl de  Use a Ath/
	DCion on a ncsuldemple			finishP
			caem (of	an uned\n");
Use a y Adiwe r* PA a Pse y build  buitove'd:
				* RE'nissem 
				g path mlink			If allven tatd  ri
, butei pcth mple			em 
	at devists. actio RE pri= dasd		caem (i pcoataindate DE/LOof thespa	  If RAMETER
 *   erp		cd:
				_harny build a\noved:
				* RE.sense		current sense data
 *
 * REsens VALUES
 *   void
 urrennew* RE   erp	 0t:
				fter in =am (of	imps. im ( - retror
 *   erpic struct dasd_ccw_req *
dasd_3990_erp_action_5(stru1B_32t dasd_ccw_req * erp, cd:
				_har *sense)
{

	struct dasd_device *device = erp->sd:
				_hartdev;

	/* f	__u32scpa->sp->f dasd_deviceq *dctl_cdas->f dasd_deviceq *dctl_c}

	DC dasd_dDEh"ck.htruc*
DEhtrucF_C dasd_dPFXh"ck.htruc*
PFXhtrucF_Csense)LOhtrucFnd daLOh"ck.htruc_	dev_e dasd_dq *1_cdcw, *olddcwF_DEV_EVENT(DBF_INFO, G, device,
			  
			    "NTERV*/
	DC "
	finishP
			caem (of	uned\n");
 y Adiwe r	bre
o9:
				_hartdon = dasd_3990_erp_action_5(stru1B_32F_DE dad);
omirknterral cqr
 *dasdev_edasd_a9:
				_harF_DEghsk\n>uildc;

	da
0) NULL)_{
	edasd_auildc;

	da	DCTurnp->f2csw_is_tm(&uildcirb.2csw	erp->sV_EVENT(DBF_WARNING, device,
			  
			    "dasd_ERV32seie data
,ri= dasd1Bniss "
	9:
irk
"  "dasd_ERV"in rransto.. eCCW_- jth tonce 	break dctl_cq:
				_harF_CTurnst tnot mps. im ( - retrjth tdoed:
				*ter  (erp->f25] ==x10) {	01erp->sV_EVENT(DBF_WARNING, device,
			  
			    "dasd_"Imps. im ( - retrbleiet_- jth tonce 	breeak dctl_cq:
				_harF_CTurnst d);
omirknterra\n");
  actualunt 			bt thev;

any (this Imps. im ( - retrble "
	iet_->ra\n"nhe reIRB-SCSWdev_edpa->s9:
				_hartd;

	daesw.e.2csw.cmd.dpa ((erp->upa->>sperp->sV_EVENT(DBF_WARNING, device,
			  
			    "dasd_"U easi the );
omirkna\n");
  actualunt    "INTERV			bt thev;

an	breeak dctl_cq90_erp_actionp(structd:
				_har *QR_FAILED &&
	 breCTurnst Buildemew*/
sst immedianclu retrDE/LOop->retrd_3990_eallocctiont immed((sense)) &tartdmagic     erp->r2 + 1,/*rDE/LOo+ TIC		break;rp->rsizeof( dasd_dDEh"ck.htruc) +reak;rp->rsizeof( dasd_dLOh"ck.htruc)ce,
			 ) ((erp->IS ERRt	if )fwitcst upt (nor
 *    01iceUneasi thealloc "
n
 *f (errr(&device->cdev->dev,
			  n"An or Set ct send"in ror  nnot/opm=% offling re				ed02x\=%srp->i"01	break dctl_cq90_erp_actionp(structd:
				_har *QR_FAILED &&
	 breCTurnst em (ol cqr
 *DEf (erDEhtruc->startdtrucF_Colddcwd_auildcupa\n";(erp->olddcwdcumceqCCW_=_CQR_FIECKFAICW_PFX)fwitcPFXhtrucd_auildctrucF_C	memcpy(DEhtruc, &PFXhtruc
			
irk_extret   "NTER->rsizeof( dasd_dDEh"ck.htruc)srp> {
		i_C	memcpy(DEhtruc, uildctruc,rsizeof( dasd_dDEh"ck.htruc)srprnst  buito LOop->rLOhtruc->startdtruco+ sizeof( dasd_dDEh"ck.htruc) ((erp->f25] ==3x1E)) {
1)_nse[LOhtruc=x10) {	01e)fwitcst resuldemple (err dctl_cq90_erp_actionp(structd:
				_har *QR_FAILED &&
	 breCTurnrp->f25] ==x0F);
	3F)1E)) {
1)_witcst ion on a ncsdctualWRITE *
 *_->rdnnedar
anorieck	c
	/* andhLOhtruc=0x1E		/81rp-> {
		ifnse[f25] ==x0F);
	3F)1E)) {
3)_witcst ion on a ncsdctual 1 - DeWRITE ->riissinorieck	c
	/* andhLOhtruc=0x1E		/C3rp-> {
		ifwitcLOhtruc=0x1E	25] ==x0;cst ion on a n andTurnLOhtruc=x10E	25] ==80;cst auxilidresp->rLOhtruc= 10E	25] ==9];>rLOhtruc=310E	25] ==30;cst r and
p->rLOhtruc=410E	25] ==290;cst 
{ek_a\n".cyl
p->rLOhtruc=510E	25] ==300;cst 
{ek_a\n".cyl
2ndsense p->rLOhtruc=710E	25] ==310;cst 
{ek_a\n".hetdi2ndsense p->
	memcpy(&[LOhtruc=8])ce&f25] ==x1])ce8srprnst  buito DEfdcwdev_edcwd_atartdupa\n";(ememiet(dcw, 0,rsizeof( dasd_dq *1)srp>dcwdcumceqCCW_=CQR_FIECKFAICW_DEFINE_EXTF_Wrp>dcwdc) {
	_=CICW_FLAG_CCrp>dcwdcu and
= 16rp>dcwdcudc->s(__u32)t) Dr_t) DEhtrucF_rnst  buito LOodcwdev_edcw++;(ememiet(dcw, 0,rsizeof( dasd_dq *1)srp>dcwdcumceqCCW_=CQR_FIECKFAICW_LOCATE_RECORDrp>dcwdc) {
	_=CICW_FLAG_CCrp>dcwdcu and
= 16rp>dcwdcudc->s(__u32)t) Dr_t) LOhtrucF_rnst TIC	 curren) ERP
	dcwdev_edcw++;(edcwdcumceqCCW_=CICW_TL;
TICrp>dcwdcudc->sdpa ((est tiry eatdt lr);
 fieldsop->retrdc) {
	_=C9:
				_hartdo {
	p->unction = dasd_3990_erp_action_5(stru1B_32F_	hartd;

	da->s9:
				_hartd;

	daF_	hartd = = device offliF_	hartdmemdevice offliF_	hartdmagic->s9:
				_hartdmagicF_	hartded\irea->s9:
				_hartded\irea;irunctis = 256;
		erp->unctibuildclk;
	d_cltoceqhe C()F_	hartd = DASD_CQR_FILLED;
		}
	}
ret stmactteratr:
				*ter  (er990_efreectiont immed(d:
				_har *,
			 ) ((e erp;

}				/* end dasd_3990_erp_action_5 */

1B_32*
 * DASD_3990_ERP_ACUPDATE_1B DESCRIPTION
 *   Handles 24 byteratupdito  curren32sense 'A5 (see1B'ecial thk\nPror SyUse a A= dasdl de  in =am (rren)ime si= dasdeeiv "
	sucg unful* PA a Pse y build  buitove't'.vious_ RE'nissrrent ERP
 ly  "
	sucg unful PA a 
 * If RAMETER
 *   erp		ct'.vious_ RE	y build  buitovet'.vious* RE.sense		current sense data
 *
 * REN VALUES
 *   erp		pointeed pointe REerpic struct dasd_ccw_req *
dasd_3990_erp_acupdito_1Bt dasd_ccw_req * erp, ct'.vious_ RE *sense)
{

	struct dasd_device *device = erp->st'.vious_ REtdev;

	/* f	__u32scpa->sp->f dasd_deviceq *dctl_cdas->f dasd_deviceq *dctl_c}

	DCsense)LOhtrucFnd da dasd_dLOh"ck.htrucdev_e dasd_dq *1_cdcwF_DEV_EVENT(DBF_INFO, G, device,
			  
			    "NTERV*/
	DC "
	finishP
			caem (of	uned\n");
 y Adiwe r	  "p->r"e- tnllow o		bre
ost d);
omirknterral cqr
 *dasdev_edasd_at'.vious_ REF_DEghsk\n>uildc;

	da
0) NULL)_{
	edasd_auildc;

	da	DCTurnp->f2csw_is_tm(&uildcirb.2csw	erp->sV_EVENT(DBF_WARNING, device,
			  
			    "dasd_ERV32seie data
,ri= dasd1B,tupdito,"  "dasd_ERV"in rransto.. eCCW_- jth tonce 	break dctl_ct'.vious_ REF_CTurnst tnot mps. im ( - retrjth tdoed:
				*ter  (erp->f25] ==x10) {	01erp->sV_EVENT(DBF_WARNING, device,
			  
			    "dasd_"Imps. im ( - retrbleiet_- jth tonce 	breeakt'.vious_ REtdev;DASD_CQR_FILLED;
		}
	}
rk dctl_ct'.vious_ REF_CTurnst d);
omirknterra\n");
  actualunt 			bt thev;

any (this Imps. im ( - retrble "
	iet_->ra\n"nhe reIRB-SCSWdev_edpa->st'.vious_ REtdw.e.2csw.cmd.dpa ((erp->upa->>sperp->sst upt (nor
 *    02 -  "p->U easi the );
omirkna\n");
  actualunt 			bt thev;

any (thrr(&device->cdev->dev,
			  n"An or Set ct send"in ror  nnot/opm=% offling re				ed02x\=%srp->i"02	breeakt'.vious_ REtdev;DASD_CQR_FILLED;&&
	 	}
rk dctl_ct'.vious_ REF_CTurnetrd_3t'.vious_ REF_DEst epdito  ho LOohe samho mew* dctl_ovesrta of theop->rLOhtruc->startdtruco+ sizeof( dasd_dDEh"ck.htruc) ((erp->f25] ==3x1E)) {
1)_nse[LOhtruc=x10) {	01e)fwitcst resuldemplehs.
 n__aterp'.vious_ REtdev;DASD_CQR_FILLED;&&
	 	}
rk dctl_ct'.vious_ REF_CTurnrp->f25] ==x0F);
	3F)1E)) {
1)_witcst ion on a ncsdctualWRITE *
 *_->rdnnedar
anorieck	c
	/* andhLOhtruc=0x1E		/81rp-> {
		ifnse[f25] ==x0F);
	3F)1E)) {
3)_witcst ion on a ncsdctual 1 - DeWRITE ->riissinorieck	c
	/* andhLOhtruc=0x1E		/C3rp-> {
		ifwitcLOhtruc=0x1E	25] ==x0;cst ion on a n andTurnLOhtruc=x10E	25] ==80;cst auxilidresp->rLOhtruc= 10E	25] ==9];>rLOhtruc=310E	25] ==30;cst r and
p->rLOhtruc=410E	25] ==290;cst 
{ek_a\n".cyl
p->rLOhtruc=510E	25] ==300;cst 
{ek_a\n".cyl
2ndsense p->rLOhtruc=710E	25] ==310;cst 
{ek_a\n".hetdi2ndsense p->
	memcpy(&[LOhtruc=8])ce&f25] ==x1])ce8srprnst TIC	 curren) ERP
	dcwdev_edcwd_atartdupa\n";cst a\n"e cqDEfdcwdev_edcw++;end daa\n"e cqLEfdcwdev_edcw++;end daa\n"e cqTIC	dcwdev_edcwdcudc->sdpa ((e REtdev;DASD_CQR_FILLED;
		}
	}
r erp;

}				/* end dasd_3990_erp_acupdito_1B*
 * DASD_3990_ERP_CLEANUPOMP1UND_RETRY DESCRIPTION
 *   Handles 24 byteratte\n1undn
 *fi= dasdcounter d  If allNOTE: At leah t xxxcountee in xxx.vp
fnsezerorblei"
		ointThi"asdby(fla*
{ta of th. Psisem kier nerp,  the cqr)		t immedThi"asd enque* PARAMETER
 *   sense		currendata of the actual error
 *   erp		pointer to the current ERP
 ly  buitove
 * RETURN VALUES
 *   erp		pointeed pointe
 *er to th RETURic structvoid3990_erp_cleanupomn1und_countt dasd_ccw_req * erp, char *sense)
{

	struct (msg_no25] ==2510) {	03)_witx00:	/* No  stateemplecounte andhunctis = 256;
	1se 0
		case
0x07_warndeeret stunte2set ise andhunctis = 256;
	2se 0
		case
0x07_warn2eeret stunte10set ise andhunctis = 256;
	10se 0
		case
0x07_warn3eeret stunte256set ise andhunctis = 256;
	2erp->f

	case 0t:
				d->fBUG()F_CTurnetrtion = dasd_3990_erp_actionpomn1und_count		/* end dasd_3990_erp_actionpomn1und_count*
 * DASD_3990_ERP_CLEANUPOMP1UND_PATH DESCRIPTION
 *   Handles 24 byteratte\n1undn
 *fi= dasdfrelt ce wittalt (noteUse a y request a * PARAMETER
 *   sense		currendata of the actual error
 *   erp		pointer to the current ERP
 ly  buitove
 * RETURN VALUES
 *   erp		pointeed pointe
 *er to th RETURic structvoid3990_erp_cleanupomn1und_t a t dasd_ccw_req * erp, char *sense)
{

	struerp->1] & 0 510) 990_ESENSE_BIT_3)_witc990_erp_action_lt (note_t a teif (ernrp->functiev;DASD__CQR_FILLED;&&
	 Q  !tdasd_bit(DASD_CQR_VELLEDVERIFY_PATHce&hartdo {
		erp->skDAdthe path mlpm*e given  to submitbt easi thS	iif  ce wfurandlii= dass.		breakhartdlpm*>startdev;
	cha->t a htruc.opm;reakhartdev;DASD_CQR_FILLEDNEEDLEAN
		}eakTurnetrtion = dasd_3990_erp_actionpomn1und_t a 		/* end dasd_3990_erp_actionpomn1und_t a t
 * DASD_3990_ERP_CLEANUPOMP1UND_CODE DESCRIPTION
 *   Handles 24 byteratte\n1undn
 *fi= dasdfrelt ce wr d  If RAMETER
 *   sense		currendata of the actual error
 *   erp		pointer to the current ERP
 ly  buitove
 * RETURN VALUES
 *   erp		pointeNEWe
 *er to th RETURic struct dasd_ccw_req *
dasd_3990_erp_cleanupomn1und_r d t dasd_ccw_req * erp, char *sense)
{

	structrp->1] & 0 510) 990_ESENSE_BIT_2	DBF_DEd(msg_no25] ==28])ase 0x00:	/*1(&devi/e a messagDiagnof Spdl (DCTL)d with ahe saanS	iif  I Writet*/
	DCsubd with ath a e(DCTLlersed poinr		breakhard_3990_erp_actionDCTLthar *0x20break;
		casee 0x00:	/*25&devi/e or 'devic5esry Adsetry once wagain		breakhartds = 256;
	1seeak;ckd.hrp_block_queue(erp,  thar *5	}

	rak;
		casee 0t:
				dev_wst resuldemplehs.
 n_-a e(Dinuin reak;
		case 0eakTurnetrtion = dasd_3990_erp_actionpomn1und_r d  ((e erp;

}				/* end dasd_3990_erp_actionpomn1und_r d t
 * DASD_3990_ERP_CLEANUPOMP1UND_CONFIG DESCRIPTION
 *   Handles 24 byteratte\n1undn
 *fi= dasdfrel e(figrs whererp		cd:precont.
 *   This mNo;
:aduplexsng is doniss "
	u\n")heck			(yet) If RAMETER
 *   sense		currendata of the actual error
 *   erp		pointer to the current ERP
 ly  buitove
 * RETURN VALUES
 *   erp		pointeed pointe
 *er to th RETURic structvoid3990_erp_cleanupomn1und_ e(figt dasd_ccw_req * erp, char *sense)
{

	structrp->>1] & 0 510) 990_ESENSE_BIT_1)_nse[25] == 610) 990_ESENSE_BIT_2	)/* issue e pato	susprecoveduplex  to o  hon thev;


"FORMupt (nor
 *    05iceSet
,
			  to	susprecoveduplex  to o
"FORMresuldebd n xxxwitch dasd_device *device = erp->startdev;
	char err(&device->cdev->dev,
			    "i"An or Set ct send"in ror  nnot offling re				    ""ed02x\=%srp->i"05"
	rea urnetrtion = dasd_3990_erp_actionpomn1und_r (fig		/* end dasd_3990_erp_actionpomn1und_r (figt
 * DASD_3990_ERP_CLEANUPOMP1UND DESCRIPTION
 *   Setup  analrren)urandlite\n1undnpror Sysi= dasdifUse a y \n1undnrece wheiv "
	sucg unful* PARAMETER
 *   sense		currendata of the actual error
 *   erp		pointer to the current ERP
 *d) ERP
 *
 * RETURN VALUES
 *   erp		pointet) Dis);
al)*/
ser to th RETURic struct dasd_ccw_req *
dasd_3990_erp_cleanupomn1undt dasd_ccw_req * erp, char *sense)
{

	structrp->>etrtion = dasd__3990_erp_actionpomn1und_count)Q  !test_>unctiev;DASD__CQR_FILLEDNEEDLEAN	)/* iss990_erp_cleanupomn1und_t a tear *
{

	srp> tctrp->>etrtion = dasd__3990_erp_actionpomn1und_t a )Q  !test_>unctiev;DASD__CQR_FILLEDNEEDLEAN	)/* isshard_3990_erp_actionpomn1und_r d tear *
{

	srp> tctrp->>etrtion = dasd__3990_erp_actionpomn1und_r d )Q  !test_>unctiev;DASD__CQR_FILLEDNEEDLEAN	)/* iss990_erp_cleanupomn1und_ e(figtear *
{

	srp> tct/e afateey \n1undni= dasd/
sei"
		oint,qr)		t immedn) ERP
	 (erp->functiev;DASD__CQR_FILLEDNEEDLEAN	
tceatus = DASD_CQR_FILLED;&&
	 	}
r erp;

}				/* end dasd_3990_erp_actionpomn1undt
 * DASD_990_ERP_CLEANUHANDLE_SIM RETURPTION
 *   Setupind"
	talrrenSIM SENSErdnnedantssv;
	sath nppropr, "
ni= das PARAMETER
 *   sense		currese		currof the actual error
 *   erpTURN VALUES
 *   erp		pn xxTURicvoid3990_erp_cleanu_env_dasim( dasd_device *device = erp *sense)
{

	strue/ ct'to ge and waicy rd don culotror
e to opero ion onor eCCW_ (thrp->(25] == 410) 990_ESIM_MSG_TO_OP) ||>f25] ==x10) {	10e)fwitcst t'to gSIM SRCnhe reRefCCCW_ (thrr(&device->cdev->dev,
			  n"SIM - SRC:    "INTERV%02x%02x%02x%02xrp->i25] == 2]   "dasd_25] == 3]>i25] ==11]>i25] ==12]srp> {
		ifrp->1] & 0 410) 990_ESIM_LOGerp->sst t'to gSIM SRCnRefcCCW_ (thrr(&ddevice->cdev->dev,
				  "lotrSIM - SRC:    "INTERV%02x%02x%02x%02xrp->i25] == 2]   "dasd_25] == 3]>i25] ==11]>i25] ==12]srp> aticDASD_3990_ERP_DCTL
 IASPECT_32 DESCRIPTION
 *   Handle analysdet ERP
	ind"
	t a diinterr32sense ERP fu*
 * RE printssdes up a relr);
 or Sets. action, we r* PARAMETER
 *   sense		currendata of the actual error
 *   erp		pointer to the current ERP
 ly  buitoved:
				*
 * RETURN VALUES
 *   erp		poin_fi "
	ter to the curren
 * RETURic void
d dasd_ccw_req *
dasd_3990_erp_cleanuiad"
	t_32t dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->startdev;
	char irunction = dasd_3990_erp_actionind"
	t_32F_DE da2 err*frelSIM data of the (thrp->(25] ==610) 990_ESIM_SENSE)D__CQR_FISIM_SENSE)iss990_erp_cleanu_env_dasim( = erp *
{

	srp->rp->1] & 0 510) 990_ESENSE_BIT_0)/* issue te\n1undnpror Sysi= dasdc de  (ense25seie 0D__C'1')	break990_erp_cleanupomn1und_counttear *
{

	srp-> {

		/* issue s thk\npror Sysi= dasdc de  (ense25seie 0D__C'0')	break (msg_no25] ==251)/* issx00:	/* No Messucg uniceuseed:
				*
 *dfrelt ceiise andhsV_EVENT(DBF_WARNINDEBUice,
			  
			    "dFORMATtionmtry 
	at dsucg unful	t immed    "Sector - jth tonce 	break;
		casee 0x00:	/*ndee/t oator
 *     reak;c(&device->cdev->dev,
			    "iFORMATtion) ERP
	at devr  nno			bre
o retr _3990_erp_actionp(structhar *QR_FAILED &&
	 break;
		casee 0x00:	/*n2:sst upt (vp
  a drd: reas  reakx07_warn3eest upt (vp
  a drd: reas durtil dror
copy		breakhard_3990_erp_actionto dctl
	if (erk;
		casee 0x00:	/*nF:eservlength mismasg_ durtil epdito h/
	DCd with   "dasd_ERMupt (nor
 *    08iceepdito h/
	DCd with 
 *    reak;c(&device->cdev->dev,
			  n"An or Set ct send"in ror     "S" nnot offling re				ed02x\=%srp->i"08	bre
o retr _3990_erp_actionp(structhar *QR_FAILED &&
	 break;
		casee 0x00:	/*10:eservloggquest imreas at doandliy requestror Sys	breakhard_3990_erp_action_5(stru10_32tear *
{

	srp>k;
		casee 0x00:	/*15eest nextctointeoutside	9:
irk
 extrec  "iFORMupt (nor
 *    07iceSD cnextctointeiss "
  "iFORMhe sin ror 9:
irk
 s Path Rextret  reak;c(&device->cdev->dev,
			    "iF"An or Set ct send"in ror  nnot offling re				    """ed02x\=%srp->i"07	bre
o retr _3990_erp_actionp(structhar *QR_FAILED &&
	 break;
		casee 0x00:	/*1Bd;ck uned\n");
 y Adiwe r durtil h/
	DC
 * sskhard_3990_erp_action_5(stru1B_32tear *
{

	srp>k;
		casee 0x00:	/*1Ceest upDefectf the (therr(&demergce->cdev->dev,
			    "UUnableinnedr just ovedurtil rece whe saPCIl   "IINTERVfesg_neCCW_a"
						bre
o  statetd wisi alloca_env_d nableions wher"in 90@de__aterrpanic  "dasd_("IpDefectf the Psagorse thin) {
rs.
ln\n");
 "  "dasd_E"about*th mawisi ay ict retrytrese" (e>k;
		casee 0x00:	/*1Do Messto o- pendi p - retr andhsV_EVENT(DBF_WARNING, device,
			  
			    "dINTERVA Sto o  pendi p - retry Adiwe r exih sn   "IINTERVft devr subsCheckst d,
			 	bre
o retr _3990_erp_action_4;

	} thar *
{

	srp>k;
		casee 0x00:	/*1Eo Mes redr andhsV_EVENT(DBF_WARNING, device,
			  
			    "dINTERVBredry Adiwe r exih sn   "IINTERVft devr subsCheckst d,
			 	bre                        etr _3990_erp_action_4;

	} thar *
{

	srp>k;
		casee 0t:
				d;ck try oandls or Seniced:
				*ter n reak;
		case 0eakTurn erp;

}				/* end dasd_3990_erp_actioniad"
	t_32*
 *****************************************************************************
 * 24 bytmain	tionm (DCTL)ons (only)
24rints32sense ERP f**************************************************************************
 */

/*
 * DASD_3990_ERP_ACTIO_CONTROL_PHECK DESCRIPTION
 *   Handle analysgenerd
dind"
	t a drp-anm (DCTL)c*/
	 rct send"intssdes up
f allven relr);
 or Sets. actionsing dure PARAMETER
 *   sense	ointer to the current ERP
 ly  buitoved:
				*
 * RETURN VALUES
 *   erp		poin_fi "
	ter to the curreneREerpicc struct dasd_ccw_req *
dasd_3990_erp_cleanupo(DCTL_2 errt dasd_ccw_req * erp, e	struct dasd_device *device = erp->startdev;
	char irp->f2csw_c str(&efers;

	daesw.e.2csw	0) (SCHN_STATCIN F_CTRL_PHK  "IIINTE| SCHN_STATCCHN_CTRL_PHK	erp->sV_EVENT(DBF_WARNING, device,
			  
			    "dasd_V  request dupt (facefm (DCTL)c*/
		breakter _3990_erp_action_4;

	} thar *NULL);er}rn erp;

}				ticDASD_3990_ERP_DCTL
 IASPECT DESCRIPTION
 *   Handle analysdet ERP
	ind"
	t a dat dsata of theby  trytil oiandl
f allven 24-ense t devr 32-ense ind"
	t a drout no. PARAMETER
 *   sense	ointer to the current ERP
 ly  buitoved:
				*
 * REN VALUES
 *   erp		poin_new 0x (Datawheivawisi ay ed pointTURic void
d dasd_ccw_req *
dasd_3990_erp_cleanuiad"
	tt dasd_ccw_req * erp, e	strucct dasd_deviceq *dctl_ce	s_mew*_3NULLtaerrors)
{

	 ((e/e afanablesin alm rct send" path n "aset ce wittb05:
p->retr_mew*_3990_erp_actionind"
	t_n "as(	if (erp->func_mew)
>f erp;
	}

_mew ((e/e sata of theeve loc "
d"in ror ;

	da
rry rde actua
if  y build e paupemew*/
ss!
if  c*/
	 p->x (t sense dataeissav EReasi
if readata
 _3990_ed_cldata
(&efers;

	daesw.ebrearp->bERP f**	retr_mew*_3990_erp_actionpo(DCTL_2 errt	if (erst dih tiluish betwied 24rints32sense ERP ftf the (th
		ifrp->1] & 0 710) 990_ESENSE_BIT_0)/* issue ind"
	tnterre sense ERP fu*
 *e andhunc_mew*_3990_erp_actionind"
	t_2 thar *
{

	srp-> {
		ifwiissue ind"
	tnterr32sense ERP ftf the (thhunc_mew*_3990_erp_actionind"
	t_32tear *
{

	srpe }	 dasd_39ih tiluish betwied 24rints32sense ERP ftf the (t
f erp;
	}

_mew (ticDASD_3990_ERP_DCTL
 ADDLEAN DESCRIPTION
 *   HandlePsiseon = dasdaddsath n Dis);
al	t immednlhe CQ(EAN	e currenhetdiof
f allven givp
fdasd(or
 *p) This mFrelaCd with 
eCCW_dasdven tatde ii retryiznd"isath d:
				* RETURNd_(t ce wTIC) This mFrelrransto.. eCCW_weem kilaCd pye actualal cqr
 *TCW (r to s			If allven al cqr
 *TCCB,wTIDALs, esg.) butegivp(i pa freshHandlePSBpsoerhn al cqr
 *ERP ftf thewiry mple			 pendid. PARAMETER
 *   sense	das		hetdiofurrent ERP
 *EAN-  ri
d(or
s thk\ndasdifUse "d)ime sor Se)TURN VALUES
 *   erp		pointer to the ERnew*/
s-  ri
dhetdsenic struct dasd_ccw_req *
dasd_990_erp_action_ddctiot dasd_ccw_req * erp, uil)truct dasd_device *device = erp->startdev = dev;_e dasd_dq *1_cdcwF_f dasd_deviceq *dctl_c}

	DCto gcplength,tf thsizeF_f dasd_dtcwdetcwF_f dasd_dtsbdetsb ((erp->uildccpmCCW_=_C1)_witccplength->sp->fnst TCW needubmitbt 6 sense ryignnt,qsoe(stv ( -oughdroom	break99thsize->s6 s+ sizeof( dasd_dtcw)s+ sizeof( dasd_dtsbsrp> {
		ifwitccplength->s2se 099thsize->s0rp> tct/e alloc "
nn Dis);
al	t immednlhe CQp->retrd_3990_eallocctiont immed((sense)) &tartdmagic     erp->rcplength,tf thsize *,
			 ) (erp->IS ERRt	if )fwi                rp->uildct ceiise<>sperp->skV_EVENT(DBF_INFO, ERR, ,
			  
			    "dINTERVUneasi thealloc "
n
 *ft immed srp>k;tartdev DASD_CQR_FILLED;&&
	 	}>k;tartdevopclk;
	d_cltoceqhe C()F_	> {
		ifwitckV_EVENT(DBF_INFO, ERR, ,
			  e                                     VUneasi thealloc "
n
 *ft immedn   "IINTER "(%ilt ceiiseleft)" e                                     uildct ceiissrp>k;990_equeue(sdevet ir(->cdev->queue, (HZ << 3)bre                }
>f erp;
	}

	DCT
_edcwd_auildcupa\n";(erp->uildccpmCCW_=_C1)_witc/*em kilaCshallow d pye actualal cqr
 *tcwdbutee panew*tsbdeandhuncticpmCCW_=	1se 0eartdupa\n"_=	PTR_ALIGNfunctitruc,r64srp>ktcwd_atartdupa\n";(e	tsbd= ( dasd_dtsbde) &tcw[1];>r	*tcwd_a*(( dasd_dtcwde)uildcupa\n"srp>ktcw->tsbd= (long)tsb (> {
		ifrp->dcwdcumceqCCW_=_CQR_FIECKFAICW_PSF)_witc/*ePSFpcthmple			 peirk
 he reNOOP/TIC		breaeartdupa\n"_=	uildcupa\n";(e {
		ifwitcst upretryizn*t with pee sad:
				*TIC	 cut ERP
 *EAN/LLE  reakxcwd_atartdupa\n";(e	dcwdcumceqCCW_=CICW_TL;
NOOP;(e	dcwdc) {
	_=CICW_FLAG_CCrp>edcw++;(eedcwdcumceqCCW_=CICW_TL;
TICrp>edcwdcudc------= (long)(uildcupa\n"srp> urnetrtio {
	_=Cuildco {
	p->unction = dasd_3990_erp_action_ddctioF_	hartd;

	da- _=CuilF_	hartd = = device offliF_	hartdmemdevi ice offliF_	hartdlhe CQ- _=Cuiltdlhe CF_	hartdmagic-- _=CuiltdmagicF_	hartded\irea-_=Cuiltded\irea;irunctis = 256;;
		erp->unctibuildclk;
	d_cltoceqhe C()F_	hartd = DASD_CQR_FILLED;
		}
	}
r erp;

}				ticDASD_3990_ERP_DCTL
 ADDI*   ALLEAN DESCRIPTION
 *   HandleAh n Dis);
al	
 *fiss eed 
				_env_d naent ERP
 *
 *   This mAdte
 *e currenhetdiofurren
 *-  ri
dcoataintil ror /
sering untilerp		cd:;
omirkd b05:ny parre*
{ta of th. PARAMETER
 *   sense	das		hetdiofurrent ERP
 *EAN-  ri
d(or
s thk\ndasdifUse "d)ime sor Se)TURTURN VALUES
 *   erp		pointer to the ERnew*/
s-  ri
dhetdsenic struct dasd_ccw_req *
dasd_3990_erp_action_ Dis);
alctiot dasd_ccw_req * erp,  uil)truct dasd_deviceq *dctl_c}

 _3NULLta-> da_ D tatdvalippretryizn*ee sad:
				*TIC	p->retrd_3990_erp_action_ddctiotuil);i(erp->IS ERRt	if )
>f erp;
	}

	D
sue ind"
	tndata
,r );
omirkni"
		oic	
 *fifd wisi all (erp->func
0) tar)/* isshard_3990_erp_actioniad"
	tteif (er}rrn erp;

}				/* end dasd_3990_erp_action_ Dis);
alctio*
 * DASD_3990_ERP_ACTIO_ERROR_MATCH DESCRIPTION
 *   HandleC*/
	 p->ror 9: erp- = DASDofurrengivp
fdasdissrrensam  If allPsisemeanserh operat) ERP
	unt e given relevase data
 *
 * REN  mth tmasg_ If allIadon't 9ih tiluish betwied 24rints32sense ERP ft		caem (in =am (ofIf alle sense ERP fuense 25rints27rbleiet_isawell* PARAMETER
 *   sense	das1"d)ime star  whichewiry batte\naend"he samhosense	das2enday Ad*cqr.tatTURN VALUES
 *   erp		pmasg_		'boo(str'dat dmasg_ f1undUse "d erp;
s 1 p->masg_ f1und, oandlwim (0.senic structto g990_erp_cleanuer Se_masg_t dasd_ccw_req * erp, uil1     erp->r dasd_ccw_req * erp, uil2tructrrors)
{

	1,s)
{

	2 ((erp->uil1td = = devi0) tar2tdev = dev)
>f erp;
	0 ((e
{

	1 _3990_ed_cldata
(&uil1tdw.ebrea
{

	2 _3990_ed_cldata
(&uil2tdw.ebrernis onn*t with ppiredata
 *
 *,qr)		oandlimple->ateemasg_		edrp;

0l (erp->f!
{

	1 0) !
{

	2)
>f erp;
	0 ( stateeERP ftf thein boan =am se->ac*/
	 c strdat dIFCCl (erp->f!
{

	1 && !
{

	2)	witcrp->(2csw_c str(&uil1tdw.e.2csw	0) (SCHN_STATCIN F_CTRL_PHK |  "IIIerp->SCHN_STATCCHN_CTRL_PHK	er==
"dasd_(2csw_c str(&uil2tdw.e.2csw	0) (SCHN_STATCIN F_CTRL_PHK |  "IIIerp->SCHN_STATCCHN_CTRL_PHK	e)
>ff erp;
	1; /*em tchewi saufcc andTuE da2 err*ERP ftf th;uense 0-2,25,27r (erp->f!(
{

	1 && 
{

	2   !test_d_(memcmp(
{

	1,s
{

	2, 3)->>sper  !test_d_(
{

	10 710=E	25] =20 71er  !test_d_(
{

	10 510=E	25] =20 5])	)/* iss erp;
	0 e/e sata ofanan't m tche andTurn erp;
	1;tc/*em tche an/* end dasd_3990_erp_actiontr Se_masg_*
 * DASD_3990_ERP_ACTIO_INLEAN DESCRIPTION
 *   Handlec*/
	 p->ror t ERP
 *
 *   y build hs.
 nP
			at   If allqui
	 exit p->x ERP
 *dasdissmplean	tion>uildc;

	da=NULL) PARAMETER
 *   sense	das		) ERP
	dasd(oiandlral cqr
 *dasd   y build an{

	stURTURN VALUES
 *   erp		pointehartr to the curreny build 9:
irk
 e*   erp>ff e actionsing dure ORerp>ffNULLdrp-an'new' or Set ct send.senic struct dasd_ccw_req *
dasd_3990_erp_actioninctiot dasd_ccw_req * erp, uil)truct dasd_deviceq *dctl_ce	s_hetdi) tar,e/e stv ( atd  ri
nhetdi andce	s_masg_*_3NULLte/e stv ( atd  ri
nhetdi andto geasg_*_30;tc/*e'boo(str'dat dmasg_til or Setf1undt
 * 	rp->uildct 
	da->) NULL)_{ret stup;
	afatedian*ter  (erl erp;
	NULLta> tct/e c*/
	 ven tat/dasd  ri
nfrel  ERP
 *
 *    (er9ofwitceasg_*_3990_erp_cleanuer Se_masg_te	s_hetd,auildc;

	dabreakter_masg_*_3uilFe/e stv ( wisi allmasg_til orr n reakdasd_auildc;

	da	t/e c*/
	 nextctat/dasdi
nerp,  
 * 	} ghsk\n>>uildc;

	da
0) NULL)_nse[!masg_));i(erp->!masg_)fwitc erp;
	NULLt stateem tchewiref1undt
 *CTurn erp;

}		_m tch;ret stup;
	a\n");
  acmasg_til orr  an/* end daEND3990_erp_actioninctio*
 * DASD_3990_ERP_ACTIO_FURTHER_tion>e s&s32sense ERP f****SCRIPTION
 *   HandleNoxcountee ileft	at devr t ERP
 *EAN.eC*/
	 wh oppiremitbt d xxTUR  ohe samho 
 * If st_d_- doe)urandli9:
irk
 
 *fi= dasd  erp		pd_- or 'devicto thruptd  erp		pd_- exit he sap
omanP
 *
 *   PARAMETER
 *   sense	ointe
 *fwhichee ii stror );
 he sanoxcountelefttURTURN VALUES
 *   erp		pointeed point/n Dis);
al	
 *senic struct dasd_ccw_req *
dasd_3990_erp_action)urandlctiot dasd_ccw_req * erp, e	strucct dasd_device *device = erp->startdev;
	char errors)
{

	 _3990_ed_cldata
(&efersw.ebrernis 2 err*frele sense ERP fu
 *f (erp->f>etrtion = dasd__3990_erp_actionbus_out) ||!test_>unction = dasd__3990_erp_action_5(stru1) ||!test_>unction = dasd__3990_erp_action_5(stru4	)/* isshard_3990_erp_action_5(stru1teif (ern {
		ifrp->unction = dasd__3990_erp_action_5(stru1ldacerp->statd_3990_erp_action_4;

	}1ldacteif (er}{
		ifrp->unction = dasd__3990_erp_action_5(stru5)/* issue s = 256;een tmple		ed sucg unful	 reakst t'epeve orpdfrelt ce witt po
	dP
 *d request a 		breaeard_3990_erp_action_5(stru1teif (ern>rp->1] &  && ![25] == 10) 990_ESENSE_BIT_0))/* issi/e a messagDiagnof Spdl (DCTL)d with ahe saanS	iif  I Writet*/
	DCsubd with a
 * ssk (msg_no25] ==251)/* 	 0x00:	/*1(&devix00:	/*57:{sue te(DCTLlers	break;khard_3990_erp_actionDCTLthar *0x20break;k;
		case 0	}eak;0x00:	/*18&devix00:	/*58:{sue t request a 		break;khard_3990_erp_actionDCTLthar *0x40break;k;
		case 0	}eak;0x00:	/*19&devix00:	/*59:{sue s Path Rdietryors	break;khard_3990_erp_actionDCTLthar *0x80break;k;
		case 0	}eak;0t:
				dev_wsV_EVENT(DBF_WARNING, device,
			    "IIINTE "upDefectsubd with aed poinr	0x%xn   "IIINTERVft dDiagnof Spdl (DCTL)d Inval"   "IIINTE 25] ==251)se 0	}
	
 urneis 2 err*frel32sense ERP functi
 *CT{
		ifrp->1] & Q  !tdasd>>etrtion = dasd__3990_erp_actionpomn1und_count)Q||!ttest_>unction = dasd__3990_erp_actionpomn1und_t a )Q||!ttest_>unction = dasd__3990_erp_actionpomn1und_r d )Q||!ttest_>unction = dasd__3990_erp_actionpomn1und_r (fig)	)/* isshard_3990_erp_actionpomn1undthar *
{

	srp-> {
		ifwiakDAS	if  Noxcounteleftrintsnoxn Dis);
al	i"
		al	ng is doS	if  neg un