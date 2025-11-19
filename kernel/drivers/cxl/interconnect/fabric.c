// SPDX-License-Identifier: GPL-2.0
/*
 * CXL Fabric Interconnect - Core Fabric Management
 *
 * Copyright (C) 2025 InterMatrix Project
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cxl/cxl_fabric.h>

#define CXL_FABRIC_NAME "cxl_fabric"
#define CXL_FABRIC_CLASS_NAME "cxl_fabric"

/* Global state */
LIST_HEAD(cxl_fabric_list);
DEFINE_MUTEX(cxl_fabric_list_lock);

static struct class *cxl_fabric_class;
static dev_t cxl_fabric_devt;
static DEFINE_IDA(cxl_fabric_ida);

/* Shim: Simulated hardware state */
struct cxl_hw_shim {
	void *simulated_memory;
	size_t memory_size;
	bool initialized;
};

static struct cxl_hw_shim hw_shim;

/*
 * Fabric sysfs attributes
 */
static ssize_t fabric_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct cxl_fabric *fabric = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", fabric->fabric_id);
}
static DEVICE_ATTR_RO(fabric_id);

static ssize_t num_switches_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct cxl_fabric *fabric = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", fabric->num_switches);
}
static DEVICE_ATTR_RO(num_switches);

static ssize_t num_endpoints_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct cxl_fabric *fabric = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", fabric->num_endpoints);
}
static DEVICE_ATTR_RO(num_endpoints);

static ssize_t generation_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct cxl_fabric *fabric = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", atomic_read(&fabric->generation));
}
static DEVICE_ATTR_RO(generation);

static struct attribute *cxl_fabric_attrs[] = {
	&dev_attr_fabric_id.attr,
	&dev_attr_num_switches.attr,
	&dev_attr_num_endpoints.attr,
	&dev_attr_generation.attr,
	NULL,
};
ATTRIBUTE_GROUPS(cxl_fabric);

/*
 * Fabric device file operations
 */
static int cxl_fabric_open(struct inode *inode, struct file *filp)
{
	struct cxl_fabric *fabric;

	fabric = container_of(inode->i_cdev, struct cxl_fabric, cdev);
	filp->private_data = fabric;

	return 0;
}

static int cxl_fabric_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static long cxl_fabric_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	/* IOCTL implementation will be added for userspace control */
	return -ENOTTY;
}

static const struct file_operations cxl_fabric_fops = {
	.owner = THIS_MODULE,
	.open = cxl_fabric_open,
	.release = cxl_fabric_release,
	.unlocked_ioctl = cxl_fabric_ioctl,
	.compat_ioctl = cxl_fabric_ioctl,
};

/*
 * Fabric management
 */
struct cxl_fabric *cxl_fabric_find(u32 fabric_id)
{
	struct cxl_fabric *fabric;

	mutex_lock(&cxl_fabric_list_lock);
	list_for_each_entry(fabric, &cxl_fabric_list, dev.bus_list) {
		if (fabric->fabric_id == fabric_id) {
			mutex_unlock(&cxl_fabric_list_lock);
			return fabric;
		}
	}
	mutex_unlock(&cxl_fabric_list_lock);

	return NULL;
}
EXPORT_SYMBOL_GPL(cxl_fabric_find);

struct cxl_fabric *cxl_fabric_create(u32 fabric_id)
{
	struct cxl_fabric *fabric;
	int ret;

	fabric = kzalloc(sizeof(*fabric), GFP_KERNEL);
	if (!fabric)
		return ERR_PTR(-ENOMEM);

	fabric->fabric_id = fabric_id;
	INIT_LIST_HEAD(&fabric->switches);
	INIT_LIST_HEAD(&fabric->endpoints);
	mutex_init(&fabric->route_lock);
	mutex_init(&fabric->fabric_lock);
	ida_init(&fabric->endpoint_ida);
	ida_init(&fabric->switch_ida);
	idr_init(&fabric->route_idr);
	atomic_set(&fabric->generation, 0);

	/* Initialize statistics */
	atomic64_set(&fabric->stats.total_bytes_transferred, 0);
	atomic64_set(&fabric->stats.total_operations, 0);
	atomic_set(&fabric->stats.active_regions, 0);
	atomic_set(&fabric->stats.route_cache_hits, 0);
	atomic_set(&fabric->stats.route_cache_misses, 0);

	/* Create device */
	fabric->dev.class = cxl_fabric_class;
	fabric->dev.groups = cxl_fabric_groups;
	dev_set_name(&fabric->dev, "fabric%u", fabric_id);
	dev_set_drvdata(&fabric->dev, fabric);

	ret = device_register(&fabric->dev);
	if (ret) {
		put_device(&fabric->dev);
		return ERR_PTR(ret);
	}

	/* Allocate character device */
	ret = alloc_chrdev_region(&fabric->devt, 0, 1, dev_name(&fabric->dev));
	if (ret) {
		device_unregister(&fabric->dev);
		return ERR_PTR(ret);
	}

	cdev_init(&fabric->cdev, &cxl_fabric_fops);
	fabric->cdev.owner = THIS_MODULE;
	ret = cdev_add(&fabric->cdev, fabric->devt, 1);
	if (ret) {
		unregister_chrdev_region(fabric->devt, 1);
		device_unregister(&fabric->dev);
		return ERR_PTR(ret);
	}

	/* Create hotplug workqueue */
	fabric->hotplug_wq = alloc_workqueue("cxl_fabric_%u", WQ_MEM_RECLAIM, 0,
					     fabric_id);
	if (!fabric->hotplug_wq) {
		cdev_del(&fabric->cdev);
		unregister_chrdev_region(fabric->devt, 1);
		device_unregister(&fabric->dev);
		return ERR_PTR(-ENOMEM);
	}

	/* Add to global list */
	mutex_lock(&cxl_fabric_list_lock);
	list_add_tail(&fabric->dev.bus_list, &cxl_fabric_list);
	mutex_unlock(&cxl_fabric_list_lock);

	pr_info("CXL Fabric: Created fabric%u\n", fabric_id);

	return fabric;
}
EXPORT_SYMBOL_GPL(cxl_fabric_create);

void cxl_fabric_destroy(struct cxl_fabric *fabric)
{
	struct cxl_switch *sw, *sw_tmp;
	struct cxl_endpoint *ep, *ep_tmp;

	if (!fabric)
		return;

	pr_info("CXL Fabric: Destroying fabric%u\n", fabric->fabric_id);

	/* Remove from global list */
	mutex_lock(&cxl_fabric_list_lock);
	list_del(&fabric->dev.bus_list);
	mutex_unlock(&cxl_fabric_list_lock);

	/* Destroy workqueue */
	if (fabric->hotplug_wq)
		destroy_workqueue(fabric->hotplug_wq);

	/* Remove all endpoints */
	list_for_each_entry_safe(ep, ep_tmp, &fabric->endpoints, fabric_node) {
		cxl_endpoint_destroy(ep);
	}

	/* Remove all switches */
	list_for_each_entry_safe(sw, sw_tmp, &fabric->switches, fabric_node) {
		cxl_switch_destroy(sw);
	}

	/* Free topology */
	if (fabric->topology) {
		if (fabric->topology->adj_matrix) {
			int i;
			for (i = 0; i < fabric->topology->num_vertices; i++)
				kfree(fabric->topology->adj_matrix[i]);
			kfree(fabric->topology->adj_matrix);
		}
		kfree(fabric->topology);
	}

	/* Cleanup IDR/IDA */
	idr_destroy(&fabric->route_idr);
	ida_destroy(&fabric->endpoint_ida);
	ida_destroy(&fabric->switch_ida);

	/* Remove character device */
	cdev_del(&fabric->cdev);
	unregister_chrdev_region(fabric->devt, 1);

	/* Unregister device */
	device_unregister(&fabric->dev);
}
EXPORT_SYMBOL_GPL(cxl_fabric_destroy);

/*
 * Shim: Simulated fabric scan
 * In real hardware, this would enumerate via ACPI CEDT and PCIe scanning
 */
int cxl_fabric_scan(struct cxl_fabric *fabric)
{
	struct cxl_switch *sw0, *sw1;
	struct cxl_endpoint *ep0, *ep1, *ep2, *ep3;
	int ret;

	pr_info("CXL Fabric: Scanning fabric%u (SHIM mode)\n", fabric->fabric_id);

	/* Create simulated topology: 2 switches, 4 endpoints */

	/* Switch 0 - 8 ports */
	sw0 = cxl_switch_create(fabric, 8);
	if (IS_ERR(sw0))
		return PTR_ERR(sw0);

	sw0->vendor_id = 0x14E4;  /* Broadcom */
	sw0->device_id = 0xB001;
	sw0->caps.max_bandwidth_gbps = 128;
	sw0->caps.supports_multicast = true;
	sw0->caps.supports_qos = true;
	sw0->caps.max_virtual_channels = 8;

	ret = cxl_switch_add_to_fabric(fabric, sw0);
	if (ret) {
		cxl_switch_destroy(sw0);
		return ret;
	}

	/* Switch 1 - 8 ports */
	sw1 = cxl_switch_create(fabric, 8);
	if (IS_ERR(sw1))
		return PTR_ERR(sw1);

	sw1->vendor_id = 0x11F8;  /* Microchip */
	sw1->device_id = 0x8556;
	sw1->caps.max_bandwidth_gbps = 128;
	sw1->caps.supports_multicast = true;
	sw1->caps.supports_qos = true;
	sw1->caps.max_virtual_channels = 8;

	ret = cxl_switch_add_to_fabric(fabric, sw1);
	if (ret) {
		cxl_switch_destroy(sw1);
		return ret;
	}

	/* Connect switches together (sw0:port0 <-> sw1:port0) */
	cxl_port_connect(&sw0->ports[0], sw1);
	cxl_port_connect(&sw1->ports[0], sw0);
	sw0->ports[0].type = CXL_PORT_DOWNSTREAM;
	sw1->ports[0].type = CXL_PORT_UPSTREAM;
	sw0->ports[0].link.speed_gbps = 128;
	sw1->ports[0].link.speed_gbps = 128;
	sw0->ports[0].link.latency_ns = 50;
	sw1->ports[0].link.latency_ns = 50;
	cxl_port_enable(&sw0->ports[0]);
	cxl_port_enable(&sw1->ports[0]);

	/* Create endpoints with simulated memory */

	/* Endpoint 0 - 512GB */
	ep0 = cxl_endpoint_create(fabric, 512ULL * 1024 * 1024 * 1024);
	if (IS_ERR(ep0))
		return PTR_ERR(ep0);

	ep0->supports_atomic = true;
	ep0->supports_cache_coherent = true;
	ep0->numa_node = 8;  /* Simulated NUMA node */

	ret = cxl_endpoint_add_to_fabric(fabric, ep0);
	if (ret) {
		cxl_endpoint_destroy(ep0);
		return ret;
	}

	/* Connect endpoint 0 to switch 0 port 1 */
	cxl_port_connect(&sw0->ports[1], ep0);
	ep0->fabric_port = &sw0->ports[1];
	sw0->ports[1].type = CXL_PORT_DOWNSTREAM;
	sw0->ports[1].link.speed_gbps = 64;
	sw0->ports[1].link.latency_ns = 100;
	cxl_port_enable(&sw0->ports[1]);

	/* Endpoint 1 - 512GB */
	ep1 = cxl_endpoint_create(fabric, 512ULL * 1024 * 1024 * 1024);
	if (IS_ERR(ep1))
		return PTR_ERR(ep1);

	ep1->supports_atomic = true;
	ep1->supports_cache_coherent = true;
	ep1->numa_node = 9;

	ret = cxl_endpoint_add_to_fabric(fabric, ep1);
	if (ret) {
		cxl_endpoint_destroy(ep1);
		return ret;
	}

	cxl_port_connect(&sw0->ports[2], ep1);
	ep1->fabric_port = &sw0->ports[2];
	sw0->ports[2].type = CXL_PORT_DOWNSTREAM;
	sw0->ports[2].link.speed_gbps = 64;
	sw0->ports[2].link.latency_ns = 100;
	cxl_port_enable(&sw0->ports[2]);

	/* Endpoint 2 - 1TB */
	ep2 = cxl_endpoint_create(fabric, 1024ULL * 1024 * 1024 * 1024);
	if (IS_ERR(ep2))
		return PTR_ERR(ep2);

	ep2->supports_atomic = true;
	ep2->supports_cache_coherent = false;  /* Non-coherent for testing */
	ep2->numa_node = 10;

	ret = cxl_endpoint_add_to_fabric(fabric, ep2);
	if (ret) {
		cxl_endpoint_destroy(ep2);
		return ret;
	}

	cxl_port_connect(&sw1->ports[1], ep2);
	ep2->fabric_port = &sw1->ports[1];
	sw1->ports[1].type = CXL_PORT_DOWNSTREAM;
	sw1->ports[1].link.speed_gbps = 64;
	sw1->ports[1].link.latency_ns = 100;
	cxl_port_enable(&sw1->ports[1]);

	/* Endpoint 3 - 1TB */
	ep3 = cxl_endpoint_create(fabric, 1024ULL * 1024 * 1024 * 1024);
	if (IS_ERR(ep3))
		return PTR_ERR(ep3);

	ep3->supports_atomic = false;  /* No atomic support for testing */
	ep3->supports_cache_coherent = false;
	ep3->numa_node = 11;

	ret = cxl_endpoint_add_to_fabric(fabric, ep3);
	if (ret) {
		cxl_endpoint_destroy(ep3);
		return ret;
	}

	cxl_port_connect(&sw1->ports[2], ep3);
	ep3->fabric_port = &sw1->ports[2];
	sw1->ports[2].type = CXL_PORT_DOWNSTREAM;
	sw1->ports[2].link.speed_gbps = 64;
	sw1->ports[2].link.latency_ns = 100;
	cxl_port_enable(&sw1->ports[2]);

	/* Increment generation counter */
	atomic_inc(&fabric->generation);

	pr_info("CXL Fabric: Scan complete - %u switches, %u endpoints\n",
		fabric->num_switches, fabric->num_endpoints);

	return 0;
}
EXPORT_SYMBOL_GPL(cxl_fabric_scan);

/*
 * Module initialization
 */
static int __init cxl_fabric_init(void)
{
	int ret;

	pr_info("CXL Fabric Interconnect v%d.%d.%d initializing\n",
		CXL_INTERCONNECT_VERSION_MAJOR,
		CXL_INTERCONNECT_VERSION_MINOR,
		CXL_INTERCONNECT_VERSION_PATCH);

	/* Initialize hardware shim */
	hw_shim.memory_size = 4ULL * 1024 * 1024 * 1024;  /* 4GB simulated */
	hw_shim.simulated_memory = vzalloc(hw_shim.memory_size);
	if (!hw_shim.simulated_memory) {
		pr_err("CXL Fabric: Failed to allocate simulated memory\n");
		return -ENOMEM;
	}
	hw_shim.initialized = true;

	/* Create device class */
	cxl_fabric_class = class_create(THIS_MODULE, CXL_FABRIC_CLASS_NAME);
	if (IS_ERR(cxl_fabric_class)) {
		pr_err("CXL Fabric: Failed to create class\n");
		vfree(hw_shim.simulated_memory);
		return PTR_ERR(cxl_fabric_class);
	}

	/* Allocate device numbers */
	ret = alloc_chrdev_region(&cxl_fabric_devt, 0, CXL_MAX_FABRICS,
				  CXL_FABRIC_NAME);
	if (ret) {
		pr_err("CXL Fabric: Failed to allocate device numbers\n");
		class_destroy(cxl_fabric_class);
		vfree(hw_shim.simulated_memory);
		return ret;
	}

	pr_info("CXL Fabric: Ready (SHIM mode for testing)\n");

	return 0;
}

static void __exit cxl_fabric_exit(void)
{
	struct cxl_fabric *fabric, *tmp;

	pr_info("CXL Fabric: Shutting down\n");

	/* Destroy all fabrics */
	list_for_each_entry_safe(fabric, tmp, &cxl_fabric_list, dev.bus_list) {
		cxl_fabric_destroy(fabric);
	}

	/* Cleanup device class */
	unregister_chrdev_region(cxl_fabric_devt, CXL_MAX_FABRICS);
	class_destroy(cxl_fabric_class);

	/* Free hardware shim */
	if (hw_shim.initialized) {
		vfree(hw_shim.simulated_memory);
		hw_shim.initialized = false;
	}

	pr_info("CXL Fabric: Shutdown complete\n");
}

module_init(cxl_fabric_init);
module_exit(cxl_fabric_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("InterMatrix Project");
MODULE_DESCRIPTION("CXL Fabric Interconnect - Core Fabric Management");
MODULE_VERSION("1.0.0");
