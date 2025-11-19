/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CXL Fabric Interconnect - Core Data Structures
 *
 * Copyright (C) 2025 InterMatrix Project
 */

#ifndef _LINUX_CXL_FABRIC_H
#define _LINUX_CXL_FABRIC_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/idr.h>

/* CXL Interconnect Version */
#define CXL_INTERCONNECT_VERSION_MAJOR 1
#define CXL_INTERCONNECT_VERSION_MINOR 0
#define CXL_INTERCONNECT_VERSION_PATCH 0

/* Configuration Constants */
#define CXL_MAX_FABRICS        8
#define CXL_MAX_SWITCHES       256
#define CXL_MAX_ENDPOINTS      4096
#define CXL_MAX_PORTS_PER_SWITCH 256
#define CXL_MAX_HOPS           16
#define CXL_MAX_MCAST_GROUPS   256

/* Port states */
enum cxl_port_state {
	CXL_PORT_DOWN = 0,
	CXL_PORT_TRAINING,
	CXL_PORT_UP,
};

/* Port types */
enum cxl_port_type {
	CXL_PORT_UPSTREAM = 0,
	CXL_PORT_DOWNSTREAM,
};

/* Link types */
enum cxl_link_type {
	CXL_LINK_COPPER = 0,
	CXL_LINK_OPTICAL,
};

/* Memory access flags */
#define CXL_MEM_READ       BIT(0)
#define CXL_MEM_WRITE      BIT(1)
#define CXL_MEM_EXEC       BIT(2)
#define CXL_MEM_ATOMIC     BIT(3)
#define CXL_MEM_COHERENT   BIT(4)
#define CXL_MEM_RW         (CXL_MEM_READ | CXL_MEM_WRITE)

/* Forward declarations */
struct cxl_fabric;
struct cxl_switch;
struct cxl_port;
struct cxl_endpoint;
struct cxl_memnet_region;
struct cxl_route;

/**
 * struct cxl_link_config - Link configuration
 */
struct cxl_link_config {
	u32 speed_gbps;
	u8 width;
	enum cxl_link_type link_type;
	u32 latency_ns;
};

/**
 * struct cxl_qos_params - QoS parameters
 */
struct cxl_qos_params {
	u32 min_bandwidth_mbps;
	u32 max_latency_ns;
	u8 priority;
	u8 virtual_channel;
};

/**
 * struct cxl_port - CXL switch port
 */
struct cxl_port {
	u16 port_id;
	enum cxl_port_type type;
	enum cxl_port_state state;

	struct cxl_switch *parent_switch;

	/* Connected device (union for type safety) */
	union {
		struct cxl_switch *child_switch;
		struct cxl_endpoint *endpoint;
		void *connected;
	};

	struct cxl_link_config link;
	struct cxl_qos_params qos;

	/* Statistics */
	struct {
		u64 bytes_tx;
		u64 bytes_rx;
		u64 errors;
	} stats;

	spinlock_t lock;
};

/**
 * struct cxl_switch_caps - Switch capabilities
 */
struct cxl_switch_caps {
	u32 max_bandwidth_gbps;
	u16 max_ports;
	bool supports_multicast;
	bool supports_qos;
	u8 max_virtual_channels;
};

/**
 * struct cxl_switch_counters - Switch telemetry counters
 */
struct cxl_switch_counters {
	u64 packets_routed;
	u64 flits_forwarded;
	u64 crc_errors;
	u64 retries;
};

/**
 * struct cxl_switch_route_table - Hardware routing table
 */
struct cxl_switch_route_table {
	u16 port_map[CXL_MAX_ENDPOINTS];

	/* Multicast groups */
	struct cxl_mcast_group {
		u32 group_id;
		DECLARE_BITMAP(member_ports, CXL_MAX_PORTS_PER_SWITCH);
	} mcast_groups[CXL_MAX_MCAST_GROUPS];
};

/**
 * struct cxl_switch_ops - Vendor-specific operations
 */
struct cxl_switch_ops {
	int (*init)(struct cxl_switch *sw);
	void (*cleanup)(struct cxl_switch *sw);

	int (*enable_port)(struct cxl_port *port);
	int (*disable_port)(struct cxl_port *port);

	int (*program_route)(struct cxl_switch *sw, u32 dst_id, u16 port);
	int (*read_counters)(struct cxl_switch *sw, struct cxl_switch_counters *cnt);

	int (*set_qos)(struct cxl_port *port, const struct cxl_qos_params *qos);
};

/**
 * struct cxl_switch - CXL fabric switch
 */
struct cxl_switch {
	struct device dev;
	struct list_head fabric_node;

	u32 switch_id;
	u32 vendor_id;
	u32 device_id;

	u16 num_ports;
	struct cxl_port *ports;
	DECLARE_BITMAP(port_active, CXL_MAX_PORTS_PER_SWITCH);

	struct cxl_switch_route_table *route_table;
	struct cxl_switch_caps caps;
	struct cxl_switch_counters counters;

	const struct cxl_switch_ops *ops;
	void *vendor_data;

	struct cxl_fabric *fabric;
	spinlock_t route_lock;
	struct mutex ops_lock;
};

/**
 * struct cxl_endpoint - CXL memory endpoint
 */
struct cxl_endpoint {
	struct device dev;
	struct list_head fabric_node;

	u32 endpoint_id;
	struct cxl_port *fabric_port;

	/* Memory resources */
	struct resource memory_resource;
	u64 total_memory;
	atomic64_t available_memory;

	/* Memory regions */
	struct list_head regions;
	struct rw_semaphore region_sem;

	/* NUMA information */
	int numa_node;

	/* Capabilities */
	bool supports_atomic;
	bool supports_cache_coherent;

	u32 access_mask;

	struct cxl_fabric *fabric;
};

/**
 * struct cxl_route - Computed route through fabric
 */
struct cxl_route {
	struct cxl_endpoint *src;
	struct cxl_endpoint *dst;

	u32 hop_count;
	struct cxl_port *hops[CXL_MAX_HOPS];

	u32 total_latency_ns;
	u32 available_bandwidth_gbps;

	struct cxl_route *alternate;
};

/**
 * struct cxl_fabric_topology - Fabric topology graph
 */
struct cxl_fabric_topology {
	u32 num_vertices;
	u32 num_edges;

	/* Adjacency matrix for routing */
	u32 **adj_matrix;

	/* Generation counter */
	atomic_t generation;
};

/**
 * struct cxl_fabric_stats - Fabric-wide statistics
 */
struct cxl_fabric_stats {
	atomic64_t total_bytes_transferred;
	atomic64_t total_operations;
	atomic_t active_regions;
	atomic_t route_cache_hits;
	atomic_t route_cache_misses;
};

/**
 * struct cxl_fabric - Global fabric instance
 */
struct cxl_fabric {
	struct device dev;
	u32 fabric_id;

	/* Topology */
	struct list_head switches;
	struct list_head endpoints;
	u32 num_switches;
	u32 num_endpoints;

	struct cxl_fabric_topology *topology;

	/* Routing */
	struct mutex route_lock;
	struct idr route_idr;

	/* Resource management */
	struct ida endpoint_ida;
	struct ida switch_ida;

	/* Statistics */
	struct cxl_fabric_stats stats;

	/* Hotplug */
	struct workqueue_struct *hotplug_wq;
	atomic_t generation;

	/* Character device */
	struct cdev cdev;
	dev_t devt;

	struct mutex fabric_lock;
};

/**
 * struct cxl_memnet_region - Remote memory region
 */
struct cxl_memnet_region {
	struct list_head ep_node;

	struct cxl_endpoint *owner;
	u64 remote_phys_addr;
	void __iomem *local_virt_addr;
	size_t size;

	struct kref refcount;
	u32 access_flags;
	pgprot_t prot;
	bool coherent;

	/* Statistics */
	atomic64_t read_count;
	atomic64_t write_count;
	ktime_t last_access;

	/* Handle for userspace */
	u64 handle;
};

/* Global fabric management */
extern struct list_head cxl_fabric_list;
extern struct mutex cxl_fabric_list_lock;

/* Fabric operations */
struct cxl_fabric *cxl_fabric_create(u32 fabric_id);
void cxl_fabric_destroy(struct cxl_fabric *fabric);
int cxl_fabric_scan(struct cxl_fabric *fabric);
struct cxl_fabric *cxl_fabric_find(u32 fabric_id);

/* Switch operations */
struct cxl_switch *cxl_switch_create(struct cxl_fabric *fabric, u16 num_ports);
void cxl_switch_destroy(struct cxl_switch *sw);
int cxl_switch_add_to_fabric(struct cxl_fabric *fabric, struct cxl_switch *sw);

/* Endpoint operations */
struct cxl_endpoint *cxl_endpoint_create(struct cxl_fabric *fabric, u64 memory_size);
void cxl_endpoint_destroy(struct cxl_endpoint *ep);
int cxl_endpoint_add_to_fabric(struct cxl_fabric *fabric, struct cxl_endpoint *ep);

/* Port operations */
int cxl_port_enable(struct cxl_port *port);
int cxl_port_disable(struct cxl_port *port);
int cxl_port_connect(struct cxl_port *port, void *target);

/* Memory network operations */
struct cxl_memnet_region *cxl_memnet_alloc_remote(struct cxl_endpoint *ep,
						  size_t size, u32 flags);
void cxl_memnet_free_remote(struct cxl_memnet_region *region);
int cxl_memnet_put(struct cxl_memnet_region *region,
		   const void *src, size_t len, loff_t offset);
int cxl_memnet_get(struct cxl_memnet_region *region,
		   void *dest, size_t len, loff_t offset);

/* Routing operations */
int cxl_route_compute(struct cxl_fabric *fabric,
		      struct cxl_endpoint *src,
		      struct cxl_endpoint *dst,
		      struct cxl_route *route);
int cxl_route_program_fabric(struct cxl_fabric *fabric, struct cxl_route *route);

#endif /* _LINUX_CXL_FABRIC_H */
