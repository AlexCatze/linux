/*
 * linux/drivers/pcmcia/pxa/pxa2xx_loox720.c
 *
 * Copyright (C) 2011 Alexander Tarasikov <alexander.tarasikov@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

#include <asm/mach-types.h>

#include <mach/gpio.h>
#include <mach/loox720.h>

#include "soc_common.h"

static struct pcmcia_irqs loox720_cf_irq = {
		.sock = 1,
		.irq = LOOX720_IRQ_CF_DETECT_N,
		.str = "CF Detect"
};

static int loox720_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
switch (skt->nr) {
	case 1:
		skt->socket.pci_irq = LOOX720_IRQ_CF_READY;
		return soc_pcmcia_request_irqs(skt, &loox720_cf_irq, 1);
		break;
	case 0:
		skt->socket.pci_irq = LOOX720_IRQ_WIFI_READY;
		break;
	}
return 0;
}

static void loox720_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	switch (skt->nr) {
	case 1:
			soc_pcmcia_free_irqs(skt, &loox720_cf_irq, 1);
	break;
	}
}

static void loox720_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
					struct pcmcia_state *state)
{
	switch (skt->nr) {
	case 1:
		state->detect = !gpio_get_value(LOOX720_EGPIO_CF_DETECT_N);
		state->ready = !!gpio_get_value(LOOX720_EGPIO_CF_READY);
		state->bvd1 = 1;
		state->bvd2 = 1;
		state->vs_3v = 1,
		state->vs_Xv = 1,
		state->wrprot = 0;
		break;
	case 0:
		state->detect = 1;
		state->ready = gpio_get_value(LOOX720_EGPIO_WIFI_ENABLED) && gpio_get_value(LOOX720_EGPIO_WIFI_READY);
		state->bvd1 = 1;
		state->bvd2 = 1;
		state->vs_3v = 1;
		state->vs_Xv = 0;
		state->wrprot = 0;
		break;
	}
}

static int loox720_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
					   const socket_state_t * state)
{
switch (skt->nr) {
	case 1:
		if (state->flags & SS_RESET) {
			gpio_set_value(LOOX720_EGPIO_CF_RESET, 1);
			msleep(30);
			gpio_set_value(LOOX720_EGPIO_CF_RESET, 0);
		}
		switch (state->Vcc) {
		case 0:
			gpio_direction_output(LOOX720_EGPIO_CF_5V, 0);
			gpio_direction_output(LOOX720_EGPIO_CF_3V3, 0);
			break;
		case 33:
		case 50:
			gpio_direction_output(LOOX720_EGPIO_CF_3V3, 1);
			gpio_direction_output(LOOX720_EGPIO_CF_5V, 1);
			break;
		}
		break;
	case 0:
		if (state->flags & SS_RESET) {
			gpio_set_value(GPIO_LOOX720_WIFI_RESET, 1);
			msleep(30);
			gpio_set_value(GPIO_LOOX720_WIFI_RESET, 0);
		}
		if (state->Vcc) {
			gpio_set_value(LOOX720_EGPIO_WIFI_POWER1, 1);
			gpio_set_value(GPIO_LOOX720_WIFI_POWER0, 1);
		}
		else {
			gpio_set_value(LOOX720_EGPIO_WIFI_POWER1, 0);
			gpio_set_value(GPIO_LOOX720_WIFI_POWER0, 0);
		}
		break;
	}

	return 0;
}

static void loox720_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
} static void loox720_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
} static struct pcmcia_low_level loox720_pcmcia_ops __initdata = {
		.owner = THIS_MODULE,
		.nr = 2,
		.hw_init = loox720_pcmcia_hw_init,
		.hw_shutdown = loox720_pcmcia_hw_shutdown,
		.socket_state = loox720_pcmcia_socket_state,
		.configure_socket = loox720_pcmcia_configure_socket,
		.socket_init = loox720_pcmcia_socket_init,
		.socket_suspend = loox720_pcmcia_socket_suspend,
};

static struct platform_device *loox720_pcmcia_device;
static int __init request_pcmcia_gpios(void)
{
	int gpios[] = {
		GPIO_LOOX720_WIFI_POWER0,
		LOOX720_EGPIO_WIFI_POWER1,
		GPIO_LOOX720_WIFI_RESET,
		LOOX720_EGPIO_WIFI_READY,
		LOOX720_EGPIO_CF_RESET,
		LOOX720_EGPIO_CF_READY,
		LOOX720_EGPIO_CF_5V,
		LOOX720_EGPIO_CF_DETECT_N,
		LOOX720_EGPIO_CF_3V3,
	};
	int ret, i;
	for (i = 0; i < ARRAY_SIZE(gpios); i++) {
		ret = gpio_request(gpios[i], "Loox 720 PCMCIA");
		if (ret)
			goto err;
	}
	return 0;
 err:	for (; i >= 0; i--) {
		gpio_free(gpios[i]);
	}
	return ret;
}

static void __exit free_pcmcia_gpios(void)
{
	int i;
	int gpios[] = {
		GPIO_LOOX720_WIFI_POWER0,
		LOOX720_EGPIO_WIFI_POWER1,
		GPIO_LOOX720_WIFI_RESET,
		LOOX720_EGPIO_WIFI_READY,
		LOOX720_EGPIO_CF_RESET,
		LOOX720_EGPIO_CF_READY,
		LOOX720_EGPIO_CF_5V,
		LOOX720_EGPIO_CF_DETECT_N,
		LOOX720_EGPIO_CF_3V3,
	};
	for (i = 0; i < ARRAY_SIZE(gpios); i++)
		gpio_free(gpios[i]);
}

static int __init loox720_pcmcia_init(void)
{
	int ret;
	if (!machine_is_loox720())
		return -ENODEV;
	
	loox720_pcmcia_device = platform_device_alloc("pxa2xx-pcmcia", -1);
	if (!loox720_pcmcia_device)
		return -ENOMEM;
	
	if (request_pcmcia_gpios()) {
		printk(KERN_ERR "%s: failed to request pcmcia gpios\n",
		       __func__);
		return -ENODEV;
	}
	
	ret = platform_device_add_data(loox720_pcmcia_device, &loox720_pcmcia_ops,
				     sizeof(loox720_pcmcia_ops));
	
	if (!ret) {
		printk(KERN_INFO "Registering Loox 720 PCMCIA interface.\n");
		ret = platform_device_add(loox720_pcmcia_device);
	}
	
	if (ret) {
		platform_device_put(loox720_pcmcia_device);
		printk(KERN_INFO "Loox 720 PCMCIA Failed.\n");
    }

	return ret;
}

static void __exit loox720_pcmcia_exit(void)
{
	platform_device_unregister(loox720_pcmcia_device);
	free_pcmcia_gpios();
}

fs_initcall(loox720_pcmcia_init);
module_exit(loox720_pcmcia_exit);

MODULE_AUTHOR("Alexander Tarasikov <alexander.tarasikov@gmail.com>");
MODULE_DESCRIPTION("Loox 720 PCMCIA Driver");
MODULE_ALIAS("platform:pxa2xx-pcmcia");
MODULE_LICENSE("GPL");
