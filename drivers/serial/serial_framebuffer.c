// SPDX-License-Identifier: GPL-2.0+
/*
 * Framebuffer UART driver
 *
 * (C) Copyright 2024 Nikroks <nikroksm@mail.ru>
 *
 * Based on uniLoader framebuffer driver
 */

#include "linux/delay.h"
#include <serial.h>
#include <misc.h>
#include <dm.h>
#include <errno.h>
#include "font.h"

#define COL_WIDTH 80 // In characters

DECLARE_GLOBAL_DATA_PTR;

struct framebuffer_serial_data {
	phys_addr_t base;
};

static int screen_width __section(".data") = 1080;
static int screen_height __section(".data") = 1920;
static int curr_x __section(".data") = 0;
static int curr_y __section(".data") = 0;
static int column __section(".data") = 0;
#define COL_WIDTH_PX (FONTW * COL_WIDTH)
#define MAX_COLS (screen_width / COL_WIDTH_PX)

static bool disable = false;

static int framebuffer_serial_setbrg(struct udevice *dev, int baudrate)
{
	return 0;
}

static int framebuffer_serial_getc(struct udevice *dev)
{
	return -EAGAIN;
}

static int framebuffer_serial_pending(struct udevice *dev, bool input)
{
	return 0;
}

static void clear_fb(phys_addr_t _base) {
	char *base = (char*)_base;
	for(int i = 0; i < (screen_width * screen_height * 4); i++) {
		base[i] = 0;
	}
}

static inline void fb_newline(phys_addr_t _base)
{
	curr_y++;
	udelay(1 * 1000);
	for (int y = 0; y < FONTH * 5; y++) {
		phys_addr_t loc = _base + ((curr_y * FONTH + y) * screen_width * 4) + (column * COL_WIDTH_PX * 4);
		if (curr_y * FONTH + y > screen_height) {
			break;
		}
		memset((void *)loc, 0, COL_WIDTH_PX * 4);
	}
}

static void fb_putc(phys_addr_t _base, const char c) {
	if (disable)
		return;

	if(c < 32) {
		if(c == '\n') {
			fb_newline(_base);
			curr_x = 0;
		} else if (c == '\r') {
			curr_x = 0;
		}
		return;
	}

	int ix = font_index(c);
	unsigned char *img = letters[ix];

	for(int y = 0; y < FONTH; y++) {
		unsigned char b = img[y];
		for(int x =0; x < FONTW; x++) {
			uint8_t col = ((b << x) & 0b10000000) > 0 ? 255 : 0;
			char *base = (char*)_base;
			uint32_t col_px = ((curr_x + column * COL_WIDTH) * FONTW + x);
			long int loc = (col_px * 4) + ((curr_y * FONTH + y) * screen_width * 4);
			base[loc] = col;
			base[loc + 1] = col;
			base[loc + 2] = col;
			base[loc + 3] = col;
		}
	}
	if(curr_x + column * COL_WIDTH > screen_width / FONTW) {
		fb_newline(_base);
		curr_x = 0;
	} else if (curr_y > screen_height / FONTH) {
		column++;
		curr_x = 0;
		curr_y = 0;
	} else {
		curr_x++;
	}

	if (column >= MAX_COLS) {
		column = 0;
		//udelay(1 * 500 * 1000);
		//clear_fb(_base);
	}
	return;
}

static int framebuffer_serial_putc(struct udevice *dev, const char ch)
{
	struct framebuffer_serial_data *priv = dev_get_priv(dev);
	fb_putc(priv->base, ch);
	return 0;
}

static int framebuffer_serial_ofdata_to_platdata(struct udevice *dev)
{
	struct framebuffer_serial_data *priv = dev_get_priv(dev);

	// FIXME: uncomment to let actual simplefb driver take over.
	// disable = true;

	priv->base = dev_read_addr(dev);
	if (priv->base == FDT_ADDR_T_NONE)
		return -EINVAL;

	ofnode_read_u32(dev_ofnode(dev), "width", &screen_width);
	ofnode_read_u32(dev_ofnode(dev), "height", &screen_height);

	// clear_fb(priv->base);

	return 0;
}

// Uncomment to bind to simple-framebuffer node to get width/height
// FIXME: we should have a static initcall using the event subsystem and read the properties
// rather than binding to the node, then let the actual simplefb driver takeover.
static const struct udevice_id framebuffer_serial_ids[] = {
	{ .compatible = "simple-framebuffer" },
	// { .compatible = "framebuffer-serial" },
	{ }
};


const struct dm_serial_ops framebuffer_serial_ops = {
	.putc = framebuffer_serial_putc,
	.pending = framebuffer_serial_pending,
	.getc = framebuffer_serial_getc,
	.setbrg = framebuffer_serial_setbrg,
};

U_BOOT_DRIVER(serial_framebuffer) = {
	.name	= "serial_framebuffer",
	.id	= UCLASS_SERIAL,
	.of_match = framebuffer_serial_ids,
	.of_to_plat = framebuffer_serial_ofdata_to_platdata,
	.priv_auto = sizeof(struct framebuffer_serial_data),
	.ops = &framebuffer_serial_ops,
};

#ifdef CONFIG_DEBUG_UART_FRAMEBUFFER

static struct framebuffer_serial_data init_serial_data = {
	.base = CONFIG_VAL(DEBUG_UART_BASE)
};

/* Serial dumb device, to reuse driver code */
static struct udevice init_dev __maybe_unused = {
	.priv_ = &init_serial_data,
};

#include <debug_uart.h>

static inline void _debug_uart_init(void)
{
	clear_fb(CONFIG_VAL(DEBUG_UART_BASE));
}

static inline void _debug_uart_putc(int ch)
{
	fb_putc(CONFIG_VAL(DEBUG_UART_BASE), ch);
}

DEBUG_UART_FUNCS

#endif