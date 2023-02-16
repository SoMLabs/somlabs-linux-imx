// // SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022, SOMLABS
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct rvt70hsmnwc00 {
	struct device *dev;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	bool prepared;
	bool enabled;
};

static const struct drm_display_mode rvt70hsmnwc00_default_mode = {
        .clock       = 51000,
        .hdisplay    = 1024,
        .hsync_start = 1024 + 160,
        .hsync_end   = 1024 + 160 + 80,
        .htotal      = 1024 + 160 + 80 + 160,
        .vdisplay    = 600,
        .vsync_start = 600 + 12,
        .vsync_end   = 600 + 12 + 10,
        .vtotal      = 600 + 12 + 10 + 23,
        .flags       = 0,
        .width_mm    = 68,
        .height_mm   = 122,
};

static inline struct rvt70hsmnwc00 *panel_to_rvt70hsmnwc00(struct drm_panel *panel)
{
	return container_of(panel, struct rvt70hsmnwc00, panel);
}

static void rvt70hsmnwc00_dcs_write_cmd(struct rvt70hsmnwc00 *ctx, u8 cmd)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int err;

        err = mipi_dsi_dcs_write_buffer(dsi, &cmd, 1);
        if (err < 0)
                dev_err_ratelimited(ctx->dev, "write failed: %d\n", err);
}

static void rvt70hsmnwc00_dcs_write_cmd_with_param(struct rvt70hsmnwc00 *ctx, u8 cmd, u8 value)
{
        struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
        int err;
        u8 buf[2] = {0};

        buf[0] = cmd;
        buf[1] = value;
        err = mipi_dsi_dcs_write_buffer(dsi, buf, 2);
        if (err < 0)
                dev_err_ratelimited(ctx->dev, "write failed: %d\n", err);
}

static void rvt70hsmnwc00_init_sequence(struct rvt70hsmnwc00 *ctx)
{
        rvt70hsmnwc00_dcs_write_cmd(ctx, 0x01);
        msleep(120);

        rvt70hsmnwc00_dcs_write_cmd_with_param(ctx, 0xB2, 0x70);
        rvt70hsmnwc00_dcs_write_cmd_with_param(ctx, 0x80, 0x4B);
        rvt70hsmnwc00_dcs_write_cmd_with_param(ctx, 0x81, 0xFF);
        rvt70hsmnwc00_dcs_write_cmd_with_param(ctx, 0x82, 0x1A);
        rvt70hsmnwc00_dcs_write_cmd_with_param(ctx, 0x83, 0x88);
        rvt70hsmnwc00_dcs_write_cmd_with_param(ctx, 0x84, 0x8F);
        rvt70hsmnwc00_dcs_write_cmd_with_param(ctx, 0x85, 0x35);
        rvt70hsmnwc00_dcs_write_cmd_with_param(ctx, 0x86, 0xB0);
}

static int rvt70hsmnwc00_enable(struct drm_panel *panel)
{
        struct rvt70hsmnwc00 *ctx = panel_to_rvt70hsmnwc00(panel);
        struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
        int ret;

        if (ctx->enabled)
                return 0;

        rvt70hsmnwc00_init_sequence(ctx);

        ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
        if (ret)
                return ret;

        msleep(125);

        ret = mipi_dsi_dcs_set_display_on(dsi);
        if (ret)
                return ret;

        msleep(20);

        ctx->enabled = true;

        return 0;
}

static int rvt70hsmnwc00_disable(struct drm_panel *panel)
{
	struct rvt70hsmnwc00 *ctx = panel_to_rvt70hsmnwc00(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	if (!ctx->enabled)
		return 0;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret)
		dev_warn(panel->dev, "failed to set display off: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret)
		dev_warn(panel->dev, "failed to enter sleep mode: %d\n", ret);

	msleep(120);

	ctx->enabled = false;

	return 0;
}

static int rvt70hsmnwc00_unprepare(struct drm_panel *panel)
{
	struct rvt70hsmnwc00 *ctx = panel_to_rvt70hsmnwc00(panel);

	if (!ctx->prepared)
		return 0;

	if (ctx->reset_gpio != NULL) {
		gpiod_set_value(ctx->reset_gpio, 0);
		usleep_range(15000, 17000);
		gpiod_set_value(ctx->reset_gpio, 1);
	}

	ctx->prepared = false;

	return 0;
}

static int rvt70hsmnwc00_prepare(struct drm_panel *panel)
{
	struct rvt70hsmnwc00 *ctx = panel_to_rvt70hsmnwc00(panel);

	if (ctx->prepared)
		return 0;

	if (ctx->reset_gpio != NULL) {
		gpiod_set_value(ctx->reset_gpio, 1);
		usleep_range(20000, 25000);

		gpiod_set_value(ctx->reset_gpio, 0);
		usleep_range(20000, 25000);
	}

	ctx->prepared = true;

	return 0;
}

static int rvt70hsmnwc00_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &rvt70hsmnwc00_default_mode);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
			rvt70hsmnwc00_default_mode.hdisplay,
			rvt70hsmnwc00_default_mode.vdisplay,
			drm_mode_vrefresh(&rvt70hsmnwc00_default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	return 1;
}

static const struct drm_panel_funcs rvt70hsmnwc00_drm_funcs = {
	.disable = rvt70hsmnwc00_disable,
	.unprepare = rvt70hsmnwc00_unprepare,
	.prepare = rvt70hsmnwc00_prepare,
	.enable = rvt70hsmnwc00_enable,
	.get_modes = rvt70hsmnwc00_get_modes,
};

static int rvt70hsmnwc00_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct rvt70hsmnwc00 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;


	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);

	if (IS_ERR(ctx->reset_gpio))
		ctx->reset_gpio = NULL;
	else
		gpiod_set_value(ctx->reset_gpio, 0);

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_VIDEO_BURST;

	drm_panel_init(&ctx->panel, dev, &rvt70hsmnwc00_drm_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "mipi_dsi_attach() failed: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static int rvt70hsmnwc00_remove(struct mipi_dsi_device *dsi)
{
	struct rvt70hsmnwc00 *ctx = mipi_dsi_get_drvdata(dsi);
	struct device *dev = &dsi->dev;

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id riverdi_rvt70hsmnwc00_of_match[] = {
	{ .compatible = "riverdi,rvt70hsmnwc00" },
	{ }
};
MODULE_DEVICE_TABLE(of, riverdi_rvt70hsmnwc00_of_match);

static struct mipi_dsi_driver riverdi_rvt70hsmnwc00_driver = {
	.probe = rvt70hsmnwc00_probe,
	.remove = rvt70hsmnwc00_remove,
	.driver = {
		.name = "panel-riverdi-rvt70hsmnwc00",
		.of_match_table = riverdi_rvt70hsmnwc00_of_match,
	},
};
module_mipi_dsi_driver(riverdi_rvt70hsmnwc00_driver);

MODULE_AUTHOR("Krzysztof Chojnowski <krzysztof.chojnowski@somlabs.com>");
MODULE_DESCRIPTION("DRM Driver for riverdi RVT70HSMNWC00 MIPI DSI panel");
MODULE_LICENSE("GPL v2");
