/*
 * fitpc2_tz.c Thermal zone driver for the FITPC2
 *
 * The parts we access in the PCI config space for 00:0.0 is undocumented.
 * This driver just mimics a bash script written by CompuLab.
 *
 * Copyright (c) 2010, Patrik Jakobsson <patrik.jakobsson@home.se>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/thermal.h>
#include <linux/dmi.h>

#define REG_DELAY	100
#define PCI_PORT5_MCR	0xD0
#define PCI_PORT5_MDR	0xD4
#define TEMP1_MASK	0x000000FF
#define TEMP1_SHIFT	0
#define TEMP2_MASK	0x0000FF00
#define TEMP2_SHIFT	8
#define B0_INIT		0xFFFFFFFF
#define B0_EXIT		0x00000000
#define TEMP_CRIT	119
#define TEMP_TRIP	119

struct pci_dev *pdev;
struct mutex read_mutex;

struct fitpc2_tz {
	int id;
	struct thermal_zone_device *dev;
};

/* We have two sensors to keep track of */
static struct fitpc2_tz *tz[2];

static int __init dmi_check_cb(const struct dmi_system_id *id)
{
	printk(KERN_INFO KBUILD_MODNAME ": found system model '%s'\n",
		   id->ident);

	return 0;
}

static struct dmi_system_id __initdata fitpc2_dmi_table[] = {
	{
		.ident = "SBC-FITPC2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "CompuLab"),
			DMI_MATCH(DMI_PRODUCT_NAME, "SBC-FITPC2"),
			DMI_MATCH(DMI_BOARD_NAME, "SBC-FITPC2"),
		},
		.callback = dmi_check_cb,
	},
	{ },
};

/* Magic conversion */
static int temp_convert(u8 temp)
{
	s32 c1, c2;
	c1 = 1680 * temp * temp / 1000000;
	c2 = 82652 * temp / 100000;

	return c1 - c2 + 127;
}

static int get_temp(struct thermal_zone_device *thermal_zone,
		    unsigned long *temperature)
{
	u32 reg;
	u8 temp1, temp2;

	if (!pdev)
		return -ENODEV;

	mutex_lock(&read_mutex);
	pci_write_config_dword(pdev, PCI_PORT5_MDR, B0_INIT);
	/* Some magic */
	pci_write_config_dword(pdev, PCI_PORT5_MCR, 0xE004B000);

	msleep(REG_DELAY);

	/* Select port 04 and register B1 */
	pci_write_config_dword(pdev, PCI_PORT5_MCR, 0xD004B100);

	pci_read_config_dword(pdev, PCI_PORT5_MDR, &reg);
	pci_write_config_dword(pdev, PCI_PORT5_MDR, B0_EXIT);

	/* Even more magic */
	pci_write_config_dword(pdev, PCI_PORT5_MCR, 0xE004B000);

	mutex_unlock(&read_mutex);

	temp1 = (reg & TEMP1_MASK) >> TEMP1_SHIFT;
	temp2 = (reg & TEMP2_MASK) >> TEMP2_SHIFT;

	if (thermal_zone == tz[0]->dev)
		*temperature = temp_convert(temp1);
	else if (thermal_zone == tz[1]->dev)
		*temperature = temp_convert(temp2);

	return 0;
}

static int get_crit_temp(struct thermal_zone_device *tz, unsigned long *temperature)
{
	*temperature = TEMP_CRIT;

	return 0;
}

static int get_trip_type(struct thermal_zone_device *tz, int trip,
			 enum thermal_trip_type *type)
{
	if (trip == 0)
		*type = THERMAL_TRIP_ACTIVE;

	return 0;
}

static int get_trip_temp(struct thermal_zone_device *tz, int trip,
			 unsigned long *temperature)
{
	if (trip == 0)
		*temperature = TEMP_TRIP;

	return 0;
}

struct thermal_zone_device_ops fitpc2_ops = {
	.get_temp = get_temp,
	.get_crit_temp = get_crit_temp,
	.get_trip_temp = get_trip_temp,
	.get_trip_type = get_trip_type,
};

static int __init fitpc2_init(void)
{
	if (!dmi_check_system(fitpc2_dmi_table))
		return -ENODEV;

	pdev = pci_get_device(PCI_VENDOR_ID_INTEL, 0x8100, NULL);

	mutex_init(&read_mutex);

	tz[0] = kzalloc(sizeof(struct fitpc2_tz), GFP_KERNEL);
	if (!tz[0])
		return -ENOMEM;

	tz[1] = kzalloc(sizeof(struct fitpc2_tz), GFP_KERNEL);
	if (!tz[1])
		return -ENOMEM;

	tz[0]->dev = thermal_zone_device_register("FITPC2-1", 0, NULL,
						  &fitpc2_ops, 0, 0, 0, 0);
	if (!tz[0]->dev)
		return -EBUSY;

	tz[1]->dev = thermal_zone_device_register("FITPC2-2", 0, NULL,
						  &fitpc2_ops, 0, 0, 0, 0);
	if (!tz[1]->dev)
		return -EBUSY;

	return 0;
}

static void __exit fitpc2_exit(void)
{
	thermal_zone_device_unregister(tz[0]->dev);
	thermal_zone_device_unregister(tz[1]->dev);

	mutex_destroy(&read_mutex);

	kfree(tz[0]);
	kfree(tz[1]);

	pci_dev_put(pdev);
}

module_init(fitpc2_init);
module_exit(fitpc2_exit);

MODULE_AUTHOR("Patrik Jakobsson <patrik.jakobsson@home.se>");
MODULE_DESCRIPTION("FITPC2 Thermal zone driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("dmi:*:svnCompuLab:SBC-FITPC2:*:SBC-FITPC2:*");
