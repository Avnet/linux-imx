/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2017-2018, Bootlin
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>
#include <video/videomode.h>

#define USE_DISPLAY_TIMINGS

struct ili9881c_panel {
	struct drm_panel	panel;
	struct mipi_dsi_device	*dsi;

	struct backlight_device *backlight;
	struct gpio_desc	*enable_gpio;
	struct gpio_desc	*reset_gpio;
};

enum ili9881c_op {
	ILI9881C_SWITCH_PAGE,
	ILI9881C_COMMAND,
};

struct ili9881c_instr {
	enum ili9881c_op	op;

	union arg {
		struct cmd {
			u8	cmd;
			u8	data;
		} cmd;
		u8	page;
	} arg;
};

#define ILI9881C_SWITCH_PAGE_INSTR(_page)	\
	{					\
		.op = ILI9881C_SWITCH_PAGE,	\
		.arg = {			\
			.page = (_page),	\
		},				\
	}

#define ILI9881C_COMMAND_INSTR(_cmd, _data)		\
	{						\
		.op = ILI9881C_COMMAND,		\
		.arg = {				\
			.cmd = {			\
				.cmd = (_cmd),		\
				.data = (_data),	\
			},				\
		},					\
	}


static const struct ili9881c_instr ili9881c_init[] = {
	ILI9881C_SWITCH_PAGE_INSTR(3),
	ILI9881C_COMMAND_INSTR(0x01, 0x00),
	ILI9881C_COMMAND_INSTR(0x02, 0x00),
	ILI9881C_COMMAND_INSTR(0x03, 0x55),
	ILI9881C_COMMAND_INSTR(0x04, 0x13),
	ILI9881C_COMMAND_INSTR(0x05, 0x00),
	ILI9881C_COMMAND_INSTR(0x06, 0x06),
	ILI9881C_COMMAND_INSTR(0x07, 0x01),
	ILI9881C_COMMAND_INSTR(0x08, 0x00),
	ILI9881C_COMMAND_INSTR(0x09, 0x01),
	ILI9881C_COMMAND_INSTR(0x0a, 0x01),
	ILI9881C_COMMAND_INSTR(0x0b, 0x00),
	ILI9881C_COMMAND_INSTR(0x0c, 0x00),
	ILI9881C_COMMAND_INSTR(0x0d, 0x00),
	ILI9881C_COMMAND_INSTR(0x0e, 0x00),
	ILI9881C_COMMAND_INSTR(0x0f, 0x18),
	ILI9881C_COMMAND_INSTR(0x10, 0x18),
	ILI9881C_COMMAND_INSTR(0x11, 0x00),
	ILI9881C_COMMAND_INSTR(0x12, 0x00),
	ILI9881C_COMMAND_INSTR(0x13, 0x00),
	ILI9881C_COMMAND_INSTR(0x14, 0x00),
	ILI9881C_COMMAND_INSTR(0x15, 0x00),
	ILI9881C_COMMAND_INSTR(0x16, 0x00),
	ILI9881C_COMMAND_INSTR(0x17, 0x00),
	ILI9881C_COMMAND_INSTR(0x18, 0x00),
	ILI9881C_COMMAND_INSTR(0x19, 0x00),
	ILI9881C_COMMAND_INSTR(0x1a, 0x00),
	ILI9881C_COMMAND_INSTR(0x1b, 0x00),
	ILI9881C_COMMAND_INSTR(0x1c, 0x00),
	ILI9881C_COMMAND_INSTR(0x1d, 0x00),
	ILI9881C_COMMAND_INSTR(0x1e, 0x44),
	ILI9881C_COMMAND_INSTR(0x1f, 0x80),
	ILI9881C_COMMAND_INSTR(0x20, 0x02),
	ILI9881C_COMMAND_INSTR(0x21, 0x03),
	ILI9881C_COMMAND_INSTR(0x22, 0x00),
	ILI9881C_COMMAND_INSTR(0x23, 0x00),
	ILI9881C_COMMAND_INSTR(0x24, 0x00),
	ILI9881C_COMMAND_INSTR(0x25, 0x00),
	ILI9881C_COMMAND_INSTR(0x26, 0x00),
	ILI9881C_COMMAND_INSTR(0x27, 0x00),
	ILI9881C_COMMAND_INSTR(0x28, 0x33),
	ILI9881C_COMMAND_INSTR(0x29, 0x03),
	ILI9881C_COMMAND_INSTR(0x2a, 0x00),
	ILI9881C_COMMAND_INSTR(0x2b, 0x00),
	ILI9881C_COMMAND_INSTR(0x2c, 0x00),
	ILI9881C_COMMAND_INSTR(0x2d, 0x00),
	ILI9881C_COMMAND_INSTR(0x2e, 0x00),
	ILI9881C_COMMAND_INSTR(0x2f, 0x00),
	ILI9881C_COMMAND_INSTR(0x30, 0x00),
	ILI9881C_COMMAND_INSTR(0x31, 0x00),
	ILI9881C_COMMAND_INSTR(0x32, 0x00),
	ILI9881C_COMMAND_INSTR(0x33, 0x00),
	ILI9881C_COMMAND_INSTR(0x34, 0x04),
	ILI9881C_COMMAND_INSTR(0x35, 0x00),
	ILI9881C_COMMAND_INSTR(0x36, 0x00),
	ILI9881C_COMMAND_INSTR(0x37, 0x00),
	ILI9881C_COMMAND_INSTR(0x38, 0x01),
	ILI9881C_COMMAND_INSTR(0x39, 0x00),
	ILI9881C_COMMAND_INSTR(0x3a, 0x00),
	ILI9881C_COMMAND_INSTR(0x3b, 0x00),
	ILI9881C_COMMAND_INSTR(0x3c, 0x00),
	ILI9881C_COMMAND_INSTR(0x3d, 0x00),
	ILI9881C_COMMAND_INSTR(0x3e, 0x00),
	ILI9881C_COMMAND_INSTR(0x3f, 0x00),
	ILI9881C_COMMAND_INSTR(0x40, 0x00),
	ILI9881C_COMMAND_INSTR(0x41, 0x00),
	ILI9881C_COMMAND_INSTR(0x42, 0x00),
	ILI9881C_COMMAND_INSTR(0x43, 0x00),
	ILI9881C_COMMAND_INSTR(0x44, 0x00),
	ILI9881C_COMMAND_INSTR(0x50, 0x01),
	ILI9881C_COMMAND_INSTR(0x51, 0x23),
	ILI9881C_COMMAND_INSTR(0x52, 0x45),
	ILI9881C_COMMAND_INSTR(0x53, 0x67),
	ILI9881C_COMMAND_INSTR(0x54, 0x89),
	ILI9881C_COMMAND_INSTR(0x55, 0xab),
	ILI9881C_COMMAND_INSTR(0x56, 0x01),
	ILI9881C_COMMAND_INSTR(0x57, 0x23),
	ILI9881C_COMMAND_INSTR(0x58, 0x45),
	ILI9881C_COMMAND_INSTR(0x59, 0x67),
	ILI9881C_COMMAND_INSTR(0x5a, 0x89),
	ILI9881C_COMMAND_INSTR(0x5b, 0xab),
	ILI9881C_COMMAND_INSTR(0x5c, 0xcd),
	ILI9881C_COMMAND_INSTR(0x5d, 0xef),
	ILI9881C_COMMAND_INSTR(0x5e, 0x11),
	ILI9881C_COMMAND_INSTR(0x5f, 0x14),
	ILI9881C_COMMAND_INSTR(0x60, 0x15),
	ILI9881C_COMMAND_INSTR(0x61, 0x0f),
	ILI9881C_COMMAND_INSTR(0x62, 0x0d),
	ILI9881C_COMMAND_INSTR(0x63, 0x0e),
	ILI9881C_COMMAND_INSTR(0x64, 0x0c),
	ILI9881C_COMMAND_INSTR(0x65, 0x06),
	ILI9881C_COMMAND_INSTR(0x66, 0x02),
	ILI9881C_COMMAND_INSTR(0x67, 0x02),
	ILI9881C_COMMAND_INSTR(0x68, 0x02),
	ILI9881C_COMMAND_INSTR(0x69, 0x02),
	ILI9881C_COMMAND_INSTR(0x6a, 0x02),
	ILI9881C_COMMAND_INSTR(0x6b, 0x02),
	ILI9881C_COMMAND_INSTR(0x6c, 0x02),
	ILI9881C_COMMAND_INSTR(0x6d, 0x02),
	ILI9881C_COMMAND_INSTR(0x6e, 0x02),
	ILI9881C_COMMAND_INSTR(0x6f, 0x02),
	ILI9881C_COMMAND_INSTR(0x70, 0x02),
	ILI9881C_COMMAND_INSTR(0x71, 0x00),
	ILI9881C_COMMAND_INSTR(0x72, 0x01),
	ILI9881C_COMMAND_INSTR(0x73, 0x08),
	ILI9881C_COMMAND_INSTR(0x74, 0x02),
	ILI9881C_COMMAND_INSTR(0x75, 0x14),
	ILI9881C_COMMAND_INSTR(0x76, 0x15),
	ILI9881C_COMMAND_INSTR(0x77, 0x0f),
	ILI9881C_COMMAND_INSTR(0x78, 0x0d),
	ILI9881C_COMMAND_INSTR(0x79, 0x0e),
	ILI9881C_COMMAND_INSTR(0x7a, 0x0c),
	ILI9881C_COMMAND_INSTR(0x7b, 0x08),
	ILI9881C_COMMAND_INSTR(0x7c, 0x02),
	ILI9881C_COMMAND_INSTR(0x7d, 0x02),
	ILI9881C_COMMAND_INSTR(0x7e, 0x02),
	ILI9881C_COMMAND_INSTR(0x7f, 0x02),
	ILI9881C_COMMAND_INSTR(0x80, 0x02),
	ILI9881C_COMMAND_INSTR(0x81, 0x02),
	ILI9881C_COMMAND_INSTR(0x82, 0x02),
	ILI9881C_COMMAND_INSTR(0x83, 0x02),
	ILI9881C_COMMAND_INSTR(0x84, 0x02),
	ILI9881C_COMMAND_INSTR(0x85, 0x02),
	ILI9881C_COMMAND_INSTR(0x86, 0x02),
	ILI9881C_COMMAND_INSTR(0x87, 0x00),
	ILI9881C_COMMAND_INSTR(0x88, 0x01),
	ILI9881C_COMMAND_INSTR(0x89, 0x06),
	ILI9881C_COMMAND_INSTR(0x8A, 0x02),
	ILI9881C_SWITCH_PAGE_INSTR(4),
	ILI9881C_COMMAND_INSTR(0x6C, 0x15),
	ILI9881C_COMMAND_INSTR(0x6E, 0x2a),
	ILI9881C_COMMAND_INSTR(0x6F, 0x33),
	ILI9881C_COMMAND_INSTR(0x3A, 0x24),
	ILI9881C_COMMAND_INSTR(0x8D, 0x14),
	ILI9881C_COMMAND_INSTR(0x87, 0xBA),
	ILI9881C_COMMAND_INSTR(0x26, 0x76),
	ILI9881C_COMMAND_INSTR(0xB2, 0xD1),
	ILI9881C_COMMAND_INSTR(0xB5, 0xD7),
	ILI9881C_COMMAND_INSTR(0x35, 0x1f),
	ILI9881C_SWITCH_PAGE_INSTR(1),
	ILI9881C_COMMAND_INSTR(0x22, 0x0A),
	ILI9881C_COMMAND_INSTR(0x53, 0x72),
	ILI9881C_COMMAND_INSTR(0x55, 0x77),
	ILI9881C_COMMAND_INSTR(0x50, 0xa6),
	ILI9881C_COMMAND_INSTR(0x51, 0xa6),
	ILI9881C_COMMAND_INSTR(0x31, 0x00),
	ILI9881C_COMMAND_INSTR(0x60, 0x20),
	ILI9881C_COMMAND_INSTR(0xA0, 0x08),
	ILI9881C_COMMAND_INSTR(0xA1, 0x1a),
	ILI9881C_COMMAND_INSTR(0xA2, 0x2a),
	ILI9881C_COMMAND_INSTR(0xA3, 0x14),
	ILI9881C_COMMAND_INSTR(0xA4, 0x17),
	ILI9881C_COMMAND_INSTR(0xA5, 0x2b),
	ILI9881C_COMMAND_INSTR(0xA6, 0x1d),
	ILI9881C_COMMAND_INSTR(0xA7, 0x20),
	ILI9881C_COMMAND_INSTR(0xA8, 0x9d),
	ILI9881C_COMMAND_INSTR(0xA9, 0x1C),
	ILI9881C_COMMAND_INSTR(0xAA, 0x29),
	ILI9881C_COMMAND_INSTR(0xAB, 0x8f),
	ILI9881C_COMMAND_INSTR(0xAC, 0x20),
	ILI9881C_COMMAND_INSTR(0xAD, 0x1f),
	ILI9881C_COMMAND_INSTR(0xAE, 0x4f),
	ILI9881C_COMMAND_INSTR(0xAF, 0x23),
	ILI9881C_COMMAND_INSTR(0xB0, 0x29),
	ILI9881C_COMMAND_INSTR(0xB1, 0x56),
	ILI9881C_COMMAND_INSTR(0xB2, 0x66),
	ILI9881C_COMMAND_INSTR(0xB3, 0x39),
	ILI9881C_COMMAND_INSTR(0xC0, 0x08),
	ILI9881C_COMMAND_INSTR(0xC1, 0x1a),
	ILI9881C_COMMAND_INSTR(0xC2, 0x2a),
	ILI9881C_COMMAND_INSTR(0xC3, 0x15),
	ILI9881C_COMMAND_INSTR(0xC4, 0x17),
	ILI9881C_COMMAND_INSTR(0xC5, 0x2b),
	ILI9881C_COMMAND_INSTR(0xC6, 0x1d),
	ILI9881C_COMMAND_INSTR(0xC7, 0x20),
	ILI9881C_COMMAND_INSTR(0xC8, 0x9d),
	ILI9881C_COMMAND_INSTR(0xC9, 0x1d),
	ILI9881C_COMMAND_INSTR(0xCA, 0x29),
	ILI9881C_COMMAND_INSTR(0xCB, 0x8f),
	ILI9881C_COMMAND_INSTR(0xCC, 0x20),
	ILI9881C_COMMAND_INSTR(0xCD, 0x1f),
	ILI9881C_COMMAND_INSTR(0xCE, 0x4f),
	ILI9881C_COMMAND_INSTR(0xCF, 0x24),
	ILI9881C_COMMAND_INSTR(0xD0, 0x29),
	ILI9881C_COMMAND_INSTR(0xD1, 0x56),
	ILI9881C_COMMAND_INSTR(0xD2, 0x66),
	ILI9881C_COMMAND_INSTR(0xD3, 0x39),
#if 0
/* BIST mode (Built-in Self-test Pattern)*/
	ILI9881C_SWITCH_PAGE_INSTR(4),
	ILI9881C_COMMAND_INSTR(0x2d, 0x08),
	ILI9881C_COMMAND_INSTR(0x2f, 0x11),
#endif
};

static inline struct ili9881c_panel *panel_to_ili9881c(struct drm_panel *panel)
{
	return container_of(panel, struct ili9881c_panel, panel);
}

static int ili9881c_switch_page(struct ili9881c_panel *tftcp, u8 page)
{
	u8 buf[4] = { 0xff, 0x98, 0x81, page };
	int ret;

	ret = mipi_dsi_dcs_write_buffer(tftcp->dsi, buf, sizeof(buf));
	if (ret < 0) {
		dev_err(&tftcp->dsi->dev, "Failed to switch_page[%d] (%d)\n", page, ret);
		return ret;
	}

	return 0;
}

static int ili9881c_send_cmd_data(struct ili9881c_panel *tftcp, u8 cmd, u8 data)
{
	u8 buf[2] = { cmd, data };
	int ret;

	ret = mipi_dsi_dcs_write_buffer(tftcp->dsi, buf, sizeof(buf));
	if (ret < 0) {
		dev_err(&tftcp->dsi->dev, "Failed to send_cmd_data[%02x,%02X] (%d)\n", cmd, data, ret);
		return ret;
	}

	return 0;
}

static int ili9881c_read_cmd_data(struct ili9881c_panel *tftcp, u8 cmd)
{
	u8 buf = 0;
	int ret;

	ret = mipi_dsi_dcs_read(tftcp->dsi, cmd, &buf, sizeof(buf));
	if (ret < 0) {
		dev_err(&tftcp->dsi->dev, "Failed to get ID (%d)\n", ret);
		return ret;
	}

	return buf;
}

static void ili9881c_getID(struct ili9881c_panel *tftcp)
{
	u8 id[3];

	ili9881c_switch_page(tftcp, 1);
	id[0] = ili9881c_read_cmd_data(tftcp, 0x00);
	id[1] = ili9881c_read_cmd_data(tftcp, 0x01);
	id[2] = ili9881c_read_cmd_data(tftcp, 0x02);

	dev_info(&tftcp->dsi->dev, "ID: 0x%02X 0x%02X 0x%02X \n", id[0], id[1], id[2]);
}

static void ili9881c_reset(struct ili9881c_panel *tftcp)
{
	/* Reset 5ms */
	if (tftcp->reset_gpio) {
		dev_dbg(&tftcp->dsi->dev,"reset the chip\n");
		gpiod_set_value(tftcp->reset_gpio, 0);
		usleep_range(5000, 10000);
		gpiod_set_value(tftcp->reset_gpio, 1);
		usleep_range(20000, 25000);
	}
}

static int ili9881c_prepare(struct drm_panel *panel)
{
	struct ili9881c_panel *tftcp = panel_to_ili9881c(panel);

	dev_dbg(&tftcp->dsi->dev,"%s\n",__func__);

    ili9881c_reset(tftcp);
	tftcp->dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	ili9881c_getID(tftcp);

	return 0;
}

static int ili9881c_enable(struct drm_panel *panel)
{
	struct ili9881c_panel *tftcp = panel_to_ili9881c(panel);
	int ret;
	unsigned int i;

	tftcp->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	for (i = 0; i < ARRAY_SIZE(ili9881c_init); i++) {
		const struct ili9881c_instr *instr = &ili9881c_init[i];

		if (instr->op == ILI9881C_SWITCH_PAGE)
			ret = ili9881c_switch_page(tftcp, instr->arg.page);
		else if (instr->op == ILI9881C_COMMAND)
			ret = ili9881c_send_cmd_data(tftcp, instr->arg.cmd.cmd,
						      instr->arg.cmd.data);
		if (ret)
			return ret;
	}

	ret = ili9881c_switch_page(tftcp, 0);
	if (ret)
		return ret;

    /* Set tear ON */
	ret = mipi_dsi_dcs_set_tear_on(tftcp->dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		dev_err(&tftcp->dsi->dev, "Failed to set tear ON (%d)\n", ret);
		return ret;
	}
	usleep_range(100000, 110000);

	/* Exit sleep mode */
	ret = mipi_dsi_dcs_exit_sleep_mode(tftcp->dsi);
	if (ret < 0) {
		dev_err(&tftcp->dsi->dev, "Failed to exit sleep mode (%d)\n", ret);
		return ret;
	}

	usleep_range(120000, 130000);

	ret = mipi_dsi_dcs_set_display_on(tftcp->dsi);
	if (ret < 0) {
		dev_err(&tftcp->dsi->dev, "Failed to set display ON (%d)\n", ret);
		return ret;
	}

	backlight_enable(tftcp->backlight);
	dev_dbg(&tftcp->dsi->dev,"%s\n",__func__);

	return 0;
}

static int ili9881c_disable(struct drm_panel *panel)
{
	struct ili9881c_panel *tftcp = panel_to_ili9881c(panel);

	tftcp->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	backlight_disable(tftcp->backlight);
	return mipi_dsi_dcs_set_display_off(tftcp->dsi);
}

static int ili9881c_unprepare(struct drm_panel *panel)
{
	struct ili9881c_panel *tftcp = panel_to_ili9881c(panel);

	dev_dbg(&tftcp->dsi->dev,"%s\n",__func__);
	mipi_dsi_dcs_enter_sleep_mode(tftcp->dsi);

	if (tftcp->enable_gpio != NULL)
		gpiod_set_value(tftcp->enable_gpio, 0);
	if (tftcp->reset_gpio != NULL)
		gpiod_set_value(tftcp->reset_gpio, 0);

	return 0;
}

#ifndef USE_DISPLAY_TIMINGS
static const struct drm_display_mode default_mode = {
	.clock		= 67000,
	.vrefresh	= 60,

	.hdisplay	= 720,
	.hsync_start = 720 + 120,
	.hsync_end	= 720 + 120 + 40,
	.htotal		= 720 + 120 + 40 + 20,

	.vdisplay	= 1280,
	.vsync_start = 1280 + 10,
	.vsync_end	= 1280 + 10 + 2,
	.vtotal		= 1280 + 10 + 2 + 15,
};

#else
/* MIPI two lanes, < 850Mbps, RGB88, 24UI/pixel
frame rate = 52.5Hz [2 data lanes: 50~60Hz]
pclk=800M * 2lane / 24bpp =66.67M */
static const struct display_timing ph720128t003_timing = {
    .pixelclock = { 64000000, 67000000, 7100000 },
	.hactive = { 720, 720, 720 },
	.hfront_porch = { 80, 120, 120 },
	.hback_porch = { 10, 20, 60 },
	.hsync_len = { 33, 40, 50 },
	.vactive = { 1280, 1280, 1280 },
	.vfront_porch = { 5, 10, 20 },
	.vback_porch = { 10, 15, 30 },
	.vsync_len = { 2, 2, 2 },
	.flags = DISPLAY_FLAGS_VSYNC_LOW |
		 DISPLAY_FLAGS_HSYNC_LOW |
		 DISPLAY_FLAGS_DE_LOW |
		 DISPLAY_FLAGS_PIXDATA_NEGEDGE,
};
#endif

static int ili9881c_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct ili9881c_panel *tftcp = panel_to_ili9881c(panel);
	struct drm_display_mode *mode;
#ifdef USE_DISPLAY_TIMINGS
	struct videomode vm;
#endif

#ifndef USE_DISPLAY_TIMINGS
  dev_dbg(&tftcp->dsi->dev,"%s get drm_display_mode\n",__func__);
	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		dev_err(&tftcp->dsi->dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay,
			default_mode.vdisplay,
			default_mode.vrefresh);
		return -ENOMEM;
	}
		drm_mode_set_name(mode);

#else
  dev_dbg(&tftcp->dsi->dev,"%s get display_timing\n",__func__);
	videomode_from_timing(&ph720128t003_timing, &vm);

	mode = drm_mode_create(connector->dev);
	if (!mode) {
		dev_err(&tftcp->dsi->dev, "Failed to create display mode!\n");
		return 0;
	}

	drm_display_mode_from_videomode(&vm, mode);
#endif

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	panel->connector->display_info.width_mm = 153;
	panel->connector->display_info.height_mm = 90;

	return 1;
}


static int dsi_dcs_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	int ret;
	u16 brightness = bl->props.brightness;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	bl->props.brightness = brightness;
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness & 0xff;
}

static int dsi_dcs_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, bl->props.brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static const struct backlight_ops dsi_bl_ops = {
	.update_status = dsi_dcs_bl_update_status,
	.get_brightness = dsi_dcs_bl_get_brightness,
};

static struct backlight_device *
drm_panel_create_dsi_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct backlight_properties props;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.brightness = 255;
	props.max_brightness = 255;

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &dsi_bl_ops, &props);
}

static const struct drm_panel_funcs ili9881c_funcs = {
	.prepare	= ili9881c_prepare,
	.unprepare	= ili9881c_unprepare,
	.enable		= ili9881c_enable,
	.disable	= ili9881c_disable,
	.get_modes	= ili9881c_get_modes,
};

static int ili9881c_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct ili9881c_panel *tftcp;
	int ret;

	tftcp = devm_kzalloc(&dsi->dev, sizeof(*tftcp), GFP_KERNEL);
	if (!tftcp)
		return -ENOMEM;
	mipi_dsi_set_drvdata(dsi, tftcp);
	tftcp->dsi = dsi;

	tftcp->enable_gpio = devm_gpiod_get(&dsi->dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(tftcp->enable_gpio)) {
		tftcp->enable_gpio = NULL;
		dev_dbg(&dsi->dev, "Couldn't get our power GPIO\n");
		return PTR_ERR(tftcp->enable_gpio);
	} else {
		gpiod_set_value(tftcp->enable_gpio, 1);
	}

	tftcp->reset_gpio = devm_gpiod_get_optional(&dsi->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(tftcp->reset_gpio))
		dev_dbg(&dsi->dev, "Couldn't get our reset GPIO\n");

	ret = of_property_read_u32(dsi->dev.of_node, "dsi-lanes", &dsi->lanes);
	if (ret < 0) {
		dev_dbg(&dsi->dev, "Failed to get dsi-lanes property, use default setting\n");
		dsi->lanes = 4;
	}

	tftcp->backlight = drm_panel_create_dsi_backlight(tftcp->dsi);
	if (IS_ERR(tftcp->backlight)) {
		ret = PTR_ERR(tftcp->backlight);
		dev_err(&dsi->dev, "failed to register backlight %d\n", ret);
		return ret;
	}

	drm_panel_init(&tftcp->panel);
	tftcp->panel.dev = &dsi->dev;
	tftcp->panel.funcs = &ili9881c_funcs;

	ret = drm_panel_add(&tftcp->panel);
	if (ret < 0)
		return ret;

	dsi->mode_flags =  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS;
	/* non-burst mode with sync pulse */
	dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
	dsi->format = MIPI_DSI_FMT_RGB888;

	return mipi_dsi_attach(dsi);
}

static int ili9881c_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct ili9881c_panel *tftcp = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&tftcp->panel);

	return 0;
}

static const struct of_device_id ili9881c_of_match[] = {
	{ .compatible = "ilitek,ili9881c" },
	{ }
};
MODULE_DEVICE_TABLE(of, ili9881c_of_match);

static struct mipi_dsi_driver ili9881c_dsi_driver = {
	.probe		= ili9881c_dsi_probe,
	.remove		= ili9881c_dsi_remove,
	.driver = {
		.name		= "panel-ili9881c-dsi",
		.of_match_table	= ili9881c_of_match,
	},
};
module_mipi_dsi_driver(ili9881c_dsi_driver);

MODULE_AUTHOR("NXP Semiconductor");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Ilitek ILI9881C Controller Driver");
MODULE_LICENSE("GPL v2");
