// SPDX-License-Identifier: GPL-2.0+

#define LOG_CATEGORY LOGC_BOARD
#define pr_fmt(fmt) "QCOM: " fmt

#include <asm/global_data.h>
#include <asm/io.h>
#include <command.h>
#include <dm/of_access.h>
#include <dm/device.h>
#include <fdt_support.h>
#include <log.h>
#include <power/pmic.h>

DECLARE_GLOBAL_DATA_PTR;

/* SDM630 GPU speedbin */
#define SDM630_QFPROM_BASE				0x780000	/* efuses block */
#define SDM630_QFPROM_CORR_FEAT_CONFIG_ROW0_LSB		0x41a0		/* contains GPU speedbin efuse */
#define SDM630_QFPROM_CFC_GFX3D_FREQ_LIMIT_MASK		GENMASK(28, 21)	/* should be 0x1fe00000 */
/* SDM630 PMIC LCDB regulator*/
#define SDM630_LCDB_BASE				0xec00
#define SDM630_LCDB_ENABLE_CTL1_REG			0x46
#    define LCDB_MODULE_EN_BIT				BIT(7)		/* 0x80 */
#    define LCDB_HWEN_RDY_BIT				BIT(6)		/* 0x40 */
#define SDM630_LCDB_BST_OUTPUT_VOLTAGE			0x41
#define SDM630_LCDB_LDO_OUTPUT_VOLTAGE			0x71
#define SDM630_LCDB_NCP_OUTPUT_VOLTAGE			0x81
#define SDM630_LCDB_SET_OUTPUT_VOLTAGE_MASK		GENMASK(4, 0)	/* 0x1f */
#define SDM630_LCDB_MIN_BST_VOLTAGE_MV			4700
#define SDM630_LCDB_VOLTAGE_MIN_STEP_100_MV		4000
#define SDM630_LCDB_VOLTAGE_MIN_STEP_50_MV		4950
#define SDM630_LCDB_VOLTAGE_STEP_100_MV			100
#define SDM630_LCDB_VOLTAGE_STEP_50_MV			50
#define SDM630_LCDB_VOLTAGE_STEP_50MV_OFFSET		0xA		/* threshold that changes voltage formula */

enum SUPPORTED_SOCS {
	SOC_UNKNOWN,
	SDM630, /* covers all sdm630/636/660 */

	NUM_SUPPORTED_SOCS /*keep this last */
};

static enum SUPPORTED_SOCS detect_soc(void)
{
	/* TODO: maybe some other method of SoC detection? */
	/* check control FDT's root compatible */
#if CONFIG_IS_ENABLED(OF_LIVE)
	if (of_device_is_compatible(gd->of_root, "qcom,sdm630", NULL, NULL)
		|| of_device_is_compatible(gd->of_root, "qcom,sdm636", NULL, NULL)
		|| of_device_is_compatible(gd->of_root, "qcom,sdm660", NULL, NULL))
		return SDM630;
#else
# error "Not supported when CONFIG_OF_LIVE is not enabled"
	const void *fdt = gd->fdt_blob;
	int offset = fdt_first_subnode(fdt, 0);
	if (offset == FDT_ERR_NOTFOUND) {
		log_err("Cannot find root compatible\n");
		return SOC_UNKNOWN;
	}
#endif

	return SOC_UNKNOWN;
}

struct soc_dump_funcs {
	int (*dump_efuses)(void);
	int (*dump_regulators)(void);
};

static int dump_efuses_sdm630(void)
{
	/* GFX3D_FREQ_LIMIT_VAL BIT[28:21] */
	const u32 efuse = readl(SDM630_QFPROM_BASE + SDM630_QFPROM_CORR_FEAT_CONFIG_ROW0_LSB);
	const u32 gpu_speedbin = (efuse & SDM630_QFPROM_CFC_GFX3D_FREQ_LIMIT_MASK) >> 21;

	printf("GPU speedbin value: %d (0x%x)\n", gpu_speedbin, gpu_speedbin);

	/* TODO: also dump CPU CPR3/4/h bins maybe? */

	return CMD_RET_SUCCESS;
}

static int sdm630_lcdb_reg_to_voltage(int regval)
{
	int voltage;
	if (regval >= SDM630_LCDB_VOLTAGE_STEP_50MV_OFFSET) {
		/* at and above this threshold step is 50 mV */
		voltage = SDM630_LCDB_VOLTAGE_MIN_STEP_50_MV + ((regval - SDM630_LCDB_VOLTAGE_STEP_50MV_OFFSET) * SDM630_LCDB_VOLTAGE_STEP_50_MV);
	} else {
		/* below this threshold step is 100 mV */
		voltage = SDM630_LCDB_VOLTAGE_MIN_STEP_100_MV + (regval * SDM630_LCDB_VOLTAGE_STEP_100_MV);
	}
	return voltage;
}

static int dump_regulators_sdm630(void)
{
	struct udevice *pmic_dev;
	int ret, reg, enable_reg;
	int vol, bst_voltage_reg, ldo_voltage_reg, ncp_voltage_reg;

	/*
	 * To dump LCDB LDO/NCP voltages and status, we need to replicate
	 * the following set of U-Boot shell commands, but in C:
	 *  - pmic dev pmic@3
	 *  - pmic read 0xec46   LCDB enable status
	 *  - pmic read 0xec41   LCDB boost
	 *  - pmic read 0xec71   LCDB LDO
	 *  - pmic read 0xec81   LCDB NCP
	 */

	ret = pmic_get("pmic@3", &pmic_dev);
	if (ret) {
		log_err("Failed to get PMIC!\n");
		return CMD_RET_FAILURE;
	}

	reg = SDM630_LCDB_BASE + SDM630_LCDB_ENABLE_CTL1_REG;
	enable_reg = pmic_reg_read(pmic_dev, reg);
	if (enable_reg < 0) {
		log_err("Failed to read PMIC register: %d: %d\n", reg, enable_reg);
		return CMD_RET_FAILURE;
	}
	if (enable_reg & LCDB_MODULE_EN_BIT) {
		printf("LCDB status: ON\n");
	} else {
		printf("LCDB status: OFF\n");
	}

	reg = SDM630_LCDB_BASE + SDM630_LCDB_BST_OUTPUT_VOLTAGE;
	bst_voltage_reg = pmic_reg_read(pmic_dev, reg);
	if (bst_voltage_reg < 0) {
		log_err("Failed to read PMIC register: %d: %d\n", reg, bst_voltage_reg);
		return CMD_RET_FAILURE;
	}
	bst_voltage_reg &= SDM630_LCDB_SET_OUTPUT_VOLTAGE_MASK;

	reg = SDM630_LCDB_BASE + SDM630_LCDB_LDO_OUTPUT_VOLTAGE;
	ldo_voltage_reg = pmic_reg_read(pmic_dev, reg);
	if (ldo_voltage_reg < 0) {
		log_err("Failed to read PMIC register: %d: %d\n", reg, ldo_voltage_reg);
		return CMD_RET_FAILURE;
	}
	ldo_voltage_reg &= SDM630_LCDB_SET_OUTPUT_VOLTAGE_MASK;

	reg = SDM630_LCDB_BASE + SDM630_LCDB_NCP_OUTPUT_VOLTAGE;
	ncp_voltage_reg = pmic_reg_read(pmic_dev, reg);
	if (ncp_voltage_reg < 0) {
		log_err("Failed to read PMIC register: %d: %d\n", reg, ncp_voltage_reg);
		return CMD_RET_FAILURE;
	}
	ncp_voltage_reg &= SDM630_LCDB_SET_OUTPUT_VOLTAGE_MASK;

	/* LCDB BOOST voltage formula: (regval * VOLTAGE_STEP_50_MV) + MIN_BST_VOLTAGE_MV */
	vol = bst_voltage_reg * SDM630_LCDB_VOLTAGE_STEP_50_MV + SDM630_LCDB_MIN_BST_VOLTAGE_MV;
	printf("LCDB boost voltage: %d mV (raw value 0x%x)\n", vol, bst_voltage_reg);

	/* LCDB LDO & NCP have the same voltage formula */
	vol = sdm630_lcdb_reg_to_voltage(ldo_voltage_reg);
	printf("LCDB LDO voltage: %d mV (raw value 0x%x)\n", vol, ldo_voltage_reg);
	vol = sdm630_lcdb_reg_to_voltage(ncp_voltage_reg);
	printf("LCDB NCP voltage: %d mV (raw value 0x%x)\n", vol, ncp_voltage_reg);

	return CMD_RET_SUCCESS;
}

struct soc_dump_funcs soc_funcs[NUM_SUPPORTED_SOCS] = {
	[SDM630] = {
		.dump_efuses = dump_efuses_sdm630,
		.dump_regulators = dump_regulators_sdm630,
	},
};

int do_dump_efuses(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	enum SUPPORTED_SOCS soc = detect_soc();
	if (soc == SOC_UNKNOWN) {
		log_err("Failed to detect SoC!\n");
		return CMD_RET_FAILURE;
	}
	return soc_funcs[soc].dump_efuses();
}

int do_dump_regulators(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	enum SUPPORTED_SOCS soc = detect_soc();
	if (soc == SOC_UNKNOWN) {
		log_err("Failed to detect SoC!\n");
		return CMD_RET_FAILURE;
	}
	return soc_funcs[soc].dump_regulators();
}

/* TODO: add arguments to support more specific fuses maybe? */
U_BOOT_CMD(
	dump_efuses, CONFIG_SYS_MAXARGS, 0, do_dump_efuses,
	"Dump SoC EFUSES contents in a nice readable way",
	"dump_efuses\n"
	" - no arguments\n"
);

/* TODO: add arguments to support more specific fuses maybe? */
U_BOOT_CMD(
	dump_regulators, CONFIG_SYS_MAXARGS, 0, do_dump_regulators,
	"Dump some regulators stats",
	"dump_regulators\n"
	" - no arguments\n"
);