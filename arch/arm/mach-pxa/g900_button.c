/*
 * Buttons support for Toshiba G900. GPIO buttons
 * based on:
 * Xiao Huang g900_button.c
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/gpio_keys.h>

#include <asm/mach-types.h>

#include <asm/arch/hardware.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/g900-gpio.h>

#define GET_GPIO(gpio) (GPLR(gpio) & GPIO_bit(gpio))

#ifdef G900_I2C_GPIO_BUTTON

static DECLARE_MUTEX(i2c_buttons_mutex);

static struct workqueue_struct *i2c_buttons_workqueue;
static struct work_struct i2c_buttons_irq_task;
static u32 last_i2c_data = 0;

extern u32 pca9535_read_input(void);
#endif

static struct gpio_keys_button gpio_buttons[] = {
	{KEY_POWER,			GPIO_NR_G900_BUTTON_POWER,			0,	"Power button"},
};

static struct input_dev *input_dev = NULL;

static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
	int i, state;
	int gpiovalue;

	printk("*****IRQ %d COME, gpio100=%d, gpio101=%d, gpio102=%d, gpio103=%d, gpio104=%d, gpio105=%d, gpio106=%d, gpio107=%d, gpio108=%d,****\n",irq, GET_GPIO(100), GET_GPIO(101), GET_GPIO(102), GET_GPIO(103), GET_GPIO(104), GET_GPIO(105), GET_GPIO(106), GET_GPIO(107), GET_GPIO(108) );

	for (i = 0; i < ARRAY_SIZE(gpio_buttons); i++)
	{
		if (IRQ_GPIO(gpio_buttons[i].gpio) == irq)
		{
			gpiovalue = GET_GPIO(gpio_buttons[i].gpio);
			printk("gpio=%d, active_low=%d\n", gpiovalue, gpio_buttons[i].active_low );

			state = (gpiovalue ? 1 : 0) ^ (gpio_buttons[i].active_low);
			/*state = (gpio_buttons[i].active_low ? !state : state);*/
			input_report_key(input_dev, gpio_buttons[i].code, state);
			input_sync(input_dev);
			printk("code=%d, state=%d\n", gpio_buttons[i].code, state);
		}
	}
	return IRQ_HANDLED;
}

#ifdef G900_I2C_GPIO_BUTTON

//gpio field here is a bitmask
static struct gpio_keys_button i2c_buttons[] = {
    {KEY_UP,		0x1,		1, "UP"},
    {KEY_RIGHT,		0x2,		1, "RIGHT"},
    {KEY_DOWN,	0x4,		1, "DOWN"},
    {KEY_LEFT,		0x8,		1, "LEFT"},
    {KEY_ENTER,		0x10,	1, "ENTER"},
};

static void i2c_buttons_handler(struct work_struct *unused)
{
	int i, state;
	u32 i2c_data, bits;
	
	printk("****I2C interrupt****\n");

	i2c_data = pca9535_read_input();

	if (i2c_data == (u32)-1) goto out;

	i2c_data = (~(i2c_data >> 8) & 0x1f);
	bits = last_i2c_data ^ i2c_data;

	if (!bits) goto out;

	for (i = 0; i < ARRAY_SIZE(i2c_buttons); i++)
	{
		if (bits & i2c_buttons[i].gpio)
		{
			state = ((bits & i2c_data) != 0);
			input_report_key(input_dev, i2c_buttons[i].code, state);
			input_sync(input_dev);
		}
	}
	last_i2c_data = i2c_data;
out:
	up(&i2c_buttons_mutex);
}

static irqreturn_t i2c_irq_handler(int irq, void *dev_id)
{
	if (!down_trylock(&i2c_buttons_mutex)) queue_work(i2c_buttons_workqueue, &i2c_buttons_irq_task);
	return IRQ_HANDLED;
}

#endif

static int buttons_probe(struct platform_device *pdev)
{
	int i, err;
	int irqflag = IRQF_SAMPLE_RANDOM;
#ifdef CONFIG_PREEMPT_RT
	irqflag |= IRQF_NODELAY;
#endif
	
	if (!(input_dev = input_allocate_device())) return -ENOMEM;
	
	input_dev->name = "Toshiba G900 buttons";
	set_bit(EV_KEY, input_dev->evbit);
	
	for (i = 0; i < ARRAY_SIZE(gpio_buttons); i++)
	{
		set_bit(gpio_buttons[i].code, input_dev->keybit);
	}
#ifdef G900_I2C_GPIO_BUTTON
	for (i = 0; i < ARRAY_SIZE(i2c_buttons); i++)
	{
		set_bit(i2c_buttons[i].code, input_dev->keybit);
	}
#endif	
	input_register_device(input_dev);

#ifdef G900_I2C_GPIO_BUTTON
	i2c_buttons_workqueue = create_singlethread_workqueue("buttond");
	INIT_WORK(&i2c_buttons_irq_task, i2c_buttons_handler);

	set_irq_type(G900_IRQ(PCA9535_IRQ), IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_TRIGGER_LOW | IRQF_TRIGGER_HIGH);
	err = request_irq(G900_IRQ(PCA9535_IRQ), i2c_irq_handler, irqflag, "g900-i2cbuttons", NULL);
	if (err)
	{
		printk(KERN_ERR "%s: Cannot assign i2c IRQ\n", __FUNCTION__);
		return err;
	}
	
	up(&i2c_buttons_mutex);
#endif	
	for (i = 0; i < ARRAY_SIZE(gpio_buttons); i++)
	{
		//assign irq and keybit
		set_irq_type(IRQ_GPIO(gpio_buttons[i].gpio), IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING);
		err = request_irq(IRQ_GPIO(gpio_buttons[i].gpio), gpio_irq_handler, irqflag, "g900-gpiobuttons", NULL);
		if (err)
		{
			printk(KERN_ERR "%s: Cannot assign GPIO(%d) IRQ\n", __FUNCTION__, gpio_buttons[i].gpio);
			return err;
		}
		printk("*****IRQ %d OK****", IRQ_GPIO(gpio_buttons[i].gpio));
	}
	
	return 0;
}

static int buttons_remove(struct platform_device *pdev)
{
	int i;

#ifdef G900_I2C_GPIO_BUTTON
	down(&i2c_buttons_mutex);
	free_irq(G900_IRQ(PCA9535_IRQ), NULL);
#endif	
	for (i = 0; i < ARRAY_SIZE(gpio_buttons); i++)
	{
		free_irq(IRQ_GPIO(gpio_buttons[i].gpio), NULL);
	}
	
	input_unregister_device(input_dev);
	input_free_device(input_dev);
	return 0;
}

static int buttons_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int buttons_resume(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver buttons_driver = {
    .driver = {
	.name           = "g900-button",
    },
    .probe          = buttons_probe,
    .remove         = buttons_remove,
#ifdef CONFIG_PM
    .suspend        = buttons_suspend,
    .resume         = buttons_resume,
#endif
};

static int __init buttons_init(void)
{
	return platform_driver_register(&buttons_driver);
}

static void __exit buttons_exit(void)
{
	platform_driver_unregister(&buttons_driver);
}

module_init(buttons_init);
module_exit(buttons_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("El Tuba<tuba.linux@gmail.com>");
MODULE_DESCRIPTION("Buttons driver for Toshiba G900");
