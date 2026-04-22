/*
 * Bluetooth and GSM Power control for Asus P525
 *
 * Copyright (c) 2009 Alexander Tarasikov
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

#include <mach/asusp525.h>

static struct rfkill *gsm_rfk, *bt_rfk;
static int gsm_mode = 0;

static void bt_poweroff(void)
{
	gpio_direction_output(GPIO_ASUSP525_BT_PWR1,0);
	gpio_direction_output(GPIO_ASUSP525_BT_PWR2,0);
	gpio_direction_output(GPIO_ASUSP525_BT_UNKNOWN,0);
	gpio_direction_output(GPIO_ASUSP525_BT_SUSPEND,0);
}

static void bt_poweron(void)
{
	gpio_direction_output(GPIO_ASUSP525_BT_PWR1,1);
	msleep(5);
	gpio_direction_output(GPIO_ASUSP525_BT_PWR2,1);
	msleep(500);
	gpio_direction_input(GPIO_ASUSP525_BT_UNKNOWN);
	gpio_direction_output(GPIO_ASUSP525_BT_SUSPEND,0);
}

static int asusp525_bt_set_block(void *data, bool blocked)
{
	if (!blocked) {
		bt_poweron();
	} else {
		bt_poweroff();
	}

	return 0;
}

static void gsm_poweroff(void)
{
	gpio_direction_output(GPIO_ASUSP525_GSM_POWER, 0);
		msleep(500);
}

static void gsm_on_warm(void)
{
	gpio_direction_output(GPIO_ASUSP525_GSM_HWRESET, 1);
		msleep(500);
	gpio_direction_output(GPIO_ASUSP525_GSM_RESET, 1);
	gpio_direction_output(GPIO_ASUSP525_GSM_POWER, 1);
		msleep(500);
}

static void gsm_on_cold(void)
{
	gpio_direction_output(GPIO_ASUSP525_GSM_POWER, 0);
		msleep(500);
	gpio_direction_output(GPIO_ASUSP525_GSM_HWRESET, 0);
		msleep(2000);
	gpio_direction_output(GPIO_ASUSP525_GSM_HWRESET, 1);
		msleep(500);
	gpio_direction_output(GPIO_ASUSP525_GSM_RESET, 1);
	gpio_direction_output(GPIO_ASUSP525_GSM_POWER, 1);
		msleep(1000);
}

static int asusp525_gsm_set_block(void *data, bool blocked)
{
	if (!blocked) {
		(gsm_mode)?gsm_on_cold:gsm_on_warm();
	} else {
		gsm_poweroff();
	}

	return 0;
}
static const struct rfkill_ops asusp525_bt_rfkill_ops = {
	.set_block = asusp525_bt_set_block,
};

static const struct rfkill_ops asusp525_gsm_rfkill_ops = {
	.set_block = asusp525_gsm_set_block,
};

static void free_gpios(void){
	gpio_free(GPIO_ASUSP525_BT_PWR1);
	gpio_free(GPIO_ASUSP525_BT_SUSPEND);
	gpio_free(GPIO_ASUSP525_BT_PWR2);
	gpio_free(GPIO_ASUSP525_BT_UNKNOWN);
	gpio_free(GPIO_ASUSP525_GSM_RESET);
	gpio_free(GPIO_ASUSP525_GSM_HWRESET);
	gpio_free(GPIO_ASUSP525_GSM_POWER);
}

static int request_gpios_bt(void){
	int rc = 0;
	rc |= gpio_request(GPIO_ASUSP525_BT_PWR1, "Bluetooth Power");
	rc |= gpio_request(GPIO_ASUSP525_BT_PWR2, "Bluetooth Power 2");
	rc |= gpio_request(GPIO_ASUSP525_BT_SUSPEND, "Bluetooth Host Suspend");
	rc |= gpio_request(GPIO_ASUSP525_BT_UNKNOWN, "Bluetooth Unknown Pin");
	return rc;
}

static int request_gpios_gsm(void){
	int rc = 0;
	rc |= gpio_request(GPIO_ASUSP525_GSM_POWER, "GSM Power");
	rc |= gpio_request(GPIO_ASUSP525_GSM_HWRESET, "GSM Reset");
	rc |= gpio_request(GPIO_ASUSP525_GSM_RESET, "GSM HW Reset");
	return rc;
}

/* Sysfs interface. Reading will return modem status
 * (i.e. whether the hardware is ready or not)
 * Writing will set the reset type (cold or warm) */
static ssize_t gsm_read(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	int state=!(gpio_get_value(25) || gpio_get_value(91));
	if (state)
	{
	return strlcpy(buf, "1\n", 3);
	}
	return strlcpy(buf, "0\n", 3);
}

static ssize_t gsm_write(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	unsigned long action = simple_strtoul(buf, NULL, 10);
	if (0==strcmp(attr->attr.name, "gsm_mode"))
	{
		gsm_mode = (action)?action:0;
	}

	return count;
}

static DEVICE_ATTR(gsm_mode, 0644, gsm_read, gsm_write);

static struct attribute *asusp525_gsm_sysfs_entries[] = {
	&dev_attr_mode.attr,
	NULL
};

static struct attribute_group asusp525_gsm_attr_group = {
	.name	= NULL,
	.attrs	= asusp525_gsm_sysfs_entries,
};

static int asusp525_rfk_probe(struct platform_device *dev)
{
	int rc;

	rc = request_gpios_bt();
	if (rc)
		goto err_gpios;

	rc = request_gpios_gsm();
	if (rc)
		goto err_gpios;

	bt_rfk = rfkill_alloc("asusp525-bt", &dev->dev, RFKILL_TYPE_BLUETOOTH,
			   &asusp525_bt_rfkill_ops, NULL);

	gsm_rfk = rfkill_alloc("asusp525-gsm", &dev->dev, RFKILL_TYPE_UWB,
			   &asusp525_gsm_rfkill_ops, NULL);

	if (!bt_rfk || !gsm_rfk)
		goto err_rfkill;

	rfkill_set_led_trigger_name(bt_rfk, "asusp525-bt");
	
	rc = rfkill_register(bt_rfk);
	rc |= rfkill_register(gsm_rfk);
	if (rc)
		goto err_rfkill;

	platform_set_drvdata(dev, NULL);
	
	rc = sysfs_create_group(&dev->dev.kobj, &asusp525_gsm_attr_group);
	return 0;

err_rfkill:
	rfkill_destroy(bt_rfk);
	rfkill_destroy(gsm_rfk);
err_gpios:
	free_gpios();
	return rc;
}

static int __devexit asusp525_rfk_remove(struct platform_device *dev)
{
	platform_set_drvdata(dev, NULL);

	sysfs_remove_group(&dev->dev.kobj, &asusp525_gsm_attr_group);

	if (gsm_rfk) {
		rfkill_unregister(gsm_rfk);
		rfkill_destroy(gsm_rfk);
	}

	if (bt_rfk) {
		rfkill_unregister(bt_rfk);
		rfkill_destroy(bt_rfk);
	}

	gsm_rfk = NULL;
	bt_rfk = NULL;
	
	free_gpios();

	return 0;
}

static struct platform_driver asusp525_rfk_driver = {
	.probe = asusp525_rfk_probe,
	.remove = __devexit_p(asusp525_rfk_remove),

	.driver = {
		.name = "asusp525-rfk",
		.owner = THIS_MODULE,
	},
};

static int __init asusp525_rfk_init(void)
{
	return platform_driver_register(&asusp525_rfk_driver);
}

static void __exit asusp525_rfk_exit(void)
{
	platform_driver_unregister(&asusp525_rfk_driver);
}

module_init(asusp525_rfk_init);
module_exit(asusp525_rfk_exit);
