// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Surface2.0/SUR40/PixelSense input driver
 *
 * Copyright (c) 2014 by Florian 'floe' Echtler <floe@butterbrot.org>
 *
 * Derived from the USB Skeleton driver 1.1,
 * Copyright (c) 2003 Greg Kroah-Hartman (greg@kroah.com)
 *
 * and from the Apple USB BCM5974 multitouch driver,
 * Copyright (c) 2008 Henrik Rydberg (rydberg@euromail.se)
 *
 * and from the generic hid-multitouch driver,
 * Copyright (c) 2010-2012 Stephane Chatty <chatty@enac.fr>
 *
 * and from the v4l2-pci-skeleton driver,
 * Copyright (c) Copyright 2014 Cisco Systems, Inc.
 *
 * Standalone version: touch and video data are retrieved with asynchronous
 * URBs that are kept queued on the bulk IN endpoints, so the host controller
 * does the work and no periodic polling is required. Video frames are
 * DMA'd directly into the videobuf2 buffers via scatter-gather URBs.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/scatterlist.h>
#include <linux/usb.h>
#include <linux/printk.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/usb/input.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-sg.h>

/* read 512 bytes from endpoint 0x86 -> get header + blobs */
struct sur40_header {

	__le16 type;       /* always 0x0001 */
	__le16 count;      /* count of blobs (if 0: continue prev. packet) */

	__le32 packet_id;  /* unique ID for all packets in one frame */

	__le32 timestamp;  /* milliseconds (inc. by 16 or 17 each frame) */
	__le32 unknown;    /* "epoch?" always 02/03 00 00 00 */

} __packed;

struct sur40_blob {

	__le16 blob_id;

	u8 action;         /* 0x02 = enter/exit, 0x03 = update (?) */
	u8 type;           /* bitmask (0x01 blob,  0x02 touch, 0x04 tag) */

	__le16 bb_pos_x;   /* upper left corner of bounding box */
	__le16 bb_pos_y;

	__le16 bb_size_x;  /* size of bounding box */
	__le16 bb_size_y;

	__le16 pos_x;      /* finger tip position */
	__le16 pos_y;

	__le16 ctr_x;      /* centroid position */
	__le16 ctr_y;

	__le16 axis_x;     /* somehow related to major/minor axis, mostly: */
	__le16 axis_y;     /* axis_x == bb_size_y && axis_y == bb_size_x */

	__le32 angle;      /* orientation in radians relative to x axis -
	                      actually an IEEE754 float, don't use in kernel */

	__le32 area;       /* size in pixels/pressure (?) */

	u8 padding[24];

	__le32 tag_id;     /* valid when type == 0x04 (SUR40_TAG) */
	__le32 unknown;

} __packed;

/* combined header/blob data */
struct sur40_data {
	struct sur40_header header;
	struct sur40_blob   blobs[];
} __packed;

/* read 512 bytes from endpoint 0x82 -> get header below
 * continue reading 16k blocks until header.size bytes read */
struct sur40_image_header {
	__le32 magic;     /* "SUBF" */
	__le32 packet_id;
	__le32 size;      /* always 0x0007e900 = 960x540 */
	__le32 timestamp; /* milliseconds (increases by 16 or 17 each frame) */
	__le32 unknown;   /* "epoch?" always 02/03 00 00 00 */
} __packed;

/* version information */
#define DRIVER_SHORT   "sur40"
#define DRIVER_LONG    "Samsung SUR40"
#define DRIVER_AUTHOR  "Florian 'floe' Echtler <floe@butterbrot.org>"
#define DRIVER_DESC    "Surface2.0/SUR40/PixelSense input driver"

/* vendor and device IDs */
#define ID_MICROSOFT 0x045e
#define ID_SUR40     0x0775

/* sensor resolution */
#define SENSOR_RES_X 1920
#define SENSOR_RES_Y 1080

/* touch data endpoint */
#define TOUCH_ENDPOINT 0x86

/* video data endpoint */
#define VIDEO_ENDPOINT 0x82

/* video header fields */
#define VIDEO_HEADER_MAGIC 0x46425553
#define VIDEO_PACKET_SIZE  16384

/* number of URBs kept queued on the touch endpoint */
#define SUR40_TOUCH_URBS 8

/* size of the scratch buffer used to discard video data */
#define SUR40_DRAIN_SIZE (64 * 1024)

/* upper bound for a plausible frame size announced by the device */
#define SUR40_MAX_FRAME_SIZE (4 * SENSOR_RES_X * SENSOR_RES_Y)

/* maximum number of contacts */
#define MAX_CONTACTS 52

/* control commands */
#define SUR40_GET_VERSION 0xb0 /* 12 bytes string    */
#define SUR40_ACCEL_CAPS  0xb3 /*  5 bytes           */
#define SUR40_SENSOR_CAPS 0xc1 /* 24 bytes           */

#define SUR40_POKE        0xc5 /* poke register byte */
#define SUR40_PEEK        0xc4 /* 48 bytes registers */

#define SUR40_GET_STATE   0xc5 /*  4 bytes state (?) */
#define SUR40_GET_SENSORS 0xb1 /*  8 bytes sensors   */

#define SUR40_BLOB	0x01
#define SUR40_TOUCH	0x02
#define SUR40_TAG	0x04

/* video controls */
#define SUR40_BRIGHTNESS_MAX 0xff
#define SUR40_BRIGHTNESS_MIN 0x00
#define SUR40_BRIGHTNESS_DEF 0xff

#define SUR40_CONTRAST_MAX 0x0f
#define SUR40_CONTRAST_MIN 0x00
#define SUR40_CONTRAST_DEF 0x0a

#define SUR40_GAIN_MAX 0x09
#define SUR40_GAIN_MIN 0x00
#define SUR40_GAIN_DEF 0x08

#define SUR40_BACKLIGHT_MAX 0x01
#define SUR40_BACKLIGHT_MIN 0x00
#define SUR40_BACKLIGHT_DEF 0x01

#define sur40_str(s) #s
#define SUR40_PARAM_RANGE(lo, hi) " (range " sur40_str(lo) "-" sur40_str(hi) ")"

/* module parameters */
static uint brightness = SUR40_BRIGHTNESS_DEF;
module_param(brightness, uint, 0644);
MODULE_PARM_DESC(brightness, "set initial brightness"
	SUR40_PARAM_RANGE(SUR40_BRIGHTNESS_MIN, SUR40_BRIGHTNESS_MAX));
static uint contrast = SUR40_CONTRAST_DEF;
module_param(contrast, uint, 0644);
MODULE_PARM_DESC(contrast, "set initial contrast"
	SUR40_PARAM_RANGE(SUR40_CONTRAST_MIN, SUR40_CONTRAST_MAX));
static uint gain = SUR40_GAIN_DEF;
module_param(gain, uint, 0644);
MODULE_PARM_DESC(gain, "set initial gain"
	SUR40_PARAM_RANGE(SUR40_GAIN_MIN, SUR40_GAIN_MAX));

static const struct v4l2_pix_format sur40_pix_format[] = {
	{
		.pixelformat = V4L2_TCH_FMT_TU08,
		.width  = SENSOR_RES_X / 2,
		.height = SENSOR_RES_Y / 2,
		.field = V4L2_FIELD_NONE,
		.colorspace = V4L2_COLORSPACE_RAW,
		.bytesperline = SENSOR_RES_X / 2,
		.sizeimage = (SENSOR_RES_X/2) * (SENSOR_RES_Y/2),
	},
	{
		.pixelformat = V4L2_PIX_FMT_GREY,
		.width  = SENSOR_RES_X / 2,
		.height = SENSOR_RES_Y / 2,
		.field = V4L2_FIELD_NONE,
		.colorspace = V4L2_COLORSPACE_RAW,
		.bytesperline = SENSOR_RES_X / 2,
		.sizeimage = (SENSOR_RES_X/2) * (SENSOR_RES_Y/2),
	}
};

/* master device state */
struct sur40_state {

	struct usb_device *usbdev;
	struct device *dev;
	struct input_dev *input;

	struct v4l2_device v4l2;
	struct video_device vdev;
	struct mutex lock;
	struct v4l2_pix_format pix_fmt;
	struct v4l2_ctrl_handler hdl;

	struct vb2_queue queue;
	struct list_head buf_list;
	spinlock_t qlock;
	u32 sequence;

	/*
	 * Touch pipeline: SUR40_TOUCH_URBS bulk URBs kept queued on the touch
	 * endpoint. It runs while the input device is open or video is
	 * streaming (the device interleaves both streams, so the touch
	 * endpoint has to be drained in either case). need_blobs is only
	 * touched from the (serialised) completion handlers of that endpoint.
	 */
	struct mutex pipe_lock;
	unsigned int users;
	struct urb *touch_urbs[SUR40_TOUCH_URBS];
	size_t touch_pkt_size;
	int need_blobs;

	/*
	 * Video pipeline: a header URB waits for the 20 byte frame header,
	 * then either a scatter-gather URB DMAs the image straight into the
	 * next vb2 buffer or, if none is available, the frame is discarded via
	 * the drain URB. All three share one endpoint, so only one of them is
	 * in flight at any time and their completions are serialised.
	 */
	struct urb *video_hdr_urb;
	struct urb *video_frame_urb;
	struct urb *video_drain_urb;
	void *video_drain_buf;
	size_t video_pkt_size;
	size_t drain_remaining;      /* 0 = resync: hunt for the next header */
	struct sur40_buffer *cur_buf;
	unsigned long frames_dropped;

	bool disconnected;
	u8 vsvideo;

	char phys[64];
};

struct sur40_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

/* forward declarations */
static const struct video_device sur40_video_device;
static const struct vb2_queue sur40_queue;
static int sur40_s_ctrl(struct v4l2_ctrl *ctrl);
static void sur40_video_submit_hdr(struct sur40_state *sur40);
static void sur40_video_submit_drain(struct sur40_state *sur40, size_t count);

static const struct v4l2_ctrl_ops sur40_ctrl_ops = {
	.s_ctrl = sur40_s_ctrl,
};

/*
 * Note: an earlier, non-public version of this driver used USB_RECIP_ENDPOINT
 * here by mistake which is very likely to have corrupted the firmware EEPROM
 * on two separate SUR40 devices. Thanks to Alan Stern who spotted this bug.
 * Should you ever run into a similar problem, the background story to this
 * incident and instructions on how to fix the corrupted EEPROM are available
 * at https://floe.butterbrot.org/matrix/hacking/surface/brick.html
*/

/* command wrapper */
static int sur40_command(struct sur40_state *dev,
			 u8 command, u16 index, void *buffer, u16 size)
{
	return usb_control_msg(dev->usbdev, usb_rcvctrlpipe(dev->usbdev, 0),
			       command,
			       USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			       0x00, index, buffer, size, 1000);
}

/* poke a byte in the panel register space */
static int sur40_poke(struct sur40_state *dev, u8 offset, u8 value)
{
	int result;
	u8 index = 0x96; // 0xae for permanent write

	result = usb_control_msg(dev->usbdev, usb_sndctrlpipe(dev->usbdev, 0),
		SUR40_POKE, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
		0x32, index, NULL, 0, 1000);
	if (result < 0)
		goto error;
	msleep(5);

	result = usb_control_msg(dev->usbdev, usb_sndctrlpipe(dev->usbdev, 0),
		SUR40_POKE, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
		0x72, offset, NULL, 0, 1000);
	if (result < 0)
		goto error;
	msleep(5);

	result = usb_control_msg(dev->usbdev, usb_sndctrlpipe(dev->usbdev, 0),
		SUR40_POKE, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
		0xb2, value, NULL, 0, 1000);
	if (result < 0)
		goto error;
	msleep(5);

error:
	return result;
}

static int sur40_set_preprocessor(struct sur40_state *dev, u8 value)
{
	u8 setting_07[2] = { 0x01, 0x00 };
	u8 setting_17[2] = { 0x85, 0x80 };
	int result;

	if (value > 1)
		return -ERANGE;

	result = usb_control_msg(dev->usbdev, usb_sndctrlpipe(dev->usbdev, 0),
		SUR40_POKE, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
		0x07, setting_07[value], NULL, 0, 1000);
	if (result < 0)
		goto error;
	msleep(5);

	result = usb_control_msg(dev->usbdev, usb_sndctrlpipe(dev->usbdev, 0),
		SUR40_POKE, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
		0x17, setting_17[value], NULL, 0, 1000);
	if (result < 0)
		goto error;
	msleep(5);

error:
	return result;
}

static void sur40_set_vsvideo(struct sur40_state *handle, u8 value)
{
	int i;

	for (i = 0; i < 4; i++)
		sur40_poke(handle, 0x1c+i, value);
	handle->vsvideo = value;
}

static void sur40_set_irlevel(struct sur40_state *handle, u8 value)
{
	int i;

	for (i = 0; i < 8; i++)
		sur40_poke(handle, 0x08+(2*i), value);
}

/* Initialization routine, called when the first consumer shows up */
static int sur40_init(struct sur40_state *dev)
{
	int result;
	u8 *buffer;

	buffer = kmalloc(24, GFP_KERNEL);
	if (!buffer) {
		result = -ENOMEM;
		goto error;
	}

	/* stupidly replay the original MS driver init sequence */
	result = sur40_command(dev, SUR40_GET_VERSION, 0x00, buffer, 12);
	if (result < 0)
		goto error;

	result = sur40_command(dev, SUR40_GET_VERSION, 0x01, buffer, 12);
	if (result < 0)
		goto error;

	result = sur40_command(dev, SUR40_GET_VERSION, 0x02, buffer, 12);
	if (result < 0)
		goto error;

	result = sur40_command(dev, SUR40_SENSOR_CAPS, 0x00, buffer, 24);
	if (result < 0)
		goto error;

	result = sur40_command(dev, SUR40_ACCEL_CAPS, 0x00, buffer, 5);
	if (result < 0)
		goto error;

	result = sur40_command(dev, SUR40_GET_VERSION, 0x03, buffer, 12);
	if (result < 0)
		goto error;

	result = 0;

	/*
	 * Discard the result buffer - no known data inside except
	 * some version strings, maybe extract these sometime...
	 */
error:
	kfree(buffer);
	return result;
}

/*
 * URB helpers
 */

/* true if the URB was cancelled or the device went away: do not resubmit */
static bool sur40_urb_dead(struct urb *urb)
{
	switch (urb->status) {
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
	case -ENODEV:
		return true;
	default:
		return false;
	}
}

/* resubmit from a completion handler; -EPERM means we are being stopped */
static void sur40_resubmit(struct sur40_state *sur40, struct urb *urb,
			   const char *what)
{
	int ret = usb_submit_urb(urb, GFP_ATOMIC);

	if (ret && ret != -EPERM && ret != -ENODEV)
		dev_err_ratelimited(sur40->dev,
			"failed to resubmit %s urb: %d\n", what, ret);
}

/*
 * Touch pipeline
 */

/*
 * This function is called when a whole contact has been processed,
 * so that it can assign it to a slot and store the data there.
 */
static void sur40_report_blob(struct sur40_blob *blob, struct input_dev *input)
{
	int wide, major, minor;
	int bb_size_x, bb_size_y, pos_x, pos_y, ctr_x, ctr_y, slotnum;

	if (blob->type != SUR40_TOUCH)
		return;

	slotnum = input_mt_get_slot_by_key(input, blob->blob_id);
	if (slotnum < 0 || slotnum >= MAX_CONTACTS)
		return;

	bb_size_x = le16_to_cpu(blob->bb_size_x);
	bb_size_y = le16_to_cpu(blob->bb_size_y);

	pos_x = le16_to_cpu(blob->pos_x);
	pos_y = le16_to_cpu(blob->pos_y);

	ctr_x = le16_to_cpu(blob->ctr_x);
	ctr_y = le16_to_cpu(blob->ctr_y);

	input_mt_slot(input, slotnum);
	input_mt_report_slot_state(input, MT_TOOL_FINGER, 1);
	wide = (bb_size_x > bb_size_y);
	major = max(bb_size_x, bb_size_y);
	minor = min(bb_size_x, bb_size_y);

	input_report_abs(input, ABS_MT_POSITION_X, pos_x);
	input_report_abs(input, ABS_MT_POSITION_Y, pos_y);
	input_report_abs(input, ABS_MT_TOOL_X, ctr_x);
	input_report_abs(input, ABS_MT_TOOL_Y, ctr_y);

	/* TODO: use a better orientation measure */
	input_report_abs(input, ABS_MT_ORIENTATION, wide);
	input_report_abs(input, ABS_MT_TOUCH_MAJOR, major);
	input_report_abs(input, ABS_MT_TOUCH_MINOR, minor);
}

/* core function: one touch packet has arrived */
static void sur40_touch_complete(struct urb *urb)
{
	struct sur40_state *sur40 = urb->context;
	struct sur40_data *data = urb->transfer_buffer;
	struct input_dev *input = sur40->input;
	unsigned int len = urb->actual_length;
	int packet_blobs, i;

	if (sur40_urb_dead(urb))
		return;

	if (urb->status) {
		dev_dbg_ratelimited(sur40->dev, "touch urb status %d\n",
				    urb->status);
		sur40->need_blobs = -1;
		goto resubmit;
	}

	dev_dbg(sur40->dev, "received %u bytes\n", len);

	if (len < sizeof(struct sur40_header) ||
	    (len - sizeof(struct sur40_header)) % sizeof(struct sur40_blob)) {
		dev_err_ratelimited(sur40->dev,
				    "transfer size mismatch (%u bytes)\n", len);
		sur40->need_blobs = -1;
		goto resubmit;
	}

	packet_blobs = (len - sizeof(struct sur40_header)) /
		       sizeof(struct sur40_blob);
	dev_dbg(sur40->dev, "received %d blobs\n", packet_blobs);

	/*
	 * The first packet of a frame carries the total blob count, the
	 * following packets (if any) have count == 0. The packet ID is only
	 * reliable for the first packet, so it is not checked.
	 */
	if (sur40->need_blobs < 0) {
		sur40->need_blobs = le16_to_cpu(data->header.count);
		dev_dbg(sur40->dev, "need %d blobs\n", sur40->need_blobs);
	}

	/* packets always contain at least 4 blobs, even if empty */
	if (packet_blobs > sur40->need_blobs)
		packet_blobs = sur40->need_blobs;

	for (i = 0; i < packet_blobs; i++)
		sur40_report_blob(&data->blobs[i], input);
	sur40->need_blobs -= packet_blobs;

	if (sur40->need_blobs <= 0) {
		input_mt_sync_frame(input);
		input_sync(input);
		sur40->need_blobs = -1;
	}

resubmit:
	sur40_resubmit(sur40, urb, "touch");
}

/* poison all touch URBs: waits for in-flight ones and blocks resubmission */
static void sur40_touch_stop(struct sur40_state *sur40)
{
	int i;

	if (sur40->disconnected)
		return;

	for (i = 0; i < SUR40_TOUCH_URBS; i++)
		usb_poison_urb(sur40->touch_urbs[i]);
}

static int sur40_touch_start(struct sur40_state *sur40)
{
	int i, ret;

	sur40->need_blobs = -1;

	for (i = 0; i < SUR40_TOUCH_URBS; i++)
		usb_unpoison_urb(sur40->touch_urbs[i]);

	for (i = 0; i < SUR40_TOUCH_URBS; i++) {
		ret = usb_submit_urb(sur40->touch_urbs[i], GFP_KERNEL);
		if (ret) {
			dev_err(sur40->dev,
				"failed to submit touch urb %d: %d\n", i, ret);
			sur40_touch_stop(sur40);
			return ret;
		}
	}

	return 0;
}

/*
 * The touch pipeline is shared by the input device and the video queue.
 * Neither sur40->lock nor input->mutex is used here, because the callers
 * hold one or the other.
 */
static int sur40_pipeline_get(struct sur40_state *sur40)
{
	int ret = 0;

	mutex_lock(&sur40->pipe_lock);
	if (sur40->users == 0) {
		ret = sur40_init(sur40);
		if (!ret)
			ret = sur40_touch_start(sur40);
	}
	if (!ret)
		sur40->users++;
	mutex_unlock(&sur40->pipe_lock);

	return ret;
}

static void sur40_pipeline_put(struct sur40_state *sur40)
{
	mutex_lock(&sur40->pipe_lock);
	if (!WARN_ON(sur40->users == 0) && --sur40->users == 0)
		sur40_touch_stop(sur40);
	mutex_unlock(&sur40->pipe_lock);
}

/*
 * Callback routines from input_dev
 */

/* Enable the device, touch URBs will now be queued. */
static int sur40_open(struct input_dev *input)
{
	struct sur40_state *sur40 = input_get_drvdata(input);

	dev_dbg(sur40->dev, "open\n");
	return sur40_pipeline_get(sur40);
}

/* Disable device. */
static void sur40_close(struct input_dev *input)
{
	struct sur40_state *sur40 = input_get_drvdata(input);

	dev_dbg(sur40->dev, "close\n");
	/*
	 * There is no known way to stop the device, so we simply
	 * stop reading from it (unless video is still streaming).
	 */
	sur40_pipeline_put(sur40);
}

/*
 * Video pipeline
 */

static struct sur40_buffer *sur40_video_pop_buffer(struct sur40_state *sur40)
{
	struct sur40_buffer *buf = NULL;
	unsigned long flags;

	spin_lock_irqsave(&sur40->qlock, flags);
	if (!list_empty(&sur40->buf_list)) {
		buf = list_first_entry(&sur40->buf_list, struct sur40_buffer,
				       list);
		list_del(&buf->list);
	}
	spin_unlock_irqrestore(&sur40->qlock, flags);

	return buf;
}

static void sur40_video_submit_hdr(struct sur40_state *sur40)
{
	sur40_resubmit(sur40, sur40->video_hdr_urb, "video header");
}

/*
 * Discard @count bytes of video data (count == 0: unknown alignment, keep
 * reading until a valid frame header shows up as a short packet).
 */
static void sur40_video_submit_drain(struct sur40_state *sur40, size_t count)
{
	struct urb *urb = sur40->video_drain_urb;

	sur40->drain_remaining = count;
	urb->transfer_buffer_length = count ? min_t(size_t, count,
		SUR40_DRAIN_SIZE) : SUR40_DRAIN_SIZE;
	sur40_resubmit(sur40, urb, "video drain");
}

/* a valid-looking 20 byte header has arrived: start the frame transfer */
static void sur40_video_handle_header(struct sur40_state *sur40,
				      struct sur40_image_header *img)
{
	struct sur40_buffer *buf;
	struct sg_table *sgt;
	struct urb *urb = sur40->video_frame_urb;
	u32 size = le32_to_cpu(img->size);
	int ret;

	if (le32_to_cpu(img->magic) != VIDEO_HEADER_MAGIC) {
		dev_dbg_ratelimited(sur40->dev, "image magic mismatch\n");
		sur40_video_submit_drain(sur40, 0);
		return;
	}

	if (size != sur40->pix_fmt.sizeimage) {
		dev_err_ratelimited(sur40->dev, "image size mismatch (%u)\n",
				    size);
		sur40_video_submit_drain(sur40,
			size <= SUR40_MAX_FRAME_SIZE ? size : 0);
		return;
	}

	dev_dbg(sur40->dev, "header acquired\n");

	buf = sur40_video_pop_buffer(sur40);
	if (!buf) {
		/* userspace is too slow: drop this frame but stay in sync */
		sur40->frames_dropped++;
		dev_dbg_ratelimited(sur40->dev, "buffer queue empty, %lu frames dropped\n",
				    sur40->frames_dropped);
		sur40_video_submit_drain(sur40, size);
		return;
	}

	/*
	 * DMA straight into the vb2 buffer. The host controller maps the
	 * scatterlist itself, so hand it the full (unmapped) entry count.
	 */
	sgt = vb2_dma_sg_plane_desc(&buf->vb.vb2_buf, 0);
	usb_fill_bulk_urb(urb, sur40->usbdev,
		usb_rcvbulkpipe(sur40->usbdev,
				VIDEO_ENDPOINT & USB_ENDPOINT_NUMBER_MASK),
		NULL, size, urb->complete, sur40);
	urb->sg = sgt->sgl;
	urb->num_sgs = sgt->orig_nents;

	sur40->cur_buf = buf;
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret) {
		if (ret != -EPERM && ret != -ENODEV)
			dev_err_ratelimited(sur40->dev,
				"failed to submit frame urb: %d\n", ret);
		sur40->cur_buf = NULL;
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
}

static void sur40_video_hdr_complete(struct urb *urb)
{
	struct sur40_state *sur40 = urb->context;

	if (sur40_urb_dead(urb))
		return;

	if (urb->status) {
		dev_dbg_ratelimited(sur40->dev, "video header urb status %d\n",
				    urb->status);
		sur40_video_submit_hdr(sur40);
		return;
	}

	if (urb->actual_length != sizeof(struct sur40_image_header)) {
		/* not at a frame boundary: skip ahead to the next header */
		dev_dbg_ratelimited(sur40->dev, "received %u bytes (%zu expected), resyncing\n",
				    urb->actual_length,
				    sizeof(struct sur40_image_header));
		sur40_video_submit_drain(sur40, 0);
		return;
	}

	sur40_video_handle_header(sur40, urb->transfer_buffer);
}

static void sur40_video_frame_complete(struct urb *urb)
{
	struct sur40_state *sur40 = urb->context;
	struct sur40_buffer *buf;
	enum vb2_buffer_state state = VB2_BUF_STATE_DONE;

	/* cancelled: stop_streaming() takes care of cur_buf */
	if (sur40_urb_dead(urb))
		return;

	buf = sur40->cur_buf;
	sur40->cur_buf = NULL;
	if (WARN_ON(!buf))
		goto next;

	if (urb->status || urb->actual_length != urb->transfer_buffer_length) {
		dev_dbg_ratelimited(sur40->dev, "frame urb status %d, %u/%u bytes\n",
				    urb->status, urb->actual_length,
				    urb->transfer_buffer_length);
		state = VB2_BUF_STATE_ERROR;
	} else {
		dev_dbg(sur40->dev, "image acquired\n");
		buf->vb.vb2_buf.timestamp = ktime_get_ns();
		buf->vb.sequence = sur40->sequence++;
		buf->vb.field = V4L2_FIELD_NONE;
	}

	vb2_buffer_done(&buf->vb.vb2_buf, state);

next:
	sur40_video_submit_hdr(sur40);
}

static void sur40_video_drain_complete(struct urb *urb)
{
	struct sur40_state *sur40 = urb->context;
	unsigned int len = urb->actual_length;

	if (sur40_urb_dead(urb))
		return;

	if (urb->status) {
		dev_dbg_ratelimited(sur40->dev, "video drain urb status %d\n",
				    urb->status);
		sur40_video_submit_drain(sur40, 0);
		return;
	}

	if (sur40->drain_remaining == 0) {
		/*
		 * Resync mode: the header is sent as a 20 byte short packet,
		 * which ends the transfer. It may be preceded by a number of
		 * full packets of the previous frame within the same URB.
		 */
		if (len >= sizeof(struct sur40_image_header) &&
		    (len - sizeof(struct sur40_image_header)) %
		    sur40->video_pkt_size == 0) {
			sur40_video_handle_header(sur40, urb->transfer_buffer +
				len - sizeof(struct sur40_image_header));
			return;
		}
		sur40_video_submit_drain(sur40, 0);
		return;
	}

	if (len >= sur40->drain_remaining) {
		/* frame fully discarded, wait for the next header */
		sur40->drain_remaining = 0;
		sur40_video_submit_hdr(sur40);
		return;
	}

	if (len < urb->transfer_buffer_length) {
		/* short packet in the middle of a frame: alignment is lost */
		dev_dbg_ratelimited(sur40->dev, "short drain packet, resyncing\n");
		sur40_video_submit_drain(sur40, 0);
		return;
	}

	sur40_video_submit_drain(sur40, sur40->drain_remaining - len);
}

/* poison all video URBs: waits for in-flight ones and blocks resubmission */
static void sur40_video_stop(struct sur40_state *sur40)
{
	if (sur40->disconnected)
		return;

	usb_poison_urb(sur40->video_hdr_urb);
	usb_poison_urb(sur40->video_frame_urb);
	usb_poison_urb(sur40->video_drain_urb);
}

static int sur40_video_start(struct sur40_state *sur40)
{
	int ret;

	sur40->drain_remaining = 0;
	usb_unpoison_urb(sur40->video_hdr_urb);
	usb_unpoison_urb(sur40->video_frame_urb);
	usb_unpoison_urb(sur40->video_drain_urb);

	ret = usb_submit_urb(sur40->video_hdr_urb, GFP_KERNEL);
	if (ret) {
		dev_err(sur40->dev, "failed to submit video header urb: %d\n",
			ret);
		sur40_video_stop(sur40);
	}

	return ret;
}

/* Initialize input device parameters. */
static int sur40_input_setup_events(struct input_dev *input_dev)
{
	int error;

	input_set_abs_params(input_dev, ABS_MT_POSITION_X,
			     0, SENSOR_RES_X, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
			     0, SENSOR_RES_Y, 0, 0);

	input_set_abs_params(input_dev, ABS_MT_TOOL_X,
			     0, SENSOR_RES_X, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOOL_Y,
			     0, SENSOR_RES_Y, 0, 0);

	/* max value unknown, but major/minor axis
	 * can never be larger than screen */
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR,
			     0, SENSOR_RES_X, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MINOR,
			     0, SENSOR_RES_Y, 0, 0);

	input_set_abs_params(input_dev, ABS_MT_ORIENTATION, 0, 1, 0, 0);

	error = input_mt_init_slots(input_dev, MAX_CONTACTS,
				    INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (error) {
		dev_err(input_dev->dev.parent, "failed to set up slots\n");
		return error;
	}

	return 0;
}

/*
 * URB allocation / release. All URBs start out poisoned so that the
 * start/stop helpers can rely on a balanced unpoison/poison sequence.
 */
static void sur40_free_urbs(struct sur40_state *sur40)
{
	int i;

	for (i = 0; i < SUR40_TOUCH_URBS; i++) {
		if (sur40->touch_urbs[i])
			kfree(sur40->touch_urbs[i]->transfer_buffer);
		usb_free_urb(sur40->touch_urbs[i]);
	}
	if (sur40->video_hdr_urb)
		kfree(sur40->video_hdr_urb->transfer_buffer);
	usb_free_urb(sur40->video_hdr_urb);
	usb_free_urb(sur40->video_frame_urb);
	usb_free_urb(sur40->video_drain_urb);
	kfree(sur40->video_drain_buf);
}

static struct urb *sur40_alloc_bulk_urb(struct sur40_state *sur40, u8 epaddr,
					size_t size, usb_complete_t complete)
{
	struct urb *urb;
	void *buf = NULL;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return NULL;

	if (size) {
		buf = kmalloc(size, GFP_KERNEL);
		if (!buf) {
			usb_free_urb(urb);
			return NULL;
		}
	}

	usb_fill_bulk_urb(urb, sur40->usbdev,
		usb_rcvbulkpipe(sur40->usbdev, epaddr & USB_ENDPOINT_NUMBER_MASK),
		buf, size, complete, sur40);
	usb_poison_urb(urb);

	return urb;
}

static int sur40_alloc_urbs(struct sur40_state *sur40)
{
	int i;

	for (i = 0; i < SUR40_TOUCH_URBS; i++) {
		sur40->touch_urbs[i] = sur40_alloc_bulk_urb(sur40,
			TOUCH_ENDPOINT, sur40->touch_pkt_size,
			sur40_touch_complete);
		if (!sur40->touch_urbs[i])
			goto err;
	}

	sur40->video_hdr_urb = sur40_alloc_bulk_urb(sur40, VIDEO_ENDPOINT,
		sur40->video_pkt_size, sur40_video_hdr_complete);
	if (!sur40->video_hdr_urb)
		goto err;

	/* transfer buffer and length are filled in per frame */
	sur40->video_frame_urb = sur40_alloc_bulk_urb(sur40, VIDEO_ENDPOINT,
		0, sur40_video_frame_complete);
	if (!sur40->video_frame_urb)
		goto err;

	sur40->video_drain_buf = kmalloc(SUR40_DRAIN_SIZE, GFP_KERNEL);
	if (!sur40->video_drain_buf)
		goto err;

	sur40->video_drain_urb = sur40_alloc_bulk_urb(sur40, VIDEO_ENDPOINT,
		0, sur40_video_drain_complete);
	if (!sur40->video_drain_urb)
		goto err;
	sur40->video_drain_urb->transfer_buffer = sur40->video_drain_buf;

	return 0;

err:
	sur40_free_urbs(sur40);
	return -ENOMEM;
}

/* Called when the last reference (video node or driver) is gone. */
static void sur40_release(struct v4l2_device *v4l2)
{
	struct sur40_state *sur40 = container_of(v4l2, struct sur40_state, v4l2);

	v4l2_ctrl_handler_free(&sur40->hdl);
	v4l2_device_unregister(&sur40->v4l2);
	sur40_free_urbs(sur40);
	put_device(sur40->dev);
	usb_put_dev(sur40->usbdev);
	kfree(sur40);
}

static void sur40_video_device_release(struct video_device *vdev)
{
	struct sur40_state *sur40 = container_of(vdev, struct sur40_state, vdev);

	v4l2_device_put(&sur40->v4l2);
}

/* Check candidate USB interface. */
static int sur40_probe(struct usb_interface *interface,
		       const struct usb_device_id *id)
{
	struct usb_device *usbdev = interface_to_usbdev(interface);
	struct sur40_state *sur40;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	struct input_dev *input;
	int error;

	/* Check if we really have the right interface. */
	iface_desc = interface->cur_altsetting;
	if (iface_desc->desc.bInterfaceClass != 0xFF)
		return -ENODEV;

	if (iface_desc->desc.bNumEndpoints < 5)
		return -ENODEV;

	/* Use endpoint #4 (0x86) for touch data ... */
	endpoint = &iface_desc->endpoint[4].desc;
	if (endpoint->bEndpointAddress != TOUCH_ENDPOINT ||
	    !usb_endpoint_is_bulk_in(endpoint))
		return -ENODEV;

	/* ... and endpoint #2 (0x82) for video data. */
	if (iface_desc->endpoint[2].desc.bEndpointAddress != VIDEO_ENDPOINT ||
	    !usb_endpoint_is_bulk_in(&iface_desc->endpoint[2].desc))
		return -ENODEV;

	/* Allocate memory for our device state and initialize it. */
	sur40 = kzalloc_obj(*sur40);
	if (!sur40)
		return -ENOMEM;

	/*
	 * Keep both devices alive until the last file handle is closed: the
	 * URBs reference the USB device and vb2 unmaps its buffers with
	 * queue.dev (the interface) possibly long after disconnect.
	 */
	sur40->usbdev = usb_get_dev(usbdev);
	sur40->dev = get_device(&interface->dev);
	sur40->touch_pkt_size = usb_endpoint_maxp(endpoint);
	sur40->video_pkt_size =
		usb_endpoint_maxp(&iface_desc->endpoint[2].desc);

	/* initialize locks/lists */
	INIT_LIST_HEAD(&sur40->buf_list);
	spin_lock_init(&sur40->qlock);
	mutex_init(&sur40->lock);
	mutex_init(&sur40->pipe_lock);

	error = sur40_alloc_urbs(sur40);
	if (error) {
		dev_err(&interface->dev, "Unable to allocate URBs.");
		goto err_free_dev;
	}

	input = input_allocate_device();
	if (!input) {
		error = -ENOMEM;
		goto err_free_urbs;
	}

	/* Set up regular input device structure */
	input->name = DRIVER_LONG;
	usb_to_input_id(usbdev, &input->id);
	usb_make_path(usbdev, sur40->phys, sizeof(sur40->phys));
	strlcat(sur40->phys, "/input0", sizeof(sur40->phys));
	input->phys = sur40->phys;
	input->dev.parent = &interface->dev;

	input->open = sur40_open;
	input->close = sur40_close;

	error = sur40_input_setup_events(input);
	if (error)
		goto err_free_input;

	input_set_drvdata(input, sur40);
	sur40->input = input;

	/* register the input device */
	error = input_register_device(input);
	if (error) {
		dev_err(&interface->dev, "Unable to register input device.");
		goto err_free_input;
	}

	/* register the video master device */
	snprintf(sur40->v4l2.name, sizeof(sur40->v4l2.name), "%s", DRIVER_LONG);
	sur40->v4l2.release = sur40_release;
	error = v4l2_device_register(sur40->dev, &sur40->v4l2);
	if (error) {
		dev_err(&interface->dev,
			"Unable to register video master device.");
		goto err_unreg_input;
	}

	/* initialize the lock and subdevice */
	sur40->queue = sur40_queue;
	sur40->queue.drv_priv = sur40;
	sur40->queue.lock = &sur40->lock;
	sur40->queue.dev = sur40->dev;

	/* initialize the queue */
	error = vb2_queue_init(&sur40->queue);
	if (error)
		goto err_unreg_v4l2;

	sur40->pix_fmt = sur40_pix_format[0];
	sur40->vdev = sur40_video_device;
	sur40->vdev.v4l2_dev = &sur40->v4l2;
	sur40->vdev.lock = &sur40->lock;
	sur40->vdev.queue = &sur40->queue;
	video_set_drvdata(&sur40->vdev, sur40);

	/* initialize the control handler for 4 controls */
	v4l2_ctrl_handler_init(&sur40->hdl, 4);
	sur40->v4l2.ctrl_handler = &sur40->hdl;
	sur40->vsvideo = (SUR40_CONTRAST_DEF << 4) | SUR40_GAIN_DEF;

	v4l2_ctrl_new_std(&sur40->hdl, &sur40_ctrl_ops, V4L2_CID_BRIGHTNESS,
	  SUR40_BRIGHTNESS_MIN, SUR40_BRIGHTNESS_MAX, 1, clamp(brightness,
	  (uint)SUR40_BRIGHTNESS_MIN, (uint)SUR40_BRIGHTNESS_MAX));

	v4l2_ctrl_new_std(&sur40->hdl, &sur40_ctrl_ops, V4L2_CID_CONTRAST,
	  SUR40_CONTRAST_MIN, SUR40_CONTRAST_MAX, 1, clamp(contrast,
	  (uint)SUR40_CONTRAST_MIN, (uint)SUR40_CONTRAST_MAX));

	v4l2_ctrl_new_std(&sur40->hdl, &sur40_ctrl_ops, V4L2_CID_GAIN,
	  SUR40_GAIN_MIN, SUR40_GAIN_MAX, 1, clamp(gain,
	  (uint)SUR40_GAIN_MIN, (uint)SUR40_GAIN_MAX));

	v4l2_ctrl_new_std(&sur40->hdl, &sur40_ctrl_ops,
	  V4L2_CID_BACKLIGHT_COMPENSATION, SUR40_BACKLIGHT_MIN,
	  SUR40_BACKLIGHT_MAX, 1, SUR40_BACKLIGHT_DEF);

	v4l2_ctrl_handler_setup(&sur40->hdl);

	if (sur40->hdl.error) {
		dev_err(&interface->dev,
			"Unable to register video controls.");
		error = sur40->hdl.error;
		goto err_free_ctrl;
	}

	/* the video node holds its own reference on the v4l2 device */
	v4l2_device_get(&sur40->v4l2);
	error = video_register_device(&sur40->vdev, VFL_TYPE_TOUCH, -1);
	if (error) {
		dev_err(&interface->dev,
			"Unable to register video subdevice.");
		v4l2_device_put(&sur40->v4l2);
		goto err_free_ctrl;
	}

	/* we can register the device now, as it is ready */
	usb_set_intfdata(interface, sur40);
	dev_dbg(&interface->dev, "%s is now attached\n", DRIVER_DESC);

	return 0;

err_free_ctrl:
	v4l2_ctrl_handler_free(&sur40->hdl);
err_unreg_v4l2:
	v4l2_device_unregister(&sur40->v4l2);
err_unreg_input:
	input_unregister_device(input);
	input = NULL;
err_free_input:
	input_free_device(input);
err_free_urbs:
	sur40_free_urbs(sur40);
err_free_dev:
	put_device(sur40->dev);
	usb_put_dev(sur40->usbdev);
	kfree(sur40);

	return error;
}

/* Unregister device & clean up. */
static void sur40_disconnect(struct usb_interface *interface)
{
	struct sur40_state *sur40 = usb_get_intfdata(interface);
	int i;

	/*
	 * Kill everything that is in flight first. Afterwards the stop
	 * helpers become no-ops (the URBs stay poisoned until they are
	 * freed in sur40_release).
	 */
	for (i = 0; i < SUR40_TOUCH_URBS; i++)
		usb_poison_urb(sur40->touch_urbs[i]);
	usb_poison_urb(sur40->video_hdr_urb);
	usb_poison_urb(sur40->video_frame_urb);
	usb_poison_urb(sur40->video_drain_urb);
	sur40->disconnected = true;

	/*
	 * Wake up userspace waiting in DQBUF, then drop the video node
	 * and disconnect the v4l2 device.
	 */
	vb2_queue_error(&sur40->queue);
	video_unregister_device(&sur40->vdev);
	v4l2_device_disconnect(&sur40->v4l2);

	input_unregister_device(sur40->input);

	usb_set_intfdata(interface, NULL);
	dev_dbg(&interface->dev, "%s is now disconnected\n", DRIVER_DESC);

	/* state is freed once the last open file handle is closed */
	v4l2_device_put(&sur40->v4l2);
}

/*
 * Setup the constraints of the queue: set the number of planes per
 * buffer and the size and allocation context of each plane. The
 * minimum buffer count is already enforced by vb2 through
 * min_queued_buffers.
 */
static int sur40_queue_setup(struct vb2_queue *q,
		       unsigned int *nbuffers, unsigned int *nplanes,
		       unsigned int sizes[], struct device *alloc_devs[])
{
	struct sur40_state *sur40 = vb2_get_drv_priv(q);

	if (*nplanes)
		return sizes[0] < sur40->pix_fmt.sizeimage ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = sur40->pix_fmt.sizeimage;

	return 0;
}

/*
 * Prepare the buffer for queueing to the DMA engine: check and set the
 * payload size.
 */
static int sur40_buffer_prepare(struct vb2_buffer *vb)
{
	struct sur40_state *sur40 = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long size = sur40->pix_fmt.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(&sur40->usbdev->dev, "buffer too small (%lu < %lu)\n",
			 vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);
	return 0;
}

/*
 * Queue this buffer to the DMA engine.
 */
static void sur40_buffer_queue(struct vb2_buffer *vb)
{
	struct sur40_state *sur40 = vb2_get_drv_priv(vb->vb2_queue);
	struct sur40_buffer *buf = (struct sur40_buffer *)vb;
	unsigned long flags;

	spin_lock_irqsave(&sur40->qlock, flags);
	list_add_tail(&buf->list, &sur40->buf_list);
	spin_unlock_irqrestore(&sur40->qlock, flags);
}

static void return_all_buffers(struct sur40_state *sur40,
			       enum vb2_buffer_state state)
{
	struct sur40_buffer *buf, *node;
	unsigned long flags;

	spin_lock_irqsave(&sur40->qlock, flags);
	list_for_each_entry_safe(buf, node, &sur40->buf_list, list) {
		vb2_buffer_done(&buf->vb.vb2_buf, state);
		list_del(&buf->list);
	}
	spin_unlock_irqrestore(&sur40->qlock, flags);
}

/*
 * Start streaming: make sure the touch pipeline is running (the device
 * interleaves both streams) and queue the first video header URB.
 */
static int sur40_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct sur40_state *sur40 = vb2_get_drv_priv(vq);
	int ret;

	ret = sur40_pipeline_get(sur40);
	if (ret)
		goto err;

	sur40->sequence = 0;
	sur40->frames_dropped = 0;
	sur40->cur_buf = NULL;

	ret = sur40_video_start(sur40);
	if (ret) {
		sur40_pipeline_put(sur40);
		goto err;
	}

	return 0;

err:
	return_all_buffers(sur40, VB2_BUF_STATE_QUEUED);
	return ret;
}

/*
 * Stop streaming: cancel the video URBs (this waits for a running
 * completion handler), then hand every buffer we still own back to vb2
 * marked as STATE_ERROR.
 */
static void sur40_stop_streaming(struct vb2_queue *vq)
{
	struct sur40_state *sur40 = vb2_get_drv_priv(vq);

	sur40_video_stop(sur40);

	if (sur40->cur_buf) {
		vb2_buffer_done(&sur40->cur_buf->vb.vb2_buf,
				VB2_BUF_STATE_ERROR);
		sur40->cur_buf = NULL;
	}

	/* Release all active buffers */
	return_all_buffers(sur40, VB2_BUF_STATE_ERROR);

	if (sur40->frames_dropped)
		dev_dbg(&sur40->usbdev->dev, "%lu frames dropped while streaming\n",
			sur40->frames_dropped);

	sur40_pipeline_put(sur40);
}

/* V4L ioctl */
static int sur40_vidioc_querycap(struct file *file, void *priv,
				 struct v4l2_capability *cap)
{
	struct sur40_state *sur40 = video_drvdata(file);

	strscpy(cap->driver, DRIVER_SHORT, sizeof(cap->driver));
	strscpy(cap->card, DRIVER_LONG, sizeof(cap->card));
	usb_make_path(sur40->usbdev, cap->bus_info, sizeof(cap->bus_info));
	return 0;
}

static int sur40_vidioc_enum_input(struct file *file, void *priv,
				   struct v4l2_input *i)
{
	if (i->index != 0)
		return -EINVAL;
	i->type = V4L2_INPUT_TYPE_TOUCH;
	i->std = V4L2_STD_UNKNOWN;
	strscpy(i->name, "In-Cell Sensor", sizeof(i->name));
	i->capabilities = 0;
	return 0;
}

static int sur40_vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	return (i == 0) ? 0 : -EINVAL;
}

static int sur40_vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int sur40_vidioc_try_fmt(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	switch (f->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_GREY:
		f->fmt.pix = sur40_pix_format[1];
		break;

	default:
		f->fmt.pix = sur40_pix_format[0];
		break;
	}

	return 0;
}

static int sur40_vidioc_s_fmt(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct sur40_state *sur40 = video_drvdata(file);

	if (vb2_is_busy(&sur40->queue))
		return -EBUSY;

	switch (f->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_GREY:
		sur40->pix_fmt = sur40_pix_format[1];
		break;

	default:
		sur40->pix_fmt = sur40_pix_format[0];
		break;
	}

	f->fmt.pix = sur40->pix_fmt;
	return 0;
}

static int sur40_vidioc_g_fmt(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct sur40_state *sur40 = video_drvdata(file);

	f->fmt.pix = sur40->pix_fmt;
	return 0;
}

static int sur40_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sur40_state *sur40  = container_of(ctrl->handler,
	  struct sur40_state, hdl);
	u8 value = sur40->vsvideo;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		sur40_set_irlevel(sur40, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		value = (value & 0x0f) | (ctrl->val << 4);
		sur40_set_vsvideo(sur40, value);
		break;
	case V4L2_CID_GAIN:
		value = (value & 0xf0) | (ctrl->val);
		sur40_set_vsvideo(sur40, value);
		break;
	case V4L2_CID_BACKLIGHT_COMPENSATION:
		sur40_set_preprocessor(sur40, ctrl->val);
		break;
	}
	return 0;
}

static int sur40_ioctl_parm(struct file *file, void *priv,
			    struct v4l2_streamparm *p)
{
	if (p->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	p->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	p->parm.capture.timeperframe.numerator = 1;
	p->parm.capture.timeperframe.denominator = 60;
	p->parm.capture.readbuffers = 3;
	return 0;
}

static int sur40_vidioc_enum_fmt(struct file *file, void *priv,
				 struct v4l2_fmtdesc *f)
{
	if (f->index >= ARRAY_SIZE(sur40_pix_format))
		return -EINVAL;

	f->pixelformat = sur40_pix_format[f->index].pixelformat;
	f->flags = 0;
	return 0;
}

static int sur40_vidioc_enum_framesizes(struct file *file, void *priv,
					struct v4l2_frmsizeenum *f)
{
	struct sur40_state *sur40 = video_drvdata(file);

	if ((f->index != 0) || ((f->pixel_format != V4L2_TCH_FMT_TU08)
		&& (f->pixel_format != V4L2_PIX_FMT_GREY)))
		return -EINVAL;

	f->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	f->discrete.width  = sur40->pix_fmt.width;
	f->discrete.height = sur40->pix_fmt.height;
	return 0;
}

static int sur40_vidioc_enum_frameintervals(struct file *file, void *priv,
					    struct v4l2_frmivalenum *f)
{
	struct sur40_state *sur40 = video_drvdata(file);

	if ((f->index > 0) || ((f->pixel_format != V4L2_TCH_FMT_TU08)
		&& (f->pixel_format != V4L2_PIX_FMT_GREY))
		|| (f->width  != sur40->pix_fmt.width)
		|| (f->height != sur40->pix_fmt.height))
		return -EINVAL;

	f->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	f->discrete.denominator  = 60;
	f->discrete.numerator = 1;
	return 0;
}


static const struct usb_device_id sur40_table[] = {
	{ USB_DEVICE(ID_MICROSOFT, ID_SUR40) },  /* Samsung SUR40 */
	{ }                                      /* terminating null entry */
};
MODULE_DEVICE_TABLE(usb, sur40_table);

/* V4L2 structures */
static const struct vb2_ops sur40_queue_ops = {
	.queue_setup		= sur40_queue_setup,
	.buf_prepare		= sur40_buffer_prepare,
	.buf_queue		= sur40_buffer_queue,
	.start_streaming	= sur40_start_streaming,
	.stop_streaming		= sur40_stop_streaming,
};

static const struct vb2_queue sur40_queue = {
	.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	/*
	 * VB2_USERPTR in currently not enabled: passing a user pointer to
	 * dma-sg will result in segment sizes that are not a multiple of
	 * 512 bytes, which is required by the host controller.
	*/
	.io_modes = VB2_MMAP | VB2_READ | VB2_DMABUF,
	.buf_struct_size = sizeof(struct sur40_buffer),
	.ops = &sur40_queue_ops,
	.mem_ops = &vb2_dma_sg_memops,
	.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC,
	.min_queued_buffers = 3,
};

static const struct v4l2_file_operations sur40_video_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.read = vb2_fop_read,
	.mmap = vb2_fop_mmap,
	.poll = vb2_fop_poll,
};

static const struct v4l2_ioctl_ops sur40_video_ioctl_ops = {

	.vidioc_querycap	= sur40_vidioc_querycap,

	.vidioc_enum_fmt_vid_cap = sur40_vidioc_enum_fmt,
	.vidioc_try_fmt_vid_cap	= sur40_vidioc_try_fmt,
	.vidioc_s_fmt_vid_cap	= sur40_vidioc_s_fmt,
	.vidioc_g_fmt_vid_cap	= sur40_vidioc_g_fmt,

	.vidioc_enum_framesizes = sur40_vidioc_enum_framesizes,
	.vidioc_enum_frameintervals = sur40_vidioc_enum_frameintervals,

	.vidioc_g_parm = sur40_ioctl_parm,
	.vidioc_s_parm = sur40_ioctl_parm,

	.vidioc_enum_input	= sur40_vidioc_enum_input,
	.vidioc_g_input		= sur40_vidioc_g_input,
	.vidioc_s_input		= sur40_vidioc_s_input,

	.vidioc_reqbufs		= vb2_ioctl_reqbufs,
	.vidioc_create_bufs	= vb2_ioctl_create_bufs,
	.vidioc_querybuf	= vb2_ioctl_querybuf,
	.vidioc_qbuf		= vb2_ioctl_qbuf,
	.vidioc_dqbuf		= vb2_ioctl_dqbuf,
	.vidioc_expbuf		= vb2_ioctl_expbuf,

	.vidioc_streamon	= vb2_ioctl_streamon,
	.vidioc_streamoff	= vb2_ioctl_streamoff,
};

static const struct video_device sur40_video_device = {
	.name = DRIVER_LONG,
	.fops = &sur40_video_fops,
	.ioctl_ops = &sur40_video_ioctl_ops,
	.release = sur40_video_device_release,
	.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_TOUCH |
		       V4L2_CAP_READWRITE | V4L2_CAP_STREAMING,
};

/* USB-specific object needed to register this driver with the USB subsystem. */
static struct usb_driver sur40_driver = {
	.name = DRIVER_SHORT,
	.probe = sur40_probe,
	.disconnect = sur40_disconnect,
	.id_table = sur40_table,
};

module_usb_driver(sur40_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
