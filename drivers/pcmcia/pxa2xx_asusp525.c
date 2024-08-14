/*
 * linux/drivers/pcmcia/pxa/pxa2xx_asusp525.c
 *
 * Copyright (C) 2008 Alexander Tarasikov <alex_dfr@mail.ru>
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
#include <mach/asusp525.h>

#include "soc_common.h"

static int asusp525_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	int ret;
	ret = gpio_request(GPIO_ASUSP525_PCMCIA_POWER, "PCMCIA Power");
	if (ret)
	return ret;
	gpio_direction_output(GPIO_ASUSP525_PCMCIA_POWER, 0);

	ret = gpio_request(GPIO_ASUSP525_PCMCIA_RESET, "PCMCIA Reset");
	if (ret)
	return ret;
	gpio_direction_output(GPIO_ASUSP525_PCMCIA_RESET, 1);

	ret = gpio_request(GPIO_ASUSP525_PCMCIA_READY, "PCMCIA Ready");
	if (ret)
	return ret;
	gpio_direction_input(GPIO_ASUSP525_PCMCIA_READY);

	skt->socket.pci_irq = GPIO_ASUSP525_PCMCIA_READY;
	gpio_free(GPIO_ASUSP525_PCMCIA_RESET);
	return ret;
}

static void asusp525_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	gpio_free(GPIO_ASUSP525_PCMCIA_READY);
	gpio_direction_output(GPIO_ASUSP525_PCMCIA_RESET, 1);
	gpio_free(GPIO_ASUSP525_PCMCIA_RESET);
	gpio_direction_output(GPIO_ASUSP525_PCMCIA_POWER, 0);
	gpio_free(GPIO_ASUSP525_PCMCIA_POWER);
}

static void asusp525_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
				       struct pcmcia_state *state)
{
	state->detect = 1;
	state->ready  = (gpio_get_value(GPIO_ASUSP525_PCMCIA_READY) == 0) ? 0 : 1;
	state->bvd1   = 1;
	state->bvd2   = 1;
	state->vs_3v  = 1;
	state->vs_Xv  = 0;
	state->wrprot = 0;
}

static int asusp525_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
					  const socket_state_t *state)
{
	gpio_set_value(GPIO_ASUSP525_PCMCIA_POWER, 1);
	if (state->flags & SS_RESET)
		{
		gpio_set_value(GPIO_ASUSP525_PCMCIA_RESET, 1);
		udelay(10);
		gpio_set_value(GPIO_ASUSP525_PCMCIA_RESET, 0);
		};
	if (state->flags & SS_POWERON)
	{
	printk(KERN_INFO "Asus Pcmcia ON.\n");
	};
	if (!state->flags & SS_POWERON)
	{
	printk(KERN_INFO "Asus Pcmcia OFF.\n");
	};
	return 0;
}

static void asusp525_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
}

static void asusp525_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
}


static struct pcmcia_low_level asusp525_pcmcia_ops __initdata = {
	.owner			= THIS_MODULE,

	.nr			= 1,
	.hw_init		= asusp525_pcmcia_hw_init,

	.hw_shutdown		= asusp525_pcmcia_hw_shutdown,
	.socket_state		= asusp525_pcmcia_socket_state,
	.configure_socket	= asusp525_pcmcia_configure_socket,

	.socket_init		= asusp525_pcmcia_socket_init,
	.socket_suspend		= asusp525_pcmcia_socket_suspend,
};

static struct platform_device *asusp525_pcmcia_device;

static int __init asusp525_pcmcia_init(void)
{
	int ret;

	if (!machine_is_asusp525())
		return -ENODEV;

	asusp525_pcmcia_device = platform_device_alloc("pxa2xx-pcmcia", -1);

	if (!asusp525_pcmcia_device)
		return -ENOMEM;

	ret = platform_device_add_data(asusp525_pcmcia_device, &asusp525_pcmcia_ops,
				       sizeof(asusp525_pcmcia_ops));

	if (!ret) {
		printk(KERN_INFO "Registering Asus P525 PCMCIA interface.\n");
		ret = platform_device_add(asusp525_pcmcia_device);
	}

	if (ret)
		platform_device_put(asusp525_pcmcia_device);
		printk(KERN_INFO "Asus P525 PCMCIA Failed.\n");

	return ret;
}

static void __exit asusp525_pcmcia_exit(void)
{
	platform_device_unregister(asusp525_pcmcia_device);
}

fs_initcall(asusp525_pcmcia_init);
module_exit(asusp525_pcmcia_exit);

MODULE_AUTHOR("Alexander Tarasikov <alex_dfr@mail.ru>");
MODULE_DESCRIPTION("Asus P525 PCMCIA Driver");
MODULE_ALIAS("platform:pxa2xx-pcmcia");
MODULE_LICENSE("GPL");
