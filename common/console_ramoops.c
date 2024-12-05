// SPDX-License-Identifier: GPL-2.0+
/*
 * Serial driver that will log output to pstore-ramoops reserved memory.
 *
 * (C) Copyright 2025 Alexey Minnekhanov <alexeymin@postmarketos.org>
 *
 * Based on Linux pstore ramoops backend.
 */

#include <console.h>
#include <dm.h>
#include <dm/device-internal.h>
#include <dm/lists.h>
#include <errno.h>
#include <log.h>
#include <malloc.h>
#include <mapmem.h>
#include <stdio_dev.h>
#include <time.h>

struct persistent_ram_buffer {
	u32    sig;
	u32    start;
	u32    size;
	u8     data[0];
};

#define PERSISTENT_RAM_SIG (0x43474244) /* DBGC */
#define RAMOOPS_KERNMSG_HDR "===="

enum pstore_type_id {
	PSTORE_TYPE_DMESG	= 0,
	PSTORE_TYPE_MCE		= 1,
	PSTORE_TYPE_CONSOLE	= 2,
	PSTORE_TYPE_FTRACE	= 3,
	PSTORE_TYPE_PPC_RTAS	= 4,
	PSTORE_TYPE_PPC_OF	= 5,
	PSTORE_TYPE_PPC_COMMON	= 6,
	PSTORE_TYPE_PMSG	= 7,
	PSTORE_TYPE_PPC_OPAL	= 8,
	PSTORE_TYPE_UNKNOWN	= 255
};


struct persistent_ram_zone {
	phys_addr_t paddr;
	size_t size;
	struct persistent_ram_buffer *buffer;
	size_t buffer_size;

	//char *old_log;
	//size_t old_log_size;
};

struct console_ramoops_data {
	phys_addr_t base;  /* memory region base */
	u64 region_size;   /* memory region size */

	uint32_t ecc_size;
	uint32_t record_size; /* size of one panic dump recod */
	uint32_t console_size;
	uint32_t ftrace_size;
	uint32_t pmsg_size;

	struct persistent_ram_zone **przs; /* zones for panic dumps */
	struct persistent_ram_zone *cprz;  /* console zone */
	struct persistent_ram_zone *fprz;  /* ftrace zone */
	struct persistent_ram_zone *mprz;  /* pmsg zone */

	uint32_t max_dump_cnt; /* max number panic dumps (number of zones) */
	uint32_t dump_write_cnt; /* how many dumps were written by us */
};

/* increase and wrap the start pointer, returning the old value */
static size_t buffer_start_add(struct persistent_ram_zone *prz, size_t a)
{
	u32 olds, news;

	olds = prz->buffer->start;
	news = olds + a;
	while (unlikely(news >= prz->buffer_size))
		news -= prz->buffer_size;
	prz->buffer->start = news;

	return olds;
}

/* increase the size counter until it hits the max size */
static void buffer_size_add(struct persistent_ram_zone *prz, size_t a)
{
	size_t olds, news;

	olds = prz->buffer->size;
	if (olds == prz->buffer_size)
		return;

	news = olds + a;
	if (news > prz->buffer_size)
		news = prz->buffer_size;
	prz->buffer->size = news;
}

int notrace persistent_ram_write(struct persistent_ram_zone *prz, const void *s, unsigned int count)
{
	int rem;
	int c = count;
	size_t start;

	if (unlikely(c > prz->buffer_size)) {
		s += c - prz->buffer_size;
		c = prz->buffer_size;
	}

	buffer_size_add(prz, c);
	start = buffer_start_add(prz, c);
	rem = prz->buffer_size - start;

	if (unlikely(rem < c)) {
		memcpy(prz->buffer->data + start, s, rem);
		//persistent_ram_update_ecc(prz, start, count);
		s += rem;
		c -= rem;
		start = 0;
	}

	memcpy(prz->buffer->data + start, s, c);
	//persistent_ram_update_ecc(prz, start, count);
	//persistent_ram_update_header_ecc(prz);

	return count;
}

void persistent_ram_zap(struct persistent_ram_zone *prz)
{
	prz->buffer->start = 0;
	prz->buffer->size = 0;
	//persistent_ram_update_header_ecc(prz);
}

void persistent_ram_free(struct persistent_ram_zone *prz)
{
	if (!prz)
		return;

	unmap_sysmem(prz->buffer);
	//persistent_ram_free_old(prz);
	free(prz);
}

struct persistent_ram_zone *persistent_ram_new(phys_addr_t start, size_t size, u32 sig)
{
	struct persistent_ram_zone *prz;

	prz = calloc(sizeof(struct persistent_ram_zone), 1);
	if (!prz) {
		pr_err("failed to allocate persistent ram zone\n");
		goto err;
	}

	prz->paddr = start;
	prz->size = size;
	prz->buffer = map_sysmem(start, size);
	prz->buffer_size = size - sizeof(struct persistent_ram_buffer);

	sig ^= PERSISTENT_RAM_SIG;

	if (prz->buffer->sig == sig) {
		if (prz->buffer->size > prz->buffer_size ||
		    prz->buffer->start > prz->buffer->size)
			log_info("found existing invalid buffer, size %ul, start %ul\n",
				prz->buffer->size, prz->buffer->start);
		else {
			log_info("found existing buffer, size %ul, start %ul\n",
				 prz->buffer->size, prz->buffer->start);
			//persistent_ram_save_old(prz);
			return 0;
		}
	} else {
		pr_debug("no valid data in buffer (sig = 0x%08x)\n",
			 prz->buffer->sig);
	}

	/* Rewind missing or invalid memory area. */
	prz->buffer->sig = sig;
	persistent_ram_zap(prz);

	return prz;
err:
	persistent_ram_free(prz);
	return NULL;
}


static size_t ramoops_write_kmsg_hdr(struct persistent_ram_zone *prz, bool compressed)
{
	size_t len;
	unsigned long tv_sec, tv_usec;
	char hdr[48];

	tv_usec = timer_get_us();
	tv_sec = tv_usec / 1000000;
	tv_usec -= tv_sec * 1000000;

	memset(hdr, '\0', sizeof(hdr));
	snprintf(hdr, sizeof(hdr)-1, RAMOOPS_KERNMSG_HDR "%lu.%lu-%c\n",
		tv_sec, tv_usec, compressed ? 'C' : 'D');
	len = strlen(hdr);
	persistent_ram_write(prz, hdr, len);

	return len;
}

static int notrace ramoops_pstore_write_buf(struct console_ramoops_data *cxt,
					    enum pstore_type_id type,
					    const char *buf,
					    size_t size,
					    bool compressed)
{
	struct persistent_ram_zone *prz;
	size_t hlen;

	if (type == PSTORE_TYPE_CONSOLE) {
		if (!cxt->cprz)
			return -ENOMEM;
		persistent_ram_write(cxt->cprz, buf, size);
		return 0;
	} else if (type == PSTORE_TYPE_FTRACE) {
		if (!cxt->fprz)
			return -ENOMEM;
		persistent_ram_write(cxt->fprz, buf, size);
		return 0;
	} else if (type == PSTORE_TYPE_PMSG) {
		if (!cxt->mprz)
			return -ENOMEM;
		persistent_ram_write(cxt->mprz, buf, size);
		return 0;
	}

	if (type != PSTORE_TYPE_DMESG)
		return -EINVAL;

	if (!cxt->przs)
		return -ENOSPC;

	prz = cxt->przs[cxt->dump_write_cnt];

	hlen = ramoops_write_kmsg_hdr(prz, compressed);
	if (size + hlen > prz->buffer_size)
		size = prz->buffer_size - hlen;
	persistent_ram_write(prz, buf, size);

	cxt->dump_write_cnt = (cxt->dump_write_cnt + 1) % cxt->max_dump_cnt;

	return 0;
}

void notrace ramoops_console_write_buf(struct console_ramoops_data *cxt, const char *buf, size_t size)
{
	ramoops_pstore_write_buf(cxt, PSTORE_TYPE_CONSOLE, buf, size, false);
	//ramoops_pstore_write_buf(cxt, PSTORE_TYPE_DMESG, buf, size, false);
}

static int ramoops_init_dump_zones(struct console_ramoops_data *cxt, phys_addr_t *paddr, size_t dump_mem_sz)
{
	int err = -ENOMEM;
	int i;

	if (!cxt->record_size)
		return 0;

	if (*paddr + dump_mem_sz - cxt->base > cxt->region_size) {
		pr_err("no room for dumps\n");
		return -ENOMEM;
	}

	cxt->max_dump_cnt = dump_mem_sz / cxt->record_size;
	if (!cxt->max_dump_cnt)
		return -ENOMEM;
	cxt->dump_write_cnt = 0;

	cxt->przs = calloc(sizeof(struct persistent_ram_zone *), cxt->max_dump_cnt);
	if (!cxt->przs) {
		pr_err("failed to initialize a prz array for dumps\n");
		goto fail_mem;
	}

	for (i = 0; i < cxt->max_dump_cnt; i++) {
		cxt->przs[i] = persistent_ram_new(*paddr, cxt->record_size, 0);
		if (!cxt->przs[i]) {
			pr_err("failed to request mem region (0x%x @ 0x%llx)\n",
				cxt->record_size, (unsigned long long)*paddr);

			while (i > 0) {
				i--;
				persistent_ram_free(cxt->przs[i]);
			}
			goto fail_prz;
		}
		*paddr += cxt->record_size;
	}

	return 0;
fail_prz:
	free(cxt->przs);
fail_mem:
	cxt->max_dump_cnt = 0;
	return err;
}

static int ramoops_init_zone(struct console_ramoops_data *cxt,
			     struct persistent_ram_zone **prz,
			     phys_addr_t *paddr, size_t sz, u32 sig)
{
	if (!sz)
		return 0;

	if (*paddr + sz - cxt->base > cxt->region_size) {
		pr_err("no room for mem region (0x%zx@0x%llx) in (0x%llx@0x%llx)\n",
			sz, (unsigned long long)*paddr,
			cxt->region_size, (unsigned long long)cxt->base);
		return -ENOMEM;
	}

	*prz = persistent_ram_new(*paddr, sz, sig);
	if (*prz == NULL) {
		pr_err("failed to request mem region (0x%zx@0x%llx)\n",
			sz, (unsigned long long)*paddr);
		return -ENOMEM;
	}

	persistent_ram_zap(*prz);

	*paddr += sz;

	return 0;
}

/* Called to start the device */
static int console_ramoops_start(struct stdio_dev *sdev)
{
	struct udevice *dev = (struct udevice *)sdev->priv;
	struct console_ramoops_data *priv = dev_get_priv(dev);
	phys_addr_t paddr;
	size_t dump_mem_sz;
	int ret;

	paddr = priv->base;
	dump_mem_sz = priv->region_size - priv->console_size - priv->ftrace_size - priv->pmsg_size;

	log_debug("console_ramoops_start: dump_mem_sz = %zu (0x%zx)\n", dump_mem_sz, dump_mem_sz);

	priv->przs = NULL;
	priv->cprz = NULL;
	priv->fprz = NULL;
	priv->mprz = NULL;

	ret = ramoops_init_dump_zones(priv, &paddr, dump_mem_sz);
	if (ret) {
		log_err("%s: failed to init dump zones: %d\n", __func__, ret);
		return ret;
	}
	log_info("%s: initialized oops dump zones\n", __func__);

	ret = ramoops_init_zone(priv, &priv->cprz, &paddr, priv->console_size, 0);
	if (ret)
		goto fail_init_zone;
	log_info("%s: initialized console zone\n", __func__);

	ret = ramoops_init_zone(priv, &priv->fprz, &paddr, priv->ftrace_size, 0);
	if (ret)
		goto fail_init_zone;
	log_info("%s: initialized ftrace zone\n", __func__);

	ret = ramoops_init_zone(priv, &priv->mprz, &paddr, priv->pmsg_size, 0);
	if (ret)
		goto fail_init_zone;
	log_info("%s: initialized pmsg zone\n", __func__);

	log_debug("console_ramoops_start: OK\n");
	return 0;

fail_init_zone:
	if (priv->cprz) {
		free(priv->cprz);
		priv->cprz = NULL;
	}

	if (priv->fprz) {
		free(priv->fprz);
		priv->fprz = NULL;
	}

	if (priv->mprz) {
		free(priv->mprz);
		priv->mprz = NULL;
	}

	log_err("console_ramoops_start: failed to init one of the zones (console, ftrace, pmsg): %d\n", ret);
	return ret;
}

/* Called to stop the device */
static int console_ramoops_stop(struct stdio_dev *sdev)
{
	/* we never stop */
	return 0;
}

/* To put a string (accelerator) */
void console_ramoops_puts(struct stdio_dev *sdev, const char *s)
{
	struct udevice *dev = (struct udevice *)sdev->priv;
	struct console_ramoops_data *cxt = dev_get_priv(dev);
	ramoops_console_write_buf(cxt, s, strlen(s));
	return;
}

/* To put a char */
void console_ramoops_putc(struct stdio_dev *sdev, const char c)
{
	char s[2] = {c, '\0'};
	console_ramoops_puts(sdev, s);
	return;
}

/* We do not support input, but.. just in case */
int console_ramoops_tstc(struct stdio_dev *sdev)
{
	(void)sdev;
	return 0;
}

int console_ramoops_getc(struct stdio_dev *sdev)
{
	(void)sdev;
	return 0;
}

#ifdef CONFIG_CONSOLE_FLUSH_SUPPORT
static void console_ramoops_flush(struct stdio_dev *sdev) { }
#endif

static int console_ramoops_of_to_plat(struct udevice *dev)
{
	struct console_ramoops_data *priv = dev_get_priv(dev);

	priv->base = dev_read_addr_size(dev, &priv->region_size);
	if (priv->base == FDT_ADDR_T_NONE)
		return -EINVAL;

	priv->record_size = dev_read_u32_default(dev, "record-size", 4096);
	priv->console_size = dev_read_u32_default(dev, "console-size", 0);
	priv->ftrace_size = dev_read_u32_default(dev, "ftrace-size", 0);
	priv->pmsg_size = dev_read_u32_default(dev, "pmsg-size", 0);
	priv->ecc_size = dev_read_u32_default(dev, "ecc-size", 0);

	/* validity checks */
	if (priv->region_size < (priv->record_size + priv->console_size
		+ priv->ftrace_size + priv->pmsg_size + priv->ecc_size)) {
		log_err("ramoops region size is not enough to hold all parts!\n");
		return -EINVAL;
	}

	if (priv->ecc_size) {
		log_warning("ramoops: we do not support ecc\n");
	}

	return 0;
}

static int console_ramoops_probe(struct udevice *dev)
{
	struct console_ramoops_data *priv = dev_get_priv(dev);
	struct stdio_dev sdev;

	/* register stdio device */
	memset (&sdev, 0, sizeof(sdev));
	sdev.flags = DEV_FLAGS_OUTPUT | DEV_FLAGS_DM;
	sdev.priv = dev; /* as specified by DEV_FLAGS_DM, priv is struct udevice */
	strcpy(sdev.name, "ramoops");
	sdev.start = console_ramoops_start;
	sdev.stop = console_ramoops_stop;
	sdev.putc = console_ramoops_putc;
	sdev.puts = console_ramoops_puts;
	STDIO_DEV_ASSIGN_FLUSH(&sdev, console_ramoops_flush);

	stdio_register(&sdev);

	log_info("%s OK, @ 0x%llx, size 0x%llx\n", __func__, priv->base, priv->region_size);
	return 0;
}

static const struct udevice_id console_ramoops_compatibles[] = {
	{ .compatible = "ramoops" },
	{ }
};

/*
 * Here we pretend to be a keyboard uclass device, so that
 * stdio_add_devices() will find us and call our .probe()
 * so we can register our stdio device.
 */

U_BOOT_DRIVER(console_ramoops) = {
	.name           = "console_ramoops",
	.id             = UCLASS_KEYBOARD,
	.of_match       = console_ramoops_compatibles,
	.of_to_plat     = console_ramoops_of_to_plat,
	.priv_auto	= sizeof(struct console_ramoops_data),
	.probe          = console_ramoops_probe,
	.flags          = 0,
};

#ifdef CONFIG_DEBUG_UART_RAMOOPS
#include <debug_uart.h>

/* values from xiaomi-clover downstream TWRP DT */
/* TODO: thes should be kconfig options */
#define RAMOOPS_BASE 0x9fe00000
#define RAMOOPS_SIZE 0x100000
#define CONSOLE_SIZE 0x80000
#define FTRACE_SIZE  0x1000
#define RECORD_SIZE  0x1000
#define PMSG_SIZE    0x8000

/* The size of the region reserved for panic/oops dumps */
#define DUMPS_SIZE    (RAMOOPS_SIZE - CONSOLE_SIZE - FTRACE_SIZE - PMSG_SIZE)
#define CONSOLE_BASE  (RAMOOPS_BASE + DUMPS_SIZE)

#define PERSISTENT_RAM_SIG (0x43474244) /* DBGC */

#define CONSOLE_BUFFER_SIZE (CONSOLE_SIZE - sizeof(struct persistent_ram_buffer))

static struct persistent_ram_buffer *console_zone;
static char *write_offset;

static inline void _debug_uart_init(void)
{
	console_zone = (struct persistent_ram_buffer *)CONSOLE_BASE;
	console_zone->sig = PERSISTENT_RAM_SIG;
	console_zone->start = 0;
	console_zone->size = 0;

	write_offset = (char *)console_zone->data;
}

static inline void _debug_uart_putc(int ch)
{
	// put the character
	(*write_offset) = (char)( ch & 0xff );

	// ring buffer size can grow, but now above maximum size
	if (console_zone->size < CONSOLE_BUFFER_SIZE)
		console_zone->size++;

	// advance the start of the ring buffer
	console_zone->start++;
	// if it goes past the end of the buffer, rewind it to the begining
	if (console_zone->start >=CONSOLE_BUFFER_SIZE)
		console_zone->start = 0;

	// next write will happen in the buffer at offset `start`
	write_offset = (char *)( console_zone->data + console_zone->start );
}

DEBUG_UART_FUNCS

#endif
