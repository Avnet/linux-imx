/*
 * Copyright 2016-2019 Embest Tech
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

#include <drm/drmP.h>
#include <drm/drm_of.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_mipi_dsi.h>


struct lt2911 {
	struct drm_bridge bridge;
	struct drm_connector connector;

	struct drm_display_mode mode;
	struct videomode vm;
	struct device *dev;
	struct mipi_dsi_device *dsi;
	struct device_node *host_node;

	u8 num_dsi_lanes;
	u8 channel_id;
	struct regmap *regmap;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
};

static int lt2911_attach_dsi(struct lt2911 *lt);

static inline struct lt2911 *bridge_to_lt2911(struct drm_bridge *b)
{
	return container_of(b, struct lt2911, bridge);
}

static inline struct lt2911 *connector_to_lt2911(struct drm_connector *c)
{
	return container_of(c, struct lt2911, connector);
}

static void lt2911_get_id(struct lt2911 *lt)
{
	unsigned int id_val[3];

	regmap_write(lt->regmap, 0xff, 0x81);//register bank
	regmap_read(lt->regmap, 0x00, &id_val[0]);
	regmap_read(lt->regmap, 0x01, &id_val[1]);
	regmap_read(lt->regmap, 0x02, &id_val[2]);

	dev_info(lt->dev, "LT2911 Chip ID: %02x-%02x-%02x\n",
		 id_val[0], id_val[1], id_val[2]);
}

static void lt2911_set_timings(struct lt2911 *lt)
{
	struct videomode *vmode = &lt->vm;
	const struct drm_display_mode *dmode = &lt->mode;
	u32 hactive, hfp, hsync, hbp, vactive, vfp, vsync, vbp, htotal, vtotal;

	hactive = vmode->hactive;
	hfp = vmode->hfront_porch;
	hsync = vmode->hsync_len;
	hbp = vmode->hback_porch;
	vactive = vmode->vactive;
	vfp = vmode->vfront_porch;
	vsync = vmode->vsync_len;
	vbp = vmode->vback_porch;
	htotal = dmode->htotal;
	vtotal = dmode->vtotal;

	dev_dbg(lt->dev, "hactive2=%d hfp=%d hbp=%d hsync=%d htotal=%d\n",
		 hactive, hfp, hbp, hsync, htotal);
	dev_dbg(lt->dev, "vactive=%d vfp=%d vbp=%d vsync=%d vtotal=%d\n",
		 vactive, vfp, vbp, vsync, vtotal);
	usleep_range(100000, 110000);
	regmap_write(lt->regmap, 0xff, 0xd0);
	regmap_write(lt->regmap, 0x0d, (u8)(vtotal>>8)); //vtotal[15:8]
	regmap_write(lt->regmap, 0x0e, (u8)(vtotal)); //vtotal[7:0]
	regmap_write(lt->regmap, 0x0f, (u8)(vactive>>8)); //vactive[15:8]
	regmap_write(lt->regmap, 0x10, (u8)(vactive)); //vactive[7:0]
	regmap_write(lt->regmap, 0x15, (u8)(vsync)); //vs[7:0]
	regmap_write(lt->regmap, 0x17, (u8)(vfp>>8)); //vfp[15:8]
	regmap_write(lt->regmap, 0x18, (u8)(vfp)); //vfp[7:0]

	regmap_write(lt->regmap, 0x11, (u8)(htotal>>8)); //htotal[15:8]
	regmap_write(lt->regmap, 0x12, (u8)(htotal)); //htotal[7:0]
	regmap_write(lt->regmap, 0x13, (u8)(hactive>>8)); //hactive[15:8]
	regmap_write(lt->regmap, 0x14, (u8)(hactive)); //hactive[7:0]
	regmap_write(lt->regmap, 0x16, (u8)(hsync)); //hs[7:0]
	regmap_write(lt->regmap, 0x19, (u8)(hfp>>8)); //hfp[15:8]
	regmap_write(lt->regmap, 0x1a, (u8)(hfp)); //hfp[7:0]

}

static void lt2911_init(struct lt2911 *lt)
{
	u32 hact1, hact2;
	u32 vact1, vact2;
	u8 Pcr_M;
	u8 Pcr_overflow;
	u8 Pcr_underflow;
	u8 loopx;
	unsigned int rd_val;

	lt2911_get_id(lt);

	//---- LT2911_SystemInt -----------------
	/* system clock init */
	regmap_write(lt->regmap, 0xff, 0x82);
	regmap_write(lt->regmap, 0x01, 0x18);
	regmap_write(lt->regmap, 0xff, 0x86);
	regmap_write(lt->regmap, 0x06, 0x61);
	regmap_write(lt->regmap, 0x07, 0xa8); //fm for sys_clk
	regmap_write(lt->regmap, 0xff, 0x87); //初始化 txpll
	regmap_write(lt->regmap, 0x14, 0x08); //default value
	regmap_write(lt->regmap, 0x15, 0x00); //default value
	regmap_write(lt->regmap, 0x18, 0x0f);
	regmap_write(lt->regmap, 0x22, 0x08); //default value
	regmap_write(lt->regmap, 0x23, 0x00); //default value
	regmap_write(lt->regmap, 0x26, 0x0f);

	//---- LT2911_MipiRxPhy -----------------
	/* Mipi rx phy */
	regmap_write(lt->regmap, 0xff, 0x82);
	regmap_write(lt->regmap, 0x02, 0x44); //port A mipi rx enable
	regmap_write(lt->regmap, 0x0d, 0x26);
	regmap_write(lt->regmap, 0x17, 0x0c);
	regmap_write(lt->regmap, 0x1d, 0x0c);
	regmap_write(lt->regmap, 0x0a, 0xf7);
	regmap_write(lt->regmap, 0x0b, 0x77);
	/*port a*/
	regmap_write(lt->regmap, 0x05, 0x32); //port A CK lane swap
	regmap_write(lt->regmap, 0x07, 0x9f); //port clk enable
	regmap_write(lt->regmap, 0x08, 0xfc); //port lprx enable
	/*port diff swap*/
	regmap_write(lt->regmap, 0x09, 0x01); //port a diff swap
	regmap_write(lt->regmap, 0x11, 0x01); //port b diff swap
	/*port lane swap*/
	regmap_write(lt->regmap, 0xff, 0x86);
	regmap_write(lt->regmap, 0x33, 0x1b); //port a lane swap	1b:no swap
	regmap_write(lt->regmap, 0x34, 0x1b); //port b lane swap 1b:no swap

	//---- LT2911_MipiRxDigital -----------------
	regmap_write(lt->regmap, 0xff, 0x86);
	regmap_write(lt->regmap, 0x30, 0x85); //mipirx HL swap
	regmap_write(lt->regmap, 0xff, 0xD8);
	regmap_write(lt->regmap, 0x16, 0x00); //mipirx HL swap
	regmap_write(lt->regmap, 0xff, 0xd0);
	regmap_write(lt->regmap, 0x43, 0x12); //rpta mode enable,ensure da_mlrx_lptx_en=0
	regmap_write(lt->regmap, 0x02, 0x05); //mipi rx controller	//settle value

	//---- LT2911_TimingSet -----------------
	usleep_range(500000, 501000);
	regmap_write(lt->regmap, 0xff, 0xd0);
	regmap_read(lt->regmap, 0x82, &hact1);
	regmap_read(lt->regmap, 0x83, &hact2);
	hact1 = hact1 << 8;
	hact1 += hact2;
	hact1 /= 3 ;
	regmap_read(lt->regmap, 0x85, &vact1);
	regmap_read(lt->regmap, 0x86, &vact2);
	vact1 = vact1 << 8;
	vact1 += vact2;
	dev_info(lt->dev,"mipi dsi pixel: hact = %d, vact = %d", hact1, vact1);

	lt2911_set_timings(lt);

	//---- LT2911_MipiRxPll -----------------
	/* dessc pll */
	regmap_write(lt->regmap, 0xff, 0x82);
	regmap_write(lt->regmap, 0x2d, 0x48);
	regmap_write(lt->regmap, 0x35, 0x83);

	//---- LT2911_MipiPcr -----------------
	usleep_range(100000, 101000);

	Pcr_M = 0x17;
	Pcr_overflow = 0x19;
	Pcr_underflow = 0x10;
	regmap_write(lt->regmap, 0xff, 0xd0);
	regmap_write(lt->regmap, 0x26,Pcr_M);
	regmap_write(lt->regmap, 0x2d,Pcr_overflow);//PCR M overflow limit setting.
	regmap_write(lt->regmap, 0x31,Pcr_underflow); //PCR M underflow limit setting.
	dev_dbg(lt->dev,"PCR M = %x", Pcr_M);
	dev_dbg(lt->dev,"PCR M overflow limit setting = %x", Pcr_overflow);
	dev_dbg(lt->dev,"PCR M underflow limit setting = %x", Pcr_underflow);

	regmap_write(lt->regmap, 0x23, 0x20);
	regmap_write(lt->regmap, 0x38, 0x02);
	regmap_write(lt->regmap, 0x39, 0x04);
	regmap_write(lt->regmap, 0x3a, 0x08);
	regmap_write(lt->regmap, 0x3b, 0x10);
	regmap_write(lt->regmap, 0x3f, 0x04);
	regmap_write(lt->regmap, 0x40, 0x08);
	regmap_write(lt->regmap, 0x41, 0x10);
	usleep_range(100000, 101000);
	regmap_write(lt->regmap, 0xff, 0x81);
	regmap_write(lt->regmap, 0x0B, 0xEF);
	regmap_write(lt->regmap, 0x0B, 0xFF);
	usleep_range(500000, 501000);
	for(loopx = 0; loopx < 5; loopx++) {//Check pcr_stable
		usleep_range(300000, 301000);
		regmap_write(lt->regmap, 0xff, 0xd0);
		regmap_read(lt->regmap, 0x87, &rd_val);

		if(rd_val & 0x08) {
			dev_dbg(lt->dev,"LT2911 pcr stable");
			break;
		} else 	{
			dev_dbg(lt->dev,"LT2911 pcr unstable!!!!");
		}
	}
	//---- LT2911_TxDigital -----------------
	dev_dbg(lt->dev,"\rLT2911 set to OUTPUT_LVDS");
	regmap_write(lt->regmap, 0xff, 0x85); /* lvds tx controller */
	regmap_write(lt->regmap, 0x59, 0x50);
	regmap_write(lt->regmap, 0x5a, 0xaa);
	regmap_write(lt->regmap, 0x5b, 0xaa);
	regmap_write(lt->regmap, 0x5c, 0x00);
	regmap_write(lt->regmap, 0x88, 0x50);
	regmap_write(lt->regmap, 0xa1, 0x77);
	regmap_write(lt->regmap, 0xff, 0x86);
	regmap_write(lt->regmap, 0x40, 0x40); //tx_src_sel
	/*port src sel*/
	regmap_write(lt->regmap, 0x41, 0x34);
	regmap_write(lt->regmap, 0x42, 0x10);
	regmap_write(lt->regmap, 0x43, 0x23); //pt0_tx_src_sel
	regmap_write(lt->regmap, 0x44, 0x41);
	regmap_write(lt->regmap, 0x45, 0x02); //pt1_tx_src_scl

	//---- LT2911_TxPhy -----------------
	regmap_write(lt->regmap, 0xff, 0x82);
	/* dual-port lvds tx phy */
	regmap_write(lt->regmap, 0x62, 0x00); //ttl output disable
	regmap_write(lt->regmap, 0x3b, 0x38);
	regmap_write(lt->regmap, 0x3e, 0x92);
	regmap_write(lt->regmap, 0x3f, 0x48);
	regmap_write(lt->regmap, 0x40, 0x31);
	regmap_write(lt->regmap, 0x43, 0x80);
	regmap_write(lt->regmap, 0x44, 0x00);
	regmap_write(lt->regmap, 0x45, 0x00);
	regmap_write(lt->regmap, 0x49, 0x00);
	regmap_write(lt->regmap, 0x4a, 0x01);
	regmap_write(lt->regmap, 0x4e, 0x00);
	regmap_write(lt->regmap, 0x4f, 0x00);
	regmap_write(lt->regmap, 0x50, 0x00);
	regmap_write(lt->regmap, 0x53, 0x00);
	regmap_write(lt->regmap, 0x54, 0x01);
	regmap_write(lt->regmap, 0xff, 0x81);
	regmap_write(lt->regmap, 0x20, 0x7b);
	regmap_write(lt->regmap, 0x20, 0xff); //mlrx mltx calib reset

	//---- LT2911_Txpll -----------------
	usleep_range(10000, 11000);
	regmap_write(lt->regmap, 0xff, 0x82);
	regmap_write(lt->regmap, 0x36, 0x01); //b7:txpll_pd
	regmap_write(lt->regmap, 0x37, 0x29);
	regmap_write(lt->regmap, 0x38, 0x06);
	regmap_write(lt->regmap, 0x39, 0x30);
	regmap_write(lt->regmap, 0x3a, 0x8e);
	regmap_write(lt->regmap, 0xff, 0x87);
	regmap_write(lt->regmap, 0x37, 0x14);
	regmap_write(lt->regmap, 0x13, 0x00);
	regmap_write(lt->regmap, 0x13, 0x80);
	usleep_range(100000, 101000);
	for(loopx = 0; loopx < 10; loopx++) {//Check Tx PLL cal
		regmap_write(lt->regmap, 0xff, 0x87);
		regmap_read(lt->regmap, 0x1f, &rd_val);

		if(rd_val & 0x80) {
			regmap_read(lt->regmap, 0x20, &rd_val);
			if(rd_val & 0x80) {
				dev_dbg(lt->dev,"LT2911 tx pll lock");
			} else {
				dev_dbg(lt->dev,"LT2911 tx pll unlocked");
			}
			dev_dbg(lt->dev,"LT2911 tx pll cal done");
			break;
		} else {
			dev_dbg(lt->dev,"LT2911 tx pll unlocked");
		}
	}
	dev_dbg(lt->dev,"LT2911 system success");
}

static void lt2911_exit(struct lt2911 *lt)
{
}

static void lt2911_power_on(struct lt2911 *lt)
{
	gpiod_set_value(lt->enable_gpio, 1);
}

static void lt2911_power_off(struct lt2911 *lt)
{
	gpiod_set_value(lt->enable_gpio, 0);
}

static enum drm_connector_status
lt2911_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_funcs lt2911_connector_funcs = {
	.detect = lt2911_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static struct drm_encoder *
lt2911_connector_best_encoder(struct drm_connector *connector)
{
	struct lt2911 *lt = connector_to_lt2911(connector);

	return lt->bridge.encoder;
}

static int lt2911_connector_get_modes(struct drm_connector *connector)
{
	struct lt2911 *lt = connector_to_lt2911(connector);
	struct drm_display_mode *mode;
	u32 bus_flags = 0;
	int ret;

	mode = drm_mode_create(connector->dev);
	if (!mode)
		return -EINVAL;

	ret = of_get_drm_display_mode(lt->dev->of_node, mode,
				      &bus_flags, OF_USE_NATIVE_MODE);
	if (ret) {
		dev_err(lt->dev, "failed to get display timings\n");
		drm_mode_destroy(connector->dev, mode);
		return 0;
	}

	mode->type |= DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_connector_helper_funcs lt2911_connector_helper_funcs = {
	.get_modes = lt2911_connector_get_modes,
	.best_encoder = lt2911_connector_best_encoder,
};

static void lt2911_bridge_post_disable(struct drm_bridge *bridge)
{
	struct lt2911 *lt = bridge_to_lt2911(bridge);

	lt2911_power_off(lt);
}

static void lt2911_bridge_disable(struct drm_bridge *bridge)
{
	struct lt2911 *lt = bridge_to_lt2911(bridge);

	lt2911_exit(lt);
}

static void lt2911_bridge_enable(struct drm_bridge *bridge)
{
	struct lt2911 *lt = bridge_to_lt2911(bridge);

	lt2911_init(lt);
}

static void lt2911_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct lt2911 *lt = bridge_to_lt2911(bridge);

	lt2911_power_on(lt);
}

static void lt2911_bridge_mode_set(struct drm_bridge *bridge,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *adj)
{
	struct lt2911 *lt = bridge_to_lt2911(bridge);

	drm_mode_copy(&lt->mode, adj);
	drm_display_mode_to_videomode(adj, &lt->vm);
}

static int lt2911_bridge_attach(struct drm_bridge *bridge)
{
	struct lt2911 *lt = bridge_to_lt2911(bridge);
	struct drm_connector *connector = &lt->connector;
	int ret;

	ret = drm_connector_init(bridge->dev, connector,
				 &lt2911_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		dev_err(lt->dev, "failed to initialize connector\n");
		return ret;
	}

	drm_connector_helper_add(connector, &lt2911_connector_helper_funcs);
	drm_mode_connector_attach_encoder(connector, bridge->encoder);

	ret = lt2911_attach_dsi(lt);
	return ret;
}

static const struct drm_bridge_funcs lt2911_bridge_funcs = {
	.attach = lt2911_bridge_attach,
	.mode_set = lt2911_bridge_mode_set,
	.pre_enable = lt2911_bridge_pre_enable,
	.enable = lt2911_bridge_enable,
	.disable = lt2911_bridge_disable,
	.post_disable = lt2911_bridge_post_disable,
};

static const struct regmap_range lt2911_volatile_ranges[] = {
	{ .range_min = 0, .range_max = 0xff },
};

static const struct regmap_access_table lt2911_volatile_table = {
	.yes_ranges = lt2911_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(lt2911_volatile_ranges),
};

static const struct regmap_config lt2911_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &lt2911_volatile_table,
	.cache_type = REGCACHE_NONE,
};

int lt2911_attach_dsi(struct lt2911 *lt)
{
	struct device *dev = lt->dev;
	struct mipi_dsi_host *host;
	struct mipi_dsi_device *dsi;
	int ret = 0;
	const struct mipi_dsi_device_info info = { .type = "lt2911",
						   .channel = lt->channel_id,
						   .node = NULL,
						 };

	host = of_find_mipi_dsi_host_by_node(lt->host_node);
	if (!host) {
		dev_err(dev, "failed to find dsi host\n");
		return -EPROBE_DEFER;
	}

	dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(dsi)) {
		dev_err(dev, "failed to create dsi device\n");
		ret = PTR_ERR(dsi);
		return ret;
	}

	lt->dsi = dsi;

	dsi->lanes = lt->num_dsi_lanes;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "failed to attach dsi to host\n");
		mipi_dsi_device_unregister(dsi);
		return ret;
	}

	return 0;
}

void lt2911_detach_dsi(struct lt2911 *lt)
{
	mipi_dsi_detach(lt->dsi);
	mipi_dsi_device_unregister(lt->dsi);
}


static int lt2911_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct lt2911 *lt;
	struct device_node *endpoint;
	int ret;

	lt = devm_kzalloc(dev, sizeof(*lt), GFP_KERNEL);
	if (!lt)
		return -ENOMEM;

	lt->dev = dev;

	lt->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(lt->enable_gpio)) {
		lt->enable_gpio = NULL;
		dev_err(dev, "Couldn't get our power GPIO\n");
		return PTR_ERR(lt->enable_gpio);
	}

	lt->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(lt->reset_gpio)) {
		ret = PTR_ERR(lt->reset_gpio);
		dev_err(dev, "failed to request reset GPIO: %d\n", ret);
	}

	lt->regmap = devm_regmap_init_i2c(client, &lt2911_regmap_config);
	if (IS_ERR(lt->regmap)) {
		ret = PTR_ERR(lt->regmap);
		dev_err(lt->dev,
			"Failed to initialize regmap: %d\n", ret);
		return ret;
	}

	lt->num_dsi_lanes = 4;
	lt->channel_id = 0;

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint)
		return -ENODEV;

	lt->host_node = of_graph_get_remote_port_parent(endpoint);
	if (!lt->host_node) {
		of_node_put(endpoint);
		return -ENODEV;
	}

	of_node_put(endpoint);
	of_node_put(lt->host_node);

	lt->bridge.funcs = &lt2911_bridge_funcs;
	lt->bridge.of_node = dev->of_node;
	ret = drm_bridge_add(&lt->bridge);
	if (ret) {
		dev_err(dev, "failed to add bridge: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(client, lt);

	return 0;
}

static int lt2911_remove(struct i2c_client *client)
{
	struct lt2911 *lt = i2c_get_clientdata(client);

	lt2911_detach_dsi(lt);
	drm_bridge_remove(&lt->bridge);

	return 0;
}

static const struct i2c_device_id lt2911_i2c_ids[] = {
	{ "lt2911", 0 },
	{ }
};

static const struct of_device_id lt2911_of_match[] = {
	{ .compatible = "lontium,lt2911" },
	{}
};
MODULE_DEVICE_TABLE(of, lt2911_of_match);

static struct mipi_dsi_driver lt2911_driver = {
	.driver.name = "lt2911",
};

static struct i2c_driver lt2911_i2c_driver = {
	.driver = {
		.name = "lt2911",
		.of_match_table = lt2911_of_match,
	},
	.id_table = lt2911_i2c_ids,
	.probe = lt2911_probe,
	.remove = lt2911_remove,
};

static int __init lt2911_i2c_drv_init(void)
{
	mipi_dsi_driver_register(&lt2911_driver);

	return i2c_add_driver(&lt2911_i2c_driver);
}
module_init(lt2911_i2c_drv_init);

static void __exit lt2911_i2c_exit(void)
{
	i2c_del_driver(&lt2911_i2c_driver);

	mipi_dsi_driver_unregister(&lt2911_driver);
}
module_exit(lt2911_i2c_exit);

MODULE_AUTHOR("nick@embest-tech.com>");
MODULE_DESCRIPTION("LT2911 dual-port MIPI/LVDS/TTL converter driver");
MODULE_LICENSE("GPL");
