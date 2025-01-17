/*
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008
 * The ACX100 Open Source Project <acx100-devel@lists.sourceforge.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * USB support for TI ACX100/TNETW1450 based devices. Many parts are taken from
 * the PCI driver.
 *
 * Authors:
 *  Martin Wawro <martin.wawro AT uni-dortmund.de>
 *  Andreas Mohr <andi AT lisas.de>
 *
 * LOCKING
 *  callback functions called by USB core are running in interrupt context
 *  and thus have names with _i_.
 */
#define ACX_MAC80211_USB 1

#include <linux/version.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <linux/vmalloc.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/nl80211.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) || LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 40)
#include <linux/ratelimit.h>
#endif

#include <net/iw_handler.h>
#include <net/mac80211.h>

#include "acx.h"

/* OW, 20091205, TODO, Info on TNETW1450 support:
 * Firmware loads, device shows activity, however RX and TX paths are broken.
 * The RX path problem possibly related to urb,rxbuffer handling (in acxusb_i_complete_rx).
 *
 * Observed as followed: (Too) big packet to mac80211 and subsequent machine crash (e.g. double faults, etc.)
 * acx: rx: CTL/UNKNOWN time:620888198 len:65528 signal:20 SNR:100 macstat:AB phystat:44 phyrate:134 status:0
 */

/*
 * BOM Config
 * ==================================================
 */

/* ACX100 (TNETW1100) USB device: D-Link DWL-120+ */
#define ACX100_VENDOR_ID 			0x2001
#define ACX100_PRODUCT_ID_UNBOOTED 	0x3B01
#define ACX100_PRODUCT_ID_BOOTED 	0x3B00

/* TNETW1450 USB devices */
#define VENDOR_ID_DLINK		0x07b8	/* D-Link Corp. */
#define PRODUCT_ID_WUG2400	0xb21a	/* AboCom WUG2400 or SafeCom SWLUT-54125 */
#define VENDOR_ID_AVM_GMBH	0x057c
#define PRODUCT_ID_AVM_WLAN_USB		0x5601
#define PRODUCT_ID_AVM_WLAN_USB_si	0x6201  /* "self install" named Version:
											 * driver kills kernel on inbound scans from fritz box ??  */
#define VENDOR_ID_ZCOM			0x0cde
#define PRODUCT_ID_ZCOM_XG750	0x0017	/* not tested yet */
#define VENDOR_ID_TI			0x0451
#define PRODUCT_ID_TI_UNKNOWN	0x60c5	/* not tested yet */

// Module Data
static const struct usb_device_id acxusb_ids[] = {
	{USB_DEVICE(ACX100_VENDOR_ID, ACX100_PRODUCT_ID_BOOTED)},
	{USB_DEVICE(ACX100_VENDOR_ID, ACX100_PRODUCT_ID_UNBOOTED)},
	{USB_DEVICE(VENDOR_ID_DLINK, PRODUCT_ID_WUG2400)},
	{USB_DEVICE(VENDOR_ID_AVM_GMBH, PRODUCT_ID_AVM_WLAN_USB)},
	{USB_DEVICE(VENDOR_ID_AVM_GMBH, PRODUCT_ID_AVM_WLAN_USB_si)},
	{USB_DEVICE(VENDOR_ID_ZCOM, PRODUCT_ID_ZCOM_XG750)},
	{USB_DEVICE(VENDOR_ID_TI, PRODUCT_ID_TI_UNKNOWN)},
	{}
};
MODULE_DEVICE_TABLE(usb, acxusb_ids);

/*
 * BOM Prototypes
 * ... static and also none-static for overview reasons (maybe not best practice ...)
 * ==================================================
 */
 
// Logging

// Data Access

// Firmware, EEPROM, Phy
int acxusb_read_phy_reg(acx_device_t * adev, u32 reg, u8 * charbuf);
int acxusb_write_phy_reg(acx_device_t * adev, u32 reg, u8 value);
static void acxusb_read_eeprom_version(acx_device_t * adev);
static int acxusb_boot(struct usb_device *usbdev, int is_tnetw1450, int *radio_type);
static inline int acxusb_fw_needs_padding(firmware_image_t *fw_image, unsigned int usb_maxlen);

// CMDs (Control Path)
int acxusb_issue_cmd_timeo_debug(acx_device_t * adev, unsigned cmd, void *buffer, unsigned buflen, unsigned timeout, const char *cmdstr);

// Init, Configure (Control Path)
static int acxusb_fill_configoption(acx_device_t * adev);

// Other (Control Path)

// Proc, Debug
#ifdef UNUSED
static void dump_device(struct usb_device *usbdev);
static void dump_config_descriptor(struct usb_config_descriptor *cd);
static void dump_device_descriptor(struct usb_device_descriptor *dd);
#endif

// Rx Path
static void acxusb_complete_rx(struct urb *);
static void acxusb_poll_rx(acx_device_t * adev, usb_rx_t * rx);

// Tx Path
tx_t *acxusb_alloc_tx(acx_device_t *adev);
void acxusb_dealloc_tx(tx_t * tx_opaque);
void *acxusb_get_txbuf(acx_device_t * adev, tx_t * tx_opaque);
void acxusb_tx_data(acx_device_t *adev, tx_t *tx_opaque, int wlanpkt_len, struct ieee80211_tx_info *ieeectl, struct sk_buff *skb);

// Irq Handling, Timer
void acxusb_irq_work(struct work_struct *work);

// Mac80211 Ops
static int acxusb_op_start(struct ieee80211_hw *);
static void acxusb_op_stop(struct ieee80211_hw *);

// Helpers
static void acxusb_unlink_urb(struct urb *urb);

// Driver, Module
static int acxusb_probe(struct usb_interface *intf, const struct usb_device_id *devID);
static void acxusb_disconnect(struct usb_interface *intf);

int __init acxusb_init_module(void);
void __exit acxusb_cleanup_module(void);

 
 /*
 * BOM Defines, static vars, etc.
 * ==================================================
 */

/* number of endpoints of an interface */
#define NUM_EP(intf) (intf)->altsetting[0].desc.bNumEndpoints
#define EP(intf, nr) (intf)->altsetting[0].endpoint[(nr)].desc
#define GET_DEV(udev) usb_get_dev((udev))
#define PUT_DEV(udev) usb_put_dev((udev))

/* removed in 2.6.14. We will use fake value for now
 * TODO: maybe we should just remove all lines that include
 * URB_ASYNC_UNLINK somewhere?
*/
#define URB_ASYNC_UNLINK 0

#define ACX_USB_CTRL_TIMEOUT	5500	/* steps in ms */

/* Buffer size for fw upload, same for both ACX100 USB and TNETW1450 */
#define USB_RWMEM_MAXLEN	2048

/* The number of bulk URBs to use */
#define ACX_TX_URB_CNT		8
#define ACX_RX_URB_CNT		2

/* Should be sent to the bulkout endpoint */
#define ACX_USB_REQ_UPLOAD_FW	0x10
#define ACX_USB_REQ_ACK_CS	0x11
#define ACX_USB_REQ_CMD		0x12

#define TXBUFSIZE sizeof(usb_txbuffer_t)
/*
 * Now, this is just plain lying, but the device insists in giving us
 * huge packets. We supply extra space after rxbuffer. Need to understand
 * it better...
 */
#define RXBUFSIZE (sizeof(rxbuffer_t) + \
		   (sizeof(usb_rx_t) - sizeof(struct usb_rx_plain)))


/*
 * BOM Logging
 * ==================================================
 */

/*
 * BOM Data Access
 * ==================================================
 */
 
/*
 * BOM Firmware, EEPROM, Phy
 * ==================================================
 */
int acxusb_read_phy_reg(acx_device_t * adev, u32 reg, u8 * charbuf)
{
	/* mem_read_write_t mem; */

	FN_ENTER;

	printk("acxusb: %s doesn't seem to work yet, disabled.\n", __func__);

	/*
	   mem.addr = cpu_to_le16(reg);
	   mem.type = cpu_to_le16(0x82);
	   mem.len = cpu_to_le32(4);
	   acx_s_issue_cmd(adev, ACX1xx_CMD_MEM_READ, &mem, sizeof(mem));
	   *charbuf = mem.data;
	   log(L_DEBUG, "acx: read radio PHY[0x%04X]=0x%02X\n", reg, *charbuf);
	 */

	FN_EXIT1(OK);
	return OK;
}

int acxusb_write_phy_reg(acx_device_t * adev, u32 reg, u8 value)
{
	mem_read_write_t mem;

	FN_ENTER;

	mem.addr = cpu_to_le16(reg);
	mem.type = cpu_to_le16(0x82);
	mem.len = cpu_to_le32(4);
	mem.data = value;
	acx_issue_cmd(adev, ACX1xx_CMD_MEM_WRITE, &mem, sizeof(mem));
	log(L_DEBUG, "acx: write radio PHY[0x%04X]=0x%02X\n", reg, value);

	FN_EXIT1(OK);
	return OK;
}

/* FIXME: maybe merge it with usual eeprom reading, into common code? */
static void acxusb_read_eeprom_version(acx_device_t * adev)
{
	u8 eeprom_ver[0x8];

	memset(eeprom_ver, 0, sizeof(eeprom_ver));
	acx_interrogate(adev, &eeprom_ver, ACX1FF_IE_EEPROM_VER);

	/* FIXME: which one of those values to take? */
	adev->eeprom_version = eeprom_ver[5];
}

/*
 * acxusb_boot()
 * Inputs:
 *    usbdev -> Pointer to kernel's usb_device structure
 *
 * Returns:
 *  (int) Errorcode or 0 on success
 *
 * This function triggers the loading of the firmware image from harddisk
 * and then uploads the firmware to the USB device. After uploading the
 * firmware and transmitting the checksum, the device resets and appears
 * as a new device on the USB bus (the device we can finally deal with)
 */
static int
acxusb_boot(struct usb_device *usbdev, int is_tnetw1450, int *radio_type)
{
	char filename[sizeof("tiacx1NNusbcRR")];

	firmware_image_t *fw_image = NULL;
	char *usbbuf;
	unsigned int offset;
	unsigned int blk_len, inpipe, outpipe;
	u32 num_processed;
	u32 img_checksum, sum;
	u32 file_size;
	int result = -EIO;
	int i;

	FN_ENTER;

	/* dump_device(usbdev); */

	usbbuf = kmalloc(USB_RWMEM_MAXLEN, GFP_KERNEL);
	if (!usbbuf) {
		printk(KERN_ERR
		       "acx: no memory for USB transfer buffer (%d bytes)\n",
		       USB_RWMEM_MAXLEN);
		result = -ENOMEM;
		goto end;
	}
	if (is_tnetw1450) {
		/* Obtain the I/O pipes */
		outpipe = usb_sndbulkpipe(usbdev, 1);
		inpipe = usb_rcvbulkpipe(usbdev, 2);

		printk(KERN_DEBUG "acx: wait for device ready\n");
		for (i = 0; i <= 2; i++) {
			result = usb_bulk_msg(usbdev, inpipe,
					      usbbuf,
					      USB_RWMEM_MAXLEN,
					      &num_processed, 2000);

			if ((*(u32 *) & usbbuf[4] == 0x40000001)
			    && (*(u16 *) & usbbuf[2] == 0x1)
			    && ((*(u16 *) usbbuf & 0x3fff) == 0)
			    && ((*(u16 *) usbbuf & 0xc000) == 0xc000))
				break;
			acx_mwait(10);
		}
		if (i == 2)
			goto fw_end;

		*radio_type = usbbuf[8];
	} else {
		/* Obtain the I/O pipes */
		outpipe = usb_sndctrlpipe(usbdev, 0);
		inpipe = usb_rcvctrlpipe(usbdev, 0);

		/* FIXME: shouldn't be hardcoded */
		*radio_type = RADIO_0D_MAXIM_MAX2820;
	}

	snprintf(filename, sizeof(filename), "tiacx1%02dusbc%02X",
		 is_tnetw1450 * 11, *radio_type);

	fw_image = acx_read_fw(&usbdev->dev, filename, &file_size);
	if (!fw_image) {
		result = -EIO;
		goto end;
	}
	log(L_INIT, "acx: firmware size: %d bytes\n", file_size);

	img_checksum = le32_to_cpu(fw_image->chksum);

	if (is_tnetw1450) {
		u8 cmdbuf[20];
		const u8 *p;
		u8 need_padding;
		u32 tmplen, val;

		memset(cmdbuf, 0, 16);

		need_padding =
		    acxusb_fw_needs_padding(fw_image, USB_RWMEM_MAXLEN);
		tmplen = need_padding ? file_size - 4 : file_size - 8;
		*(u16 *) & cmdbuf[0] = 0xc000;
		*(u16 *) & cmdbuf[2] = 0x000b;
		*(u32 *) & cmdbuf[4] = tmplen;
		*(u32 *) & cmdbuf[8] = file_size - 8;
		*(u32 *) & cmdbuf[12] = img_checksum;

		result =
		    usb_bulk_msg(usbdev, outpipe, cmdbuf, 16, &num_processed,
				 HZ);
		if (result < 0)
			goto fw_end;

		p = (const u8 *)&fw_image->size;

		/* first calculate checksum for image size part */
		sum = p[0] + p[1] + p[2] + p[3];
		p += 4;

		/* now continue checksum for firmware data part */
		tmplen = le32_to_cpu(fw_image->size);
		for (i = 0; i < tmplen /* image size */ ; i++) {
			sum += *p++;
		}

		if (sum != le32_to_cpu(fw_image->chksum)) {
			printk("acx: FATAL: firmware upload: "
			       "checksums don't match! "
			       "(0x%08x vs. 0x%08x)\n", sum, fw_image->chksum);
			goto fw_end;
		}

		offset = 8;
		while (offset < file_size) {
			blk_len = file_size - offset;
			if (blk_len > USB_RWMEM_MAXLEN) {
				blk_len = USB_RWMEM_MAXLEN;
			}

			log(L_INIT,
			    "acx: uploading firmware (%d bytes, offset=%d)\n",
			    blk_len, offset);
			memcpy(usbbuf, ((u8 *) fw_image) + offset, blk_len);

			p = usbbuf;
			for (i = 0; i < blk_len; i += 4) {
				*(u32 *) p = be32_to_cpu(*(u32 *) p);
				p += 4;
			}

			result =
			    usb_bulk_msg(usbdev, outpipe, usbbuf, blk_len,
					 &num_processed, HZ);
			if ((result < 0) || (num_processed != blk_len))
				goto fw_end;
			offset += blk_len;
		}
		if (need_padding) {
			printk(KERN_DEBUG "acx: send padding\n");
			memset(usbbuf, 0, 4);
			result =
			    usb_bulk_msg(usbdev, outpipe, usbbuf, 4,
					 &num_processed, HZ);
			if ((result < 0) || (num_processed != 4))
				goto fw_end;
		}
		printk(KERN_DEBUG "acx: read firmware upload result\n");
		memset(cmdbuf, 0, 20);	/* additional memset */
		result =
		    usb_bulk_msg(usbdev, inpipe, cmdbuf, 20, &num_processed,
				 2000);
		if (result < 0)
			goto fw_end;
		if (*(u32 *) & cmdbuf[4] == 0x40000003)
			goto fw_end;
		if (*(u32 *) & cmdbuf[4])
			goto fw_end;
		if (*(u16 *) & cmdbuf[16] != 1)
			goto fw_end;

		val = *(u32 *) & cmdbuf[0];
		if ((val & 0x3fff)
		    || ((val & 0xc000) != 0xc000))
			goto fw_end;

		val = *(u32 *) & cmdbuf[8];
		if (val & 2) {
			result =
			    usb_bulk_msg(usbdev, inpipe, cmdbuf, 20,
					 &num_processed, 2000);
			if (result < 0)
				goto fw_end;
			val = *(u32 *) & cmdbuf[8];
		}
		/* yup, no "else" here! */
		if (val & 1) {
			memset(usbbuf, 0, 4);
			result =
			    usb_bulk_msg(usbdev, outpipe, usbbuf, 4,
					 &num_processed, HZ);
			if ((result < 0) || (!num_processed))
				goto fw_end;
		}

		printk("acx: TNETW1450 firmware successfully uploaded\n");
		result = 0;
		goto end;
	      fw_end:
		result = -EIO;
		goto end;
	} else {
		/* ACX100 USB */

		/* now upload the firmware, slice the data into blocks */
		offset = 8;
		while (offset < file_size) {
			blk_len = file_size - offset;
			if (blk_len > USB_RWMEM_MAXLEN) {
				blk_len = USB_RWMEM_MAXLEN;
			}
			log(L_INIT,
			    "acx: uploading firmware (%d bytes, offset=%d)\n",
			    blk_len, offset);
			memcpy(usbbuf, ((u8 *) fw_image) + offset, blk_len);
			result = usb_control_msg(usbdev, outpipe, ACX_USB_REQ_UPLOAD_FW, USB_TYPE_VENDOR | USB_DIR_OUT, (file_size - 8) & 0xffff,	/* value */
						 (file_size - 8) >> 16,	/* index */
						 usbbuf,	/* dataptr */
						 blk_len,	/* size */
						 3000	/* timeout in ms */
			    );
			offset += blk_len;
			if (result < 0) {
				printk(KERN_ERR "acx: error %d while uploading "
				       "the firmware, aborting\n", result);
				goto end;
			}
		}

		/* finally, send the checksum and reboot the device */
		/* does this trigger the reboot? */
		result = usb_control_msg(usbdev, outpipe, ACX_USB_REQ_UPLOAD_FW, USB_TYPE_VENDOR | USB_DIR_OUT, img_checksum & 0xffff,	/* value */
					 img_checksum >> 16,	/* index */
					 NULL,	/* dataptr */
					 0,	/* size */
					 3000	/* timeout in ms */
		    );
		if (result < 0) {
			printk(KERN_ERR "acx: error %d during tx of checksum, "
			       "aborting\n", result);
			goto end;
		}
		result = usb_control_msg(usbdev, inpipe, ACX_USB_REQ_ACK_CS, USB_TYPE_VENDOR | USB_DIR_IN, img_checksum & 0xffff,	/* value */
					 img_checksum >> 16,	/* index */
					 usbbuf,	/* dataptr */
					 8,	/* size */
					 3000	/* timeout in ms */
		    );
		if (result < 0) {
			printk(KERN_ERR "acx: error %d during ACK of checksum, "
			       "aborting\n", result);
			goto end;
		}
		if (*usbbuf != 0x10) {
			printk(KERN_ERR "acx: invalid checksum?\n");
			result = -EINVAL;
			goto end;
		}
		result = 0;
	}

      end:
	vfree(fw_image);
	kfree(usbbuf);

	FN_EXIT1(result);
	return result;
}


static inline
int acxusb_fw_needs_padding(firmware_image_t *fw_image, unsigned int usb_maxlen)
{
	unsigned int num_xfers = ((fw_image->size - 1) / usb_maxlen) + 1;

	return ((num_xfers % 2) == 0);
}

/*
 * BOM CMDs (Control Path)
 * ==================================================
 */

/*
 * acxusb_s_issue_cmd_timeo
 * Excecutes a command in the command mailbox
 *
 * buffer = a pointer to the data.
 * The data must not include 4 byte command header
 */
int
acxusb_issue_cmd_timeo_debug(acx_device_t * adev,
			       unsigned cmd,
			       void *buffer,
			       unsigned buflen,
			       unsigned timeout, const char *cmdstr)
{

	/* TODO: ideally we shall always know how much we need
	** and this shall be 0 */
	#define BOGUS_SAFETY_PADDING 0x40

	/* USB ignores timeout param */

	struct usb_device *usbdev;
	struct {
		u16 cmd;
		u16 status;
		u8 data[1];
	} ACX_PACKED *loc;
	const char *devname;
	int acklen, blocklen, inpipe, outpipe;
	int cmd_status;
	int result;

	FN_ENTER;

	devname = wiphy_name(adev->ieee->wiphy);
	/* no "wlan%%d: ..." please */
	if (!devname || !devname[0] || devname[4] == '%')
		devname = "acx";

	log(L_CTL, "acx: %s: cmd=%s, buflen=%u, type=0x%04X\n",
	    __func__,
	    cmdstr, buflen,
	    buffer ? le16_to_cpu(((acx_ie_generic_t *) buffer)->type) : -1);

	loc = kmalloc(buflen + 4 + BOGUS_SAFETY_PADDING, GFP_KERNEL);
	if (!loc) {
		printk("acx: %s: %s: no memory for data buffer\n", __func__, devname);
		goto bad;
	}

	/* get context from acx_device */
	usbdev = adev->usbdev;

	/* check which kind of command was issued */
	loc->cmd = cpu_to_le16(cmd);
	loc->status = 0;

	/* NB: buflen == frmlen + 4
	**
	** Interrogate: write 8 bytes: (cmd,status,rid,frmlen), then
	**		read (cmd,status,rid,frmlen,data[frmlen]) back
	**
	** Configure: write (cmd,status,rid,frmlen,data[frmlen])
	**
	** Possibly bogus special handling of ACX1xx_IE_SCAN_STATUS removed
	*/

	/* now write the parameters of the command if needed */
	acklen = buflen + 4 + BOGUS_SAFETY_PADDING;
	blocklen = buflen;
	if (buffer && buflen) {
		/* if it's an INTERROGATE command, just pass the length
		 * of parameters to read, as data */
		if (cmd == ACX1xx_CMD_INTERROGATE) {
			blocklen = 4;
			acklen = buflen + 4;
		}
		memcpy(loc->data, buffer, blocklen);
	}
	blocklen += 4;		/* account for cmd,status */

	/* obtain the I/O pipes */
	outpipe = usb_sndctrlpipe(usbdev, 0);
	inpipe = usb_rcvctrlpipe(usbdev, 0);
	log(L_CTL, "acx: trl inpipe=0x%X outpipe=0x%X\n", inpipe, outpipe);
	log(L_CTL, "acx: sending USB control msg (out) (blocklen=%d)\n", blocklen);
	if (acx_debug & L_DATA)
		acx_dump_bytes(loc, blocklen);

	result = usb_control_msg(usbdev, outpipe, ACX_USB_REQ_CMD,	/* request */
				 USB_TYPE_VENDOR | USB_DIR_OUT,	/* requesttype */
				 0,	/* value */
				 0,	/* index */
				 loc,	/* dataptr */
				 blocklen,	/* size */
				 ACX_USB_CTRL_TIMEOUT	/* timeout in ms */
	    );

	if (result == -ENODEV) {
		log(L_CTL, "acx: no device present (unplug?)\n");
		goto good;
	}

	log(L_CTL, "acx: wrote %d bytes\n", result);
	if (result < 0) {
		goto bad;
	}

	/* check for device acknowledge */
	log(L_CTL, "acx: sending USB control msg (in) (acklen=%d)\n", acklen);
	loc->status = 0;	/* delete old status flag -> set to IDLE */
	/* shall we zero out the rest? */
	result = usb_control_msg(usbdev, inpipe, ACX_USB_REQ_CMD,	/* request */
				 USB_TYPE_VENDOR | USB_DIR_IN,	/* requesttype */
				 0,	/* value */
				 0,	/* index */
				 loc,	/* dataptr */
				 acklen,	/* size */
				 ACX_USB_CTRL_TIMEOUT	/* timeout in ms */
	    );
	if (result < 0) {
		printk("acx: %s: %s: USB read error %d\n", __func__, devname, result);
		goto bad;
	}
	if (acx_debug & L_CTL) {
		printk("acx: read %d bytes: ", result);
		acx_dump_bytes(loc, result);
	}

	/*
	   check for result==buflen+4? Was seen:

	interrogate(type:ACX100_IE_DOT11_ED_THRESHOLD,len:4)
	issue_cmd(cmd:ACX1xx_CMD_INTERROGATE,buflen:8,type:4111)
	ctrl inpipe=0x80000280 outpipe=0x80000200
	sending USB control msg (out) (blocklen=8)
	01 00 00 00 0F 10 04 00
	wrote 8 bytes
	sending USB control msg (in) (acklen=12) sizeof(loc->data
	read 4 bytes <==== MUST BE 12!!
	*/

	cmd_status = le16_to_cpu(loc->status);
	if (cmd_status != 1) {
		printk("acx: %s: %s: cmd %s is not SUCCESS: %d (%s)\n",
		       __func__, devname, cmdstr,
		       cmd_status, acx_cmd_status_str(cmd_status));
		goto bad;
	}
	if ((cmd == ACX1xx_CMD_INTERROGATE) && buffer && buflen) {
		memcpy(buffer, loc->data, buflen);
		log(L_CTL, "acx: response frame: cmd=0x%04X status=%d\n",
		    le16_to_cpu(loc->cmd), cmd_status);
	}

  good:
	kfree(loc);
	FN_EXIT1(OK);
	return OK;

  bad:
	/* Give enough info so that callers can avoid
	 ** printing their own diagnostic messages */

	//dump_stack();
	kfree(loc);
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/*
 * BOM Init, Configure (Control Path)
 * ==================================================
 */

/*
 * acxusb_s_fill_configoption
 *
 * temporary helper function to at least fill important cfgopt members with
 * useful replacement values until we figure out how one manages to fetch
 * the configoption struct in the USB device case...
 */
static int acxusb_fill_configoption(acx_device_t * adev)
{
	adev->cfgopt_probe_delay = 200;
	adev->cfgopt_dot11CCAModes = 4;
	adev->cfgopt_dot11Diversity = 1;
	adev->cfgopt_dot11ShortPreambleOption = 1;
	adev->cfgopt_dot11PBCCOption = 1;
	adev->cfgopt_dot11ChannelAgility = 0;
	adev->cfgopt_dot11PhyType = 5;
	adev->cfgopt_dot11TempType = 1;
	return OK;
}


/*
 * BOM Other (Control Path)
 * ==================================================
 */

/*
 * BOM Proc, Debug
 * ==================================================
 */

// OW TODO Could perhaps go into the proc-debug info for usb ?
#if ACX_DEBUG

#ifdef UNUSED
static void dump_device(struct usb_device *usbdev)
{
	int i;
	struct usb_config_descriptor *cd;

	printk("acx: device dump:\n");
	printk("acx:   devnum: %d\n", usbdev->devnum);
	printk("acx:   speed: %d\n", usbdev->speed);
	printk("acx:   tt: 0x%X\n", (unsigned int)(usbdev->tt));
	printk("acx:   ttport: %d\n", (unsigned int)(usbdev->ttport));
	printk("acx:   toggle[0]: 0x%X  toggle[1]: 0x%X\n",
	       (unsigned int)(usbdev->toggle[0]),
	       (unsigned int)(usbdev->toggle[1]));
	/* This saw a change after 2.6.10 */
	printk("acx:   ep_in wMaxPacketSize: ");
	for (i = 0; i < 16; ++i)
		if (usbdev->ep_in[i] != NULL)
			printk("%d:%d ", i,
			       usbdev->ep_in[i]->desc.wMaxPacketSize);
	printk("\n");
	printk("acx:   ep_out wMaxPacketSize: ");
	for (i = 0; i < ARRAY_SIZE(usbdev->ep_out); ++i)
		if (usbdev->ep_out[i] != NULL)
			printk("%d:%d ", i,
			       usbdev->ep_out[i]->desc.wMaxPacketSize);
	printk("\n");
	printk("acx:   parent: 0x%X\n", (unsigned int)usbdev->parent);
	printk("acx:   bus: 0x%X\n", (unsigned int)usbdev->bus);
#ifdef NO_DATATYPE
	printk("  configs: ");
	for (i = 0; i < usbdev->descriptor.bNumConfigurations; i++)
		printk("0x%X ", usbdev->config[i]);
	printk("\n");
#endif
	printk("acx:   actconfig: %p\n", usbdev->actconfig);
	dump_device_descriptor(&usbdev->descriptor);

	cd = &usbdev->config->desc;
	dump_config_descriptor(cd);
}


/***********************************************************************
*/
static void dump_config_descriptor(struct usb_config_descriptor *cd)
{
	printk("acx: Configuration Descriptor:\n");
	if (!cd) {
		printk("acx: NULL\n");
		return;
	}
	printk("acx:  bLength: %d (0x%X)\n", cd->bLength, cd->bLength);
	printk("acx:  bDescriptorType: %d (0x%X)\n", cd->bDescriptorType,
	       cd->bDescriptorType);
	printk("acx:  bNumInterfaces: %d (0x%X)\n", cd->bNumInterfaces,
	       cd->bNumInterfaces);
	printk("acx:  bConfigurationValue: %d (0x%X)\n", cd->bConfigurationValue,
	       cd->bConfigurationValue);
	printk("acx:  iConfiguration: %d (0x%X)\n", cd->iConfiguration,
	       cd->iConfiguration);
	printk("acx:  bmAttributes: %d (0x%X)\n", cd->bmAttributes,
	       cd->bmAttributes);
	/* printk("acx:  MaxPower: %d (0x%X)\n", cd->bMaxPower, cd->bMaxPower); */
}


static void dump_device_descriptor(struct usb_device_descriptor *dd)
{
	printk("Device Descriptor:\n");
	if (!dd) {
		printk("NULL\n");
		return;
	}
	printk("acx:  bLength: %d (0x%X)\n", dd->bLength, dd->bLength);
	printk("acx:  bDescriptortype: %d (0x%X)\n", dd->bDescriptorType,
	       dd->bDescriptorType);
	printk("acx:  bcdUSB: %d (0x%X)\n", dd->bcdUSB, dd->bcdUSB);
	printk("acx:  bDeviceClass: %d (0x%X)\n", dd->bDeviceClass,
	       dd->bDeviceClass);
	printk("acx:  bDeviceSubClass: %d (0x%X)\n", dd->bDeviceSubClass,
	       dd->bDeviceSubClass);
	printk("acx:  bDeviceProtocol: %d (0x%X)\n", dd->bDeviceProtocol,
	       dd->bDeviceProtocol);
	printk("acx:  bMaxPacketSize0: %d (0x%X)\n", dd->bMaxPacketSize0,
	       dd->bMaxPacketSize0);
	printk("acx:  idVendor: %d (0x%X)\n", dd->idVendor, dd->idVendor);
	printk("acx:  idProduct: %d (0x%X)\n", dd->idProduct, dd->idProduct);
	printk("acx:  bcdDevice: %d (0x%X)\n", dd->bcdDevice, dd->bcdDevice);
	printk("acx:  iManufacturer: %d (0x%X)\n", dd->iManufacturer,
	       dd->iManufacturer);
	printk("acx:  iProduct: %d (0x%X)\n", dd->iProduct, dd->iProduct);
	printk("acx:  iSerialNumber: %d (0x%X)\n", dd->iSerialNumber,
	       dd->iSerialNumber);
	printk("acx:  bNumConfigurations: %d (0x%X)\n", dd->bNumConfigurations,
	       dd->bNumConfigurations);
}
#endif /* UNUSED */

#endif /* ACX_DEBUG */


/*
 * BOM Rx Path
 * ==================================================
 */

/*
 * acxusb_i_complete_rx()
 * Inputs:
 *     urb -> pointer to USB request block
 *    regs -> pointer to register-buffer for syscalls (see asm/ptrace.h)
 *
 * This function is invoked by USB subsystem whenever a bulk receive
 * request returns.
 * The received data is then committed to the network stack and the next
 * USB receive is triggered.
 */
static void acxusb_complete_rx(struct urb *urb)
{
	acx_device_t *adev;
	rxbuffer_t *ptr;
	rxbuffer_t *inbuf;
	usb_rx_t *rx;
	int size, remsize, packetsize, rxnum;
	usb_tx_t *tx;
	struct sk_buff *skb;
	struct ieee80211_tx_info *txstatus;
	// unsigned long flags;

	FN_ENTER;

	BUG_ON(!urb->context);

	rx = (usb_rx_t *) urb->context;
	adev = rx->adev;

	// OW, 20100613: A urb call-back is done in_interrupt(), therefore
	// I could image, that no locking is actually required
	// spin_lock_irqsave(&adev->spinlock, flags);

	/*
	 * Happens on disconnect or close. Don't play with the urb.
	 * Don't resubmit it. It will get unlinked by close()
	 */
	if (unlikely(!(adev->dev_state_mask & ACX_STATE_IFACE_UP))) {
		log(L_USBRXTX,
			"acx: rx: device is down, not doing anything\n");
		goto end_unlock;
	}

	inbuf = &rx->bulkin;
	size = urb->actual_length;
	remsize = size;
	rxnum = rx - adev->usb_rx;

	log(L_USBRXTX, "acxusb: RETURN RX (%d) status=%d size=%d\n",
		rxnum, urb->status, size);

	/* Send the URB that's waiting. */
	log(L_USBRXTX, "acxusb: rxnum=%d, sending=%d\n",
		rxnum, rxnum ^ 1);
	acxusb_poll_rx(adev, &adev->usb_rx[rxnum ^ 1]);

	if (unlikely(size > sizeof(rxbuffer_t)))
		log(L_USBRXTX, "acxusb: rx too large: %d, please report\n", size);

	/* check if the transfer was aborted */
	switch (urb->status) {
	case 0:		/* No error */
		break;
	case -EOVERFLOW:
		printk(KERN_ERR "acx: rx data overrun\n");
		adev->rxtruncsize = 0;	/* Not valid anymore. */
		goto end_unlock;
	case -ECONNRESET:
		adev->rxtruncsize = 0;
		goto end_unlock;
	case -ESHUTDOWN:	/* rmmod */
		adev->rxtruncsize = 0;
		goto end_unlock;
	default:
		adev->rxtruncsize = 0;
		adev->stats.rx_errors++;
		printk("acx: rx error (urb status=%d)\n", urb->status);
		goto end_unlock;
	}

	if (unlikely(!size))
		printk("acx: warning, encountered zerolength rx packet\n");

	if (urb->transfer_buffer != inbuf)
		goto end_unlock;

	/* check if previous frame was truncated
	 ** FIXME: this code can only handle truncation
	 ** of consecutive packets!
	 */
	ptr = inbuf;
	if (adev->rxtruncsize) {
		int tail_size;
		ptr = &adev->rxtruncbuf;
		packetsize = RXBUF_BYTES_USED(ptr);

		if (acx_debug & (L_USBRXTX)) {
			printk("acx: handling truncated frame (truncsize=%d, size=%d, "
			       "packetsize(from trunc)=%d)\n",
			       adev->rxtruncsize, size, packetsize);
			acx_dump_bytes(inbuf, RXBUF_HDRSIZE);
			acx_dump_bytes(ptr, RXBUF_HDRSIZE);
		}

		/* bytes needed for rxtruncbuf completion: */
		tail_size = packetsize - adev->rxtruncsize;

		if (size < tail_size) {
			/* there is not enough data to complete this packet,
			 ** simply append the stuff to the truncation buffer
			 */
			memcpy(((char *)ptr) + adev->rxtruncsize, inbuf, size);
			adev->rxtruncsize += size;
			remsize = 0;
		} else {
			/* ok, this data completes the previously
			 ** truncated packet. copy it into a descriptor
			 ** and give it to the rest of the stack        */

			/* append tail to previously truncated part
			 ** NB: adev->rxtruncbuf (pointed to by ptr) can't
			 ** overflow because this is already checked before
			 ** truncation buffer was filled. See below,
			 ** "if (packetsize > sizeof(rxbuffer_t))..." code */
			memcpy(((char *)ptr) + adev->rxtruncsize, inbuf,
			       tail_size);

			if (acx_debug & (L_USBRXTX)) {
				printk("acxusb: full trailing packet + 12 bytes:\n");
				acx_dump_bytes(inbuf, tail_size + RXBUF_HDRSIZE);
			}
			acx_process_rxbuf(adev, ptr);
			adev->rxtruncsize = 0;
			ptr = (rxbuffer_t *) (((char *)inbuf) + tail_size);
			remsize -= tail_size;
		}
		if (acx_debug & (L_USBRXTX))
				printk("acxusb: post-merge size=%d remsize=%d\n", size, remsize);
	}

	/* size = USB data block size
	 ** remsize = unprocessed USB bytes left
	 ** ptr = current pos in USB data block
	 */
	while (remsize) {

		if (remsize < RXBUF_HDRSIZE) {
			printk("acxusb: truncated rx header (%d bytes)!\n",
			       remsize);
			if (ACX_DEBUG)
				acx_dump_bytes(ptr, remsize);
			break;
		}

		packetsize = RXBUF_BYTES_USED(ptr);
		log(L_USBRXTX,
			"acxusb: packet with packetsize=%d\n", packetsize);

		if (RXBUF_IS_TXSTAT(ptr)) {

			/* do rate handling */
			// OW TODO rate handling done by mac80211
			usb_txstatus_t *stat = (void *)ptr;

			log(L_USBRXTX,
					"acx: tx: stat: mac_cnt_rcvd:%04X "
					"queue_index:%02X mac_status:%02X "
					"hostdata:%08X rate:%u ack_failures:%02X "
					"rts_failures:%02X rts_ok:%02X\n",
					stat->mac_cnt_rcvd, stat->queue_index,
					stat->mac_status, stat->hostdata, stat->rate,
					stat->ack_failures, stat->rts_failures,
					stat->rts_ok);

            tx = (usb_tx_t*) (adev->usb_tx + stat->hostdata);
            skb = tx->skb;
    		txstatus = IEEE80211_SKB_CB(skb);

            if (!(txstatus->flags & IEEE80211_TX_CTL_NO_ACK))
    			txstatus->flags |= IEEE80211_TX_STAT_ACK;

        	txstatus->status.rates[0].count = stat->ack_failures + 1;

    		// report upstream
    		ieee80211_tx_status(adev->ieee, skb);

        	tx->busy = 0;
    		adev->tx_free++;

    		if ((adev->tx_free >= TX_START_QUEUE) && acx_queue_stopped(adev->ieee)) {
    			log(L_BUF, "acx: tx: wake queue (avail. Tx desc %u)\n",
    					adev->tx_free);
    			acx_wake_queue(adev->ieee, NULL);
    			ieee80211_queue_work(adev->ieee, &adev->tx_work);
    		}

			goto next;
		}

		if (packetsize > sizeof(rxbuffer_t)) {
			printk("acxusb: packet exceeds max wlan "
			       "frame size (%d > %d). size=%d\n",
			       packetsize, (int)sizeof(rxbuffer_t), size);
			if (ACX_DEBUG& (L_USBRXTX))
				acx_dump_bytes(ptr, 16);
			/* FIXME: put some real error-handling in here! */
			break;
		}

		if (packetsize > remsize) {
			/* frame truncation handling */
			if (acx_debug & (L_USBRXTX)) {
				printk("acxusb: need to truncate packet, "
				       "packetsize=%d remsize=%d "
				       "size=%d bytes:",
				       packetsize, remsize, size);
				acx_dump_bytes(ptr, RXBUF_HDRSIZE);
			}
			memcpy(&adev->rxtruncbuf, ptr, remsize);
			adev->rxtruncsize = remsize;
			break;
		}

		/* packetsize <= remsize */
		/* now handle the received data */
		acx_process_rxbuf(adev, ptr);

		next:
		ptr = (rxbuffer_t *) (((char *)ptr) + packetsize);
		remsize -= packetsize;
		if ((acx_debug & (L_USBRXTX)) && remsize) {
			printk("acx: more than one packet in buffer, "
			       "second packet hdr:");
			acx_dump_bytes(ptr, RXBUF_HDRSIZE);
		}

	}

    end_unlock:
	//spin_unlock_irqrestore(&adev->spinlock, flags);

	/* end: */
	FN_EXIT0;
}

/*
 * acxusb_l_poll_rx
 * This function (re)initiates a bulk-in USB transfer on a given urb
 */
static void acxusb_poll_rx(acx_device_t * adev, usb_rx_t * rx)
{
	struct usb_device *usbdev;
	struct urb *rxurb;
	int errcode, rxnum;
	unsigned int inpipe;

	FN_ENTER;

	rxurb = rx->urb;
	usbdev = adev->usbdev;

	rxnum = rx - adev->usb_rx;

	inpipe = usb_rcvbulkpipe(usbdev, adev->bulkinep);
	if (unlikely(rxurb->status == -EINPROGRESS)) {
		printk(KERN_ERR
		       "acx: error, rx triggered while rx urb in progress\n");
		/* FIXME: this is nasty, receive is being cancelled by this code
		 * on the other hand, this should not happen anyway...
		 */
		usb_unlink_urb(rxurb);
	} else if (unlikely(rxurb->status == -ECONNRESET)) {
		log(L_USBRXTX, "acx: _poll_rx: connection reset\n");
		goto end;
	}
	rxurb->actual_length = 0;
	usb_fill_bulk_urb(rxurb, usbdev, inpipe, &rx->bulkin,	/* dataptr */
			  RXBUFSIZE,	/* size */
			  acxusb_complete_rx,	/* handler */
			  rx	/* handler param */
	    );
	rxurb->transfer_flags = URB_ASYNC_UNLINK;

	/* ATOMIC: we may be called from complete_rx() usb callback */
	errcode = usb_submit_urb(rxurb, GFP_ATOMIC);
	/* FIXME: evaluate the error code! */
	log(L_USBRXTX,
		"acx: SUBMIT RX (%d) inpipe=0x%X size=%d errcode=%d\n",
		rxnum, inpipe, (int)RXBUFSIZE, errcode);
      end:
	FN_EXIT0;
}

/*
static void acxusb_i_set_rx_mode(struct net_device *ndev)
{
}
*/

/*
 * BOM Tx Path
 * ==================================================
 */

/*
 * acxusb_i_complete_tx()
 * Inputs:
 *     urb -> pointer to USB request block
 *    regs -> pointer to register-buffer for syscalls (see asm/ptrace.h)
 *
 * This function is invoked upon termination of a USB transfer.
 */
static void acxusb_complete_tx(struct urb *urb)
{
	acx_device_t *adev;
	usb_tx_t *tx;
	//unsigned long flags;

	FN_ENTER;

	BUG_ON(!urb->context);

	tx = (usb_tx_t *) urb->context;
	adev = tx->adev;

	// OW, 20100613: A urb call-back is done in_interrupt(), therefore
	// I could image, that no locking is actually required
	// spin_lock_irqsave(&adev->spinlock, flags);

	/*
	 * If the iface isn't up, we don't have any right
	 * to play with them. The urb may get unlinked.
	 */
	if (unlikely(!(adev->dev_state_mask & ACX_STATE_IFACE_UP))) {
		printk("acx: tx: device is down, not doing anything\n");
		goto end_unlock;
	}

	//	printk("acx: RETURN TX (%d): status=%d size=%d\n",
	//		txnum, urb->status, urb->actual_length);

	/* handle USB transfer errors */
	switch (urb->status) {
	case 0:		/* No error */
		break;
	case -ESHUTDOWN:
		goto end_unlock;
		break;
	case -ECONNRESET:
		goto end_unlock;
		break;
		/* FIXME: real error-handling code here please */
	default:
		printk(KERN_ERR "acx: tx error, urb status=%d\n", urb->status);
		/* FIXME: real error-handling code here please */
	}

    end_unlock:
	//spin_unlock_irqrestore(&adev->spinlock, flags);

    /* end: */
	FN_EXIT0;
}

/*
 * acxusb_l_alloc_tx
 * Actually returns a usb_tx_t* ptr
 */
tx_t *acxusb_alloc_tx(acx_device_t *adev)
{
	usb_tx_t *tx;
	unsigned head;

	FN_ENTER;

	head = adev->tx_head;
	do {
		head = (head + 1) % ACX_TX_URB_CNT;
		if (!adev->usb_tx[head].busy) {
			log(L_USBRXTX,
				"acx: allocated tx %d\n", head);
			tx = &adev->usb_tx[head];
			tx->busy = 1;
			adev->tx_free--;
			goto end;
		}
	} while (likely(head != adev->tx_head));
	tx = NULL;
	printk_ratelimited("acxusb: tx buffers full\n");

	end:
	adev->tx_head = head;
	FN_EXIT0;
	return (tx_t *) tx;
}

/*
 * Used if alloc_tx()'ed buffer needs to be cancelled without doing tx
 */
void acxusb_dealloc_tx(tx_t * tx_opaque)
{
	usb_tx_t *tx = (usb_tx_t *) tx_opaque;
	tx->busy = 0;
}

void *acxusb_get_txbuf(acx_device_t * adev, tx_t * tx_opaque)
{
	usb_tx_t *tx = (usb_tx_t *) tx_opaque;
	return &tx->bulkout.data;
}

/*
 * acxusb_l_tx_data
 *
 * Can be called from IRQ (rx -> (AP bridging or mgmt response) -> tx).
 * Can be called from acx_i_start_xmit (data frames from net core).
 */
void acxusb_tx_data(acx_device_t *adev, tx_t *tx_opaque, int wlanpkt_len,
		struct ieee80211_tx_info *ieeectl, struct sk_buff *skb) {
	struct usb_device *usbdev;
	struct urb *txurb;
	usb_tx_t *tx;
	usb_txbuffer_t *txbuf;
	//	client_t *clt;
	// FIXME Cleanup ?: struct ieee80211_hdr *whdr;
	unsigned int outpipe;
	int ucode, txnum;
	u8 rate_100;

	FN_ENTER;

	tx = ((usb_tx_t *) tx_opaque);

	tx->skb = skb;

	txurb = tx->urb;
	txbuf = &tx->bulkout;
	// FIXME Cleanup ?: whdr = (struct ieee80211_hdr *) txbuf->data;
	txnum = tx - adev->usb_tx;

	log(L_DEBUG, "acx: using buf#%d free=%d len=%d\n",
	    txnum, adev->tx_free, wlanpkt_len);

	/* fill the USB transfer header */
	txbuf->desc = cpu_to_le16(USB_TXBUF_TXDESC);
	txbuf->mpdu_len = cpu_to_le16(wlanpkt_len);
	txbuf->queue_index = 1;

	// FIXME OW 20100619 acx111 handling missing here
	rate_100 = ieee80211_get_tx_rate(adev->ieee, ieeectl)->bitrate;
	txbuf->rate = rate_100 ;

	txbuf->hostdata = (u32) txnum;

	txbuf->ctrl1 = DESC_CTL_FIRSTFRAG;
	if (1 == adev->preamble_cur)
		SET_BIT(txbuf->ctrl1, DESC_CTL_SHORT_PREAMBLE);
	txbuf->ctrl2 = 0;
	txbuf->data_len = cpu_to_le16(wlanpkt_len);

	if (unlikely(acx_debug & L_DATA)) {
		printk("acx: dump of bulk out urb:\n");
		acx_dump_bytes(txbuf, wlanpkt_len + USB_TXBUF_HDRSIZE);
	}

	if (unlikely(txurb->status == -EINPROGRESS)) {
		printk
		    ("acx: trying to submit tx urb while already in progress\n");
	}

	/* now schedule the USB transfer */
	usbdev = adev->usbdev;
	outpipe = usb_sndbulkpipe(usbdev, adev->bulkoutep);

	usb_fill_bulk_urb(txurb, usbdev, outpipe, txbuf,	/* dataptr */
			  wlanpkt_len + USB_TXBUF_HDRSIZE,	/* size */
			  acxusb_complete_tx,	/* handler */
			  tx	/* handler param */
	    );

	txurb->transfer_flags = URB_ASYNC_UNLINK | URB_ZERO_PACKET;
	ucode = usb_submit_urb(txurb, GFP_ATOMIC);
	log(L_USBRXTX, "acx: SUBMIT TX (%d): outpipe=0x%X buf=%p txsize=%d "
	    "rate=%u errcode=%d\n", txnum, outpipe, txbuf,
	    wlanpkt_len + USB_TXBUF_HDRSIZE, txbuf->rate, ucode);

	if (unlikely(ucode)) {
		printk(KERN_ERR "acx: submit_urb() error=%d txsize=%d\n",
		       ucode, wlanpkt_len + USB_TXBUF_HDRSIZE);

		/* on error, just mark the frame as done and update
		 ** the statistics
		 */
		adev->stats.tx_errors++;
		tx->busy = 0;
		adev->tx_free++;
		/* needed? if (adev->tx_free > TX_START_QUEUE) acx_wake_queue(...) */
	}
	FN_EXIT0;
}

#ifdef HAVE_TX_TIMEOUT
/*
void acxusb_i_tx_timeout(struct net_device *ndev)
{
	acx_device_t *adev = ndev2adev(ndev);
	unsigned long flags;
	int i;

	FN_ENTER;

	acx_lock(adev, flags);
*/	/* unlink the URBs */
/*	for (i = 0; i < ACX_TX_URB_CNT; i++) {
		acxusb_unlink_urb(adev->usb_tx[i].urb);
		adev->usb_tx[i].busy = 0;
	}
	adev->tx_free = ACX_TX_URB_CNT;
*/	/* TODO: stats update */
/*	acx_unlock(adev, flags);

	FN_EXIT0;
}
*/
#endif

/*
 * BOM Irq Handling, Timer
 * ==================================================
 */

void acxusb_irq_work(struct work_struct *work)
{
	acx_device_t *adev =
			container_of(work, struct acx_device, irq_work);

	if (adev->after_interrupt_jobs)
		acx_after_interrupt_task(adev);
}


/*
 * BOM Mac80211 Ops
 * ==================================================
 */

static const struct ieee80211_ops acxusb_hw_ops = {
	.tx = acx_op_tx,
	.conf_tx = acx_conf_tx,
	.add_interface = acx_op_add_interface,
	.remove_interface = acx_op_remove_interface,
	.start = acxusb_op_start,
	.configure_filter = acx_op_configure_filter,
	.stop = acxusb_op_stop,
	.config = acx_op_config,
	.bss_info_changed = acx_op_bss_info_changed,
	.set_key = acx_op_set_key,
	.get_stats = acx_op_get_stats,
#if CONFIG_ACX_MAC80211_VERSION < KERNEL_VERSION(2, 6, 34)
	.get_tx_stats = acx_e_op_get_tx_stats,
#endif
};

/*
 * acxusb_e_start()
 * This function is called when the user sets up the network interface.
 * It initializes a management timer, sets up the USB card and starts
 * the network tx queue and USB receive.
 */
static int acxusb_op_start(struct ieee80211_hw *hw)
{
	acx_device_t *adev = ieee2adev(hw);
	int i;

	FN_ENTER;
	acx_sem_lock(adev);

	adev->initialized = 0;

	/* Reset URBs status */
	for (i = 0; i < ACX_RX_URB_CNT; i++) {
		adev->usb_rx[i].urb->status = 0;
		adev->usb_rx[i].busy = 0;
	}

	for (i = 0; i < ACX_TX_URB_CNT; i++) {
		adev->usb_tx[i].urb->status = 0;
		adev->usb_tx[i].busy = 0;
	}
	adev->tx_free = ACX_TX_URB_CNT;

	/* put the ACX100 out of sleep mode */
	acx_issue_cmd(adev, ACX1xx_CMD_WAKE, NULL, 0);

	init_timer(&adev->mgmt_timer);
	adev->mgmt_timer.function = acx_timer;
	adev->mgmt_timer.data = (unsigned long)adev;

	/* acx_s_start needs it */
	SET_BIT(adev->dev_state_mask, ACX_STATE_IFACE_UP);
	acx_start(adev);

	acxusb_poll_rx(adev, &adev->usb_rx[0]);

	acx_wake_queue(adev->ieee, NULL);

	adev->initialized = 1;

	acx_sem_unlock(adev);

	FN_EXIT0;
	return 0;
}

/*
 * acxusb_e_stop()
 *
 * This function stops the network functionality of the interface (invoked
 * when the user calls ifconfig <wlan> down). The tx queue is halted and
 * the device is marked as down. In case there were any pending USB bulk
 * transfers, these are unlinked (asynchronously). The module in-use count
 * is also decreased in this function.
 */
static void acxusb_op_stop(struct ieee80211_hw *hw)
{
	acx_device_t *adev = ieee2adev(hw);
	int i;

	FN_ENTER;
	acx_sem_lock(adev);

	acx_stop_queue(adev->ieee, "on ifdown");

	/* Make sure we don't get any more rx requests */
	acx_issue_cmd(adev, ACX1xx_CMD_DISABLE_RX, NULL, 0);
	acx_issue_cmd(adev, ACX1xx_CMD_DISABLE_TX, NULL, 0);

	/* Power down the device */
	acx_issue_cmd(adev, ACX1xx_CMD_SLEEP, NULL, 0);

	acx_sem_unlock(adev);
	cancel_work_sync(&adev->irq_work);
	cancel_work_sync(&adev->tx_work);
	acx_sem_lock(adev);

	acx_tx_queue_flush(adev);

	/* stop pending rx/tx urb transfers */
	// OW TODO Maybe we need to report pending skbs in urbs still to mac80211 ? see wl1251 flush
	for (i = 0; i < ACX_TX_URB_CNT; i++) {
		acxusb_unlink_urb(adev->usb_tx[i].urb);
		adev->usb_tx[i].busy = 0;
	}
	for (i = 0; i < ACX_RX_URB_CNT; i++) {
		acxusb_unlink_urb(adev->usb_rx[i].urb);
		adev->usb_rx[i].busy = 0;
	}
	adev->tx_free = ACX_TX_URB_CNT;

	/* Must do this outside of lock */
	del_timer_sync(&adev->mgmt_timer);

	CLEAR_BIT(adev->dev_state_mask, ACX_STATE_IFACE_UP);
	adev->initialized = 0;

	log(L_INIT, "acxusb: closed device\n");

	acx_sem_unlock(adev);

	FN_EXIT0;
}


/*
 * BOM Helpers
 * ==================================================
 */

/*
 * USB helper
 *
 * ldd3 ch13 says:
 * When the function is usb_kill_urb, the urb lifecycle is stopped. This
 * function is usually used when the device is disconnected from the system,
 * in the disconnect callback. For some drivers, the usb_unlink_urb function
 * should be used to tell the USB core to stop an urb. This function does not
 * wait for the urb to be fully stopped before returning to the caller.
 * This is useful for stoppingthe urb while in an interrupt handler or when
 * a spinlock is held, as waiting for a urb to fully stop requires the ability
 * for the USB core to put the calling process to sleep. This function requires
 * that the URB_ASYNC_UNLINK flag value be set in the urb that is being asked
 * to be stopped in order to work properly.
 *
 * (URB_ASYNC_UNLINK is obsolete, usb_unlink_urb will always be
 * asynchronous while usb_kill_urb is synchronous and should be called
 * directly (drivers/usb/core/urb.c))
 *
 * In light of this, timeout is just for paranoid reasons...
 *
 * Actually, it's useful for debugging. If we reach timeout, we're doing
 * something wrong with the urbs.
 */
static void acxusb_unlink_urb(struct urb *urb)
{
	if (!urb)
		return;

	if (urb->status == -EINPROGRESS) {
		int timeout = 10;

		usb_unlink_urb(urb);
		while (--timeout && urb->status == -EINPROGRESS) {
			mdelay(1);
		}
		if (!timeout) {
			printk(KERN_ERR "acx_usb: urb unlink timeout!\n");
		}
	}
}


/*
 * BOM Ioctls
 * ==================================================
 */

/*
 * BOM Driver, Module
 * ==================================================
 */

/* USB driver data structure as required by the kernel's USB core */
static struct usb_driver
 acxusb_driver = {
	.name = "acx_usb",
	.probe = acxusb_probe,
	.disconnect = acxusb_disconnect,
	.id_table = acxusb_ids
};


/*
 * acxusb_e_probe()
 *
 * This function is invoked by the kernel's USB core whenever a new device is
 * attached to the system or the module is loaded. It is presented a usb_device
 * structure from which information regarding the device is obtained and evaluated.
 * In case this driver is able to handle one of the offered devices, it returns
 * a non-null pointer to a driver context and thereby claims the device.
 */
static int
acxusb_probe(struct usb_interface *intf, const struct usb_device_id *devID)
{
	struct usb_device *usbdev = interface_to_usbdev(intf);
	acx_device_t *adev = NULL;
	struct usb_config_descriptor *config;
	struct usb_endpoint_descriptor *epdesc;
	struct usb_host_endpoint *ep;
	struct usb_interface_descriptor *ifdesc;
	const char *msg;
	int numconfigs, numfaces, numep;
	int result = OK;
	int i;
	int err;
	/* yes indeed, the radio_type variable shouldn't be static, but OTOH
	   we read the radio type upon booting (1st .probe()) only, whereas
	   assignment to adev->radio_type can be done on 2nd .probe() only...
	   Ideally we would figure out how to do a USB request to get
	   the radio type of ACX100 cards and query this on second .probe().
	*/
	   
	static int radio_type;
	/* this one needs to be more precise in case there appears
	 * a TNETW1450 from the same vendor */
	int is_tnetw1450 = (usbdev->descriptor.idVendor != ACX100_VENDOR_ID);
	struct ieee80211_hw *ieee;

	FN_ENTER;

	if (is_tnetw1450) {
		/* Boot the device (i.e. upload the firmware) */
		acxusb_boot(usbdev, is_tnetw1450, &radio_type);

		/* TNETW1450-based cards will continue right away with
		 * the same USB ID after booting */
	} else {
		/* First check if this is the "unbooted" hardware */
		if (usbdev->descriptor.idProduct == ACX100_PRODUCT_ID_UNBOOTED) {

			/* Boot the device (i.e. upload the firmware) */
			acxusb_boot(usbdev, is_tnetw1450, &radio_type);

			/* DWL-120+ will first boot the firmware,
			 * then later have a *separate* probe() run
			 * since its USB ID will have changed after
			 * firmware boot!
			 * Since the first probe() run has no
			 * other purpose than booting the firmware,
			 * simply return immediately.
			 */
			log(L_INIT,
			    "acx: finished booting, returning from probe()\n");
			result = OK;	/* success */
			goto end;
		} else {
			if (usbdev->descriptor.idProduct != ACX100_PRODUCT_ID_BOOTED)
				/* device not unbooted, but invalid USB ID!? */
				goto end_nodev;
		}
	}

/* Ok, so it's our device and it has already booted */

	/* Allocate memory for a network device */

	ieee = ieee80211_alloc_hw(sizeof(*adev), &acxusb_hw_ops);
	if (!ieee) {
		msg = "acx: no memory for ieee80211_dev\n";
		goto end_nomem;
	}


	ieee->flags &= ~IEEE80211_HW_RX_INCLUDES_FCS;
	/* TODO: mainline doesn't support the following flags yet */
	/*
	 ~IEEE80211_HW_MONITOR_DURING_OPER &
	 ~IEEE80211_HW_WEP_INCLUDE_IV;
	 */
	ieee->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION)
			| BIT(NL80211_IFTYPE_ADHOC);
	ieee->queues = 1;
	// OW TODO Check if RTS/CTS threshold can be included here

	// We base signal quality on winlevel approach of previous driver
	// TODO OW 20100615 This should into a common init code
	ieee->flags |= IEEE80211_HW_SIGNAL_UNSPEC;
	ieee->max_signal = 100;

	/* Setup private driver context */

	adev = ieee2adev(ieee);
	adev->ieee = ieee;

	adev->dev_type = DEVTYPE_USB;
	adev->radio_type = radio_type;
	if (is_tnetw1450) {
		/* well, actually it's a TNETW1450, but since it
		 * seems to be sufficiently similar to TNETW1130,
		 * I don't want to change large amounts of code now */
		adev->chip_type = CHIPTYPE_ACX111;
	} else {
		adev->chip_type = CHIPTYPE_ACX100;
	}

	adev->usbdev = usbdev;
	spin_lock_init(&adev->spinlock);	/* initial state: unlocked */
	mutex_init(&adev->mutex);

	/* Check that this is really the hardware we know about.
	 ** If not sure, at least notify the user that he
	 ** may be in trouble...
	 */
	numconfigs = (int)usbdev->descriptor.bNumConfigurations;
	if (numconfigs != 1)
		printk("acx: number of configurations is %d, "
		       "this driver only knows how to handle 1, "
		       "be prepared for surprises\n", numconfigs);

	config = &usbdev->config->desc;
	numfaces = config->bNumInterfaces;
	if (numfaces != 1)
		printk("acx: number of interfaces is %d, "
		       "this driver only knows how to handle 1, "
		       "be prepared for surprises\n", numfaces);

	ifdesc = &intf->altsetting->desc;
	numep = ifdesc->bNumEndpoints;
	log(L_DEBUG, "acx: # of endpoints: %d\n", numep);

	if (is_tnetw1450) {
		adev->bulkoutep = 1;
		adev->bulkinep = 2;
	} else {
		/* obtain information about the endpoint
		 ** addresses, begin with some default values
		 */
		adev->bulkoutep = 1;
		adev->bulkinep = 1;
		for (i = 0; i < numep; i++) {
			ep = usbdev->ep_in[i];
			if (!ep)
				continue;
			epdesc = &ep->desc;
			if (epdesc->bmAttributes & USB_ENDPOINT_XFER_BULK) {
				if (epdesc->bEndpointAddress & 0x80)
					adev->bulkinep =
					    epdesc->bEndpointAddress & 0xF;
				else
					adev->bulkoutep =
					    epdesc->bEndpointAddress & 0xF;
			}
		}
	}
	log(L_DEBUG, "acx: bulkout ep: 0x%X\n", adev->bulkoutep);
	log(L_DEBUG, "acx: bulkin ep: 0x%X\n", adev->bulkinep);

	/* already done by memset: adev->rxtruncsize = 0; */
	log(L_DEBUG, "acx: TXBUFSIZE=%d RXBUFSIZE=%d\n",
	    (int)TXBUFSIZE, (int)RXBUFSIZE);

	/* Allocate the RX/TX containers. */
	adev->usb_tx = kmalloc(sizeof(usb_tx_t) * ACX_TX_URB_CNT, GFP_KERNEL);
	if (!adev->usb_tx) {
		msg = "acx: no memory for tx container";
		goto end_nomem;
	}
	adev->usb_rx = kmalloc(sizeof(usb_rx_t) * ACX_RX_URB_CNT, GFP_KERNEL);
	if (!adev->usb_rx) {
		msg = "acx: no memory for rx container";
		goto end_nomem;
	}

	/* Setup URBs for bulk-in/out messages */
	for (i = 0; i < ACX_RX_URB_CNT; i++) {
		adev->usb_rx[i].urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!adev->usb_rx[i].urb) {
			msg = "acx: no memory for input URB\n";
			goto end_nomem;
		}
		adev->usb_rx[i].urb->status = 0;
		adev->usb_rx[i].adev = adev;
		adev->usb_rx[i].busy = 0;
	}

	for (i = 0; i < ACX_TX_URB_CNT; i++) {
		adev->usb_tx[i].urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!adev->usb_tx[i].urb) {
			msg = "acx: no memory for output URB\n";
			goto end_nomem;
		}
		adev->usb_tx[i].urb->status = 0;
		adev->usb_tx[i].adev = adev;
		adev->usb_tx[i].busy = 0;
	}
	adev->tx_free = ACX_TX_URB_CNT;

	usb_set_intfdata(intf, adev);
	SET_IEEE80211_DEV(ieee, &intf->dev);

	/* TODO: move all of fw cmds to open()? But then we won't know our MAC addr
	   until ifup (it's available via reading ACX1xx_IE_DOT11_STATION_ID)... */

	acx_init_task_scheduler(adev);

	// Mac80211 Tx_queue
	INIT_WORK(&adev->tx_work, acx_tx_work);
	skb_queue_head_init(&adev->tx_queue);

	/* put acx out of sleep mode and initialize it */
	acx_issue_cmd(adev, ACX1xx_CMD_WAKE, NULL, 0);

	result = acx_init_mac(adev);
	if (result)
		goto end;

	/* TODO: see similar code in pci.c */
	acxusb_read_eeprom_version(adev);
	acxusb_fill_configoption(adev);
	acx_set_defaults(adev);
	acx_get_firmware_version(adev);
	acx_display_hardware_details(adev);

/*	MAC_COPY(ndev->dev_addr, adev->dev_addr); */

	err = acx_setup_modes(adev);
	if (err) {
		msg = "acx: can't register hwmode\n";
		goto end_nomem;
	}

	/* Register the network device */
	log(L_INIT, "acx: registering network device\n");
	result = ieee80211_register_hw(adev->ieee);
	if (result) {
		msg = "acx: failed to register USB network device "
		    "(error %d)\n";
		goto end_nomem;
	}

	acx_proc_register_entries(ieee);

	printk("acx: USB module loaded successfully\n");

#if CMD_DISCOVERY
	great_inquisitor(adev);
#endif

	/* Everything went OK, we are happy now */
	result = OK;
	goto end;

  end_nomem:
	printk(msg, result);

	if (ieee) {
		if (adev->usb_rx) {
			for (i = 0; i < ACX_RX_URB_CNT; i++)
				usb_free_urb(adev->usb_rx[i].urb);
			kfree(adev->usb_rx);
		}
		if (adev->usb_tx) {
			for (i = 0; i < ACX_TX_URB_CNT; i++)
				usb_free_urb(adev->usb_tx[i].urb);
			kfree(adev->usb_tx);
		}
		ieee80211_free_hw(ieee);
	}

	result = -ENOMEM;
	goto end;

  end_nodev:
	/* no device we could handle, return error. */
	result = -EIO;

  end:
	FN_EXIT1(result);
	return result;
}

/*
 * acxusb_e_disconnect()
 *
 * This function is invoked whenever the user pulls the plug from the USB
 * device or the module is removed from the kernel. In these cases, the
 * network devices have to be taken down and all allocated memory has
 * to be freed.
 */
static void acxusb_disconnect(struct usb_interface *intf)
{
	int i;
	acx_device_t *adev = usb_get_intfdata(intf);

	FN_ENTER;

	/* No WLAN device... no sense */
	if (!adev)
		goto end;

	/* Unregister network device
	 *
	 * If the interface is up, unregister_netdev() will take
	 * care of calling our close() function, which takes
	 * care of unlinking the urbs, sending the device to
	 * sleep, etc...
	 * This can't be called with sem or lock held because
	 * _close() will try to grab it as well if it's called,
	 * deadlocking the machine.
	 */
	acx_proc_unregister_entries(adev->ieee);
	ieee80211_unregister_hw(adev->ieee);

	acx_sem_lock(adev);
	/* This device exists no more */
	usb_set_intfdata(intf, NULL);

	/*
	 * Here we only free them. _close() took care of
	 * unlinking them.
	 */
	for (i = 0; i < ACX_RX_URB_CNT; ++i) {
		usb_free_urb(adev->usb_rx[i].urb);
	}
	for (i = 0; i < ACX_TX_URB_CNT; ++i) {
		usb_free_urb(adev->usb_tx[i].urb);
	}

	/* Freeing containers */
	kfree(adev->usb_rx);
	kfree(adev->usb_tx);

	acx_sem_unlock(adev);

	ieee80211_free_hw(adev->ieee);

	end:
	FN_EXIT0;
}

/*
 * init_module()
 *
 * This function is invoked upon loading of the kernel module.
 * It registers itself at the kernel's USB subsystem.
 *
 * Returns: Errorcode on failure, 0 on success
 */
int __init acxusb_init_module(void)
{
	log(L_INIT, "acx: USB module initialized, "
	    "probing for devices...\n");
	return usb_register(&acxusb_driver);
}



/*
 * cleanup_module()
 *
 * This function is invoked as last step of the module unloading. It simply
 * deregisters this module at the kernel's USB subsystem.
 */
void __exit acxusb_cleanup_module(void)
{
	usb_deregister(&acxusb_driver);
	log(L_INIT, "acx: USB module unloaded\n");
}

