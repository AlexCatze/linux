/**
 *
 * Hardware definitions for the Toshiba Portege G900
 *
 * Initially based on Asus P535 Android port.
 * Most of initial work (FB, MMC, keyboard) was done by
 * "El Tuba" <tuba.linux@gmail.com>
 * and
 * LeStat (Eugene Nikitin).
 *
 * Hardware info and ideas provided by AngellFear <>
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ioport.h>

#include <linux/platform_device.h>

#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>

#include <linux/gpio_keys.h>
#include <linux/pda_power.h>
#include <linux/pwm_backlight.h>
#if 0
#include <linux/wm97xx_batt.h>
#endif
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>

#include <asm/gpio.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/mfp-pxa27x.h>
#include <mach/pxa2xx-regs.h>
#include <mach/ohci.h>
#include <linux/usb/gpio_vbus.h>
#include <mach/pxa2xx_spi.h>
#include <mach/pxa27x-udc.h>
#include <mach/udc.h>
#include <plat/i2c.h>
#include <mach/mmc.h>
#include <mach/audio.h>
#include <mach/regs-ac97.h>
#include <mach/pxa27x_keypad.h>
#ifdef NEW_G900_FB
#include <mach/pxafb.h>
#endif
#include <linux/i2c/ak4183.h>
#include <linux/mfd/wm8350/audio.h>
#include <linux/mfd/wm8350/core.h>

#include <mach/g900-init.h>
#include <mach/g900-gpio.h>

#include "generic.h"
#include "devices.h"

#define G900_CFG_IN(pin, af)		\
	((MFP_CFG_DEFAULT & ~(MFP_AF_MASK | MFP_DIR_MASK)) |\
	 (MFP_PIN(pin) | MFP_##af | MFP_DIR_IN))
#define G900_CFG_OUT(pin, af, state)	\
	((MFP_CFG_DEFAULT & ~(MFP_AF_MASK | MFP_DIR_MASK | MFP_LPM_STATE_MASK)) |\
	 (MFP_PIN(pin) | MFP_##af | MFP_DIR_OUT | MFP_LPM_##state))

static unsigned long g900_pin_config[] __initdata = {
	/* MMC */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,
#if 0
	GPIO12_GPIO,				/* mmc detect */
#endif

	/* Sound */
	GPIO113_AC97_nRESET,
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	/* AC97_SDATA_IN_1 - input ???? */
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,
	GPIO89_AC97_SYSCLK,

	/* Bluetooth ??? */
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,
	GPIO45_BTUART_RTS,
	//G900_CFG_OUT(GPIO83_BT_ON, AF0, DRIVE_LOW),
	//G900_CFG_OUT(83, AF0, DRIVE_LOW), this pin switched from (OUT 0) to (IN F) after enabling BT, possibly 
	G900_CFG_OUT(114, AF0, DRIVE_HIGH),
#if 0
	MIO_CFG_IN(GPIO14_BT_nACTIVITY, AF0),
	MIO_CFG_OUT(GPIO83_BT_ON, AF0, DRIVE_LOW),
	MIO_CFG_OUT(GPIO77_BT_UNKNOWN1, AF0, DRIVE_HIGH),
	MIO_CFG_OUT(GPIO86_BT_MAYBE_nRESET, AF0, DRIVE_HIGH),
	From ezx.c
	/*  bluetooth (bcm2045) */
	GPIO13_GPIO | WAKEUP_ON_EDGE_RISE,      /*  HOSTWAKE */
	GPIO37_GPIO,                            /*  RESET */
	GPIO57_GPIO,                            /*  WAKEUP */
#endif

	/* SPI */
	/* guessed by pin AF and direction */
	GPIO11_SSP2_RXD,
	GPIO38_SSP3_TXD,
	GPIO87_SSP2_TXD,
#if 0
	GPIO19_SSP2_SCLK_OUT        MFP_CFG_OUT(GPIO19, AF1, DRIVE_LOW)
	GPIO34_SSP3_SCLK_OUT        MFP_CFG_OUT(GPIO34, AF3, DRIVE_LOW)
	GPIO88_SSP2_SFRM_OUT        MFP_CFG_OUT(GPIO88, AF3, DRIVE_LOW)
#endif

	/* Variable Latency I/O Ready Pin */
	/* An external variable-latency I/O (VLIO) device asserts RDY when it is ready to transfer data. */
	GPIO18_RDY,
	/* PC Card Write Enable */
	/* Enables writes to PC Card memory and PC Card attribute space. Also serves as the write enable signal for variable-latency I/O. */
	GPIO49_nPWE,
	/* DMA Request 0 */
	/* DMA request from an external companion chip. */
	GPIO20_DREQ_0,
	/* Pulse Width Modulation Channel 1 */
	/* Pulse width modulator channel 1 output. */
	GPIO17_PWM1_OUT,

	/* TouchScreen IRQ */
	GPIO76_GPIO,

	/* Static Chip Selects */
	/* Chip selects to static memory devices such as ROM and flash,
	 * individually programmable in the memory configuration registers.
	 * nCS<5:0> can be used with variable-latency I/O devices. nCS<3:0> can be used with
	 * synchronous flash. */
	GPIO78_nCS_2,
	GPIO80_nCS_4,
	GPIO33_nCS_5,

	/* USB */ /* UHC - USBH?_PWR (IN), USBH?_PEN (OUT, DRIVE_LOW) */ /* UDC - interrupt? */
	G900_CFG_IN(GPIO40_nUSB_DETECT, AF0),
	GPIO41_USB_P2_7,
	G900_CFG_OUT(GPIO75_USB_ENABLE, AF0, DRIVE_LOW),
	G900_CFG_OUT(GPIO93_USB_ENABLE, AF0, DRIVE_LOW),

	/* Leds */
	G900_CFG_OUT(GPIO16_LED_nVibra, AF0, DRIVE_HIGH),  // or should be GPIO16_PWM0_OUT ?
	G900_CFG_OUT(GPIO37_LED_nFlash, AF0, DRIVE_HIGH),  // it can't be GPIO37_USB_P2_8 ?
	G900_CFG_OUT(GPIO85_LED_nKeyboard, AF0, DRIVE_HIGH),
	G900_CFG_OUT(GPIO86_LED_nKeypad, AF0, DRIVE_HIGH),

	/* Power button */
	G900_CFG_IN(GPIO3_BTN_nPower, AF0),
	/* Keyboard open */
	G900_CFG_IN(GPIO9_BTN_nKBOpen, AF0),
	/* Headset button */
	G900_CFG_IN(GPIO10_BTN_nHeadSet, AF0),
	/* Headset jack insert event */
	G900_CFG_IN(GPIO51_BTN_nJackInsert, AF0),

	/*  MATRIX KEYPAD */
	GPIO100_KP_MKIN_0 | WAKEUP_ON_LEVEL_HIGH,
	GPIO101_KP_MKIN_1 | WAKEUP_ON_LEVEL_HIGH,
	GPIO102_KP_MKIN_2 | WAKEUP_ON_LEVEL_HIGH,
	GPIO97_KP_MKIN_3 | WAKEUP_ON_LEVEL_HIGH,
	GPIO98_KP_MKIN_4 | WAKEUP_ON_LEVEL_HIGH,
	GPIO99_KP_MKIN_5 | WAKEUP_ON_LEVEL_HIGH,
	GPIO95_KP_MKIN_6 | WAKEUP_ON_LEVEL_HIGH,
	GPIO13_KP_MKIN_7 | WAKEUP_ON_LEVEL_HIGH,
	GPIO103_KP_MKOUT_0,
	GPIO104_KP_MKOUT_1,
	GPIO105_KP_MKOUT_2,
	GPIO106_KP_MKOUT_3,
	GPIO107_KP_MKOUT_4,
	GPIO108_KP_MKOUT_5,
	GPIO35_KP_MKOUT_6,
	GPIO22_KP_MKOUT_7,

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,
};

static unsigned int g900_matrix_keys[] = {
	KEY(0, 0, KEY_C),
	KEY(0, 1, KEY_Z),
	KEY(0, 2, KEY_B),
	KEY(0, 3, KEY_X),
	KEY(0, 4, KEY_V),
	KEY(0, 5, KEY_LEFTSHIFT),
	KEY(0, 6, KEY_F1), /* Left Softkey on keyboard */
	KEY(0, 7, KEY_N),

	KEY(1, 0, KEY_LANGUAGE), /* RU button */
	KEY(1, 1, KEY_LEFTMETA), /* Windows key on keyboard */
	KEY(1, 2, KEY_SPACE),
	KEY(1, 3, KEY_OK),
	KEY(1, 4, KEY_2), 
	KEY(1, 5, KEY_FN), /* Keyboard modificator key */
	KEY(1, 6, KEY_F2), /* Right Softkey on keyboard */
	KEY(1, 7, KEY_COMMA),

	KEY(2, 0, KEY_R),
	KEY(2, 1, KEY_W),
	KEY(2, 2, KEY_Y),
	KEY(2, 3, KEY_E),
	KEY(2, 4, KEY_T),
	KEY(2, 5, KEY_Q),
	KEY(2, 6, KEY_U),
	KEY(2, 7, KEY_J),

	KEY(3, 0, KEY_F),
	KEY(3, 1, KEY_S),
	KEY(3, 2, KEY_G),
	KEY(3, 3, KEY_D),
	KEY(3, 4, KEY_H),
	KEY(3, 5, KEY_A),
	KEY(3, 6, KEY_WWW),
	KEY(3, 7, KEY_ENTER),

	KEY(4, 0, KEY_BACKSPACE),
	KEY(4, 1, KEY_P),
	KEY(4, 2, KEY_O),
	KEY(4, 3, KEY_I),
	KEY(4, 4, KEY_1),
	KEY(4, 5, KEY_VOLUMEDOWN),
	KEY(4, 6, KEY_VOLUMEUP),
	KEY(4, 7, KEY_CAMERA),

	KEY(5, 0, KEY_KPENTER), /* Middle button below the screen */
	KEY(5, 1, KEY_UP),
	KEY(5, 2, KEY_APOSTROPHE),
	KEY(5, 3, KEY_M),
	KEY(5, 4, KEY_PHONE),
	KEY(5, 5, KEY_UP),
	KEY(5, 6, KEY_OK),
	KEY(5, 7, KEY_CANCEL),

	KEY(6, 0, KEY_LEFTCTRL),
	KEY(6, 1, KEY_SLASH), /* Square braces */
	KEY(6, 2, KEY_L),
	KEY(6, 3, KEY_K),
	KEY(6, 4, KEY_PROG2), /* Right Softkey below the screen */
	KEY(6, 5, KEY_LEFT),
	KEY(6, 6, KEY_PROG1), /* Left Softkey below the screen */
	KEY(6, 7, KEY_DOWN),

	KEY(7, 0, KEY_RIGHT),
	KEY(7, 1, KEY_DOWN),
	KEY(7, 2, KEY_LEFT),
	KEY(7, 3, KEY_DOT),
	KEY(7, 4, KEY_MAIL),
	KEY(7, 5, KEY_RIGHT),
	KEY(7, 6, KEY_RIGHTMETA), /* Windows key below the screen */
	KEY(7, 7, KEY_HOME)
};

static struct pxa27x_keypad_platform_data g900_keypad_platform_data = {
	.matrix_key_rows	= 8,
	.matrix_key_cols	= 8,
	.matrix_key_map		= g900_matrix_keys,
	.matrix_key_map_size	= ARRAY_SIZE(g900_matrix_keys),
//	.direct_key_map		= { KEY_CONNECT },
//	.direct_key_num		= 1,

	.debounce_interval	= 30,
};

/*
 * GPIO Key Configuration
 */
#define G900_KEY(key, _gpio, _desc, _wakeup) \
	{ .code = (key), .gpio = (_gpio), .active_low = 0, \
	.desc = (_desc), .type = EV_KEY, .wakeup = (_wakeup) }
#define G900_SW(key, _gpio, _desc, _wakeup) \
	{ .code = (key), .gpio = (_gpio), .active_low = 1, \
	.desc = (_desc), .type = EV_SW, .wakeup = (_wakeup), .debounce_interval = 300 }
static struct gpio_keys_button g900_button_table[] = {
	// TODO Need to test this more. Check tosa.c for example
	//G900_KEY(KEY_EXIT, GPIO3_BTN_nPower, "Power button", 1),
	{
		.type	= EV_PWR,
		.code	= KEY_RESERVED,
		.gpio   = GPIO3_BTN_nPower,
		.desc   = "Poweron",
		.wakeup = 1,
		.active_low = 1,
	},
	G900_KEY(KEY_UNKNOWN, GPIO9_BTN_nKBOpen, "Keyboard open", 1), // Maybe use SW_TABLET_MODE here or SW_KEYPAD_SLIDE
	G900_KEY(KEY_PLAY, GPIO10_BTN_nHeadSet, "HeadSet button", 0),
	G900_SW(SW_HEADPHONE_INSERT, GPIO51_BTN_nJackInsert, "HeadPhone insert", 0),
};

static struct gpio_keys_platform_data g900_gpio_keys_data = {
	.buttons  = g900_button_table,
	.nbuttons = ARRAY_SIZE(g900_button_table),
};

/*
 * Leds and vibrator
 */
/* Strange, but if used none, g900 is endlessly vibrating and flash light is on */
#define ONE_LED(_gpio, _name) \
{ .gpio = (_gpio), .name = (_name), .active_low = false, .default_trigger = "none" }
//{ .gpio = (_gpio), .name = (_name), .active_low = true, .default_trigger = "none" }
//{ .gpio = (_gpio), .name = (_name), .active_low = true, .default_trigger = "default-on" }
static struct gpio_led gpio_leds[] = {
	ONE_LED(GPIO16_LED_nVibra, "g900:none:vibra"),
	ONE_LED(GPIO37_LED_nFlash, "g900:white:flash"),
	ONE_LED(GPIO85_LED_nKeyboard, "g900:white:keyboard"),
	ONE_LED(GPIO86_LED_nKeypad, "g900:white:keypad"),
};

static struct gpio_led_platform_data g900_gpio_leds_data = {
	.leds = gpio_leds,
	.num_leds = ARRAY_SIZE(gpio_leds),
};

/******************************************************************************
 * USB Host (UHC, OHCI)
 ******************************************************************************/
#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static struct pxaohci_platform_data g900_ohci_platform_data = {
	.port_mode	= PMM_PERPORT_MODE,
	.flags		= ENABLE_PORT1 | ENABLE_PORT2 |
			POWER_CONTROL_LOW | POWER_SENSE_LOW,
//	.flags		= ENABLE_PORT_ALL | POWER_CONTROL_LOW | POWER_SENSE_LOW,
};

static void __init g900_uhc_init(void)
{
	printk(KERN_INFO "pxa_set_ohci_info\n");
	// TODO do we need to do something here?
	pxa_set_ohci_info(&g900_ohci_platform_data);
}
#else
static inline void g900_uhc_init(void) {}
#endif

/******************************************************************************
 * USB Gadget (UDC)
 ******************************************************************************/
#if defined(CONFIG_USB_GADGET_PXA27X)||defined(CONFIG_USB_GADGET_PXA27X_MODULE)
static struct pxa2xx_udc_mach_info g900_udc_info __initdata = {
//	.udc_is_connected	= is_usb_connected,
	.gpio_vbus		= GPIO40_nUSB_DETECT,
//	.gpio_vbus		= GPIO41_USB_P2_7,
//	.gpio_pullup		= GPIO93_USB_ENABLE,
	.gpio_pullup		= GPIO75_USB_ENABLE,
//	.udc_command		= g900_udc_command,
};

static void __init g900_udc_init(void)
{
	printk(KERN_INFO "pxa_set_udc_info\n");
	// TODO do we need to do something here?
	pxa_set_udc_info(&g900_udc_info);
}
#else
static inline void g900_udc_init(void) {}
#endif

struct gpio_vbus_mach_info gpio_vbus_data = {
	.gpio_vbus = GPIO40_nUSB_DETECT,
	.gpio_vbus_inverted = 1,
	.gpio_pullup = -1,
};

#if 0
/*
 * USB UDC
 * 1. вешаемся на прерывание gpio_40.
 * 2. при вызове прерывания проверяем gpio_41
 * если оно равно 1 то переходим в режим udc (client)
 * усло оно равно 0 то устанавливаем gpio_75 в 1 и потом gpio_93 в 1
 * и переходим в режим хоста.
 * GPIO40_nUSB_DETECT
 * GPIO41_nUSB_DETECT USB_P2_7
 * GPIO75_USB_ENABLE
 * GPIO93_USB_ENABLE
 */
static int is_usb_connected(void)
{
	//return !gpio_get_value(GPIO40_nUSB_DETECT);
	int ret = gpio_get_value(GPIO40_nUSB_DETECT);
	printk(KERN_INFO "UDC isConnect %d\n", !ret);
	return !ret;
}

static void g900_udc_command(int cmd)
{
	printk(KERN_INFO "UDC: cmd %d\n", cmd);

	switch(cmd){
		case PXA2XX_UDC_CMD_DISCONNECT:
			//pxa_gpio_mode (USBP_PULLUP | GPIO_IN);
			break;
		case PXA2XX_UDC_CMD_CONNECT:
			//pxa_gpio_mode (USBP_PULLUP | GPIO_OUT);
			break;
		default:
			printk(KERN_INFO "g900_udc_control: unknown command!\n");
			break;
	}
}

/*
 * USB "Transceiver"
 */
static struct resource gpio_vbus_resource = {
	.flags	= IORESOURCE_IRQ,
	.start	= IRQ_MAGICIAN_VBUS,
	.end	= IRQ_MAGICIAN_VBUS,
};

static struct gpio_vbus_mach_info gpio_vbus_data = {
	.gpio_pullup = GPIO27_MAGICIAN_USBC_PUEN,
	.gpio_vbus   = EGPIO_MAGICIAN_CABLE_STATE_USB,
};

static struct platform_device gpio_vbus = {
	.name          = "gpio-vbus",
	.id            = -1,
	.num_resources = 1,
	.resource      = &gpio_vbus_resource,
	.dev = {
		.platform_data = &gpio_vbus_info,
	},
};


	static struct pxa2xx_udc_mach_info mioa701_udc_info = {
	        .udc_is_connected = is_usb_connected,
		        .gpio_pullup      = GPIO22_USB_ENABLE,
			};

			struct gpio_vbus_mach_info gpio_vbus_data = {
			        .gpio_vbus = GPIO13_nUSB_DETECT,
				        .gpio_vbus_inverted = 1,
					        .gpio_pullup = -1,
						};
#endif

#ifdef CONFIG_MMC_PXA
static struct pxamci_platform_data g900_mci_platform_data = {
	.ocr_mask		= MMC_VDD_32_33 | MMC_VDD_33_34,
//	.init			= g900_mci_init,
//	.setpower		= g900_mci_set_power,
	.gpio_card_detect	= GPIO_NR_G900_SD_DETECT_N,
	.gpio_card_ro		= -1,
	.gpio_power		= GPIO_NR_G900_SD_POWER_N,
//	.get_ro			= g900_mci_get_ro,
//	.exit			= g900_mci_exit,
};

static void __init g900_mmc_init(void)
{
	pxa_set_mci_info(&g900_mci_platform_data);
}
#else
static void __init g900_mmc_init(void)
{
	pr_debug("G900 mmc disabled\n");
}
#endif

#ifdef NEW_G900_FB

/**
 * Our framebuffer is based now on Acer n311 framebuffer
 * (http://code.google.com/p/acer-n311-linux/).
 * There was an idea that we can use pxafb, with some modifications (just as
 * FB). Possibly I'll check this idea somewhen later.
 */

//static struct pxafb_mode_info toshiba_ltm04c380k_mode = {
static struct pxafb_mode_info toshiba_g900_mode = {
	.pixclock		= 50000,
	.xres			= 480,
	.yres			= 800,
	.bpp			= 16,
	.hsync_len		= 1,
	.left_margin		= 0x9f,
	.right_margin		= 1,
	.vsync_len		= 44,
	.upper_margin		= 0,
	.lower_margin		= 0,
	.sync			= FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT,
};
//	.red =		{ 11, 5, 0 },
//    	.green =	{ 5, 6, 0 },
//	.blue =		{ 0, 5, 0 },
	//The following values seem to not affect the screeen behavior	
//	.pixclock	= 10,
//	.left_margin	= 0,
//	.right_margin	= 0,
//	.upper_margin	= 33,
//	.lower_margin	= 10,
//	.hsync_len	= 96,
//	.vsync_len	= 2,
//	.vmode		= FB_VMODE_NONINTERLACED

static struct pxafb_mach_info g900_pxafb_info = {
	.num_modes      	= 1,
	.modes			= &toshiba_g900_mode,
	.lcd_conn		= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL,
};
#endif
//#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
static struct platform_pwm_backlight_data g900_backlight_data = {
	.pwm_id		= 1,
	.max_brightness	= 100,
	.dft_brightness	= 75,
	.pwm_period_ns	= 861280, /* 76.9 ns * 64 * 175 */
};

static struct platform_device g900_backlight_device = {
	.name		= "pwm-backlight",
	.dev		= {
		.parent = &pxa27x_device_pwm1.dev,
		.platform_data = &g900_backlight_data,
	},
};

static void __init g900_backlight_register(void)
{
	int ret = platform_device_register(&g900_backlight_device);
	if (ret)
		printk(KERN_ERR "g900: failed to register backlight device: %d\n", ret);
}
//#else
//#define mainstone_backlight_register()	do { } while (0)
//#endif

// here will be power management
#if 0
/*
 * Power Supply
 */
static char *supplicants[] = {
	"mioa701_battery"
};

static int is_ac_connected(void)
{
	return gpio_get_value(GPIO96_AC_DETECT);
}

static void mioa701_set_charge(int flags)
{
	gpio_set_value(GPIO9_CHARGE_EN, (flags == PDA_POWER_CHARGE_USB));
}

static struct pda_power_pdata power_pdata = {
	.is_ac_online	= is_ac_connected,
	.is_usb_online	= is_usb_connected,
	.set_charge = mioa701_set_charge,
	.supplied_to = supplicants,
	.num_supplicants = ARRAY_SIZE(supplicants),
};

static struct resource power_resources[] = {
	[0] = {
		.name	= "ac",
		.start	= gpio_to_irq(GPIO96_AC_DETECT),
		.end	= gpio_to_irq(GPIO96_AC_DETECT),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE |
		IORESOURCE_IRQ_LOWEDGE,
	},
	[1] = {
		.name	= "usb",
		.start	= gpio_to_irq(GPIO13_nUSB_DETECT),
		.end	= gpio_to_irq(GPIO13_nUSB_DETECT),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE |
		IORESOURCE_IRQ_LOWEDGE,
	},
};

static struct platform_device power_dev = {
	.name		= "pda-power",
	.id		= -1,
	.resource	= power_resources,
	.num_resources	= ARRAY_SIZE(power_resources),
	.dev = {
		.platform_data	= &power_pdata,
	},
};

static struct wm97xx_batt_info mioa701_battery_data = {
	.batt_aux	= WM97XX_AUX_ID1,
	.temp_aux	= -1,
	.charge_gpio	= -1,
	.min_voltage	= 0xc00,
	.max_voltage	= 0xfc0,
	.batt_tech	= POWER_SUPPLY_TECHNOLOGY_LION,
	.batt_div	= 1,
	.batt_mult	= 1,
	.batt_name	= "mioa701_battery",
};

/*
 * Voltage regulation
 */
static struct regulator_consumer_supply max1586_consumers[] = {
	{
		.supply = "vcc_core",
	}
};

static struct regulator_init_data max1586_v3_info = {
	.constraints = {
		.name = "vcc_core range",
		.min_uV = 1000000,
		.max_uV = 1705000,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(max1586_consumers),
	.consumer_supplies = max1586_consumers,
};

static struct max1586_subdev_data max1586_subdevs[] = {
	{ .name = "vcc_core", .id = MAX1586_V3,
	  .platform_data = &max1586_v3_info },
};

static struct max1586_platform_data max1586_info = {
	.subdevs = max1586_subdevs,
	.num_subdevs = ARRAY_SIZE(max1586_subdevs),
	.v3_gain = MAX1586_GAIN_NO_R24, /* 700..1475 mV */
};

/*
 * Camera interface
 */
struct pxacamera_platform_data mioa701_pxacamera_platform_data = {
	.flags  = PXA_CAMERA_MASTER | PXA_CAMERA_DATAWIDTH_8 |
		PXA_CAMERA_PCLK_EN | PXA_CAMERA_MCLK_EN,
	.mclk_10khz = 5000,
};

static struct i2c_board_info __initdata mioa701_pi2c_devices[] = {
	{
		I2C_BOARD_INFO("max1586", 0x14),
		.platform_data = &max1586_info,
	},
};

/* Board I2C devices. */
static struct i2c_board_info __initdata mioa701_i2c_devices[] = {
	{
		I2C_BOARD_INFO("mt9m111", 0x5d),
	},
};

static struct soc_camera_link iclink = {
	.bus_id		= 0, /* Match id in pxa27x_device_camera in device.c */
	.board_info	= &mioa701_i2c_devices[0],
	.i2c_adapter_id	= 0,
	.module_name	= "mt9m111",
};

struct i2c_pxa_platform_data i2c_pdata = {
	.fast_mode = 1,
};

static pxa2xx_audio_ops_t mioa701_ac97_info = {
	.reset_gpio = 95,
};

/*
 * Mio global
 */

/* Devices */
#define MIO_PARENT_DEV(var, strname, tparent, pdata)	\
static struct platform_device var = {			\
	.name		= strname,			\
	.id		= -1,				\
	.dev		= {				\
		.platform_data = pdata,			\
		.parent	= tparent,			\
	},						\
};
#define MIO_SIMPLE_DEV(var, strname, pdata)	\
	MIO_PARENT_DEV(var, strname, NULL, pdata)

MIO_SIMPLE_DEV(mioa701_gpio_keys, "gpio-keys",	    &mioa701_gpio_keys_data)
MIO_PARENT_DEV(mioa701_backlight, "pwm-backlight",  &pxa27x_device_pwm0.dev,
		&mioa701_backlight_data);
MIO_SIMPLE_DEV(mioa701_led,	  "leds-gpio",	    &gpio_led_info)
MIO_SIMPLE_DEV(pxa2xx_pcm,	  "pxa2xx-pcm",	    NULL)
MIO_SIMPLE_DEV(mioa701_sound,	  "mioa701-wm9713", NULL)
MIO_SIMPLE_DEV(mioa701_board,	  "mioa701-board",  NULL)
MIO_SIMPLE_DEV(gpio_vbus,	  "gpio-vbus",      &gpio_vbus_data);
MIO_SIMPLE_DEV(mioa701_camera,	  "soc-camera-pdrv",&iclink);

static struct platform_device *devices[] __initdata = {
	&mioa701_gpio_keys,
	&mioa701_backlight,
	&mioa701_led,
	&pxa2xx_pcm,
	&mioa701_sound,
	&power_dev,
	&strataflash,
	&gpio_vbus,
	&mioa701_camera,
	&mioa701_board,
};

static void mioa701_machine_exit(void);

static void mioa701_poweroff(void)
{
	mioa701_machine_exit();
	arm_machine_restart('s', NULL);
}

static void mioa701_restart(char c, const char *cmd)
{
	mioa701_machine_exit();
	arm_machine_restart('s', cmd);
}

static struct gpio_ress global_gpios[] = {
	MIO_GPIO_OUT(GPIO9_CHARGE_EN, 1, "Charger enable"),
	MIO_GPIO_OUT(GPIO18_POWEROFF, 0, "Power Off"),
	MIO_GPIO_OUT(GPIO87_LCD_POWER, 0, "LCD Power"),
};

static void __init mioa701_machine_init(void)
{
	PSLR  = 0xff100000; /* SYSDEL=125ms, PWRDEL=125ms, PSLR_SL_ROD=1 */
	PCFR = PCFR_DC_EN | PCFR_GPR_EN | PCFR_OPDE;
	RTTR = 32768 - 1; /* Reset crazy WinCE value */
	UP2OCR = UP2OCR_HXOE;

	pxa2xx_mfp_config(ARRAY_AND_SIZE(mioa701_pin_config));
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);
	mio_gpio_request(ARRAY_AND_SIZE(global_gpios));
	bootstrap_init();
	set_pxa_fb_info(&mioa701_pxafb_info);
	mioa701_mci_info.detect_delay = msecs_to_jiffies(250);
	pxa_set_mci_info(&mioa701_mci_info);
	pxa_set_keypad_info(&mioa701_keypad_info);
	wm97xx_bat_set_pdata(&mioa701_battery_data);
	pxa_set_udc_info(&mioa701_udc_info);
	pxa_set_ac97_info(&mioa701_ac97_info);
	pm_power_off = mioa701_poweroff;
	arm_pm_restart = mioa701_restart;
	platform_add_devices(devices, ARRAY_SIZE(devices));
	gsm_init();

	i2c_register_board_info(1, ARRAY_AND_SIZE(mioa701_pi2c_devices));
	pxa_set_i2c_info(&i2c_pdata);
	pxa27x_set_i2c_power_info(NULL);
	pxa_set_camera_info(&mioa701_pxacamera_platform_data);
#endif

static int ts_get_pendown_state(void)
{
	//printk(KERN_INFO "\t >>> %s <<< \n", __FUNCTION__);
	return !gpio_get_value(GPIO_NR_G900_TOUCHSCREEN_GPIO);
}

static int ts_init(void)
{
	if (gpio_request(GPIO_NR_G900_TOUCHSCREEN_GPIO, "AK4183 pendown") < 0)
	{
		printk(KERN_ERR "can't get AK4183 pen down GPIO\n");
		return -1;
	}
	return 0;
}

struct ak4183_platform_data ak4183_info = {
	.model			= 4183,
	.get_pendown_state	= ts_get_pendown_state,
	.init_platform_hw	= ts_init,
	.x_plate_ohms		= 8000,
};

/* Board I2C devices. */
static struct i2c_board_info __initdata g900_i2c_devices[] = {
	{
		I2C_BOARD_INFO("ak4183", 0x48),
		.platform_data = &ak4183_info,
		.irq = IRQ_GPIO(GPIO_NR_G900_TOUCHSCREEN_GPIO),
	},
};

struct i2c_pxa_platform_data g900_i2c_pdata = {
	.fast_mode = 1,
};

/*  Devices */
#define G900_PARENT_DEV(var, strname, tparent, pdata)	\
	static struct platform_device var = {		\
	.name		= strname,			\
	.id		= -1,				\
	.dev		= {				\
		.platform_data	= pdata,		\
		.parent		= tparent,		\
	},						\
};
#define G900_SIMPLE_DEV(var, strname, pdata)	\
	G900_PARENT_DEV(var, strname, NULL, pdata)

#if 0
static struct platform_device g900_lcd = {
    .name = "g900-lcd",
};
#endif

G900_SIMPLE_DEV(g900_gpio_keys,	"gpio_keys",	&g900_gpio_keys_data)
G900_SIMPLE_DEV(g900_gpio_leds,	"leds-gpio",	&g900_gpio_leds_data)
G900_SIMPLE_DEV(g900_ts,	"g900-ts",	NULL)

G900_SIMPLE_DEV(pxa2xx_pcm,	"pxa2xx-pcm",	NULL)
G900_SIMPLE_DEV(g900_sound,	"g900-wm9714",	NULL)
G900_SIMPLE_DEV(gpio_vbus,	"gpio-vbus",	&gpio_vbus_data);

static struct platform_device *g900_devices[] __initdata = {
//	&g900_lcd,
//	&g900_backlight,
//	&g900_button,
//	&g900_keypad,
//	&g900_ac97,
//	&g900_i2c,
	&g900_gpio_keys,
	&g900_gpio_leds,
	&g900_ts,
	&pxa2xx_pcm,
	&g900_sound,
	&gpio_vbus,
};

static void __init g900_machine_init(void)
{
#if 0
	/* Copied from mioa701.c */
	PSLR  = 0xff100000; /* SYSDEL=125ms, PWRDEL=125ms, PSLR_SL_ROD=1 */
	PCFR = PCFR_DC_EN | PCFR_GPR_EN | PCFR_OPDE;
	RTTR = 32768 - 1; /* Reset crazy WinCE value */
	UP2OCR = UP2OCR_HXOE;
#endif
	GCR |= GCR_ACLINK_OFF;

	pxa2xx_mfp_config(ARRAY_AND_SIZE(g900_pin_config));

	//pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	//pxa_set_stuart_info(NULL);

#ifdef NEW_G900_FB
	set_pxa_fb_info(&g900_pxafb_info);
#endif

	g900_uhc_init();
	g900_udc_init();

	printk(KERN_INFO "pxa_set_ac97_info\n");
	pxa_set_ac97_info(NULL);

	//mxc_iomux_mode(IOMUX_MODE(MX31_PIN_CSPI2_MOSI, IOMUX_CONFIG_ALT1));
	//mxc_iomux_mode(IOMUX_MODE(MX31_PIN_CSPI2_MISO, IOMUX_CONFIG_ALT1));

	g900_mmc_init();
	pxa_set_keypad_info(&g900_keypad_platform_data);

	i2c_register_board_info(0, ARRAY_AND_SIZE(g900_i2c_devices));
	pxa_set_i2c_info(&g900_i2c_pdata);
	//pxa_set_i2c_info(NULL);
	//pxa27x_set_i2c_power_info(NULL);

	/* Strange, if moved above i2c, then vibrating and flash light is on */
	platform_add_devices(ARRAY_AND_SIZE(g900_devices));

	g900_backlight_register();

#if 0
	if (a780_camera_init() == 0) {
		pxa_set_camera_info(&a780_pxacamera_platform_data);
		platform_device_register(&a780_camera);
	}
#endif
}

MACHINE_START(G900, "Toshiba G900")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= 0xa0000100,
	.map_io		= &pxa_map_io,
	.init_irq	= &pxa27x_init_irq,
	.init_machine	= g900_machine_init,
	.timer		= &pxa_timer,
MACHINE_END
