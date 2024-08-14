/*
 *
 * Hardware definitions for Fujitsu-Siemens PocketLOOX 7xx series handhelds
 *
 * Copyright 2010 Alexander Tarasikov <alexander.tarasikov@gmail.com>
 * Copyright 2008 Tomasz Figa
 * Copyright 2004 Fujitsu-Siemens Computers.
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 *
 * based on loox720.c
 */

#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/pda_power.h>
#include <linux/serial.h>
#include <linux/spi/spi.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mfd/htc-egpio.h>
#include <linux/spi/ads7846.h>
#include <linux/regulator/max1586.h>
#include <linux/pwm_backlight.h>

#include <asm/mach-types.h>
#include <asm/setup.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include <mach/pxa2xx-regs.h>
#include <mach/pxa2xx_spi.h>
#include <plat/ssp.h>
#include <mach/camera.h>
#include <mach/udc.h>
#include <mach/audio.h>
#include <mach/ohci.h>
#include <mach/irda.h>
#include <mach/mmc.h>
#include <mach/pxa27x.h>
#include <mach/irqs.h>
#include <mach/pxafb.h>
#include <mach/pxa27x_keypad.h>
#include <mach/mfp-pxa27x.h>
#include <plat/i2c.h>

#include <media/soc_camera.h>
#include <linux/i2c-gpio.h>

#include <mach/loox720.h>
#include "../devices.h"
#include "../generic.h"

/******************************************************************************
 * GPIO Setup
 ******************************************************************************/
static unsigned long loox720_pin_config[] = {
	GPIO1_GPIO | WAKEUP_ON_EDGE_BOTH,
	/* Platform-specific */
	GPIO9_GPIO | WAKEUP_ON_EDGE_BOTH,
	GPIO11_GPIO,
	GPIO12_GPIO | WAKEUP_ON_EDGE_RISE,
	GPIO13_GPIO | WAKEUP_ON_EDGE_BOTH,
	GPIO94_GPIO,
	MFP_CFG_OUT(GPIO14, AF0, DRIVE_LOW),

	MFP_CFG_OUT(GPIO19, AF0, DRIVE_LOW),
	MFP_CFG_OUT(GPIO22, AF0, DRIVE_HIGH),
	MFP_CFG_OUT(GPIO36, AF0, DRIVE_HIGH),
	MFP_CFG_OUT(GPIO75, AF0, DRIVE_HIGH),
	MFP_CFG_OUT(GPIO81, AF0, DRIVE_LOW),	// setup but function unknown
	MFP_CFG_OUT(GPIO86, AF0, DRIVE_HIGH),	// setup but function unknown
	MFP_CFG_OUT(GPIO90, AF0, DRIVE_HIGH), //Camera I2C Clock
	MFP_CFG_OUT(GPIO91, AF0, DRIVE_HIGH), //Camera I2C Data
	MFP_CFG_OUT(GPIO95, AF0, DRIVE_LOW),	// setup but function unknown
	MFP_CFG_OUT(GPIO106, AF0, DRIVE_HIGH),
	MFP_CFG_OUT(GPIO107, AF0, DRIVE_HIGH),

	/* Crystal and Clock Signals */
	GPIO10_HZ_CLK,

	/* PC CARD */
	GPIO15_nPCE_1,
	GPIO18_RDY,
	GPIO49_nPWE,
	GPIO48_nPOE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,
	GPIO78_nPCE_2,
	GPIO79_PSKTSEL,

	/* SDRAM and Static Memory I/O Signals */
	GPIO20_nSDCS_2,
	GPIO21_nSDCS_3,
	GPIO80_nCS_4,
	GPIO33_nCS_5,

	/* FFUART */
	GPIO34_FFUART_RXD,
	GPIO35_FFUART_CTS,
	GPIO37_FFUART_DSR,
	GPIO39_FFUART_TXD,
	GPIO41_FFUART_RTS,
	GPIO40_FFUART_DTR,

	/* BTUART */
	GPIO44_BTUART_CTS,
	GPIO42_BTUART_RXD,
	GPIO45_BTUART_RTS,
	GPIO43_BTUART_TXD,

	/* STUART */
	GPIO46_STUART_RXD,
	GPIO47_STUART_TXD,

	/* PWM 0/1/2/3 */
	GPIO16_PWM0_OUT,
	GPIO17_PWM1_OUT,

	/* SSP 1 */
	GPIO23_SSP1_SCLK,
	/* GPIO24_SSP1_SFRM,
	* you cannot really use SSP1 unless
	* you switch gpio 24 to act as chip select */
	GPIO24_GPIO,	
	GPIO25_SSP1_TXD,
	GPIO26_SSP1_RXD,

	/* QCIF Interface */
	GPIO27_CIF_DD_0,
	GPIO53_CIF_MCLK,
	GPIO54_CIF_PCLK,
	GPIO82_CIF_DD_5,
	GPIO83_CIF_DD_4,
	GPIO84_CIF_FV,
	GPIO85_CIF_LV,
	GPIO93_CIF_DD_6,
	GPIO108_CIF_DD_7,
	GPIO114_CIF_DD_1,
	GPIO115_CIF_DD_3,
	GPIO116_CIF_DD_2,

	/* MMC */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,

	/* LCD */
	GPIO58_LCD_LDD_0,
	GPIO59_LCD_LDD_1,
	GPIO60_LCD_LDD_2,
	GPIO61_LCD_LDD_3,
	GPIO62_LCD_LDD_4,
	GPIO63_LCD_LDD_5,
	GPIO64_LCD_LDD_6,
	GPIO65_LCD_LDD_7,
	GPIO66_LCD_LDD_8,
	GPIO67_LCD_LDD_9,
	GPIO68_LCD_LDD_10,
	GPIO69_LCD_LDD_11,
	GPIO70_LCD_LDD_12,
	GPIO71_LCD_LDD_13,
	GPIO72_LCD_LDD_14,
	GPIO73_LCD_LDD_15,
	GPIO76_LCD_PCLK,
	GPIO77_LCD_BIAS,

	/* USB Host Port 1 */
	GPIO88_USBH1_PWR,
	GPIO89_USBH1_PEN,

	/* I2S */
	GPIO29_I2S_SDATA_IN,
	GPIO28_I2S_BITCLK_OUT,
	GPIO30_I2S_SDATA_OUT,
	GPIO31_I2S_SYNC,
	GPIO113_I2S_SYSCLK,

	/* Keypad */
	GPIO100_KP_MKIN_0,
	GPIO101_KP_MKIN_1,
	GPIO102_KP_MKIN_2,
	GPIO97_KP_MKIN_3,
	GPIO98_KP_MKIN_4,
	GPIO99_KP_MKIN_5,
	GPIO103_KP_MKOUT_0,
	GPIO104_KP_MKOUT_1,
	GPIO105_KP_MKOUT_2,

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,
};

/******************************************************************************
 * CPLD gpio extender
 ******************************************************************************/
static struct resource egpio_resources[] = {
	[0] = {
	.start = PXA_CS4_PHYS,
	.end = PXA_CS4_PHYS + 0x20 - 1,
	.flags = IORESOURCE_MEM,
	},
	[1] = {
	.start = gpio_to_irq(GPIO_LOOX720_EGPIO_INT),
	.end = gpio_to_irq(GPIO_LOOX720_EGPIO_INT),
	.flags = IORESOURCE_IRQ,
	},
};

static struct htc_egpio_chip egpio_chips[] = {
	{
	.reg_start = 2,
	.gpio_base = LOOX720_EGPIO_BANK(0),
	.num_gpios = 16,
	.direction = HTC_EGPIO_INPUT,
	},
	{
	.reg_start = LOOX720_EGPIO_REG_OUT,
	.gpio_base = LOOX720_EGPIO_BANK(1),
	.num_gpios = 16,
	.direction = HTC_EGPIO_OUTPUT,
	},
	{
	.reg_start = LOOX720_EGPIO_REG_OUT + 1,
	.gpio_base = LOOX720_EGPIO_BANK(2),
	.num_gpios = 16,
	.direction = HTC_EGPIO_OUTPUT,
	},
	{
	.reg_start = LOOX720_EGPIO_REG_OUT + 2,
	.gpio_base = LOOX720_EGPIO_BANK(3),
	.num_gpios = 16,
	.direction = HTC_EGPIO_OUTPUT,
	.initial_values	= (1 << LOOX720_EGPIO3_SERIAL),
	},
	{
	.reg_start = LOOX720_EGPIO_REG_OUT + 3,
	.gpio_base = LOOX720_EGPIO_BANK(4),
	.num_gpios = 16,
	.direction = HTC_EGPIO_OUTPUT,
	},
	{
	.reg_start = LOOX720_EGPIO_REG_OUT + 4,
	.gpio_base = LOOX720_EGPIO_BANK(5),
	.num_gpios = 16,
	.direction = HTC_EGPIO_OUTPUT,
	},
	{
	.reg_start = LOOX720_EGPIO_REG_OUT + 5,
	.gpio_base = LOOX720_EGPIO_BANK(6),
	.num_gpios = 16,
	.direction = HTC_EGPIO_OUTPUT,
	},
	{
	.reg_start = LOOX720_EGPIO_REG_OUT + 6,
	.gpio_base = LOOX720_EGPIO_BANK(7),
	.num_gpios = 16,
	.direction = HTC_EGPIO_OUTPUT,
	},
	{
	.reg_start = LOOX720_EGPIO_REG_OUT + 7,
	.gpio_base = LOOX720_EGPIO_BANK(8),
	.num_gpios = 16,
	.direction = HTC_EGPIO_OUTPUT,
	.initial_values = 1 << LOOX720_EGPIO8_BACKLIGHT,
	},
};

static struct htc_egpio_platform_data egpio_info = {
	.reg_width = 16,
	.bus_width = 16,
	.irq_base = LOOX720_CPLD_IRQ_BASE,
	.num_irqs = 16,
	.ack_register = 0,
	.irq_register = 1,
	.chip = egpio_chips,
	.num_chips = ARRAY_SIZE(egpio_chips),
};

static struct platform_device loox720_cpld = {
	.name = "htc-egpio",
	.id = -1,
	.resource = egpio_resources,
	.num_resources = ARRAY_SIZE(egpio_resources),
	.dev = {
		.platform_data = &egpio_info,
		},
};
/******************************************************************************
 * IrDA transceiver
 ******************************************************************************/
static struct pxaficp_platform_data loox_ficp_info = {
	.gpio_pwdown = GPIO_LOOX720_IR_ON_N,
	.gpio_pwdown_inverted = 1,
	.transceiver_cap = IR_SIRMODE | IR_FIRMODE | IR_OFF,
};

/******************************************************************************
 * GPIO Keys
 ******************************************************************************/
static struct gpio_keys_button loox720_button_table[] = {
	[0] = {
	.desc = "wakeup",
	.code = KEY_POWER,
	.type = EV_KEY,
	.gpio = GPIO_LOOX720_KEY_ON,
	.wakeup = 1,
	},
};

static struct gpio_keys_platform_data loox720_pxa_keys_data = {
	.buttons = loox720_button_table,
	.nbuttons = ARRAY_SIZE(loox720_button_table),
};

static struct platform_device loox720_pxa_keys = {
	.name = "gpio-keys",
	.id = -1,
	.dev = {
		.platform_data = &loox720_pxa_keys_data,
		},
};

/******************************************************************************
 * PXA Matrix Keypad
 ******************************************************************************/
static unsigned int loox720_key_matrix[] = {
	// KEY( row , column , KEY_CODE )

	KEY(0, 0, KEY_CAMERA),
	KEY(0, 1, KEY_UP),
	KEY(0, 2, KEY_OK),

	KEY(1, 0, KEY_RECORD),
	KEY(1, 1, KEY_DOWN),
	KEY(1, 2, KEY_SCROLLDOWN),

	KEY(2, 0, KEY_F1),
	KEY(2, 1, KEY_RIGHT),
	KEY(2, 2, KEY_SCROLLUP),

	KEY(3, 0, KEY_F3),
	KEY(3, 1, KEY_LEFT),

	KEY(4, 0, KEY_F2),
	KEY(4, 1, KEY_ENTER),

	KEY(5, 0, KEY_F4),
};

struct pxa27x_keypad_platform_data loox720_keypad_info = {
	.matrix_key_rows = 6,
	.matrix_key_cols = 3,
	.matrix_key_map = loox720_key_matrix,
	.matrix_key_map_size = ARRAY_SIZE(loox720_key_matrix),
	.debounce_interval = 30,
};

/******************************************************************************
 * USB
 ******************************************************************************/
static struct pxa2xx_udc_mach_info loox720_udc_info __initdata = {
	.gpio_vbus = GPIO_LOOX720_USB_DETECT_N,
	.gpio_vbus_inverted = 1,
	.gpio_pullup = LOOX720_EGPIO_USB_PULLUP,
};

static struct pxaohci_platform_data loox720_ohci_info = {
	.port_mode = PMM_PERPORT_MODE,
	.flags = ENABLE_PORT1 | POWER_SENSE_LOW,
	.power_budget = 500,
};

/******************************************************************************
 * SD/MMC
 ******************************************************************************/
static struct pxamci_platform_data loox7xx_mci_info = {
	.ocr_mask = MMC_VDD_32_33 | MMC_VDD_33_34,
	.get_ro = 0,
	.gpio_card_detect = GPIO_LOOX720_MMC_DETECT_N,
	.gpio_card_ro = GPIO_LOOX720_MMC_RO,
	.gpio_power = LOOX720_EGPIO_SDMMC,
};

/******************************************************************************
 * Framebuffer
 ******************************************************************************/
static void loox720_lcd_power(int on, struct fb_var_screeninfo *si)
{
	if (on) {
		gpio_direction_output(LOOX720_EGPIO_LCD1, 1);
		gpio_direction_output(LOOX720_EGPIO_LCD2, 1);
		gpio_direction_output(LOOX720_EGPIO_LCD3, 1);
		gpio_direction_output(LOOX720_EGPIO_LCD4, 1);
	} else {
		gpio_direction_output(LOOX720_EGPIO_LCD2, 0);
		gpio_direction_output(LOOX720_EGPIO_LCD3, 0);
		gpio_direction_output(LOOX720_EGPIO_LCD4, 0);
		gpio_direction_output(LOOX720_EGPIO_LCD1, 0);
	}
}

static struct pxafb_mode_info loox720_lcd_mode_info = {
	.pixclock = 96153,	// Since we now use double pixel clock
	.bpp = 16,
	.xres = 480,
	.yres = 640,
	.hsync_len = 4,
	.left_margin = 20,
	.right_margin = 8,
	.vsync_len = 1,
	.upper_margin = 7,
	.lower_margin = 8,
	.sync = 0,
};

static struct pxafb_mach_info loox720_fb_info = {
	.modes = &loox720_lcd_mode_info,
	.num_modes = 1,
	.lccr0 = LCCR0_Act | LCCR0_Sngl | LCCR0_Color,
	.lccr3 = LCCR3_OutEnH | LCCR3_PixRsEdg | LCCR3_DPC,
	.pxafb_lcd_power = loox720_lcd_power,
};

/******************************************************************************
 * Power Supply
 ******************************************************************************/
static char *supplicants[] = {
	"main_battery",
};

static struct resource loox720_power_resources[] = {
	[0] = {
	.name = "ac",
	.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE |
	IORESOURCE_IRQ_LOWEDGE,
	},
	[1] = {
	.name = "usb",
	.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE |
	IORESOURCE_IRQ_LOWEDGE,
	},
};

static int loox720_is_usb_online(void)
{
	return !gpio_get_value(GPIO_LOOX720_USB_DETECT_N);
}

static int loox720_is_ac_online(void)
{
	return !gpio_get_value(GPIO_LOOX720_AC_IN_N);
}

static void loox720_set_charge(int flags)
{
	if (flags == PDA_POWER_CHARGE_AC) {
		gpio_direction_output(GPIO_LOOX720_USB_CHARGE_N, 1);
	} else {
		gpio_direction_output(GPIO_LOOX720_USB_CHARGE_N, 0);
	}

	if (!(flags & (PDA_POWER_CHARGE_AC|PDA_POWER_CHARGE_USB))) {
		gpio_set_value(LOOX720_EGPIO_BATTERY, 0);
		gpio_set_value(GPIO_LOOX720_CHARGE_EN_N, 1);
		return;
	}
	else {
		gpio_set_value(LOOX720_EGPIO_BATTERY, 0);
	}

	if (!!gpio_get_value(GPIO_LOOX720_BATTERY_FULL_N)) {
		gpio_set_value(GPIO_LOOX720_CHARGE_EN_N, 0);
	} else {
		gpio_set_value(GPIO_LOOX720_CHARGE_EN_N, 1);
	}
}

static int loox720_power_init(struct device *dev)
{
	int rc = 0;

	rc = gpio_request(GPIO_LOOX720_CHARGE_EN_N, "Loox 720 Power Supply");
	if (rc)
		goto err_chg;

	rc = gpio_request(GPIO_LOOX720_AC_IN_N, "Loox 720 Power Supply");
	if (rc)
		goto err_ac_in;

	rc = gpio_request(GPIO_LOOX720_USB_CHARGE_N,
			"Loox 720 Power Supply");
	if (rc)
		goto err_usb_chg;

	rc = gpio_request(GPIO_LOOX720_BATTERY_FULL_N,
			"Loox 720 Power Supply");
	if (rc)
		goto err_bat;

	loox720_power_resources[0].start =
	gpio_to_irq(GPIO_LOOX720_USB_DETECT_N);
	loox720_power_resources[0].end = loox720_power_resources[0].start;
	loox720_power_resources[1].start = gpio_to_irq(GPIO_LOOX720_AC_IN_N);
	loox720_power_resources[1].end = loox720_power_resources[1].start;

 err_bat:
	gpio_free(GPIO_LOOX720_BATTERY_FULL_N);
 err_usb_chg:
	gpio_free(GPIO_LOOX720_USB_CHARGE_N);
 err_ac_in:
	gpio_free(GPIO_LOOX720_AC_IN_N);
 err_chg:
	gpio_free(GPIO_LOOX720_CHARGE_EN_N);

	return rc;
}

static void loox720_power_exit(struct device *dev)
{
	gpio_free(GPIO_LOOX720_BATTERY_FULL_N);
	gpio_free(GPIO_LOOX720_USB_CHARGE_N);
	gpio_free(GPIO_LOOX720_AC_IN_N);
	gpio_free(GPIO_LOOX720_CHARGE_EN_N);
}

static struct pda_power_pdata loox720_power_data = {
	.init = loox720_power_init,
	.is_ac_online = loox720_is_ac_online,
	.is_usb_online = loox720_is_usb_online,
	.set_charge = loox720_set_charge,
	.exit = loox720_power_exit,
	.supplied_to = supplicants,
	.num_supplicants = ARRAY_SIZE(supplicants),
};

static struct platform_device loox720_powerdev = {
	.name = "pda-power",
	.id = -1,
	.resource = loox720_power_resources,
	.num_resources = ARRAY_SIZE(loox720_power_resources),
	.dev = {
		.platform_data = &loox720_power_data,
		},
};

/******************************************************************************
 * MTD Flash
 ******************************************************************************/
//it's better not to tamper with it at all, because
//partition offsets vary on different firmware versions
static struct mtd_partition wince_partitions[] = {
	{
		name:		"bootloader",
		offset:		MTDPART_OFS_NXTBLK,
		size:		SZ_256K,
		mask_flags:	MTD_WRITEABLE,	/* force read-only */
	},
	{
		name:		"winmobile",
		offset:		MTDPART_OFS_NXTBLK,
		size:		135 * SZ_256K,
		mask_flags:	MTD_WRITEABLE,	/* force read-only */
	},
	{
		name:		"looxstore",
		offset:		MTDPART_OFS_NXTBLK,
		size:		MTDPART_SIZ_FULL,
		mask_flags:	MTD_WRITEABLE,	/* force read-only */
	},
};

static struct resource loox7xx_flash_resource = {
	.start	= PXA_CS0_PHYS,
	.end	= PXA_CS0_PHYS + SZ_64M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct flash_platform_data loox7xx_flash_data = {
	.name = "loox720-flash",
	.map_name = "cfi_probe",
	.width = 4,
	.nr_parts = ARRAY_SIZE(wince_partitions),
	.parts = wince_partitions,
};

struct platform_device loox7xx_flash = {
	.name = "pxa2xx-flash",
	.id = -1,
	.num_resources = 1,
	.resource = &loox7xx_flash_resource,
	.dev = {
		.platform_data = &loox7xx_flash_data,
	},
};

/******************************************************************************
 * Touchscreen/SSP
 ******************************************************************************/
static const struct ads7846_platform_data ads7846_info = {
	.model            = 7846,
	.vref_mv	= 2500,
	.vref_delay_usecs = 100,
	.pressure_max     = 1024,
	.debounce_max     = 12,
	.debounce_tol     = 4,
	.debounce_rep     = 1,
	.x_min = 400,
	.x_max = 3610,
	.y_min = 3333,
	.y_max = 3780,
	.penirq_recheck_delay_usecs = 100,
	.gpio_pendown	= GPIO_LOOX720_TOUCHPANEL_IRQ_N,
};

static struct pxa2xx_spi_chip ads7846_chip = {
	.tx_threshold = 1,
	.rx_threshold = 2,
	.timeout      = 64,
	.gpio_cs	= GPIO_LOOX720_TSC_CS,
};

static struct spi_board_info spi_board_devices[] = {
	{
		.modalias        = "ads7846",
		.bus_num         = 1,
		.max_speed_hz    = 200000,
		.irq             = IRQ_GPIO(GPIO_LOOX720_TOUCHPANEL_IRQ_N),
		.platform_data   = &ads7846_info,
		.controller_data = &ads7846_chip,
	},
};

static struct pxa2xx_spi_master pxa_ssp1_master_info = {
        .clock_enable = CKEN_SSP1,
        .num_chipselect = 1,
        .enable_dma = 0,
};
/******************************************************************************
 * Voltage Regulator
 ******************************************************************************/
static struct regulator_consumer_supply max1586_consumers[] = {
	{
	.supply = "vcc_core",
	}
};

static struct regulator_init_data max1586_v3_info = {
	.constraints = {
			.name = "vcc_core range",
			.min_uV = 900000,
			.max_uV = 1705000,
			.always_on = 1,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
			},
	.num_consumer_supplies = ARRAY_SIZE(max1586_consumers),
	.consumer_supplies = max1586_consumers,
};

static struct max1586_subdev_data max1586_subdevs[] = {
	{
	.name = "vcc_core",
	.id = MAX1586_V3,
	.platform_data = &max1586_v3_info},
};

static struct max1586_platform_data max1586_info = {
	.subdevs = max1586_subdevs,
	.num_subdevs = ARRAY_SIZE(max1586_subdevs),
	.v3_gain = MAX1586_GAIN_R24_3k32,
};

static struct i2c_board_info loox720_pi2c_board_info[] = {
	{
	I2C_BOARD_INFO("max1586", 0x14),
	.platform_data = &max1586_info,
	},
};
/******************************************************************************
 * LCD Backlight
 ******************************************************************************/

//PWM1 on Loox 720 must be set to the fixed values whenever lcd brightness is on
static int loox720_backlight_callback(struct device* dev, int brightness) {
	if (brightness) {
		__REG(0x40c00008) = 0xd7;
		__REG(0x40c00004) = 0x4d,
		__REG(0x40c00000) = 1;
		gpio_direction_output(LOOX720_EGPIO_BACKLIGHT, 1);
	}
	else {
		__REG(0x40c00000) = 0;
		__REG(0x40c00004) = 0;
		__REG(0x40c00008) = 0;
		gpio_direction_output(LOOX720_EGPIO_BACKLIGHT, 0);
	}
	return brightness;
}

static int loox720_backlight_init(struct device *dev)
{
	int ret;
	ret = gpio_request(LOOX720_EGPIO_BACKLIGHT, "Loox 720 Backlight");
	if (ret) {
		printk(KERN_ERR "failed to register backlight gpio\n");
	}

	return ret;
}

static void loox720_backlight_exit(struct device *dev)
{
	gpio_free(LOOX720_EGPIO_BACKLIGHT);
}

static struct platform_pwm_backlight_data loox720_backlight_data = {
	.pwm_id = 0,
	.max_brightness = 193,
	.dft_brightness = 150,
	.pwm_period_ns = 16534,
	.init = loox720_backlight_init,
	.exit = loox720_backlight_exit,
	.notify = loox720_backlight_callback,
};

static struct platform_device loox720_backlight = {
	.name = "pwm-backlight",
	.dev = {
		.parent = &pxa27x_device_pwm0.dev,
		.platform_data = &loox720_backlight_data,
	},
};

/******************************************************************************
 * OV9640 Camera Sensor
 ******************************************************************************/
static struct i2c_gpio_platform_data i2c_bus_data = {
	.sda_pin = GPIO_LOOX720_CAMERA_I2C_DATA,
	.scl_pin = GPIO_LOOX720_CAMERA_I2C_CLK,
	.udelay  = 24, //20 KHz
	.timeout = 100,
};

static struct platform_device loox720_i2c_bitbang = {
	.name		= "i2c-gpio",
	.id		= 2, // start after PXA I2Cs
	.dev = {
		.platform_data = &i2c_bus_data,
	}
};

static struct pxacamera_platform_data loox720_pxacamera_platform_data = {
	.flags  = PXA_CAMERA_MASTER | PXA_CAMERA_DATAWIDTH_8 |
		PXA_CAMERA_PCLK_EN | PXA_CAMERA_MCLK_EN,
	.mclk_10khz = 2600,
};

static struct i2c_board_info loox720_cam_devices[] = {
	{
		I2C_BOARD_INFO("ov9640", 0x30),
	},
};

static int loox720_cam_power(struct device *dev, int on)
{
	gpio_set_value(LOOX720_EGPIO_CAMERA_POWER, on);

	if (!on)
		return 0;

	mdelay(0x32);
	gpio_set_value(LOOX720_EGPIO_CAMERA_RESET, 1);
	mdelay(0x32);
	gpio_set_value(LOOX720_EGPIO_CAMERA_RESET, 0);

	return 0;
}


static struct soc_camera_link iclink = {
	.bus_id		= 0,
	.power		= loox720_cam_power,
	.board_info	= &loox720_cam_devices[0],
	.i2c_adapter_id	= 2,
	.module_name	= "ov9640",
};

static struct platform_device loox720_camera = {
	.name = "soc-camera-pdrv",
	.id = 0,
	.dev = {
		.platform_data = &iclink,	
	}
};

/******************************************************************************
 * Board Init
 ******************************************************************************/
static struct platform_device loox720_bt = {
	.name = "loox720-rfk",
};

static struct platform_device loox720_leds = {
	.name = "loox720-leds",
};


static struct platform_device loox720_pm = {
	.name = "loox720-pm",
	.id = -1,
};

static struct platform_device *devices[] __initdata = {
//	&loox720_cpld,
	&loox720_pxa_keys,
	&loox720_bt,
	&loox720_pm,
	&loox720_powerdev,
	&loox720_leds,
	&loox720_backlight,
	&loox720_i2c_bitbang,
	&loox720_camera,
//	&loox7xx_flash,
};

static struct i2c_pxa_platform_data i2c_pdata = {
	.fast_mode = 1,
};

#define LOOX720_GPIO_IN(num, _desc) \
	{ .gpio = (num), .dir = 0, .desc = (_desc) }
#define LOOX720_GPIO_OUT(num, _init, _desc) \
	{ .gpio = (num), .dir = 1, .init = (_init), .desc = (_desc) }

struct gpio_ress {
	unsigned gpio:14;
	unsigned dir : 1;
	unsigned init : 1;
	char *desc;
};

static int __initdata loox720_gpio_request(struct gpio_ress *gpios, int size)
{
	int i, rc = 0;
	int gpio;
	int dir;

	for (i = 0; (!rc) && (i < size); i++) {
		gpio = gpios[i].gpio;
		dir = gpios[i].dir;
		rc = gpio_request(gpio, gpios[i].desc);
		if (rc) {
			pr_err("Error requesting GPIO %d(%s) : %d\n",
			gpio, gpios[i].desc, rc);
			continue;
		}
		if (dir)
			gpio_direction_output(gpio, gpios[i].init);
		else
			gpio_direction_input(gpio);
	}
	while ((rc) && (--i >= 0))
		gpio_free(gpios[i].gpio);
	return rc;
}

static struct gpio_ress global_gpios[] __initdata = {
	LOOX720_GPIO_IN(GPIO_LOOX720_USB_DETECT_N, "Loox 720 USB Detection"),
	LOOX720_GPIO_OUT(LOOX720_EGPIO_LCD2, 0, "Loox 720 LCD"),
	LOOX720_GPIO_OUT(LOOX720_EGPIO_LCD3, 0, "Loox 720 LCD"),
	LOOX720_GPIO_OUT(LOOX720_EGPIO_LCD4, 0, "Loox 720 LCD"),
	LOOX720_GPIO_OUT(LOOX720_EGPIO_LCD1, 0, "Loox 720 LCD"),
};

static void __init loox720_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(loox720_pin_config));
	platform_device_register(&loox720_cpld);
	loox720_gpio_request(ARRAY_AND_SIZE(global_gpios));
	platform_add_devices(ARRAY_AND_SIZE(devices));

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	pxa_set_i2c_info(&i2c_pdata);
	pxa27x_set_i2c_power_info(NULL);
	i2c_register_board_info(1, ARRAY_AND_SIZE(loox720_pi2c_board_info));

	set_pxa_fb_info(&loox720_fb_info);
	pxa_set_keypad_info(&loox720_keypad_info);
	pxa_set_mci_info(&loox7xx_mci_info);
	pxa_set_ficp_info(&loox_ficp_info);
	pxa_set_udc_info(&loox720_udc_info);
	pxa_set_ohci_info(&loox720_ohci_info);

	pxa2xx_set_spi_info(1, &pxa_ssp1_master_info);
	spi_register_board_info(ARRAY_AND_SIZE(spi_board_devices));

	pxa_set_camera_info(&loox720_pxacamera_platform_data);
}

static void __init loox720_fixup(struct machine_desc *desc,
				struct tag *tags, char **cmdline,
				struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = 0xa8000000;
	//mi->bank[0].node = 0;
	mi->bank[0].size = (128 * 1024 * 1024);
}

MACHINE_START(LOOX720, "FSC Loox 720")
	.phys_io = 0x40000000,
	.io_pg_offst = (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params = 0xa8000100,
	.map_io = pxa_map_io,
	.fixup = loox720_fixup,
	.init_irq = pxa27x_init_irq,
	.timer = &pxa_timer,
	.init_machine = loox720_init,
MACHINE_END
