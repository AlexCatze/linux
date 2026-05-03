/*
 * drivers/input/touchscreen/ak4183.c
 *
 * Copyright (c) 2008 Angell
 *	Kwangwoo Lee <angell@angellfear.ru>
 *  
 * Using code from:
 *  tsc2007.c
 *	
 *	Copyright (c) 2008 MtekVision Co., Ltd.
 *	Copyright (c) 2005 David Brownell
 *	Copyright (c) 2006 Nokia Corporation
 *  - corgi_ts.c
 *	Copyright (C) 2004-2005 Richard Purdie
 *  - omap_ts.[hc], ads7846.h, ts_osk.c
 *	Copyright (C) 2002 MontaVista Software
 *	Copyright (C) 2004 Texas Instruments
 *	Copyright (C) 2005 Dirk Behme
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/i2c/ak4183.h>

#define TS_POLL_DELAY			1 /* ms delay between samples */
#define TS_POLL_PERIOD			1 /* ms delay between samples */


#define AK4183_MEASURE_X		0xc0
#define AK4183_MEASURE_Y		0xd0
#define AK4183_MEASURE_Z1		0xE0
#define AK4183_MEASURE_Z2		0xF0

#define AK4183_POWER_OFF_IRQ_EN	0x00
#define AK4183_ADC_ON_IRQ_DIS0		(0x1 << 2)

#define AK4183_12BIT			0x02

#define	MAX_12BIT			((1 << 12) - 1)

#define ADC_ON_12BIT	0x02
//(AK4183_12BIT | AK4183_ADC_ON_IRQ_DIS0)

#define READ_Y		(ADC_ON_12BIT | AK4183_MEASURE_Y)
#define READ_Z1		(ADC_ON_12BIT | AK4183_MEASURE_Z1)
#define READ_Z2		(ADC_ON_12BIT | AK4183_MEASURE_Z2)
#define READ_X		(ADC_ON_12BIT | AK4183_MEASURE_X)
#define PWRDOWN		(AK4183_12BIT | AK4183_POWER_OFF_IRQ_EN)

struct ts_event {
	u16	x;
	u16	y;
	u16	z1, z2;
};

struct ak4183 {
	struct input_dev	*input;
	char			phys[32];
	struct delayed_work	work;

	struct i2c_client	*client;

	u16			model;
	u16			x_plate_ohms;

	bool			pendown;
	int			irq;

	int			(*get_pendown_state)(void);
	void			(*clear_penirq)(void);
};

static inline int ak4183_xfer(struct ak4183 *tsc, u8 cmd)
{
	s32 data;
	u16 val;

	data = i2c_smbus_read_word_data(tsc->client, cmd);
	if (data < 0) {
		dev_err(&tsc->client->dev, "i2c io error: %d\n", data);
		return data;
	}
	
	/* The protocol and raw data format from i2c interface:
	 * S Addr Wr [A] Comm [A] S Addr Rd [A] [DataLow] A [DataHigh] NA P
	 * Where DataLow has [D11-D4], DataHigh has [D3-D0 << 4 | Dummy 4bit].
	 */
	val = swab16(data) >> 4;

	dev_dbg(&tsc->client->dev, "data: 0x%x, val: 0x%x\n", data, val);

	return val;
}

static void ak4183_read_values(struct ak4183 *tsc, struct ts_event *tc)
{
	/* y- still on; turn on only y+ (and ADC) */
	tc->y = ak4183_xfer(tsc, READ_Y);

	/* turn y- off, x+ on, then leave in lowpower */
	tc->x = ak4183_xfer(tsc, READ_X);

	/* turn y+ off, x- on; we'll use formula #1 */
	tc->z1 = ak4183_xfer(tsc, READ_Z1);
	tc->z2 = ak4183_xfer(tsc, READ_Z2);

	/* Prepare for next touch reading - power down ADC, enable PENIRQ */
	//ak4183_xfer(tsc, PWRDOWN);
}

static u32 ak4183_calculate_pressure(struct ak4183 *tsc, struct ts_event *tc)
{
	u32 rt = 0;

	/* range filtering */
	if (tc->x == MAX_12BIT)
		tc->x = 0;

	if (likely(tc->x && tc->z1)) {
		/* compute touch pressure resistance using equation #1 */
		rt = tc->z2 - tc->z1;
		rt *= tc->x;
		rt *= tsc->x_plate_ohms;
		rt /= tc->z1;
		rt = (rt + 2047) >> 12;
	}

	return rt;
}

static void ak4183_send_up_event(struct ak4183 *tsc)
{
	struct input_dev *input = tsc->input;

	dev_dbg(&tsc->client->dev, "UP\n");

	input_report_key(input, BTN_TOUCH, 0);
	input_report_abs(input, ABS_PRESSURE, 0);
	input_sync(input);
}

static void ak4183_work(struct work_struct *work)
{
	struct ak4183 *ts =
		container_of(to_delayed_work(work), struct ak4183, work);
	struct ts_event tc;
	u32 rt;

	/*
	 * NOTE: We can't rely on the pressure to determine the pen down
	 * state, even though this controller has a pressure sensor.
	 * The pressure value can fluctuate for quite a while after
	 * lifting the pen and in some cases may not even settle at the
	 * expected value.
	 *
	 * The only safe way to check for the pen up condition is in the
	 * work function by reading the pen signal state (it's a GPIO
	 * and IRQ). Unfortunately such callback is not always available,
	 * in that case we have rely on the pressure anyway.
	 */
	if (ts->get_pendown_state) {
		if (unlikely(!ts->get_pendown_state())) {
			ak4183_send_up_event(ts);
			ts->pendown = false;
			goto out;
		}

		dev_dbg(&ts->client->dev, "pen is still down\n");
	}

	ak4183_read_values(ts, &tc);

	rt = ak4183_calculate_pressure(ts, &tc);
	if (rt > MAX_12BIT) {
		/*
		 * Sample found inconsistent by debouncing or pressure is
		 * beyond the maximum. Don't report it to user space,
		 * repeat at least once more the measurement.
		 */
		dev_dbg(&ts->client->dev, "ignored pressure %d\n", rt);
		goto out;

	}

	if (rt) {
		struct input_dev *input = ts->input;

		if (!ts->pendown) {
			dev_dbg(&ts->client->dev, "DOWN\n");

			input_report_key(input, BTN_TOUCH, 1);
			ts->pendown = true;
		}

		input_report_abs(input, ABS_X, tc.x);
		input_report_abs(input, ABS_Y, tc.y);
		input_report_abs(input, ABS_PRESSURE, rt);

		input_sync(input);

		dev_dbg(&ts->client->dev, "point(%4d,%4d), pressure (%4u)\n",
			tc.x, tc.y, rt);


	} else if (!ts->get_pendown_state && ts->pendown) {
		/*
		 * We don't have callback to check pendown state, so we
		 * have to assume that since pressure reported is 0 the
		 * pen was lifted up.
		 */
		ak4183_send_up_event(ts);
		ts->pendown = false;
	}

 out:
	if (ts->pendown)
		schedule_delayed_work(&ts->work,
				      msecs_to_jiffies(TS_POLL_PERIOD));
	else
		enable_irq(ts->irq);
}

static irqreturn_t ak4183_irq(int irq, void *handle)
{
	struct ak4183 *ts = handle;

	if (!ts->get_pendown_state || likely(ts->get_pendown_state())) {
		disable_irq_nosync(ts->irq);
		schedule_delayed_work(&ts->work,
				      msecs_to_jiffies(TS_POLL_DELAY));
	}

	if (ts->clear_penirq)
		ts->clear_penirq();
/**/
		schedule_delayed_work(&ts->work,
				      msecs_to_jiffies(TS_POLL_DELAY));
/**/
	return IRQ_HANDLED;
}

static void ak4183_free_irq(struct ak4183 *ts)
{
	free_irq(ts->irq, ts);

	if (cancel_delayed_work_sync(&ts->work)) {
		/*
		 * Work was pending, therefore we need to enable
		 * IRQ here to balance the disable_irq() done in the
		 * interrupt handler.
		 */
		enable_irq(ts->irq);
	}

}

static int __devinit ak4183_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct ak4183 *ts;
	struct ak4183_platform_data *pdata = pdata = client->dev.platform_data;
	struct input_dev *input_dev;
	int err;
	printk(KERN_INFO"ak4183-ts: probe\n");

	if (!pdata) {

		dev_err(&client->dev, "platform data is required!\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -EIO;
	ts = kzalloc(sizeof(struct ak4183), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ts || !input_dev) {
		err = -ENOMEM;
		goto err_free_mem;
	}

	ts->client = client;
	ts->irq = client->irq;
	ts->input = input_dev;
	INIT_DELAYED_WORK(&ts->work, ak4183_work);


	ts->model             = pdata->model;
	ts->x_plate_ohms      = pdata->x_plate_ohms;
	ts->get_pendown_state = pdata->get_pendown_state;
	ts->clear_penirq      = pdata->clear_penirq;

	snprintf(ts->phys, sizeof(ts->phys),
		 "%s/input0", dev_name(&client->dev));

//	strcpy(ts->phys, "input/ts0");
	
	input_dev->name = "AK4183 Touchscreen";
	input_dev->phys = ts->phys;
	input_dev->id.bustype = BUS_I2C;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	input_set_abs_params(input_dev, ABS_X, 0, MAX_12BIT, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, MAX_12BIT, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, MAX_12BIT, 0, 0);

	if (pdata->init_platform_hw)
		pdata->init_platform_hw();


	err = request_irq(ts->irq, ak4183_irq, 
			IRQF_DISABLED | IRQF_TRIGGER_FALLING,
			client->dev.driver->name, ts);

	if (err < 0) {
		dev_err(&client->dev, "irq %d busy?\n", ts->irq);
		goto err_free_mem;
	}

	/* Prepare for touch readings - power down ADC and enable PENIRQ */
	//err = ak4183_xfer(ts, PWRDOWN);
	//if (err < 0)
	//	goto err_free_irq;

	err = input_register_device(input_dev);
	if (err)
		goto err_free_irq;
	
	i2c_set_clientdata(client, ts);


	return 0;

 err_free_irq:
	ak4183_free_irq(ts);
	if (pdata->exit_platform_hw)
		pdata->exit_platform_hw();
 err_free_mem:
	input_free_device(input_dev);
	kfree(ts);
	return err;
}

static int __devexit ak4183_remove(struct i2c_client *client)
{
	struct ak4183	*ts = i2c_get_clientdata(client);
	struct ak4183_platform_data *pdata = client->dev.platform_data;

	ak4183_free_irq(ts);

	if (pdata->exit_platform_hw)
		pdata->exit_platform_hw();

	input_unregister_device(ts->input);
	kfree(ts);

	return 0;
}

static struct i2c_device_id ak4183_idtable[] = {
	{ "ak4183", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, ak4183_idtable);

static struct i2c_driver ak4183_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "ak4183"
	},
	.id_table	= ak4183_idtable,
	.probe		= ak4183_probe,
	.remove		= __devexit_p(ak4183_remove),
};

static int __init ak4183_init(void)
{
	return i2c_add_driver(&ak4183_driver);
}

static void __exit ak4183_exit(void)
{
	i2c_del_driver(&ak4183_driver);
}

module_init(ak4183_init);
module_exit(ak4183_exit);

MODULE_AUTHOR("angell <angell@angellfear.ru>");
MODULE_DESCRIPTION("AK4183 TouchScreen Driver");
MODULE_LICENSE("GPL");
