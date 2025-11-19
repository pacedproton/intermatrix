// SPDX-License-Identifier: GPL-2.0
/*
 * CXL Fabric Interconnect - Switch Management
 *
 * Copyright (C) 2025 InterMatrix Project
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/cxl/cxl_fabric.h>

/*
 * Shim: Generic switch operations (simulated hardware)
 */
static int cxl_switch_shim_init(struct cxl_switch *sw)
{
	pr_debug("CXL Switch: Initializing switch%u (SHIM)\n", sw->switch_id);
	return 0;
}

static void cxl_switch_shim_cleanup(struct cxl_switch *sw)
{
	pr_debug("CXL Switch: Cleaning up switch%u (SHIM)\n", sw->switch_id);
}

static int cxl_switch_shim_enable_port(struct cxl_port *port)
{
	port->state = CXL_PORT_UP;
	pr_debug("CXL Switch: Port %u enabled\n", port->port_id);
	return 0;
}

static int cxl_switch_shim_disable_port(struct cxl_port *port)
{
	port->state = CXL_PORT_DOWN;
	pr_debug("CXL Switch: Port %u disabled\n", port->port_id);
	return 0;
}

static int cxl_switch_shim_program_route(struct cxl_switch *sw, u32 dst_id, u16 port)
{
	if (port >= sw->num_ports)
		return -EINVAL;

	spin_lock(&sw->route_lock);
	sw->route_table->port_map[dst_id] = port;
	spin_unlock(&sw->route_lock);

	pr_debug("CXL Switch: Programmed route dst=%u -> port=%u\n", dst_id, port);
	return 0;
}

static int cxl_switch_shim_read_counters(struct cxl_switch *sw,
					struct cxl_switch_counters *counters)
{
	memcpy(counters, &sw->counters, sizeof(*counters));
	return 0;
}

static int cxl_switch_shim_set_qos(struct cxl_port *port,
				  const struct cxl_qos_params *qos)
{
	memcpy(&port->qos, qos, sizeof(*qos));
	pr_debug("CXL Switch: QoS set on port %u (priority=%u)\n",
		 port->port_id, qos->priority);
	return 0;
}

static const struct cxl_switch_ops cxl_switch_shim_ops = {
	.init = cxl_switch_shim_init,
	.cleanup = cxl_switch_shim_cleanup,
	.enable_port = cxl_switch_shim_enable_port,
	.disable_port = cxl_switch_shim_disable_port,
	.program_route = cxl_switch_shim_program_route,
	.read_counters = cxl_switch_shim_read_counters,
	.set_qos = cxl_switch_shim_set_qos,
};

/*
 * Port operations
 */
int cxl_port_enable(struct cxl_port *port)
{
	struct cxl_switch *sw = port->parent_switch;

	if (!sw || !sw->ops || !sw->ops->enable_port)
		return -EINVAL;

	return sw->ops->enable_port(port);
}
EXPORT_SYMBOL_GPL(cxl_port_enable);

int cxl_port_disable(struct cxl_port *port)
{
	struct cxl_switch *sw = port->parent_switch;

	if (!sw || !sw->ops || !sw->ops->disable_port)
		return -EINVAL;

	return sw->ops->disable_port(port);
}
EXPORT_SYMBOL_GPL(cxl_port_disable);

int cxl_port_connect(struct cxl_port *port, void *target)
{
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	port->connected = target;
	spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(cxl_port_connect);

/*
 * Switch sysfs attributes
 */
static ssize_t switch_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct cxl_switch *sw = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", sw->switch_id);
}
static DEVICE_ATTR_RO(switch_id);

static ssize_t vendor_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct cxl_switch *sw = dev_get_drvdata(dev);
	return sprintf(buf, "0x%04x\n", sw->vendor_id);
}
static DEVICE_ATTR_RO(vendor_id);

static ssize_t device_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct cxl_switch *sw = dev_get_drvdata(dev);
	return sprintf(buf, "0x%04x\n", sw->device_id);
}
static DEVICE_ATTR_RO(device_id);

static ssize_t num_ports_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct cxl_switch *sw = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", sw->num_ports);
}
static DEVICE_ATTR_RO(num_ports);

static struct attribute *cxl_switch_attrs[] = {
	&dev_attr_switch_id.attr,
	&dev_attr_vendor_id.attr,
	&dev_attr_device_id.attr,
	&dev_attr_num_ports.attr,
	NULL,
};
ATTRIBUTE_GROUPS(cxl_switch);

/*
 * Switch management
 */
struct cxl_switch *cxl_switch_create(struct cxl_fabric *fabric, u16 num_ports)
{
	struct cxl_switch *sw;
	int i, ret;

	if (num_ports > CXL_MAX_PORTS_PER_SWITCH)
		return ERR_PTR(-EINVAL);

	sw = kzalloc(sizeof(*sw), GFP_KERNEL);
	if (!sw)
		return ERR_PTR(-ENOMEM);

	sw->num_ports = num_ports;
	sw->fabric = fabric;

	/* Allocate ports */
	sw->ports = kcalloc(num_ports, sizeof(struct cxl_port), GFP_KERNEL);
	if (!sw->ports) {
		kfree(sw);
		return ERR_PTR(-ENOMEM);
	}

	/* Initialize ports */
	for (i = 0; i < num_ports; i++) {
		sw->ports[i].port_id = i;
		sw->ports[i].parent_switch = sw;
		sw->ports[i].state = CXL_PORT_DOWN;
		sw->ports[i].link.link_type = CXL_LINK_COPPER;
		spin_lock_init(&sw->ports[i].lock);
	}

	/* Allocate routing table */
	sw->route_table = kzalloc(sizeof(*sw->route_table), GFP_KERNEL);
	if (!sw->route_table) {
		kfree(sw->ports);
		kfree(sw);
		return ERR_PTR(-ENOMEM);
	}

	/* Initialize locks */
	spin_lock_init(&sw->route_lock);
	mutex_init(&sw->ops_lock);

	/* Set default operations (shim) */
	sw->ops = &cxl_switch_shim_ops;

	/* Initialize device */
	device_initialize(&sw->dev);
	sw->dev.class = fabric->dev.class;
	sw->dev.parent = &fabric->dev;
	sw->dev.groups = cxl_switch_groups;
	dev_set_drvdata(&sw->dev, sw);

	/* Allocate switch ID */
	ret = ida_simple_get(&fabric->switch_ida, 0, CXL_MAX_SWITCHES, GFP_KERNEL);
	if (ret < 0) {
		kfree(sw->route_table);
		kfree(sw->ports);
		kfree(sw);
		return ERR_PTR(ret);
	}
	sw->switch_id = ret;

	dev_set_name(&sw->dev, "switch%u", sw->switch_id);

	/* Call switch init */
	if (sw->ops && sw->ops->init) {
		ret = sw->ops->init(sw);
		if (ret) {
			ida_simple_remove(&fabric->switch_ida, sw->switch_id);
			kfree(sw->route_table);
			kfree(sw->ports);
			kfree(sw);
			return ERR_PTR(ret);
		}
	}

	pr_debug("CXL Switch: Created switch%u with %u ports\n",
		 sw->switch_id, num_ports);

	return sw;
}
EXPORT_SYMBOL_GPL(cxl_switch_create);

void cxl_switch_destroy(struct cxl_switch *sw)
{
	if (!sw)
		return;

	pr_debug("CXL Switch: Destroying switch%u\n", sw->switch_id);

	/* Call cleanup */
	if (sw->ops && sw->ops->cleanup)
		sw->ops->cleanup(sw);

	/* Remove from fabric if registered */
	if (device_is_registered(&sw->dev)) {
		device_unregister(&sw->dev);
	}

	/* Free resources */
	if (sw->fabric)
		ida_simple_remove(&sw->fabric->switch_ida, sw->switch_id);

	kfree(sw->route_table);
	kfree(sw->ports);
	kfree(sw);
}
EXPORT_SYMBOL_GPL(cxl_switch_destroy);

int cxl_switch_add_to_fabric(struct cxl_fabric *fabric, struct cxl_switch *sw)
{
	int ret;

	mutex_lock(&fabric->fabric_lock);

	/* Register device */
	ret = device_add(&sw->dev);
	if (ret) {
		mutex_unlock(&fabric->fabric_lock);
		return ret;
	}

	/* Add to fabric list */
	list_add_tail(&sw->fabric_node, &fabric->switches);
	fabric->num_switches++;

	mutex_unlock(&fabric->fabric_lock);

	pr_info("CXL Switch: Added switch%u to fabric%u\n",
		sw->switch_id, fabric->fabric_id);

	return 0;
}
EXPORT_SYMBOL_GPL(cxl_switch_add_to_fabric);
