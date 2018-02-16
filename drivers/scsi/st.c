/*
   SCSI Tape Driver for Linux version 1.1 and newer. See the accompanying
   file Documentation/scsi/st.txt for more information.

   History:
   Rewritten from Dwayne Forsyth's SCSI tape driver by Kai Makisara.
   Contribution and ideas from several people including (in alphabetical
   order) Klaus Ehrenfried, Eugene Exarevsky, Eric Lee Green, Wolfgang Denk,
   Steve Hirsch, Andreas Koppenh"ofer, Michael Leodolter, Eyal Lebedinsky,
   Michael Schaefer, J"org Weule, and Eric Youngdale.

   Copyright 1992 - 2010 Kai Makisara
   email Kai.Makisara@kolumbus.fi

   Some small formal changes - aeb, 950809

   Last modified: 18-JAN-1998 Richard Gooch <rgooch@atnf.csiro.au> Devfs support
 */

static const char *verstr = "20101219";

#include <linux/module.h>

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/mtio.h>
#include <linux/cdrom.h>
#include <linux/ioctl.h>
#include <linux/fcntl.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/moduleparam.h>
#include <linux/cdev.h>
#include <linux/idr.h>
#include <linux/delay.h>
#include <linux/mutex.h>

#include <asm/uaccess.h>
#include <asm/dma.h>

#include <scsi/scsi.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/sg.h>


/* The driver prints some debugging information on the console if DEBUG
   is defined and non-zero. */
#define DEBUG 0

#define ST_DEB_MSG  KERN_NOTICE
#if DEBUG
/* The message level for the debug messages is currently set to KERN_NOTICE
   so that people can easily see the messages. Later when the debugging messages
   in the drivers are more widely classified, this may be changed to KERN_DEBUG. */
#define DEB(a) a
#define DEBC(a) if (debugging) { a ; }
#else
#define DEB(a)
#define DEBC(a)
#endif

#define ST_KILOBYTE 1024

#include "st_options.h"
#include "st.h"

static int buffer_kbs;
static int max_sg_segs;
static int try_direct_io = TRY_DIRECT_IO;
static int try_rdio = 1;
static int try_wdio = 1;

static struct class st_sysfs_class;
static const struct attribute_group *st_dev_groups[];

MODULE_AUTHOR("Kai Makisara");
MODULE_DESCRIPTION("SCSI tape (st) driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(SCSI_TAPE_MAJOR);
MODULE_ALIAS_SCSI_DEVICE(TYPE_TAPE);

/* Set 'perm' (4th argument) to 0 to disable module_param's definition
 * of sysfs parameters (which module_param doesn't yet support).
 * Sysfs parameters defined explicitly later.
 */
module_param_named(buffer_kbs, buffer_kbs, int, 0);
MODULE_PARM_DESC(buffer_kbs, "Default driver buffer size for fixed block mode (KB; 32)");
module_param_named(max_sg_segs, max_sg_segs, int, 0);
MODULE_PARM_DESC(max_sg_segs, "Maximum number of scatter/gather segments to use (256)");
module_param_named(try_direct_io, try_direct_io, int, 0);
MODULE_PARM_DESC(try_direct_io, "Try direct I/O between user buffer and tape drive (1)");

/* Extra parameters for testing */
module_param_named(try_rdio, try_rdio, int, 0);
MODULE_PARM_DESC(try_rdio, "Try direct read i/o when possible");
module_param_named(try_wdio, try_wdio, int, 0);
MODULE_PARM_DESC(try_wdio, "Try direct write i/o when possible");

#ifndef MODULE
static int write_threshold_kbs;  /* retained for compatibility */
static struct st_dev_parm {
	char *name;
	int *val;
} parms[] __initdata = {
	{
		"buffer_kbs", &buffer_kbs
	},
	{       /* Retained for compatibility with 2.4 */
		"write_threshold_kbs", &write_threshold_kbs
	},
	{
		"max_sg_segs", NULL
	},
	{
		"try_direct_io", &try_direct_io
	}
};
#endif

/* Restrict the number of modes so that names for all are assigned */
#if ST_NBR_MODES > 16
#error "Maximum number of modes is 16"
#endif
/* Bit reversed order to get same names for same minors with all
   mode counts */
static const char *st_formats[] = {
	"",  "r", "k", "s", "l", "t", "o", "u",
	"m", "v", "p", "x", "a", "y", "q", "z"}; 

/* The default definitions have been moved to st_options.h */

#define ST_FIXED_BUFFER_SIZE (ST_FIXED_BUFFER_BLOCKS * ST_KILOBYTE)

/* The buffer size should fit into the 24 bits for length in the
   6-byte SCSI read and write commands. */
#if ST_FIXED_BUFFER_SIZE >= (2 << 24 - 1)
#error "Buffer size should not exceed (2 << 24 - 1) bytes!"
#endif

static int debugging = DEBUG;

#define MAX_RETRIES 0
#define MAX_WRITE_RETRIES 0
#define MAX_READY_RETRIES 0
#define NO_TAPE  NOT_READY

#define ST_TIMEOUT (900 * HZ)
#define ST_LONG_TIMEOUT (14000 * HZ)

/* Remove mode bits and auto-rewind bit (7) */
#define TAPE_NR(x) ( ((iminor(x) & ~255) >> (ST_NBR_MODE_BITS + 1)) | \
    (iminor(x) & ~(-1 << ST_MODE_SHIFT)) )
#define TAPE_MODE(x) ((iminor(x) & ST_MODE_MASK) >> ST_MODE_SHIFT)

/* Construct the minor number from the device (d), mode (m), and non-rewind (n) data */
#define TAPE_MINOR(d, m, n) (((d & ~(255 >> (ST_NBR_MODE_BITS + 1))) << (ST_NBR_MODE_BITS + 1)) | \
  (d & (255 >> (ST_NBR_MODE_BITS + 1))) | (m << ST_MODE_SHIFT) | ((n != 0) << 7) )

/* Internal ioctl to set both density (uppermost 8 bits) and blocksize (lower
   24 bits) */
#define SET_DENS_AND_BLK 0x10001

static int st_fixed_buffer_size = ST_FIXED_BUFFER_SIZE;
static int st_max_sg_segs = ST_MAX_SG;

static int modes_defined;

static int enlarge_buffer(struct st_buffer *, int, int);
static void clear_buffer(struct st_buffer *);
static void normalize_buffer(struct st_buffer *);
static int append_to_buffer(const char __user *, struct st_buffer *, int);
static int from_buffer(struct st_buffer *, char __user *, int);
static void move_buffer_data(struct st_buffer *, int);

static int sgl_map_user_pages(struct st_buffer *, const unsigned int,
			      unsigned long, size_t, int);
static int sgl_unmap_user_pages(struct st_buffer *, const unsigned int, int);

static int st_probe(struct device *);
static int st_remove(struct device *);

static int do_create_sysfs_files(void);
static void do_remove_sysfs_files(void);

static struct scsi_driver st_template = {
	.owner			= THIS_MODULE,
	.gendrv = {
		.name		= "st",
		.probe		= st_probe,
		.remove		= st_remove,
	},
};

static int st_compression(struct scsi_tape *, int);

static int find_partition(struct scsi_tape *);
static int switch_partition(struct scsi_tape *);

static int st_int_ioctl(struct scsi_tape *, unsigned int, unsigned long);

static void scsi_tape_release(struct kref *);

#define to_scsi_tape(obj) container_of(obj, struct scsi_tape, kref)

static DEFINE_MUTEX(st_ref_mutex);
static DEFINE_SPINLOCK(st_index_lock);
static DEFINE_SPINLOCK(st_use_lock);
static DEFINE_IDR(st_index_idr);



#include "osst_detect.h"
#ifndef SIGS_FROM_OSST
#define SIGS_FROM_OSST \
	{"OnStream", "SC-", "", "osst"}, \
	{"OnStream", "DI-", "", "osst"}, \
	{"OnStream", "DP-", "", "osst"}, \
	{"OnStream", "USB", "", "osst"}, \
	{"OnStream", "FW-", "", "osst"}
#endif

static struct scsi_tape *scsi_tape_get(int dev)
{
	struct scsi_tape *STp = NULL;

	mutex_lock(&st_ref_mutex);
	spin_lock(&st_index_lock);

	STp = idr_find(&st_index_idr, dev);
	if (!STp) goto out;

	kref_get(&STp->kref);

	if (!STp->device)
		goto out_put;

	if (scsi_device_get(STp->device))
		goto out_put;

	goto out;

out_put:
	kref_put(&STp->kref, scsi_tape_release);
	STp = NULL;
out:
	spin_unlock(&st_index_lock);
	mutex_unlock(&st_ref_mutex);
	return STp;
}

static void scsi_tape_put(struct scsi_tape *STp)
{
	struct scsi_device *sdev = STp->device;

	mutex_lock(&st_ref_mutex);
	kref_put(&STp->kref, scsi_tape_release);
	scsi_device_put(sdev);
	mutex_unlock(&st_ref_mutex);
}

struct st_reject_data {
	char *vendor;
	char *model;
	char *rev;
	char *driver_hint; /* Name of the correct driver, NULL if unknown */
};

static struct st_reject_data reject_list[] = {
	/* {"XXX", "Yy-", "", NULL},  example */
	SIGS_FROM_OSST,
	{NULL, }};

/* If the device signature is on the list of incompatible drives, the
   function returns a pointer to the name of the correct driver (if known) */
static char * st_incompatible(struct scsi_device* SDp)
{
	struct st_reject_data *rp;

	for (rp=&(reject_list[0]); rp->vendor != NULL; rp++)
		if (!strncmp(rp->vendor, SDp->vendor, strlen(rp->vendor)) &&
		    !strncmp(rp->model, SDp->model, strlen(rp->model)) &&
		    !strncmp(rp->rev, SDp->rev, strlen(rp->rev))) {
			if (rp->driver_hint)
				return rp->driver_hint;
			else
				return "unknown";
		}
	return NULL;
}


static inline char *tape_name(struct scsi_tape *tape)
{
	return tape->disk->disk_name;
}

#define st_printk(prefix, t, fmt, a...) \
	sdev_printk(prefix, (t)->device, "%s: " fmt, \
		    tape_name(t), ##a)
#ifdef DEBUG
#define DEBC_printk(t, fmt, a...) \
	if (debugging) { st_printk(ST_DEB_MSG, t, fmt, ##a ); }
#else
#define DEBC_printk(t, fmt, a...)
#endif

static void st_analyze_sense(struct st_request *SRpnt, struct st_cmdstatus *s)
{
	const u8 *ucp;
	const u8 *sense = SRpnt->sense;

	s->have_sense = scsi_normalize_sense(SRpnt->sense,
				SCSI_SENSE_BUFFERSIZE, &s->sense_hdr);
	s->flags = 0;

	if (s->have_sense) {
		s->deferred = 0;
		s->remainder_valid =
			scsi_get_sense_info_fld(sense, SCSI_SENSE_BUFFERSIZE, &s->uremainder64);
		switch (sense[0] & 0x7f) {
		case 0x71:
			s->deferred = 1;
		case 0x70:
			s->fixed_format = 1;
			s->flags = sense[2] & 0xe0;
			break;
		case 0x73:
			s->deferred = 1;
		case 0x72:
			s->fixed_format = 0;
			ucp = scsi_sense_desc_find(sense, SCSI_SENSE_BUFFERSIZE, 4);
			s->flags = ucp ? (ucp[3] & 0xe0) : 0;
			break;
		}
	}
}


/* Convert the result to success code */
static int st_chk_result(struct scsi_tape *STp, struct st_request * SRpnt)
{
	int result = SRpnt->result;
	u8 scode;
	DEB(const char *stp;)
	char *name = tape_name(STp);
	struct st_cmdstatus *cmdstatp;

	if (!result)
		return 0;

	cmdstatp = &STp->buffer->cmdstat;
	st_analyze_sense(SRpnt, cmdstatp);

	if (cmdstatp->have_sense)
		scode = STp->buffer->cmdstat.sense_hdr.sense_key;
	else
		scode = 0;

	DEB(
	if (debugging) {
		st_printk(ST_DEB_MSG, STp,
			    "Error: %x, cmd: %x %x %x %x %x %x\n", result,
			    SRpnt->cmd[0], SRpnt->cmd[1], SRpnt->cmd[2],
			    SRpnt->cmd[3], SRpnt->cmd[4], SRpnt->cmd[5]);
		if (cmdstatp->have_sense)
			 __scsi_print_sense(name, SRpnt->sense, SCSI_SENSE_BUFFERSIZE);
	} ) /* end DEB */
	if (!debugging) { /* Abnormal conditions for tape */
		if (!cmdstatp->have_sense)
			st_printk(KERN_WARNING, STp,
			       "Error %x (driver bt 0x%x, host bt 0x%x).\n",
			       result, driver_byte(result), host_byte(result));
		else if (cmdstatp->have_sense &&
			 scode != NO_SENSE &&
			 scode != RECOVERED_ERROR &&
			 /* scode != UNIT_ATTENTION && */
			 scode != BLANK_CHECK &&
			 scode != VOLUME_OVERFLOW &&
			 SRpnt->cmd[0] != MODE_SENSE &&
			 SRpnt->cmd[0] != TEST_UNIT_READY) {

			__scsi_print_sense(name, SRpnt->sense, SCSI_SENSE_BUFFERSIZE);
		}
	}

	if (cmdstatp->fixed_format &&
	    STp->cln_mode >= EXTENDED_SENSE_START) {  /* Only fixed format sense */
		if (STp->cln_sense_value)
			STp->cleaning_req |= ((SRpnt->sense[STp->cln_mode] &
					       STp->cln_sense_mask) == STp->cln_sense_value);
		else
			STp->cleaning_req |= ((SRpnt->sense[STp->cln_mode] &
					       STp->cln_sense_mask) != 0);
	}
	if (cmdstatp->have_sense &&
	    cmdstatp->sense_hdr.asc == 0 && cmdstatp->sense_hdr.ascq == 0x17)
		STp->cleaning_req = 1; /* ASC and ASCQ => cleaning requested */

	STp->pos_unknown |= STp->device->was_reset;

	if (cmdstatp->have_sense &&
	    scode == RECOVERED_ERROR
#if ST_RECOVERED_WRITE_FATAL
	    && SRpnt->cmd[0] != WRITE_6
	    && SRpnt->cmd[0] != WRITE_FILEMARKS
#endif
	    ) {
		STp->recover_count++;
		STp->recover_reg++;

		DEB(
		if (debugging) {
			if (SRpnt->cmd[0] == READ_6)
				stp = "read";
			else if (SRpnt->cmd[0] == WRITE_6)
				stp = "write";
			else
				stp = "ioctl";
			st_printk(ST_DEB_MSG, STp,
				  "Recovered %s error (%d).\n",
				  stp, STp->recover_count);
		} ) /* end DEB */

		if (cmdstatp->flags == 0)
			return 0;
	}
	return (-EIO);
}

static struct st_request *st_allocate_request(struct scsi_tape *stp)
{
	struct st_request *streq;

	streq = kzalloc(sizeof(*streq), GFP_KERNEL);
	if (streq)
		streq->stp = stp;
	else {
		st_printk(KERN_ERR, stp,
			  "Can't get SCSI request.\n");
		if (signal_pending(current))
			stp->buffer->syscall_result = -EINTR;
		else
			stp->buffer->syscall_result = -EBUSY;
	}

	return streq;
}

static void st_release_request(struct st_request *streq)
{
	kfree(streq);
}

static void st_scsi_execute_end(struct request *req, int uptodate)
{
	struct st_request *SRpnt = req->end_io_data;
	struct scsi_tape *STp = SRpnt->stp;
	struct bio *tmp;

	STp->buffer->cmdstat.midlevel_result = SRpnt->result = req->errors;
	STp->buffer->cmdstat.residual = req->resid_len;

	tmp = SRpnt->bio;
	if (SRpnt->waiting)
		complete(SRpnt->waiting);

	blk_rq_unmap_user(tmp);
	__blk_put_request(req->q, req);
}

static int st_scsi_execute(struct st_request *SRpnt, const unsigned char *cmd,
			   int data_direction, void *buffer, unsigned bufflen,
			   int timeout, int retries)
{
	struct request *req;
	struct rq_map_data *mdata = &SRpnt->stp->buffer->map_data;
	int err = 0;
	int write = (data_direction == DMA_TO_DEVICE);

	req = blk_get_request(SRpnt->stp->device->request_queue, write,
			      GFP_KERNEL);
	if (IS_ERR(req))
		return DRIVER_ERROR << 24;

	blk_rq_set_block_pc(req);
	req->cmd_flags |= REQ_QUIET;

	mdata->null_mapped = 1;

	if (bufflen) {
		err = blk_rq_map_user(req->q, req, mdata, NULL, bufflen,
				      GFP_KERNEL);
		if (err) {
			blk_put_request(req);
			return DRIVER_ERROR << 24;
		}
	}

	SRpnt->bio = req->bio;
	req->cmd_len = COMMAND_SIZE(cmd[0]);
	memset(req->cmd, 0, BLK_MAX_CDB);
	memcpy(req->cmd, cmd, req->cmd_len);
	req->sense = SRpnt->sense;
	req->sense_len = 0;
	req->timeout = timeout;
	req->retries = retries;
	req->end_io_data = SRpnt;

	blk_execute_rq_nowait(req->q, NULL, req, 1, st_scsi_execute_end);
	return 0;
}

/* Do the scsi command. Waits until command performed if do_wait is true.
   Otherwise write_behind_check() is used to check that the command
   has finished. */
static struct st_request *
st_do_scsi(struct st_request * SRpnt, struct scsi_tape * STp, unsigned char *cmd,
	   int bytes, int direction, int timeout, int retries, int do_wait)
{
	struct completion *waiting;
	struct rq_map_data *mdata = &STp->buffer->map_data;
	int ret;

	/* if async, make sure there's no command outstanding */
	if (!do_wait && ((STp->buffer)->last_SRpnt)) {
		st_printk(KERN_ERR, STp,
			  "Async command already active.\n");
		if (signal_pending(current))
			(STp->buffer)->syscall_result = (-EINTR);
		else
			(STp->buffer)->syscall_result = (-EBUSY);
		return NULL;
	}

	if (!SRpnt) {
		SRpnt = st_allocate_request(STp);
		if (!SRpnt)
			return NULL;
	}

	/* If async IO, set last_SRpnt. This ptr tells write_behind_check
	   which IO is outstanding. It's nulled out when the IO completes. */
	if (!do_wait)
		(STp->buffer)->last_SRpnt = SRpnt;

	waiting = &STp->wait;
	init_completion(waiting);
	SRpnt->waiting = waiting;

	if (STp->buffer->do_dio) {
		mdata->page_order = 0;
		mdata->nr_entries = STp->buffer->sg_segs;
		mdata->pages = STp->buffer->mapped_pages;
	} else {
		mdata->page_order = STp->buffer->reserved_page_order;
		mdata->nr_entries =
			DIV_ROUND_UP(bytes, PAGE_SIZE << mdata->page_order);
		mdata->pages = STp->buffer->reserved_pages;
		mdata->offset = 0;
	}

	memcpy(SRpnt->cmd, cmd, sizeof(SRpnt->cmd));
	STp->buffer->cmdstat.have_sense = 0;
	STp->buffer->syscall_result = 0;

	ret = st_scsi_execute(SRpnt, cmd, direction, NULL, bytes, timeout,
			      retries);
	if (ret) {
		/* could not allocate the buffer or request was too large */
		(STp->buffer)->syscall_result = (-EBUSY);
		(STp->buffer)->last_SRpnt = NULL;
	} else if (do_wait) {
		wait_for_completion(waiting);
		SRpnt->waiting = NULL;
		(STp->buffer)->syscall_result = st_chk_result(STp, SRpnt);
	}

	return SRpnt;
}


/* Handle the write-behind checking (waits for completion). Returns -ENOSPC if
   write has been correct but EOM early warning reached, -EIO if write ended in
   error or zero if write successful. Asynchronous writes are used only in
   variable block mode. */
static int write_behind_check(struct scsi_tape * STp)
{
	int retval = 0;
	struct st_buffer *STbuffer;
	struct st_partstat *STps;
	struct st_cmdstatus *cmdstatp;
	struct st_request *SRpnt;

	STbuffer = STp->buffer;
	if (!STbuffer->writing)
		return 0;

	DEB(
	if (STp->write_pending)
		STp->nbr_waits++;
	else
		STp->nbr_finished++;
	) /* end DEB */

	wait_for_completion(&(STp->wait));
	SRpnt = STbuffer->last_SRpnt;
	STbuffer->last_SRpnt = NULL;
	SRpnt->waiting = NULL;

	(STp->buffer)->syscall_result = st_chk_result(STp, SRpnt);
	st_release_request(SRpnt);

	STbuffer->buffer_bytes -= STbuffer->writing;
	STps = &(STp->ps[STp->partition]);
	if (STps->drv_block >= 0) {
		if (STp->block_size == 0)
			STps->drv_block++;
		else
			STps->drv_block += STbuffer->writing / STp->block_size;
	}

	cmdstatp = &STbuffer->cmdstat;
	if (STbuffer->syscall_result) {
		retval = -EIO;
		if (cmdstatp->have_sense && !cmdstatp->deferred &&
		    (cmdstatp->flags & SENSE_EOM) &&
		    (cmdstatp->sense_hdr.sense_key == NO_SENSE ||
		     cmdstatp->sense_hdr.sense_key == RECOVERED_ERROR)) {
			/* EOM at write-behind, has all data been written? */
			if (!cmdstatp->remainder_valid ||
			    cmdstatp->uremainder64 == 0)
				retval = -ENOSPC;
		}
		if (retval == -EIO)
			STps->drv_block = -1;
	}
	STbuffer->writing = 0;

	DEB(if (debugging && retval)
		    st_printk(ST_DEB_MSG, STp,
				"Async write error %x, return value %d.\n",
				STbuffer->cmdstat.midlevel_result, retval);) /* end DEB */

	return retval;
}


/* Step over EOF if it has been inadvertently crossed (ioctl not used because
   it messes up the block number). */
static int cross_eof(struct scsi_tape * STp, int forward)
{
	struct st_request *SRpnt;
	unsigned char cmd[MAX_COMMAND_SIZE];

	cmd[0] = SPACE;
	cmd[1] = 0x01;		/* Space FileMarks */
	if (forward) {
		cmd[2] = cmd[3] = 0;
		cmd[4] = 1;
	} else
		cmd[2] = cmd[3] = cmd[4] = 0xff;	/* -1 filemarks */
	cmd[5] = 0;

	DEBC_printk(STp, "Stepping over filemark %s.\n",
		    forward ? "forward" : "backward");

	SRpnt = st_do_scsi(NULL, STp, cmd, 0, DMA_NONE,
			   STp->device->request_queue->rq_timeout,
			   MAX_RETRIES, 1);
	if (!SRpnt)
		return (STp->buffer)->syscall_result;

	st_release_request(SRpnt);
	SRpnt = NULL;

	if ((STp->buffer)->cmdstat.midlevel_result != 0)
		st_printk(KERN_ERR, STp,
			  "Stepping over filemark %s failed.\n",
			  forward ? "forward" : "backward");

	return (STp->buffer)->syscall_result;
}


/* Flush the write buffer (never need to write if variable blocksize). */
static int st_flush_write_buffer(struct scsi_tape * STp)
{
	int transfer, blks;
	int result;
	unsigned char cmd[MAX_COMMAND_SIZE];
	struct st_request *SRpnt;
	struct st_partstat *STps;

	result = write_behind_check(STp);
	if (result)
		return result;

	result = 0;
	if (STp->dirty == 1) {

		transfer = STp->buffer->buffer_bytes;
		DEBC_printk(STp, "Flushing %d bytes.\n", transfer);

		memset(cmd, 0, MAX_COMMAND_SIZE);
		cmd[0] = WRITE_6;
		cmd[1] = 1;
		blks = transfer / STp->block_size;
		cmd[2] = blks >> 16;
		cmd[3] = blks >> 8;
		cmd[4] = blks;

		SRpnt = st_do_scsi(NULL, STp, cmd, transfer, DMA_TO_DEVICE,
				   STp->device->request_queue->rq_timeout,
				   MAX_WRITE_RETRIES, 1);
		if (!SRpnt)
			return (STp->buffer)->syscall_result;

		STps = &(STp->ps[STp->partition]);
		if ((STp->buffer)->syscall_result != 0) {
			struct st_cmdstatus *cmdstatp = &STp->buffer->cmdstat;

			if (cmdstatp->have_sense && !cmdstatp->deferred &&
			    (cmdstatp->flags & SENSE_EOM) &&
			    (cmdstatp->sense_hdr.sense_key == NO_SENSE ||
			     cmdstatp->sense_hdr.sense_key == RECOVERED_ERROR) &&
			    (!cmdstatp->remainder_valid ||
			     cmdstatp->uremainder64 == 0)) { /* All written at EOM early warning */
				STp->dirty = 0;
				(STp->buffer)->buffer_bytes = 0;
				if (STps->drv_block >= 0)
					STps->drv_block += blks;
				result = (-ENOSPC);
			} else {
				st_printk(KERN_ERR, STp, "Error on flush.\n");
				STps->drv_block = (-1);
				result = (-EIO);
			}
		} else {
			if (STps->drv_block >= 0)
				STps->drv_block += blks;
			STp->dirty = 0;
			(STp->buffer)->buffer_bytes = 0;
		}
		st_release_request(SRpnt);
		SRpnt = NULL;
	}
	return result;
}


/* Flush the tape buffer. The tape will be positioned correctly unless
   seek_next is true. */
static int flush_buffer(struct scsi_tape *STp, int seek_next)
{
	int backspace, result;
	struct st_buffer *STbuffer;
	struct st_partstat *STps;

	STbuffer = STp->buffer;

	/*
	 * If there was a bus reset, block further access
	 * to this device.
	 */
	if (STp->pos_unknown)
		return (-EIO);

	if (STp->ready != ST_READY)
		return 0;
	STps = &(STp->ps[STp->partition]);
	if (STps->rw == ST_WRITING)	/* Writing */
		return st_flush_write_buffer(STp);

	if (STp->block_size == 0)
		return 0;

	backspace = ((STp->buffer)->buffer_bytes +
		     (STp->buffer)->read_pointer) / STp->block_size -
	    ((STp->buffer)->read_pointer + STp->block_size - 1) /
	    STp->block_size;
	(STp->buffer)->buffer_bytes = 0;
	(STp->buffer)->read_pointer = 0;
	result = 0;
	if (!seek_next) {
		if (STps->eof == ST_FM_HIT) {
			result = cross_eof(STp, 0);	/* Back over the EOF hit */
			if (!result)
				STps->eof = ST_NOEOF;
			else {
				if (STps->drv_file >= 0)
					STps->drv_file++;
				STps->drv_block = 0;
			}
		}
		if (!result && backspace > 0)
			result = st_int_ioctl(STp, MTBSR, backspace);
	} else if (STps->eof == ST_FM_HIT) {
		if (STps->drv_file >= 0)
			STps->drv_file++;
		STps->drv_block = 0;
		STps->eof = ST_NOEOF;
	}
	return result;

}

/* Set the mode parameters */
static int set_mode_densblk(struct scsi_tape * STp, struct st_modedef * STm)
{
	int set_it = 0;
	unsigned long arg;

	if (!STp->density_changed &&
	    STm->default_density >= 0 &&
	    STm->default_density != STp->density) {
		arg = STm->default_density;
		set_it = 1;
	} else
		arg = STp->density;
	arg <<= MT_ST_DENSITY_SHIFT;
	if (!STp->blksize_changed &&
	    STm->default_blksize >= 0 &&
	    STm->default_blksize != STp->block_size) {
		arg |= STm->default_blksize;
		set_it = 1;
	} else
		arg |= STp->block_size;
	if (set_it &&
	    st_int_ioctl(STp, SET_DENS_AND_BLK, arg)) {
		st_printk(KERN_WARNING, STp,
			  "Can't set default block size to %d bytes "
			  "and density %x.\n",
			  STm->default_blksize, STm->default_density);
		if (modes_defined)
			return (-EINVAL);
	}
	return 0;
}


/* Lock or unlock the drive door. Don't use when st_request allocated. */
static int do_door_lock(struct scsi_tape * STp, int do_lock)
{
	int retval, cmd;

	cmd = do_lock ? SCSI_IOCTL_DOORLOCK : SCSI_IOCTL_DOORUNLOCK;
	DEBC_printk(STp, "%socking drive door.\n", do_lock ? "L" : "Unl");
	retval = scsi_ioctl(STp->device, cmd, NULL);
	if (!retval) {
		STp->door_locked = do_lock ? ST_LOCKED_EXPLICIT : ST_UNLOCKED;
	}
	else {
		STp->door_locked = ST_LOCK_FAILS;
	}
	return retval;
}


/* Set the internal state after reset */
static void reset_state(struct scsi_tape *STp)
{
	int i;
	struct st_partstat *STps;

	STp->pos_unknown = 0;
	for (i = 0; i < ST_NBR_PARTITIONS; i++) {
		STps = &(STp->ps[i]);
		STps->rw = ST_IDLE;
		STps->eof = ST_NOEOF;
		STps->at_sm = 0;
		STps->last_block_valid = 0;
		STps->drv_block = -1;
		STps->drv_file = -1;
	}
	if (STp->can_partitions) {
		STp->partition = find_partition(STp);
		if (STp->partition < 0)
			STp->partition = 0;
		STp->new_partition = STp->partition;
	}
}

/* Test if the drive is ready. Returns either one of the codes below or a negative system
   error code. */
#define CHKRES_READY       0
#define CHKRES_NEW_SESSION 1
#define CHKRES_NOT_READY   2
#define CHKRES_NO_TAPE     3

#define MAX_ATTENTIONS    10

static int test_ready(struct scsi_tape *STp, int do_wait)
{
	int attentions, waits, max_wait, scode;
	int retval = CHKRES_READY, new_session = 0;
	unsigned char cmd[MAX_COMMAND_SIZE];
	struct st_request *SRpnt = NULL;
	struct st_cmdstatus *cmdstatp = &STp->buffer->cmdstat;

	max_wait = do_wait ? ST_BLOCK_SECONDS : 0;

	for (attentions=waits=0; ; ) {
		memset((void *) &cmd[0], 0, MAX_COMMAND_SIZE);
		cmd[0] = TEST_UNIT_READY;
		SRpnt = st_do_scsi(SRpnt, STp, cmd, 0, DMA_NONE,
				   STp->long_timeout, MAX_READY_RETRIES, 1);

		if (!SRpnt) {
			retval = (STp->buffer)->syscall_result;
			break;
		}

		if (cmdstatp->have_sense) {

			scode = cmdstatp->sense_hdr.sense_key;

			if (scode == UNIT_ATTENTION) { /* New media? */
				new_session = 1;
				if (attentions < MAX_ATTENTIONS) {
					attentions++;
					continue;
				}
				else {
					retval = (-EIO);
					break;
				}
			}

			if (scode == NOT_READY) {
				if (waits < max_wait) {
					if (msleep_interruptible(1000)) {
						retval = (-EINTR);
						break;
					}
					waits++;
					continue;
				}
				else {
					if ((STp->device)->scsi_level >= SCSI_2 &&
					    cmdstatp->sense_hdr.asc == 0x3a)	/* Check ASC */
						retval = CHKRES_NO_TAPE;
					else
						retval = CHKRES_NOT_READY;
					break;
				}
			}
		}

		retval = (STp->buffer)->syscall_result;
		if (!retval)
			retval = new_session ? CHKRES_NEW_SESSION : CHKRES_READY;
		break;
	}

	if (SRpnt != NULL)
		st_release_request(SRpnt);
	return retval;
}


/* See if the drive is ready and gather information about the tape. Return values:
   < 0   negative error code from errno.h
   0     drive ready
   1     drive not ready (possibly no tape)
*/
static int check_tape(struct scsi_tape *STp, struct file *filp)
{
	int i, retval, new_session = 0, do_wait;
	unsigned char cmd[MAX_COMMAND_SIZE], saved_cleaning;
	unsigned short st_flags = filp->f_flags;
	struct st_request *SRpnt = NULL;
	struct st_modedef *STm;
	struct st_partstat *STps;
	struct inode *inode = file_inode(filp);
	int mode = TAPE_MODE(inode);

	STp->ready = ST_READY;

	if (mode != STp->current_mode) {
		DEBC_printk(STp, "Mode change from %d to %d.\n",
			    STp->current_mode, mode);
		new_session = 1;
		STp->current_mode = mode;
	}
	STm = &(STp->modes[STp->current_mode]);

	saved_cleaning = STp->cleaning_req;
	STp->cleaning_req = 0;

	do_wait = ((filp->f_flags & O_NONBLOCK) == 0);
	retval = test_ready(STp, do_wait);

	if (retval < 0)
	    goto err_out;

	if (retval == CHKRES_NEW_SESSION) {
		STp->pos_unknown = 0;
		STp->partition = STp->new_partition = 0;
		if (STp->can_partitions)
			STp->nbr_partitions = 1; /* This guess will be updated later
                                                    if necessary */
		for (i = 0; i < ST_NBR_PARTITIONS; i++) {
			STps = &(STp->ps[i]);
			STps->rw = ST_IDLE;
			STps->eof = ST_NOEOF;
			STps->at_sm = 0;
			STps->last_block_valid = 0;
			STps->drv_block = 0;
			STps->drv_file = 0;
		}
		new_session = 1;
	}
	else {
		STp->cleaning_req |= saved_cleaning;

		if (retval == CHKRES_NOT_READY || retval == CHKRES_NO_TAPE) {
			if (retval == CHKRES_NO_TAPE)
				STp->ready = ST_NO_TAPE;
			else
				STp->ready = ST_NOT_READY;

			STp->density = 0;	/* Clear the erroneous "residue" */
			STp->write_prot = 0;
			STp->block_size = 0;
			STp->ps[0].drv_file = STp->ps[0].drv_block = (-1);
			STp->partition = STp->new_partition = 0;
			STp->door_locked = ST_UNLOCKED;
			return CHKRES_NOT_READY;
		}
	}

	if (STp->omit_blklims)
		STp->min_block = STp->max_block = (-1);
	else {
		memset((void *) &cmd[0], 0, MAX_COMMAND_SIZE);
		cmd[0] = READ_BLOCK_LIMITS;

		SRpnt = st_do_scsi(SRpnt, STp, cmd, 6, DMA_FROM_DEVICE,
				   STp->device->request_queue->rq_timeout,
				   MAX_READY_RETRIES, 1);
		if (!SRpnt) {
			retval = (STp->buffer)->syscall_result;
			goto err_out;
		}

		if (!SRpnt->result && !STp->buffer->cmdstat.have_sense) {
			STp->max_block = ((STp->buffer)->b_data[1] << 16) |
			    ((STp->buffer)->b_data[2] << 8) | (STp->buffer)->b_data[3];
			STp->min_block = ((STp->buffer)->b_data[4] << 8) |
			    (STp->buffer)->b_data[5];
			if ( DEB( debugging || ) !STp->inited)
				st_printk(KERN_INFO, STp,
					  "Block limits %d - %d bytes.\n",
					  STp->min_block, STp->max_block);
		} else {
			STp->min_block = STp->max_block = (-1);
			DEBC_printk(STp, "Can't read block limits.\n");
		}
	}

	memset((void *) &cmd[0], 0, MAX_COMMAND_SIZE);
	cmd[0] = MODE_SENSE;
	cmd[4] = 12;

	SRpnt = st_do_scsi(SRpnt, STp, cmd, 12, DMA_FROM_DEVICE,
			   STp->device->request_queue->rq_timeout,
			   MAX_READY_RETRIES, 1);
	if (!SRpnt) {
		retval = (STp->buffer)->syscall_result;
		goto err_out;
	}

	if ((STp->buffer)->syscall_result != 0) {
		DEBC_printk(STp, "No Mode Sense.\n");
		STp->block_size = ST_DEFAULT_BLOCK;	/* Educated guess (?) */
		(STp->buffer)->syscall_result = 0;	/* Prevent error propagation */
		STp->drv_write_prot = 0;
	} else {
		DEBC_printk(STp,"Mode sense. Length %d, "
			    "medium %x, WBS %x, BLL %d\n",
			    (STp->buffer)->b_data[0],
			    (STp->buffer)->b_data[1],
			    (STp->buffer)->b_data[2],
			    (STp->buffer)->b_data[3]);

		if ((STp->buffer)->b_data[3] >= 8) {
			STp->drv_buffer = ((STp->buffer)->b_data[2] >> 4) & 7;
			STp->density = (STp->buffer)->b_data[4];
			STp->block_size = (STp->buffer)->b_data[9] * 65536 +
			    (STp->buffer)->b_data[10] * 256 + (STp->buffer)->b_data[11];
			DEBC_printk(STp, "Density %x, tape length: %x, "
				    "drv buffer: %d\n",
				    STp->density,
				    (STp->buffer)->b_data[5] * 65536 +
				    (STp->buffer)->b_data[6] * 256 +
				    (STp->buffer)->b_data[7],
				    STp->drv_buffer);
		}
		STp->drv_write_prot = ((STp->buffer)->b_data[2] & 0x80) != 0;
		if (!STp->drv_buffer && STp->immediate_filemark) {
			st_printk(KERN_WARNING, STp,
				  "non-buffered tape: disabling "
				  "writing immediate filemarks\n");
			STp->immediate_filemark = 0;
		}
	}
	st_release_request(SRpnt);
	SRpnt = NULL;
	STp->inited = 1;

	if (STp->block_size > 0)
		(STp->buffer)->buffer_blocks =
			(STp->buffer)->buffer_size / STp->block_size;
	else
		(STp->buffer)->buffer_blocks = 1;
	(STp->buffer)->buffer_bytes = (STp->buffer)->read_pointer = 0;

	DEBC_printk(STp, "Block size: %d, buffer size: %d (%d blocks).\n",
		    STp->block_size, (STp->buffer)->buffer_size,
		    (STp->buffer)->buffer_blocks);

	if (STp->drv_write_prot) {
		STp->write_prot = 1;

		DEBC_printk(STp, "Write protected\n");

		if (do_wait &&
		    ((st_flags & O_ACCMODE) == O_WRONLY ||
		     (st_flags & O_ACCMODE) == O_RDWR)) {
			retval = (-EROFS);
			goto err_out;
		}
	}

	if (STp->can_partitions && STp->nbr_partitions < 1) {
		/* This code is reached when the device is opened for the first time
		   after the driver has been initialized with tape in the drive and the
		   partition support has been enabled. */
		DEBC_printk(STp, "Updating partition number in status.\n");
		if ((STp->partition = find_partition(STp)) < 0) {
			retval = STp->partition;
			goto err_out;
		}
		STp->new_partition = STp->partition;
		STp->nbr_partitions = 1; /* This guess will be updated when necessary */
	}

	if (new_session) {	/* Change the drive parameters for the new mode */
		STp->density_changed = STp->blksize_changed = 0;
		STp->compression_changed = 0;
		if (!(STm->defaults_for_writes) &&
		    (retval = set_mode_densblk(STp, STm)) < 0)
		    goto err_out;

		if (STp->default_drvbuffer != 0xff) {
			if (st_int_ioctl(STp, MTSETDRVBUFFER, STp->default_drvbuffer))
				st_printk(KERN_WARNING, STp,
					  "Can't set default drive "
					  "buffering to %d.\n",
					  STp->default_drvbuffer);
		}
	}

	return CHKRES_READY;

 err_out:
	return retval;
}


/* Open the device. Needs to take the BKL only because of incrementing the SCSI host
   module count. */
static int st_open(struct inode *inode, struct file *filp)
{
	int i, retval = (-EIO);
	int resumed = 0;
	struct scsi_tape *STp;
	struct st_partstat *STps;
	int dev = TAPE_NR(inode);

	/*
	 * We really want to do nonseekable_open(inode, filp); here, but some
	 * versions of tar incorrectly call lseek on tapes and bail out if that
	 * fails.  So we disallow pread() and pwrite(), but permit lseeks.
	 */
	filp->f_mode &= ~(FMODE_PREAD | FMODE_PWRITE);

	if (!(STp = scsi_tape_get(dev))) {
		return -ENXIO;
	}

	filp->private_data = STp;

	spin_lock(&st_use_lock);
	if (STp->in_use) {
		spin_unlock(&st_use_lock);
		scsi_tape_put(STp);
		DEBC_printk(STp, "Device already in use.\n");
		return (-EBUSY);
	}

	STp->in_use = 1;
	spin_unlock(&st_use_lock);
	STp->rew_at_close = STp->autorew_dev = (iminor(inode) & 0x80) == 0;

	if (scsi_autopm_get_device(STp->device) < 0) {
		retval = -EIO;
		goto err_out;
	}
	resumed = 1;
	if (!scsi_block_when_processing_errors(STp->device)) {
		retval = (-ENXIO);
		goto err_out;
	}

	/* See that we have at least a one page buffer available */
	if (!enlarge_buffer(STp->buffer, PAGE_SIZE, STp->restr_dma)) {
		st_printk(KERN_WARNING, STp,
			  "Can't allocate one page tape buffer.\n");
		retval = (-EOVERFLOW);
		goto err_out;
	}

	(STp->buffer)->cleared = 0;
	(STp->buffer)->writing = 0;
	(STp->buffer)->syscall_result = 0;

	STp->write_prot = ((filp->f_flags & O_ACCMODE) == O_RDONLY);

	STp->dirty = 0;
	for (i = 0; i < ST_NBR_PARTITIONS; i++) {
		STps = &(STp->ps[i]);
		STps->rw = ST_IDLE;
	}
	STp->try_dio_now = STp->try_dio;
	STp->recover_count = 0;
	DEB( STp->nbr_waits = STp->nbr_finished = 0;
	     STp->nbr_requests = STp->nbr_dio = STp->nbr_pages = 0; )

	retval = check_tape(STp, filp);
	if (retval < 0)
		goto err_out;
	if ((filp->f_flags & O_NONBLOCK) == 0 &&
	    retval != CHKRES_READY) {
		if (STp->ready == NO_TAPE)
			retval = (-ENOMEDIUM);
		else
			retval = (-EIO);
		goto err_out;
	}
	return 0;

 err_out:
	normalize_buffer(STp->buffer);
	spin_lock(&st_use_lock);
	STp->in_use = 0;
	spin_unlock(&st_use_lock);
	if (resumed)
		scsi_autopm_put_device(STp->device);
	scsi_tape_put(STp);
	return retval;

}


/* Flush the tape buffer before close */
static int st_flush(struct file *filp, fl_owner_t id)
{
	int result = 0, result2;
	unsigned char cmd[MAX_COMMAND_SIZE];
	struct st_request *SRpnt;
	struct scsi_tape *STp = filp->private_data;
	struct st_modedef *STm = &(STp->modes[STp->current_mode]);
	struct st_partstat *STps = &(STp->ps[STp->partition]);

	if (file_count(filp) > 1)
		return 0;

	if (STps->rw == ST_WRITING && !STp->pos_unknown) {
		result = st_flush_write_buffer(STp);
		if (result != 0 && result != (-ENOSPC))
			goto out;
	}

	if (STp->can_partitions &&
	    (result2 = switch_partition(STp)) < 0) {
		DEBC_printk(STp, "switch_partition at close failed.\n");
		if (result == 0)
			result = result2;
		goto out;
	}

	DEBC( if (STp->nbr_requests)
		st_printk(KERN_DEBUG, STp,
			  "Number of r/w requests %d, dio used in %d, "
			  "pages %d.\n", STp->nbr_requests, STp->nbr_dio,
			  STp->nbr_pages));

	if (STps->rw == ST_WRITING && !STp->pos_unknown) {
		struct st_cmdstatus *cmdstatp = &STp->buffer->cmdstat;

#if DEBUG
		DEBC_printk(STp, "Async write waits %d, finished %d.\n",
			    STp->nbr_waits, STp->nbr_finished);
#endif
		memset(cmd, 0, MAX_COMMAND_SIZE);
		cmd[0] = WRITE_FILEMARKS;
		if (STp->immediate_filemark)
			cmd[1] = 1;
		cmd[4] = 1 + STp->two_fm;

		SRpnt = st_do_scsi(NULL, STp, cmd, 0, DMA_NONE,
				   STp->device->request_queue->rq_timeout,
				   MAX_WRITE_RETRIES, 1);
		if (!SRpnt) {
			result = (STp->buffer)->syscall_result;
			goto out;
		}

		if (STp->buffer->syscall_result == 0 ||
		    (cmdstatp->have_sense && !cmdstatp->deferred &&
		     (cmdstatp->flags & SENSE_EOM) &&
		     (cmdstatp->sense_hdr.sense_key == NO_SENSE ||
		      cmdstatp->sense_hdr.sense_key == RECOVERED_ERROR) &&
		     (!cmdstatp->remainder_valid || cmdstatp->uremainder64 == 0))) {
			/* Write successful at EOM */
			st_release_request(SRpnt);
			SRpnt = NULL;
			if (STps->drv_file >= 0)
				STps->drv_file++;
			STps->drv_block = 0;
			if (STp->two_fm)
				cross_eof(STp, 0);
			STps->eof = ST_FM;
		}
		else { /* Write error */
			st_release_request(SRpnt);
			SRpnt = NULL;
			st_printk(KERN_ERR, STp,
				  "Error on write filemark.\n");
			if (result == 0)
				result = (-EIO);
		}

		DEBC_printk(STp, "Buffer flushed, %d EOF(s) written\n", cmd[4]);
	} else if (!STp->rew_at_close) {
		STps = &(STp->ps[STp->partition]);
		if (!STm->sysv || STps->rw != ST_READING) {
			if (STp->can_bsr)
				result = flush_buffer(STp, 0);
			else if (STps->eof == ST_FM_HIT) {
				result = cross_eof(STp, 0);
				if (result) {
					if (STps->drv_file >= 0)
						STps->drv_file++;
					STps->drv_block = 0;
					STps->eof = ST_FM;
				} else
					STps->eof = ST_NOEOF;
			}
		} else if ((STps->eof == ST_NOEOF &&
			    !(result = cross_eof(STp, 1))) ||
			   STps->eof == ST_FM_HIT) {
			if (STps->drv_file >= 0)
				STps->drv_file++;
			STps->drv_block = 0;
			STps->eof = ST_FM;
		}
	}

      out:
	if (STp->rew_at_close) {
		result2 = st_int_ioctl(STp, MTREW, 1);
		if (result == 0)
			result = result2;
	}
	return result;
}


/* Close the device and release it. BKL is not needed: this is the only thread
   accessing this tape. */
static int st_release(struct inode *inode, struct file *filp)
{
	int result = 0;
	struct scsi_tape *STp = filp->private_data;

	if (STp->door_locked == ST_LOCKED_AUTO)
		do_door_lock(STp, 0);

	normalize_buffer(STp->buffer);
	spin_lock(&st_use_lock);
	STp->in_use = 0;
	spin_unlock(&st_use_lock);
	scsi_autopm_put_device(STp->device);
	scsi_tape_put(STp);

	return result;
}

/* The checks common to both reading and writing */
static ssize_t rw_checks(struct scsi_tape *STp, struct file *filp, size_t count)
{
	ssize_t retval = 0;

	/*
	 * If we are in the middle of error recovery, don't let anyone
	 * else try and use this device.  Also, if error recovery fails, it
	 * may try and take the device offline, in which case all further
	 * access to the device is prohibited.
	 */
	if (!scsi_block_when_processing_errors(STp->device)) {
		retval = (-ENXIO);
		goto out;
	}

	if (STp->ready != ST_READY) {
		if (STp->ready == ST_NO_TAPE)
			retval = (-ENOMEDIUM);
		else
			retval = (-EIO);
		goto out;
	}

	if (! STp->modes[STp->current_mode].defined) {
		retval = (-ENXIO);
		goto out;
	}


	/*
	 * If there was a bus reset, block further access
	 * to this device.
	 */
	if (STp->pos_unknown) {
		retval = (-EIO);
		goto out;
	}

	if (count == 0)
		goto out;

	DEB(
	if (!STp->in_use) {
		st_printk(ST_DEB_MSG, STp,
			  "Incorrect device.\n");
		retval = (-EIO);
		goto out;
	} ) /* end DEB */

	if (STp->can_partitions &&
	    (retval = switch_partition(STp)) < 0)
		goto out;

	if (STp->block_size == 0 && STp->max_block > 0 &&
	    (count < STp->min_block || count > STp->max_block)) {
		retval = (-EINVAL);
		goto out;
	}

	if (STp->do_auto_lock && STp->door_locked == ST_UNLOCKED &&
	    !do_door_lock(STp, 1))
		STp->door_locked = ST_LOCKED_AUTO;

 out:
	return retval;
}


static int setup_buffering(struct scsi_tape *STp, const char __user *buf,
			   size_t count, int is_read)
{
	int i, bufsize, retval = 0;
	struct st_buffer *STbp = STp->buffer;

	if (is_read)
		i = STp->try_dio_now && try_rdio;
	else
		i = STp->try_dio_now && try_wdio;

	if (i && ((unsigned long)buf & queue_dma_alignment(
					STp->device->request_queue)) == 0) {
		i = sgl_map_user_pages(STbp, STbp->use_sg, (unsigned long)buf,
				       count, (is_read ? READ : WRITE));
		if (i > 0) {
			STbp->do_dio = i;
			STbp->buffer_bytes = 0;   /* can be used as transfer counter */
		}
		else
			STbp->do_dio = 0;  /* fall back to buffering with any error */
		STbp->sg_segs = STbp->do_dio;
		DEB(
		     if (STbp->do_dio) {
			STp->nbr_dio++;
			STp->nbr_pages += STbp->do_dio;
		     }
		)
	} else
		STbp->do_dio = 0;
	DEB( STp->nbr_requests++; )

	if (!STbp->do_dio) {
		if (STp->block_size)
			bufsize = STp->block_size > st_fixed_buffer_size ?
				STp->block_size : st_fixed_buffer_size;
		else {
			bufsize = count;
			/* Make sure that data from previous user is not leaked even if
			   HBA does not return correct residual */
			if (is_read && STp->sili && !STbp->cleared)
				clear_buffer(STbp);
		}

		if (bufsize > STbp->buffer_size &&
		    !enlarge_buffer(STbp, bufsize, STp->restr_dma)) {
			st_printk(KERN_WARNING, STp,
				  "Can't allocate %d byte tape buffer.\n",
				  bufsize);
			retval = (-EOVERFLOW);
			goto out;
		}
		if (STp->block_size)
			STbp->buffer_blocks = bufsize / STp->block_size;
	}

 out:
	return retval;
}


/* Can be called more than once after each setup_buffer() */
static void release_buffering(struct scsi_tape *STp, int is_read)
{
	struct st_buffer *STbp;

	STbp = STp->buffer;
	if (STbp->do_dio) {
		sgl_unmap_user_pages(STbp, STbp->do_dio, is_read);
		STbp->do_dio = 0;
		STbp->sg_segs = 0;
	}
}


/* Write command */
static ssize_t
st_write(struct file *filp, const char __user *buf, size_t count, loff_t * ppos)
{
	ssize_t total;
	ssize_t i, do_count, blks, transfer;
	ssize_t retval;
	int undone, retry_eot = 0, scode;
	int async_write;
	unsigned char cmd[MAX_COMMAND_SIZE];
	const char __user *b_point;
	struct st_request *SRpnt = NULL;
	struct scsi_tape *STp = filp->private_data;
	struct st_modedef *STm;
	struct st_partstat *STps;
	struct st_buffer *STbp;

	if (mutex_lock_interruptible(&STp->lock))
		return -ERESTARTSYS;

	retval = rw_checks(STp, filp, count);
	if (retval || count == 0)
		goto out;

	/* Write must be integral number of blocks */
	if (STp->block_size != 0 && (count % STp->block_size) != 0) {
		st_printk(KERN_WARNING, STp,
			  "Write not multiple of tape block size.\n");
		retval = (-EINVAL);
		goto out;
	}

	STm = &(STp->modes[STp->current_mode]);
	STps = &(STp->ps[STp->partition]);

	if (STp->write_prot) {
		retval = (-EACCES);
		goto out;
	}


	if (STps->rw == ST_READING) {
		retval = flush_buffer(STp, 0);
		if (retval)
			goto out;
		STps->rw = ST_WRITING;
	} else if (STps->rw != ST_WRITING &&
		   STps->drv_file == 0 && STps->drv_block == 0) {
		if ((retval = set_mode_densblk(STp, STm)) < 0)
			goto out;
		if (STm->default_compression != ST_DONT_TOUCH &&
		    !(STp->compression_changed)) {
			if (st_compression(STp, (STm->default_compression == ST_YES))) {
				st_printk(KERN_WARNING, STp,
					  "Can't set default compression.\n");
				if (modes_defined) {
					retval = (-EINVAL);
					goto out;
				}
			}
		}
	}

	STbp = STp->buffer;
	i = write_behind_check(STp);
	if (i) {
		if (i == -ENOSPC)
			STps->eof = ST_EOM_OK;
		else
			STps->eof = ST_EOM_ERROR;
	}

	if (STps->eof == ST_EOM_OK) {
		STps->eof = ST_EOD_1;  /* allow next write */
		retval = (-ENOSPC);
		goto out;
	}
	else if (STps->eof == ST_EOM_ERROR) {
		retval = (-EIO);
		goto out;
	}

	/* Check the buffer readability in cases where copy_user might catch
	   the problems after some tape movement. */
	if (STp->block_size != 0 &&
	    !STbp->do_dio &&
	    (copy_from_user(&i, buf, 1) != 0 ||
	     copy_from_user(&i, buf + count - 1, 1) != 0)) {
		retval = (-EFAULT);
		goto out;
	}

	retval = setup_buffering(STp, buf, count, 0);
	if (retval)
		goto out;

	total = count;

	memset(cmd, 0, MAX_COMMAND_SIZE);
	cmd[0] = WRITE_6;
	cmd[1] = (STp->block_size != 0);

	STps->rw = ST_WRITING;

	b_point = buf;
	while (count > 0 && !retry_eot) {

		if (STbp->do_dio) {
			do_count = count;
		}
		else {
			if (STp->block_size == 0)
				do_count = count;
			else {
				do_count = STbp->buffer_blocks * STp->block_size -
					STbp->buffer_bytes;
				if (do_count > count)
					do_count = count;
			}

			i = append_to_buffer(b_point, STbp, do_count);
			if (i) {
				retval = i;
				goto out;
			}
		}
		count -= do_count;
		b_point += do_count;

		async_write = STp->block_size == 0 && !STbp->do_dio &&
			STm->do_async_writes && STps->eof < ST_EOM_OK;

		if (STp->block_size != 0 && STm->do_buffer_writes &&
		    !(STp->try_dio_now && try_wdio) && STps->eof < ST_EOM_OK &&
		    STbp->buffer_bytes < STbp->buffer_size) {
			STp->dirty = 1;
			/* Don't write a buffer that is not full enough. */
			if (!async_write && count == 0)
				break;
		}

	retry_write:
		if (STp->block_size == 0)
			blks = transfer = do_count;
		else {
			if (!STbp->do_dio)
				blks = STbp->buffer_bytes;
			else
				blks = do_count;
			blks /= STp->block_size;
			transfer = blks * STp->block_size;
		}
		cmd[2] = blks >> 16;
		cmd[3] = blks >> 8;
		cmd[4] = blks;

		SRpnt = st_do_scsi(SRpnt, STp, cmd, transfer, DMA_TO_DEVICE,
				   STp->device->request_queue->rq_timeout,
				   MAX_WRITE_RETRIES, !async_write);
		if (!SRpnt) {
			retval = STbp->syscall_result;
			goto out;
		}
		if (async_write && !STbp->syscall_result) {
			STbp->writing = transfer;
			STp->dirty = !(STbp->writing ==
				       STbp->buffer_bytes);
			SRpnt = NULL;  /* Prevent releasing this request! */
			DEB( STp->write_pending = 1; )
			break;
		}

		if (STbp->syscall_result != 0) {
			struct st_cmdstatus *cmdstatp = &STp->buffer->cmdstat;

			DEBC_printk(STp, "Error on write:\n");
			if (cmdstatp->have_sense && (cmdstatp->flags & SENSE_EOM)) {
				scode = cmdstatp->sense_hdr.sense_key;
				if (cmdstatp->remainder_valid)
					undone = (int)cmdstatp->uremainder64;
				else if (STp->block_size == 0 &&
					 scode == VOLUME_OVERFLOW)
					undone = transfer;
				else
					undone = 0;
				if (STp->block_size != 0)
					undone *= STp->block_size;
				if (undone <= do_count) {
					/* Only data from this write is not written */
					count += undone;
					b_point -= undone;
					do_count -= undone;
					if (STp->block_size)
						blks = (transfer - undone) / STp->block_size;
					STps->eof = ST_EOM_OK;
					/* Continue in fixed block mode if all written
					   in this request but still something left to write
					   (retval left to zero)
					*/
					if (STp->block_size == 0 ||
					    undone > 0 || count == 0)
						retval = (-ENOSPC); /* EOM within current request */
					DEBC_printk(STp, "EOM with %d "
						    "bytes unwritten.\n",
						    (int)count);
				} else {
					/* EOT within data buffered earlier (possible only
					   in fixed block mode without direct i/o) */
					if (!retry_eot && !cmdstatp->deferred &&
					    (scode == NO_SENSE || scode == RECOVERED_ERROR)) {
						move_buffer_data(STp->buffer, transfer - undone);
						retry_eot = 1;
						if (STps->drv_block >= 0) {
							STps->drv_block += (transfer - undone) /
								STp->block_size;
						}
						STps->eof = ST_EOM_OK;
						DEBC_printk(STp, "Retry "
							    "write of %d "
							    "bytes at EOM.\n",
							    STp->buffer->buffer_bytes);
						goto retry_write;
					}
					else {
						/* Either error within data buffered by driver or
						   failed retry */
						count -= do_count;
						blks = do_count = 0;
						STps->eof = ST_EOM_ERROR;
						STps->drv_block = (-1); /* Too cautious? */
						retval = (-EIO);	/* EOM for old data */
						DEBC_printk(STp, "EOM with "
							    "lost data.\n");
					}
				}
			} else {
				count += do_count;
				STps->drv_block = (-1);		/* Too cautious? */
				retval = STbp->syscall_result;
			}

		}

		if (STps->drv_block >= 0) {
			if (STp->block_size == 0)
				STps->drv_block += (do_count > 0);
			else
				STps->drv_block += blks;
		}

		STbp->buffer_bytes = 0;
		STp->dirty = 0;

		if (retval || retry_eot) {
			if (count < total)
				retval = total - count;
			goto out;
		}
	}

	if (STps->eof == ST_EOD_1)
		STps->eof = ST_EOM_OK;
	else if (STps->eof != ST_EOM_OK)
		STps->eof = ST_NOEOF;
	retval = total - count;

 out:
	if (SRpnt != NULL)
		st_release_request(SRpnt);
	release_buffering(STp, 0);
	mutex_unlock(&STp->lock);

	return retval;
}

/* Read data from the tape. Returns zero in the normal case, one if the
   eof status has changed, and the negative error code in case of a
   fatal error. Otherwise updates the buffer and the eof state.

   Does release user buffer mapping if it is set.
*/
static long read_tape(struct scsi_tape *STp, long count,
		      struct st_request ** aSRpnt)
{
	int transfer, blks, bytes;
	unsigned char cmd[MAX_COMMAND_SIZE];
	struct st_request *SRpnt;
	struct st_modedef *STm;
	struct st_partstat *STps;
	struct st_buffer *STbp;
	int retval = 0;

	if (count == 0)
		return 0;

	STm = &(STp->modes[STp->current_mode]);
	STps = &(STp->ps[STp->partition]);
	if (STps->eof == ST_FM_HIT)
		return 1;
	STbp = STp->buffer;

	if (STp->block_size == 0)
		blks = bytes = count;
	else {
		if (!(STp->try_dio_now && try_rdio) && STm->do_read_ahead) {
			blks = (STp->buffer)->buffer_blocks;
			bytes = blks * STp->block_size;
		} else {
			bytes = count;
			if (!STbp->do_dio && bytes > (STp->buffer)->buffer_size)
				bytes = (STp->buffer)->buffer_size;
			blks = bytes / STp->block_size;
			bytes = blks * STp->block_size;
		}
	}

	memset(cmd, 0, MAX_COMMAND_SIZE);
	cmd[0] = READ_6;
	cmd[1] = (STp->block_size != 0);
	if (!cmd[1] && STp->sili)
		cmd[1] |= 2;
	cmd[2] = blks >> 16;
	cmd[3] = blks >> 8;
	cmd[4] = blks;

	SRpnt = *aSRpnt;
	SRpnt = st_do_scsi(SRpnt, STp, cmd, bytes, DMA_FROM_DEVICE,
			   STp->device->request_queue->rq_timeout,
			   MAX_RETRIES, 1);
	release_buffering(STp, 1);
	*aSRpnt = SRpnt;
	if (!SRpnt)
		return STbp->syscall_result;

	STbp->read_pointer = 0;
	STps->at_sm = 0;

	/* Something to check */
	if (STbp->syscall_result) {
		struct st_cmdstatus *cmdstatp = &STp->buffer->cmdstat;

		retval = 1;
		DEBC_printk(STp,
			    "Sense: %2x %2x %2x %2x %2x %2x %2x %2x\n",
			    SRpnt->sense[0], SRpnt->sense[1],
			    SRpnt->sense[2], SRpnt->sense[3],
			    SRpnt->sense[4], SRpnt->sense[5],
			    SRpnt->sense[6], SRpnt->sense[7]);
		if (cmdstatp->have_sense) {

			if (cmdstatp->sense_hdr.sense_key == BLANK_CHECK)
				cmdstatp->flags &= 0xcf;	/* No need for EOM in this case */

			if (cmdstatp->flags != 0) { /* EOF, EOM, or ILI */
				/* Compute the residual count */
				if (cmdstatp->remainder_valid)
					transfer = (int)cmdstatp->uremainder64;
				else
					transfer = 0;
				if (STp->block_size == 0 &&
				    cmdstatp->sense_hdr.sense_key == MEDIUM_ERROR)
					transfer = bytes;

				if (cmdstatp->flags & SENSE_ILI) {	/* ILI */
					if (STp->block_size == 0 &&
					    transfer < 0) {
						st_printk(KERN_NOTICE, STp,
							  "Failed to read %d "
							  "byte block with %d "
							  "byte transfer.\n",
							  bytes - transfer,
							  bytes);
						if (STps->drv_block >= 0)
							STps->drv_block += 1;
						STbp->buffer_bytes = 0;
						return (-ENOMEM);
					} else if (STp->block_size == 0) {
						STbp->buffer_bytes = bytes - transfer;
					} else {
						st_release_request(SRpnt);
						SRpnt = *aSRpnt = NULL;
						if (transfer == blks) {	/* We did not get anything, error */
							st_printk(KERN_NOTICE, STp,
								  "Incorrect "
								  "block size.\n");
							if (STps->drv_block >= 0)
								STps->drv_block += blks - transfer + 1;
							st_int_ioctl(STp, MTBSR, 1);
							return (-EIO);
						}
						/* We have some data, deliver it */
						STbp->buffer_bytes = (blks - transfer) *
						    STp->block_size;
						DEBC_printk(STp, "ILI but "
							    "enough data "
							    "received %ld "
							    "%d.\n", count,
							    STbp->buffer_bytes);
						if (STps->drv_block >= 0)
							STps->drv_block += 1;
						if (st_int_ioctl(STp, MTBSR, 1))
							return (-EIO);
					}
				} else if (cmdstatp->flags & SENSE_FMK) {	/* FM overrides EOM */
					if (STps->eof != ST_FM_HIT)
						STps->eof = ST_FM_HIT;
					else
						STps->eof = ST_EOD_2;
					if (STp->block_size == 0)
						STbp->buffer_bytes = 0;
					else
						STbp->buffer_bytes =
						    bytes - transfer * STp->block_size;
					DEBC_printk(STp, "EOF detected (%d "
						    "bytes read).\n",
						    STbp->buffer_bytes);
				} else if (cmdstatp->flags & SENSE_EOM) {
					if (STps->eof == ST_FM)
						STps->eof = ST_EOD_1;
					else
						STps->eof = ST_EOM_OK;
					if (STp->block_size == 0)
						STbp->buffer_bytes = bytes - transfer;
					else
						STbp->buffer_bytes =
						    bytes - transfer * STp->block_size;

					DEBC_printk(STp, "EOM detected (%d "
						    "bytes read).\n",
						    STbp->buffer_bytes);
				}
			}
			/* end of EOF, EOM, ILI test */
			else {	/* nonzero sense key */
				DEBC_printk(STp, "Tape error while reading.\n");
				STps->drv_block = (-1);
				if (STps->eof == ST_FM &&
				    cmdstatp->sense_hdr.sense_key == BLANK_CHECK) {
					DEBC_printk(STp, "Zero returned for "
						    "first BLANK CHECK "
						    "after EOF.\n");
					STps->eof = ST_EOD_2;	/* First BLANK_CHECK after FM */
				} else	/* Some other extended sense code */
					retval = (-EIO);
			}

			if (STbp->buffer_bytes < 0)  /* Caused by bogus sense data */
				STbp->buffer_bytes = 0;
		}
		/* End of extended sense test */
		else {		/* Non-extended sense */
			retval = STbp->syscall_result;
		}

	}
	/* End of error handling */
	else {			/* Read successful */
		STbp->buffer_bytes = bytes;
		if (STp->sili) /* In fixed block mode residual is always zero here */
			STbp->buffer_bytes -= STp->buffer->cmdstat.residual;
	}

	if (STps->drv_block >= 0) {
		if (STp->block_size == 0)
			STps->drv_block++;
		else
			STps->drv_block += STbp->buffer_bytes / STp->block_size;
	}
	return retval;
}


/* Read command */
static ssize_t
st_read(struct file *filp, char __user *buf, size_t count, loff_t * ppos)
{
	ssize_t total;
	ssize_t retval = 0;
	ssize_t i, transfer;
	int special, do_dio = 0;
	struct st_request *SRpnt = NULL;
	struct scsi_tape *STp = filp->private_data;
	struct st_modedef *STm;
	struct st_partstat *STps;
	struct st_buffer *STbp = STp->buffer;

	if (mutex_lock_interruptible(&STp->lock))
		return -ERESTARTSYS;

	retval = rw_checks(STp, filp, count);
	if (retval || count == 0)
		goto out;

	STm = &(STp->modes[STp->current_mode]);
	if (STp->block_size != 0 && (count % STp->block_size) != 0) {
		if (!STm->do_read_ahead) {
			retval = (-EINVAL);	/* Read must be integral number of blocks */
			goto out;
		}
		STp->try_dio_now = 0;  /* Direct i/o can't handle split blocks */
	}

	STps = &(STp->ps[STp->partition]);
	if (STps->rw == ST_WRITING) {
		retval = flush_buffer(STp, 0);
		if (retval)
			goto out;
		STps->rw = ST_READING;
	}
	DEB(
	if (debugging && STps->eof != ST_NOEOF)
		st_printk(ST_DEB_MSG, STp,
			  "EOF/EOM flag up (%d). Bytes %d\n",
			  STps->eof, STbp->buffer_bytes);
	) /* end DEB */

	retval = setup_buffering(STp, buf, count, 1);
	if (retval)
		goto out;
	do_dio = STbp->do_dio;

	if (STbp->buffer_bytes == 0 &&
	    STps->eof >= ST_EOD_1) {
		if (STps->eof < ST_EOD) {
			STps->eof += 1;
			retval = 0;
			goto out;
		}
		retval = (-EIO);	/* EOM or Blank Check */
		goto out;
	}

	if (do_dio) {
		/* Check the buffer writability before any tape movement. Don't alter
		   buffer data. */
		if (copy_from_user(&i, buf, 1) != 0 ||
		    copy_to_user(buf, &i, 1) != 0 ||
		    copy_from_user(&i, buf + count - 1, 1) != 0 ||
		    copy_to_user(buf + count - 1, &i, 1) != 0) {
			retval = (-EFAULT);
			goto out;
		}
	}

	STps->rw = ST_READING;


	/* Loop until enough data in buffer or a special condition found */
	for (total = 0, special = 0; total < count && !special;) {

		/* Get new data if the buffer is empty */
		if (STbp->buffer_bytes == 0) {
			special = read_tape(STp, count - total, &SRpnt);
			if (special < 0) {	/* No need to continue read */
				retval = special;
				goto out;
			}
		}

		/* Move the data from driver buffer to user buffer */
		if (STbp->buffer_bytes > 0) {
			DEB(
			if (debugging && STps->eof != ST_NOEOF)
				st_printk(ST_DEB_MSG, STp,
					  "EOF up (%d). Left %d, needed %d.\n",
					  STps->eof, STbp->buffer_bytes,
					  (int)(count - total));
			) /* end DEB */
			transfer = STbp->buffer_bytes < count - total ?
			    STbp->buffer_bytes : count - total;
			if (!do_dio) {
				i = from_buffer(STbp, buf, transfer);
				if (i) {
					retval = i;
					goto out;
				}
			}
			buf += transfer;
			total += transfer;
		}

		if (STp->block_size == 0)
			break;	/* Read only one variable length block */

	}			/* for (total = 0, special = 0;
                                   total < count && !special; ) */

	/* Change the eof state if no data from tape or buffer */
	if (total == 0) {
		if (STps->eof == ST_FM_HIT) {
			STps->eof = ST_FM;
			STps->drv_block = 0;
			if (STps->drv_file >= 0)
				STps->drv_file++;
		} else if (STps->eof == ST_EOD_1) {
			STps->eof = ST_EOD_2;
			STps->drv_block = 0;
			if (STps->drv_file >= 0)
				STps->drv_file++;
		} else if (STps->eof == ST_EOD_2)
			STps->eof = ST_EOD;
	} else if (STps->eof == ST_FM)
		STps->eof = ST_NOEOF;
	retval = total;

 out:
	if (SRpnt != NULL) {
		st_release_request(SRpnt);
		SRpnt = NULL;
	}
	if (do_dio) {
		release_buffering(STp, 1);
		STbp->buffer_bytes = 0;
	}
	mutex_unlock(&STp->lock);

	return retval;
}



DEB(
/* Set the driver options */
static void st_log_options(struct scsi_tape * STp, struct st_modedef * STm)
{
	if (debugging) {
		st_printk(KERN_INFO, STp,
			  "Mode %d options: buffer writes: %d, "
			  "async writes: %d, read ahead: %d\n",
			  STp->current_mode, STm->do_buffer_writes,
			  STm->do_async_writes, STm->do_read_ahead);
		st_printk(KERN_INFO, STp,
			  "    can bsr: %d, two FMs: %d, "
			  "fast mteom: %d, auto lock: %d,\n",
			  STp->can_bsr, STp->two_fm, STp->fast_mteom,
			  STp->do_auto_lock);
		st_printk(KERN_INFO, STp,
			  "    defs for wr: %d, no block limits: %d, "
			  "partitions: %d, s2 log: %d\n",
			  STm->defaults_for_writes, STp->omit_blklims,
			  STp->can_partitions, STp->scsi2_logical);
		st_printk(KERN_INFO, STp,
			  "    sysv: %d nowait: %d sili: %d "
			  "nowait_filemark: %d\n",
			  STm->sysv, STp->immediate, STp->sili,
			  STp->immediate_filemark);
		st_printk(KERN_INFO, STp, "    debugging: %d\n", debugging);
	}
}
	)


static int st_set_options(struct scsi_tape *STp, long options)
{
	int value;
	long code;
	struct st_modedef *STm;
	struct cdev *cd0, *cd1;
	struct device *d0, *d1;

	STm = &(STp->modes[STp->current_mode]);
	if (!STm->defined) {
		cd0 = STm->cdevs[0];
		cd1 = STm->cdevs[1];
		d0  = STm->devs[0];
		d1  = STm->devs[1];
		memcpy(STm, &(STp->modes[0]), sizeof(struct st_modedef));
		STm->cdevs[0] = cd0;
		STm->cdevs[1] = cd1;
		STm->devs[0]  = d0;
		STm->devs[1]  = d1;
		modes_defined = 1;
		DEBC_printk(STp, "Initialized mode %d definition from mode 0\n",
			    STp->current_mode);
	}

	code = options & MT_ST_OPTIONS;
	if (code == MT_ST_BOOLEANS) {
		STm->do_buffer_writes = (options & MT_ST_BUFFER_WRITES) != 0;
		STm->do_async_writes = (options & MT_ST_ASYNC_WRITES) != 0;
		STm->defaults_for_writes = (options & MT_ST_DEF_WRITES) != 0;
		STm->do_read_ahead = (options & MT_ST_READ_AHEAD) != 0;
		STp->two_fm = (options & MT_ST_TWO_FM) != 0;
		STp->fast_mteom = (options & MT_ST_FAST_MTEOM) != 0;
		STp->do_auto_lock = (options & MT_ST_AUTO_LOCK) != 0;
		STp->can_bsr = (options & MT_ST_CAN_BSR) != 0;
		STp->omit_blklims = (options & MT_ST_NO_BLKLIMS) != 0;
		if ((STp->device)->scsi_level >= SCSI_2)
			STp->can_partitions = (options & MT_ST_CAN_PARTITIONS) != 0;
		STp->scsi2_logical = (options & MT_ST_SCSI2LOGICAL) != 0;
		STp->immediate = (options & MT_ST_NOWAIT) != 0;
		STp->immediate_filemark = (options & MT_ST_NOWAIT_EOF) != 0;
		STm->sysv = (options & MT_ST_SYSV) != 0;
		STp->sili = (options & MT_ST_SILI) != 0;
		DEB( debugging = (options & MT_ST_DEBUGGING) != 0;
		     st_log_options(STp, STm); )
	} else if (code == MT_ST_SETBOOLEANS || code == MT_ST_CLEARBOOLEANS) {
		value = (code == MT_ST_SETBOOLEANS);
		if ((options & MT_ST_BUFFER_WRITES) != 0)
			STm->do_buffer_writes = value;
		if ((options & MT_ST_ASYNC_WRITES) != 0)
			STm->do_async_writes = value;
		if ((options & MT_ST_DEF_WRITES) != 0)
			STm->defaults_for_writes = value;
		if ((options & MT_ST_READ_AHEAD) != 0)
			STm->do_read_ahead = value;
		if ((options & MT_ST_TWO_FM) != 0)
			STp->two_fm = value;
		if ((options & MT_ST_FAST_MTEOM) != 0)
			STp->fast_mteom = value;
		if ((options & MT_ST_AUTO_LOCK) != 0)
			STp->do_auto_lock = value;
		if ((options & MT_ST_CAN_BSR) != 0)
			STp->can_bsr = value;
		if ((options & MT_ST_NO_BLKLIMS) != 0)
			STp->omit_blklims = value;
		if ((STp->device)->scsi_level >= SCSI_2 &&
		    (options & MT_ST_CAN_PARTITIONS) != 0)
			STp->can_partitions = value;
		if ((options & MT_ST_SCSI2LOGICAL) != 0)
			STp->scsi2_logical = value;
		if ((options & MT_ST_NOWAIT) != 0)
			STp->immediate = value;
		if ((options & MT_ST_NOWAIT_EOF) != 0)
			STp->immediate_filemark = value;
		if ((options & MT_ST_SYSV) != 0)
			STm->sysv = value;
		if ((options & MT_ST_SILI) != 0)
			STp->sili = value;
		DEB(
		if ((options & MT_ST_DEBUGGING) != 0)
			debugging = value;
			st_log_options(STp, STm); )
	} else if (code == MT_ST_WRITE_THRESHOLD) {
		/* Retained for compatibility */
	} else if (code == MT_ST_DEF_BLKSIZE) {
		value = (options & ~MT_ST_OPTIONS);
		if (value == ~MT_ST_OPTIONS) {
			STm->default_blksize = (-1);
			DEBC_printk(STp, "Default block size disabled.\n");
		} else {
			STm->default_blksize = value;
			DEBC_printk(STp,"Default block size set to "
				    "%d bytes.\n", STm->default_blksize);
			if (STp->ready == ST_READY) {
				STp->blksize_changed = 0;
				set_mode_densblk(STp, STm);
			}
		}
	} else if (code == MT_ST_TIMEOUTS) {
		value = (options & ~MT_ST_OPTIONS);
		if ((value & MT_ST_SET_LONG_TIMEOUT) != 0) {
			STp->long_timeout = (value & ~MT_ST_SET_LONG_TIMEOUT) * HZ;
			DEBC_printk(STp, "Long timeout set to %d seconds.\n",
				    (value & ~MT_ST_SET_LONG_TIMEOUT));
		} else {
			blk_queue_rq_timeout(STp->device->request_queue,
					     value * HZ);
			DEBC_printk(STp, "Normal timeout set to %d seconds.\n",
				    value);
		}
	} else if (code == MT_ST_SET_CLN) {
		value = (options & ~MT_ST_OPTIONS) & 0xff;
		if (value != 0 &&
			(value < EXTENDED_SENSE_START ||
				value >= SCSI_SENSE_BUFFERSIZE))
			return (-EINVAL);
		STp->cln_mode = value;
		STp->cln_sense_mask = (options >> 8) & 0xff;
		STp->cln_sense_value = (options >> 16) & 0xff;
		st_printk(KERN_INFO, STp,
			  "Cleaning request mode %d, mask %02x, value %02x\n",
			  value, STp->cln_sense_mask, STp->cln_sense_value);
	} else if (code == MT_ST_DEF_OPTIONS) {
		code = (options & ~MT_ST_CLEAR_DEFAULT);
		value = (options & MT_ST_CLEAR_DEFAULT);
		if (code == MT_ST_DEF_DENSITY) {
			if (value == MT_ST_CLEAR_DEFAULT) {
				STm->default_density = (-1);
				DEBC_printk(STp,
					    "Density default disabled.\n");
			} else {
				STm->default_density = value & 0xff;
				DEBC_printk(STp, "Density default set to %x\n",
					    STm->default_density);
				if (STp->ready == ST_READY) {
					STp->density_changed = 0;
					set_mode_densblk(STp, STm);
				}
			}
		} else if (code == MT_ST_DEF_DRVBUFFER) {
			if (value == MT_ST_CLEAR_DEFAULT) {
				STp->default_drvbuffer = 0xff;
				DEBC_printk(STp,
					    "Drive buffer default disabled.\n");
			} else {
				STp->default_drvbuffer = value & 7;
				DEBC_printk(STp,
					    "Drive buffer default set to %x\n",
					    STp->default_drvbuffer);
				if (STp->ready == ST_READY)
					st_int_ioctl(STp, MTSETDRVBUFFER, STp->default_drvbuffer);
			}
		} else if (code == MT_ST_DEF_COMPRESSION) {
			if (value == MT_ST_CLEAR_DEFAULT) {
				STm->default_compression = ST_DONT_TOUCH;
				DEBC_printk(STp,
					    "Compression default disabled.\n");
			} else {
				if ((value & 0xff00) != 0) {
					STp->c_algo = (value & 0xff00) >> 8;
					DEBC_printk(STp, "Compression "
						    "algorithm set to 0x%x.\n",
						    STp->c_algo);
				}
				if ((value & 0xff) != 0xff) {
					STm->default_compression = (value & 1 ? ST_YES : ST_NO);
					DEBC_printk(STp, "Compression default "
						    "set to %x\n",
						    (value & 1));
					if (STp->ready == ST_READY) {
						STp->compression_changed = 0;
						st_compression(STp, (STm->default_compression == ST_YES));
					}
				}
			}
		}
	} else
		return (-EIO);

	return 0;
}

#define MODE_HEADER_LENGTH  4

/* Mode header and page byte offsets */
#define MH_OFF_DATA_LENGTH     0
#define MH_OFF_MEDIUM_TYPE     1
#define MH_OFF_DEV_SPECIFIC    2
#define MH_OFF_BDESCS_LENGTH   3
#define MP_OFF_PAGE_NBR        0
#define MP_OFF_PAGE_LENGTH     1

/* Mode header and page bit masks */
#define MH_BIT_WP              0x80
#define MP_MSK_PAGE_NBR        0x3f

/* Don't return block descriptors */
#define MODE_SENSE_OMIT_BDESCS 0x08

#define MODE_SELECT_PAGE_FORMAT 0x10

/* Read a mode page into the tape buffer. The block descriptors are included
   if incl_block_descs is true. The page control is ored to the page number
   parameter, if necessary. */
static int read_mode_page(struct scsi_tape *STp, int page, int omit_block_descs)
{
	unsigned char cmd[MAX_COMMAND_SIZE];
	struct st_request *SRpnt;

	memset(cmd, 0, MAX_COMMAND_SIZE);
	cmd[0] = MODE_SENSE;
	if (omit_block_descs)
		cmd[1] = MODE_SENSE_OMIT_BDESCS;
	cmd[2] = page;
	cmd[4] = 255;

	SRpnt = st_do_scsi(NULL, STp, cmd, cmd[4], DMA_FROM_DEVICE,
			   STp->device->request_queue->rq_timeout, 0, 1);
	if (SRpnt == NULL)
		return (STp->buffer)->syscall_result;

	st_release_request(SRpnt);

	return STp->buffer->syscall_result;
}


/* Send the mode page in the tape buffer to the drive. Assumes that the mode data
   in the buffer is correctly formatted. The long timeout is used if slow is non-zero. */
static int write_mode_page(struct scsi_tape *STp, int page, int slow)
{
	int pgo;
	unsigned char cmd[MAX_COMMAND_SIZE];
	struct st_request *SRpnt;
	int timeout;

	memset(cmd, 0, MAX_COMMAND_SIZE);
	cmd[0] = MODE_SELECT;
	cmd[1] = MODE_SELECT_PAGE_FORMAT;
	pgo = MODE_HEADER_LENGTH + (STp->buffer)->b_data[MH_OFF_BDESCS_LENGTH];
	cmd[4] = pgo + (STp->buffer)->b_data[pgo + MP_OFF_PAGE_LENGTH] + 2;

	/* Clear reserved fields */
	(STp->buffer)->b_data[MH_OFF_DATA_LENGTH] = 0;
	(STp->buffer)->b_data[MH_OFF_MEDIUM_TYPE] = 0;
	(STp->buffer)->b_data[MH_OFF_DEV_SPECIFIC] &= ~MH_BIT_WP;
	(S