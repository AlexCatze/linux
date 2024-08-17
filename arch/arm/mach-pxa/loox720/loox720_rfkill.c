/*
 * Bluetooth and GSM Power control for Loox 720
 *
 * Copyright (c) 2010 Alexander Tarasikov
 * based on the BT driver by Tomasz Figa
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/rfkill.h>

#include <mach/loox720.h>

static struct rfkill *bt_rfk;

static void bt_poweroff(void)
{
	gpio_direction_output(LOOX720_EGPIO_BT_RADIO, 0);
	gpio_direction_output(LOOX720_EGPIO_BT_POWER, 0);
}

static void bt_poweron(void)
{
	gpio_direction_output(LOOX720_EGPIO_BT_POWER, 1);
	gpio_direction_output(LOOX720_EGPIO_BT_RADIO, 1);
	gpio_set_value(GPIO_LOOX720_CPU_BT_RESET_N, 0);
	mdelay(1);
	gpio_set_value(GPIO_LOOX720_CPU_BT_RESET_N, 1);
}

static int loox720_bt_set_block(void *data, bool blocked)
{
	if (!blocked) {
		bt_poweron();
	} else {
		bt_poweroff();
	}

	return 0;
}

static const struct rfkill_ops loox720_bt_rfkill_ops = {
	.set_block = loox720_bt_set_block,
};

static void free_gpios(void){
	gpio_free(GPIO_LOOX720_CPU_BT_RESET_N);
	gpio_free(LOOX720_EGPIO_BT_RADIO);
	gpio_free(LOOX720_EGPIO_BT_POWER);
}

static int request_gpios_bt(void){
	int rc = 0;
	rc |= gpio_request(GPIO_LOOX720_CPU_BT_RESET_N, "Bluetooth Reset");
	rc |= gpio_request(LOOX720_EGPIO_BT_RADIO, "Bluetooth Radio ON");
	rc |= gpio_request(LOOX720_EGPIO_BT_POWER, "Bluetooth Power");
	return rc;
}

static int loox720_rfk_probe(struct platform_device *dev)
{
	int rc;

	rc = request_gpios_bt();
	if (rc)
		goto err_gpios;

	bt_rfk = rfkill_alloc("loox720-bt", &dev->dev, RFKILL_TYPE_BLUETOOTH,
			   &loox720_bt_rfkill_ops, NULL);


	if (!bt_rfk)
		goto err_rfkill;

	rfkill_set_led_trigger_name(bt_rfk, "loox720-bt");
	
	rc = rfkill_register(bt_rfk);
	if (rc)
		goto err_rfkill;

	platform_set_drvdata(dev, NULL);
	
	return 0;

err_rfkill:
	rfkill_destroy(bt_rfk);
err_gpios:
	free_gpios();
	return rc;
}

static int __devexit loox720_rfk_remove(struct platform_device *dev)
{
	platform_set_drvdata(dev, NULL);

	if (bt_rfk) {
		rfkill_unregister(bt_rfk);
		rfkill_destroy(bt_rfk);
	}

	bt_rfk = NULL;
	
	free_gpios();

	return 0;
}

static struct platform_driver loox720_rfk_driver = {
	.probe = loox720_rfk_probe,
	.remove = __devexit_p(loox720_rfk_remove),

	.driver = {
		.name = "loox720-rfk",
		.owner = THIS_MODULE,
	},
};

static int __init loox720_rfk_init(void)
{
	return platform_driver_register(&loox720_rfk_driver);
}

static void __exit loox720_rfk_exit(void)
{
	platform_driver_unregister(&loox720_rfk_driver);
}

module_init(loox720_rfk_init);
module_exit(loox720_rfk_exit);

MODULE_AUTHOR("Alexander Tarasikov <alexander.tarasikov@gmail.com>");
MODULE_DESCRIPTION("Driver for the PocketLOOX 720 bluetooth chip");
MODULE_LICENSE("GPL");
