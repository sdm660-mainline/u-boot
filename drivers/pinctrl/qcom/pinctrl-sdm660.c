// SPDX-License-Identifier: GPL-2.0+
/*
 * Qualcomm sdm630/660 TLMM pinctrl
 *
 */

#include <dm.h>

#include "pinctrl-qcom.h"

/* reg = <0x03100000 0x400000>, <0x03500000 0x400000>, <0x03900000 0x400000>;
   reg-names = "south", "center", "north"; */

#define TLMM_BASE  0x03100000
#define SOUTH  (0x03100000 - TLMM_BASE)
#define CENTER (0x03500000 - TLMM_BASE)
#define NORTH  (0x03900000 - TLMM_BASE)

#define MAX_PIN_NAME_LEN 32
static char pin_name[MAX_PIN_NAME_LEN] __section(".data");

static const struct pinctrl_function sdm660_pinctrl_functions[] = {
	{ "gpio", 0 },
	{ "blsp_uart2", 3 }, /* gpio4 & gpio5, often used for debug uart */
};

static const unsigned int sdm660_pin_offsets[] = {
	[0] = SOUTH,
	[1] = SOUTH,
	[2] = SOUTH,
	[3] = SOUTH,
	[4] = NORTH,
	[5] = SOUTH,
	[6] = SOUTH,
	[7] = SOUTH,
	[8] = NORTH,
	[9] = NORTH,
	[10] = NORTH,
	[11] = NORTH,
	[12] = NORTH,
	[13] = NORTH,
	[14] = NORTH,
	[15] = NORTH,
	[16] = CENTER,
	[17] = CENTER,
	[18] = CENTER,
	[19] = CENTER,
	[20] = SOUTH,
	[21] = SOUTH,
	[22] = CENTER,
	[23] = CENTER,
	[24] = NORTH,
	[25] = NORTH,
	[26] = NORTH,
	[27] = NORTH,
	[28] = CENTER,
	[29] = CENTER,
	[30] = CENTER,
	[31] = CENTER,
	[32] = SOUTH,
	[33] = SOUTH,
	[34] = SOUTH,
	[35] = SOUTH,
	[36] = SOUTH,
	[37] = SOUTH,
	[38] = SOUTH,
	[39] = SOUTH,
	[40] = SOUTH,
	[41] = SOUTH,
	[42] = SOUTH,
	[43] = SOUTH,
	[44] = SOUTH,
	[45] = SOUTH,
	[46] = SOUTH,
	[47] = SOUTH,
	[48] = SOUTH,
	[49] = SOUTH,
	[50] = SOUTH,
	[51] = SOUTH,
	[52] = SOUTH,
	[53] = NORTH,
	[54] = NORTH,
	[55] = SOUTH,
	[56] = SOUTH,
	[57] = SOUTH,
	[58] = SOUTH,
	[59] = NORTH,
	[60] = NORTH,
	[61] = NORTH,
	[62] = NORTH,
	[63] = NORTH,
	[64] = SOUTH,
	[65] = SOUTH,
	[66] = NORTH,
	[67] = NORTH,
	[68] = NORTH,
	[69] = NORTH,
	[70] = NORTH,
	[71] = NORTH,
	[72] = NORTH,
	[73] = NORTH,
	[74] = NORTH,
	[75] = NORTH,
	[76] = NORTH,
	[77] = NORTH,
	[78] = NORTH,
	[79] = SOUTH,
	[80] = SOUTH,
	[81] = CENTER,
	[82] = CENTER,
	[83] = SOUTH,
	[84] = SOUTH,
	[85] = SOUTH,
	[86] = SOUTH,
	[87] = SOUTH,
	[88] = SOUTH,
	[89] = SOUTH,
	[90] = SOUTH,
	[91] = SOUTH,
	[92] = SOUTH,
	[93] = SOUTH,
	[94] = SOUTH,
	[95] = SOUTH,
	[96] = SOUTH,
	[97] = SOUTH,
	[98] = SOUTH,
	[99] = SOUTH,
	[100] = SOUTH,
	[101] = SOUTH,
	[102] = SOUTH,
	[103] = SOUTH,
	[104] = SOUTH,
	[105] = SOUTH,
	[106] = SOUTH,
	[107] = SOUTH,
	[108] = SOUTH,
	[109] = SOUTH,
	[110] = SOUTH,
	[111] = SOUTH,
	[112] = SOUTH,
	[113] = SOUTH,
	/* Special pins */
	[114] = 0, /* sdc1_clk */
	[115] = 0, /* sdc1_cmd */
	[116] = 0, /* sdc1_data */
	[117] = 0, /* sdc2_clk */
	[118] = 0, /* sdc2_cmd */
	[119] = 0, /* sdc2_data */
	[120] = 0, /* sdc1_rclk */
};

static const char *sdm660_get_function_name(struct udevice *dev, unsigned int selector)
{
	return sdm660_pinctrl_functions[selector].name;
}

static const char *sdm660_get_pin_name(struct udevice *dev, unsigned int selector)
{
	static const char *special_pins_names[] = {
		"sdc1_clk", "sdc1_cmd", "sdc1_data",
		"sdc2_clk", "sdc2_cmd", "sdc2_data",
		"sdc1_rclk"
	};

	if (selector >= 114 && selector <= 120)
		snprintf(pin_name, MAX_PIN_NAME_LEN, special_pins_names[selector - 114]);
	else
		snprintf(pin_name, MAX_PIN_NAME_LEN, "gpio%u", selector);

	return pin_name;
}

static unsigned int sdm660_get_function_mux(__maybe_unused unsigned int pin, unsigned int selector)
{
	return sdm660_pinctrl_functions[selector].val;
}

struct msm_pinctrl_data sdm660_data = {
	.pin_data = {
		.pin_offsets = sdm660_pin_offsets,
		.pin_count = ARRAY_SIZE(sdm660_pin_offsets),
		.special_pins_start = 114,
	},
	.functions_count = ARRAY_SIZE(sdm660_pinctrl_functions),
	.get_function_name = sdm660_get_function_name,
	.get_function_mux = sdm660_get_function_mux,
	.get_pin_name = sdm660_get_pin_name,
};

static const struct udevice_id msm_pinctrl_ids[] = {
	{
		.compatible = "qcom,sdm630-pinctrl",
		.data = (ulong)&sdm660_data
	},
	{
		.compatible = "qcom,sdm660-pinctrl",
		.data = (ulong)&sdm660_data
	},
	{ /* Sentinel */ }
};

U_BOOT_DRIVER(pinctrl_ssdm660) = {
	.name = "pinctrl_sdm660",
	.id = UCLASS_NOP,
	.of_match = msm_pinctrl_ids,
	.ops = &msm_pinctrl_ops,
	.bind = msm_pinctrl_bind,
};
