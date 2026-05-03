/*linux/drivers/video/g900fb.c -- Toshiba g900 Simple Frame Buffer device
 *
 *	Copyright (C) 2008 El Tuba
 *
 *	Based on N311fb framebuffer
 *
 *	Copyright (C) 2008 Vladimir Chernichkin
 *
 *      Copyright (C) 2002 James Simmons
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <linux/fb.h>
#include <linux/init.h>
#include <asm/io.h>

#define VIDEOMEMSIZE	(4*800*480)	/* screen size KB */
#define VIDEOMEMSTART 0x0a000020	//vram variable value from HaRET
static void *videomemory;
static u_long videomemorysize = VIDEOMEMSIZE;
module_param(videomemorysize, ulong, 0);


static struct fb_var_screeninfo g900fb_default __initdata = {

	.xres =		480,
	.yres =		800,
	.xres_virtual =	480,
	.yres_virtual =	800,
	.bits_per_pixel	= 16,
	.red =		{ 11, 5, 0 },
      	.green =	{ 5, 6, 0 },
      	.blue =		{ 0, 5, 0 },
	.height		= -1,
	.width		= -1,
	.accel_flags	= FB_ACCEL_NONE,
	//The following values seem to not affect the screeen behavior	
	.pixclock	= 10,
	.left_margin	= 0,
	.right_margin	= 0,
	.upper_margin	= 33,
	.lower_margin	= 10,
	.hsync_len	= 96,
	.vsync_len	= 2,
	.vmode		= FB_VMODE_NONINTERLACED
};

static struct fb_fix_screeninfo g900fb_fix __initdata = {
	.id =		"g900 FB",
	.type =		FB_TYPE_PACKED_PIXELS,	
	.visual =	FB_VISUAL_TRUECOLOR,
	.xpanstep =	8,
	.accel =	FB_ACCEL_NONE,
	.ypanstep = 	1,
	.ywrapstep = 	0,
	.smem_start =	VIDEOMEMSTART,
	.smem_len = 	VIDEOMEMSIZE,
};

static int g900fb_enable __initdata = 1;	/* disabled by default */
module_param(g900fb_enable, bool, 0);

static int g900fb_check_var(struct fb_var_screeninfo *var,
			 struct fb_info *info);
static int g900fb_set_par(struct fb_info *info);
static int g900fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			 u_int transp, struct fb_info *info);
static int g900fb_pan_display(struct fb_var_screeninfo *var,struct fb_info *info);


static struct fb_ops g900fb_ops = {
	
	.fb_check_var	= g900fb_check_var,
	.fb_set_par	= g900fb_set_par,
	.fb_setcolreg	= g900fb_setcolreg,
	.fb_pan_display	= g900fb_pan_display,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	
};

    /*
     *  Internal routines
     */

static u_long get_line_length(int xres_virtual, int bpp)
{
	u_long length;

	length = xres_virtual * bpp;
	length = (length + 31) & ~31;
	length >>= 3;
	return (length);
}

    /*
     *  Setting the video mode has been split into two parts.
     *  First part, xxxfb_check_var, must not write anything
     *  to hardware, it should only verify and adjust var.
     *  This means it doesn't alter par but it does use hardware
     *  data from it to check this var. 
     */

static int g900fb_check_var(struct fb_var_screeninfo *var,
			 struct fb_info *info)
{
	u_long line_length;

	if (!var->xres)
		var->xres = 1;
	if (!var->yres)
		var->yres = 1;
	if (var->xres > var->xres_virtual)
		var->xres_virtual = var->xres;
	if (var->yres > var->yres_virtual)
		var->yres_virtual = var->yres;
	if (var->bits_per_pixel <= 1)
		var->bits_per_pixel = 1;
	else if (var->bits_per_pixel <= 8)
		var->bits_per_pixel = 8;
	else if (var->bits_per_pixel <= 16)
		var->bits_per_pixel = 16;
	else if (var->bits_per_pixel <= 24)
		var->bits_per_pixel = 24;
	else if (var->bits_per_pixel <= 32)
		var->bits_per_pixel = 32;
	else
		return -EINVAL;

	if (var->xres_virtual < var->xoffset + var->xres)
		var->xres_virtual = var->xoffset + var->xres;
	if (var->yres_virtual < var->yoffset + var->yres)
		var->yres_virtual = var->yoffset + var->yres;

	/*
	 *  Memory limit
	 */
	line_length =
	    get_line_length(var->xres_virtual, var->bits_per_pixel);
	if (line_length * var->yres_virtual > videomemorysize)
		return -ENOMEM;

	/*
	 * Now that we checked it we alter var. The reason being is that the video
	 * mode passed in might not work but slight changes to it might make it 
	 * work. This way we let the user know what is acceptable.
	 */
	switch (var->bits_per_pixel) {
	case 1:
	case 8:
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 16:		/* RGBA 5551 */
		if (var->transp.length) {
			var->red.offset = 0;
			var->red.length = 5;
			var->green.offset = 5;
			var->green.length = 5;
			var->blue.offset = 10;
			var->blue.length = 5;
			var->transp.offset = 15;
			var->transp.length = 1;
		} else {	/* RGB 565 */
			var->red.offset = 0;
			var->red.length = 5;
			var->green.offset = 5;
			var->green.length = 6;
			var->blue.offset = 11;
			var->blue.length = 5;
			var->transp.offset = 0;
			var->transp.length = 0;
		}
		break;
	case 24:		/* RGB 888 */
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 16;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 32:		/* RGBA 8888 */
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 16;
		var->blue.length = 8;
		var->transp.offset = 24;
		var->transp.length = 8;
		break;
	}
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;

	return 0;
}

/* This routine actually sets the video mode. It's in here where we
 * the hardware state info->par and fix which can be affected by the 
 * change in par. For this driver it doesn't do much. 
 */
static int g900fb_set_par(struct fb_info *info)
{
	info->fix.line_length = get_line_length(info->var.xres_virtual,	info->var.bits_per_pixel);
	return 0;
}

    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     */

static int g900fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			 u_int transp, struct fb_info *info)
{
	if (regno >= 256) /* no. of hw registers */
		return 1;
	/*
	 * Program hardware... do anything you want with transp
	 */

	/* grayscale works only partially under directcolor */
	/* grayscale = 0.30*R + 0.59*G + 0.11*B */
	if (info->var.grayscale) {red = green = blue =(red * 77 + green * 151 + blue * 28) >> 8;}

	/* Directcolor:
	 *   var->{color}.offset contains start of bitfield
	 *   var->{color}.length contains length of bitfield
	 *   {hardwarespecific} contains width of RAMDAC
	 *   cmap[X] is programmed to (X << red.offset) | (X << green.offset) | (X << blue.offset)
	 *   RAMDAC[X] is programmed to (red, green, blue)
	 * 
	 * Pseudocolor:
	 *    uses offset = 0 && length = RAMDAC register width.
	 *    var->{color}.offset is 0
	 *    var->{color}.length contains widht of DAC
	 *    cmap is not used
	 *    RAMDAC[X] is programmed to (red, green, blue)
	 * Truecolor:
	 *    does not use DAC. Usually 3 are present.
	 *    var->{color}.offset contains start of bitfield
	 *    var->{color}.length contains length of bitfield
	 *    cmap is programmed to (red << red.offset) | (green << green.offset) |
	 *                      (blue << blue.offset) | (transp << transp.offset)
	 *    RAMDAC does not exist
	 */
#define CNVT_TOHW(val,width) ((((val)<<(width))+0x7FFF-(val))>>16)
	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
	case FB_VISUAL_PSEUDOCOLOR:
		red = CNVT_TOHW(red, info->var.red.length);
		green = CNVT_TOHW(green, info->var.green.length);
		blue = CNVT_TOHW(blue, info->var.blue.length);
		transp = CNVT_TOHW(transp, info->var.transp.length);
		break;
	case FB_VISUAL_DIRECTCOLOR:
		red = CNVT_TOHW(red, 8);	/* expect 8 bit DAC */
		green = CNVT_TOHW(green, 8);
		blue = CNVT_TOHW(blue, 8);
		/* hey, there is bug in transp handling... */
		transp = CNVT_TOHW(transp, 8);
		break;
	}
#undef CNVT_TOHW
	/* Truecolor has hardware independent palette */
	if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
		u32 v;
		
		if (regno >= 16)
			return 1;

		v = (red << info->var.red.offset) |
		    (green << info->var.green.offset) |
		    (blue << info->var.blue.offset) |
		    (transp << info->var.transp.offset);
		
		switch (info->var.bits_per_pixel) {
		case 8:
			break;
		case 16:
			((u32 *) (info->pseudo_palette))[regno] = v;
			break;
		case 24:
		case 32:
			((u32 *) (info->pseudo_palette))[regno] = v;
			break;
		}
		return 0;
	}
	return 0;
}


    /*
     *  Pan or Wrap the Display
     *
     *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
     */

static int g900fb_pan_display(struct fb_var_screeninfo *var,
			   struct fb_info *info)
{
	u32 total;

	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->yoffset < 0
		    || var->yoffset >= info->var.yres_virtual
		    || var->xoffset)
			return -EINVAL;
	} else {
		if (var->xoffset + var->xres > info->var.xres_virtual ||
		    var->yoffset + var->yres > info->var.yres_virtual)
			return -EINVAL;
	}
	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP)
		info->var.vmode |= FB_VMODE_YWRAP;
	else
		info->var.vmode &= ~FB_VMODE_YWRAP;
	

	
//	var->xoffset = 20;
	total = var->yoffset * info->fix.line_length + var->xoffset;

	// Needs to be rewritten
        info->fix.smem_start=VIDEOMEMSTART - total;


	return 0;
}




#ifndef MODULE
static int __init g900fb_setup(char *options)
{
	char *this_opt;

	g900fb_enable = 1;

	if (!options || !*options)
		return 1;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;
		if (!strncmp(this_opt, "disable", 7))
			g900fb_enable = 0;
	}
	return 1;
}
#endif  /*  MODULE  */

    /*
     *  Initialisation
     */

static int __init g900fb_probe(struct platform_device *dev)
{
	struct fb_info *info;
	int retval = -ENOMEM;
	
	info = framebuffer_alloc(sizeof(u32) * 256, &dev->dev);
	if (!info)
		goto err;

	if (!request_mem_region(VIDEOMEMSTART, VIDEOMEMSIZE,"g900fb")) {
		printk(KERN_INFO"g900fb: cannot get framebuffer\n");
		goto err2;
	}
	


	info->screen_base = ioremap(VIDEOMEMSTART,videomemorysize);
	info->fbops = &g900fb_ops;
	info->var = g900fb_default;
	info->fix = g900fb_fix;
	info->pseudo_palette = info->par;
	info->par = NULL;
	info->flags = FBINFO_FLAG_DEFAULT;
	
	retval = fb_alloc_cmap(&info->cmap, 256, 0);
	if (retval < 0)
		goto err1;

	retval = register_framebuffer(info);
	if (retval < 0)
		goto err2;
	platform_set_drvdata(dev, info);

	printk(KERN_INFO"fb%d: g900 frame buffer device, using %ldK of video memory\n",
	       info->node, videomemorysize >> 10);
	return 0;
err2:
	fb_dealloc_cmap(&info->cmap);
err1:
	framebuffer_release(info);
err:
	iounmap(videomemory);
	return retval;
}

static int g900fb_remove(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);

	if (info) {
		unregister_framebuffer(info);
		iounmap(videomemory);
		framebuffer_release(info);
	}
	return 0;
}

static struct platform_driver g900fb_driver = {
	.probe	= g900fb_probe,
	.remove = g900fb_remove,
	.driver = {
		.name	= "g900fb",
	},
};

static struct platform_device *g900fb_device;

static int __init g900fb_init(void)
{
	int ret = 0;

#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("g900fb", &option))
		return -ENODEV;
	g900fb_setup(option);
#endif

	if (!g900fb_enable)
		return -ENXIO;

	ret = platform_driver_register(&g900fb_driver);

	if (!ret) {
		g900fb_device = platform_device_alloc("g900fb", 0);

		if (g900fb_device)
			ret = platform_device_add(g900fb_device);
		else
			ret = -ENOMEM;

		if (ret) {
			platform_device_put(g900fb_device);
			platform_driver_unregister(&g900fb_driver);
		}
	}

	return ret;
}

module_init(g900fb_init);

#ifdef MODULE
static void __exit g900fb_exit(void)
{
	platform_device_unregister(g900fb_device);
	platform_driver_unregister(&g900fb_driver);
}

module_exit(g900fb_exit);
MODULE_AUTHOR("El Tuba <tuba.linux@gmail.com>");
MODULE_DESCRIPTION("Framebuffer driver for the Toshiba g900");
MODULE_LICENSE("GPL");
#endif				/* MODULE */
