/*
 * Support for Asus P525 PDA
 *
 * (C) 2008-2009 Alexander Tarasikov <alex_dfr@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/pwm_backlight.h>
#include <linux/sysdev.h>

#include <linux/i2c/pca953x.h>
#include <linux/pda_power.h>
#include <linux/power_supply.h>
#include <linux/wm97xx.h>
#include <linux/regulator/max1586.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/pxafb.h>
#include <mach/mfp-pxa27x.h>
#include <mach/pxa27x_keypad.h>
#include <mach/pxa2xx-regs.h>
#include <mach/pxa27x-udc.h>
#include <mach/mmc.h>
#include <mach/udc.h>
#include <mach/audio.h>
#include <mach/irda.h>

#include <mach/camera.h>
#include <media/soc_camera.h>

#include <asm/gpio.h>
#include <plat/i2c.h>

#include <mach/asusp525.h>
#include "generic.h"
#include "devices.h"

#include <linux/debugfs.h>

/******************************************************************************
 * GPIO Setup
 ******************************************************************************/
static unsigned long asusp525_pin_config[] __initdata = {
	GPIO1_GPIO | WAKEUP_ON_EDGE_BOTH,
	GPIO10_GPIO | WAKEUP_ON_EDGE_FALL,
	GPIO11_GPIO,
	GPIO113_GPIO,

	GPIO16_PWM0_OUT,

	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,
	GPIO98_AC97_SYSCLK,

	/*PCMCIA+WiFi */
	GPIO15_nPCE_1,
	GPIO49_nPWE,
	GPIO48_nPOE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,

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
	GPIO74_LCD_FCLK,
	GPIO75_LCD_LCLK,
	GPIO76_LCD_PCLK,
	GPIO77_LCD_BIAS,

	GPIO46_FICP_RXD,
	GPIO47_FICP_TXD,

	/* GSM UART */
	GPIO25_GPIO,
	GPIO91_GPIO,
	GPIO34_FFUART_RXD,
	GPIO35_FFUART_CTS,
	GPIO36_FFUART_DCD,
	GPIO37_FFUART_DSR,
	GPIO38_FFUART_RI,
	GPIO39_FFUART_TXD,
	GPIO40_FFUART_DTR,
	GPIO41_FFUART_RTS,

	/* Keypad */
	GPIO100_KP_MKIN_0,
	GPIO101_KP_MKIN_1,
	GPIO102_KP_MKIN_2,
	GPIO103_KP_MKOUT_0,
	GPIO104_KP_MKOUT_1,
	GPIO105_KP_MKOUT_2,
	GPIO106_KP_MKOUT_3,
	GPIO107_KP_MKOUT_4,
	GPIO108_KP_MKOUT_5,

	/* MMC */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,

	/* BLUETOOTH */
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,
	GPIO45_BTUART_RTS,

	/* QCI */
	GPIO12_CIF_DD_7,
	GPIO93_CIF_DD_6,
	GPIO94_CIF_DD_5,
	GPIO52_CIF_DD_4,
	GPIO115_CIF_DD_3,
	GPIO116_CIF_DD_2,
	GPIO114_CIF_DD_1,
	GPIO81_CIF_DD_0,
	GPIO53_CIF_MCLK,
	GPIO54_CIF_PCLK,
	GPIO84_CIF_FV,
	GPIO85_CIF_LV,

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,
};

/******************************************************************************
 * IrDA transceiver
 ******************************************************************************/
static void asusp525_irda_transceiver_mode(struct device *dev, int mode)
{
	gpio_set_value(GPIO_ASUSP525_IR_DISABLE, mode & IR_OFF);
	pxa2xx_transceiver_mode(dev, mode);
}

static struct pxaficp_platform_data asusp525_ficp_info = {
	.transceiver_cap = IR_SIRMODE | IR_OFF,
	.transceiver_mode = asusp525_irda_transceiver_mode,
};

/******************************************************************************
 * GPIO Expanders
 ******************************************************************************/
static struct pca953x_platform_data gpio_exp[] = {
	[0] = {
	       .gpio_base = EGPIO_BASE0,
	       },
	[1] = {
	       .gpio_base = EGPIO_BASE1,
	       },
};

static struct i2c_board_info asusp525_i2c_board_info[] = {
	{
	 .type = "pca9535",
	 .addr = 0x20,
	 .platform_data = &gpio_exp[0],
	},
	{
	 .type = "pca9535",
	 .addr = 0x21,
	 .platform_data = &gpio_exp[1],
	 },
};

static struct i2c_pxa_platform_data i2c_pdata = {
	.fast_mode = 1,
};

/******************************************************************************
 * Voltage Regulator
 ******************************************************************************/
static struct regulator_consumer_supply max8588_consumers[] = {
	{
	 .supply = "vcc_core",
	 }
};

static struct regulator_init_data max8588_v3_info = {
	.constraints = {
			.name = "vcc_core range",
			.min_uV = 1000000,
			.max_uV = 1705000,
			.always_on = 1,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
			},
	.num_consumer_supplies = ARRAY_SIZE(max8588_consumers),
	.consumer_supplies = max8588_consumers,
};

static struct max1586_subdev_data max8588_subdevs[] = {
	{
	 .name = "vcc_core",
	 .id = MAX1586_V3,
	 .platform_data = &max8588_v3_info},
};

static struct max1586_platform_data max8588_info = {
	.subdevs = max8588_subdevs,
	.num_subdevs = ARRAY_SIZE(max8588_subdevs),
	.v3_gain = MAX1586_GAIN_NO_R24,
};

static struct i2c_board_info asusp525_pi2c_board_info[] = {
	{
	 I2C_BOARD_INFO("max1586", 0x14),
	 .platform_data = &max8588_info,
	 },
};

/******************************************************************************
 * LEDs
 ******************************************************************************/
static struct gpio_led asusp525_gpio_leds[] = {
	{
	 .name = "asus:white:keylight",
	 .default_trigger = "backlight",
	 .gpio = GPIO_ASUSP525_LED_KEYLED,
	 },
	{
	 .name = "asus:orange:warning",
	 .default_trigger = "none",
	 .gpio = GPIO_ASUSP525_LED_ORANGE,
	 },
	{
	 .name = "asus:blue:wireless",
	 .default_trigger = "none",
	 .gpio = GPIO_ASUSP525_LED_WIRELESS,
	 },
	{
	 .name = "asus:white:flash",
	 .default_trigger = "none",
	 .gpio = GPIO_ASUSP525_LED_FLASHLIGHT,
	 },
	{
	 .name = "asus:vibra",
	 .default_trigger = "none",
	 .gpio = GPIO_ASUSP525_LED_VIBRA,
	 },
};

static struct gpio_led_platform_data asusp525_gpio_leds_platform_data = {
	.leds = asusp525_gpio_leds,
	.num_leds = ARRAY_SIZE(asusp525_gpio_leds),
};

static struct platform_device asusp525_leds = {
	.name = "leds-gpio",
	.id = -1,
	.dev = {
		.platform_data = &asusp525_gpio_leds_platform_data,
		},
};

/******************************************************************************
 * SD/MMC
 ******************************************************************************/
static struct pxamci_platform_data asusp525_mci_platform_data = {
	.ocr_mask = MMC_VDD_32_33 | MMC_VDD_33_34,
	.get_ro = 0,
	.gpio_card_detect = -1,
	.gpio_card_ro = -1,
	.gpio_power = -1,
};

/******************************************************************************
 * USB Device
 ******************************************************************************/
static void udc_power_command(int cmd)
{
	switch (cmd) {
	case PXA2XX_UDC_CMD_DISCONNECT:
		UP2OCR &= ~(UP2OCR_DMPUE | UP2OCR_DPPUE | UP2OCR_HXOE);
		gpio_set_value_cansleep(GPIO_ASUSP525_USB_PULLUP, 0);
		break;
	case PXA2XX_UDC_CMD_CONNECT:
		UP2OCR |= (UP2OCR_DMPUE | UP2OCR_DPPUE | UP2OCR_HXOE);
		gpio_set_value_cansleep(GPIO_ASUSP525_USB_PULLUP, 1);
		break;
	default:
		printk(KERN_INFO "udc_control: unknown command (0x%x)!\n", cmd);
		break;
	}
}

static struct pxa2xx_udc_mach_info asusp525_udc_info __initdata = {
	.gpio_vbus = GPIO_ASUSP525_USB_CABLE_DETECT,
	.gpio_pullup = -1,
	.udc_command = udc_power_command,
};

/******************************************************************************
 * Matrix keyboard
 ******************************************************************************/
static unsigned int asusp525_mkbd[] = {
	KEY(0, 0, KEY_3),
	KEY(0, 1, KEY_6),
	KEY(0, 2, KEY_9),
	KEY(0, 3, KEY_F2),
	KEY(0, 4, KEY_F1),
	KEY(0, 5, KEY_SPACE),

	KEY(1, 0, KEY_2),
	KEY(1, 1, KEY_5),
	KEY(1, 2, KEY_8),
	KEY(1, 3, KEY_OK),	/* OK key */
	KEY(1, 4, KEY_MENU),	/* Circle key */
	KEY(1, 5, KEY_0),

	KEY(2, 0, KEY_1),
	KEY(2, 1, KEY_4),
	KEY(2, 2, KEY_7),
	KEY(2, 3, KEY_VOLUMEUP),
	KEY(2, 4, KEY_VOLUMEDOWN),
	KEY(2, 5, KEY_KPASTERISK),

};

static struct pxa27x_keypad_platform_data asusp525_keypad_data = {
	.matrix_key_rows = 3,
	.matrix_key_cols = 6,
	.matrix_key_map = asusp525_mkbd,
	.matrix_key_map_size = ARRAY_SIZE(asusp525_mkbd),

	.debounce_interval = 30,
};

/******************************************************************************
 * GPIO Keys
 ******************************************************************************/
static struct gpio_keys_button asusp525_gpio_buttons[] = {
	{KEY_P, GPIO_ASUSP525_POWER_KEY_N, 1, "Power Button"},
	{KEY_P, GPIO_ASUSP525_BATTERY_DOOR, 1, "Battery Door"},
};

static struct gpio_keys_platform_data asusp525_buttons_data = {
	.buttons = asusp525_gpio_buttons,
	.nbuttons = ARRAY_SIZE(asusp525_gpio_buttons),
};

static struct platform_device asusp525_buttons = {
	.name = "gpio-keys",
	.id = -1,
	.dev = {
		.platform_data = &asusp525_buttons_data,
		},
};

/******************************************************************************
 * Framebuffer
 ******************************************************************************/
static void asusp525_lcd_power(int on, struct fb_var_screeninfo *si)
{
	if (on) {
		gpio_set_value_cansleep(GPIO_ASUSP525_LCD_POWER1, 1);
		gpio_set_value_cansleep(GPIO_ASUSP525_LCD_POWER2, 1);

	} else {
		gpio_set_value_cansleep(GPIO_ASUSP525_LCD_POWER2, 0);
		gpio_set_value_cansleep(GPIO_ASUSP525_LCD_POWER1, 0);
	}

}

static struct pxafb_mode_info asusp525_ltm0305a776c = {
	.pixclock = 156000,
	.xres = 240,
	.yres = 320,
	.bpp = 16,
	.hsync_len = 10,
	.left_margin = 20,
	.right_margin = 10,
	.vsync_len = 2,
	.upper_margin = 2,
	.lower_margin = 2,
	.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
};

static struct pxafb_mach_info asusp525_pxafb_info = {
	.modes = &asusp525_ltm0305a776c,
	.num_modes = 1,
	.lcd_conn = LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_RISE,
	.lccr0 = LCCR0_Act | LCCR0_Sngl | LCCR0_Color,
	.lccr3 = LCCR3_OutEnH | LCCR3_PixRsEdg,
	.pxafb_lcd_power = asusp525_lcd_power,
};

/******************************************************************************
 * LCD Backlight
 ******************************************************************************/
static struct platform_pwm_backlight_data asusp525_backlight_data = {
	.pwm_id = 0,
	.max_brightness = 255,
	.dft_brightness = 150,
	.pwm_period_ns = 19692,
};

static struct platform_device asusp525_backlight = {
	.name = "pwm-backlight",
	.dev = {
		.parent = &pxa27x_device_pwm0.dev,
		.platform_data = &asusp525_backlight_data,
		},
};

/******************************************************************************
 * Power Supply
 ******************************************************************************/
static char *supplicants[] = {
	"main_battery"
};

static int asusp525_usb_online(void)
{
	return !gpio_get_value(GPIO_ASUSP525_USB_CHARGE_DETECT_N);
}

static int asusp525_ac_online(void)
{
	return !gpio_get_value(GPIO_ASUSP525_USB_AC_DETECT_N);
}

static void asusp525_set_charge(int mode)
{
	gpio_set_value(GPIO_ASUSP525_CHARGE_ENABLE,
		       (mode == PDA_POWER_CHARGE_USB));
}

static struct pda_power_pdata power_pdata = {
	.is_usb_online = asusp525_usb_online,
	.is_ac_online = asusp525_ac_online,
	.set_charge = asusp525_set_charge,
	.supplied_to = supplicants,
	.num_supplicants = ARRAY_SIZE(supplicants),
};

static struct resource power_resources[] = {
	[0] = {
	       .name = "ac",
	       .start = gpio_to_irq(GPIO_ASUSP525_USB_CABLE_DETECT),
	       .end = gpio_to_irq(GPIO_ASUSP525_USB_CABLE_DETECT),
	       .flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE |
	       IORESOURCE_IRQ_LOWEDGE,
	       },
	[1] = {
	       .name = "usb",
	       .start = gpio_to_irq(GPIO_ASUSP525_USB_CABLE_DETECT),
	       .end = gpio_to_irq(GPIO_ASUSP525_USB_CABLE_DETECT),
	       .flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE |
	       IORESOURCE_IRQ_LOWEDGE,
	       },
};

static struct platform_device asusp525_powerdev = {
	.name = "pda-power",
	.id = -1,
	.resource = power_resources,
	.num_resources = ARRAY_SIZE(power_resources),
	.dev = {
		.platform_data = &power_pdata,
		},
};

/******************************************************************************
 * WM97xx battery
 ******************************************************************************/
static struct wm97xx_batt_pdata asusp525_battery_data = {
	.batt_aux = WM97XX_AUX_ID1,
	.temp_aux = WM97XX_AUX_ID3,
	.charge_gpio = -1,
	.max_voltage = 0xa30,
	.min_voltage = 0x800,
	.batt_mult = 1,
	.batt_div = 1,
	.temp_div = 1,
	.temp_mult = 1,
	.batt_tech = POWER_SUPPLY_TECHNOLOGY_LION,
	.batt_name = "main_battery",
};

static struct platform_device asusp525_rfk = {
	.name = "asusp525-rfk",
};

static struct platform_device asusp525_joy = {
	.name = "asusp525-joy",
};

static struct wm97xx_pdata wm97xx_data = {
	.batt_pdata = &asusp525_battery_data,
};

static struct platform_device asusp525_touchscreen = {
	.name = "wm97xx-ts",
	.id = -1,
	.dev = {
		.platform_data = &wm97xx_data,
		}
};

static struct platform_device asusp525_kbd = {
	.name = "asusp525-kbd",
};

static struct platform_device pxa2xx_pcm = {
	.name = "pxa2xx-pcm",
	.id = -1,
};

static struct platform_device asusp525_sound = {
	.name = "asusp5x5-audio",
	.id = -1,
};

/******************************************************************************
 * MT9M111 Camera Sensor
 ******************************************************************************/
struct pxacamera_platform_data asusp525_pxacamera_platform_data = {
	.flags  = PXA_CAMERA_MASTER | PXA_CAMERA_DATAWIDTH_8 |
		PXA_CAMERA_PCLK_EN | PXA_CAMERA_MCLK_EN,
	.mclk_10khz = 5000,
};

static struct i2c_board_info asusp525_cam_devices[] = {
	{
		I2C_BOARD_INFO("mt9m111", 0x5d),
	},
};

static int asusp525_cam_power(struct device *dev, int on)
{
	printk("[ASUS P525]: Setting camera power to %d\n", on);
	gpio_set_value(EGPIO_BANK0(1), on);
	gpio_set_value(EGPIO_BANK0(0), !on);

	return 0;
}


static struct soc_camera_link iclink = {
	.bus_id		= 0,
	.power		= asusp525_cam_power,
	.board_info	= &asusp525_cam_devices[0],
	.i2c_adapter_id	= 0,
	.module_name	= "mt9m111",
};

static struct platform_device asusp525_camera = {
	.name = "soc-camera-pdrv",
	.id = 0,
	.dev = {
		.platform_data = &iclink,	
	}
};

static struct platform_device *devices[] __initdata = {
	&asusp525_buttons,
	&asusp525_leds,
	&asusp525_backlight,
	&asusp525_touchscreen,
	&asusp525_rfk,
	&asusp525_joy,
	&asusp525_kbd,
	&pxa2xx_pcm,
	&asusp525_sound,
	&asusp525_powerdev,
	&asusp525_camera,
};

static void __init asusp525_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(asusp525_pin_config));

	pxa_set_i2c_info(&i2c_pdata);
	i2c_register_board_info(0, ARRAY_AND_SIZE(asusp525_i2c_board_info));

//      pxa27x_set_i2c_power_info(NULL);
//      i2c_register_board_info(1, ARRAY_AND_SIZE(asusp525_pi2c_board_info));

	set_pxa_fb_info(&asusp525_pxafb_info);

	pxa_set_udc_info(&asusp525_udc_info);

	pxa_set_keypad_info(&asusp525_keypad_data);

	pxa_set_mci_info(&asusp525_mci_platform_data);

	pxa_set_ac97_info(NULL);

	pxa_set_ficp_info(&asusp525_ficp_info);

	//wm97xx_bat_set_pdata(&asusp525_battery_data);

	pxa_set_camera_info(&asusp525_pxacamera_platform_data);
	platform_add_devices(devices, ARRAY_SIZE(devices));
}

MACHINE_START(ASUSP525, "Asus P525")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= 0xa0000100,
	.map_io		= pxa_map_io,
	.init_irq	= pxa27x_init_irq,
	.init_machine	= asusp525_init,
	.timer		= &pxa_timer,
MACHINE_END
