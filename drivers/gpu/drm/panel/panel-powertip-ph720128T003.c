// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020, SOMLABS
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

#include <video/of_videomode.h>
#include <video/videomode.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <linux/of.h>


static const u32 ph720128t003_bus_formats[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_RGB666_1X18,
	MEDIA_BUS_FMT_RGB565_1X16,
};

struct ph720128t003 {
	struct drm_panel	panel;
	struct mipi_dsi_device	*dsi;

	struct gpio_desc	*reset;
	struct backlight_device *backlight;

	struct regulator	*power;

	bool prepared;
	bool enabled;

	struct videomode vm;
	u32 width_mm;
	u32 height_mm;
};

struct ph720128t003_instr {
        u8 cmd;
        u8 data;
};

#define CMD_INSTR(CMD, DATA)     {.cmd = CMD, .data = DATA}


static const struct ph720128t003_instr ph720128t003_init_data[] = {
	CMD_INSTR(0xFF, 0x03),
	CMD_INSTR(0x01, 0x00),
	CMD_INSTR(0x02, 0x00),
	CMD_INSTR(0x03, 0x55),
	CMD_INSTR(0x04, 0x13),
	CMD_INSTR(0x05, 0x00),
	CMD_INSTR(0x06, 0x06),
	CMD_INSTR(0x07, 0x01),
	CMD_INSTR(0x08, 0x00),
	CMD_INSTR(0x09, 0x01),
	CMD_INSTR(0x0A, 0x01),
	CMD_INSTR(0x0B, 0x00),
	CMD_INSTR(0x0C, 0x00),
	CMD_INSTR(0x0D, 0x00),
	CMD_INSTR(0x0E, 0x00),
	CMD_INSTR(0x0F, 0x18),
	CMD_INSTR(0x10, 0x18),
	CMD_INSTR(0x11, 0x00),
	CMD_INSTR(0x12, 0x00),
	CMD_INSTR(0x13, 0x00),
	CMD_INSTR(0x14, 0x00),
	CMD_INSTR(0x15, 0x00),
	CMD_INSTR(0x16, 0x00),
	CMD_INSTR(0x17, 0x00),
	CMD_INSTR(0x18, 0x00),
	CMD_INSTR(0x19, 0x00),
	CMD_INSTR(0x1A, 0x00),
	CMD_INSTR(0x1B, 0x00),
	CMD_INSTR(0x1C, 0x00),
	CMD_INSTR(0x1D, 0x00),
	CMD_INSTR(0x1E, 0x44),
	CMD_INSTR(0x1F, 0x80),
	CMD_INSTR(0x20, 0x02),
	CMD_INSTR(0x21, 0x03),
	CMD_INSTR(0x22, 0x00),
	CMD_INSTR(0x23, 0x00),
	CMD_INSTR(0x24, 0x00),
	CMD_INSTR(0x25, 0x00),
	CMD_INSTR(0x26, 0x00),
	CMD_INSTR(0x27, 0x00),
	CMD_INSTR(0x28, 0x33),
	CMD_INSTR(0x29, 0x03),
	CMD_INSTR(0x2A, 0x00),
	CMD_INSTR(0x2B, 0x00),
	CMD_INSTR(0x2C, 0x00),
	CMD_INSTR(0x2D, 0x00),
	CMD_INSTR(0x2E, 0x00),
	CMD_INSTR(0x2F, 0x00),
	CMD_INSTR(0x30, 0x00),
	CMD_INSTR(0x31, 0x00),
	CMD_INSTR(0x32, 0x00),
	CMD_INSTR(0x33, 0x00),
	CMD_INSTR(0x34, 0x04),
	CMD_INSTR(0x35, 0x00),
	CMD_INSTR(0x36, 0x00),
	CMD_INSTR(0x37, 0x00),
	CMD_INSTR(0x38, 0x01),
	CMD_INSTR(0x39, 0x00),
	CMD_INSTR(0x3A, 0x00),
	CMD_INSTR(0x3B, 0x00),
	CMD_INSTR(0x3C, 0x00),
	CMD_INSTR(0x3D, 0x00),
	CMD_INSTR(0x3E, 0x00),
	CMD_INSTR(0x3F, 0x00),
	CMD_INSTR(0x40, 0x00),
	CMD_INSTR(0x41, 0x00),
	CMD_INSTR(0x42, 0x00),
	CMD_INSTR(0x43, 0x00),
	CMD_INSTR(0x44, 0x00),
	CMD_INSTR(0x50, 0x01),
	CMD_INSTR(0x51, 0x23),
	CMD_INSTR(0x52, 0x45),
	CMD_INSTR(0x53, 0x67),
	CMD_INSTR(0x54, 0x89),
	CMD_INSTR(0x55, 0xAB),
	CMD_INSTR(0x56, 0x01),
	CMD_INSTR(0x57, 0x23),
	CMD_INSTR(0x58, 0x45),
	CMD_INSTR(0x59, 0x67),
	CMD_INSTR(0x5A, 0x89),
	CMD_INSTR(0x5B, 0xAB),
	CMD_INSTR(0x5C, 0xCD),
	CMD_INSTR(0x5D, 0xEF),
	CMD_INSTR(0x5E, 0x11),
	CMD_INSTR(0x5F, 0x14),
	CMD_INSTR(0x60, 0x15),
	CMD_INSTR(0x61, 0x0F),
	CMD_INSTR(0x62, 0x0D),
	CMD_INSTR(0x63, 0x0E),
	CMD_INSTR(0x64, 0x0C),
	CMD_INSTR(0x65, 0x06),
	CMD_INSTR(0x66, 0x02),
	CMD_INSTR(0x67, 0x02),
	CMD_INSTR(0x68, 0x02),
	CMD_INSTR(0x69, 0x02),
	CMD_INSTR(0x6A, 0x02),
	CMD_INSTR(0x6B, 0x02),
	CMD_INSTR(0x6C, 0x02),
	CMD_INSTR(0x6D, 0x02),
	CMD_INSTR(0x6E, 0x02),
	CMD_INSTR(0x6F, 0x02),
	CMD_INSTR(0x70, 0x02),
	CMD_INSTR(0x71, 0x00),
	CMD_INSTR(0x72, 0x01),
	CMD_INSTR(0x73, 0x08),
	CMD_INSTR(0x74, 0x02),
	CMD_INSTR(0x75, 0x14),
	CMD_INSTR(0x76, 0x15),
	CMD_INSTR(0x77, 0x0F),
	CMD_INSTR(0x78, 0x0D),
	CMD_INSTR(0x79, 0x0E),
	CMD_INSTR(0x7A, 0x0C),
	CMD_INSTR(0x7B, 0x08),
	CMD_INSTR(0x7C, 0x02),
	CMD_INSTR(0x7D, 0x02),
	CMD_INSTR(0x7E, 0x02),
	CMD_INSTR(0x7F, 0x02),
	CMD_INSTR(0x80, 0x02),
	CMD_INSTR(0x81, 0x02),
	CMD_INSTR(0x82, 0x02),
	CMD_INSTR(0x83, 0x02),
	CMD_INSTR(0x84, 0x02),
	CMD_INSTR(0x85, 0x02),
	CMD_INSTR(0x86, 0x02),
	CMD_INSTR(0x87, 0x00),
	CMD_INSTR(0x88, 0x01),
	CMD_INSTR(0x89, 0x06),
	CMD_INSTR(0x8A, 0x02),
	CMD_INSTR(0xFF, 0x04),
	CMD_INSTR(0x6C, 0x15),
	CMD_INSTR(0x6E, 0x2A),
	CMD_INSTR(0x6F, 0x33),
	CMD_INSTR(0x3A, 0x24),
	CMD_INSTR(0x8D, 0x14),
	CMD_INSTR(0x87, 0xBA),
	CMD_INSTR(0x26, 0x76),
	CMD_INSTR(0xB2, 0xD1),
	CMD_INSTR(0xB5, 0xD7),
	CMD_INSTR(0x35, 0x1F),
	CMD_INSTR(0xFF, 0x01),
	CMD_INSTR(0x22, 0x0A),
	CMD_INSTR(0x53, 0x72),
	CMD_INSTR(0x55, 0x77),
	CMD_INSTR(0x50, 0xA6),
	CMD_INSTR(0x51, 0xA6),
	CMD_INSTR(0x31, 0x00),
	CMD_INSTR(0x60, 0x20),
	CMD_INSTR(0xA0, 0x08),
	CMD_INSTR(0xA1, 0x1A),
	CMD_INSTR(0xA2, 0x2A),
	CMD_INSTR(0xA3, 0x14),
	CMD_INSTR(0xA4, 0x17),
	CMD_INSTR(0xA5, 0x2B),
	CMD_INSTR(0xA6, 0x1D),
	CMD_INSTR(0xA7, 0x20),
	CMD_INSTR(0xA8, 0x9D),
	CMD_INSTR(0xA9, 0x1C),
	CMD_INSTR(0xAA, 0x29),
	CMD_INSTR(0xAB, 0x8F),
	CMD_INSTR(0xAC, 0x20),
	CMD_INSTR(0xAD, 0x1F),
	CMD_INSTR(0xAE, 0x4F),
	CMD_INSTR(0xAF, 0x23),
	CMD_INSTR(0xB0, 0x29),
	CMD_INSTR(0xB1, 0x56),
	CMD_INSTR(0xB2, 0x66),
	CMD_INSTR(0xB3, 0x39),
	CMD_INSTR(0xC0, 0x08),
	CMD_INSTR(0xC1, 0x1A),
	CMD_INSTR(0xC2, 0x2A),
	CMD_INSTR(0xC3, 0x15),
	CMD_INSTR(0xC4, 0x17),
	CMD_INSTR(0xC5, 0x2B),
	CMD_INSTR(0xC6, 0x1D),
	CMD_INSTR(0xC7, 0x20),
	CMD_INSTR(0xC8, 0x9D),
	CMD_INSTR(0xC9, 0x1D),
	CMD_INSTR(0xCA, 0x29),
	CMD_INSTR(0xCB, 0x8F),
	CMD_INSTR(0xCC, 0x20),
	CMD_INSTR(0xCD, 0x1F),
	CMD_INSTR(0xCE, 0x4F),
	CMD_INSTR(0xCF, 0x24),
	CMD_INSTR(0xD0, 0x29),
	CMD_INSTR(0xD1, 0x56),
	CMD_INSTR(0xD2, 0x66),
	CMD_INSTR(0xD3, 0x39),
	CMD_INSTR(0xFF, 0x00),
	CMD_INSTR(0x11, 0x00),
};

static inline struct ph720128t003 *panel_to_ph720128t003(struct drm_panel *panel)
{
	return container_of(panel, struct ph720128t003, panel);
}

/*
 * The panel seems to accept some private DCS commands that map
 * directly to registers.
 *
 * It is organised by page, with each page having its own set of
 * registers, and the first page looks like it's holding the standard
 * DCS commands.
 *
 * So before any attempt at sending a command or data, we have to be
 * sure if we're in the right page or not.
 */
static int ph720128t003_switch_page(struct mipi_dsi_device *dsi, u8 page)
{
	u8 buf[4] = { 0xff, 0x98, 0x81, page };

	return mipi_dsi_dcs_write_buffer(dsi, buf, sizeof(buf));
}

static int ph720128t003_send_cmd_data(struct mipi_dsi_device *dsi, u8 cmd, u8 data)
{
	u8 buf[2] = { cmd, data };
	return mipi_dsi_dcs_write_buffer(dsi, buf, sizeof(buf));
}

static int ph720128t003_init(struct mipi_dsi_device *dsi)
{
	size_t i;
	int ret = 0;
	for (i = 0; i < ARRAY_SIZE(ph720128t003_init_data); i++) {
		const struct ph720128t003_instr *instr = &ph720128t003_init_data[i];

		if (instr->cmd == 0xFF) {
			ret = ph720128t003_switch_page(dsi, instr->data);
		} else {
			ret = ph720128t003_send_cmd_data(dsi, instr->cmd,
						      instr->data);
		}
		if (ret < 0) {
			dev_err(&dsi->dev, "Error when setting device @ %u (cmd: %08X)\n", i, instr->cmd);
			return ret;
		}
	}
	if(ret > 0) {
		ret = 0;
	}
	return ret;
}

static int color_format_from_dsi_format(enum mipi_dsi_pixel_format format)
{
	switch (format) {
	case MIPI_DSI_FMT_RGB565:
		return 0x55;
	case MIPI_DSI_FMT_RGB666:
	case MIPI_DSI_FMT_RGB666_PACKED:
		return 0x66;
	case MIPI_DSI_FMT_RGB888:
		return 0x77;
	default:
		return 0x77; /* for backward compatibility */
	}
};

static int ph720128t003_prepare(struct drm_panel *panel)
{
	struct ph720128t003 *ctx = panel_to_ph720128t003(panel);

	if (ctx->reset) {

		gpiod_set_value(ctx->reset, 1);
		usleep_range(20000, 25000);

		gpiod_set_value(ctx->reset, 0);
		usleep_range(20000, 25000);
	}

	ctx->prepared = true;

	return 0;
}

static int ph720128t003_unprepare(struct drm_panel *panel)
{
	struct ph720128t003 *ctx = panel_to_ph720128t003(panel);
		struct device *dev = &ctx->dsi->dev;

	if (!ctx->prepared)
		return 0;

	if (ctx->enabled) {
		DRM_DEV_ERROR(dev, "Panel still enabled!\n");
		return -EPERM;
	}

	if (ctx->reset != NULL) {
		gpiod_set_value(ctx->reset, 0);
		usleep_range(15000, 17000);
		gpiod_set_value(ctx->reset, 1);
	}

	ctx->prepared = false;
	return 0;
}

static int ph720128t003_enable(struct drm_panel *panel)
{
	struct ph720128t003 *ctx = panel_to_ph720128t003(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int color_format = color_format_from_dsi_format(dsi->format);
	u16 brightness;
	int ret;
	u8 buf[2] = {0};

	if (ctx->enabled)
		return 0;

	if (!ctx->prepared) {
		DRM_DEV_ERROR(dev, "Panel not prepared!\n");
		return -EPERM;
	}

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;	//?????

	ph720128t003_init(dsi);

	usleep_range(15000, 17000);


	ph720128t003_switch_page(dsi, 0);
	buf[0] = MIPI_DCS_EXIT_SLEEP_MODE;
	buf[1] = 0;
	mipi_dsi_dcs_write_buffer(dsi, buf, 2);
	mdelay(120);
	buf[0] = MIPI_DCS_SET_DISPLAY_ON;
	mipi_dsi_dcs_write_buffer(dsi, buf, 2);

	// /* Exit sleep mode */
	// ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	// if (ret < 0) {
	// 	DRM_DEV_ERROR(dev, "Failed to exit sleep mode (%d)\n", ret);
	// 	goto fail;
	// }

	// usleep_range(5000, 10000);

	// ret = mipi_dsi_dcs_set_display_on(dsi);
	// if (ret < 0) {
	// 	DRM_DEV_ERROR(dev, "Failed to set display ON (%d)\n", ret);
	// 	goto fail;
	// }

	backlight_enable(ctx->backlight);

	ctx->enabled = true;
	dsi->mode_flags &= MIPI_DSI_MODE_LPM;

	return 0;
fail:
	//if (ctx->reset != NULL)
	//	gpiod_set_value(ctx->reset, 0);

	return ret;
}

static int ph720128t003_disable(struct drm_panel *panel)
{
	struct ph720128t003 *ctx = panel_to_ph720128t003(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	if (!ctx->enabled)
		return 0;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	backlight_disable(ctx->backlight);

	usleep_range(10000, 15000);

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to set display OFF (%d)\n", ret);
		//return ret;
	}

	usleep_range(5000, 10000);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to enter sleep mode (%d)\n", ret);
		//return ret;
	}

	ctx->enabled = false;

	return 0;
}

static int ph720128t003_get_modes(struct drm_panel *panel)
{
	struct ph720128t003 *ctx = panel_to_ph720128t003(panel);
	struct device *dev = &ctx->dsi->dev;
	struct drm_connector *connector = panel->connector;
	struct drm_display_mode *mode;
	u32 *bus_flags = &connector->display_info.bus_flags;
	int ret;

	DRM_DEV_INFO(dev, "get modes\n");

	mode = drm_mode_create(connector->dev);
	if (!mode) {
		DRM_DEV_ERROR(dev, "Failed to create display mode!\n");
		return 0;
	}

	drm_display_mode_from_videomode(&ctx->vm, mode);
	mode->width_mm = ctx->width_mm;
	mode->height_mm = ctx->height_mm;
	connector->display_info.width_mm = ctx->width_mm;
	connector->display_info.height_mm = ctx->height_mm;
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	if (ctx->vm.flags & DISPLAY_FLAGS_DE_HIGH)
		*bus_flags |= DRM_BUS_FLAG_DE_HIGH;
	if (ctx->vm.flags & DISPLAY_FLAGS_DE_LOW)
		*bus_flags |= DRM_BUS_FLAG_DE_LOW;
	if (ctx->vm.flags & DISPLAY_FLAGS_PIXDATA_NEGEDGE)
		*bus_flags |= DRM_BUS_FLAG_PIXDATA_NEGEDGE;
	if (ctx->vm.flags & DISPLAY_FLAGS_PIXDATA_POSEDGE)
		*bus_flags |= DRM_BUS_FLAG_PIXDATA_POSEDGE;

	ret = drm_display_info_set_bus_formats(&connector->display_info,
			ph720128t003_bus_formats, ARRAY_SIZE(ph720128t003_bus_formats));
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to set display bus format (error:%d)!\n", ret);
		return ret;
	}

	drm_mode_probed_add(panel->connector, mode);

	return 1;
}

static const struct drm_panel_funcs ph720128t003_funcs = {
	.prepare	= ph720128t003_prepare,
	.unprepare	= ph720128t003_unprepare,
	.enable		= ph720128t003_enable,
	.disable	= ph720128t003_disable,
	.get_modes	= ph720128t003_get_modes,
};

/*
static const struct drm_display_mode ph720128t003_default_mode = {
	.clock		= 54000,
	.vrefresh	= 40,

	.hdisplay	= 720,
	.hsync_start	= 720 + 20,
	.hsync_end	= 720 + 20 + 60,
	.htotal		= 720 + 20 + 60 + 20,

	.vdisplay	= 1280,
	.vsync_start	= 1280 + 10,
	.vsync_end	= 1280 + 10 + 2,
	.vtotal		= 1280 + 10 + 2 + 15,
	.flags		= 0,
};
*/

/*
 */
static const struct display_timing ph720128t003_default_timing = {
	.pixelclock = { 54000000, 55000000, 56000000},
	.hactive = { 720, 720, 720 },
	.hfront_porch = { 20, 20 , 20 },
	.hsync_len = { 60, 60, 60 },
	.hback_porch = { 20, 20, 20 },
	.vactive = { 1280, 1280, 1280 },
	.vfront_porch = { 10, 10, 10 },
	.vsync_len = { 2, 2, 2 },
	.vback_porch = { 15, 15, 15 },
	.flags = DISPLAY_FLAGS_HSYNC_LOW |
		 DISPLAY_FLAGS_VSYNC_LOW |
		 DISPLAY_FLAGS_DE_LOW |
		 DISPLAY_FLAGS_PIXDATA_NEGEDGE,
};

static int ph720128t003_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *np;
	struct ph720128t003 *ctx;
	int ret;

	DRM_DEV_INFO(dev, "\n");

	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;

	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE | MIPI_DSI_MODE_LPM;
//	dsi->mode_flags |= MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_VIDEO_HSE;
	dsi->lanes = 2;

	videomode_from_timing(&ph720128t003_default_timing, &ctx->vm);

	ctx->width_mm = 90;
	ctx->height_mm = 152;

	ctx->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);

	if (IS_ERR(ctx->reset))
		ctx->reset = NULL;
	else
		gpiod_set_value(ctx->reset, 0);

	np = of_parse_phandle(dsi->dev.of_node, "backlight", 0);
	if (np) {
		ctx->backlight = of_find_backlight_by_node(np);
		of_node_put(np);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = &dsi->dev;
	ctx->panel.funcs = &ph720128t003_funcs;
	dev_set_drvdata(dev, ctx);

	ret = drm_panel_add(&ctx->panel);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

	return ret;
}

static int ph720128t003_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct ph720128t003 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	if (ctx->backlight)
		put_device(&ctx->backlight->dev);

	return 0;
}

static const struct of_device_id ph720128t003_of_match[] = {
	{ .compatible = "powertip,ph720128t003" },
	{ }
};
MODULE_DEVICE_TABLE(of, ph720128t003_of_match);

static struct mipi_dsi_driver ph720128t003_dsi_driver = {
	.probe		= ph720128t003_dsi_probe,
	.remove		= ph720128t003_dsi_remove,
	.driver = {
		.name		= "ph720128t003-dsi",
		.of_match_table	= ph720128t003_of_match,
	},
};
module_mipi_dsi_driver(ph720128t003_dsi_driver);

MODULE_AUTHOR("Arkadiusz Karas <arkadiusz.karas@somlabs.com>");
MODULE_DESCRIPTION("Powertip PH720128T003 Controller Driver");
MODULE_LICENSE("GPL v2");
