// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/io.h>
#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/platform_device.h>

#define ECRAM_BASE 0xFE0B0100
#define ECRAM_LEN  0x100

#define REG_TMP    0x07
#define REG_RPM1   0xD0
#define REG_RPM2   0xD2

static void __iomem *ecram;
static struct platform_device *pdev;

static unsigned int tach_div = 2156220;
module_param(tach_div, uint, 0644);
MODULE_PARM_DESC(tach_div, "tachometer constant: RPM = tach_div / raw");

static bool force;
module_param(force, bool, 0444);
MODULE_PARM_DESC(force, "skip the DMI match");

/* the EC lays the tach out big-endian */
static u16 ec_r16(unsigned int off)
{
	return (ioread8(ecram + off) << 8) | ioread8(ecram + off + 1);
}

static umode_t g5_visible(const void *d, enum hwmon_sensor_types t,
			  u32 attr, int ch)
{
	return 0444;
}

static int g5_read(struct device *dev, enum hwmon_sensor_types type,
		   u32 attr, int channel, long *val)
{
	u16 raw;

	switch (type) {
	case hwmon_fan:
		raw = ec_r16(channel ? REG_RPM2 : REG_RPM1);
		*val = raw ? tach_div / raw : 0;
		return 0;
	case hwmon_temp:
		*val = ioread8(ecram + REG_TMP) * 1000;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_channel_info * const g5_info[] = {
	HWMON_CHANNEL_INFO(fan,  HWMON_F_INPUT, HWMON_F_INPUT),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	NULL
};

static const struct hwmon_ops g5_ops = {
	.is_visible = g5_visible,
	.read = g5_read,
};

static const struct hwmon_chip_info g5_chip = {
	.ops = &g5_ops,
	.info = g5_info,
};

static const struct dmi_system_id g5_dmi[] = {
	{ .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "GIGABYTE"),
		DMI_MATCH(DMI_PRODUCT_NAME, "G5 KD"),
	} },
	{ }
};

static int __init g5_init(void)
{
	struct device *hwmon;

	if (!force && !dmi_check_system(g5_dmi)) {
		pr_info("no DMI match, use force=1 to override\n");
		return -ENODEV;
	}

	ecram = ioremap(ECRAM_BASE, ECRAM_LEN);
	if (!ecram)
		return -ENOMEM;

	pdev = platform_device_register_simple("gigabyte_ec", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		iounmap(ecram);
		return PTR_ERR(pdev);
	}

	hwmon = devm_hwmon_device_register_with_info(&pdev->dev,
			"gigabyte_ec", NULL, &g5_chip, NULL);
	if (IS_ERR(hwmon)) {
		platform_device_unregister(pdev);
		iounmap(ecram);
		return PTR_ERR(hwmon);
	}
	return 0;
}

static void __exit g5_exit(void)
{
	platform_device_unregister(pdev);
	iounmap(ecram);
}

module_init(g5_init);
module_exit(g5_exit);
MODULE_DESCRIPTION("Fan tachometers for Gigabyte G5 KD");
MODULE_LICENSE("GPL");
