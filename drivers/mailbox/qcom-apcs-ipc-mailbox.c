// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017, Linaro Ltd
 */
#define LOG_DEBUG

#include <dm.h>
#include <log.h>
#include <mailbox-uclass.h>
#include <regmap.h>

#define QCOM_APCS_IPC_BITS	32

struct qcom_apcs_ipc {
	struct mbox_chan mbox_chans[QCOM_APCS_IPC_BITS];
	struct regmap *regmap;
	unsigned long offset; /* ipc register offset within global block */
};

static int qcom_apcs_ipc_send(struct mbox_chan *chan, const void *data)
{
	struct qcom_apcs_ipc *apcs = dev_get_priv(chan->dev);
	unsigned long idx = (unsigned long)chan->con_priv;

	return regmap_write(apcs->regmap, apcs->offset, BIT(idx));
}

static int qcom_apcs_ipc_recv(struct mbox_chan *chan, void *data)
{
	/* Linux driver does not have such method */
	return -ENODATA;
}

static int qcom_apcs_ipc_probe(struct udevice *dev)
{
	struct qcom_apcs_ipc *apcs = dev_get_priv(dev);
	struct regmap *regmap;
	unsigned long i;
	int ret;

	ret = regmap_init_mem(dev_ofnode(dev), &regmap);
	if (ret) {
		debug("Could not init regmap (err = %d)\n", ret);
		return ret;
	}

	apcs->regmap = regmap;
	apcs->offset = dev_get_driver_data(dev);

	/* Initialize channel identifiers */
	for (i = 0; i < ARRAY_SIZE(apcs->mbox_chans); i++) {
		apcs->mbox_chans[i].dev = dev;
		apcs->mbox_chans[i].con_priv = (void *)i;
	}

	debug("Probed %s; offset = %lx (regmap width=%d, range cnt: %d: [%lx sz %lx])\n",
		dev->name, apcs->offset, regmap->width, regmap->range_count,
		regmap->ranges[0].start, regmap->ranges[0].size);

	return 0;
}

/* .data is the offset of the ipc register within the global block */
static const struct udevice_id qcom_apcs_ipc_of_match[] = {
	{ .compatible = "qcom,ipq6018-apcs-apps-global", .data = 8 },
	{ .compatible = "qcom,msm8916-apcs-kpss-global", .data = 8 },
	{ .compatible = "qcom,msm8939-apcs-kpss-global", .data = 8 },
	{ .compatible = "qcom,msm8953-apcs-kpss-global", .data = 8 },
	{ .compatible = "qcom,msm8994-apcs-kpss-global", .data = 8 },
	{ .compatible = "qcom,msm8996-apcs-hmss-global", .data = 16 },
	{ .compatible = "qcom,qcm2290-apcs-hmss-global", .data = 8 },
	{ .compatible = "qcom,sdm845-apss-shared", .data = 12 },
	{ .compatible = "qcom,sdx55-apcs-gcc", .data = 0x1008 },
	/* Do not add any more entries using existing driver data */
	{ .compatible = "qcom,msm8976-apcs-kpss-global", .data = 8 },
	{ .compatible = "qcom,msm8998-apcs-hmss-global", .data = 8 },
	{ .compatible = "qcom,qcs404-apcs-apps-global", .data = 8 },
	{ .compatible = "qcom,sdm660-apcs-hmss-global", .data = 8 },
	{ .compatible = "qcom,sm4250-apcs-hmss-global", .data = 8 },
	{ .compatible = "qcom,sm6125-apcs-hmss-global", .data = 8 },
	{ .compatible = "qcom,sm6115-apcs-hmss-global", .data = 8 },
	{ .compatible = "qcom,ipq5332-apcs-apps-global", .data = 8 },
	{ .compatible = "qcom,ipq5424-apcs-apps-global", .data = 8 },
	{ .compatible = "qcom,ipq8074-apcs-apps-global", .data = 8 },
	{ .compatible = "qcom,sc7180-apss-shared", .data = 12 },
	{ .compatible = "qcom,sc8180x-apss-shared", .data = 12 },
	{ .compatible = "qcom,sm8150-apss-shared", .data = 12 },
	{}
};

struct mbox_ops qcom_apcs_mbox_ops = {
	.send = qcom_apcs_ipc_send,
	.recv = qcom_apcs_ipc_recv,
};

U_BOOT_DRIVER(qcom_apcs_ipc) = {
	.name = "qcom_apcs_ipc",
	.id = UCLASS_MAILBOX,
	.of_match = qcom_apcs_ipc_of_match,
	.probe = qcom_apcs_ipc_probe,
	.priv_auto = sizeof(struct qcom_apcs_ipc),
	.ops = &qcom_apcs_mbox_ops,
};

 