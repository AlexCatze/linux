/*
 * Driver for Asus P525 JoyStick.
 *
 * Copyright 2008-2009 Alexander Tarasikov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


/* TODO: Make asus_joy_isr handler less ugly/more compacts
 */

#include <linux/module.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>

#include <asm/gpio.h>
#include <mach/asusp525.h>

static struct input_dev *p525_joydev;

static int buttons[5] ={
	GPIO_ASUSP525_JOY_CNTR,
	GPIO_ASUSP525_JOY_NW,
	GPIO_ASUSP525_JOY_NE,
	GPIO_ASUSP525_JOY_SE,
	GPIO_ASUSP525_JOY_SW,
};

int keycodes[5]={KEY_ENTER,KEY_UP,KEY_RIGHT,KEY_DOWN,KEY_LEFT,};

static irqreturn_t asus_joy_isr(int irq, void *dev_id)
{
	int state[5],i;
	for (i=0;i<5;i++) state[i]=gpio_get_value(buttons[i]);


	if ( (buttons[1]==irq_to_gpio(irq)) || (buttons[2]==irq_to_gpio(irq)) )
	{
		if ((state[1]||state[2])==0)
		{
			input_event(p525_joydev, EV_KEY, keycodes[1], 1);
			input_sync(p525_joydev);
			return IRQ_HANDLED;
		};
	}

	if ( (buttons[2]==irq_to_gpio(irq)) || (buttons[3]==irq_to_gpio(irq)) )
	{
		if ((state[2]||state[3])==0)
		{
			input_event(p525_joydev, EV_KEY, keycodes[2], 1);
			input_sync(p525_joydev);
			return IRQ_HANDLED;
		};
	}

	if ( (buttons[3]==irq_to_gpio(irq)) || (buttons[4]==irq_to_gpio(irq)) )
	{
		if ((state[3]||state[4])==0)
		{
			input_event(p525_joydev, EV_KEY, keycodes[3], 1);
			input_sync(p525_joydev);
			return IRQ_HANDLED;
		};
	}

	if ( (buttons[1]==irq_to_gpio(irq)) || (buttons[4]==irq_to_gpio(irq)) )
	{
		if ((state[1]||state[4])==0)
		{
			input_event(p525_joydev, EV_KEY, keycodes[4], 1);
			input_sync(p525_joydev);
			return IRQ_HANDLED;
		};
	}

	if ((state[1]&&state[2]&&state[3]&&state[4])!=0)
		for (i=1;i<5;i++)
			{
			input_event(p525_joydev, EV_KEY, keycodes[i], 0);
			input_sync(p525_joydev);
			}

	if (buttons[0]==irq_to_gpio(irq))
	{
		if ((state[1]&&state[2]&&state[3]&&state[4])!=0)
		{
			if (state[0]==0){
			input_event(p525_joydev, EV_KEY, keycodes[0], 1);
			input_event(p525_joydev, EV_KEY, keycodes[0], 0);
			input_sync(p525_joydev);
			}

		};
	}
	
	return IRQ_HANDLED;
}

static int __devinit asus_joy_probe(struct platform_device *pdev)
{
	int i,err;
	int wakeup = 0;

	struct input_dev *input;
	input = input_allocate_device();

	input->name = pdev->name;
	input->phys = "asusp525-joy/input0";
	input->dev.parent = &pdev->dev;

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	p525_joydev = input;

	for (i=0;i<5;i++)
	err = request_irq(gpio_to_irq(buttons[i]), asus_joy_isr, IRQF_SAMPLE_RANDOM | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "Asus P525 KeyPad", pdev);
	if (err) goto err0;

	for (i=0;i<5;i++)
	input_set_capability(input, EV_KEY, keycodes[i]);

	err = input_register_device(input);
	if (err) goto err1;
	device_init_wakeup(&pdev->dev, wakeup);

	return 0;

	err0: {
		while (i>=0) 
			{
			free_irq (gpio_to_irq(buttons[i]),pdev);
			i--;		
		}
		input_unregister_device(input);
		};
	err1: input_free_device(input);
	return err;
}

static int __devexit asus_joy_remove(struct platform_device *pdev)
{
	struct input_dev *input = p525_joydev;
	device_init_wakeup(&pdev->dev, 0);
	input_unregister_device(input);

	return 0;
}

static struct platform_driver asus_joy_device_driver = {
	.probe		= asus_joy_probe,
	.remove		= asus_joy_remove,
	.driver		= {
		.name	= "asusp525-joy",
		.owner	= THIS_MODULE,
	}
};

static int __init asus_joy_init(void)
{
	return platform_driver_register(&asus_joy_device_driver);
}

static void __exit asus_joy_exit(void)
{
	platform_driver_unregister(&asus_joy_device_driver);
}

module_init(asus_joy_init);
module_exit(asus_joy_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Tarasikov <alex_dfr@mail.ru>");
MODULE_DESCRIPTION("JoyStick driver Asus P525");
MODULE_ALIAS("platform:asusp525-joy");
