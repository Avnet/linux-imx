// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for ON Semiconductor AS0260 1/6" 1080p High-Definition(HD) 
 * 2.0Mp SOC CMOS Image Sensor Digital Image Sensor
 *
 * Copyright (c) 2021 Avnet Inc <www.avnet.com>
 * Based on Aptina AS0260 SoC Camera Driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <media/soc_camera.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>

static unsigned int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level");

#define as0260_dbg(fmt,args...) do{ if(debug){dev_info(&client->dev, fmt, args);} }while(0)

#define AS0260_CHIPID                        0x4581

/* SYSCTL Registers */
#define CHIP_ID_REG                          0x0000
#define RESET_REG                            0x301A
#define RESET_AND_MISC_CONTROL               0x001A
#define MCU_BOOT_MODE                        0x001C
#define PHYSICAL_ADDRESS_ACCESS              0x098A
#define LOGICAL_ADDRESS_ACCESS               0x098E
#define ACCESS_CTL_STAT                      0x0982
#define COMMAND_REGISTER                     0x0080
#define SYSMGR_NEXT_STATE                    0xDC00
#define SYSMGR_CURRENT_STATE                 0xDC01

/* PLL Settings Registers */
#define CAM_SYSCTL_PLL_ENABLE                0xCA12
#define CAM_SYSCTL_PLL_DIV2_EN               0xCA13
#define CAM_SYSCTL_PLL_DIVIDER_M_N           0xCA14
#define CAM_SYSCTL_PLL_DIVIDER_P             0xCA16
#define CAM_SYSCTL_PLL_DIVIDER_P4_P5_P6      0xCA18
#define CAM_SYSCTL_PLL_DIVIDER_P7            0xCA1A
#define CAM_PORT_OUTPUT_CONTROL              0xCA1C

/* Timing settings Registers */
#define CAM_SENSOR_CFG_Y_ADDR_START          0xC800
#define CAM_SENSOR_CFG_X_ADDR_START          0xC802
#define CAM_SENSOR_CFG_Y_ADDR_END            0xC804
#define CAM_SENSOR_CFG_X_ADDR_END            0xC806
#define CAM_SENSOR_CFG_PIXCLK_H              0xC808
#define CAM_SENSOR_CFG_PIXCLK_L              0xC80A
#define CAM_SENSOR_CFG_ROW_SPEED             0xC80C
#define CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN   0xC80E
#define CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX   0xC810
#define CAM_SENSOR_CFG_FRAME_LENGTH_LINES    0xC812
#define CAM_SENSOR_CFG_LINE_LENGTH_PCK       0xC814
#define CAM_SENSOR_CFG_FINE_CORRECTION       0xC816
#define CAM_SENSOR_CFG_CPIPE_LAST_ROW        0xC818
#define CAM_SENSOR_CONTROL_READ_MODE         0xC830
#define CAM_CROP_WINDOW_XOFFSET              0xC858
#define CAM_CROP_WINDOW_YOFFSET              0xC85A
#define CAM_CROP_WINDOW_WIDTH                0xC85C
#define CAM_CROP_WINDOW_HEIGHT               0xC85E
#define CAM_OUTPUT_WIDTH                     0xC86C
#define CAM_OUTPUT_HEIGHT                    0xC86E
#define CAM_OUTPUT_FORMAT                    0xC870
#define CAM_AET_MAX_FRAME_RATE               0xC88E
#define CAM_AET_MIN_FRAME_RATE               0xC890
#define CAM_STAT_AWB_CLIP_WINDOW_XSTART      0xC94C
#define CAM_STAT_AWB_CLIP_WINDOW_YSTART      0xC94E
#define CAM_STAT_AWB_CLIP_WINDOW_XEND        0xC950
#define CAM_STAT_AWB_CLIP_WINDOW_YEND        0xC952
#define CAM_STAT_AE_INITIAL_WINDOW_XSTART    0xC954
#define CAM_STAT_AE_INITIAL_WINDOW_YSTART    0xC956
#define CAM_STAT_AE_INITIAL_WINDOW_XEND      0xC958
#define CAM_STAT_AE_INITIAL_WINDOW_YEND      0xC95A
#define OUTPUT_FORMAT_CONFIGURATION          0x332E

enum as0260_fmt {
	AS0260_FMT_1920x1080,	/* 1080P  */
	AS0260_FMT_1280x720,	/* 720P */
	AS0260_FMT_MAX,
};

typedef struct {
	u32 width;
	u32 height;
	u32 mbus_code;
	enum as0260_fmt mode;
	enum v4l2_field field;
	enum v4l2_colorspace colorspace;
} as0260_format_t;

struct as0260_sensor {
	struct v4l2_subdev sd;
	as0260_format_t *fmt;
	struct v4l2_captureparm streamcap;

	int rst_gpio;		/* reset GPIO pin */
	u32 mclk;		/* AS0260 Rapi adapter crystal frequency */
};

#define to_sensor(x) container_of(x, struct as0260_sensor, sd)

typedef struct {
	u16 addr;
	u16 val;
	u16 mask;
} as0260_reg;

struct as0260_datafmt {
	u32 code;
	enum v4l2_colorspace colorspace;
};

static const struct as0260_datafmt as0260_colour_fmts[] = {
	{ MEDIA_BUS_FMT_YUYV8_2X8, V4L2_COLORSPACE_JPEG },
};

static const int as0260_framerates[] = {
	30, 15, 25, 20, 18
};

as0260_format_t as0260_formats[AS0260_FMT_MAX] = {
	[AS0260_FMT_1920x1080] = {
				  .mode = AS0260_FMT_1920x1080,
				  .width = 1920,
				  .height = 1080,
				  .mbus_code = MEDIA_BUS_FMT_YUYV8_2X8,
				  .field = V4L2_FIELD_NONE,
				  .colorspace = V4L2_COLORSPACE_JPEG,
				   },
	[AS0260_FMT_1280x720] = {
				.mode = AS0260_FMT_1280x720,
				.width = 1280,
				.height = 720,
				.mbus_code = MEDIA_BUS_FMT_YUYV8_2X8,
				.field = V4L2_FIELD_NONE,
				.colorspace = V4L2_COLORSPACE_JPEG,
				 },
};

/* register configure table for 1920x1080 */
static const as0260_reg regs_1920x1080[] = {
	/* logic address access */
	{ LOGICAL_ADDRESS_ACCESS, 0x4800, 2 },

	{ CAM_SENSOR_CFG_Y_ADDR_START, 0x0020, 2 },
	{ CAM_SENSOR_CFG_X_ADDR_START, 0x0020, 2 },
	{ CAM_SENSOR_CFG_Y_ADDR_END, 0x045F, 2 },
	{ CAM_SENSOR_CFG_X_ADDR_END, 0x07A7, 2 },

	{ CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN, 0x0336, 2 },
	{ CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX, 0x0A7B, 2 },
	{ CAM_SENSOR_CFG_FRAME_LENGTH_LINES, 0x0491, 2 },
	{ CAM_SENSOR_CFG_FINE_CORRECTION, 0x00D4, 2 },
	{ CAM_SENSOR_CFG_CPIPE_LAST_ROW, 0x043B, 2 },
	{ CAM_SENSOR_CONTROL_READ_MODE, 0x0002, 2 },

	{ CAM_CROP_WINDOW_XOFFSET, 0x0000, 2 },
	{ CAM_CROP_WINDOW_YOFFSET, 0x0000, 2 },
	{ CAM_CROP_WINDOW_WIDTH, 1920, 2 },
	{ CAM_CROP_WINDOW_HEIGHT, 1080, 2 },
	{ CAM_OUTPUT_WIDTH, 1920, 2 },
	{ CAM_OUTPUT_HEIGHT, 1080, 2 },

	{ CAM_STAT_AWB_CLIP_WINDOW_XSTART, 0x0000, 2 },
	{ CAM_STAT_AWB_CLIP_WINDOW_YSTART, 0x0000, 2 },
	{ CAM_STAT_AWB_CLIP_WINDOW_XEND, 1919, 2 },
	{ CAM_STAT_AWB_CLIP_WINDOW_YEND, 1079, 2 },

	{ CAM_STAT_AE_INITIAL_WINDOW_XSTART, 0x0000, 2 },
	{ CAM_STAT_AE_INITIAL_WINDOW_YSTART, 0x0000, 2 },
	{ CAM_STAT_AE_INITIAL_WINDOW_XEND, 0x017F, 2 },
	{ CAM_STAT_AE_INITIAL_WINDOW_YEND, 0x00D7, 2 },
};

/* register configure table for 1280x720 */
static const as0260_reg regs_1280x720[] = {
	/* logic address access */
	{ LOGICAL_ADDRESS_ACCESS, 0x4800, 2 },

	{ CAM_SENSOR_CFG_Y_ADDR_START, 0x0020, 2 },
	{ CAM_SENSOR_CFG_X_ADDR_START, 0x0020, 2 },
	{ CAM_SENSOR_CFG_Y_ADDR_END, 0x045F, 2 },
	{ CAM_SENSOR_CFG_X_ADDR_END, 0x07A7, 2 },

	{ CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN, 0x0336, 2 },
	{ CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX, 0x0A7B, 2 },
	{ CAM_SENSOR_CFG_FRAME_LENGTH_LINES, 0x0491, 2 },
	{ CAM_SENSOR_CFG_FINE_CORRECTION, 0x00D4, 2 },
	{ CAM_SENSOR_CFG_CPIPE_LAST_ROW, 0x043B, 2 },
	{ CAM_SENSOR_CONTROL_READ_MODE, 0x0002, 2 },

	{ CAM_CROP_WINDOW_XOFFSET, 0x0000, 2 },
	{ CAM_CROP_WINDOW_YOFFSET, 0x0000, 2 },
	{ CAM_CROP_WINDOW_WIDTH, 1920, 2 },
	{ CAM_CROP_WINDOW_HEIGHT, 1080, 2 },
	{ CAM_OUTPUT_WIDTH, 1280, 2 },
	{ CAM_OUTPUT_HEIGHT, 720, 2 },

	{ CAM_STAT_AWB_CLIP_WINDOW_XSTART, 0x0000, 2 },
	{ CAM_STAT_AWB_CLIP_WINDOW_YSTART, 0x0000, 2 },
	{ CAM_STAT_AWB_CLIP_WINDOW_XEND, 1279, 2 },
	{ CAM_STAT_AWB_CLIP_WINDOW_YEND, 719, 2 },

	{ CAM_STAT_AE_INITIAL_WINDOW_XSTART, 0x0000, 2 },
	{ CAM_STAT_AE_INITIAL_WINDOW_YSTART, 0x0000, 2 },
	{ CAM_STAT_AE_INITIAL_WINDOW_XEND, 0x00FF, 2 },
	{ CAM_STAT_AE_INITIAL_WINDOW_YEND, 0x008F, 2 },
};



/* register configure table for 640x480 */
static const as0260_reg regs_640x480[] = {
	/* logic address access */
	{ LOGICAL_ADDRESS_ACCESS, 0x4800, 2 },

	{ CAM_SENSOR_CFG_Y_ADDR_START, 0x0020, 2 },
	{ CAM_SENSOR_CFG_X_ADDR_START, 0x0100, 2 },
	{ CAM_SENSOR_CFG_Y_ADDR_END, 0x045D, 2 },
	{ CAM_SENSOR_CFG_X_ADDR_END, 0x06AD, 2 },

	{ CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN, 0x06A4, 2 },
	{ CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX, 0x085E, 2 },
	{ CAM_SENSOR_CFG_FRAME_LENGTH_LINES, 0x04DA, 2 },
	{ CAM_SENSOR_CFG_FINE_CORRECTION, 0x01D9, 2 },
	{ CAM_SENSOR_CFG_CPIPE_LAST_ROW, 0x021B, 2 },
	{ CAM_SENSOR_CONTROL_READ_MODE, 0x0012, 2 },

	{ CAM_CROP_WINDOW_XOFFSET, 0x0000, 2 },
	{ CAM_CROP_WINDOW_YOFFSET, 0x0000, 2 },
	{ CAM_CROP_WINDOW_WIDTH, 720, 2 },
	{ CAM_CROP_WINDOW_HEIGHT, 536, 2 },
	{ CAM_OUTPUT_WIDTH, 640, 2 },
	{ CAM_OUTPUT_HEIGHT, 480, 2 },

	{ CAM_STAT_AWB_CLIP_WINDOW_XSTART, 0x0000, 2 },
	{ CAM_STAT_AWB_CLIP_WINDOW_YSTART, 0x0000, 2 },
	{ CAM_STAT_AWB_CLIP_WINDOW_XEND, 639, 2 },
	{ CAM_STAT_AWB_CLIP_WINDOW_YEND, 479, 2 },

	{ CAM_STAT_AE_INITIAL_WINDOW_XSTART, 0x0000, 2 },
	{ CAM_STAT_AE_INITIAL_WINDOW_YSTART, 0x0000, 2 },
	{ CAM_STAT_AE_INITIAL_WINDOW_XEND, 0x007F, 2 },
	{ CAM_STAT_AE_INITIAL_WINDOW_YEND, 0x005F, 2 }
};

static const as0260_reg upper32k_phy_reg[] = {
	/* R0x0982 bit[0]: en_upper_32k_phy_access bit[15] 
	 * R0x098A bit[14:0]: en_upper_32k_phy_access bit[14:0] 
	 */
	{ 0x0982, 0x0001, 2 },
	{ 0x098A, 0x6B30, 2 },

	/* physical_address: 1<<15 | 0x6B30 = 0xEB30 */
	{ 0xEB30, 0xC0F1, 2 },
	{ 0xEB32, 0x0942, 2 },
	{ 0xEB34, 0x0720, 2 },
	{ 0xEB36, 0xD81D, 2 },
	{ 0xEB38, 0x0F8A, 2 },
	{ 0xEB3A, 0x0700, 2 },
	{ 0xEB3C, 0xE080, 2 },
	{ 0xEB3E, 0x0066, 2 },
	{ 0xEB40, 0x0001, 2 },
	{ 0xEB42, 0x0DE6, 2 },
	{ 0xEB44, 0x07E0, 2 },
	{ 0xEB46, 0xD802, 2 },
	{ 0xEB48, 0xD900, 2 },
	{ 0xEB4A, 0x70CF, 2 },
	{ 0xEB4C, 0xFF00, 2 },
	{ 0xEB4E, 0x31B0, 2 },
	{ 0xEB50, 0xB038, 2 },
	{ 0xEB52, 0x1CFC, 2 },
	{ 0xEB54, 0xB388, 2 },
	{ 0xEB56, 0x76CF, 2 },
	{ 0xEB58, 0xFF00, 2 },
	{ 0xEB5A, 0x33CC, 2 },
	{ 0xEB5C, 0x200A, 2 },
	{ 0xEB5E, 0x0F80, 2 },
	{ 0xEB60, 0xFFFF, 2 },
	{ 0xEB62, 0xEB78, 2 },
	{ 0xEB64, 0x1CFC, 2 },
	{ 0xEB66, 0xB008, 2 },
	{ 0xEB68, 0x2022, 2 },
	{ 0xEB6A, 0x0F80, 2 },
	{ 0xEB6C, 0x0000, 2 },
	{ 0xEB6E, 0xFC3C, 2 },
	{ 0xEB70, 0x2020, 2 },
	{ 0xEB72, 0x0F80, 2 },
	{ 0xEB74, 0x0000, 2 },
	{ 0xEB76, 0xEA34, 2 },
	{ 0xEB78, 0x1404, 2 },
	{ 0xEB7A, 0x340E, 2 },
	{ 0xEB7C, 0x70CF, 2 },
	{ 0xEB7E, 0xFF00, 2 },
	{ 0xEB80, 0x31B0, 2 },
	{ 0xEB82, 0xD901, 2 },
	{ 0xEB84, 0xB038, 2 },
	{ 0xEB86, 0x70CF, 2 },
	{ 0xEB88, 0xFF00, 2 },
	{ 0xEB8A, 0x0078, 2 },
	{ 0xEB8C, 0x9012, 2 },
	{ 0xEB8E, 0x0817, 2 },
	{ 0xEB90, 0x035E, 2 },
	{ 0xEB92, 0x08E2, 2 },
	{ 0xEB94, 0x0720, 2 },
	{ 0xEB96, 0xD83C, 2 },
	{ 0xEB98, 0x0B2A, 2 },
	{ 0xEB9A, 0x0840, 2 },
	{ 0xEB9C, 0x216F, 2 },
	{ 0xEB9E, 0x003F, 2 },
	{ 0xEBA0, 0xF1FC, 2 },
	{ 0xEBA2, 0x08D2, 2 },
	{ 0xEBA4, 0x0720, 2 },
	{ 0xEBA6, 0xD81E, 2 },
	{ 0xEBA8, 0xC0D1, 2 },
	{ 0xEBAA, 0x7EE0, 2 },
	{ 0xEBAC, 0xC0F1, 2 },
	{ 0xEBAE, 0xE889, 2 },
	{ 0xEBB0, 0x70CF, 2 },
	{ 0xEBB2, 0xFF00, 2 },
	{ 0xEBB4, 0x0000, 2 },
	{ 0xEBB6, 0x900E, 2 },
	{ 0xEBB8, 0xB8E7, 2 },
	{ 0xEBBA, 0x0F78, 2 },
	{ 0xEBBC, 0xFFC1, 2 },
	{ 0xEBBE, 0xD800, 2 },
	{ 0xEBC0, 0xF1F4, 2 },

	{ 0x098E, 0x0000, 2 },	/* logic address access */
	{ 0x098A, 0x5F38, 2 },	/* physical address access */
	{ 0x0990, 0xFFFF, 2 },	/* mcu_variable_data0 */
	{ 0x0992, 0xEBAC, 2 },	/* mcu_variable_data1 */
	{ 0x001C, 0x0600, 2 },	/* MCU_BOOT_MODE */
};

static const as0260_reg output_mipi_yuyv_reg[] = {
	/* logic address access */
	{ LOGICAL_ADDRESS_ACCESS, 0x4A1C, 2 },

	/* MIPI, Raw 10-bit, MIPI clock & PIXCLK continuous */
	{ CAM_PORT_OUTPUT_CONTROL, 0x8043, 2 },
	/* cam_output_format: YUYV */
	{ CAM_OUTPUT_FORMAT, 0x4010, 2 },
	/* Swap order of Y and Cb/Cr values */
	{ OUTPUT_FORMAT_CONFIGURATION, 0x0002, 2 },
};

static int as0260_s_stream(struct v4l2_subdev *sd, int enable);

static int as0260_read_reg(struct v4l2_subdev *sd, u16 addr, u16 * val, u8 reg_size)
{
	int ret;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg[] = {
		{
		 .addr = client->addr,
		 .flags = 0,
		 .len = 2,
		 .buf = (u8 *) & addr,
		  },
		{
		 .addr = client->addr,
		 .flags = I2C_M_RD,
		 .len = reg_size,
		 .buf = (u8 *) val,
		  },
	};

	addr = swab16(addr);

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		dev_err(&client->dev, "Failed reading register 0x%04x, ret=%d!\n", addr, ret);
		return ret;
	}

	if (reg_size == 2)
		*val = swab16(*val);

	if (debug > 1)
		printk(KERN_INFO "as0260: i2c R 0x%04X <= 0x%04X\n", swab16(addr), *val);

	msleep_interruptible(1);
	return 0;
}

static int as0260_write_reg(struct v4l2_subdev *sd, u16 addr, u16 val, u8 reg_size)
{
	int ret = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg;
	struct {
		u16 addr;
		u16 val;
	} __packed buf;

	if (reg_size < 1 && reg_size > 2)
		return -1;

	msg.addr = client->addr;
	msg.flags = 0;		/* write */
	msg.len = 2 + reg_size;
	buf.addr = swab16(addr);

	buf.val = (reg_size == 1) ? (u8) val : swab16(val);
	msg.buf = (u8 *) & buf;

	if (debug > 1)
		printk(KERN_INFO "as0260: i2c W 0x%04X -> 0x%04X (len=%db)\n", addr, val, msg.len);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		dev_err(&client->dev, "Write reg error: reg=%x, val=%x\n", addr, val);
		return ret;
	}

	msleep_interruptible(1);
	return 0;
}

/* write register array */
static int as0260_write_reg_arr(struct v4l2_subdev *sd, const as0260_reg * regarr, int arrlen)
{
	int i;
	int ret = 0;

	for (i = 0; i < arrlen; i++) {
		ret = as0260_write_reg(sd, regarr[i].addr, regarr[i].val, regarr[i].mask);
		if (ret < 0)
			return ret;
	}

	return ret;
}

/* change config of the camera */
static int as0260_change_config(struct v4l2_subdev *sd)
{
	int ret = 0;

	ret += as0260_write_reg(sd, LOGICAL_ADDRESS_ACCESS, 0xDC00, 2);

	/*  28->SYS_STATE_ENTER_CONFIG_CHANGE */
	ret += as0260_write_reg(sd, SYSMGR_NEXT_STATE, 0x28, 1);

	/*  write command register to take effect */
	ret += as0260_write_reg(sd, COMMAND_REGISTER, 0x8002, 2);

	return ret;
}

/* configure PLL to setup MIPI as 768MHz */
static int as0260_pll_config(struct v4l2_subdev *sd, int mclk)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int fVCO = 768;		/* expect fVCO=768MHz */
	int divM;		/* CAM_SYSCTL_PLL_DIVIDER_M_N register bit[7:0] */
	int divN;		/* CAM_SYSCTL_PLL_DIVIDER_M_N register bit[13:8] */
	int ret = 0;
	int found = 0;

	/* fVCO=(2*M*mclk)/(N+1) */
	for (divM = 1; divM < 0xFF; divM++) {
		for (divN = 0; divN < 0x3F; divN++) {
			if ((2 * divM * mclk) / (divN + 1) == fVCO) {
				found = 1;
				goto out;
			}
		}
	}

out:
	if (!found)
		return -EINVAL;

	dev_info(&client->dev, "configure MIPI[%dMHz] from crystal[%dMHz]\n", fVCO, mclk);

	ret += as0260_write_reg(sd, LOGICAL_ADDRESS_ACCESS, 0xCA12, 2);
	ret += as0260_write_reg(sd, CAM_SYSCTL_PLL_ENABLE, 0x01, 1);
	ret += as0260_write_reg(sd, CAM_SYSCTL_PLL_DIV2_EN, 0x00, 1);

	ret += as0260_write_reg(sd, CAM_SYSCTL_PLL_DIVIDER_M_N, divN << 8 | divM, 2);

	/* fMIPI= fVCO / (P3 + 0) = 768MHz/(0+1) = 768MHz */
	ret += as0260_write_reg(sd, CAM_SYSCTL_PLL_DIVIDER_P, 0x0070, 2);

	ret += as0260_write_reg(sd, CAM_SYSCTL_PLL_DIVIDER_P4_P5_P6, 0x7F7D, 2);
	ret += as0260_write_reg(sd, CAM_SYSCTL_PLL_DIVIDER_P7, 0x1007, 2);

	/* configure pix clock */
	ret += as0260_write_reg(sd, CAM_SENSOR_CFG_PIXCLK_H, 0x0345, 2);
	ret += as0260_write_reg(sd, CAM_SENSOR_CFG_PIXCLK_L, 0x0DB6, 2);
	ret += as0260_write_reg(sd, CAM_SENSOR_CFG_ROW_SPEED, 0x0001, 2);

	return ret;
}

/*  
 *    fps=(2*cfg_pixclk)/(cfg_line_length_pck*cfg_frame_length_lines)
 * => cfg_line_length_pck = (2*0x3450DB6)/(fps*cfg_frame_length_lines)
 */
static int as0260_set_framerate(struct v4l2_subdev *sd, int fps)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u16 frame_length_lines = 0;
	u16 line_length_pck = 0;
	int ret = 0;

	/* read cfg_frame_length_lines value from register */
	ret = as0260_read_reg(sd, CAM_SENSOR_CFG_FRAME_LENGTH_LINES, &frame_length_lines, 2);
	if (ret || !frame_length_lines)
		return -EINVAL;

	line_length_pck = (2 * 0x3450DB6) / (fps * frame_length_lines);

	ret += as0260_write_reg(sd, CAM_SENSOR_CFG_LINE_LENGTH_PCK, line_length_pck, 2);
	ret += as0260_write_reg(sd, CAM_AET_MAX_FRAME_RATE, (fps * 256), 2);
	ret += as0260_write_reg(sd, CAM_AET_MIN_FRAME_RATE, (fps * 256), 2);

	as0260_dbg("Configure frame rate to %dfps\n", fps);

	return ret;
}

static int as0260_init(struct v4l2_subdev *sd)
{
	int ret = 0;
	struct as0260_sensor *sensor = to_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	as0260_dbg("%s(): initial AS0260 register\n", __func__);

	/* software reset action  */
	ret += as0260_write_reg(sd, RESET_AND_MISC_CONTROL, 0x0005, 2);
	ret += as0260_write_reg(sd, MCU_BOOT_MODE, 0x000C, 2);
	ret += as0260_write_reg(sd, RESET_AND_MISC_CONTROL, 0x0014, 2);

	/* enable upper 32K physical address access */
	ret += as0260_write_reg_arr(sd, upper32k_phy_reg, ARRAY_SIZE(upper32k_phy_reg));

	/* configure MIPI frequency to 768MHz */
	ret += as0260_pll_config(sd, sensor->mclk / 1000000);

	/* configure AS0260 work on 1920x1080@30fps as default  */
	ret += as0260_write_reg_arr(sd, regs_1920x1080, ARRAY_SIZE(regs_1920x1080));
	ret += as0260_set_framerate(sd, 30);

	/* set MIPI output format as YUYV */
	ret += as0260_write_reg_arr(sd, output_mipi_yuyv_reg, ARRAY_SIZE(output_mipi_yuyv_reg));

	/* let all the registers changes take effect */
	ret += as0260_change_config(sd);

	/* place the sensor in low power mode */
	as0260_s_stream(sd, 0);

	return ret;
}

static int as0260_reset(struct v4l2_subdev *sd, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	dev_info(&client->dev, "reset AS0260...\n");

	/* reset_soc_i2c Soft system reset */
	ret += as0260_write_reg(sd, RESET_AND_MISC_CONTROL, 0x0001, 2);

	/* reset_pseudo_pin Hard system reset */
	ret += as0260_write_reg(sd, RESET_AND_MISC_CONTROL, 0x0002, 2);

	return ret;
}

/* !
 * Description: V4L2 sensor interface handler for VIDIOC_G_PARM ioctl
 * @s: pointer to standard V4L2 sub device structure
 * @a: pointer to standard V4L2 VIDIOC_G_PARM ioctl structure
 *
 * Returns the sensor's video CAPTURE parameters.
 */
static int as0260_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct as0260_sensor *sensor = to_sensor(sd);
	struct v4l2_captureparm *cparm = &a->parm.capture;
	int ret = 0;

	switch (a->type) {
		/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		memset(a, 0, sizeof(*a));
		a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cparm->capability = sensor->streamcap.capability;
		cparm->timeperframe = sensor->streamcap.timeperframe;
		cparm->capturemode = sensor->streamcap.capturemode;
		ret = 0;
		break;

		/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		as0260_dbg("%s() unkown type[%d]\n", __func__, a->type);
		ret = -EINVAL;
		break;

	default:
		as0260_dbg("%s() unkown type[%d]\n", __func__, a->type);
		ret = -EINVAL;
	}

	return ret;
}

/* !
 * Description: V4L2 sensor interface handler for VIDIOC_S_PARM ioctl
 * @s: pointer to standard V4L2 sub device structure
 * @a: pointer to standard V4L2 VIDIOC_S_PARM ioctl structure
 *
 * Configures the sensor to use the input parameters, if possible.  If
 * not possible, reverts to the old parameters and returns the
 * appropriate error code.
 */
static int as0260_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct as0260_sensor *sensor = to_sensor(sd);
	struct v4l2_fract *timeperframe = &a->parm.capture.timeperframe;
	int ret = 0;
	int i, found = 0;
	u32 frame_rate;

	switch (a->type) {
		/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:

		as0260_dbg("%s(): denominator[%d]/numerator[%d] fps\n",
			   __func__, timeperframe->denominator, timeperframe->numerator);

		if ((!timeperframe->numerator) || (!timeperframe->denominator)) {
			timeperframe->denominator = 30;
			timeperframe->numerator = 1;
		}

		frame_rate = timeperframe->denominator / timeperframe->numerator;
		for (i = 0; i < ARRAY_SIZE(as0260_framerates); i++) {
			if (frame_rate == as0260_framerates[i]) {
				found = 1;
				break;
			}
		}

		if (!found) {
			dev_err(&client->dev, "don't support frame rate %dfps\n", frame_rate);
			return -EINVAL;
		}

		if (sensor->fmt->mode == AS0260_FMT_1920x1080) {
			ret += as0260_write_reg_arr(sd, regs_1920x1080, ARRAY_SIZE(regs_1920x1080));
        } else if (sensor->fmt->mode == AS0260_FMT_1280x720) {
			ret += as0260_write_reg_arr(sd, regs_1280x720, ARRAY_SIZE(regs_1280x720));
		}

		ret += as0260_set_framerate(sd, frame_rate);
		ret += as0260_change_config(sd);
		break;

		/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		dev_err(&client->dev, "%s() unkown type[%d]\n", __func__, a->type);
		ret = -EINVAL;
		break;

	default:
		dev_err(&client->dev, "%s() unkown type[%d]\n", __func__, a->type);
		ret = -EINVAL;
	}

	return ret;
}

/*!
 * Description: V4L2 sensor interface handler for VIDIOC_ENUM_FRAMESIZES ioctl
 * @s: pointer to standard V4L2 device structure
 * @fsize: standard V4L2 VIDIOC_ENUM_FRAMESIZES ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
static int as0260_enum_framesizes(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (fse->index >= AS0260_FMT_MAX)
		return -EINVAL;

	fse->max_width = as0260_formats[fse->index].width;
	fse->min_width = fse->max_width;

	fse->max_height = as0260_formats[fse->index].height;
	fse->min_height = fse->max_height;

	as0260_dbg("%s() fse[%d] %dx%d.\n", __func__, fse->index, fse->max_width, fse->max_height);

	return 0;
}

/*!
 * Description: V4L2 sensor interface handler for VIDIOC_ENUM_FRAMEINTERVALS ioctl
 * @s: pointer to standard V4L2 device structure
 * @fival: standard V4L2 VIDIOC_ENUM_FRAMEINTERVALS ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
static int as0260_enum_frameintervals(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
				      struct v4l2_subdev_frame_interval_enum *fie)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int i, j, count;

	if (fie->index >= AS0260_FMT_MAX)
		return -EINVAL;

	if (!fie->width || !fie->height || !fie->code) {
		dev_err(&client->dev, "%s() invalid arugments.\n", __func__);
		return -EINVAL;
	}

	as0260_dbg("enum_frameintervals[%d]: %dx%d\n", fie->index, fie->width, fie->height);

	fie->interval.numerator = 1;

	count = 0;
	for (i = 0; i < ARRAY_SIZE(as0260_framerates); i++) {
		for (j = 0; j < ARRAY_SIZE(as0260_formats); j++) {
			if (fie->width == as0260_formats[j].width
			    && fie->height == as0260_formats[j].height)
				count++;

			if (fie->index == (count - 1)) {
				fie->interval.denominator = as0260_framerates[i];
				return 0;
			}
		}
	}

	return -EINVAL;
}

static int as0260_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;
	u16 regval;

	as0260_dbg("s_stream: %d\n", enable);

	as0260_read_reg(sd, RESET_REG, &regval, 2);
	if (enable) {
		regval |= (1 << 2);
		ret += as0260_write_reg(sd, RESET_REG, regval, 2);
	} else {
		regval &= ~(1 << 2);
		ret += as0260_write_reg(sd, RESET_REG, regval, 2);
	}

	return ret;
}

/* !
 * Description: V4L2 sensor interface handler for VIDIOC_ENUM_FMT ioctl
 */
static int as0260_enum_mbus_code(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (code->pad || code->index >= ARRAY_SIZE(as0260_colour_fmts)) {
		return -EINVAL;
	}

	as0260_dbg("%s(): index=%d\n", __func__, code->index);
	code->code = as0260_colour_fmts[code->index].code;

	return 0;
}

/* !
 *  Description: V4L2 sensor interface handler for VIDIOC_S_FMT ioctl
 */
static int as0260_set_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct as0260_sensor *sensor = to_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int i;
	int found = 0;

	as0260_dbg("%s() format: %dx%d, code=0x%04X, field=%d, colorspace=%d\n",
		   __func__, fmt->width, fmt->height, fmt->code, fmt->field, fmt->colorspace);

	for (i = 0; i < AS0260_FMT_MAX; i++) {
		if (as0260_formats[i].width == fmt->width
		    && as0260_formats[i].height == fmt->height) {
			sensor->fmt = &as0260_formats[i];
			found = 1;
			break;
		}
	}

	if (!found) {
		dev_err(&client->dev, "%s() unkown format: %dx%d!\n",
			__func__, fmt->width, fmt->height);
		return -EINVAL;
	}

	return 0;
}

/* !
 *  Description: V4L2 sensor interface handler for VIDIOC_G_FMT ioctl
 */
static int as0260_get_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct as0260_sensor *sensor = to_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!sensor->fmt) {
		return -EINVAL;
	}

	fmt->width = sensor->fmt->width;
	fmt->height = sensor->fmt->height;
	fmt->code = sensor->fmt->mbus_code;
	fmt->field = sensor->fmt->field;
	fmt->colorspace = sensor->fmt->colorspace;

	as0260_dbg("%s() format: %dx%d, code=0x%04X, field=%d, colorspace=%d\n",
		   __func__, fmt->width, fmt->height, fmt->code, fmt->field, fmt->colorspace);

	return 0;
}

static const struct v4l2_subdev_core_ops as0260_core_ops = {
	.reset = as0260_reset,
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static struct v4l2_subdev_video_ops as0260_video_ops = {
	.g_parm = as0260_g_parm,
	.s_parm = as0260_s_parm,
	.s_stream = as0260_s_stream,
};

static const struct v4l2_subdev_pad_ops as0260_pad_ops = {
	.enum_mbus_code = as0260_enum_mbus_code,
	.enum_frame_size = as0260_enum_framesizes,
	.enum_frame_interval = as0260_enum_frameintervals,
	.set_fmt = as0260_set_fmt,
	.get_fmt = as0260_get_fmt,
};

static const struct v4l2_subdev_ops as0260_ops = {
	.core = &as0260_core_ops,
	.video = &as0260_video_ops,
	.pad = &as0260_pad_ops,
};

static int as0260_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct as0260_sensor *sensor;
	struct v4l2_subdev *sd;
	struct device *dev = &client->dev;
	u16 chip_id;
	int ret;

	dev_warn(dev, "start probe camera sensor AS0260 driver.\n");

	sensor = kzalloc(sizeof(*sensor), GFP_KERNEL);
	if (!sensor) {
		dev_err(dev, "Failed to allocate memory for as0260 driver!\n");
		return -ENOMEM;
	}
	sd = &sensor->sd;
	v4l2_set_subdevdata(sd, client);

	/* request reset pin from dts and set to high */
	sensor->rst_gpio = of_get_named_gpio(dev->of_node, "rst-gpios", 0);
	if (!gpio_is_valid(sensor->rst_gpio)) {
		dev_err(dev, "No sensor reset pin available");
	} else {
		ret = devm_gpio_request_one(dev, sensor->rst_gpio,
					    GPIOF_OUT_INIT_HIGH, "as0260_mipi_reset");
		if (ret < 0) {
			dev_warn(dev, "Failed to set reset pin\n");
			ret = -EINVAL;
			goto failed;
		}
	}

	/* request AS0260 Rapi adapter crystal frequency, should be 27MHz */
	ret = of_property_read_u32(dev->of_node, "mclk", &(sensor->mclk));
	if (ret) {
		dev_err(dev, "mclk missing or invalid\n");
		ret = -EINVAL;
		goto failed;
	}

	/* read and check chip ID  */
	ret = as0260_read_reg(sd, CHIP_ID_REG, &chip_id, 2);
	if (ret || AS0260_CHIPID != chip_id) {
		ret = -ENODEV;
		goto failed;
	}
	dev_info(dev, "detected AS0260 chipID [0x%04X]\n", chip_id);

	sensor->fmt = &as0260_formats[0];
	sensor->streamcap.capability = V4L2_MODE_HIGHQUALITY | V4L2_CAP_TIMEPERFRAME;
	sensor->streamcap.capturemode = 0;
	sensor->streamcap.timeperframe.denominator = 30;	/* 30 FPS */
	sensor->streamcap.timeperframe.numerator = 1;

	ret = as0260_init(sd);
	if (ret) {
		dev_err(dev, "AS0260 driver failed to init camera\n");
		goto failed;
	}

	/* Register with V4L2 layer as slave device */
	v4l2_i2c_subdev_init(sd, client, &as0260_ops);

	ret = v4l2_async_register_subdev(sd);
	if (ret < 0) {
		dev_err(&client->dev, "AS0260 driver Async register failed, ret=%d\n", ret);
		goto failed;
	}

	dev_info(dev, "camera sensor AS0260 driver probe ok.\n");
	return 0;

failed:
	kfree(sensor);
	return ret;
}

static int as0260_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct as0260_sensor *sensor = to_sensor(sd);

	dev_warn(&client->dev, "AS0260 driver remove\n");

	v4l2_device_unregister_subdev(sd);
	kfree(sensor);
	return 0;
}

static const struct i2c_device_id as0260_id[] = {
	{ "as0260", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, as0260_id);

static const struct of_device_id as0260_dt_ids[] = {
	{.compatible = "onsemi,as0260" },
	{ /* sentinel */  }
};

MODULE_DEVICE_TABLE(of, as0260_dt_ids);

static struct i2c_driver as0260_driver = {
	.driver = {
		   .name = "as0260",
		   .owner = THIS_MODULE,
		   .of_match_table = as0260_dt_ids,
		    },
	.probe = as0260_probe,
	.remove = as0260_remove,
	.id_table = as0260_id
};

module_i2c_driver(as0260_driver);

MODULE_DESCRIPTION("OnSemi AS0260 SoC Camera Driver");
MODULE_AUTHOR("Wenxue <wenxue.guo@avnet.com>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
MODULE_ALIAS("CSI");
