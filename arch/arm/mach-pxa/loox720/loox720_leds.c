/*
 * LEDs driver for GPIOs
 *
 * Copyright (C) 2010 Alexander Tarasikov <alexander.tarasikov@gmail.com>
 * Copyright (C) 2008 Tomasz Figa
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include <asm/gpio.h>
#include <mach/loox720.h>

enum led_state {
	ON	= 1 << 0,
	INVERTED= 1 << 1,
	BLINK	= 1 << 2,
};

enum loox_led {
	LEFT_BLUE,
	LEFT_GREEN,
	RIGHT_GREEN,
	RIGHT_ORANGE,
	RIGHT_RED
};

static char old_left_state = 3;
static char old_right_state = 0;

struct loox_led_data {
	struct led_classdev cdev;
	enum led_state state;
};

static struct loox_led_data leds[] = {
	[LEFT_GREEN] = {
		{
			.name = "loox:left:green",
			.default_trigger = "none"
		},
		0,
	},
	[LEFT_BLUE] = {
		{
			.name = "loox:left:blue",
			.default_trigger = "rfkill0"
		},
		0,
	},
	[RIGHT_GREEN] = {
		{
			.name = "loox:right:green",
			.default_trigger = "none"
		},
		0,
	},
	[RIGHT_ORANGE] = {
		{
			.name = "loox:right:orange",
			.default_trigger = "none"
		},
		0,
	},
	[RIGHT_RED] = {
		{
			.name = "loox:right:red",
			.default_trigger = "none"
		},
		0,
	},
};

static struct work_struct loox_leds_work;


/*
	Loox 720 leds are driven by CPLD. For each LED, there are 4+1 bits on CPLD.
	
	Bit combination table for LED 1 (left):
	| A | B | C | D | X | LED status
	| 1 | 1 | 1 | 1 | 1 | Inverted blinking green
	| 0 | 1 | 1 | 1 | 1 | Inverted blinking blue
	| 0 | 0 | 0 | 1 | 1 | Blinking green and blue
	| 1 | 0 | 0 | 1 | 1 | Blinking green
	| 0 | 1 | 0 | 1 | 1 | Blinking blue
	| 1 | 1 | 1 | 1 | 0 | Solid green
	| 0 | 1 | 1 | 1 | 0 | Solid blue


	Bit combination table for LED 2 (right):
	| A | B | C | D | X | LED status
	| 1 | 1 | 0 | 0 | 1 | Blinking green and orange
	| 1 | 1 | 1 | 0 | 1 | Blinking orange
	| 0 | 1 | 0 | 0 | 1 | Blinking green
	| 1 | 0 | 0 | 0 | 1 | Inverted blinking orange
	| 0 | 0 | 1 | 0 | 1 | Inverted blinking green
	| 1 | 0 | 1 | 0 | 1 | Inverted blinking red (?! #2)
	| 0 | 0 | 1 | 0 | 0 | Solid green
	| 1 | 0 | 0 | 0 | 0 | Solid orange
	| 1 | 0 | 1 | 0 | 0 | Solid red (?!)
*/

static void loox_bitbang_led2(char byte) {
	gpio_direction_output(LOOX720_EGPIO_LED2_BIT0, byte & 1);
	gpio_direction_output(LOOX720_EGPIO_LED2_BIT1, (byte >> 1) & 1);
	gpio_direction_output(LOOX720_EGPIO_LED2_BIT2, (byte >> 2) & 1);
	gpio_direction_output(LOOX720_EGPIO_LED2_BIT3, (byte >> 3) & 1);
	gpio_direction_output(LOOX720_EGPIO_LED2_BIT4, (byte >> 4) & 1);
}

static void loox_bitbang_led1(char byte) {
	gpio_direction_output(LOOX720_EGPIO_LED1_BIT0, byte & 1);
	gpio_direction_output(LOOX720_EGPIO_LED1_BIT1, (byte >> 1) & 1);
	gpio_direction_output(LOOX720_EGPIO_LED1_BIT2, (byte >> 2) & 1);
	gpio_direction_output(LOOX720_EGPIO_LED1_BIT3, (byte >> 3) & 1);
	gpio_direction_output(LOOX720_EGPIO_LED1_BIT4, (byte >> 4) & 1);
}

static void loox_leds_update(struct work_struct *work)
{
	int i;
	char left_state = 0, right_state = 0;

	i = leds[LEFT_BLUE].state | leds[LEFT_GREEN].state;
	
	if (i)
		left_state |= (1 << 3);
	else
		left_state &= ~(1 << 3);

		left_state |= (1 << 2);

	if (i & BLINK) {
		left_state |= (1 << 4);
	//	left_state &= ~(1 << 2);
	}
	else {
		left_state &= ~(1 << 4);
	//	left_state |= (1 << 2);
	}

	if (leds[LEFT_GREEN].state)
		left_state |= 1;
	else
		left_state &= ~1;

	if (leds[LEFT_BLUE].state)
		left_state |= (1 << 1);
	else
		left_state &= ~(1 << 1);

	i = leds[RIGHT_RED].state | leds[RIGHT_GREEN].state | leds[RIGHT_ORANGE].state;

	if (i)
		right_state &= ~(1 << 3);
	else
		right_state |= (1 << 3);

	if (i & ON)
		right_state &= ~(1 << 4);
	else
		right_state |= (1 << 4);

	if (i & BLINK) {
		if (leds[RIGHT_GREEN].state)
			right_state &= ~(1 << 2);
		else
			right_state |= (1 << 2);

		if (leds[RIGHT_ORANGE].state)
			right_state |= 1;
		else
			right_state &= ~1;

		if (leds[RIGHT_RED].state)
			right_state &= ~(1 << 1);
		else
			right_state |= (1 << 1);
	}
	else {
		if (leds[RIGHT_GREEN].state)
			right_state |= (1 << 2);
		else
			right_state &= ~(1 << 2);

		if (leds[RIGHT_ORANGE].state)
			right_state |= 1;
		else
			right_state &= ~1;

		if (leds[RIGHT_RED].state)
			right_state |= (1 << 1);
		else
			right_state &= ~(1 << 1);
	}
	
	loox_bitbang_led2(right_state);
	loox_bitbang_led1(left_state);

	old_left_state = left_state;
	old_right_state = right_state;
}

static void loox_led_set(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	struct loox_led_data *led_dat =
		container_of(led_cdev, struct loox_led_data, cdev);

	if (value == LED_OFF) {
		led_dat->state &= ~ON;
	}
	else {
		led_dat->state |= ON;
	}
	printk("%s: led[%s].state == %d\n", __func__, led_dat->cdev.name, led_dat->state);
	schedule_work(&loox_leds_work);
}

static int loox_blink_set(struct led_classdev *led_cdev,
	unsigned long *delay_on, unsigned long *delay_off)
{
	struct loox_led_data *led_dat =
		container_of(led_cdev, struct loox_led_data, cdev);

	if (*delay_off) {
		led_dat->state |= BLINK;
	}
	else {
		led_dat->state &= ~BLINK;
	}
	printk("%s: led[%s].state == %d\n", __func__, led_dat->cdev.name, led_dat->state);
	schedule_work(&loox_leds_work);
	return 0;
}

static int __devinit create_loox_leds(struct device *parent)
{
	int ret = 0, i;

	for (i = 0; i < ARRAY_SIZE(leds); i++) {
		leds[i].cdev.blink_set = loox_blink_set;
		leds[i].cdev.brightness_set = loox_led_set;
		leds[i].cdev.brightness = LED_OFF;

		ret = led_classdev_register(parent, &leds[i].cdev);
		if (ret < 0)
			break;
	}
	if (i < ARRAY_SIZE(leds)) {
		while (--i > 0) {
			led_classdev_unregister(&leds[i].cdev);
		}
	}
	INIT_WORK(&loox_leds_work, loox_leds_update);

	return ret;
}

static void __devexit delete_loox_leds(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(leds); i++) {
		led_classdev_unregister(&leds[i].cdev);
	}
	cancel_work_sync(&loox_leds_work);
}

static int __devinit request_gpios(void) {
	int i, ret;
	int gpios[] = {
		LOOX720_EGPIO_LED2_BIT0,
		LOOX720_EGPIO_LED2_BIT1,
		LOOX720_EGPIO_LED2_BIT2,
		LOOX720_EGPIO_LED2_BIT3,
		LOOX720_EGPIO_LED2_BIT4,
		LOOX720_EGPIO_LED1_BIT0,
		LOOX720_EGPIO_LED1_BIT1,
		LOOX720_EGPIO_LED1_BIT2,
		LOOX720_EGPIO_LED1_BIT3,
		LOOX720_EGPIO_LED1_BIT4,
	};
	for (i = 0; i < ARRAY_SIZE(gpios); i++) {
		ret = gpio_request(gpios[i], "Loox 720 LEDs");
		if (ret) break;
	}
	if (i != ARRAY_SIZE(gpios)) {
		while (--i > 0) {
			gpio_free(gpios[i]);
		}
	}

	return ret;
}

static void __devexit free_gpios(void) {
	int i;
	int gpios[] = {
		LOOX720_EGPIO_LED2_BIT0,
		LOOX720_EGPIO_LED2_BIT1,
		LOOX720_EGPIO_LED2_BIT2,
		LOOX720_EGPIO_LED2_BIT3,
		LOOX720_EGPIO_LED2_BIT4,
		LOOX720_EGPIO_LED1_BIT0,
		LOOX720_EGPIO_LED1_BIT1,
		LOOX720_EGPIO_LED1_BIT2,
		LOOX720_EGPIO_LED1_BIT3,
		LOOX720_EGPIO_LED1_BIT4,
	};
	for (i = 0; i < ARRAY_SIZE(gpios); i++) {
		gpio_free(gpios[i]);
	}
}

static int __devinit loox_led_probe(struct platform_device *pdev)
{
	int ret = 0;

	ret = request_gpios();
	if (ret)
		goto err_led;

	ret = create_loox_leds(&pdev->dev);
	if (ret)
		goto err_led;

	return 0;

err_led:
	return ret;
}

static int __devexit loox_led_remove(struct platform_device *pdev)
{
	delete_loox_leds();
	free_gpios();
	return 0;
}

static struct platform_driver loox_led_driver = {
	.probe		= loox_led_probe,
	.remove		= loox_led_remove,
	.driver		= {
		.name	= "loox720-leds",
		.owner	= THIS_MODULE,
	}
};

MODULE_ALIAS("platform:leds-loox720");

static int __init loox_led_init(void)
{
	return platform_driver_register(&loox_led_driver);
}

static void __exit loox_led_exit(void)
{
	platform_driver_unregister(&loox_led_driver);
}

module_init(loox_led_init);
module_exit(loox_led_exit);

MODULE_AUTHOR("Alexander Tarasikov <alexander.tarasikov@gmail.com>");
MODULE_DESCRIPTION("GPIO LED driver");
MODULE_LICENSE("GPL");
