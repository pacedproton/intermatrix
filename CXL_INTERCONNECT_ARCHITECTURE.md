# CXL Interconnect Linux Software Stack Architecture
## Project: InterMatrix - CXL as Low-Cost High-Speed Interconnect

**Version:** 1.0
**Date:** 2025-11-19
**Status:** Architecture Planning Phase

---

## Executive Summary

This document defines the complete Linux systems programming stack required to transform CXL (Compute Express Link) from a memory expansion technology into a low-cost, high-speed interconnect replacement for InfiniBand and high-rate Ethernet.

**Core Value Proposition:**
- **Eliminate NIC Tax**: Use CPU's native PCIe/CXL lanes instead of $1K-$3K NICs
- **Sub-microsecond Latency**: 200-400ns vs 2-10μs for IB/Ethernet
- **Memory Semantics**: Direct load/store bypassing kernel network stack
- **Commodity Optics**: Standard QSFP-DD using Ethernet supply chain

---

## 1. Architecture Overview

### 1.1 Stack Layers

```
┌─────────────────────────────────────────────────────────────┐
│         Application Layer (AI/ML, HPC, Storage)             │
├─────────────────────────────────────────────────────────────┤
│  LibCXL - Memory Semantic API │  Compatibility Layers       │
│  (Direct Memory Access)        │  (RDMA, Socket Emulation)  │
├─────────────────────────────────────────────────────────────┤
│  CXL Runtime & Orchestration (Userspace)                    │
│  - Resource Manager  - Fabric Discovery  - QoS Controller   │
├─────────────────────────────────────────────────────────────┤
│  Kernel CXL Interconnect Subsystem                          │
│  - cxl_fabric  - cxl_memnet  - cxl_switch  - cxl_route      │
├─────────────────────────────────────────────────────────────┤
│  Linux Kernel Core Extensions                               │
│  - MM Subsystem    - Device Tree    - PCIe/CXL Drivers      │
├─────────────────────────────────────────────────────────────┤
│  Hardware Abstraction Layer (HAL)                           │
│  - PCIe Gen5/6 Root Complex - CXL 3.0/3.1 PHY               │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. Kernel-Level Components

### 2.1 Core CXL Interconnect Subsystem (`drivers/cxl/interconnect/`)

#### **2.1.1 CXL Fabric Manager (`cxl_fabric.c`)**

**Purpose:** Discover, enumerate, and manage CXL fabric topology including switches, endpoints, and optical links.

**Key Functions:**
```c
// Fabric topology discovery
int cxl_fabric_scan(struct cxl_fabric *fabric);
int cxl_fabric_enumerate_switches(struct cxl_fabric *fabric);
int cxl_fabric_build_route_table(struct cxl_fabric *fabric);

// Dynamic reconfiguration
int cxl_fabric_hotplug_device(struct cxl_device *dev);
int cxl_fabric_remove_device(struct cxl_device *dev);

// Fabric-level QoS
int cxl_fabric_set_bandwidth_allocation(struct cxl_port *port, u64 bw_mbps);
```

**Data Structures:**
```c
struct cxl_fabric {
    struct list_head switches;        // CXL fabric switches
    struct list_head endpoints;       // CXL memory endpoints
    struct cxl_route_table *routes;   // Port-based routing table
    struct mutex fabric_lock;         // Fabric modification lock
    u32 fabric_id;                    // Global fabric identifier
};

struct cxl_switch {
    struct device dev;
    u16 num_ports;
    struct cxl_port *ports;           // Port array
    u32 switch_id;
    struct cxl_switch_caps caps;      // Hardware capabilities
};

struct cxl_port {
    u16 port_id;
    enum cxl_port_type type;          // Upstream/Downstream
    struct cxl_switch *parent_switch;
    struct cxl_device *connected_device;
    struct cxl_qos_params qos;
};
```

**Integration Points:**
- Linux Device Tree for hardware topology
- Sysfs interface: `/sys/class/cxl_fabric/`
- Netlink API for userspace fabric control

---

#### **2.1.2 CXL Memory Network Driver (`cxl_memnet.c`)**

**Purpose:** Expose remote CXL memory as network-accessible resources using memory semantics instead of packet switching.

**Key Functions:**
```c
// Remote memory region management
struct cxl_memnet_region *cxl_memnet_alloc_remote(
    struct cxl_endpoint *ep,
    size_t size,
    gfp_t flags
);

// Zero-copy memory operations
int cxl_memnet_put(struct cxl_memnet_region *region,
                   void *data, size_t len, loff_t offset);
int cxl_memnet_get(struct cxl_memnet_region *region,
                   void *buf, size_t len, loff_t offset);

// RDMA-like verbs (compatibility)
struct cxl_memnet_qp *cxl_memnet_create_qp(struct cxl_memnet_ctx *ctx);
int cxl_memnet_post_send(struct cxl_memnet_qp *qp, struct cxl_send_wr *wr);
```

**Data Structures:**
```c
struct cxl_memnet_region {
    dma_addr_t remote_dma_addr;       // Physical address on remote node
    void __iomem *local_mapping;      // Kernel virtual mapping
    size_t size;
    struct cxl_endpoint *owner;       // Remote endpoint
    atomic_t refcount;
    u32 access_flags;                 // Read/Write/Atomic permissions
};

struct cxl_endpoint {
    struct device dev;
    u64 total_memory;                 // Total exportable memory
    u64 available_memory;             // Free memory
    struct list_head regions;         // Allocated regions
    struct cxl_port *fabric_port;     // Connection to fabric
    u32 endpoint_id;                  // Unique ID in fabric
};
```

**Key Innovation:**
- Uses **CXL.mem protocol** (not CXL.io) for direct memory access
- Implements **flit-based data transfer** instead of packet framing
- Bypasses TCP/IP stack entirely

---

#### **2.1.3 CXL Routing Engine (`cxl_route.c`)**

**Purpose:** Implement port-based routing (CXL 3.0 spec) for fabric-wide memory addressing.

**Key Functions:**
```c
// Route computation (shortest path with QoS)
int cxl_route_compute(struct cxl_fabric *fabric,
                      struct cxl_endpoint *src,
                      struct cxl_endpoint *dst,
                      struct cxl_route *route);

// Route programming into switch hardware
int cxl_route_program_switch(struct cxl_switch *sw,
                              struct cxl_route *route);

// Dynamic rerouting (failure recovery)
int cxl_route_failover(struct cxl_fabric *fabric,
                       struct cxl_port *failed_port);
```

**Routing Algorithm:**
- **Primary:** Shortest Path First (SPF) with bandwidth awareness
- **Secondary:** Multipath routing for load balancing
- **Failover:** Automatic reroute on link failure (<100ms recovery)

---

#### **2.1.4 CXL Switch Driver (`cxl_switch.c`)**

**Purpose:** Control CXL fabric switches (PCIe Gen6 switch chips with CXL extensions).

**Key Functions:**
```c
// Switch initialization
int cxl_switch_init(struct pci_dev *pdev);
int cxl_switch_configure_ports(struct cxl_switch *sw);

// Port management
int cxl_switch_enable_port(struct cxl_port *port);
int cxl_switch_disable_port(struct cxl_port *port);

// Telemetry
int cxl_switch_read_counters(struct cxl_switch *sw,
                              struct cxl_switch_stats *stats);
```

**Vendor Abstraction:**
- Support for Broadcom, Microchip, XConn switch chips
- Unified API hiding vendor-specific register layouts

---

### 2.2 Memory Management Extensions (`mm/cxl_mm.c`)

#### **2.2.1 Global Memory Pool**

**Purpose:** Create a unified memory pool spanning all CXL-connected nodes (the "Petalith" architecture).

**Key Functions:**
```c
// Pool initialization
int cxl_mm_init_global_pool(void);

// NUMA-aware allocation
void *cxl_mm_alloc_pages(size_t size, int numa_node, gfp_t flags);
void cxl_mm_free_pages(void *addr, size_t size);

// Transparent page migration
int cxl_mm_migrate_pages(struct vm_area_struct *vma,
                         int target_node);
```

**Integration:**
- Extends Linux NUMA subsystem with CXL-aware zones
- Works with existing `mbind()` and `move_pages()` syscalls
- Automatic page migration based on access patterns

---

### 2.3 Device Drivers

#### **2.3.1 CXL Optical Cable Driver (`drivers/cxl/cxl_optics.c`)**

**Purpose:** Manage active optical cables (Smart Cable Modules) that extend CXL over meters/kilometers.

**Key Functions:**
```c
// Cable detection and initialization
int cxl_optics_probe(struct device *dev);
int cxl_optics_configure_link(struct cxl_optics *optics, u32 speed_gbps);

// Link monitoring
int cxl_optics_get_link_status(struct cxl_optics *optics,
                                struct cxl_link_status *status);

// Power management
int cxl_optics_set_power_mode(struct cxl_optics *optics,
                               enum cxl_power_mode mode);
```

**Standards Compliance:**
- QSFP-DD (400G) and OSFP form factors
- Uses standard SFF-8636/CMIS management interface
- Works with Astera Labs, Broadcom, Intel optical modules

---

## 3. Userspace Components

### 3.1 CXL Runtime Library (`libcxl`)

#### **3.1.1 Memory Semantic API (`libcxl/cxl_mem.c`)**

**Purpose:** Provide applications with direct load/store access to remote CXL memory.

**API Example:**
```c
#include <libcxl/cxl_mem.h>

// Open remote memory region
cxl_mem_t *mem = cxl_mem_open("/dev/cxl/fabric0/endpoint5",
                              CXL_MEM_RW);

// Direct memory operations (zero-copy)
uint64_t *remote_ptr = cxl_mem_map(mem, 0, 1UL << 30); // 1GB
remote_ptr[1024] = 0xDEADBEEF;  // Store directly to remote memory
uint64_t val = remote_ptr[2048]; // Load from remote memory

// Atomic operations
cxl_mem_atomic_add(mem, offset, 1);
cxl_mem_compare_swap(mem, offset, old_val, new_val);

cxl_mem_close(mem);
```

**Key Features:**
- **mmap()** interface for transparent remote memory access
- CPU cache coherency via CXL.cache protocol
- Atomic operations for distributed synchronization

---

#### **3.1.2 RDMA Compatibility Layer (`libcxl/cxl_verbs.c`)**

**Purpose:** Provide drop-in replacement for `libibverbs` to migrate RDMA applications to CXL.

**API Example:**
```c
// Standard RDMA verbs API, backed by CXL
struct ibv_context *ctx = ibv_open_device(cxl_device);
struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);

// Post RDMA Write (mapped to CXL store)
ibv_post_send(qp, &wr, &bad_wr);
```

**Implementation:**
- Translates `ibv_post_send()` to CXL memory writes
- Emulates RDMA completion queues using interrupts
- Binary compatible with existing RDMA applications

---

#### **3.1.3 Socket Emulation Layer (`libcxl/cxl_socket.c`)**

**Purpose:** Allow legacy TCP/UDP applications to transparently use CXL for intra-fabric communication.

**API Example:**
```c
// LD_PRELOAD wrapper for socket()
int sock = socket(AF_CXL, SOCK_STREAM, 0);
connect(sock, (struct sockaddr_cxl *)&addr, sizeof(addr));

// send() translated to CXL memory copy
send(sock, buffer, size, 0);
```

**Use Case:** Enable existing distributed applications (databases, message queues) to gain CXL's low latency without code changes.

---

### 3.2 CXL Fabric Manager Daemon (`cxlfmd`)

#### **3.2.1 Fabric Discovery Service**

**Purpose:** Continuously discover new CXL devices and maintain live topology map.

**Functionality:**
- Monitors sysfs `/sys/class/cxl_fabric/` for hotplug events
- Builds fabric graph (switches, endpoints, routes)
- Publishes topology via D-Bus for monitoring tools

---

#### **3.2.2 Resource Orchestration**

**Purpose:** Allocate and manage CXL memory resources across the fabric.

**Functionality:**
```yaml
# Example policy (YAML config)
memory_pools:
  - name: "ai_training_pool"
    size: "2TB"
    numa_nodes: [0, 1, 2, 3]
    qos_priority: high

  - name: "storage_cache_pool"
    size: "512GB"
    numa_nodes: [4, 5]
    qos_priority: medium
```

**API:**
```c
// Request memory from pool
cxl_pool_handle_t pool = cxl_pool_open("ai_training_pool");
void *mem = cxl_pool_alloc(pool, 100UL << 30); // 100GB
```

---

#### **3.2.3 QoS Controller**

**Purpose:** Enforce bandwidth and latency guarantees across fabric.

**Functionality:**
- Traffic shaping at switch ports
- Priority queuing for critical workloads
- Congestion avoidance using fabric backpressure

**Configuration:**
```json
{
  "qos_classes": [
    {"name": "realtime", "max_latency_ns": 500, "min_bandwidth_gbps": 100},
    {"name": "bulk", "max_latency_ns": 10000, "min_bandwidth_gbps": 10}
  ]
}
```

---

### 3.3 Monitoring & Telemetry (`cxl-telemetry`)

#### **3.3.1 Metrics Collection**

**Collected Metrics:**
- Bandwidth utilization per link (GB/s)
- Latency distribution (p50, p99, p999)
- Error rates (CRC errors, retries)
- Power consumption per device

**Export Formats:**
- Prometheus metrics endpoint
- JSON logs for Splunk/ELK
- Grafana dashboards

**Example Prometheus Metrics:**
```
cxl_fabric_link_bandwidth_gbps{switch="cxl0",port="5"} 95.2
cxl_fabric_link_latency_ns{src="node0",dst="node3",percentile="99"} 387
cxl_endpoint_memory_allocated_bytes{endpoint="ep2"} 549755813888
```

---

## 4. Application-Level APIs

### 4.1 High-Level Memory Semantic API

#### **4.1.1 Python Bindings (`pycxl`)**

```python
import pycxl

# Open remote memory
fabric = pycxl.Fabric("/dev/cxl/fabric0")
endpoint = fabric.endpoints["node3"]

# Allocate remote memory
remote_buf = endpoint.alloc(size=1024*1024*1024)  # 1GB

# NumPy-compatible interface for AI/ML
import numpy as np
arr = np.ndarray(shape=(1024, 1024), dtype=np.float32, buffer=remote_buf)
arr[0, 0] = 3.14  # Direct store to remote memory
```

---

#### **4.1.2 C++ Smart Pointers (`libcxl++`)**

```cpp
#include <cxl/memory.hpp>

// RAII remote memory
auto remote_mem = cxl::make_remote<float[]>(endpoint_id, 1000000);
remote_mem[999] = 42.0f;  // Transparent remote access

// STL allocator for distributed containers
std::vector<int, cxl::allocator<int>> distributed_vec(cxl_pool);
```

---

### 4.2 Framework Integration

#### **4.2.1 CUDA/HIP Unified Memory Extension**

**Purpose:** Allow GPUs to directly access CXL fabric memory.

```c
// Allocate GPU-accessible CXL memory
void *cxl_gpu_mem;
cudaMallocCXL(&cxl_gpu_mem, size, endpoint_id);

// GPU kernel accesses remote memory directly
gpu_kernel<<<blocks, threads>>>(cxl_gpu_mem);
```

**Benefit:** Eliminate the "NVIDIA NVLink Tax" by using open CXL instead.

---

#### **4.2.2 DPDK Integration**

**Purpose:** Allow high-performance packet processing frameworks to use CXL memory pools.

```c
// DPDK mempool backed by CXL
struct rte_mempool *mp = rte_pktmbuf_pool_create_cxl(
    "cxl_pool", 8192, 0, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
    cxl_endpoint_id
);
```

---

## 5. Implementation Roadmap

### Phase 1: Kernel Foundation (Months 1-4)
**Deliverables:**
- [ ] CXL fabric discovery and enumeration
- [ ] Basic CXL switch driver (Broadcom PCIe Gen6)
- [ ] CXL memory region allocation
- [ ] Sysfs interfaces

**Milestone:** Can enumerate CXL fabric and allocate remote memory regions.

---

### Phase 2: Memory Semantics (Months 5-8)
**Deliverables:**
- [ ] Zero-copy load/store operations
- [ ] CPU cache coherency (CXL.cache protocol)
- [ ] Atomic operations support
- [ ] NUMA-aware page allocation

**Milestone:** Applications can use remote CXL memory like local RAM.

---

### Phase 3: Routing & Fabric Management (Months 9-12)
**Deliverables:**
- [ ] Port-based routing engine
- [ ] Dynamic path computation (SPF algorithm)
- [ ] Failover and redundancy
- [ ] `cxlfmd` userspace daemon

**Milestone:** Multi-switch fabrics with automatic routing.

---

### Phase 4: Optical Links & Distance (Months 13-16)
**Deliverables:**
- [ ] Optical cable driver (QSFP-DD/OSFP)
- [ ] Long-distance link management
- [ ] Link error detection and recovery
- [ ] Power management

**Milestone:** CXL spans rack-to-rack (10+ meters).

---

### Phase 5: Performance & QoS (Months 17-20)
**Deliverables:**
- [ ] Bandwidth allocation and traffic shaping
- [ ] Latency-sensitive QoS classes
- [ ] Congestion control
- [ ] Performance benchmarking vs. InfiniBand

**Target:** <400ns latency, >200 GB/s aggregate bandwidth.

---

### Phase 6: Application Ecosystem (Months 21-24)
**Deliverables:**
- [ ] RDMA verbs compatibility layer
- [ ] Socket API emulation
- [ ] Python/C++ bindings
- [ ] CUDA/HIP integration
- [ ] Migration guides for IB/Ethernet apps

**Milestone:** Major AI frameworks (PyTorch, TensorFlow) run on CXL.

---

## 6. Migration Strategy: From InfiniBand/Ethernet to CXL

### 6.1 Compatibility Modes

#### **Mode 1: Native CXL**
- Applications rewritten to use `libcxl` memory semantic API
- **Best Performance:** <400ns latency
- **Use Case:** New AI/ML workloads

#### **Mode 2: RDMA Emulation**
- Existing RDMA apps use `libibverbs` → CXL translation
- **Performance:** ~1-2μs latency (overhead from emulation)
- **Use Case:** Legacy HPC applications

#### **Mode 3: Socket Emulation**
- TCP/UDP apps transparently use CXL via `LD_PRELOAD`
- **Performance:** ~2-5μs latency
- **Use Case:** Databases, message queues

---

### 6.2 Hybrid Deployments

**Coexistence Strategy:**
```
┌─────────────┐                  ┌─────────────┐
│  Legacy IB  │ ◄──────IB────► │  Legacy IB  │
│   Cluster   │                  │   Cluster   │
└──────┬──────┘                  └──────┬──────┘
       │                                │
       └──────────── CXL Gateway ────────┘
                         │
                    ┌────▼────┐
                    │   CXL   │
                    │ Fabric  │
                    └─────────┘
```

**CXL Gateway:** Protocol translator allowing IB and CXL clusters to interoperate.

---

## 7. Cost Analysis

### 7.1 Hardware Cost Comparison (Per Node)

| Component | InfiniBand NDR | 400G Ethernet | CXL Interconnect |
|-----------|----------------|---------------|------------------|
| **NIC/HCA** | $2,500 | $1,800 | $0 (CPU integrated) |
| **Cable (3m)** | $150 | $80 | $100 (optical) |
| **Switch Port** | $1,200 | $800 | $300 (PCIe switch) |
| **Total** | **$3,850** | **$2,680** | **$400** |
| **Savings** | Baseline | 30% | **90%** |

---

### 7.2 TCO Analysis (100-Node Cluster, 3 Years)

| Cost Category | InfiniBand | CXL Interconnect | Savings |
|---------------|------------|------------------|---------|
| **Hardware** | $385,000 | $40,000 | $345,000 |
| **Power (3yr)** | $90,000 | $30,000 | $60,000 |
| **Cooling (3yr)** | $45,000 | $15,000 | $30,000 |
| **Maintenance** | $50,000 | $10,000 | $40,000 |
| **Total TCO** | **$570,000** | **$95,000** | **$475,000 (83%)** |

---

## 8. Performance Targets

### 8.1 Latency

| Metric | Target | Baseline (IB) | Improvement |
|--------|--------|---------------|-------------|
| **Load/Store** | <200ns | 2,000ns | 10x |
| **Remote memcpy** | <400ns | 3,000ns | 7.5x |
| **Atomic CAS** | <300ns | 2,500ns | 8x |

---

### 8.2 Bandwidth

| Metric | Target | Baseline (IB NDR) |
|--------|--------|-------------------|
| **Unidirectional** | 128 GB/s | 100 GB/s |
| **Bidirectional** | 256 GB/s | 200 GB/s |
| **Multi-path Aggregate** | >1 TB/s | 800 GB/s |

---

### 8.3 Scalability

| Metric | Target | Baseline (IB) |
|--------|--------|---------------|
| **Max Endpoints** | 4,096 | 2,000 |
| **Max Fabric Diameter** | 10 hops | 5 hops |
| **Failover Time** | <100ms | <500ms |

---

## 9. Security Considerations

### 9.1 Threat Model

**Attack Vectors:**
1. **Memory Scraping:** Unauthorized read of remote memory
2. **DMA Attacks:** Malicious device writing to protected memory
3. **Fabric Hijacking:** Rogue switch in fabric topology
4. **Side Channels:** Timing attacks via memory access patterns

---

### 9.2 Security Mechanisms

#### **9.2.1 Hardware Isolation**
- **PCIe ACS (Access Control Services):** Prevent peer-to-peer attacks
- **IOMMU/SMMU:** Isolate device DMA to authorized memory regions
- **CXL HDM Decoders:** Hardware-enforced memory range protection

#### **9.2.2 Cryptographic Protection**
- **TLS for Fabric Control:** Encrypt management traffic
- **Data-at-Rest Encryption:** AES-256 for CXL memory (optional)
- **Attestation:** TPM-based device authentication

#### **9.2.3 Kernel Security**
```c
// Example: Permission checks for remote memory access
int cxl_memnet_alloc_remote(...) {
    if (!capable(CAP_SYS_RAWIO)) {
        return -EPERM;
    }

    // Check cgroup memory limits
    if (cxl_cgroup_charge(current->cgroups, size) < 0) {
        return -ENOMEM;
    }

    // Proceed with allocation
}
```

---

## 10. Testing & Validation

### 10.1 Unit Tests

**Kernel:**
- `kunit` tests for CXL fabric discovery
- Mock CXL switches for route computation tests
- Fault injection for error handling paths

**Userspace:**
- `pytest` for Python bindings
- Google Test for C++ API
- Stress tests for memory allocation

---

### 10.2 Integration Tests

**Test Scenarios:**
1. **Hotplug:** Add/remove CXL device while workload running
2. **Failover:** Disconnect link and verify automatic reroute
3. **Multi-tenant:** Isolate memory access between containers
4. **Performance:** Benchmark latency/bandwidth vs. InfiniBand

**Test Environment:**
```
┌──────────┐     CXL      ┌──────────┐     CXL      ┌──────────┐
│  Node 0  │ ◄──────────► │  Switch  │ ◄──────────► │  Node 1  │
│ (Client) │              │   (DUT)  │              │ (Server) │
└──────────┘              └──────────┘              └──────────┘
```

---

### 10.3 Benchmarks

**Standard Benchmarks:**
- **OSU Micro-Benchmarks:** Latency, bandwidth
- **STREAM:** Memory bandwidth
- **Graph500:** Random access performance
- **AI Workloads:** ResNet-50 training on distributed memory

**Target Metrics:**
- OSU latency: <400ns (vs. 2μs for IB)
- STREAM bandwidth: >200 GB/s per link
- Graph500 TEPS: >2x InfiniBand baseline

---

## 11. Open Source Strategy

### 11.1 Licensing

**Kernel Components:** GPL-2.0 (required for kernel inclusion)
**Userspace:** LGPL-2.1 (allows proprietary app linkage)
**Tools/Daemons:** Apache-2.0 (maximum flexibility)

---

### 11.2 Community Engagement

**Upstream Targets:**
1. **Linux Kernel:** Submit patches to `drivers/cxl/` maintainers
2. **QEMU:** Add CXL fabric emulation for testing
3. **Buildroot/Yocto:** Provide prebuilt CXL-enabled images

**Governance:**
- Form CXL Interconnect Special Interest Group (SIG)
- Quarterly meetings with hardware vendors
- Public roadmap and RFC process

---

## 12. References

### 12.1 Specifications

- **CXL 3.1 Specification** (Compute Express Link Consortium)
- **PCIe 6.0 Base Specification** (PCI-SIG)
- **ACPI 6.5** (UEFI Forum) - CXL Early Discovery Table (CEDT)

### 12.2 Prior Art

- **Linux CXL Subsystem:** `drivers/cxl/` (Dan Williams, Intel)
- **RDMA Core:** `libibverbs`, `rdma-core`
- **DPDK:** Data Plane Development Kit
- **Astera Labs LEO SDK:** Smart cable module reference

---

## 13. Glossary

- **CXL.cache:** Cache coherency protocol for CPU-device coherence
- **CXL.mem:** Memory protocol for load/store access
- **CXL.io:** PCIe-equivalent I/O protocol
- **Flit:** Flow Control Unit - CXL's atomic data transfer unit
- **HDM:** Host-managed Device Memory - CXL memory range descriptor
- **Petalith:** Rack-scale unified memory (>1 PB)
- **Port-Based Routing:** CXL 3.0 fabric routing via switch port tables

---

## Appendix A: Code Structure

```
intermatrix/
├── kernel/
│   ├── drivers/cxl/interconnect/
│   │   ├── cxl_fabric.c          # Fabric manager
│   │   ├── cxl_memnet.c          # Memory network driver
│   │   ├── cxl_route.c           # Routing engine
│   │   ├── cxl_switch.c          # Switch driver
│   │   └── cxl_optics.c          # Optical cable driver
│   ├── mm/cxl_mm.c               # Memory management extensions
│   └── include/linux/cxl/
│       ├── cxl_fabric.h
│       └── cxl_memnet.h
├── userspace/
│   ├── libcxl/                   # Core library
│   │   ├── cxl_mem.c             # Memory API
│   │   ├── cxl_verbs.c           # RDMA compatibility
│   │   └── cxl_socket.c          # Socket emulation
│   ├── cxlfmd/                   # Fabric manager daemon
│   │   ├── discovery.c
│   │   ├── orchestrator.c
│   │   └── qos.c
│   ├── cxl-telemetry/            # Monitoring
│   │   ├── prometheus.c
│   │   └── grafana-dashboards/
│   └── tools/
│       ├── cxl-cli                # CLI tool
│       └── cxl-benchmark          # Performance testing
├── bindings/
│   ├── python/pycxl/             # Python bindings
│   ├── cpp/libcxl++/             # C++ smart pointers
│   └── rust/cxl-rs/              # Rust bindings
├── tests/
│   ├── kunit/                    # Kernel unit tests
│   ├── integration/              # End-to-end tests
│   └── benchmarks/               # Performance tests
└── docs/
    ├── api/                      # API documentation
    ├── guides/                   # User guides
    └── design/                   # Design documents
```

---

**Document Status:** DRAFT - Ready for technical review
**Next Steps:** Review with kernel maintainers and CXL consortium
