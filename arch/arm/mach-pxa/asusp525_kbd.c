/*
 * Driver for Asus P525 I2C KeyPad.
 *
 * Copyright 2008-2009 Alexander Tarasikov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
#include <linux/workqueue.h>

#include <asm/gpio.h>
#include <mach/asusp525.h>

#define ASUSKBD_NAME "asuskbd"

struct key{
	int keycode;
	int egpio;
};

static DEFINE_MUTEX(asuskbd_mutex);

static void asuskbd_update(struct work_struct *delayed_work);
static DECLARE_DELAYED_WORK(asuskbd_work, asuskbd_update);
static struct workqueue_struct *asuskbd_workqueue;


static int oldstate=0xf807,state;
static struct key buttons[] = {
	{KEY_PLAY, EGPIO1_ASUSP525_KEY_HP},
	{KEY_RECORD, EGPIO1_ASUSP525_KEY_RECORD},
	{KEY_CAMERA, EGPIO1_ASUSP525_KEY_CAMERA},
	{KEY_4, EGPIO1_ASUSP525_KEY_FOCUS},
	{KEY_5, EGPIO1_ASUSP525_HP_JACK},
	{KEY_6, EGPIO1_ASUSP525_SD_DETECT},
	{KEY_7, EGPIO1_ASUSP525_SW_HOLD},
	{KEY_ESC, EGPIO1_ASUSP525_KEY_END},
	{KEY_PHONE, EGPIO1_ASUSP525_KEY_SEND},
	{KEY_BACKSPACE, EGPIO1_ASUSP525_KEY_CLEAR},
	{KEY_B, EGPIO1_ASUSP525_KEY_WIN},
};

static struct input_dev *input;

static void asuskbd_update(struct work_struct *work){
	int i,shift;
	state = 0;

	for (i=0; i<ARRAY_SIZE(buttons) ;i++){
		gpio_request(EGPIO_BANK1(buttons[i].egpio), "Asus kbd");
		gpio_direction_input(EGPIO_BANK1(buttons[i].egpio));
		state |= ( gpio_get_value(EGPIO_BANK1(buttons[i].egpio)) << buttons[i].egpio );
		gpio_free(EGPIO_BANK1(buttons[i].egpio));
	}

	oldstate = oldstate ^ state;

	for (i=0;i<ARRAY_SIZE(buttons);i++){
	shift = (buttons[i].egpio);
	if (1 & (oldstate>>shift))
		switch(shift){
		case 3 ... 7:
			input_event(input, EV_KEY, buttons[i].keycode, (1 & (state>>shift)) );
			break;
		case 12 ... 15:
			input_event(input, EV_KEY, buttons[i].keycode, 1^(1 & (state>>shift)) );
			break;
		case 8:
			printk("ASUS: Headphones are %s\n", (1 & (state>>shift))?"Plugged":"Unplugged");
			break;
		case 9:
			printk("ASUS: SD-MMC is %s\n", (1 & (state>>shift))?"Unplugged":"Plugged");
			break;
		case 11:
			printk("ASUS: HOLD is %s\n", (1 & (state>>shift))?"OFF":"ON");
			break;

		}
	}

	input_sync(input);

	oldstate = state;
	mutex_unlock(&asuskbd_mutex);
}

static irqreturn_t asus_kbd_isr(int irq, void *dev_id){
	if (mutex_trylock(&asuskbd_mutex)) queue_delayed_work(asuskbd_workqueue, &asuskbd_work, 1);
	return IRQ_HANDLED;
}

static int __devinit asus_kbd_probe(struct platform_device *pdev){
	int i,err;

	input = input_allocate_device();

	input->name = pdev->name;
	input->phys = "asusp525-kbd/input0";
	input->dev.parent = &pdev->dev;

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	err = request_irq(gpio_to_irq(GPIO_ASUSP525_IOEXP_IRQ), asus_kbd_isr, IRQF_TRIGGER_FALLING, "Asus P525 KeyPad", pdev);
	if (err) goto err0;

	for (i=0;i<ARRAY_SIZE(buttons);i++){
	input_set_capability(input, EV_KEY, buttons[i].keycode);
	}

	asuskbd_workqueue = create_singlethread_workqueue(ASUSKBD_NAME);
	mutex_unlock(&asuskbd_mutex);

	err = input_register_device(input);
	if (err)
		goto err1;
	device_init_wakeup(&pdev->dev, 0);

	return 0;

err1:
	input_unregister_device(input);

err0:
	free_irq(gpio_to_irq(GPIO_ASUSP525_IOEXP_IRQ),pdev);
	
	return err;
}

static int __devexit asus_kbd_remove(struct platform_device *pdev){
	int i;

	mutex_lock(&asuskbd_mutex);
	cancel_delayed_work(&asuskbd_work);
	flush_workqueue(asuskbd_workqueue);
	destroy_workqueue(asuskbd_workqueue);

	free_irq(gpio_to_irq(GPIO_ASUSP525_IOEXP_IRQ),pdev);

	device_init_wakeup(&pdev->dev, 0);
	input_unregister_device(input);

	return 0;
}

static struct platform_driver asus_kbd_device_driver = {
	.probe		= asus_kbd_probe,
	.remove		= asus_kbd_remove,
	.driver		= {
		.name	= "asusp525-kbd",
		.owner	= THIS_MODULE,
	}
};

static int __init asus_kbd_init(void){
	return platform_driver_register(&asus_kbd_device_driver);
}

static void __exit asus_kbd_exit(void){
	platform_driver_unregister(&asus_kbd_device_driver);
}

module_init(asus_kbd_init);
module_exit(asus_kbd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Tarasikov <alex_dfr@mail.ru>");
MODULE_DESCRIPTION("I2C KeyPad driver for Asus P525");
MODULE_ALIAS("platform:asusp525-kbd");
