/*
 * WUSB Wire Adapter
 * Data transfer and URB enqueing
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * How transfers work: get a buffer, break it up in segments (segment
 * size is a multiple of the maxpacket size). For each segment issue a
 * segment request (struct wa_xfer_*), then send the data buffer if
 * out or nothing if in (all over the DTO endpoint).
 *
 * For each submitted segment request, a notification will come over
 * the NEP endpoint and a transfer result (struct xfer_result) will
 * arrive in the DTI URB. Read it, get the xfer ID, see if there is
 * data coming (inbound transfer), schedule a read and handle it.
 *
 * Sounds simple, it is a pain to implement.
 *
 *
 * ENTRY POINTS
 *
 *   FIXME
 *
 * LIFE CYCLE / STATE DIAGRAM
 *
 *   FIXME
 *
 * THIS CODE IS DISGUSTING
 *
 *   Warned you are; it's my second try and still not happy with it.
 *
 * NOTES:
 *
 *   - No iso
 *
 *   - Supports DMA xfers, control, bulk and maybe interrupt
 *
 *   - Does not recycle unused rpipes
 *
 *     An rpipe is assigned to an endpoint the first time it is used,
 *     and then it's there, assigned, until the endpoint is disabled
 *     (destroyed [{h,d}wahc_op_ep_disable()]. The assignment of the
 *     rpipe to the endpoint is done under the wa->rpipe_sem semaphore
 *     (should be a mutex).
 *
 *     Two methods it could be done:
 *
 *     (a) set up a timer every time an rpipe's use count drops to 1
 *         (which means unused) or when a transfer ends. Reset the
 *         timer when a xfer is queued. If the timer expires, release
 *         the rpipe [see rpipe_ep_disable()].
 *
 *     (b) when looking for free rpipes to attach [rpipe_get_by_ep()],
 *         when none are found go over the list, check their endpoint
 *         and their activity record (if no last-xfer-done-ts in the
 *         last x seconds) take it
 *
 *     However, due to the fact that we have a set of limited
 *     resources (max-segments-at-the-same-time per xfer,
 *     xfers-per-ripe, blocks-per-rpipe, rpipes-per-host), at the end
 *     we are going to have to rebuild all this based on an scheduler,
 *     to where we have a list of transactions to do and based on the
 *     availability of the different required components (blocks,
 *     rpipes, segment slots, etc), we go scheduling them. Painful.
 */
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/hash.h>
#include <linux/ratelimit.h>
#include <linux/export.h>
#include <linux/scatterlist.h>

#include "wa-hc.h"
#include "wusbhc.h"

enum {
	/* [WUSB] section 8.3.3 allocates 7 bits for the segment index. */
	WA_SEGS_MAX = 128,
};

enum wa_seg_status {
	WA_SEG_NOTREADY,
	WA_SEG_READY,
	WA_SEG_DELAYED,
	WA_SEG_SUBMITTED,
	WA_SEG_PENDING,
	WA_SEG_DTI_PENDING,
	WA_SEG_DONE,
	WA_SEG_ERROR,
	WA_SEG_ABORTED,
};

static void wa_xfer_delayed_run(struct wa_rpipe *);
static int __wa_xfer_delayed_run(struct wa_rpipe *rpipe, int *dto_waiting);

/*
 * Life cycle governed by 'struct urb' (the refcount of the struct is
 * that of the 'struct urb' and usb_free_urb() would free the whole
 * struct).
 */
struct wa_seg {
	struct urb tr_urb;		/* transfer request urb. */
	struct urb *isoc_pack_desc_urb;	/* for isoc packet descriptor. */
	struct urb *dto_urb;		/* for data output. */
	struct list_head list_node;	/* for rpipe->req_list */
	struct wa_xfer *xfer;		/* out xfer */
	u8 index;			/* which segment we are */
	int isoc_frame_count;	/* number of isoc frames in this segment. */
	int isoc_frame_offset;	/* starting frame offset in the xfer URB. */
	/* Isoc frame that the current transfer buffer corresponds to. */
	int isoc_frame_index;
	int isoc_size;	/* size of all isoc frames sent by this seg. */
	enum wa_seg_status status;
	ssize_t result;			/* bytes xfered or error */
	struct wa_xfer_hdr xfer_hdr;
};

static inline void wa_seg_init(struct wa_seg *seg)
{
	usb_init_urb(&seg->tr_urb);

	/* set the remaining memory to 0. */
	memset(((void *)seg) + sizeof(seg->tr_urb), 0,
		sizeof(*seg) - sizeof(seg->tr_urb));
}

/*
 * Protected by xfer->lock
 *
 */
struct wa_xfer {
	struct kref refcnt;
	struct list_head list_node;
	spinlock_t lock;
	u32 id;

	struct wahc *wa;		/* Wire adapter we are plugged to */
	struct usb_host_endpoint *ep;
	struct urb *urb;		/* URB we are transferring for */
	struct wa_seg **seg;		/* transfer segments */
	u8 segs, segs_submitted, segs_done;
	unsigned is_inbound:1;
	unsigned is_dma:1;
	size_t seg_size;
	int result;

	gfp_t gfp;			/* allocation mask */

	struct wusb_dev *wusb_dev;	/* for activity timestamps */
};

static void __wa_populate_dto_urb_isoc(struct wa_xfer *xfer,
	struct wa_seg *seg, int curr_iso_frame);
static void wa_complete_remaining_xfer_segs(struct wa_xfer *xfer,
		int starting_index, enum wa_seg_status status);

static inline void wa_xfer_init(struct wa_xfer *xfer)
{
	kref_init(&xfer->refcnt);
	INIT_LIST_HEAD(&xfer->list_node);
	spin_lock_init(&xfer->lock);
}

/*
 * Destroy a transfer structure
 *
 * Note that freeing xfer->seg[cnt]->tr_urb will free the containing
 * xfer->seg[cnt] memory that was allocated by __wa_xfer_setup_segs.
 */
static void wa_xfer_destroy(struct kref *_xfer)
{
	struct wa_xfer *xfer = container_of(_xfer, struct wa_xfer, refcnt);
	if (xfer->seg) {
		unsigned cnt;
		for (cnt = 0; cnt < xfer->segs; cnt++) {
			struct wa_seg *seg = xfer->seg[cnt];
			if (seg) {
				usb_free_urb(seg->isoc_pack_desc_urb);
				if (seg->dto_urb) {
					kfree(seg->dto_urb->sg);
					usb_free_urb(seg->dto_urb);
				}
				usb_free_urb(&seg->tr_urb);
			}
		}
		kfree(xfer->seg);
	}
	kfree(xfer);
}

static void wa_xfer_get(struct wa_xfer *xfer)
{
	kref_get(&xfer->refcnt);
}

static void wa_xfer_put(struct wa_xfer *xfer)
{
	kref_put(&xfer->refcnt, wa_xfer_destroy);
}

/*
 * Try to get exclusive access to the DTO endpoint resource.  Return true
 * if successful.
 */
static inline int __wa_dto_try_get(struct wahc *wa)
{
	return (test_and_set_bit(0, &wa->dto_in_use) == 0);
}

/* Release the DTO endpoint resource. */
static inline void __wa_dto_put(struct wahc *wa)
{
	clear_bit_unlock(0, &wa->dto_in_use);
}

/* Service RPIPEs that are waiting on the DTO resource. */
static void wa_check_for_delayed_rpipes(struct wahc *wa)
{
	unsigned long flags;
	int dto_waiting = 0;
	struct wa_rpipe *rpipe;

	spin_lock_irqsave(&wa->rpipe_lock, flags);
	while (!list_empty(&wa->rpipe_delayed_list) && !dto_waiting) {
		rpipe = list_first_entry(&wa->rpipe_delayed_list,
				struct wa_rpipe, list_node);
		__wa_xfer_delayed_run(rpipe, &dto_waiting);
		/* remove this RPIPE from the list if it is not waiting. */
		if (!dto_waiting) {
			pr_debug("%s: RPIPE %d serviced and removed from delayed list.\n",
				__func__,
				le16_to_cpu(rpipe->descr.wRPipeIndex));
			list_del_init(&rpipe->list_node);
		}
	}
	spin_unlock_irqrestore(&wa->rpipe_lock, flags);
}

/* add this RPIPE to the end of the delayed RPIPE list. */
static void wa_add_delayed_rpipe(struct wahc *wa, struct wa_rpipe *rpipe)
{
	unsigned long flags;

	spin_lock_irqsave(&wa->rpipe_lock, flags);
	/* add rpipe to the list if it is not already on it. */
	if (list_empty(&rpipe->list_node)) {
		pr_debug("%s: adding RPIPE %d to the delayed list.\n",
			__func__, le16_to_cpu(rpipe->descr.wRPipeIndex));
		list_add_tail(&rpipe->list_node, &wa->rpipe_delayed_list);
	}
	spin_unlock_irqrestore(&wa->rpipe_lock, flags);
}

/*
 * xfer is referenced
 *
 * xfer->lock has to be unlocked
 *
 * We take xfer->lock for setting the result; this is a barrier
 * against drivers/usb/core/hcd.c:unlink1() being called after we call
 * usb_hcd_giveback_urb() and wa_urb_dequeue() trying to get a
 * reference to the transfer.
 */
static void wa_xfer_giveback(struct wa_xfer *xfer)
{
	unsigned long flags;

	spin_lock_irqsave(&xfer->wa->xfer_list_lock, flags);
	list_del_init(&xfer->list_node);
	usb_hcd_unlink_urb_from_ep(&(xfer->wa->wusb->usb_hcd), xfer->urb);
	spin_unlock_irqrestore(&xfer->wa->xfer_list_lock, flags);
	/* FIXME: segmentation broken -- kills DWA */
	wusbhc_giveback_urb(xfer->wa->wusb, xfer->urb, xfer->result);
	wa_put(xfer->wa);
	wa_xfer_put(xfer);
}

/*
 * xfer is referenced
 *
 * xfer->lock has to be unlocked
 */
static void wa_xfer_completion(struct wa_xfer *xfer)
{
	if (xfer->wusb_dev)
		wusb_dev_put(xfer->wusb_dev);
	rpipe_put(xfer->ep->hcpriv);
	wa_xfer_giveback(xfer);
}

/*
 * Initialize a transfer's ID
 *
 * We need to use a sequential number; if we use the pointer or the
 * hash of the pointer, it can repeat over sequential transfers and
 * then it will confuse the HWA....wonder why in hell they put a 32
 * bit handle in there then.
 */
static void wa_xfer_id_init(struct wa_xfer *xfer)
{
	xfer->id = atomic_add_return(1, &xfer->wa->xfer_id_count);
}

/* Return the xfer's ID. */
static inline u32 wa_xfer_id(struct wa_xfer *xfer)
{
	return xfer->id;
}

/* Return the xfer's ID in transport format (little endian). */
static inline __le32 wa_xfer_id_le32(struct wa_xfer *xfer)
{
	return cpu_to_le32(xfer->id);
}

/*
 * If transfer is done, wrap it up and return true
 *
 * xfer->lock has to be locked
 */
static unsigned __wa_xfer_is_done(struct wa_xfer *xfer)
{
	struct device *dev = &xfer->wa->usb_iface->dev;
	unsigned result, cnt;
	struct wa_seg *seg;
	struct urb *urb = xfer->urb;
	unsigned found_short = 0;

	result = xfer->segs_done == xfer->segs_submitted;
	if (result == 0)
		goto out;
	urb->actual_length = 0;
	for (cnt = 0; cnt < xfer->segs; cnt++) {
		seg = xfer->seg[cnt];
		switch (seg->status) {
		case WA_SEG_DONE:
			if (found_short && seg->result > 0) {
				dev_dbg(dev, "xfer %p ID %08X#%u: bad short segments (%zu)\n",
					xfer, wa_xfer_id(xfer), cnt,
					seg->result);
				urb->status = -EINVAL;
				goto out;
			}
			urb->actual_length += seg->result;
			if (!(usb_pipeisoc(xfer->urb->pipe))
				&& seg->result < xfer->seg_size
			    && cnt != xfer->segs-1)
				found_short = 1;
			dev_dbg(dev, "xfer %p ID %08X#%u: DONE short %d "
				"result %zu urb->actual_length %d\n",
				xfer, wa_xfer_id(xfer), seg->index, found_short,
				seg->result, urb->actual_length);
			break;
		case WA_SEG_ERROR:
			xfer->result = seg->result;
			dev_dbg(dev, "xfer %p ID %08X#%u: ERROR result %zi(0x%08zX)\n",
				xfer, wa_xfer_id(xfer), seg->index, seg->result,
				seg->result);
			goto out;
		case WA_SEG_ABORTED:
			xfer->result = seg->result;
			dev_dbg(dev, "xfer %p ID %08X#%u: ABORTED result %zi(0x%08zX)\n",
				xfer, wa_xfer_id(xfer), seg->index, seg->result,
				seg->result);
			goto out;
		default:
			dev_warn(dev, "xfer %p ID %08X#%u: is_done bad state %d\n",
				 xfer, wa_xfer_id(xfer), cnt, seg->status);
			xfer->result = -EINVAL;
			goto out;
		}
	}
	xfer->result = 0;
out:
	return result;
}

/*
 * Mark the given segment as done.  Return true if this completes the xfer.
 * This should only be called for segs that have been submitted to an RPIPE.
 * Delayed segs are not marked as submitted so they do not need to be marked
 * as done when cleaning up.
 *
 * xfer->lock has to be locked
 */
static unsigned __wa_xfer_mark_seg_as_done(struct wa_xfer *xfer,
	struct wa_seg *seg, enum wa_seg_status status)
{
	seg->status = status;
	xfer->segs_done++;

	/* check for done. */
	return __wa_xfer_is_done(xfer);
}

/*
 * Search for a transfer list ID on the HCD's URB list
 *
 * For 32 bit architectures, we use the pointer itself; for 64 bits, a
 * 32-bit hash of the pointer.
 *
 * @returns NULL if not found.
 */
static struct wa_xfer *wa_xfer_get_by_id(struct wahc *wa, u32 id)
{
	unsigned long flags;
	struct wa_xfer *xfer_itr;
	spin_lock_irqsave(&wa->xfer_list_lock, flags);
	list_for_each_entry(xfer_itr, &wa->xfer_list, list_node) {
		if (id == xfer_itr->id) {
			wa_xfer_get(xfer_itr);
			goto out;
		}
	}
	xfer_itr = NULL;
out:
	spin_unlock_irqrestore(&wa->xfer_list_lock, flags);
	return xfer_itr;
}

struct wa_xfer_abort_buffer {
	struct urb urb;
	struct wahc *wa;
	struct wa_xfer_abort cmd;
};

static void __wa_xfer_abort_cb(struct urb *urb)
{
	struct wa_xfer_abort_buffer *b = urb->context;
	struct wahc *wa = b->wa;

	/*
	 * If the abort request URB failed, then the HWA did not get the abort
	 * command.  Forcibly clean up the xfer without waiting for a Transfer
	 * Result from the HWA.
	 */
	if (urb->status < 0) {
		struct wa_xfer *xfer;
		struct device *dev = &wa->usb_iface->dev;

		xfer = wa_xfer_get_by_id(wa, le32_to_cpu(b->cmd.dwTransferID));
		dev_err(dev, "%s: Transfer Abort request failed. result: %d\n",
			__func__, urb->status);
		if (xfer) {
			unsigned long flags;
			int done, seg_index = 0;
			struct wa_rpipe *rpipe = xfer->ep->hcpriv;

			dev_err(dev, "%s: cleaning up xfer %p ID 0x%08X.\n",
				__func__, xfer, wa_xfer_id(xfer));
			spin_lock_irqsave(&xfer->lock, flags);
			/* skip done segs. */
			while (seg_index < xfer->segs) {
				struct wa_seg *seg = xfer->seg[seg_index];

				if ((seg->status == WA_SEG_DONE) ||
					(seg->status == WA_SEG_ERROR)) {
					++seg_index;
				} else {
					break;
				}
			}
			/* mark remaining segs as aborted. */
			wa_complete_remaining_xfer_segs(xfer, seg_index,
				WA_SEG_ABORTED);
			done = __wa_xfer_is_done(xfer);
			spin_unlock_irqrestore(&xfer->lock, flags);
			if (done)
				wa_xfer_completion(xfer);
			wa_xfer_delayed_run(rpipe);
			wa_xfer_put(xfer);
		} else {
			dev_err(dev, "%s: xfer ID 0x%08X already gone.\n",
				 __func__, le32_to_cpu(b->cmd.dwTransferID));
		}
	}

	wa_put(wa);	/* taken in __wa_xfer_abort */
	usb_put_urb(&b->urb);
}

/*
 * Aborts an ongoing transaction
 *
 * Assumes the transfer is referenced and locked and in a submitted
 * state (mainly that there is an endpoint/rpipe assigned).
 *
 * The callback (see above) does nothing but freeing up the data by
 * putting the URB. Because the URB is allocated at the head of the
 * struct, the whole space we allocated is kfreed. *
 */
static int __wa_xfer_abort(struct wa_xfer *xfer)
{
	int result = -ENOMEM;
	struct device *dev = &xfer->wa->usb_iface->dev;
	struct wa_xfer_abort_buffer *b;
	struct wa_rpipe *rpipe = xfer->ep->hcpriv;

	b = kmalloc(sizeof(*b), GFP_ATOMIC);
	if (b == NULL)
		goto error_kmalloc;
	b->cmd.bLength =  sizeof(b->cmd);
	b->cmd.bRequestType = WA_XFER_ABORT;
	b->cmd.wRPipe = rpipe->descr.wRPipeIndex;
	b->cmd.dwTransferID = wa_xfer_id_le32(xfer);
	b->wa = wa_get(xfer->wa);

	usb_init_urb(&b->urb);
	usb_fill_bulk_urb(&b->urb, xfer->wa->usb_dev,
		usb_sndbulkpipe(xfer->wa->usb_dev,
				xfer->wa->dto_epd->bEndpointAddress),
		&b->cmd, sizeof(b->cmd), __wa_xfer_abort_cb, b);
	result = usb_submit_urb(&b->urb, GFP_ATOMIC);
	if (result < 0)
		goto error_submit;
	return result;				/* callback frees! */


error_submit:
	wa_put(xfer->wa);
	if (printk_ratelimit())
		dev_err(dev, "xfer %p: Can't submit abort request: %d\n",
			xfer, result);
	kfree(b);
error_kmalloc:
	return result;

}

/*
 * Calculate the number of isoc frames starting from isoc_frame_offset
 * that will fit a in transfer segment.
 */
static int __wa_seg_calculate_isoc_frame_count(struct wa_xfer *xfer,
	int isoc_frame_offset, int *total_size)
{
	int segment_size = 0, frame_count = 0;
	int index = isoc_frame_offset;
	struct usb_iso_packet_descriptor *iso_frame_desc =
		xfer->urb->iso_frame_desc;

	while ((index < xfer->urb->number_of_packets)
		&& ((segment_size + iso_frame_desc[index].length)
				<= xfer->seg_size)) {
		/*
		 * For Alereon HWA devices, only include an isoc frame in an
		 * out segment if it is physically contiguous with the previous
		 * frame.  This is required because those devices expect
		 * the isoc frames to be sent as a single USB transaction as
		 * opposed to one transaction per frame with standard HWA.
		 */
		if ((xfer->wa->quirks & WUSB_QUIRK_ALEREON_HWA_CONCAT_ISOC)
			&& (xfer->is_inbound == 0)
			&& (index > isoc_frame_offset)
			&& ((iso_frame_desc[index - 1].offset +
				iso_frame_desc[index - 1].length) !=
				iso_frame_desc[index].offset))
			break;

		/* this frame fits. count it. */
		++frame_count;
		segment_size += iso_frame_desc[index].length;

		/* move to the next isoc frame. */
		++index;
	}

	*total_size = segment_size;
	return frame_count;
}

/*
 *
 * @returns < 0 on error, transfer segment request size if ok
 */
static ssize_t __wa_xfer_setup_sizes(struct wa_xfer *xfer,
				     enum wa_xfer_type *pxfer_type)
{
	ssize_t result;
	struct device *dev = &xfer->wa->usb_iface->dev;
	size_t maxpktsize;
	struct urb *urb = xfer->urb;
	struct wa_rpipe *rpipe = xfer->ep->hcpriv;

	switch (rpipe->descr.bmAttribute & 0x3) {
	case USB_ENDPOINT_XFER_CONTROL:
		*pxfer_type = WA_XFER_TYPE_CTL;
		result = sizeof(struct wa_xfer_ctl);
		break;
	case USB_ENDPOINT_XFER_INT:
	case USB_ENDPOINT_XFER_BULK:
		*pxfer_type = WA_XFER_TYPE_BI;
		result = sizeof(struct wa_xfer_bi);
		break;
	case USB_ENDPOINT_XFER_ISOC:
		*pxfer_type = WA_XFER_TYPE_ISO;
		result = sizeof(struct wa_xfer_hwaiso);
		break;
	default:
		/* never happens */
		BUG();
		result = -EINVAL;	/* shut gcc up */
	}
	xfer->is_inbound = urb->pipe & USB_DIR_IN ? 1 : 0;
	xfer->is_dma = urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP ? 1 : 0;

	maxpktsize = le16_to_cpu(rpipe->descr.wMaxPacketSize);
	xfer->seg_size = le16_to_cpu(rpipe->descr.wBlocks)
		* 1 << (xfer->wa->wa_descr->bRPipeBlockSize - 1);
	/* Compute the segment size and make sure it is a multiple of
	 * the maxpktsize (WUSB1.0[8.3.3.1])...not really too much of
	 * a check (FIXME) */
	if (xfer->seg_size < maxpktsize) {
		dev_err(dev,
			"HW BUG? seg_size %zu smaller than maxpktsize %zu\n",
			xfer->seg_size, maxpktsize);
		result = -EINVAL;
		goto error;
	}
	xfer->seg_size = (xfer->seg_size / maxpktsize) * maxpktsize;
	if ((rpipe->descr.bmAttribute & 0x3) == USB_ENDPOINT_XFER_ISOC) {
		int index = 0;

		xfer->segs = 0;
		/*
		 * loop over urb->number_of_packets to determine how many
		 * xfer segments will be needed to send the isoc frames.
		 */
		while (index < urb->number_of_packets) {
			int seg_size; /* don't care. */
			index += __wa_seg_calculate_isoc_frame_count(xfer,
					index, &seg_size);
			++xfer->segs;
		}
	} else {
		xfer->segs = DIV_ROUND_UP(urb->transfer_buffer_length,
						xfer->seg_size);
		if (xfer->segs == 0 && *pxfer_type == WA_XFER_TYPE_CTL)
			xfer->segs = 1;
	}

	if (xfer->segs > WA_SEGS_MAX) {
		dev_err(dev, "BUG? oops, number of segments %zu bigger than %d\n",
			(urb->transfer_buffer_length/xfer->seg_size),
			WA_SEGS_MAX);
		result = -EINVAL;
		goto error;
	}
error:
	return result;
}

static void __wa_setup_isoc_packet_descr(
		struct wa_xfer_packet_info_hwaiso *packet_desc,
		struct wa_xfer *xfer,
		struct wa_seg *seg) {
	struct usb_iso_packet_descriptor *iso_frame_desc =
		xfer->urb->iso_frame_desc;
	int frame_index;

	/* populate isoc packet descriptor. */
	packet_desc->bPacketType = WA_XFER_ISO_PACKET_INFO;
	packet_desc->wLength = cpu_to_le16(sizeof(*packet_desc) +
		(sizeof(packet_desc->PacketLength[0]) *
			seg->isoc_frame_count));
	for (frame_index = 0; frame_index < seg->isoc_frame_count;
		++frame_index) {
		int offset_index = frame_index + seg->isoc_frame_offset;
		packet_desc->PacketLength[frame_index] =
			cpu_to_le16(iso_frame_desc[offset_index].length);
	}
}


/* Fill in the common request header and xfer-type specific data. */
static void __wa_xfer_setup_hdr0(struct wa_xfer *xfer,
				 struct wa_xfer_hdr *xfer_hdr0,
				 enum wa_xfer_type xfer_type,
				 size_t xfer_hdr_size)
{
	struct wa_rpipe *rpipe = xfer->ep->hcpriv;
	struct wa_seg *seg = xfer->seg[0];

	xfer_hdr0 = &seg->xfer_hdr;
	xfer_hdr0->bLength = xfer_hdr_size;
	xfer_hdr0->bRequestType = xfer_type;
	xfer_hdr0->wRPipe = rpipe->descr.wRPipeIndex;
	xfer_hdr0->dwTransferID = wa_xfer_id_le32(xfer);
	xfer_hdr0->bTransferSegment = 0;
	switch (xfer_type) {
	case WA_XFER_TYPE_CTL: {
		struct wa_xfer_ctl *xfer_ctl =
			container_of(xfer_hdr0, struct wa_xfer_ctl, hdr);
		xfer_ctl->bmAttribute = xfer->is_inbound ? 1 : 0;
		memcpy(&xfer_ctl->baSetupData, xfer->urb->setup_packet,
		       sizeof(xfer_ctl->baSetupData));
		break;
	}
	case WA_XFER_TYPE_BI:
		break;
	case WA_XFER_TYPE_ISO: {
		struct wa_xfer_hwaiso *xfer_iso =
			container_of(xfer_hdr0, struct wa_xfer_hwaiso, hdr);
		struct wa_xfer_packet_info_hwaiso *packet_desc =
			((void *)xfer_iso) + xfer_hdr_size;

		/* populate the isoc section of the transfer request. */
		xfer_iso->dwNumOfPackets = cpu_to_le32(seg->isoc_frame_count);
		/* populate isoc packet descriptor. */
		__wa_setup_isoc_packet_descr(packet_desc, xfer, seg);
		break;
	}
	default:
		BUG();
	};
}

/*
 * Callback for the OUT data phase of the segment request
 *
 * Check wa_seg_tr_cb(); most comments also apply here because this
 * function does almost the same thing and they work closely
 * together.
 *
 * If the seg request has failed but this DTO phase has succeeded,
 * wa_seg_tr_cb() has already failed the segment and moved the
 * status to WA_SEG_ERROR, so this will go through 'case 0' and
 * effectively do nothing.
 */
static void wa_seg_dto_cb(struct urb *urb)
{
	struct wa_seg *seg = urb->context;
	struct wa_xfer *xfer = seg->xfer;
	struct wahc *wa;
	struct device *dev;
	struct wa_rpipe *rpipe;
	unsigned long flags;
	unsigned rpipe_ready = 0;
	int data_send_done = 1, release_dto = 0, holding_dto = 0;
	u8 done = 0;
	int result;

	/* free the sg if it was used. */
	kfree(urb->sg);
	urb->sg = NULL;

	spin_lock_irqsave(&xfer->lock, flags);
	wa = xfer->wa;
	dev = &wa->usb_iface->dev;
	if (usb_pipeisoc(xfer->urb->pipe)) {
		/* Alereon HWA sends all isoc frames in a single transfer. */
		if (wa->quirks & WUSB_QUIRK_ALEREON_HWA_CONCAT_ISOC)
			seg->isoc_frame_index += seg->isoc_frame_count;
		else
			seg->isoc_frame_index += 1;
		if (seg->isoc_frame_index < seg->isoc_frame_count) {
			data_send_done = 0;
			holding_dto = 1; /* checked in error cases. */
			/*
			 * if this is the last isoc frame of the segment, we
			 * can release DTO after sending this frame.
			 */
			if ((seg->isoc_frame_index + 1) >=
				seg->isoc_frame_count)
				release_dto = 1;
		}
		dev_dbg(dev, "xfer 0x%08X#%u: isoc frame = %d, holding_dto = %d, release_dto = %d.\n",
			wa_xfer_id(xfer), seg->index, seg->isoc_frame_index,
			holding_dto, release_dto);
	}
	spin_unlock_irqrestore(&xfer->lock, flags);

	switch (urb->status) {
	case 0:
		spin_lock_irqsave(&xfer->lock, flags);
		seg->result += urb->actual_length;
		if (data_send_done) {
			dev_dbg(dev, "xfer 0x%08X#%u: data out done (%zu bytes)\n",
				wa_xfer_id(xfer), seg->index, seg->result);
			if (seg->status < WA_SEG_PENDING)
				seg->status = WA_SEG_PENDING;
		} else {
			/* should only hit this for isoc xfers. */
			/*
			 * Populate the dto URB with the next isoc frame buffer,
			 * send the URB and release DTO if we no longer need it.
			 */
			 __wa_populate_dto_urb_isoc(xfer, seg,
				seg->isoc_frame_offset + seg->isoc_frame_index);

			/* resubmit the URB with the next isoc frame. */
			/* take a ref on resubmit. */
			wa_xfer_get(xfer);
			result = usb_submit_urb(seg->dto_urb, GFP_ATOMIC);
			if (result < 0) {
				dev_err(dev, "xfer 0x%08X#%u: DTO submit failed: %d\n",
				       wa_xfer_id(xfer), seg->index, result);
				spin_unlock_irqrestore(&xfer->lock, flags);
				goto error_dto_submit;
			}
		}
		spin_unlock_irqrestore(&xfer->lock, flags);
		if (release_dto) {
			__wa_dto_put(wa);
			wa_check_for_delayed_rpipes(wa);
		}
		break;
	case -ECONNRESET:	/* URB unlinked; no need to do anything */
	case -ENOENT:		/* as it was done by the who unlinked us */
		if (holding_dto) {
			__wa_dto_put(wa);
			wa_check_for_delayed_rpipes(wa);
		}
		break;
	default:		/* Other errors ... */
		dev_err(dev, "xfer 0x%08X#%u: data out error %d\n",
			wa_xfer_id(xfer), seg->index, urb->status);
		goto error_default;
	}

	/* taken when this URB was submitted. */
	wa_xfer_put(xfer);
	return;

error_dto_submit:
	/* taken on resubmit attempt. */
	wa_xfer_put(xfer);
error_default:
	spin_lock_irqsave(&xfer->lock, flags);
	rpipe = xfer->ep->hcpriv;
	if (edc_inc(&wa->nep_edc, EDC_MAX_ERRORS,
		    EDC_ERROR_TIMEFRAME)){
		dev_err(dev, "DTO: URB max acceptable errors exceeded, resetting device\n");
		wa_reset_all(wa);
	}
	if (seg->status != WA_SEG_ERROR) {
		seg->result = urb->status;
		__wa_xfer_abort(xfer);
		rpipe_ready = rpipe_avail_inc(rpipe);
		done = __wa_xfer_mark_seg_as_done(xfer, seg, WA_SEG_ERROR);
	}
	spin_unlock_irqrestore(&xfer->lock, flags);
	if (holding_dto) {
		__wa_dto_put(wa);
		wa_check_for_delayed_rpipes(wa);
	}
	if (done)
		wa_xfer_completion(xfer);
	if (rpipe_ready)
		wa_xfer_delayed_run(rpipe);
	/* taken when this URB was submitted. */
	wa_xfer_put(xfer);
}

/*
 * Callback for the isoc packet descriptor phase of the segment request
 *
 * Check wa_seg_tr_cb(); most comments also apply here because this
 * function does almost the same thing and they work closely
 * together.
 *
 * If the seg request has failed but this phase has succeeded,
 * wa_seg_tr_cb() has already failed the segment and moved the
 * status to WA_SEG_ERROR, so this will go through 'case 0' and
 * effectively do nothing.
 */
static void wa_seg_iso_pack_desc_cb(struct urb *urb)
{
	struct wa_seg *seg = urb->context;
	struct wa_xfer *xfer = seg->xfer;
	struct wahc *wa;
	struct device *dev;
	struct wa_rpipe *rpipe;
	unsigned long flags;
	unsigned rpipe_ready = 0;
	u8 done = 0;

	switch (urb->status) {
	case 0:
		spin_lock_irqsave(&xfer->lock, flags);
		wa = xfer->wa;
		dev = &wa->usb_iface->dev;
		dev_dbg(dev, "iso xfer %08X#%u: packet descriptor done\n",
			wa_xfer_id(xfer), seg->index);
		if (xfer->is_inbound && seg->status < WA_SEG_PENDING)
			seg->status = WA_SEG_PENDING;
		spin_unlock_irqrestore(&xfer->lock, flags);
		break;
	case -ECONNRESET:	/* URB unlinked; no need to do anything */
	case -ENOENT:		/* as it was done by the who unlinked us */
		break;
	default:		/* Other errors ... */
		spin_lock_irqsave(&xfer->lock, flags);
		wa = xfer->wa;
		dev = &wa->usb_iface->dev;
		rpipe = xfer->ep->hcpriv;
		pr_err_ratelimited("iso xfer %08X#%u: packet descriptor error %d\n",
				wa_xfer_id(xfer), seg->index, urb->status);
		if (edc_inc(&wa->nep_edc, EDC_MAX_ERRORS,
			    EDC_ERROR_TIMEFRAME)){
			dev_err(dev, "iso xfer: URB max acceptable errors exceeded, resetting device\n");
			wa_reset_all(wa);
		}
		if (seg->status != WA_SEG_ERROR) {
			usb_unlink_urb(seg->dto_urb);
			seg->result = urb->status;
			__wa_xfer_abort(xfer);
			rpipe_ready = rpipe_avail_inc(rpipe);
			done = __wa_xfer_mark_seg_as_done(xfer, seg,
					WA_SEG_ERROR);
		}
		spin_unlock_irqrestore(&xfer->lock, flags);
		if (done)
			wa_xfer_completion(xfer);
		if (rpipe_ready)
			wa_xfer_delayed_run(rpipe);
	}
	/* taken when this URB was submitted. */
	wa_xfer_put(xfer);
}

/*
 * Callback for the segment request
 *
 * If successful transition state (unless already transitioned or
 * outbound transfer); otherwise, take a note of the error, mark this
 * segment done and try completion.
 *
 * Note we don't access until we are sure that the transfer hasn't
 * been cancelled (ECONNRESET, ENOENT), which could mean that
 * seg->xfer could be already gone.
 *
 * We have to check before setting the status to WA_SEG_PENDING
 * because sometimes the xfer result callback arrives before this
 * callback (geeeeeeze), so it might happen that we are already in
 * another state. As well, we don't set it if the transfer is not inbound,
 * as in that case, wa_seg_dto_cb will do it when the OUT data phase
 * finishes.
 */
static void wa_seg_tr_cb(struct urb *urb)
{
	struct wa_seg *seg = urb->context;
	struct wa_xfer *xfer = seg->xfer;
	struct wahc *wa;
	struct device *dev;
	struct wa_rpipe *rpipe;
	unsigned long flags;
	unsigned rpipe_ready;
	u8 done = 0;

	switch (urb->status) {
	case 0:
		spin_lock_irqsave(&xfer->lock, flags);
		wa = xfer->wa;
		dev = &wa->usb_iface->dev;
		dev_dbg(dev, "xfer %p ID 0x%08X#%u: request done\n",
			xfer, wa_xfer_id(xfer), seg->index);
		if (xfer->is_inbound &&
			seg->status < WA_SEG_PENDING &&
			!(usb_pipeisoc(xfer->urb->pipe)))
			seg->status = WA_SEG_PENDING;
		spin_unlock_irqrestore(&xfer->lock, flags);
		break;
	case -ECONNRESET:	/* URB unlinked; no need to do anything */
	case -ENOENT:		/* as it was done by the who unlinked us */
		break;
	default:		/* Other errors ... */
		spin_lock_irqsave(&xfer->lock, flags);
		wa = xfer->wa;
		dev = &wa->usb_iface->dev;
		rpipe = xfer->ep->hcpriv;
		if (printk_ratelimit())
			dev_err(dev, "xfer %p ID 0x%08X#%u: request error %d\n",
				xfer, wa_xfer_id(xfer), seg->index,
				urb->status);
		if (edc_inc(&wa->nep_edc, EDC_MAX_ERRORS,
			    EDC_ERROR_TIMEFRAME)){
			dev_err(dev, "DTO: URB max acceptable errors "
				"exceeded, resetting device\n");
			wa_reset_all(wa);
		}
		usb_unlink_urb(seg->isoc_pack_desc_urb);
		usb_unlink_urb(seg->dto_urb);
		seg->result = urb->status;
		__wa_xfer_abort(xfer);
		rpipe_ready = rpipe_avail_inc(rpipe);
		done = __wa_xfer_mark_seg_as_done(xfer, seg, WA_SEG_ERROR);
		spin_unlock_irqrestore(&xfer->lock, flags);
		if (done)
			wa_xfer_completion(xfer);
		if (rpipe_ready)
			wa_xfer_delayed_run(rpipe);
	}
	/* taken when this URB was submitted. */
	wa_xfer_put(xfer);
}

/*
 * Allocate an SG list to store bytes_to_transfer bytes and copy the
 * subset of the in_sg that matches the buffer subset
 * we are about to transfer.
 */
static struct scatterlist *wa_xfer_create_subset_sg(struct scatterlist *in_sg,
	const unsigned int bytes_transferred,
	const unsigned int bytes_to_transfer, int *out_num_sgs)
{
	struct scatterlist *out_sg;
	unsigned int bytes_processed = 0, offset_into_current_page_data = 0,
		nents;
	struct scatterlist *current_xfer_sg = in_sg;
	struct scatterlist *current_seg_sg, *last_seg_sg;

	/* skip previously transferred pages. */
	while ((current_xfer_sg) &&
			(bytes_processed < bytes_transferred)) {
		bytes_processed += current_xfer_sg->length;

		/* advance the sg if current segment starts on or past the
			next page. */
		if (bytes_processed <= bytes_transferred)
			current_xfer_sg = sg_next(current_xfer_sg);
	}

	/* the data for the current segment starts in current_xfer_sg.
		calculate the offset. */
	if (bytes_processed > bytes_transferred) {
		offset_into_current_page_data = current_xfer_sg->length -
			(bytes_processed - bytes_transferred);
	}

	/* calculate the number of pages needed by this segment. */
	nents = DIV_ROUND_UP((bytes_to_transfer +
		offset_into_current_page_data +
		current_xfer_sg->offset),
		PAGE_SIZE);

	out_sg = kmalloc((sizeof(struct scatterlist) * nents), GFP_ATOMIC);
	if (out_sg) {
		sg_init_table(out_sg, nents);

		/* copy the portion of the incoming SG that correlates to the
		 * data to be transferred by this segment to the segment SG. */
		last_seg_sg = current_seg_sg = out_sg;
		bytes_processed = 0;

		/* reset nents and calculate the actual number of sg entries
			needed. */
		nents = 0;
		while ((bytes_processed < bytes_to_transfer) &&
				current_seg_sg && current_xfer_sg) {
			unsigned int page_len = min((current_xfer_sg->length -
				offset_into_current_page_data),
				(bytes_to_transfer - bytes_processed));

			sg_set_page(current_seg_sg, sg_page(current_xfer_sg),
				page_len,
				current_xfer_sg->offset +
				offset_into_current_page_data);

			bytes_processed += page_len;

			last_seg_sg = current_seg_sg;
			current_seg_sg = sg_next(current_seg_sg);
			current_xfer_sg = sg_next(current_xfer_sg);

			/* only the first page may require additional offset. */
			offset_into_current_page_data = 0;
			nents++;
		}

		/* update num_sgs and terminate the list since we may have
		 *  concatenated pages. */
		sg_mark_end(last_seg_sg);
		*out_num_sgs = nents;
	}

	return out_sg;
}

/*
 * Populate DMA buffer info for the isoc dto VICE releask;
	case -ECONNRESET:A to be transferres on or past the
			next page. */
		if (by
layed_rpfer);
	MOfer->is_dma = urb->trans;ge. */
		if (by
as used. */e. */
		if (by
		sg_mark_0ready)ulate Deturn oua,
				 oc lug("%s: ngth[frame_indt urb * */
		if (by
layed_rpfpe & UA_SEG_PENDIlayed_rpfpe &e_leA_SEG_PENDIngth[frame_indest the
			nextlength) ready)an e_pipeisoc(xfer->urb	 * the iframe_inb->pipe))ser->lock,ompletion(ses in a single transfer. */
		if (wa->quirks & WUS* */
		if (by
layed_rpfgger than %d\ AT_ISOC)
			dr;
	xf_index * */
		if (by
layed_rpfgger than %d\ Ar %p ID G_PENDIngth[frame_indest the
			nextleytes_prom_sgs = nents;
	}
turn oupt/* Filan max
	return ou}

er_put(x
}

/*
 ulate DMA buffer inf a in traisoc dto VICE re;
	case -ECONNRESET:A to be transferres on o enum wturfer_ruct wa_x enum wturfer_r	int isoc_fraEINVAL;
			g
r, wa_xfer_id(pe 	}
	if (seg		if (by
layed_rpfpe & r %p ID G_PENDIlayed_rpfpe &ewturfer_ruct wa;	if (seg		if (by
layed_rpfer);
	MOfer->is_dma = urb->trans;gee. */
		if (by
as used. */ee. */
		if (by
		sg_mark_0rea			seg->staber_oeturn ou}

er_into_cuIPE from t (seg		if (by
layed_rpfer);
	& r %p~er->is_dma = urb->trans;gee_desc[in= WA_SEalwaymes  0es the xa isoc frame. */
. */
		if (by
		sgmcalldg_mark_0refer, wa_xfer_ (by
layed_rpfgger tr (cnt = */
		if (by
layed_rpfgger t& r %pp ID G_PENDIlayed_rpfgger t&e_len,turfer_ruct wa;	ife. */
		if (by
as used. */eee. */
		if (by
		sg_mark_0rea				seg->stat= */
		if (by
layed_rpfgger t& sed. */
	 only hit ta. */
	wa_xfer_put(xfer);
}

(urb->trage_day hit ta to store bs_to_transfer b ID G_PENDIs and cy hit tpy the
 * subset of the ig that matches ty hit txferrelease DTO . */
		if (by
as user.
 */
static struct scar %pp ID G_PENDIs__wa_xfturfer_ruct wa_xturfer_r	int_wa_xf&(. */
		if (by
		sg_ma)submit_urb!
				if (seg->dto_x + 1) >ort(struct wa_xfelist_del */
		if (by
layed_rpfgger than %d\ ATturfer_r	int;
out;
		}
	}
	xfer->result d. */
	wa be trabmitrs a transwa_xfer_g>xfeansfer mere is an edev_err(dget_b the to etion(xferup_seg)that case,ONNREt up a ti is er every zero;urb-st x sng the s>xfeatransfer[in
	retucause thilifbytespe, ina
 * suisocby
 *tf *_xfers,pute the	/* rsg_b the tonlink1()* taken )/*
 * Seruct wa_duisocby
 (twut(max eh?)MA buffer inf a in tra	currey that wa;
	case -ECONNRESET:A 	 enum wa_xfer_type xfer_t_fraEINVAL %d\n",e_dto_urb_isoc(xfefer);
	enum wa. */oc frame.acket_d ID G_cpriv;x + -, xfer->urb-G_cpriv; = xfer->s*packet_desc =
			(fer *xfer,
	g->xfer;			/* al_irqsave(&usb_dev,
;nst unsier *xfer,
	
 * stat_seg *seg) {
e(xfer-l_irqsave(&use(xfer-;to be transferres on);
	enum wturfer__xturfan maxturfer_r	int;
out;ort(struct wa_xfeurb-G_cprsg->cffset)urb-G_cprs,, xfer->urb-G_cpriv;(sizeof(struct scatteurb-G_cprsgc(sizeof(*b), GFP_ATOcprs_kzC);
	if (urfer_efer);
turfan m& UA_SEG_PENDIlayed_rpfgger than %d\;	goto out;
	urb->actual_length = 0;
	for (cnt enum wdo nokt_seg *et, int * (donex < ur>isoc_frame_indet_sg;
< ur>isoct, int * (
	int indexAdju becausec_frame be transferobjec(xferruct wahe headoto
uired because err_ratelimited("igger tsaction per fr;
	dev = &wa->usb_iface->dev;
	if (	< ur>isoc_frame_indet_ + 1)ize; /* don't care. */
			index += __wa_seg_calc_count(struct wa_x&< ur>isoct, it_xfer_do nokt_seg *et, integ_caype = WA_XFER_TYPE_ISOfer_hwaiso, hdr);
)&e_len,(< ur>isoc_frame_indet*, xfer->_cketL__, le32_t = 0; cnt < xfer->sesg->offset)a. */oc fra+wdo nokt_seg *et, iOUND_UP(ult = usb_submice\n");sgc(sizeof(**b), GFP_ATOcprOMIC);
	if 	tively d thenc_packe ENOENT), ; cnt acke ENOEt;
		++fcnt);
	er->wa);

	usb_initsdto_urb);
, b_dev,
		usb_  _urb(&b->urb, xfb_dev,
		usb_b_  e(xfer->wa->usb_dev,
				xfer-b_  *seg = xfer->s,cket_desc =
		fer-b_  phase
 * finisoc_packeturfer_r	intxfer_sgturfan maxcnt < xfect, it_xferr fr;
	dev = &wa->usb_iface->dev;
	if (	< ug->isoc_frame_inde AT_ISr>isoc_frame_inde;f (	< ug->isoc_framesoc(xfefe offset_index = frame	< ug->isocc frame. *_)
			dr;
	xfee_deRRORc_frame_count);
		/* pop	< ug->isoca);
		}
		usbnteg_ca	er->a. */o_ini0esult = usb_submit_urb< ug->isoca);
		}
		usbntc(sizeof(***b), GFP_ATOdo nothing.
 *C);
	if 	only hit tT subset of_xfer_put(xfer);
}

/*
 * Callb* the y hit tasegme
		/* populate the i}
}


/*set_iny hit ttransferobjec(xe the cgger tsactase DTO er->wa);

	usb_inieg_cay ug->isoca);
		}
		usb, b_dev,
		usb__urb(&b->urb, xfb_dev,
		usb_be(xfer->wa->usb_dev,
				xfer-b_ket_info(*seg = xfer->s)&e_len,	ket_desc =
		fer-b_do nokt_seg *et, iOUND_Utively do nothing.
 */
isoc_pacxfee_deadju bealculate th*
			 c(xfe_xfet segse of allalc_count(struct waNCAT_ISr>isoc_frame_inde;f (}efer, wa_xfer_REON_HWA_CONCAT_to_turfan m&
			if (fo/e (unless auest headTO . */
		if (b */
			a. */o_ini0esult = usb_submit_urb< ug-		if (b *c(sizeof(***b), GFP_ATO		ifC);
	if 	oer->wa);

	usb_inieg_cay ug-);
			resb_dev,
		usb__urb(&b->urb, xfb_dev,
		usb_bbe(xfer->wa->usb_dev,
				xfer-b_size,sg;
s not inboundisoc_pacxfeer fr;
	dev = &wa->usb_iface->dev;
	if (	only hih)
		[offset_indNT), turn out_sgeturout__xfer_py hih)
	(curre	/* resubmit Struc the ) {
		/* Alfer r	 hit ttransferermine hwa);e = 0; Filahe ) {aitingr	 hit t
			ck, flags) r(uni{
		f thhe actr	 hit / + 1)ize; no longer need it.
			 */
			 __wa_po	< ug->isoc_framesoc(xfsubmitEG_ERROR)) {
 = 0[offset_indNT), turn out_sgeturout headTO ut;ort(strn traisoc dto VICE re; */
			 __wa_po	keturfer_axturfer_r	intnlock_i_submit_urb(&b->ur(**b), GFP_ATOcprO(unless aisoc dto wa_seg(urfer_e+ATturfer_r	int;
_seg(urfan m&-ATturfer_r	int;
_segs);
				sb_pipeisoc(xfer->urREADYeady)
t;
		}
0->context;
F;
	u8 doe the ccurrent_xfer_sg);
	}

	/en canded,
 * oanswa a Tr Ue data fac(xfpy thfer[inlefor_dg tomputer(dev, "tT susegs as a
it ttransfeetermine h>ep->/* ap to etion(xferup_se a TranFP_ATOcprO(unless aisoc dto:
b_free_urb(segcnt < xfer->seack_desc_urbFP_ATO		ifC);
	:
b_free_urb(segcnt < xfer->seacall(wa);
		}
		usb_unFP_ATOdo nothing.
 *C);
	:
_free_urb(&seg->r->se_to_cpu(rpiper->sesg-ed. */FP_ATOcprOMIC);
	:
FP_ATOcprs_kzC);
	 out;
		}
	}
	xfer->result d. */
	wurb->p * We urnhow many
		 
error_/* populaere is B donscated at the) doeurn out_r_/put(x
		dev_err(,g>xfeanneg reqaURB is allurof the
 * stt in tAssum:		/* in cnt < xfer	cpu_tnsfer li->wa->merg gonethat wa;)functionases. pculrame b		} s also , noer lllllllle -ECONNREStwg;
}

 = 0sthat cwe which run st x:	/* URt_r_er llllllll * the nneA buffer inf a in tra	currey th;
	case -ECONNRESET:A 	 hes.
 */
static voi0, holding_dpe *pxfer_type)
{
	ssize_t result;
	struct device truct wa_xfer_hdr *xfer_hdr
	urb-ns */
		ap GCC urb *enum wa_xfer_type x %d\n",layed_rpf=
			(fer *xfe(struct wa_xfer *xfer,
xfer *xfer;
out;ort(struest size if ok
 */
stET:A 	&*xfer_hdr usb_submit_urb(&b->urb, GFP_ATOMIf ok
 */
s;
	ket_desc =
		_wa_lding_dpt;ort(struest size if ok
  */
			wa_a_xfer_type xfusb_submit_urb(&b-
	}

	if (xfer->segintk_ra: Fed,
 * oaa. */
	wa_waitv_err(xfer %p: Can't suburb-G_cprs,,     wa_xfeb, GFP_ATOMIf ok
 				iny)
				[offnext(curre
}


/*urb er *xfer,ssize_t recpriv; = xfer->s;
 packet des d the
		if (dtype specific data. *			wa_a_xfer_t0, *xfer_hdr0_a_xfer_type xfus)
				[off
					brea
}


/s*urb er *xfer&seg->xfer_0 scatteurb-e);
		if (xfer->segs =ISO)size);
		e = rpipe->descr.Ln %d\ Ar %p32(struct wa_xfer cpriv; =>isoct, it_x	goto out;
	u1b->actual_length = 0;
	for (cntdr0, struct wa_xfer_hwaiso, hdr);
		struct wa_x;cntdr0, structspe = xfer->ep->hcpri->segs; 	break;
	case WA_XFER_TYPE_ISO:  wa_seer *xfer&se*seg = xfer->seg[ection of ++fcuct wa_xfer_hwaiso *_wa_po	kr_iso =
			container_of(xfer_hd	struct wa_xfe cket_info_hwai->s*packet_desc =
			(f	only hit tCstorvalhe t waiting 0d\ 
}


/. S
	}

	/*mmon rey hit tvalhe tay gonene lowsactase DTO bute =  xfer->s,cket_desc0a_a_xfer_type xfusbseer *xfer= wa_xfer_id_le32(xfecnt);
	eer *xfer= pe->descr.Ln %d\ Ar %p	t. */
		xfer_iso->dwNupe xfusbseer *xof the transfer requewa_po	t. */
		xfer_iso->dwNumOfPackets = cpupulate isoc packet descriptor. */
		__wa_setup_isoc_packe		sb_pipeisoc(xfer->urREADYeaddex, &seg_size)layed_rpf=
		  USB_DIR_IN ? 1 gger than %d\;	g);
		e = rpipe->descr.Ln %d\ A,layed_rpf=
		&
	cnt < xfect, i ?r %p32(struct wa_xfer cprupe xf :r %p32(struct walayed_rpf=
		packelayed_rpf=
		&-AT	cnt < xfect, i_x	goto out;
	u1b->actual_length = 0;
	for (cntder *xfer&se*cnt < xfer->seac xfer->seg[ecbute =  xfer->s,cket_desc0a_a_xfer_type xfusbseer *xfer= wa_xfer_id_le32(xfecnt);
	eer *xfer= pe->descr.Ln %d\ Ar %p	layed_rpf=
		&
	cnt < xfect, i ?r %p%p32(struct wa_xfer cprupe xfr %p%p: 32(struct walayed_rpf=
		packe	cnt < xfer->seacipeisoc(xfer->urREADYeaddelayed_rpf=
		&-AT	cnt < xfect, i_x	gxfer_get(xffer= wa_xfer_id_le32(x|	urx80;e_desc[inrror cases. ;
	}

	/>segs_NVAL;
			gP_ATOMIf ok
 			:
P_ATOMIf ok
 */
s out;
		}
	}
	xfer->resultnsfer l = xferxfecdone rroheld!hat will fit a in transfe 
erro(ct wahc *wa;
	struct dfer_iso =
			con__wa_xfer_slll be transferres on or pa
e(xfrestorvoi0, holding_deady)u the wCONNREnefere			 wfer'kets  oua segme- HWA depe))ser number of
e(xfrest->isoccontext;
Tith the neoto >xfeatransfer*/
sd segsdNT), canwe ddiscallarpletio
it ta. rame be lt callbs run a Trans		/* take a ref on r/* + seg->isoc* populate the isoc se	sb_pipeisoc(xfer->urSUBMITTED;of(b->cmd), __wa_xfer_abortsdto_urb);
, 
	result = usb_submit_urb(&b- (cntdev;
	((xfer);
		%p
			dREQ		dev_err(dev, "xfer 0x%0lllllllning up xfer %p  ENOEt;
		,,     wa_xfen when this URB was eb, GFP_ATOMtOMIC);
	if } r/* + seg->isoct(xfer);
}

/*
 * Callb_supt_sgnrent_xfer_sy ug->isoca);
		}
		usb- (cnt		/* take a ref on reubmit. */
			wa_xfer_get(xfer>isoca);
		}
		usb, ult = usb_submi_iso->dwNumOfPacigned long fl_urb(seg->dto_urb, GFdev;
	((xfer);
		%p
			dISOer);
}

/*
 * Callb*dev_err(dev, "xfer 0x%08lllllllning up xfer %p  ENOEt;
		,,     wa_xfeen when this URB was e*b), GFP_ATOdo nothing.
 *->lock, fl}f } r/* + seg->isocdev_d) doked in erroa
		*ote the isoc se_urb< ug-		if (b the HWA.
	 */
uffer *b e_t resu;cnt		/* take a ref on reubmit. */
			wa_xfer_get(xfer);
			result = usb_submi_urb(seg->dto_urb, GFdev;
	((xfer);
		%p
			d
				dev_err(dev, "xfer 0x%08lllllllning up xfer %p  ENOEt;
		,,     wa_xfeen when this URB was e*b), GFP_ATOxfer->lock, fl
			nt indexIf the
		 * data *_xfers mhe xfe_put_ segments wiv, "xf index*_xo
/*
 ulat   ourcepletion.
  the a. rts will be n O sg_wa_sion of non-_pipeisoe.  Thisaction per fraames in a single transfer. */
		if (wa->quirks & ONCAT_ISOC)
		< ug->isoc_frame_inde > 1_x + 1
e(xfrest->i0eady)
txfer_abort(deer);
		rpipt;
		}
0->c. */
	wa_xfer_put(xce\n");
			wa_reset_all(wa);
		}
		usb_unFP_ATOdo nothing.
 *->lockt(xce\n");
			wa_rtsdto_urb);
_unFP_ATOtOMIC);
	:se	sb_pipeisoc(xfer->urmark_*/e. */
ubmit. */_lding_dp
e(xfrest->isocut;
		}
	}
	xfer->result Execfer_mhe xtheupipens < 0 oransfeetletion make simumte thfer_sg)a. *weer.
  * Mark the given ffer,
   ourceprpipache prevc frame bufdhere is an eugsg_e(xfer/done uc thece/*
 * Seev, "xpad\ i}

	/* caa
 * sked
 * as done ngetursg_n< 0se be tracdone c frwe d  Thv
/saMA buffer inf a in tra	currer);
		if (rct wahc *wa;
	struct dfer pa
e(xfForciblfer_t_fraEINVAL %		ifCche prev_sg;
e(xfrest->i0eade *pxfer_type)
{
	ssiz = xferOMEM;
	struct device *dev = &xferres on);
	A.
	 */
	if (urb->statce *dev;
	struct wa_rdp
e(xfForciblys;
	unsiefault:		/* Other  = xferxfecdoneLL;

	spin_lries
	atomic_xferr  = xferxfes_abortATOM)&
		
8llllll)
	!
	spio_suyr  = xferxfecdg = 
8llllll)
	(		ifCche prev_sock, flagtryke a  = xferOM))	}
	if (sv_ssave(&currr_list_&( = xferxfecdg = ,l be transferrfer-b_ ch_entry(x, flch_ener)rtsdto_ch_entry(x, flNT), ; seg = xfe;			nt indexGet the nn
 *
 a to be xn out_rlinke be lt callbs _xfer_py h ags);E.
 * Delayebyin transfe 
error_dto_suxferruaining
uired becNT), t the xfer nts also aruainingll be needed	/* take a ref on reubmit. */n transfe 
erro(uct dfe */
			 __ &e(xfresto;gee_deame buff/*
 ulat   ourcepked in eRPIPE i}
rest-me_inrame. */
_irqr(xfrestor	>lock, flags);
 = xferOM);
->wa;
		dev = &wa->usb_ifact())
		.
 * Delaye waiter);
		 [_waitv_err( abortATOM]fer %p: Can't sub%u: request error %d\n",
				xferatomic_xferr  = xferxfes_abortATOM),,     wa_xfer fr;);
kelyb(seg->dto_uile (index resttes_to__done(xfer, seg, WA_SEG = xferxfecdoneLL;

	spin__func__, xfer, wa_xfer_id(xfer));
			spin_lo;
			seg->result = urb->stnly hit tT e
		 *prpip
		  caa
 
 * Delayehat c anythis);/*
y hit t/*
 RPIPE xfecdg =.  Merwiix restsactase DTO l_length =frestnto_cu seg_index,
				WA_SEG_ABORTED);
			done = __wa_xfer_is_done(xfer);
			spin_unlock_irqrestore(&xfer->lock, flags);
			if (doneiefault:		/* Other  = xferxfecdoneLL;

	spin_l
			n when this URB was }context;
Merwise,  RPIPE thiForciblyirqulatythiwe dCche preocaterthat ext;
er);
		 se
			nennohe	/ivoc* populan of with socup  dtor a Transfer
!		ifCche prev)
	!
	spio_suyr  = xferxfecdg = 
8C)
		atomic_xferr  = xferxfes_abortATOM)&=Ar %pwMaxPacketSize);
	xfer->sength = s&
			!
e(xfForciblys;1	unsiefaue(xfer, seg, WA_SEG = xferxfecdoneLL;

	spincut;
		}
r(xfrestoto error;
	}
errtra	currer);
		if (rct wahc *wa;
	struct dfer_t_frae(xfForcibl;_t_frae(xfseg_index,
				WAer);
		if (rpipe__ &e(xfForciblfoccontext;
Ied in eRPIPE i}
Forcibly*
 * Seer,
   ource,ua,
iix  to be xfelsize and makForciblydg =.e andOitioned oriven ffWAfer,
   ourceprpipache prevc frame bufd tonminatex,
				WAer);
		if (,ht happenRPIPE gs and tr_dto_su
 * oaache pr
it t
			c frded,
 *dur
 *
 *e doibmit phase /*
 ur);
		 put(xc frinto_cu
it tanykForc/* sh Slcul searchate the ndex);

		RPIPE i
			 a Transfer
e(xfForciblfe		n wa,

		__wa_dto_pu
 = xferOM, );
		rpipseg_s_irqr(xfrestor	>lding_dto) {
		__wa_dto_put( = xferOM);
segment_size * as done rrora_xfnt_sizeOnrded,u
 * N* Delate .
 ju bealop* N* Delate c fram
		}
S_MAX);iled bd itentheup_n )/erminexecfer__index] flags) pad\A buffer inf a in tra	curre
erro(ct wahc *wif (urb->sfer_t_fraEINVAL %		ifCche prev_sg;
e(xfrest->i0, e(xfForciblys;
	uHWA.
	 */
uffer *b e_t resu;cne *pxfer_type)
{
	ssizOMEM;
	struct device ce *dev;
cnt);
*dev = &xferres on);
ce *dev;
	struct wa_rpype,
				 size_t xfer_hdr_size)
{
	struct wa_dev = &xfens < 0oc(xwMaxPacketSize);
	xfer->sength = s&; longabortATOM; longo_suy	unsiefault:		/* Other OMEM	curr
	spidoneLL;

	spin_
	spia,

xfelne(xfer);h_entry(,  OMEM	curr
	sppin_iefaue(xfer, seg, WA_SEGOMEM	curr
	spidoneLL;

	spin
xfer_ON	atomic_xferr  = xferxfes_abortATOM)&
	&xfens < 0o);egs_NVAL;
			geiefault:		/* Other  = xferxfecdoneLL;

	spin_oto out;
	urb->actual_length = 0;
	for (cnt_frae	__w_ (sv_s1	uns	abortATOMv_satomic_xferr  = xferxfes_abortATOM)in_lo_suyv_ssave(o_suyr  = xferxfecdg = ;
_t = 0; cnt < xfer->se_xfer frabortATOMv)
	o_suy	if (fo/ey hit tO sg_wdto_suxferCche prfer,
			 * nd tr_aitv_erry hit t/		 * xsactase DTO 		ifCche prev_sock, flagtryke a  = xferOM)lock_irqre	ifCche preurb, GFP_A__w_ (sv_s			offeubmit. */n transfe 
erro(uct dfe */
			 __wa_po	k	&e(xfresto;gee->wa;
		dev = &wa->usb_iface->dev;
		dabortATOMv		.o_suyv		.
 * Delayr %p ID 0n't sub%u: request error d\n",abortATOMp ID 0no_suy	lock_i_subr(xfrestor	>l>lock, flags);
 = xferOM);
ock_i_sub(seg->dto_urb, GFPo;
			seg->result = urb->st**b), GFP_ATOcprO->lock, flal
			egs);
	*/
_irqrA__w_ (sactual_length;
		if (data_b_iface->dev;
		dabortATOMv		.o_suyv		.		__wa_r %p ID 0x%08X#%u: request error d\n",abortATOMp  o_suy	lock_	sb_pipeisoc(xfer->urDELAYED;of	_
	spia,

xfelnesdto_ch_entry(,   = xferxfecdg = ;
_t}
O l_length =f
 * Delaynto_c}
FP_ATOcprO->lock:context;
Merwise,  RPIPE thiForciblyirqulatythiwe dCche preocaterthat ext;
er);
		 se
			nennohe	/ivoc* populan of with socup  dtor a Transfer
!		ifCche prev)
	!
	spio_suyr  = xferxfecdg = 
8C)
		atomic_xferr  = xferxfes_abortATOM)&=Ar %pwMaxPacketSize);
	xfer->sength = s&
			!e(xfForciblys;1	usiefaue(xfer, seg, WA_SEG = xferxfecdoneLL;

	spincufer
e(xfForciblfe		n wa,

		__wa_dto_pu
 = xferOM, );
		rpipseg_s_irqr(xfrestor	>lding_dto) {
		__wa_dto_put( = xferOM);
cut;
		}
	}
	xfer->result Seco friculrameags);/* populatentheup_erryt_sizeAssuto WA_,  ex]e t waitd bd itentheup )/[gs hat  to WA;iled bd itentheup_f (r)]. A * If thb_de:nt_size * as wa	wa);e =c framfkets  dsize * as ep	wa);e =me_inxfer_hamfkets  ds_ier llllllllllllller);
		 NCATsize * as */
s	wa);e =c framfkets  d (sc[inrror calinkehat clt c dsizeeeeeeeeeeeeee waitd bd itentheup )/ascwe wh].ofwait
			wa_xfer_get)sizeeeeeeeeeeeeeec frhat clt c d to etid itentheup_f (r),/ascwe tooe c sizeeeeeeeeeeeeee

	rthe neer epayebyinf (r)tasegmeweram
		})MA be * as gfp	wa);e  * Callbacwerded,r_dgn tra	curre
erro()ocaten .
 ju be be al			 * ay gdnneA ba tranf r_ofweraun__index] flags) pnto_durmitHb-st x s			 * ay gwe A bayex restlready ough 'caec frhai(x
}

/*
 ex] flags) hc fllan fwai
and mak
 * Delayes);E.}

the ndex) * as(seg->dpad\ of ki al	n.lbac * aer l seg->dr_hwaiki a/* A,d becNT), erminoibm	*otthe ndex)isoccry(R, so thdetheup )/ermine h>t c dMA buffer inf a id bd itentheup_n ct wahc *wif (urb->sfer_t_fraEINVAL);
ce *dev;
	struct wa_rpype,
		->dev;
	size_t maxpktsize;
	struuffer *b e_t resu;cne *pxfew
		uffer
		uffuserres
		;cne *pxfew
		/* al*w
		/* a);
ce *dev;
resttes_ubmit. */_xfer_e a_by_ep(w: 0;
		meep",
		,e * as gfp usb_submit_urb(&b- (cntdev;
	((xferFP_ATO_xfer_e ar %plning up was eb, GFP_ATOM_xfer_e aeady)
t;ort(struct DEVn r/* i->wa->itv_errurout_bro_xfe--ikills DWAfransmferxcdoneEGO
		uf->mferx);ee_deget thle t * al*ansfer
SB_DI{
	ssc(sizeo (cntmferxce(xferEGO
		uf->mferx);cntdev;
	((xferFP_ATt
		 * algv, "xfelning up was eb, GFP_ATOMlenggesttedy)
w
		/* al*/n t
		/* a_e a_by_
		/* a(O
		uf",
				* a usb_subw
		/* al*c(sizeo (cntmferxce(xferEGO
		uf->mferx);cnt	if (xfe&
SB_DI{
			* a f (xferFP_ATtw
		 * algv, "xfen_lo;
ng up was eb, GFP_ATOMlenggesttedy)
mferxce(xferEGO
		uf->mferx);c
	wa_xfer_put(xfer);
error_default:
	spin_e_t res
		/* al*/w
		/* a);
b_unlink_urb(seg->dto_ufer
SB_DI		wa_resetsizePROGRESS-
	}

	if (xfe&
SB_DI{
			* a f (xferFP_AT_detheupd"xfelning up was eb, GFP_ATOMletheupd;st_seg_sort(struest size if ok*			wa_sc_urb)_submit_urb(&b-
	}

	if (xfe&
SB_DI{
			* a f (xferFP_AT_size if ok"xfelning up was eb, GFP_ATOMsize if okas }context;
Get thxulate nn
 *
 aate nun tra	curre
erro/* the dasync tonouu
it toperuroutsand copyyrror,ferruainingd becNT), t the xfer nts also 
it texits a Trans		/* take a ref on r_sort(struest size i
erro(ref on r_submit_urb(&b-
	}

	if (xfe&
SB_DI{
			* a f (xferFP_AT_size i
erro"xfelning up was eb, GFP_ATOMsize iC);
	if } rdone = __wa_xfer_is_done(xfer);
			spin_unlocn when this URB was submit
0->context;
sc[inrrobasicursg_er->lock, flags);
	)_bro_xfeup 		/* takeivoallb()ext;
eply h n when this )ack (germincurs etion(xferup_seg)tc frundo
it ttr ok*) a TranFP_ATOsize if ok:
P_ATOMletheupd: rdone = __wa_xfer_is_done(xfer);
			spin_unloc/* i->wa->itv_errurout_bro_xf,ikills DWAfrans_subw
		/* aor	>l
		/* a_s);
	
		/* aobmit attemnggest:)
txfer_his URB e)
{
	structobmit att_xfer_e a:n_e_t reubmit. */_lding_dpt;
		}
	}
	xfer
P_ATOMsize iC);
	:n_seg_index,
				WA_SEG_ABORTED);
	e_t reubmit. */_lding_dpdone(xfer, seg, WA_SEG_ERROR);
	}
	spin_unlock_irck_for_delayed_rpipes(wa);
	}
	if (n when this URB was _deam
		}
k for taate nu/*
 ex] flags) r(uni{
germinrun />segs_bmit
0->->result Execfer_/*
 ur);
		 * populan set_indW prfAdapegme@wa * seg->xfe -ECONNe h>te nulg_tr_,/ascdetheup )/which coult c d set_inyt tpidfll "tT at'sthayready oated at thh 'caeundgme
		;iled EM	curr
	spidone.lbacdetheup )/jump/* A,di(x
curredonese * as doneA ba traaten  be as

		/* upd--ick wscwe which coulche p* URt_rinv
/s	;ileord08X#%e has  /*
 ur);
		 put(xferC>iticu
	waput(xlries
donee =c fratenytes_toeg->isom=me_iches 		/* upddone heldMA buf}
errtrad itentheup_f (re *pxfewol_in *pxfe*wsc void wa_seg_uffer *b cuct wa_xfer_ws,l be transh_setup_tentheup_wol_));
	A.
	 */
	if (urb->s, *n)
{
	struct w->dev;
	
	sLIST_HEAD(tmpcdg = ;
s _deCtaticrC> storame be d EM	curr		__wa_dput(xlries
qrestors 		/*one c se	a_xfer_put(xEGOMEM	curr
	spidonepin_
	spicfe_poansiti(&tmpcdg =,  OMEM	curr		__wa_dput( %08X#%EM	curr		__wa_dput(.nt_spin_iefaue(xfer, seEGOMEM	curr
	spidonepincontext;
entheuptthe ndempaput(xle_iches* upddone helddate num bd itentheup_n
st isoc 
 * o * as done a are a a adone mferxes a Transsave(&TOM>xfer_list_saf
		done n)
{,  tmpcdg =, ch_entry(x
	}

ch_ener) d thee(xfer);h_entry();
ock;
	size_t maxpktsic framebd itentheup_n URB wb(&b->ur(		/* takeivoallb( = urb->sk1()* taken sc_ur
			wa_xfer_deltheu* URB un}>->EXPORT_SYMBOL_GPLamebd itentheup_f ();
ogs = nento_cu * Seev, "		 * populan oet_indW prfAdapegmechesidframeis  oruptMA buf}
errtrainto_cu_ev, "		o_currents_f (re *pxfewol_in *pxfe*wsc void wa_seg_uffer *b cuct wa_xfer_ws,l be transh_setup_te_ATOMwol_));
	A.
	 */
	if (urb->s, *n)
{
	sLIST_HEAD(tmpcdg = ;
s deviso,((xferRun ur);
		 STALL_into_cuIPE "xfelning up wass _deCtaticrC> storame be d EM	currev, "		oput(xlries
qrestors 		/*one c se	a_xfer_put(xEGOMEM	curr
	spidonepin_
	spicfe_poansiti(&tmpcdg =,  OMEM	currev, "		oput( %08X#%EM	currev, "		oput(.nt_spin_iefaue(xfer, seEGOMEM	curr
	spidonepincontext;
aun__xfer_>ep-r_fatiurein a lug("%s: dempaput(xle_iches* upddoneext;
heldMA Transsave(&TOM>xfer_list_saf
		done n)
{,  tmpcdg =, ch_entry(x
	}

er *xfer,
	horrr_lusb_de *epb->ske *dev;
	struct wa_rpuct wahc *wa;
	struct dev
_func__, xfer, wa_xfer_id(xfer));
			spin_lephdr_size)
{n reufer_hdra->usb_iface->
			holding_dto, release_dto);
	}
	spin_unlock
		sgep-r RPIPE fatiure n a lug(le_ichesqrestorsa done.le DTO_xfer_>ep-r_fatiurein a lug(w: 0epnlock
		sguainingd becNT),.tT e
	rehas do anthe ndex)tmpaput(. needed	/* takpipes(wa);
	}
	if ck
		sgbe al
}

wol_. needed	/* taker);
		if (rpipe_ready)->EXPORT_SYMBOL_GPLamebinto_cu_ev, "		o_currents_f ();
ogs = nS
error_/* popula  to be W prfAdapegmet_r_/ur);
		 wayere is an einto_curameentheu* URt_vols dopoasiTOMvsleepa;)f[se	;ileentheup_n ),x
}

/*
 _xfer_e a;)functionamferxcdoneE)]. I		 * ay ;ile 0; Fsatomic>italso , eadyefgme
		/entheup_n )/lt c--seg_swoult c d prcthere is @d i:->xfown the nn
 *
 a toix rest/
	case HCI Linux)isocn ase /*RESET,%u: DTOrmine h
	retuap to lt c'caeu,
	hcdkeivoallbaken )/*r tonlinnnnnnns_bmit'caeFP_ATtthe nder nts also a->eFPgoready in
 er coulnlinnnnnnns_fkets nd re nee a id bd itentheup(d wa_seg_uffer , er *xfer,
	horrr_lusb_de *ep0x%0llltruct w->dev;
	, gfp_t gfpc voi0, holding_dpe *pxfer_type)
{
	ssizOMEM;
	struct device *dev = &xfif (urb->s);
ce *dev;
	strumy_ct wa_rpipe *rpipsoct_sleepefe , w_discblug() |ructatomic(pincufer
( (by
layed_rpfgger t& c(sizeof(llll)
	( (by
as uc(sizeof(llll)
	!( (by
layed_rpfer);
	&fer->is_dma = urb->transof(llll)
	 (by
layed_rpfgger than %d\ !=&b-
	}

	if (xfer->segBUG?w->dera: sizedNT), turn ou& NO->t"xfelsc_urb);dump_n ase();st_segiefault:		/* Other OMEM	curr
	spidoneLLmy_ct waon r_sort(stru,
	hcdk;
			wa__(xfer(&(erres
		EM;
	shcd)elsc_urb)iefaue(xfer, seg, WA_SEGOMEM	curr
	spidoneLLmy_ct waon r_submit_urb(&b->urb, GFP_ATOM;
			wa_;
out;ort(struct wa_xfeurb-sg->zC);
	,
		PAGE* error gfp usb_subxr t& c(sizeof(rb, GFP_ATOMMIC);
	ifout;ort(struct ENTo_ufer
SB_DI		wa_resetsizePROGRESS-rocessthat theneedeb, GFP_ATOMletheupd;k
		sready golculate?Trans		/* takd the
		if (de_t resu user.e a;OM)loc * as */
sk_urbloc * as gfpsk_gfploc * as epefe
{n rSB_DIsb_ifa ; cnt ac
_length;
		if (data_b_i->dera fer_he->d2x [_wage_da] %s %s %s"xfen_l			wa_sc_",
				ct dfe (by
layed_rpfgger than %d\,
r, wa_xlayed_rpfer);
	&fer->is_dma = urb->trans ? "dma" : "tryma",
r, wa_xfer_h&)iso_DIR_IN ? "N_HWA_C" : "(unless ",
r,soct_sleepe? "d nn
"		" : "i);
	e"pincufer
soct_sleep-
	}

;
	se a_ken sc_ur
_func__, xfer, wa_xfeOMEM	curr
	spidoneLLmy_ct waon r_
	spia,

xfelne(xfer);h_entry(,  OMEM	curr		__wa_dput()ce->
			holding_dto, releaseOMEM	curr
	spidoneLLmy_ct waon r_theup_wol_
	
		d,  OMEM	currentheup_wol_));
 &seg_size)t;ort(strmebd itentheup_n URB wtsic fra(seg->dto_urb, GF/ey hit tby
 _toeg-/entheuptt(dev, "tCep->uap,eam
		}
a
y hit tFP_ATtuncty oughraun__indet callb "tT rroa}
eray hit ta ut_sd thak
 * De/guainingd = 0sactase DTO 	if (xfer->segxferby
 entheuptt(dev, "xfer 0x%08lllning up xf     wa_xfeen whis URB e)if (holdinghen this URB was _func__, xfer, wa_xfeOMEM	curr
	spidoneLLmy_ct waon r_	u,
	hcdk");
			wa__the fer(&(erres
		EM;
	shcd)elsc_urb)->
			holding_dto, releaseOMEM	curr
	spidoneLLmy_ct waon r_pt;
		}
	}
	xferfl}f } rt;
		}
0->c. */
	wetheupd: rfree_urb(&obmit attMIC);
	:
func__, xfer, wa_xfeOMEM	curr
	spidoneLLmy_ct waon ru,
	hcdk");
			wa__the fer(&(erres
		EM;
	shcd)elsc_urb)
			holding_dto, releaseOMEM	curr
	spidoneLLmy_ct waon P_ATOM;
			wa_ out;
		}
	}
	xfer->EXPORT_SYMBOL_GPLamebd itentheup);
ogs = nDetheupeags);cb() h * o Noteuw
	hcdkeivoallbaken )/[pipes(wa); = nhc flla]nrro>t c dMA b = nUetion_/* popula gply k for the sg_neto WA_d bd itentheup )/iESET,e -EsCONNe hwetheupd=me_inex] flags) lt c'ca;er_delstu al	n/ur);
		;ileorsready gst size if ok*)nrro>t c d, eade -ECONNRES mark this
 * segmoughrif ok hey worompuhiwe sb_ifa ywa_xnd copp->sand cond coentheupsizeeeeeeeeeeeeestrminhaennohoibmt/		 *tcase,ONNREtp. Bg the sizeeeeeeeeeeeeeSB_DI		wa_re= WA_SEe hhappenfe_pusizePROGRESS,sizeeeeeeeeeeeeeentheup )/ermincued rnd cob() bfelsiuthere is Ir state. As wel reqgest_neto WA_if ok, .
 ju bee -ECONN>ep->/iESET,tp. Ir itl reqgest_neto WA_i
erro()ocwxfer couldresul itl[me_inb mightsync pens < 0]a traaten h * o Notewoultthat >xfeatransfehere isee a id bd itdetheup d wa_seg_uffer , er *xfer>dev;
	, i
	/* t_id( voike *dev;
	struct wa	spin_u2ce *dev = &xfif (urb->s);
*dev = &xferres on);
	A.
	 */
	a;
	struct device *dev;
d\n",rest->i0, seg->resul_p_luibl;_tipe *rpipe;
	unsigned long 0, holding_deady) be al			ute thsaf
ould");
		. c se	a_xfer_put(xOther OMEM	curr
	spidoneLL;

	spin__sort(stru,
	hcdkng_dto");
			wa_rt(erres
		EM;
	shcd)elsc_,/* t_id(o_ufer
a(seg->dNCAT_l)
	 (by
structo->stabe
	xt;
Get thxulate nouldnt_s_sg)a rheadme_in		/* takeivoallb
	xt;
>ep->'caeupd becNT), eries
 * ay gwol_ate .e_inram
be needed	/* take a  (by
structoif } rdone = __wa_xfer_is_doneOMEM	curr
	spidoneLL;

	spin_ fra(seg->)
_pt;
		}
	}
	xferfeurb-sg- (by
structusb_subxr t& c(sizeof(rt;
		}
uct ENTo_uunc__, xfer, wa_xfer_id(xfer));
			spin_p
	webug((xferDEQUEUE xn outdce->dev"xfelning up X#%u: request erropin__fer_hdr_size)
{
	struct wane)
			wal*c(sizeo (cntp
	webug((xferdata_b_itdce->devl reqnohRPIPE "t%sfen_lo;
ng up fe */
		%u: request errorn_lo"Probcblyze), so iresul dM"xf on reubmit. */uct ENTo_ueb, GF	*ou= __waas }context;
Cbe al
}

rest-uldr}
errrheate .e_in		/* takeivoallbta to sark th a
it ttwice a Transfer
ex,
				WA_SEG_ABORTED)o (cntp
	webug((xferdata_b_itdce->devle), so irests"xfelning up X
D 0x%08X#%u: request erroon reubmit. */uct ENTo_ueb, GF	*ou= __waas }cont phase /*
 ur);
		 put(x->riven fr_,/ame buffa to sark te c se	a_xfer_put(xOther OMEM	curr
	spidoneLL;

	s2pin_ fra!
	spio_suyr (xfer);h_entry()l)
	(xfer)sprsgc(sizeof(*b), Gdetheupr		__wa_rb)
			holding_dto, releaseOMEM	curr
	spidoneLL;

	s2pin_ fra(xfer)sprsgc(sizeo  r/* +trminhail wtxfeng_heneedeb, GF	*ou= __waar/* +f ok*),/entheup_n )/luainingleneedunlik,d becNT), in setflis
 *e), so ,di('sre surif ok  Fila);
	}
	/*urb er *xresul_p_luiblindex,
				WAresult = ur >k_0ready
it tgrab
/*
 _xfererxfecdone orompuldnt_s_sg)rheate .e_i
it tex,
				WAer);
		if ( a Trans	a_xfer_pEG = xferxfecdonepin_oto out;
	urb->actual_length = 0;
	for (cnt = 0; cnt < xfer->se_xfep
	webug((xferdata_tdce->dev#_waipeisoc(xxfer 0x%08ning up X#%u: request error d\n",	sb_pipeiso)ce->
gned rp	sb_pipeiso) (cntlinkefer->urNOTREADY:cntlinkefer->urREADY:cnt	
		rpi(KERNrmar (data_b_;
		ddetheup bfwaipeiMv		er 0x%08lllllllx%08X#d\n",	sb_pipeiso)ce->	WARNrON(1)ce->	s done btlinkefer->urDELAYED:, GF/ey hit ter) te the n			walur);
		 put(. hey noaitv_err( *
y hit t/*ise * afer coe suri);
	}
	/,dex,
				WA_SEG_AB/ermiy hit t/rigg oua eivoallbte lows dOitioned or mak
 * Delayy hit tsransfeetermine h>uainingd set_in DTIeis  oruptMActase DTO 	sb_pipeisoc(xfer->urABORTED;of	_. */
ubmit. */uct ENTo_uelch_ener)rtsdto_ch_entry(x, fl l_length =frestnto_cu s done btlinkefer->urDONE:cntlinkefer->urmark_:cntlinkefer->urABORTED:_cu s done btonly hit tT subse_	n/uext(curr_aitv_err*set_iny hit tfer->urDTI_inbound ipeiMv the	/ivolyzbeate xferrelease Le */
	bse_	n_cbnhc fll	uteate nuit/ermine h>t c dy hit ta d/erminte rp_err l_length =frest "tCep->'caeupy hit torompwhich  the s/
	bse_	n_cbnferCco_cu * Sel_ley hit tasegmeitl reqe sur>uainingd/b theMActase DTOlinkefer->urDTI_inbound:_cu s done btonly hit tItucauset
	wure lowor makc(xfr_type)e), so iknoway hit tabches 		/e buffer  hey  Fsaesul ens < 0 rpipeerr,y hit ta. *wr makc(xfuldntto_curn tAssuhai(x
}

/*
y hit tubmit.ss dOitioned or makDTIeipeiMv Filahgr>uainingdy hit tkets sisoc get ches
		dyncMActase DTOlinkefer->urSUBMITTED:cntlinkefer->urinbound:_cu nly hit tCbe al			0;

	esul rpipe)for the sg_sgnrentT rrowhichy hit te hwaeg_s_ir makc(xf reqe surrehas ) butcwxfer cl wy hit tb, tetucausdisconnec(xughn reurout_ywaMActase DTO  fra!er *xresul_p_luiblurb, GFP	sb_pipeisoc(xfer->urABORTED;of	_>status;
		__wa_xfer_abort(xfer);
		rpipel l_length =frestnto_cu }_cu s done bt}f } rdone = __waEG = xferxfecdonepin_e_t reubmit. */urb(seg->dtodunluct ENTeors-ECONNRESETse DTseg_index,
				WA_SEG_ABORTED);
	done(xfer, seg, WA_SEG_ERROR);
	}
	spin_unlock_irck_for_delayed_rpipes(wa);
	}
	if (ne)
			wa_xfer_compd	/* taker);
		if (rpipe_readn when this URB was submit
	}
	xferf	*ou= __wa: rdone = __wa_xfer_is_done(xfer);
			spin_unlocn when this URB was submit
	}
	xferfdetheupr		__wa_:

ch_ener) d thee(xfer);h_entry();
)
			holding_dto, releaseOMEM	curr
	spidoneLL;

	s2pin_e_t reubmit. */urb(seg->dto rdone = __wa_xfer_is_done(xfer);
			spin_unlocn when teivoallb( = urb->n when this URB was k1()* taken sc_ur
dunlwxfgot the n*setentheup )/>segs_bmit
0->->EXPORT_SYMBOL_GPLamebd itdetheup);
ogs = na_xfelurout__he n(xfepeisoccry(s (le t1.0 TATOMv8.15)  GFP_Anoer lcry(sere is PoansiveFP_Anotvalhe tay gis  o thets);nsut(mncilist to= WA_SEe  is pin_gv;
	su

/. NegasiveFaompulde hpacurreupd to be he r set_inyt tngetur wayhere is @epeiso:)isoc(xfepeisoccry(d--ihis
Stwg;bir(dget_s/riepayMA buffer inf a id bsize ipeiso_(xfe_Ano(u8/* t_id( voi a ie_Ano; longxfel_ipeisoc(xeg->dto rder inf a ixlur[esg-b, G[(xfer->sSTATUSrSUCCESSesg-		0, of[(xfer->sSTATUSrHALTEDesg-		-EPIPE, of[(xfer->sSTATUSrDATA_BUFr->smark_esg-	uct BUFS, of[(xfer->sSTATUSrBABBLEesg-		-EOVERFLOW, of[(xfer->sRESERVEDesg-			EINVAL, of[(xfer->sSTATUSrNOT_F by esg		0, of[(xfer->sSTATUSrINSUFrICIENTsRESOURCEesg-uct wa_, of[(xfer->sSTATUSrdma =ACTIONsmark_esg-	ucILSEQ, of[(xfer->sSTATUSrABORTEDesg		uct ENT, of[(xfer->sSTATUSrRPIPErNOT_READYesg-	EINVAL, of[(xfer->sINVALID_F RMATesg-		EINVAL, of[(xfer->sUNEXPECTEDr->uMENTsNUMBEResg-	EINVAL, of[(xfer->sSTATUSrRPIPEregs =MISMATCHesg-	EINVAL, o}o rder u
	& ce-3fincufer
ipeisoc(CAT_ISOs_bmit
0->ufer
ipeisoc>k_ARRAY
		cu(xlur)o (cntp
	rpi_u
	wli Deed(KERNrmar (%s(): BUG?w"x%08lllllll"Unknownc(xfe. As welipeisoce->d2xer 0x%08lllllllning up xfxfel_ipeisoon reub
		}
ucINVALas }coP_Anot=ixlur[ipeiso]o_ufer
S);
kelybP_Anot>o_uile (ip
	rpi_u
	wli Deed(KERNrmar (%s(): BUG?w"x%08lllllll"Is);nsut(mntc(xfepeiso:ce->d2xer 0x%08lllllllning up xfxfel_ipeisoon reP_Anot=i-e_Ano; l} rt;
		}
e_Ano; ->result ey  ases. ;
	}

	/pin_st t/urr_a* populate mit. FP_ATtrror'kets  o	/,yt tnghhappen;
	}

	/* populate mit.etermine ht;
		}ug("%s: d*
 urvice at;
Merwisesusegs as ak
 * Delaye "xp_luiblisize dash>uainingd d segRESET, becNT), ermin sark te >ep->lyhere is (xfer);
		 mu bee hheldere iseerror;
	}
errtra sark te_segs as a size ifwa;
	case -ECONNRESET:A 
donex <lculate_t;
		,,truct wacprO-	wa_re= t_id( voi a it;
		);
	A.
	 */
	a;
	struct dhdr_size)
{
	struct wn_oto oigned lo<lculate_t;
		;it;
		tual_length =f
 * Delay;it;
		for (cnt dev = &xferreseg_sg;
			c0; cnt < xfer	cpu_tf ck
fer_ON	eg_sg;
			c0;c(sizeoev
_fugned rpeg_sg;
			c_pipeiso) (cntlinkefer->urSUBMITTED:cntlinkefer->urinbound:_culinkefer->urDTI_inbound:_cu _xfer_abort(xfer);
		rpipebe
	xt;
y oughrte rp_err RPIPE tbortx
}

/*
 fer->urDELAYEDalink
	xt;
ate nuit/hthiwe de suri);
	}
	/d to be RPIPE 
tase DTOlinkefer->urDELAYED:, GFl_length =frestnto_cu eg_sg;
			c_pipeisoc(xeg->dto ru s done btlinkefer->urABORTED:_cu s done btu the w:e->	WARN(1segxferONNREe->dev#_w. bfwai	c0ipeisoc(xxfer 0x%088ning up X#%u: request error ,
				xfer eg_sg;
			c_pipeiso)ce->	s done bt}ady)->resnents;
	}
 be 
	retua Detbufd *
 * Serfer_sg)egmene. As welipeie. buffer inf a in traisoc dto bse_	n_ureleask;
	case -Euffer ,	struct w->devbse_	n_urefer_iso =
			con__wa_xfl be transferres on( voi a iurel<lculc_fram AT_ISOC)
			mOfPacigned +T_ISOC)
			mOfPacex = framnex < ur>;
		,,totel_letu>i0, wa__thfPacigned lourel<lculc_fram);
	A.
	 *;
	sto nothiet_seg *seg) {
ngth[frame_induewa_po	p ID G_PENDIngth[frame_ind;nst unsi_frae(inothiet_=
		  US,
	
 * stat_maxp((&use(ifer-)ramnex n)
{c_frame_intiguous);
	A.
	 *;
	sto nothiet_seg *seg) {
ngth[framin
xfer_ON	bse_	n_ure_pipeisoc(etsizePROGRESS-occontext;
Ied iSerfer_sg)_fram e	/* rhan %d\ rrowhntiguous .e_index);

		_fram
it ta d/e	/* rhan %d\ rroa segmeplframe be DTIe_lusb_de maxer);
}

t, iOUNr lcrmbined iSerfer_sg)_fram .e_index);

		_framet_r_/ * the URBentT rrext;
aeduce
 * surucbgmecfes);E.nd copu bee hi);
	}
	/