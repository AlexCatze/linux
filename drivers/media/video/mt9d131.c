/*
 * Micron Camera driver
 * (c)2007 Stretch, Inc.
 * (c)2008 emlix GmbH <info@emlix.com>
 * Authors:	Fabian Godehardt <fg@emlix.com>
 *		Oskar Schirmer <os@emlix.com>
 *		Daniel GlÃ¶ckner <dg@emlix.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/time.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/stddef.h>
#include <linux/videodev2.h>

#define MT9D131_REG_SENSOR 0	/* direct access (Table 5) */
#define MT9D131_REG_IFP1 1	/* direct access (Table 6) */
#define MT9D131_REG_IFP2 2	/* direct access (Table 7) */
#define MT9D131_REG_JPEG 3	/* indirect access, IFP2 reg 30/31 (Table 8) */
#define MT9D131_REG8_UPROC 4	/* indirect access, IFP1 reg 0xc6/0xc8 (8 bits) */
#define MT9D131_REG8_UPROC_seq 5	/* indirect, IFP1 reg 0xc6/0xc8 (Table 10) */
#define MT9D131_REG8_UPROC_ae 6	/* indirect, IFP1 reg 0xc6/0xc8 (Table 11) */
#define MT9D131_REG8_UPROC_wb 7	/* indirect, IFP1 reg 0xc6/0xc8 (Table 11) */
#define MT9D131_REG8_UPROC_mode 11	/* indirect, IFP1 reg 0xc6/0xc8 (Table 12) */
#define MT9D131_REG8_UPROC_jpeg 13	/* indirect, IFP1 reg 0xc6/0xc8 (Table 13) */
#define MT9D131_REG16_UPROC 24	/* indirect, IFP1 reg 0xc6/0xc8 (16 bits) */
#define MT9D131_REG16_UPROC_seq 25
#define MT9D131_REG16_UPROC_ae 26
#define MT9D131_REG16_UPROC_wb 27
#define MT9D131_REG16_UPROC_mode 31
#define MT9D131_REG16_UPROC_jpeg 33

#define MT9D131_CONTEXT_A	0
#define MT9D131_CONTEXT_B	1
#define MT9D131_NB_CONTEXTS	2

#define MT9D131_CORE_ROWSTART		0x01
#define MT9D131_CORE_COLSTART		0x02
#define MT9D131_CORE_ROWWIDTH		0x03
#define MT9D131_CORE_COLWIDTH		0x04
#define MT9D131_CORE_HBLANKB		0x05
#define MT9D131_CORE_VBLANKB		0x06
#define MT9D131_CORE_HBLANKA		0x07
#define MT9D131_CORE_VBLANKA		0x08
#define MT9D131_CORE_SHUTTERWIDTH	0x09
#define MT9D131_CORE_ROWSPEED		0x0A
#define MT9D131_CORE_READMODEB		0x20
#define MT9D131_CORE_READMODEA		0x21
#define MT9D131_CORE_GREEN1GAIN		0x2B
#define MT9D131_CORE_BLUEGAIN		0x2C
#define MT9D131_CORE_REDGAIN		0x2D
#define MT9D131_CORE_GREEN2GAIN		0x2E
#define MT9D131_CORE_CLKCTRL		0x65
#define MT9D131_CORE_PLLCTRL1		0x66
#define MT9D131_CORE_PLLCTRL2		0x67

#define MT9D131_IFP1_CROPWINDOWX0	0x11
#define MT9D131_IFP1_CROPWINDOWX1	0x12
#define MT9D131_IFP1_CROPWINDOWY0	0x13
#define MT9D131_IFP1_CROPWINDOWY1	0x14
#define MT9D131_IFP1_HORZDECIMATIONWGT	0x16
#define MT9D131_IFP1_VERTDECIMATIONWGT	0x17
#define MT9D131_IFP1_OUTPUTFMTCONFIG	0x97
#define MT9D131_IFP1_YUVCTRL		0xBE
#define MT9D131_IFP1_YRGBOFFSET		0xBF
#define MT9D131_IFP1_UPROCVARADDR	0xC6
#define MT9D131_IFP1_UPROCVARADDR_ADDR		0
#define MT9D131_IFP1_UPROCVARADDR_ADDR_MASK		0xFF
#define MT9D131_IFP1_UPROCVARADDR_DRVID		8
#define MT9D131_IFP1_UPROCVARADDR_DRVID_MASK		0x1F
#define MT9D131_IFP1_UPROCVARADDR_LOGICAL	13
#define MT9D131_IFP1_UPROCVARADDR_BYTEWISE	15
#define MT9D131_IFP1_UPROCVARADDR_JPEG		0
#define MT9D131_IFP1_UPROCVARADDR_JPEG_MASK		0x7FF
#define MT9D131_IFP1_UPROCVARADDR_I2CBURST	13
#define MT9D131_IFP1_UPROCVARADDR_WRITE		14
#define MT9D131_IFP1_UPROCVARADDR_AUTOINCR	15
#define MT9D131_IFP1_UPROCVARDATA	0xC8

#define MT9D131_IFP2_JPEGINDIRECTDATA	0x1F

#define MT9D131_COMMON_PAGEREGISTER	0xF0
#define MT9D131_COMMON_BYTEWISEADDR	0xF1

#define MT9D131_MAXSENSOR_WIDTH		1600
#define MT9D131_MAXSENSOR_HEIGHT	1200

#define MT9D131_VBLANK_MIN		11
#define MT9D131_HBLANK_MIN		286

struct mt9d131_settings {
	/* sensor/crop/output geometries */
	unsigned sensor_w, sensor_h;	/* height/width of sensor area read-out */
	unsigned sensor_x0, sensor_y0;	/* informational only */
	unsigned crop_w, crop_h;	/* crop window, height and width */
	unsigned crop_x0, crop_y0;	/* informational only */
	unsigned output_w, output_h;	/* output window, height and width */
	unsigned std_num;
	enum v4l2_colorspace colorspace;
	struct v4l2_subdev subdev;
};

struct mt9d131_device {
	int current_reg_set;
	struct mt9d131_settings current_settings;
};

static int
raw_read_register(struct i2c_client *client, u8 reg_addr, u16 * ret_data)
{
	s32 ret;
	ret = i2c_smbus_read_word_data(client, reg_addr);
	if (ret < 0)
		return ret;
	*ret_data = swab16(ret);
	return 0;
}

static int
raw_write_register(struct i2c_client *client, u8 reg_addr, u16 write_data)
{
	return i2c_smbus_write_word_data(client, reg_addr, swab16(write_data));
}

static inline void mt9d131_switch_page(struct i2c_client *client, int reg_set)
{
	struct mt9d131_device *dev = i2c_get_clientdata(client);
	if (dev->current_reg_set != reg_set) {
		raw_write_register(client, MT9D131_COMMON_PAGEREGISTER,
				   reg_set);
		dev->current_reg_set = reg_set;
	}
}

static int
mt9d131_write_register(struct i2c_client *client, int reg_set,
		       u32 reg_offset, u16 wdata)
{
	if (reg_set >= MT9D131_REG8_UPROC) {
		mt9d131_switch_page(client, MT9D131_REG_IFP1);
		if (reg_set >= MT9D131_REG16_UPROC)
			raw_write_register(client, MT9D131_IFP1_UPROCVARADDR,
					   ((reg_offset
					     &
					     MT9D131_IFP1_UPROCVARADDR_ADDR_MASK)
					    << MT9D131_IFP1_UPROCVARADDR_ADDR) |
					   (((reg_set - MT9D131_REG16_UPROC)
					     &
					     MT9D131_IFP1_UPROCVARADDR_DRVID_MASK)
					    << MT9D131_IFP1_UPROCVARADDR_DRVID)
					   | (1 <<
					      MT9D131_IFP1_UPROCVARADDR_LOGICAL)
					   | (0 <<
					      MT9D131_IFP1_UPROCVARADDR_BYTEWISE));
		else
			raw_write_register(client, MT9D131_IFP1_UPROCVARADDR,
					   ((reg_offset
					     &
					     MT9D131_IFP1_UPROCVARADDR_ADDR_MASK)
					    << MT9D131_IFP1_UPROCVARADDR_ADDR) |
					   (((reg_set - MT9D131_REG8_UPROC)
					     &
					     MT9D131_IFP1_UPROCVARADDR_DRVID_MASK)
					    << MT9D131_IFP1_UPROCVARADDR_DRVID)
					   | (1 <<
					      MT9D131_IFP1_UPROCVARADDR_LOGICAL)
					   | (1 <<
					      MT9D131_IFP1_UPROCVARADDR_BYTEWISE));
		return raw_write_register(client, MT9D131_IFP1_UPROCVARDATA,
					  wdata);
	}
	if (reg_set == MT9D131_REG_JPEG) {
		BUG();		/* broken? */
		mt9d131_switch_page(client, MT9D131_REG_IFP2);
		raw_write_register(client, MT9D131_IFP2_JPEGINDIRECTDATA,
				   wdata);
		return raw_write_register(client, MT9D131_IFP1_UPROCVARADDR,
					  ((reg_offset &
					    MT9D131_IFP1_UPROCVARADDR_JPEG_MASK)
					   << MT9D131_IFP1_UPROCVARADDR_JPEG) |
					  (0 <<
					   MT9D131_IFP1_UPROCVARADDR_I2CBURST) |
					  (1 << MT9D131_IFP1_UPROCVARADDR_WRITE)
					  | (0 <<
					     MT9D131_IFP1_UPROCVARADDR_AUTOINCR));
	}
	mt9d131_switch_page(client, reg_set);
	return raw_write_register(client, reg_offset, wdata);
}

static int
mt9d131_read_register(struct i2c_client *client, int reg_set,
		      u32 reg_offset, u16 * rdata)
{
	if (!rdata)
		return -EINVAL;
	*rdata = 0;
	if (reg_set >= MT9D131_REG8_UPROC) {
		mt9d131_switch_page(client, MT9D131_REG_IFP1);
		if (reg_set >= MT9D131_REG16_UPROC)
			raw_write_register(client, MT9D131_IFP1_UPROCVARADDR,
					   ((reg_offset
					     &
					     MT9D131_IFP1_UPROCVARADDR_ADDR_MASK)
					    << MT9D131_IFP1_UPROCVARADDR_ADDR) |
					   (((reg_set - MT9D131_REG16_UPROC)
					     &
					     MT9D131_IFP1_UPROCVARADDR_DRVID_MASK)
					    << MT9D131_IFP1_UPROCVARADDR_DRVID)
					   | (1 <<
					      MT9D131_IFP1_UPROCVARADDR_LOGICAL)
					   | (0 <<
					      MT9D131_IFP1_UPROCVARADDR_BYTEWISE));
		else
			raw_write_register(client, MT9D131_IFP1_UPROCVARADDR,
					   ((reg_offset
					     &
					     MT9D131_IFP1_UPROCVARADDR_ADDR_MASK)
					    << MT9D131_IFP1_UPROCVARADDR_ADDR) |
					   (((reg_set - MT9D131_REG8_UPROC)
					     &
					     MT9D131_IFP1_UPROCVARADDR_DRVID_MASK)
					    << MT9D131_IFP1_UPROCVARADDR_DRVID)
					   | (1 <<
					      MT9D131_IFP1_UPROCVARADDR_LOGICAL)
					   | (1 <<
					      MT9D131_IFP1_UPROCVARADDR_BYTEWISE));
		return raw_read_register(client, MT9D131_IFP1_UPROCVARDATA,
					 rdata);
	}

	if (reg_set == MT9D131_REG_JPEG) {
		BUG();		/* broken? */
		mt9d131_switch_page(client, MT9D131_REG_IFP2);
		raw_write_register(client, MT9D131_IFP1_UPROCVARADDR,
				   ((reg_offset
				     & MT9D131_IFP1_UPROCVARADDR_JPEG_MASK)
				    << MT9D131_IFP1_UPROCVARADDR_JPEG) |
				   (0 << MT9D131_IFP1_UPROCVARADDR_I2CBURST) |
				   (0 << MT9D131_IFP1_UPROCVARADDR_WRITE) |
				   (0 << MT9D131_IFP1_UPROCVARADDR_AUTOINCR));
		return raw_read_register(client, MT9D131_IFP2_JPEGINDIRECTDATA,
					 rdata);
	}
	mt9d131_switch_page(client, reg_set);
	return raw_read_register(client, reg_offset, rdata);
}

/**
    Setup the PLL inside MT9D131
    Parameters:
	"dev" - The device handle, returned by sx_mt9d131_get_device()
	"extclk_freq" - The frequency of the clock at the EXTCLK input.
	"desired_fout_freq" - The desired output frequency
 */
static int
mt9d131_setup_pll(struct i2c_client *client,
		  u32 extclk_freq, u32 desired_fout_freq)
{
	u32 div;
	u32 N, M, P;
	u32 f_vco;
	u16 reg16;

	if (desired_fout_freq > 80 || desired_fout_freq < 6)
		return -EINVAL;

	if (extclk_freq > 64 || extclk_freq < 6)
		return -EINVAL;

	/* the VCO center is 175MHz. Aim for that. */
	div = 175 / desired_fout_freq;
	P = div / 2 - 1;
	f_vco = desired_fout_freq * 2 * (P + 1);

	/*
	 * Calculate M and N, subject to:
	 *     f_in       N+1
	 *     ----   =  -----
	 *     f_vco       M
	 */
	M = f_vco;
	N = extclk_freq;

	while (!(N % 2) && !(M % 2) && M >= 16 * 2) {
		N /= 2;
		M /= 2;
	}
	while (!(N % 3) && !(M % 3) && M >= 16 * 3) {
		N /= 3;
		M /= 3;
	}
	N--;

	/*
	 * Now that all the parameters are determined, program the PLL and
	 * power it up.
	 */
	mt9d131_write_register(client, MT9D131_REG_SENSOR,
			       MT9D131_CORE_PLLCTRL1, M << 8 | N);
	mt9d131_read_register(client, MT9D131_REG_SENSOR,
			      MT9D131_CORE_PLLCTRL2, &reg16);
	reg16 &= ~0x7f;		/* bits[6:0] = P */
	reg16 |= (P & 0x7f);
	mt9d131_write_register(client, MT9D131_REG_SENSOR,
			       MT9D131_CORE_PLLCTRL2, reg16);

	/* power up the PLL */
	mt9d131_read_register(client, MT9D131_REG_SENSOR,
			      MT9D131_CORE_CLKCTRL, &reg16);
	reg16 &= ~(1 << 14);	/* powerdown bit */
	mt9d131_write_register(client, MT9D131_REG_SENSOR,
			       MT9D131_CORE_CLKCTRL, reg16);

	/* wait for it to lock and settle */
	mdelay(1);
	udelay(150);

	/* turn off PLL bypass */
	reg16 &= ~(1 << 15);	/* bypass bit */
	mt9d131_write_register(client, MT9D131_REG_SENSOR,
			       MT9D131_CORE_CLKCTRL, reg16);
	return 0;
}

/*******************************************************************************
    Set the size and position of the cropping window, and the size of the output
    window.  The size of the cropping window must be larger than or equal to the
    size of the output window.  It also must be smaller than or equal to the
    size of the sensor FOV (set by sx_mt9d131_set_sensor_fov)

    Parameters:
	"dev" - The device handle, returned by sx_mt9d131_get_device()
	"crop_x0"
	"crop_y0" - The upper left corner of the crop window
	"crop_width_x"
	"crop_width_y" - The size of the crop window
	"out_width_x"
	"out_width_y" - The size of the crop window
*******************************************************************************/
static int
mt9d131_set_window(struct i2c_client *client,
		   u32 crop_x0, u32 crop_y0,
		   u32 crop_width_x, u32 crop_height_y,
		   u32 out_width_x, u32 out_height_y)
{
	struct mt9d131_device *dev = i2c_get_clientdata(client);
	int context;

	if ((crop_width_x < out_width_x) || (crop_height_y < out_height_y))
		return -EINVAL;

	if (((crop_width_x + crop_x0) > dev->current_settings.sensor_w) ||
	    ((crop_height_y + crop_y0) > dev->current_settings.sensor_h))
		return -EINVAL;

	/*
	 * save the crop/output window size and position into the
	 * current settings.
	 */
	dev->current_settings.crop_w = crop_width_x;
	dev->current_settings.crop_h = crop_height_y;
	dev->current_settings.crop_x0 = crop_x0;
	dev->current_settings.crop_y0 = crop_y0;
	dev->current_settings.output_w = out_width_x;
	dev->current_settings.output_h = out_height_y;

	/*
	 * upload the settings into both context A and B.
	 * This ensures that switches between the two (required when FOV
	 * is changed). Do not cause output dimension glitchs.
	 * This function wants to assume that it is called while
	 * the sensor is in context B. Therefore, it should be
	 * able to update the crop/output windows for A.
	 */
	for (context = MT9D131_CONTEXT_B; context <= MT9D131_CONTEXT_B;
	     context++) {
		mt9d131_write_register(client, MT9D131_REG16_UPROC_mode, 5 + 4 * context,	/* mode.height_a/b */
				       out_height_y);
		mt9d131_write_register(client, MT9D131_REG16_UPROC_mode, 3 + 4 * context,	/* mode.width_a/b */
				       out_width_x);
	}

	for (context = MT9D131_CONTEXT_B; context <= MT9D131_CONTEXT_B;
	     context++) {
		mt9d131_write_register(client, MT9D131_REG16_UPROC_mode, 39 + 14 * context,	/* mode.crop_x0_a/b */
				       crop_x0);
		mt9d131_write_register(client, MT9D131_REG16_UPROC_mode, 41 + 14 * context,	/* mode.crop_x1_a/b */
				       crop_x0 + crop_width_x);
		mt9d131_write_register(client, MT9D131_REG16_UPROC_mode, 43 + 14 * context,	/* mode.crop_y0_a/b */
				       crop_y0);
		mt9d131_write_register(client, MT9D131_REG16_UPROC_mode, 45 + 14 * context,	/* mode.crop_y1_a/b */
				       crop_y0 + crop_height_y);
	}

	return 0;
}

/*******************************************************************************
    Internal: write to the seq.cmd variable and wait for its effect to propagate

    Parameters:
	"dev" - The device handle, returned by sx_mt9d131_get_device()
	"cmd" - command to write
*******************************************************************************/
static void mt9d131_seq_cmd(struct i2c_client *client, u8 cmd)
{
	mt9d131_write_register(client, MT9D131_REG8_UPROC_seq, 3, cmd);
}

/*******************************************************************************
    Internal: Set the context (A or B) of the MT9D131 sensor

    Parameters:
	"dev" - The device handle, returned by sx_mt9d131_get_device()
	"context" - either MT9D131_CONTEXT_A or MT9D131_CONTEXT_B
*******************************************************************************/
static void mt9d131_set_context(struct i2c_client *client, int context)
{
	if (context == MT9D131_CONTEXT_A) {
		mt9d131_write_register(client,	/* seq.captureParams = 0 */
				       MT9D131_REG8_UPROC_seq, 0x20, 0);
		mt9d131_seq_cmd(client, 1);	/* seq.cmd = do_preview */
	} else {
		mt9d131_write_register(client,	/* seq.captureParams = 2 */
				       MT9D131_REG8_UPROC_seq, 0x20, 2);
		mt9d131_seq_cmd(client, 2);	/* seq.cmd = do_capture */
	}

	mdelay(750);
}

/*******************************************************************************
    Set the size and position of the sensor FOV.
    The FOV must be within the native sensor resolution, and its size must be
    smaller than or equal to the crop window.

    Parameters:
	"dev" - The device handle, returned by sx_mt9d131_get_device()
	"row_start"
	"col_start" - The upper left corner of the FOV
	"width_x"
	"height_y" - The size of the FOV
*******************************************************************************/
static int
mt9d131_set_sensor_fov(struct i2c_client *client,
		       u32 row_start, u32 col_start, u32 width_x, u32 height_y)
{
	struct mt9d131_device *dev = i2c_get_clientdata(client);
	int context;

	/*
	 * sensor height/width must be a multiple of 2.  Otherwise, the
	 * colours will be shifted.
	 */
	row_start &= ~1;
	col_start &= ~1;
	width_x &= ~1;
	height_y &= ~1;
	if (((width_x + row_start) > MT9D131_MAXSENSOR_WIDTH) ||
	    ((height_y + col_start) > MT9D131_MAXSENSOR_HEIGHT)) {
		return -EINVAL;
	}

	if ((width_x < dev->current_settings.crop_w) ||
	    (height_y < dev->current_settings.crop_h)) {
		return -EINVAL;
	}

	/* save the FOV size and position into the current settings. */
	dev->current_settings.sensor_w = width_x;
	dev->current_settings.sensor_h = height_y;
	dev->current_settings.sensor_x0 = row_start;
	dev->current_settings.sensor_y0 = col_start;

	/* change to preview mode (context A) */
	mt9d131_set_context(client, MT9D131_CONTEXT_A);

	for (context = MT9D131_CONTEXT_B; context <= MT9D131_CONTEXT_B;
	     context++) {
		/*
		 * (28,60) is added to row/col start to account for
		 * the black region in the border
		 */
		mt9d131_write_register(client, MT9D131_REG16_UPROC_mode,
				       15 + 12 * context, row_start + 28);
		mt9d131_write_register(client, MT9D131_REG16_UPROC_mode,
				       17 + 12 * context, col_start + 60);
		mt9d131_write_register(client, MT9D131_REG16_UPROC_mode,
				       19 + 12 * context, height_y);
		mt9d131_write_register(client, MT9D131_REG16_UPROC_mode,
				       21 + 12 * context, width_x);
	}

	/* switch back to context B */
	mt9d131_set_context(client, MT9D131_CONTEXT_B);

	return 0;
}

/*******************************************************************************
    Set the size and position of the sensor FOV, crop window, and output
    window, all at the same time.  This is normally used at init time

    Parameters:
	"dev" - The device handle, returned by sx_mt9d131_get_device()
	"sensor_x0"
	"sensor_y0" - The upper left corner of the FOV
	"sensor_width_x"
	"sensor_height_y" - The size of the FOV
	"crop_x0"
	"crop_y0" - The upper left corner of the crop window
	"crop_width_x"
	"crop_height_y" - The size of the FOVcrop window
	"out_width_x"
	"out_height_y" - The size of the FOVcrop window
*******************************************************************************/
static int
mt9d131_set_fov_and_window(struct i2c_client *client,
			   u32 sensor_x0, u32 sensor_y0,
			   u32 sensor_width_x, u32 sensor_height_y,
			   u32 crop_x0, u32 crop_y0,
			   u32 crop_width_x, u32 crop_height_y,
			   u32 out_width_x, u32 out_height_y)
{
	struct mt9d131_device *dev = i2c_get_clientdata(client);
	u32 stat;

	/* First thing to check is the validity of the crop window size. */
	if ((crop_width_x < out_width_x) || (crop_width_x > sensor_width_x)) {
		return -EINVAL;
	}

	if ((crop_height_y < out_height_y) || (crop_height_y > sensor_height_y)) {
		return -EINVAL;
	}

	/*
	 * save the sensor geometry (that we are about to change to)
	 * This prevents mt9d131_set_window from failing the crop
	 * size check
	 */
	dev->current_settings.sensor_w = sensor_width_x;
	dev->current_settings.sensor_h = sensor_height_y;

	/*
	 * since we've already checked the sanity of the crop window size,
	 * this should never fail
	 */
	stat = mt9d131_set_window(client,
				  crop_x0, crop_y0,
				  crop_width_x, crop_height_y,
				  out_width_x, out_height_y);

	if (stat) {
		printk(KERN_ERR "error - invalid crop window size\n");
		return -EINVAL;
	}

	/* Now, set the sensor FOV.  This should also never fail. */
	return mt9d131_set_sensor_fov(client,
				      sensor_x0, sensor_y0,
				      sensor_width_x, sensor_height_y);
}

/*******************************************************************************
    Turn on auto-exposure

    Parameters:
	"dev" - The device handle, returned by sx_mt9d131_get_device()
*******************************************************************************/
static void mt9d131_auto_exposure_on(struct i2c_client *client)
{
	u16 mode;

	mt9d131_read_register(client, MT9D131_REG8_UPROC_seq, 2, &mode);
	mode |= 0x01;		/* set bit 0, which enables the AE driver */
	mt9d131_write_register(client, MT9D131_REG8_UPROC_seq, 2, mode);
}

static int
mt9d131_set_colorspace(struct i2c_client *client, enum v4l2_colorspace s)
{
	struct mt9d131_device *dev = i2c_get_clientdata(client);
	u16 val;

	switch (s) {
	case V4L2_COLORSPACE_470_SYSTEM_BG:
		val = 0xf;
		break;
	case V4L2_COLORSPACE_REC709:
		val = 0xd;
		break;
	case V4L2_COLORSPACE_JPEG:
		val = 0x6;
		break;
	default:
		return -EINVAL;
	}
	mt9d131_write_register(client, MT9D131_REG_IFP1, MT9D131_IFP1_YUVCTRL,
			       val);
	dev->current_settings.colorspace = s;
	return 0;
}

/*******************************************************************************
    Internal: common init
*******************************************************************************/
static void mt9d131_common_init(struct i2c_client *client)
{
	struct mt9d131_device *dev = i2c_get_clientdata(client);
	/* mode.mode_config = 0x30  (disable JPEG on both A and B) */
	mt9d131_write_register(client, MT9D131_REG16_UPROC_mode, 11, 0x30);

	/* mode.fifo_config1_b = 0x501 (set N1 to 1, PCLK1_slew=7) */
	mt9d131_write_register(client, MT9D131_REG16_UPROC_mode, 116, 0x5E1);

	mt9d131_write_register(client, MT9D131_REG_SENSOR,
			       MT9D131_CORE_HBLANKB, MT9D131_HBLANK_MIN);
	mt9d131_write_register(client, MT9D131_REG_SENSOR,
			       MT9D131_CORE_VBLANKB, MT9D131_VBLANK_MIN);
	mt9d131_write_register(client, MT9D131_REG_SENSOR,
			       MT9D131_CORE_HBLANKA, MT9D131_HBLANK_MIN);
	mt9d131_write_register(client, MT9D131_REG_SENSOR,
			       MT9D131_CORE_VBLANKA, MT9D131_VBLANK_MIN);

	mt9d131_set_context(client, MT9D131_CONTEXT_B);

	mt9d131_write_register(client, MT9D131_REG8_UPROC_ae, 0x17,	/* ae.IndexTH23 */
			       4);

	mt9d131_write_register(client, MT9D131_REG8_UPROC_ae, 0x0E,	/* ae.maxIndex */
			       4);
	mt9d131_write_register(client, MT9D131_REG_SENSOR,
			       MT9D131_CORE_SHUTTERWIDTH,
			       dev->current_settings.output_h - 1);

	mt9d131_seq_cmd(client, 5);	/* seq.cmd = refresh */

	mdelay(500);
	mt9d131_auto_exposure_on(client);
}

static struct {
	const char *name;
	unsigned width;
	unsigned height;
} standards[] = {
	{
	"fixed 29.97fps", 1192, 892}, {
	"fixed 25fps", 1322, 984}, {
	"fixed min fps", MT9D131_MAXSENSOR_WIDTH, MT9D131_MAXSENSOR_HEIGHT},
	{
	"as fast as possible", 0, 0}
};

static void mt9d131_reconfigure(struct i2c_client *client)
{
	struct mt9d131_device *dev = i2c_get_clientdata(client);
	struct mt9d131_settings *cur = &dev->current_settings;

	mt9d131_set_fov_and_window(client, cur->sensor_x0, cur->sensor_y0,
				   cur->sensor_w, cur->sensor_h,
				   cur->crop_x0, cur->crop_y0,
				   cur->crop_w, cur->crop_h,
				   cur->output_w, cur->output_h);
	mt9d131_seq_cmd(client, 5);	/* seq.cmd = refresh */
}

static int mt9d131_enum_input(void *ctx, struct v4l2_input *inp)
{
	if (inp->index)
		return -EINVAL;
	strcpy(inp->name, "Micron MT9D131");
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = (1 << ARRAY_SIZE(standards)) - 1;
	inp->std <<= 32;
	return 0;
}

static int mt9d131_s_input(void *ctx, unsigned i, int busy)
{
	return i ? -EINVAL : 0;
}

static int mt9d131_enum_std(void *ctx, struct v4l2_standard *std)
{
	if (std->index >= ARRAY_SIZE(standards))
		return -EINVAL;
	std->id = 1 << std->index;
	std->id <<= 32;		/* upper 32 bits are custom standards */
	strcpy(std->name, standards[std->index].name);
	std->framelines = MT9D131_MAXSENSOR_HEIGHT;
	if (standards[std->index].width) {
		std->frameperiod.numerator = (standards[std->index].width
					      + MT9D131_HBLANK_MIN) *
		    (standards[std->index].height + MT9D131_VBLANK_MIN);
		std->frameperiod.denominator = 40000000;	/* at 80MHz */
	}
	return 0;
}

static int mt9d131_s_std(void *ctx, v4l2_std_id * mask, int busy)
{
	struct i2c_client *client = ctx;
	struct mt9d131_device *dev = i2c_get_clientdata(client);
	struct mt9d131_settings *cur = &dev->current_settings;
	int i;

	if (busy)
		return -EBUSY;

	for (i = 0; i < ARRAY_SIZE(standards); i++)
		if (*mask & ((v4l2_std_id) 1 << (32 + i)))
			break;

	if (i == ARRAY_SIZE(standards))
		return -EINVAL;

	*mask = (v4l2_std_id) 1 << (32 + i);
	cur->std_num = i;
	cur->sensor_x0 += cur->crop_x0;
	cur->sensor_y0 += cur->crop_y0;
	if (standards[i].width) {
		cur->sensor_w = standards[i].width;
		cur->sensor_h = standards[i].height;
		if (cur->crop_w > cur->sensor_w)
			cur->crop_w = cur->sensor_w;
		if (cur->crop_h > cur->sensor_h)
			cur->crop_h = cur->sensor_h;
		if (cur->output_w > cur->crop_w)
			cur->output_w = cur->crop_w;
		if (cur->output_h > cur->crop_h)
			cur->output_h = cur->crop_h;
		if (cur->sensor_x0 > MT9D131_MAXSENSOR_WIDTH - cur->sensor_w) {
			cur->crop_x0 = cur->sensor_x0;
			cur->sensor_x0 =
			    MT9D131_MAXSENSOR_WIDTH - cur->sensor_w;
			cur->crop_x0 -= cur->sensor_x0;
		}
		if (cur->sensor_y0 > MT9D131_MAXSENSOR_HEIGHT - cur->sensor_h) {
			cur->crop_y0 = cur->sensor_y0;
			cur->sensor_y0 =
			    MT9D131_MAXSENSOR_HEIGHT - cur->sensor_h;
			cur->crop_y0 -= cur->sensor_y0;
		}
	} else {
		cur->sensor_w = cur->crop_w;
		cur->sensor_h = cur->crop_h;
	}
	mt9d131_reconfigure(client);
	return 0;
}

static int mt9d131_cropcap(void *ctx, struct v4l2_cropcap *cap)
{
	struct i2c_client *client = ctx;
	struct mt9d131_device *dev = i2c_get_clientdata(client);
	struct mt9d131_settings *cur = &dev->current_settings;

	cap->bounds.left = 0;
	cap->bounds.top = 0;
	cap->bounds.width = MT9D131_MAXSENSOR_WIDTH;
	cap->bounds.height = MT9D131_MAXSENSOR_HEIGHT;
	cap->pixelaspect.numerator = 1;
	cap->pixelaspect.denominator = 1;

	if (standards[cur->std_num].width) {
		cap->defrect.left = ((MT9D131_MAXSENSOR_WIDTH
				      -
				      standards[cur->std_num].width) / 2) & ~1;
		cap->defrect.top =
		    ((MT9D131_MAXSENSOR_HEIGHT -
		      standards[cur->std_num].height) / 2) & ~1;
		cap->defrect.width = standards[cur->std_num].width;
		cap->defrect.height = standards[cur->std_num].height;
	} else {
		cap->defrect = cap->bounds;
	}
	return 0;
}

static int mt9d131_s_crop(void *ctx, struct v4l2_crop *crop, int busy)
{
	struct i2c_client *client = ctx;
	struct mt9d131_device *dev = i2c_get_clientdata(client);
	struct mt9d131_settings *cur = &dev->current_settings;

	if (busy
	    && (crop->c.width != cur->crop_w || crop->c.height != cur->crop_h))
		return -EBUSY;
	cur->crop_x0 = (crop->c.left + (crop->c.width & 1)) & ~1;
	cur->crop_y0 = (crop->c.top + (crop->c.height & 1)) & ~1;
	cur->crop_w = crop->c.width & ~1;
	cur->crop_h = crop->c.height & ~1;
	if (!standards[cur->std_num].width) {
		cur->sensor_w = cur->crop_w;
		cur->sensor_h = cur->crop_h;
		if (cur->sensor_w > MT9D131_MAXSENSOR_WIDTH)
			cur->sensor_w = MT9D131_MAXSENSOR_WIDTH;
		if (cur->sensor_h > MT9D131_MAXSENSOR_HEIGHT)
			cur->sensor_h = MT9D131_MAXSENSOR_HEIGHT;
	}
	if (cur->crop_w > cur->sensor_w)
		cur->crop_w = cur->sensor_w;
	if (cur->crop_h > cur->sensor_h)
		cur->crop_h = cur->sensor_h;
	if (cur->output_w > cur->crop_w)
		cur->output_w = cur->crop_w;
	if (cur->output_h > cur->crop_h)
		cur->output_h = cur->crop_h;
	if (cur->crop_x0 > MT9D131_MAXSENSOR_WIDTH - cur->crop_w)
		cur->crop_x0 = MT9D131_MAXSENSOR_WIDTH - cur->crop_w;
	if (cur->crop_y0 > MT9D131_MAXSENSOR_HEIGHT - cur->crop_h)
		cur->crop_y0 = MT9D131_MAXSENSOR_HEIGHT - cur->crop_h;
	cur->sensor_x0 = cur->crop_x0;
	if (cur->sensor_x0 > MT9D131_MAXSENSOR_WIDTH - cur->sensor_w)
		cur->sensor_x0 = MT9D131_MAXSENSOR_WIDTH - cur->sensor_w;
	cur->crop_x0 -= cur->sensor_x0;
	cur->sensor_y0 = cur->crop_y0;
	if (cur->sensor_y0 > MT9D131_MAXSENSOR_HEIGHT - cur->sensor_h)
		cur->sensor_y0 = MT9D131_MAXSENSOR_HEIGHT - cur->sensor_h;
	cur->crop_y0 -= cur->sensor_y0;
	mt9d131_reconfigure(client);
	return 0;
}

static int mt9d131_g_crop(void *ctx, struct v4l2_crop *crop)
{
	struct i2c_client *client = ctx;
	struct mt9d131_device *dev = i2c_get_clientdata(client);
	struct mt9d131_settings *cur = &dev->current_settings;

	crop->c.left = cur->sensor_x0 + cur->crop_x0;
	crop->c.top = cur->sensor_y0 + cur->crop_y0;
	crop->c.width = cur->crop_w;
	crop->c.height = cur->crop_h;
	return 0;
}

static int
mt9d131_s_fmt(void *ctx, int try_fmt, struct v4l2_pix_format *fmt, int busy)
{
	struct i2c_client *client = ctx;
	struct mt9d131_device *dev = i2c_get_clientdata(client);
	struct mt9d131_settings *cur = &dev->current_settings;
	int maxwidth, maxheight;

	if (!try_fmt && busy)
		return -EBUSY;

	fmt->pixelformat = V4L2_PIX_FMT_UYVY;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = cur->colorspace;	/* not selectable here */
	if (standards[cur->std_num].width) {
		maxwidth = cur->sensor_w;
		maxheight = cur->sensor_h;
	} else {
		maxwidth = MT9D131_MAXSENSOR_WIDTH;
		maxheight = MT9D131_MAXSENSOR_HEIGHT;
	}
	fmt->width &= ~1;
	fmt->height &= ~1;
	if (fmt->width > maxwidth)
		fmt->width = maxwidth;
	if (fmt->height > maxheight)
		fmt->height = maxheight;
	if (!try_fmt) {
		cur->output_w = fmt->width;
		cur->output_h = fmt->height;
		if (cur->crop_w < cur->output_w)
			cur->crop_w = cur->output_w;
		if (cur->crop_h < cur->output_h)
			cur->crop_h = cur->output_h;
		cur->crop_x0 += cur->sensor_x0;
		cur->crop_y0 += cur->sensor_y0;
		if (cur->crop_x0 > MT9D131_MAXSENSOR_WIDTH - cur->crop_w)
			cur->crop_x0 = MT9D131_MAXSENSOR_WIDTH - cur->crop_w;
		if (cur->crop_y0 > MT9D131_MAXSENSOR_HEIGHT - cur->crop_h)
			cur->crop_y0 = MT9D131_MAXSENSOR_HEIGHT - cur->crop_h;
		if (!standards[cur->std_num].width) {
			cur->sensor_w = cur->crop_w;
			cur->sensor_h = cur->crop_h;
		}
		cur->sensor_x0 = cur->crop_x0;
		cur->sensor_y0 = cur->crop_y0;
		if (cur->sensor_x0 > MT9D131_MAXSENSOR_WIDTH - cur->sensor_w)
			cur->sensor_x0 =
			    MT9D131_MAXSENSOR_WIDTH - cur->sensor_w;
		if (cur->sensor_y0 > MT9D131_MAXSENSOR_HEIGHT - cur->sensor_h)
			cur->sensor_y0 =
			    MT9D131_MAXSENSOR_HEIGHT - cur->sensor_h;
		cur->crop_x0 -= cur->sensor_x0;
		cur->crop_y0 -= cur->sensor_y0;
		mt9d131_reconfigure(client);
	}
	return 0;
}

static int mt9d131_g_fmt(void *ctx, struct v4l2_pix_format *fmt)
{
	struct i2c_client *client = ctx;
	struct mt9d131_device *dev = i2c_get_clientdata(client);
	struct mt9d131_settings *cur = &dev->current_settings;

	fmt->pixelformat = V4L2_PIX_FMT_UYVY;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = cur->colorspace;
	fmt->width = cur->output_w;
	fmt->height = cur->output_h;
	return 0;
}

static struct v4l2_subdev_video_ops mt9m111_subdev_video_ops = {
	.s_mbus_fmt	= mt9m111_s_fmt,
	.g_mbus_fmt	= mt9m111_g_fmt,
	.try_mbus_fmt	= mt9m111_try_fmt,
	.s_crop		= mt9m111_s_crop,
	.g_crop		= mt9m111_g_crop,
	.cropcap	= mt9m111_cropcap,
	.enum_mbus_fmt	= mt9m111_enum_fmt,
};

static struct v4l2_subdev_ops mt9m111_subdev_ops = {
	.core	= &mt9m111_subdev_core_ops,
	.video	= &mt9m111_subdev_video_ops,
};

static int
mt9d131_probe(struct i2c_client *new_client, const struct i2c_device_id *id)
{
	struct mt9d131_device *mt9d131;
	struct mt9d131_settings *cur;
	struct soc_camera_device *icd = client->dev.platform_data;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct soc_camera_link *icl;
	int ret;

	if (!icd) {
		dev_err(&new_client->dev, "[MT9D131]: missing soc-camera data!\n");
		return -EINVAL;
	}

	icl = to_soc_camera_link(icd);
	if (!icl) {
		dev_err(&new_client->dev, "[MT9D131] driver needs platform data\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_warn(&adapter->dev,
			 "[MT9D131]: I2C-Adapter doesn't support I2C_FUNC_SMBUS_WORD\n");
		return -EIO;
	}

	mt9d131 = kzalloc(sizeof(*mt9d131), GFP_KERNEL);
	if (!mt9d131) {
		return -ENOMEM;
	}

	mt9d131->current_reg_set = -1;
	i2c_set_clientdata(new_client, mt9d131);

	ret = mt9d131_setup_pll(new_client, 26, 80);
	if (ret < 0) {
		icd->ops = NULL;
		i2c_set_clientdata(new_client, NULL);
		kfree(dev);
		return ret;
	}

	v4l2_i2c_subdev_init(&mt9d131->subdev, new_client, &mt9d131_subdev_ops);

	mt9d131_write_register(new_client, MT9D131_REG_IFP1,
			       MT9D131_IFP1_YRGBOFFSET, 16 << 8);
	mt9d131_set_colorspace(new_client, V4L2_COLORSPACE_470_SYSTEM_BG);

	cur = &mt9d131->current_settings;
	cur->std_num = ARRAY_SIZE(standards) - 1;
	cur->sensor_w = MT9D131_MAXSENSOR_WIDTH;
	cur->sensor_h = MT9D131_MAXSENSOR_HEIGHT;
	cur->crop_w = cur->sensor_w;
	cur->crop_h = cur->sensor_h;
	cur->output_w = 48;
	cur->output_h = 32;
	cur->sensor_x0 = 0;
	cur->sensor_y0 = 0;
	cur->crop_x0 = 0;
	cur->crop_y0 = 0;
	mt9d131_reconfigure(new_client);
	mt9d131_common_init(new_client);
	printk(KERN_INFO "successfully probed mt9d131\n");
	return 0;
}

static int mt9d131_remove(struct i2c_client *client)
{
	struct mt9d131_device *dev = i2c_get_clientdata(client);
	i2c_set_clientdata(client, 0);
	kfree(dev);
	return 0;
}

static const struct i2c_device_id mt9d131_id[] = {
	{"mt9d131", 0},
	{}
};

static struct i2c_driver mt9d131_driver = {
	.driver = {
		   .name = "s6dp-mt9d131",
		   },
	.probe = mt9d131_probe,
	.remove = mt9d131_remove,
	.id_table = mt9d131_id,
};

static int __init mt9d131_init(void)
{
	return i2c_add_driver(&mt9d131_driver);
}

static void __exit mt9d131_exit(void)
{
	i2c_del_driver(&mt9d131_driver);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fabian Godehardt");

module_init(mt9d131_init);
module_exit(mt9d131_exit);
