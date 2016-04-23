/*
 * Greybus Module code
 *
 * Copyright 2016 Google Inc.
 * Copyright 2016 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"


static ssize_t module_id_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct gb_module *module = to_gb_module(dev);

	return sprintf(buf, "%u\n", module->module_id);
}
static DEVICE_ATTR_RO(module_id);

static ssize_t num_interfaces_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct gb_module *module = to_gb_module(dev);

	return sprintf(buf, "%zu\n", module->num_interfaces);
}
static DEVICE_ATTR_RO(num_interfaces);

static struct attribute *module_attrs[] = {
	&dev_attr_module_id.attr,
	&dev_attr_num_interfaces.attr,
	NULL,
};
ATTRIBUTE_GROUPS(module);

static void gb_module_release(struct device *dev)
{
	struct gb_module *module = to_gb_module(dev);

	kfree(module);
}

struct device_type greybus_module_type = {
	.name		= "greybus_module",
	.release	= gb_module_release,
};

struct gb_module *gb_module_create(struct gb_host_device *hd, u8 module_id,
					size_t num_interfaces)
{
	struct gb_interface *intf;
	struct gb_module *module;
	int i;

	module = kzalloc(sizeof(*module) + num_interfaces * sizeof(intf),
				GFP_KERNEL);
	if (!module)
		return NULL;

	module->hd = hd;
	module->module_id = module_id;
	module->num_interfaces = num_interfaces;

	module->dev.parent = &hd->dev;
	module->dev.bus = &greybus_bus_type;
	module->dev.type = &greybus_module_type;
	module->dev.groups = module_groups;
	module->dev.dma_mask = hd->dev.dma_mask;
	device_initialize(&module->dev);
	dev_set_name(&module->dev, "%d-%u", hd->bus_id, module_id);

	for (i = 0; i < num_interfaces; ++i) {
		intf = gb_interface_create(module, module_id + i);
		if (!intf) {
			dev_err(&module->dev, "failed to create interface %u\n",
					module_id + i);
			goto err_put_interfaces;
		}
		module->interfaces[i] = intf;
	}

	return module;

err_put_interfaces:
	for (--i; i > 0; --i)
		gb_interface_put(module->interfaces[i]);

	put_device(&module->dev);

	return NULL;
}

/*
 * Register and enable an interface after first attempting to activate it.
 */
static void gb_module_register_interface(struct gb_interface *intf)
{
	struct gb_module *module = intf->module;
	u8 intf_id = intf->interface_id;
	int ret;

	ret = gb_interface_activate(intf);
	if (ret) {
		dev_err(&module->dev, "failed to activate interface %u: %d\n",
				intf_id, ret);
		gb_interface_add(intf);
		return;
	}

	ret = gb_interface_add(intf);
	if (ret)
		goto err_interface_deactivate;

	ret = gb_interface_enable(intf);
	if (ret) {
		dev_err(&module->dev, "failed to enable interface %u: %d\n",
				intf_id, ret);
		goto err_interface_deactivate;
	}

	return;

err_interface_deactivate:
	gb_interface_deactivate(intf);
}

static void gb_module_deregister_interface(struct gb_interface *intf)
{
	/* Mark as disconnected to prevent I/O during disable. */
	if (intf->module->disconnected)
		intf->disconnected = true;

	gb_interface_disable(intf);
	gb_interface_deactivate(intf);

	gb_interface_del(intf);
}

/* Register a module and its interfaces. */
int gb_module_add(struct gb_module *module)
{
	size_t i;
	int ret;

	ret = device_add(&module->dev);
	if (ret) {
		dev_err(&module->dev, "failed to register module: %d\n", ret);
		return ret;
	}

	for (i = 0; i < module->num_interfaces; ++i)
		gb_module_register_interface(module->interfaces[i]);

	return 0;
}

/* Deregister a module and its interfaces. */
void gb_module_del(struct gb_module *module)
{
	size_t i;

	for (i = 0; i < module->num_interfaces; ++i)
		gb_module_deregister_interface(module->interfaces[i]);

	device_del(&module->dev);
}

void gb_module_put(struct gb_module *module)
{
	size_t i;

	for (i = 0; i < module->num_interfaces; ++i)
		gb_interface_put(module->interfaces[i]);

	put_device(&module->dev);
}
