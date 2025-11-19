# CXL Interconnect Kernel Subsystem Design
## Detailed Implementation Specification

**Version:** 1.0
**Date:** 2025-11-19
**Target Kernel:** Linux 6.8+

---

## 1. Overview

This document provides the detailed kernel-level design for the CXL Interconnect subsystem. It extends the existing `drivers/cxl/` infrastructure (currently focused on memory expansion) to support fabric-based interconnection replacing traditional networks.

**Key Design Principles:**
1. **Zero Software Tax:** Bypass kernel network stack entirely
2. **Native Memory Semantics:** Expose CXL memory as directly accessible addresses
3. **NUMA-Aware:** Integrate with Linux NUMA for optimal placement
4. **Hot-Pluggable:** Support dynamic fabric topology changes
5. **Vendor-Neutral:** Abstract hardware differences via HAL

---

## 2. Kernel Module Structure

### 2.1 Module Organization

```
drivers/cxl/
├── core/                    # Existing CXL core (CXL 2.0 memory)
│   ├── core.h
│   ├── memdev.c
│   └── port.c
├── interconnect/            # NEW: Fabric interconnect subsystem
│   ├── Kconfig              # Kernel configuration
│   ├── Makefile
│   ├── fabric.c             # Fabric topology management
│   ├── memnet.c             # Memory network interface
│   ├── route.c              # Routing engine
│   ├── switch.c             # Switch driver framework
│   ├── qos.c                # Quality of Service
│   ├── optics.c             # Optical cable support
│   └── debug.c              # Debugfs/sysfs interfaces
├── pci/                     # Existing PCIe integration
└── acpi/                    # Existing ACPI/CEDT parsing
```

---

## 3. Core Data Structures

### 3.1 Fabric Representation

```c
/* drivers/cxl/interconnect/fabric.c */

/**
 * struct cxl_fabric - Global fabric instance
 *
 * Represents a single CXL fabric domain. Multiple fabrics can coexist
 * (e.g., different security domains or physical networks).
 */
struct cxl_fabric {
    struct device dev;               /* Embedded device */
    u32 fabric_id;                   /* Globally unique ID */

    /* Topology */
    struct list_head switches;       /* List of cxl_switch */
    struct list_head endpoints;      /* List of cxl_endpoint */
    struct cxl_fabric_topology *topo; /* Graph representation */

    /* Routing */
    struct cxl_route_table *routes;  /* Computed routes */
    struct mutex route_lock;         /* Route modification */

    /* Resource management */
    struct cxl_mem_pool global_pool; /* Fabric-wide memory pool */
    struct ida endpoint_ida;         /* Endpoint ID allocator */

    /* QoS */
    struct cxl_qos_manager qos_mgr;

    /* Statistics */
    struct cxl_fabric_stats stats;
    struct dentry *debugfs_dir;      /* /sys/kernel/debug/cxl/fabric0/ */

    /* Hotplug handling */
    struct workqueue_struct *hotplug_wq;
    atomic_t generation;             /* Topology version */
};

/**
 * struct cxl_fabric_topology - Graph representation of fabric
 *
 * Efficient graph structure for routing algorithms.
 */
struct cxl_fabric_topology {
    u32 num_vertices;                /* Switches + Endpoints */
    u32 num_edges;                   /* Links */

    struct cxl_topo_vertex *vertices; /* Adjacency list */
    u32 **adj_matrix;                 /* Distance matrix (Floyd-Warshall) */

    /* Cached shortest paths */
    struct cxl_path_cache *path_cache;
};

/**
 * struct cxl_switch - CXL fabric switch
 *
 * Represents a multi-port CXL switch (typically PCIe Gen6 with CXL 3.0).
 */
struct cxl_switch {
    struct device dev;
    struct list_head fabric_node;    /* Link in fabric->switches */

    /* Hardware identification */
    struct pci_dev *pdev;            /* Underlying PCIe device */
    u32 switch_id;                   /* Unique ID in fabric */
    u32 vendor_id;
    u32 device_id;

    /* Port topology */
    u16 num_ports;
    struct cxl_port *ports;          /* Array of ports */
    DECLARE_BITMAP(port_active, 256); /* Active port bitmap */

    /* Routing table */
    struct cxl_switch_route_table *hw_route_table;
    spinlock_t route_lock;

    /* Capabilities */
    struct cxl_switch_caps {
        u32 max_bandwidth_gbps;      /* Per-port max BW */
        u16 max_ports;
        bool supports_multicast;
        bool supports_qos;
        u8 max_virtual_channels;
    } caps;

    /* Vendor-specific ops */
    const struct cxl_switch_ops *ops;
    void *vendor_data;               /* Opaque vendor context */

    /* Telemetry */
    struct cxl_switch_counters {
        u64 packets_routed;
        u64 flits_forwarded;
        u64 crc_errors;
        u64 retries;
    } counters;
};

/**
 * struct cxl_port - Individual switch port
 *
 * Each port can connect to another switch (upstream/downstream) or
 * to an endpoint device.
 */
struct cxl_port {
    u16 port_id;                     /* Port number (0-255) */
    enum cxl_port_type {
        CXL_PORT_UPSTREAM,           /* To parent switch/root */
        CXL_PORT_DOWNSTREAM,         /* To child switch/device */
    } type;

    struct cxl_switch *parent_switch;

    /* Connection state */
    enum cxl_port_state {
        CXL_PORT_DOWN,
        CXL_PORT_TRAINING,           /* Link training in progress */
        CXL_PORT_UP,
    } state;

    /* Connected device */
    union {
        struct cxl_switch *child_switch;
        struct cxl_endpoint *endpoint;
    } connected;

    /* Link properties */
    struct cxl_link_config {
        u32 speed_gbps;              /* Current speed */
        u8 width;                    /* Lane count (x1, x2, x4, x8, x16) */
        enum cxl_link_type {
            CXL_LINK_COPPER,
            CXL_LINK_OPTICAL,
        } link_type;
    } link;

    /* QoS */
    struct cxl_qos_params {
        u32 min_bandwidth_mbps;
        u32 max_latency_ns;
        u8 priority;                 /* 0-7 */
        u8 virtual_channel;          /* VC assignment */
    } qos;

    /* Optical module (if applicable) */
    struct cxl_optics *optics;

    /* Statistics */
    struct {
        u64 bytes_tx;
        u64 bytes_rx;
        u64 errors;
    } stats;
};

/**
 * struct cxl_endpoint - CXL memory endpoint
 *
 * Represents a device with CXL memory (server, accelerator, memory blade).
 */
struct cxl_endpoint {
    struct device dev;
    struct list_head fabric_node;    /* Link in fabric->endpoints */

    u32 endpoint_id;                 /* Unique ID in fabric */
    struct cxl_port *fabric_port;    /* Connection to fabric */

    /* Memory resources */
    struct resource memory_resource; /* Physical address range */
    u64 total_memory;                /* Total exportable memory */
    u64 available_memory;            /* Free memory */

    /* Memory regions */
    struct list_head regions;        /* List of cxl_memnet_region */
    struct rw_semaphore region_sem;  /* Protect region list */

    /* NUMA information */
    int numa_node;                   /* Assigned NUMA node ID */

    /* Security */
    u32 access_mask;                 /* Bitmap of allowed access modes */

    /* Capabilities */
    bool supports_atomic;            /* Atomic operations */
    bool supports_cache_coherent;    /* CXL.cache protocol */
};
```

---

### 3.2 Memory Network Structures

```c
/* drivers/cxl/interconnect/memnet.c */

/**
 * struct cxl_memnet_region - Remote memory region
 *
 * Represents a chunk of remote CXL memory that has been allocated
 * and is accessible from this host.
 */
struct cxl_memnet_region {
    struct list_head ep_node;        /* Link in endpoint->regions */

    /* Addressing */
    struct cxl_endpoint *owner;      /* Remote endpoint */
    u64 remote_phys_addr;            /* Physical addr on remote node */
    void __iomem *local_virt_addr;   /* Kernel virtual mapping */
    size_t size;

    /* Reference counting */
    struct kref refcount;

    /* Access control */
    enum cxl_mem_access {
        CXL_MEM_READ  = BIT(0),
        CXL_MEM_WRITE = BIT(1),
        CXL_MEM_EXEC  = BIT(2),      /* For code regions */
        CXL_MEM_ATOMIC = BIT(3),
    } access_flags;

    /* Memory properties */
    pgprot_t prot;                   /* Page protection flags */
    bool coherent;                   /* Cache coherent via CXL.cache */

    /* Statistics */
    atomic64_t read_count;
    atomic64_t write_count;
    ktime_t last_access;
};

/**
 * struct cxl_memnet_qp - Queue Pair (RDMA compatibility)
 *
 * Emulates RDMA queue pairs for libibverbs compatibility.
 */
struct cxl_memnet_qp {
    u32 qp_num;

    /* Queues */
    struct cxl_memnet_queue {
        void *ring;                  /* Ring buffer */
        u32 head;
        u32 tail;
        u32 size;
        spinlock_t lock;
    } send_queue, recv_queue;

    /* Completion */
    struct cxl_memnet_cq *send_cq;
    struct cxl_memnet_cq *recv_cq;

    /* State */
    enum cxl_qp_state {
        CXL_QPS_RESET,
        CXL_QPS_INIT,
        CXL_QPS_RTR,                 /* Ready to Receive */
        CXL_QPS_RTS,                 /* Ready to Send */
        CXL_QPS_ERR,
    } state;

    /* Remote endpoint */
    struct cxl_endpoint *remote_ep;
};
```

---

### 3.3 Routing Structures

```c
/* drivers/cxl/interconnect/route.c */

/**
 * struct cxl_route - Computed path through fabric
 *
 * Represents a route from source to destination endpoint.
 */
struct cxl_route {
    struct cxl_endpoint *src;
    struct cxl_endpoint *dst;

    /* Path (list of ports to traverse) */
    u32 hop_count;
    struct cxl_port *hops[CXL_MAX_HOPS]; /* Max 16 hops */

    /* Metrics */
    u32 total_latency_ns;            /* Estimated latency */
    u32 available_bandwidth_gbps;    /* Bottleneck BW */

    /* Failover */
    struct cxl_route *alternate;     /* Backup path */
};

/**
 * struct cxl_route_table - Global routing table
 *
 * Maintains computed routes for all endpoint pairs.
 */
struct cxl_route_table {
    /* Hash table: key = (src_id << 16 | dst_id) */
    DECLARE_HASHTABLE(routes, 16);

    /* RCU for lockless reads */
    struct rcu_head rcu;

    /* Version for cache invalidation */
    u64 generation;
};

/**
 * struct cxl_switch_route_table - Hardware routing table
 *
 * Programmed into switch silicon for fast forwarding.
 */
struct cxl_switch_route_table {
    /* Indexed by destination endpoint ID */
    u16 port_map[CXL_MAX_ENDPOINTS]; /* Output port for each dest */

    /* Multicast groups (optional) */
    struct cxl_mcast_group {
        u32 group_id;
        DECLARE_BITMAP(member_ports, 256);
    } mcast_groups[CXL_MAX_MCAST_GROUPS];
};
```

---

## 4. Kernel Subsystem Initialization

### 4.1 Module Init Sequence

```c
/* drivers/cxl/interconnect/fabric.c */

static int __init cxl_interconnect_init(void)
{
    int ret;

    pr_info("CXL Interconnect: Initializing fabric subsystem\n");

    /* Register bus type */
    ret = bus_register(&cxl_fabric_bus_type);
    if (ret)
        return ret;

    /* Register device class */
    ret = class_register(&cxl_fabric_class);
    if (ret)
        goto err_bus;

    /* Initialize global fabric list */
    INIT_LIST_HEAD(&global_fabric_list);
    mutex_init(&global_fabric_lock);

    /* Register PCIe driver for CXL switches */
    ret = pci_register_driver(&cxl_switch_pci_driver);
    if (ret)
        goto err_class;

    /* Initialize sysfs */
    ret = cxl_fabric_sysfs_init();
    if (ret)
        goto err_pci;

    /* Initialize debugfs */
    cxl_fabric_debugfs_root = debugfs_create_dir("cxl_fabric", NULL);

    /* Register ACPI notifier for hotplug */
    register_acpi_notifier(&cxl_fabric_acpi_notifier);

    pr_info("CXL Interconnect: Ready\n");
    return 0;

err_pci:
    pci_unregister_driver(&cxl_switch_pci_driver);
err_class:
    class_unregister(&cxl_fabric_class);
err_bus:
    bus_unregister(&cxl_fabric_bus_type);
    return ret;
}
module_init(cxl_interconnect_init);
```

---

### 4.2 Fabric Discovery

```c
/* drivers/cxl/interconnect/fabric.c */

/**
 * cxl_fabric_scan - Discover fabric topology
 * @fabric: Fabric instance to populate
 *
 * Walks the ACPI CEDT (CXL Early Discovery Table) and PCIe bus to
 * enumerate switches and endpoints.
 *
 * Returns: 0 on success, negative errno on failure
 */
int cxl_fabric_scan(struct cxl_fabric *fabric)
{
    struct acpi_table_cedt *cedt;
    acpi_status status;
    int ret;

    /* Parse ACPI CEDT for fabric topology */
    status = acpi_get_table("CEDT", 0, (struct acpi_table_header **)&cedt);
    if (ACPI_FAILURE(status)) {
        dev_warn(&fabric->dev, "No CEDT found, scanning PCIe\n");
        goto scan_pci;
    }

    ret = cxl_fabric_parse_cedt(fabric, cedt);
    acpi_put_table((struct acpi_table_header *)cedt);
    if (ret)
        return ret;

scan_pci:
    /* Scan PCIe bus for CXL switches (Class Code 0x0C0B) */
    ret = cxl_fabric_scan_pci_switches(fabric);
    if (ret)
        return ret;

    /* Enumerate endpoints */
    ret = cxl_fabric_enumerate_endpoints(fabric);
    if (ret)
        return ret;

    /* Build topology graph */
    ret = cxl_fabric_build_topology(fabric);
    if (ret)
        return ret;

    /* Compute initial routes */
    ret = cxl_fabric_compute_all_routes(fabric);
    if (ret)
        return ret;

    dev_info(&fabric->dev, "Fabric scan complete: %u switches, %u endpoints\n",
             list_count_nodes(&fabric->switches),
             list_count_nodes(&fabric->endpoints));

    return 0;
}
```

---

## 5. Memory Network Implementation

### 5.1 Remote Memory Allocation

```c
/* drivers/cxl/interconnect/memnet.c */

/**
 * cxl_memnet_alloc_remote - Allocate remote CXL memory
 * @endpoint: Target endpoint to allocate from
 * @size: Size in bytes
 * @flags: Access flags (READ/WRITE/ATOMIC)
 *
 * Returns: Pointer to cxl_memnet_region, or ERR_PTR on failure
 */
struct cxl_memnet_region *cxl_memnet_alloc_remote(
    struct cxl_endpoint *endpoint,
    size_t size,
    u32 flags)
{
    struct cxl_memnet_region *region;
    u64 remote_phys;
    void __iomem *virt;
    int ret;

    /* Validate size and alignment */
    if (!IS_ALIGNED(size, PAGE_SIZE))
        return ERR_PTR(-EINVAL);

    /* Check available memory */
    if (atomic64_read(&endpoint->available_memory) < size)
        return ERR_PTR(-ENOMEM);

    /* Allocate region structure */
    region = kzalloc(sizeof(*region), GFP_KERNEL);
    if (!region)
        return ERR_PTR(-ENOMEM);

    /* Allocate physical memory on remote endpoint */
    ret = cxl_endpoint_alloc_memory(endpoint, size, &remote_phys);
    if (ret) {
        kfree(region);
        return ERR_PTR(ret);
    }

    /* Map into local address space via CXL fabric */
    virt = cxl_fabric_ioremap(endpoint->fabric_port->parent_switch->fabric,
                              endpoint, remote_phys, size);
    if (!virt) {
        cxl_endpoint_free_memory(endpoint, remote_phys, size);
        kfree(region);
        return ERR_PTR(-ENOMEM);
    }

    /* Initialize region */
    region->owner = endpoint;
    region->remote_phys_addr = remote_phys;
    region->local_virt_addr = virt;
    region->size = size;
    region->access_flags = flags;
    region->coherent = endpoint->supports_cache_coherent;
    kref_init(&region->refcount);

    /* Set page protection */
    if (flags & CXL_MEM_WRITE)
        region->prot = PAGE_KERNEL;
    else
        region->prot = PAGE_KERNEL_RO;

    /* Add to endpoint's region list */
    down_write(&endpoint->region_sem);
    list_add(&region->ep_node, &endpoint->regions);
    up_write(&endpoint->region_sem);

    /* Update statistics */
    atomic64_sub(size, &endpoint->available_memory);

    return region;
}
EXPORT_SYMBOL_GPL(cxl_memnet_alloc_remote);
```

---

### 5.2 Zero-Copy Memory Operations

```c
/* drivers/cxl/interconnect/memnet.c */

/**
 * cxl_memnet_put - Write data to remote memory (zero-copy)
 * @region: Target memory region
 * @src: Source buffer (local)
 * @len: Length in bytes
 * @offset: Offset into region
 *
 * Directly writes to remote memory using CPU store instructions.
 * No software overhead, just memory bus transactions.
 *
 * Returns: 0 on success, negative errno on failure
 */
int cxl_memnet_put(struct cxl_memnet_region *region,
                   const void *src, size_t len, loff_t offset)
{
    void __iomem *dest;

    /* Bounds check */
    if (offset + len > region->size)
        return -EINVAL;

    /* Permission check */
    if (!(region->access_flags & CXL_MEM_WRITE))
        return -EACCES;

    dest = region->local_virt_addr + offset;

    /* Use memcpy_toio for MMIO-safe copy */
    if (region->coherent) {
        /* Cache-coherent: can use normal memcpy */
        memcpy_toio(dest, src, len);
    } else {
        /* Non-coherent: ensure write ordering */
        memcpy_toio_wc(dest, src, len);
        wmb(); /* Write memory barrier */
    }

    /* Update statistics */
    atomic64_inc(&region->write_count);
    region->last_access = ktime_get();

    return 0;
}
EXPORT_SYMBOL_GPL(cxl_memnet_put);

/**
 * cxl_memnet_get - Read data from remote memory (zero-copy)
 */
int cxl_memnet_get(struct cxl_memnet_region *region,
                   void *dest, size_t len, loff_t offset)
{
    const void __iomem *src;

    if (offset + len > region->size)
        return -EINVAL;

    if (!(region->access_flags & CXL_MEM_READ))
        return -EACCES;

    src = region->local_virt_addr + offset;

    if (region->coherent) {
        memcpy_fromio(dest, src, len);
    } else {
        memcpy_fromio(dest, src, len);
        rmb(); /* Read memory barrier */
    }

    atomic64_inc(&region->read_count);
    region->last_access = ktime_get();

    return 0;
}
EXPORT_SYMBOL_GPL(cxl_memnet_get);
```

---

### 5.3 Atomic Operations

```c
/* drivers/cxl/interconnect/memnet.c */

/**
 * cxl_memnet_atomic_add - Atomic add on remote memory
 * @region: Target memory region
 * @offset: Offset into region (must be 8-byte aligned)
 * @value: Value to add
 *
 * Uses CXL atomic operations (if supported) or mutex for fallback.
 *
 * Returns: 0 on success, negative errno on failure
 */
int cxl_memnet_atomic_add(struct cxl_memnet_region *region,
                          loff_t offset, s64 value)
{
    u64 __iomem *addr;

    if (!IS_ALIGNED(offset, sizeof(u64)))
        return -EINVAL;

    if (!(region->access_flags & CXL_MEM_ATOMIC))
        return -EACCES;

    addr = region->local_virt_addr + offset;

    if (region->owner->supports_atomic) {
        /* Hardware atomic operation via CXL.mem protocol */
        cxl_hw_atomic_add(addr, value);
    } else {
        /* Software fallback (slow) */
        u64 old, new;
        do {
            old = readq(addr);
            new = old + value;
        } while (cmpxchg64(addr, old, new) != old);
    }

    return 0;
}
EXPORT_SYMBOL_GPL(cxl_memnet_atomic_add);

/**
 * cxl_memnet_compare_swap - Atomic compare-and-swap
 */
int cxl_memnet_compare_swap(struct cxl_memnet_region *region,
                            loff_t offset, u64 old_val, u64 new_val,
                            u64 *actual_old)
{
    u64 __iomem *addr;
    u64 result;

    if (!IS_ALIGNED(offset, sizeof(u64)))
        return -EINVAL;

    addr = region->local_virt_addr + offset;

    if (region->owner->supports_atomic) {
        result = cxl_hw_cmpxchg(addr, old_val, new_val);
    } else {
        result = cmpxchg64(addr, old_val, new_val);
    }

    if (actual_old)
        *actual_old = result;

    return (result == old_val) ? 0 : -EAGAIN;
}
EXPORT_SYMBOL_GPL(cxl_memnet_compare_swap);
```

---

## 6. Routing Engine

### 6.1 Shortest Path Computation

```c
/* drivers/cxl/interconnect/route.c */

/**
 * cxl_route_compute - Compute route between endpoints
 * @fabric: Fabric instance
 * @src: Source endpoint
 * @dst: Destination endpoint
 * @route: Output route structure
 *
 * Uses modified Dijkstra's algorithm with bandwidth awareness.
 *
 * Returns: 0 on success, negative errno on failure
 */
int cxl_route_compute(struct cxl_fabric *fabric,
                      struct cxl_endpoint *src,
                      struct cxl_endpoint *dst,
                      struct cxl_route *route)
{
    struct cxl_fabric_topology *topo = fabric->topo;
    u32 *dist;           /* Distance array */
    u32 *prev;           /* Previous vertex */
    bool *visited;
    u32 src_idx, dst_idx;
    int ret = 0;

    if (src == dst)
        return -EINVAL;

    /* Allocate working arrays */
    dist = kcalloc(topo->num_vertices, sizeof(*dist), GFP_KERNEL);
    prev = kcalloc(topo->num_vertices, sizeof(*prev), GFP_KERNEL);
    visited = kcalloc(topo->num_vertices, sizeof(*visited), GFP_KERNEL);
    if (!dist || !prev || !visited) {
        ret = -ENOMEM;
        goto out;
    }

    /* Initialize */
    src_idx = cxl_topo_endpoint_to_vertex(topo, src);
    dst_idx = cxl_topo_endpoint_to_vertex(topo, dst);

    for (u32 i = 0; i < topo->num_vertices; i++) {
        dist[i] = U32_MAX;
        prev[i] = U32_MAX;
    }
    dist[src_idx] = 0;

    /* Dijkstra's algorithm */
    for (u32 i = 0; i < topo->num_vertices; i++) {
        u32 u = cxl_topo_min_distance(dist, visited, topo->num_vertices);
        if (u == U32_MAX)
            break;

        visited[u] = true;

        if (u == dst_idx)
            break; /* Found shortest path */

        /* Relax edges */
        for (u32 v = 0; v < topo->num_vertices; v++) {
            u32 weight = topo->adj_matrix[u][v];
            if (!visited[v] && weight != U32_MAX) {
                u32 alt = dist[u] + weight;
                if (alt < dist[v]) {
                    dist[v] = alt;
                    prev[v] = u;
                }
            }
        }
    }

    /* Reconstruct path */
    if (dist[dst_idx] == U32_MAX) {
        ret = -EHOSTUNREACH; /* No path */
        goto out;
    }

    ret = cxl_route_reconstruct_path(topo, prev, src_idx, dst_idx, route);
    if (ret)
        goto out;

    /* Calculate metrics */
    route->total_latency_ns = dist[dst_idx];
    route->available_bandwidth_gbps = cxl_route_min_bandwidth(route);

out:
    kfree(dist);
    kfree(prev);
    kfree(visited);
    return ret;
}
```

---

### 6.2 Hardware Route Programming

```c
/* drivers/cxl/interconnect/route.c */

/**
 * cxl_route_program_switch - Program route into switch hardware
 * @sw: Target switch
 * @route: Route to program
 *
 * Writes routing table entry into switch silicon.
 *
 * Returns: 0 on success, negative errno on failure
 */
int cxl_route_program_switch(struct cxl_switch *sw,
                              struct cxl_route *route)
{
    u16 output_port;
    u32 dst_id = route->dst->endpoint_id;
    int ret;

    /* Find which port this switch should forward to */
    output_port = cxl_route_find_output_port(sw, route);
    if (output_port >= sw->num_ports)
        return -EINVAL;

    /* Use vendor-specific operation */
    if (sw->ops && sw->ops->program_route) {
        ret = sw->ops->program_route(sw, dst_id, output_port);
    } else {
        /* Generic PCIe config space write */
        ret = cxl_switch_write_route_entry(sw, dst_id, output_port);
    }

    if (ret)
        return ret;

    /* Update software cache */
    spin_lock(&sw->route_lock);
    sw->hw_route_table->port_map[dst_id] = output_port;
    spin_unlock(&sw->route_lock);

    return 0;
}
```

---

## 7. Vendor Abstraction Layer

### 7.1 Switch Operations

```c
/* drivers/cxl/interconnect/switch.c */

/**
 * struct cxl_switch_ops - Vendor-specific switch operations
 *
 * Abstracts hardware differences between switch vendors.
 */
struct cxl_switch_ops {
    /* Initialization */
    int (*init)(struct cxl_switch *sw);
    void (*cleanup)(struct cxl_switch *sw);

    /* Port management */
    int (*enable_port)(struct cxl_port *port);
    int (*disable_port)(struct cxl_port *port);
    int (*get_port_status)(struct cxl_port *port,
                           struct cxl_port_status *status);

    /* Routing */
    int (*program_route)(struct cxl_switch *sw, u32 dst_id, u16 port);
    int (*read_route_table)(struct cxl_switch *sw,
                            struct cxl_switch_route_table *table);

    /* QoS */
    int (*set_port_qos)(struct cxl_port *port,
                        const struct cxl_qos_params *qos);

    /* Telemetry */
    int (*read_counters)(struct cxl_switch *sw,
                         struct cxl_switch_counters *counters);

    /* Error handling */
    int (*handle_error)(struct cxl_switch *sw, u32 error_code);
};

/* Broadcom switch implementation */
static const struct cxl_switch_ops broadcom_switch_ops = {
    .init = broadcom_switch_init,
    .cleanup = broadcom_switch_cleanup,
    .enable_port = broadcom_enable_port,
    .program_route = broadcom_program_route,
    /* ... */
};

/* Microchip switch implementation */
static const struct cxl_switch_ops microchip_switch_ops = {
    .init = microchip_switch_init,
    /* ... */
};
```

---

## 8. NUMA Integration

### 8.1 Memory Policy Extension

```c
/* mm/cxl_mm.c */

/**
 * cxl_mm_init_numa_zones - Initialize CXL NUMA zones
 *
 * Extends Linux NUMA subsystem to treat remote CXL memory as
 * additional NUMA nodes.
 */
int __init cxl_mm_init_numa_zones(void)
{
    struct cxl_fabric *fabric;
    struct cxl_endpoint *ep;
    int node_id;
    int ret;

    list_for_each_entry(fabric, &global_fabric_list, list) {
        list_for_each_entry(ep, &fabric->endpoints, fabric_node) {
            /* Allocate new NUMA node ID */
            node_id = alloc_node_id();
            if (node_id < 0)
                return node_id;

            ep->numa_node = node_id;

            /* Register memory zone */
            ret = add_memory(node_id,
                            ep->memory_resource.start,
                            resource_size(&ep->memory_resource));
            if (ret) {
                free_node_id(node_id);
                return ret;
            }

            /* Set NUMA distance (latency-based) */
            set_node_distance(node_id, 0,
                             ep->fabric_port->link.latency_ns / 10);

            pr_info("CXL: Registered NUMA node %d for endpoint %u\n",
                    node_id, ep->endpoint_id);
        }
    }

    return 0;
}
```

---

## 9. Sysfs Interface

### 9.1 Fabric Hierarchy

```
/sys/class/cxl_fabric/
├── fabric0/
│   ├── fabric_id
│   ├── num_switches
│   ├── num_endpoints
│   ├── topology/
│   │   ├── graph              (DOT format)
│   │   └── distances          (matrix)
│   ├── switches/
│   │   ├── switch0/
│   │   │   ├── vendor
│   │   │   ├── device
│   │   │   ├── num_ports
│   │   │   ├── ports/
│   │   │   │   ├── 0/
│   │   │   │   │   ├── state
│   │   │   │   │   ├── speed_gbps
│   │   │   │   │   ├── stats/
│   │   │   │   │   │   ├── bytes_tx
│   │   │   │   │   │   └── bytes_rx
│   │   │   │   ├── 1/
│   │   │   │   └── .../
│   │   │   └── route_table
│   │   └── switch1/
│   └── endpoints/
│       ├── endpoint0/
│       │   ├── endpoint_id
│       │   ├── total_memory
│       │   ├── available_memory
│       │   ├── numa_node
│       │   ├── regions/
│       │   │   ├── region0/
│       │   │   │   ├── size
│       │   │   │   ├── access_flags
│       │   │   │   └── stats
│       │   │   └── region1/
│       │   └── capabilities
│       └── endpoint1/
└── fabric1/
```

---

## 10. Performance Optimizations

### 10.1 Route Caching

```c
/* drivers/cxl/interconnect/route.c */

/**
 * struct cxl_path_cache - LRU cache for computed routes
 *
 * Avoids recomputing routes for frequently accessed endpoint pairs.
 */
struct cxl_path_cache {
    struct hlist_head hash[CXL_ROUTE_CACHE_SIZE];
    struct list_head lru;
    spinlock_t lock;
    u32 hits;
    u32 misses;
};

struct cxl_cached_route {
    struct hlist_node hash_node;
    struct list_head lru_node;

    u32 src_id;
    u32 dst_id;
    struct cxl_route route;

    ktime_t last_used;
    u32 use_count;
};
```

---

### 10.2 Lock-Free Fast Path

```c
/* drivers/cxl/interconnect/memnet.c */

/**
 * cxl_memnet_get_fast - Lockless read from cached region
 *
 * Uses RCU for zero-lock overhead on read path.
 */
static inline void *cxl_memnet_get_fast(struct cxl_memnet_region *region,
                                        loff_t offset)
{
    void __iomem *addr;

    rcu_read_lock();
    addr = READ_ONCE(region->local_virt_addr);
    if (likely(addr && offset < region->size)) {
        rcu_read_unlock();
        return addr + offset;
    }
    rcu_read_unlock();

    return NULL; /* Fall back to slow path */
}
```

---

## 11. Error Handling

### 11.1 Link Failure Recovery

```c
/* drivers/cxl/interconnect/fabric.c */

/**
 * cxl_fabric_handle_link_down - Handle link failure
 * @port: Failed port
 *
 * Automatically reroutes traffic around failed link.
 */
void cxl_fabric_handle_link_down(struct cxl_port *port)
{
    struct cxl_fabric *fabric = port->parent_switch->fabric;
    struct cxl_route *route, *tmp;

    dev_warn(&fabric->dev, "Link down on switch %u port %u\n",
             port->parent_switch->switch_id, port->port_id);

    /* Mark port as down */
    port->state = CXL_PORT_DOWN;

    /* Find affected routes */
    mutex_lock(&fabric->route_lock);
    hash_for_each_safe(fabric->routes->routes, bkt, tmp, route, hash_node) {
        if (cxl_route_uses_port(route, port)) {
            /* Try alternate path */
            if (route->alternate) {
                cxl_route_activate_alternate(fabric, route);
            } else {
                /* Recompute route */
                cxl_route_compute(fabric, route->src, route->dst, route);
            }
        }
    }
    mutex_unlock(&fabric->route_lock);

    /* Trigger topology update */
    atomic_inc(&fabric->generation);
}
```

---

## 12. Kernel Configuration

### 12.1 Kconfig

```kconfig
# drivers/cxl/interconnect/Kconfig

config CXL_INTERCONNECT
    tristate "CXL Fabric Interconnect Support"
    depends on CXL_BUS && PCI
    select NUMA
    help
      Enable CXL (Compute Express Link) as a fabric interconnect for
      rack-scale memory sharing and low-latency communication.

      This extends CXL beyond memory expansion to replace InfiniBand
      and high-speed Ethernet with memory-semantic communication.

      If unsure, say N.

config CXL_INTERCONNECT_RDMA_COMPAT
    bool "RDMA Compatibility Layer"
    depends on CXL_INTERCONNECT && INFINIBAND
    help
      Provides libibverbs compatibility for migrating RDMA applications
      to CXL without code changes.

config CXL_INTERCONNECT_DEBUG
    bool "CXL Interconnect Debugging"
    depends on CXL_INTERCONNECT && DEBUG_FS
    help
      Enable verbose debugging output and debugfs interfaces for
      troubleshooting CXL fabric issues.

config CXL_INTERCONNECT_STATS
    bool "CXL Interconnect Statistics"
    depends on CXL_INTERCONNECT
    default y
    help
      Collect detailed performance statistics (bandwidth, latency, errors).

      Minimal performance overhead (<1%).
```

---

## 13. Testing Infrastructure

### 13.1 KUnit Tests

```c
/* drivers/cxl/interconnect/tests/route_test.c */

#include <kunit/test.h>
#include "../route.h"

static void test_cxl_route_shortest_path(struct kunit *test)
{
    struct cxl_fabric *fabric;
    struct cxl_endpoint *ep0, *ep1;
    struct cxl_route route;
    int ret;

    /* Setup mock fabric */
    fabric = cxl_test_create_fabric(test, 4, 8); /* 4 switches, 8 endpoints */
    KUNIT_ASSERT_NOT_NULL(test, fabric);

    ep0 = cxl_test_get_endpoint(fabric, 0);
    ep1 = cxl_test_get_endpoint(fabric, 5);

    /* Compute route */
    ret = cxl_route_compute(fabric, ep0, ep1, &route);
    KUNIT_EXPECT_EQ(test, ret, 0);
    KUNIT_EXPECT_LE(test, route.hop_count, 5);

    cxl_test_destroy_fabric(fabric);
}

static struct kunit_case cxl_route_test_cases[] = {
    KUNIT_CASE(test_cxl_route_shortest_path),
    KUNIT_CASE(test_cxl_route_failover),
    KUNIT_CASE(test_cxl_route_multipath),
    {}
};

static struct kunit_suite cxl_route_test_suite = {
    .name = "cxl_route",
    .test_cases = cxl_route_test_cases,
};

kunit_test_suite(cxl_route_test_suite);
```

---

## 14. Next Steps

1. **Prototype on QEMU**: Develop QEMU emulation of CXL fabric for testing
2. **Hardware Bring-Up**: Test on real PCIe Gen6 switches with CXL support
3. **Upstream Submission**: Submit RFC patches to Linux kernel mailing list
4. **Performance Validation**: Benchmark against InfiniBand using OSU micro-benchmarks

---

**Document Status:** Ready for implementation
**Estimated LOC:** ~15,000 lines of kernel code
