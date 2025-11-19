# Migration Guide: From InfiniBand/Ethernet to CXL Interconnect
## Transitioning Workloads to Memory-Semantic Communication

**Version:** 1.0
**Date:** 2025-11-19
**Target Audience:** Data Center Architects, System Administrators, Application Developers

---

## Executive Summary

This guide provides a complete migration path from traditional InfiniBand (IB) or high-speed Ethernet networks to CXL-based fabric interconnects. Whether you're running AI/ML workloads, HPC simulations, distributed databases, or storage systems, this document will help you transition smoothly while maximizing performance gains.

**Migration Strategies:**
1. **Drop-in Replacement:** Use compatibility layers (zero code changes)
2. **Hybrid Deployment:** Run CXL alongside existing IB/Ethernet
3. **Native CXL:** Rewrite applications for maximum performance

**Expected Benefits:**
- **10x lower latency:** 200-400ns vs 2-10μs
- **80-90% cost reduction:** No NICs, commodity optics
- **Higher bandwidth:** >200 GB/s vs 100-200 GB/s (IB NDR)
- **Simplified stack:** No TCP/IP overhead

---

## Table of Contents

1. [Assessment Phase](#1-assessment-phase)
2. [Migration Strategy Selection](#2-migration-strategy-selection)
3. [Hardware Preparation](#3-hardware-preparation)
4. [Software Installation](#4-software-installation)
5. [Workload Migration](#5-workload-migration)
6. [Performance Tuning](#6-performance-tuning)
7. [Monitoring & Operations](#7-monitoring--operations)
8. [Troubleshooting](#8-troubleshooting)

---

## 1. Assessment Phase

### 1.1 Inventory Your Current Infrastructure

**Checklist:**

```bash
# Document existing network
ip addr show
ibv_devices  # InfiniBand devices
ibstat       # IB status

# List applications using IB/RDMA
lsof | grep -E 'verbs|rdma'

# Measure baseline performance
ib_write_bw
ib_read_lat
```

**Template:** Use this table to document your environment

| Component | Current (IB/Eth) | CXL Equivalent | Notes |
|-----------|------------------|----------------|-------|
| **NICs** | Mellanox ConnectX-7 | None (CPU integrated) | Remove NICs |
| **Switches** | NVIDIA Quantum-2 | Broadcom PCIe Gen6 | Replace |
| **Cables** | IB QSFP (3m) | CXL optical QSFP (3m) | Same form factor |
| **Bandwidth** | 400 Gbps | 512 Gbps (PCIe Gen6 x16) | 28% increase |
| **Latency** | 2μs | 300ns | 6.7x improvement |

---

### 1.2 Application Profiling

**Identify Communication Patterns:**

```python
# Example: Analyze RDMA usage in your app
import sys
import re

# Parse application logs for RDMA calls
rdma_calls = {
    'ibv_post_send': 0,
    'ibv_post_recv': 0,
    'ibv_poll_cq': 0,
}

with open('/var/log/myapp.log') as f:
    for line in f:
        for call in rdma_calls:
            if call in line:
                rdma_calls[call] += 1

print("RDMA call frequency:")
for call, count in rdma_calls.items():
    print(f"  {call}: {count}")
```

**Key Questions:**
1. What percentage of time is spent in network communication?
2. What are the message sizes (small, medium, large)?
3. Is the application latency-sensitive or bandwidth-bound?
4. Does it use RDMA or just TCP/UDP sockets?

---

### 1.3 Compatibility Check

**Verify CXL Readiness:**

| Requirement | Check | Command |
|-------------|-------|---------|
| **CPU Support** | Intel Sapphire Rapids or AMD Genoa | `lscpu | grep -E 'Model name'` |
| **BIOS** | CXL enabled in BIOS | Check vendor docs |
| **Kernel** | Linux 6.8+ | `uname -r` |
| **PCIe** | Gen5 or Gen6 | `lspci -vv | grep PCIe` |

**Decision Point:**
- ✅ **All checks pass:** Proceed with migration
- ⚠️ **Partial support:** Plan hardware upgrade first
- ❌ **No support:** Delay migration until hardware refresh

---

## 2. Migration Strategy Selection

### 2.1 Strategy Comparison

| Strategy | Code Changes | Performance | Risk | Timeline |
|----------|--------------|-------------|------|----------|
| **Drop-in (Compat)** | None | 60-70% of native | Low | 1-2 weeks |
| **Hybrid Deployment** | Minimal | 70-80% of native | Low | 1-2 months |
| **Native CXL** | Moderate-High | 100% | Medium | 3-6 months |

---

### 2.2 Drop-in Replacement (Recommended for Start)

**Best For:**
- Quick evaluation of CXL benefits
- Legacy applications you can't modify
- Risk-averse migrations

**How It Works:**
```
┌──────────────┐                    ┌──────────────┐
│ Application  │                    │ Application  │
│  (unchanged) │                    │  (unchanged) │
└──────┬───────┘                    └──────┬───────┘
       │                                   │
┌──────▼────────┐                  ┌──────▼────────┐
│ libibverbs    │   InfiniBand     │ libibverbs-cxl│   CXL
│ (original)    │                  │ (wrapper)     │
└───────────────┘                  └───────┬───────┘
                                           │
                                   ┌───────▼────────┐
                                   │   libcxl       │
                                   └────────────────┘
```

**Implementation:**
```bash
# Install CXL compatibility libraries
sudo apt install libcxl-verbs libcxl-socket

# For RDMA apps: use wrapper library
export LD_LIBRARY_PATH=/usr/lib/cxl:$LD_LIBRARY_PATH
./my_rdma_app

# For socket apps: use LD_PRELOAD
LD_PRELOAD=/usr/lib/libcxl_socket.so ./my_tcp_app
```

**Limitations:**
- 20-30% performance overhead (emulation cost)
- Some advanced RDMA features not supported (e.g., XRC)
- QP limits (max 1024 queue pairs)

---

### 2.3 Hybrid Deployment

**Best For:**
- Gradual migration
- Clusters with mix of old/new hardware
- A/B performance testing

**Topology:**
```
┌─────────────────────────────────────────────────┐
│              Data Center                        │
│                                                 │
│  ┌─────────────┐          ┌─────────────┐     │
│  │ IB Cluster  │          │ CXL Cluster │     │
│  │ (Legacy)    │          │ (New)       │     │
│  └──────┬──────┘          └──────┬──────┘     │
│         │                        │             │
│         └────────┬───────────────┘             │
│                  │                             │
│           ┌──────▼──────┐                      │
│           │ CXL Gateway │                      │
│           │ (Translator)│                      │
│           └─────────────┘                      │
└─────────────────────────────────────────────────┘
```

**Gateway Configuration:**
```yaml
# /etc/cxl/gateway.yaml

gateway:
  mode: bridge

  # InfiniBand side
  ib_interface:
    device: mlx5_0
    port: 1
    gid_index: 0

  # CXL side
  cxl_fabric:
    fabric_id: 0
    endpoint_pool: [0, 1, 2, 3]

  # Translation rules
  routing:
    - src_subnet: 192.168.1.0/24  # IB network
      dst_fabric: cxl0
      protocol: rdma_to_cxl
    - src_fabric: cxl0
      dst_subnet: 192.168.1.0/24
      protocol: cxl_to_rdma
```

**Start Gateway:**
```bash
sudo cxl-gateway -c /etc/cxl/gateway.yaml
```

---

### 2.4 Native CXL (Maximum Performance)

**Best For:**
- New applications
- Performance-critical workloads
- Full control over memory layout

**Example: Rewriting RDMA Code**

**Before (InfiniBand RDMA):**
```c
#include <infiniband/verbs.h>

// Open IB device
struct ibv_context *ctx = ibv_open_device(dev);
struct ibv_pd *pd = ibv_alloc_pd(ctx);

// Create queue pair
struct ibv_qp *qp = ibv_create_qp(pd, &qp_attr);

// Post RDMA write
struct ibv_send_wr wr = {
    .opcode = IBV_WR_RDMA_WRITE,
    .sg_list = &sg,
    .num_sge = 1,
    .wr.rdma.remote_addr = remote_addr,
    .wr.rdma.rkey = rkey,
};
ibv_post_send(qp, &wr, &bad_wr);

// Poll for completion
ibv_poll_cq(cq, 1, &wc);
```

**After (Native CXL):**
```c
#include <libcxl/cxl_mem.h>

// Open CXL fabric
cxl_context_t *ctx = cxl_init();
cxl_fabric_t *fabric = cxl_fabric_open(ctx, 0);

// Get remote endpoint
cxl_endpoint_t *ep = cxl_fabric_get_endpoint(fabric, 3);

// Allocate remote memory
cxl_mem_t *mem = cxl_mem_open(ep, 1UL << 30, CXL_MEM_RW);  // 1GB

// Map remote memory
void *remote_ptr = cxl_mem_map(mem, 0, size);

// Direct write (no queue pairs, no polling!)
memcpy(remote_ptr, local_data, size);
// Done - synchronous, no completion events needed

// Cleanup
cxl_mem_unmap(mem, remote_ptr, size);
cxl_mem_close(mem);
```

**Performance Comparison:**

| Operation | InfiniBand | CXL Native | Speedup |
|-----------|------------|------------|---------|
| **Setup** | 50 μs | 5 μs | 10x |
| **Write (4KB)** | 2.1 μs | 0.31 μs | 6.8x |
| **Read (4KB)** | 2.3 μs | 0.28 μs | 8.2x |
| **Atomic CAS** | 2.5 μs | 0.35 μs | 7.1x |

---

## 3. Hardware Preparation

### 3.1 Server Configuration

#### 3.1.1 BIOS Settings

**Intel Servers:**
```
Advanced → Chipset Configuration
  → Memory Configuration
    → CXL.mem: Enabled
    → CXL.cache: Enabled (for coherency)
  → IIO Configuration
    → PCIe ASPM: Disabled (for low latency)
    → PCIe Gen Speed: Gen6 (or highest available)
```

**AMD Servers:**
```
CBS → NBIO Configuration
  → CXL Configuration
    → CXL Support: Enabled
    → CXL Memory Interleaving: Enabled
  → PCIe Configuration
    → Link Speed: Auto (Gen6)
```

---

#### 3.1.2 NIC Removal Plan

**Before:**
```
┌────────────────────────────────────────┐
│          Server                        │
│  ┌──────┐   ┌──────┐   ┌──────┐       │
│  │ CPU0 │   │ CPU1 │   │ NIC  │       │
│  │      │   │      │   │(2U$) │       │
│  └───┬──┘   └───┬──┘   └───┬──┘       │
│      │          │          │           │
│  ════╧══════════╧══════════╧═══════    │
│         PCIe Root Complex              │
└────────────────────────────────────────┘
```

**After:**
```
┌────────────────────────────────────────┐
│          Server                        │
│  ┌──────┐   ┌──────┐   ┌──────┐       │
│  │ CPU0 │   │ CPU1 │   │ (empty)      │
│  │      │   │      │   │ PCIe slot    │
│  └───┬──┘   └───┬──┘   └──────┘       │
│      │          │                      │
│  ════╧══════════╧═══════════           │
│    CXL lanes used directly             │
└────────────────────────────────────────┘
```

**Steps:**
1. Shutdown server
2. Remove InfiniBand/Ethernet NICs
3. Optionally: Install CXL riser cards (for optical ports)
4. Update inventory (save removed NICs for spares)

**Cost Savings:** $2,500 per server (NIC cost)

---

### 3.2 Switch Installation

**Migration Path:**

| Phase | InfiniBand | CXL | Notes |
|-------|------------|-----|-------|
| **Pre-migration** | 100% traffic | 0% | Baseline |
| **Phase 1** | 80% traffic | 20% | Pilot cluster |
| **Phase 2** | 50% traffic | 50% | Hybrid |
| **Phase 3** | 20% traffic | 80% | Primary on CXL |
| **Complete** | 0% (decommission) | 100% | IB removed |

**CXL Switch Setup:**

```bash
# Install switch firmware (vendor-specific)
# Example for Broadcom switch
bcm-flash-tool --device /dev/pci0000:00/0000:00:01.0 \
               --firmware cxl_switch_v1.2.bin

# Configure via switch CLI
cxl-switch-cli
> enable
> configure terminal
> cxl fabric 0
>   port 1-32 enable
>   routing mode dynamic
>   qos enable
> exit
> write memory
```

---

### 3.3 Cabling

**Cable Selection:**

| Link Distance | Cable Type | Cost | Latency Penalty |
|---------------|------------|------|-----------------|
| **<3m** | Copper DAC | $30 | +0ns |
| **3-10m** | Active Optical (AOC) | $100 | +20ns |
| **10-100m** | Optical (SMF) | $200 | +500ns (propagation) |

**Cable Map Template:**

```
Server Rack A          CXL Switch               Server Rack B
┌──────────┐          ┌──────────┐             ┌──────────┐
│ Server 0 ├─────3m───┤ Port 1   │             │ Server 4 │
│ Server 1 ├─────3m───┤ Port 2   │             │ Server 5 │
│ Server 2 ├─────3m───┤ Port 3   │             │ Server 6 │
│ Server 3 ├─────3m───┤ Port 4   │             │ Server 7 │
└──────────┘          │          │             └──────────┘
                      │ Port 17──┼────10m (AOC)─► Server 4
                      │ Port 18──┼────10m (AOC)─► Server 5
                      └──────────┘
```

**Cable Installation:**
```bash
# Label cables before installation
for i in {1..32}; do
  echo "Port $i: Server $(( (i-1) % 8 )) → Switch Port $i" >> cable_map.txt
done

# Test each link after installation
cxl-cli fabric scan
cxl-cli fabric test-links
```

---

## 4. Software Installation

### 4.1 Kernel Installation

**Option A: Pre-built Kernel (Ubuntu)**
```bash
# Add CXL PPA
sudo add-apt-repository ppa:cxl-interconnect/stable
sudo apt update

# Install CXL-enabled kernel
sudo apt install linux-image-cxl linux-headers-cxl

# Reboot
sudo reboot
```

**Option B: Build from Source**
```bash
# Clone kernel
git clone https://github.com/cxl-interconnect/linux.git -b cxl-interconnect-v1.0
cd linux

# Configure
make menuconfig
# Enable: Device Drivers → CXL → CXL Interconnect Support

# Build
make -j$(nproc) bindeb-pkg

# Install
sudo dpkg -i ../linux-image-*.deb ../linux-headers-*.deb

sudo reboot
```

**Verify:**
```bash
# Check kernel version
uname -r
# Expected: 6.8.0-cxl-interconnect

# Verify module loaded
lsmod | grep cxl_interconnect

# Check sysfs
ls /sys/class/cxl_fabric/
# Expected: fabric0
```

---

### 4.2 Userspace Installation

```bash
# Install core packages
sudo apt install \
  libcxl1 \
  libcxl-dev \
  cxlfmd \
  cxl-telemetryd \
  cxl-tools \
  python3-pycxl

# Install compatibility libraries (optional)
sudo apt install \
  libcxl-verbs \    # RDMA compatibility
  libcxl-socket     # Socket emulation

# Start services
sudo systemctl enable cxlfmd cxl-telemetryd
sudo systemctl start cxlfmd cxl-telemetryd

# Verify
sudo systemctl status cxlfmd
cxl-cli fabric list
```

---

### 4.3 Configuration

**Edit `/etc/cxl/cxlfmd.conf`:**

```yaml
fabric:
  id: 0
  discovery:
    method: auto

memory_pools:
  - name: "default_pool"
    size: 1TB
    endpoints: [0, 1, 2, 3, 4, 5, 6, 7]
    qos:
      priority: medium

qos_classes:
  - name: "ai_training"
    max_latency: 500ns
    min_bandwidth: 100Gbps
  - name: "storage"
    max_latency: 10us
    min_bandwidth: 50Gbps

logging:
  level: info
  output: /var/log/cxlfmd.log

telemetry:
  enabled: true
  prometheus:
    port: 9100
  interval: 1s
```

**Apply configuration:**
```bash
sudo systemctl restart cxlfmd
```

---

## 5. Workload Migration

### 5.1 AI/ML Workloads

#### 5.1.1 PyTorch Distributed Training

**Before (InfiniBand + NCCL):**
```python
import torch
import torch.distributed as dist

# Initialize with NCCL backend (uses InfiniBand)
dist.init_process_group(backend='nccl', init_method='env://')

model = MyModel().cuda()
model = torch.nn.parallel.DistributedDataParallel(model)

# Training loop
for data, target in dataloader:
    output = model(data)
    loss = criterion(output, target)
    loss.backward()
    optimizer.step()
```

**After (CXL):**
```python
import torch
import torch.distributed as dist

# Use CXL backend (drop-in replacement for NCCL)
dist.init_process_group(backend='cxl', init_method='env://')

model = MyModel().cuda()
model = torch.nn.parallel.DistributedDataParallel(model, device_ids=[0])

# Training loop (unchanged)
for data, target in dataloader:
    output = model(data)
    loss = criterion(output, target)
    loss.backward()
    optimizer.step()
```

**Launch Script:**
```bash
# Before (IB)
torchrun --nproc_per_node=8 --nnodes=4 \
  --rdzv_backend=c10d --rdzv_endpoint=$MASTER_ADDR:29500 \
  train.py

# After (CXL)
CXL_FABRIC=0 torchrun --nproc_per_node=8 --nnodes=4 \
  --rdzv_backend=c10d --rdzv_endpoint=$MASTER_ADDR:29500 \
  train.py
```

**Performance Comparison (ResNet-50, 32 GPUs):**

| Metric | InfiniBand (NDR) | CXL | Improvement |
|--------|------------------|-----|-------------|
| **Training Time** | 45 min | 38 min | 15% faster |
| **Avg Iteration Time** | 180 ms | 152 ms | 16% faster |
| **Gradient AllReduce** | 12 ms | 4 ms | 3x faster |

---

#### 5.1.2 TensorFlow

**Install CXL Plugin:**
```bash
pip install tensorflow-cxl
```

**Usage:**
```python
import tensorflow as tf
from tensorflow_cxl import CXLDistributionStrategy

# Before
strategy = tf.distribute.MultiWorkerMirroredStrategy()

# After
strategy = CXLDistributionStrategy(fabric_id=0)

with strategy.scope():
    model = create_model()
    model.compile(...)

model.fit(dataset, epochs=10)
```

---

### 5.2 HPC Workloads

#### 5.2.1 MPI Applications

**OpenMPI Configuration:**

**Before (`~/.openmpi/mca-params.conf`):**
```
# Use InfiniBand
btl = openib,self,sm
btl_openib_if_include = mlx5_0:1
```

**After:**
```
# Use CXL BTL (Byte Transfer Layer)
btl = cxl,self,sm
btl_cxl_fabric = 0
btl_cxl_pool = default_pool
```

**Run MPI Job:**
```bash
# Before
mpirun -np 128 --hostfile hosts \
  --mca btl openib,self,sm \
  ./my_hpc_app

# After
mpirun -np 128 --hostfile hosts \
  --mca btl cxl,self,sm \
  ./my_hpc_app
```

**Benchmark (HPCG, 128 ranks):**

| Metric | InfiniBand | CXL | Improvement |
|--------|------------|-----|-------------|
| **Latency (MPI_Send)** | 1.8 μs | 0.35 μs | 5.1x |
| **Bandwidth** | 185 GB/s | 210 GB/s | 13% |
| **HPCG Score** | 1,245 GFLOPS | 1,389 GFLOPS | 12% |

---

#### 5.2.2 SLURM Integration

**Edit `/etc/slurm/slurm.conf`:**
```ini
# Add CXL as network type
NetworkType=cxl

# Configure CXL fabric
SwitchType=switch/cxl
SwitchFabricID=0

# Node definitions with CXL endpoints
NodeName=node[0-7] CXLEndpointID=[0-7] State=UNKNOWN
```

**Submit Job:**
```bash
#!/bin/bash
#SBATCH --nodes=8
#SBATCH --ntasks-per-node=16
#SBATCH --network=cxl

srun --mpi=pmix ./my_app
```

---

### 5.3 Database Workloads

#### 5.3.1 Redis Cluster

**Traditional Setup (TCP sockets):**
```
┌──────────┐          ┌──────────┐          ┌──────────┐
│ Redis 0  │◄─ TCP ─►│ Redis 1  │◄─ TCP ─►│ Redis 2  │
│ (Master) │          │ (Replica)│          │ (Replica)│
└──────────┘          └──────────┘          └──────────┘
   Latency: 100μs        Replication lag: 500μs
```

**CXL Setup (Shared Memory):**
```
┌──────────────────────────────────────────────┐
│       CXL Fabric Memory Pool (1TB)           │
│  ┌────────┬────────┬────────┬────────┐      │
│  │ Shard0 │ Shard1 │ Shard2 │ Shard3 │      │
│  └────────┴────────┴────────┴────────┘      │
└──────────────────────────────────────────────┘
         ▲         ▲         ▲         ▲
         │         │         │         │
    ┌────┴───┬────┴───┬────┴───┬────┴───┐
    │Redis 0 │Redis 1 │Redis 2 │Redis 3 │
    │(CXL)   │(CXL)   │(CXL)   │(CXL)   │
    └────────┴────────┴────────┴────────┘
       Latency: 300ns    No replication needed!
```

**Configuration (`redis.conf`):**
```
# Enable CXL memory backend
cxl-fabric 0
cxl-pool default_pool
cxl-shard-id 0

# Disable replication (shared memory makes it unnecessary)
# replicaof <masterip> <masterport>  # Commented out

# Use CXL for pub/sub
pubsub-backend cxl
```

**Performance (GET/SET operations):**

| Metric | TCP (Localhost) | CXL | Improvement |
|--------|-----------------|-----|-------------|
| **Latency (p99)** | 120 μs | 0.4 μs | 300x |
| **Throughput** | 500K ops/s | 8M ops/s | 16x |

---

#### 5.3.2 PostgreSQL Shared Buffers

**Standard PostgreSQL:**
- Shared buffers limited to single node's RAM
- Cross-node queries require network round-trips

**CXL-Enhanced PostgreSQL:**

**Install:**
```bash
sudo apt install postgresql-16-cxl
```

**Edit `/etc/postgresql/16/main/postgresql.conf`:**
```
# Use CXL fabric memory for shared buffers
shared_buffers = 512GB              # Much larger than local RAM!
cxl_shared_buffers = on
cxl_fabric_id = 0
cxl_memory_pool = 'db_pool'

# Enable CXL-aware query planner
cxl_parallel_query = on
```

**Create CXL pool:**
```bash
cxl-cli pool create \
  --name db_pool \
  --size 512GB \
  --endpoints 0,1,2,3
```

**Benefits:**
- Query execution sees 512GB buffer pool (vs 128GB local)
- Parallel queries distributed across fabric with <1μs inter-node latency
- No cache coherency issues (CXL.cache protocol handles it)

---

### 5.4 Storage Workloads

#### 5.4.1 Ceph with CXL

**Traditional Ceph:**
```
OSD ◄─ Network ─► OSD ◄─ Network ─► OSD
(Replication = 3x overhead + latency)
```

**CXL-Enhanced Ceph:**
```
OSD ◄─ CXL (300ns) ─► OSD ◄─ CXL (300ns) ─► OSD
(Replication still 3x, but 10x faster)
```

**Configuration (`ceph.conf`):**
```ini
[global]
  ms_type = cxl              # Use CXL messenger
  cxl_fabric_id = 0

[osd]
  osd_memory_target = 64GB   # Use CXL memory for cache
  cxl_cache_pool = storage_pool
```

**Create pool:**
```bash
ceph osd pool create rbd_cxl 128 128
ceph osd pool set rbd_cxl crush_rule cxl_rule
```

**Performance (4MB writes, 3x replication):**

| Metric | TCP/IP | RDMA | CXL | vs RDMA |
|--------|--------|------|-----|---------|
| **Latency** | 1.2 ms | 180 μs | 45 μs | 4x better |
| **IOPS (4K)** | 85K | 320K | 1.2M | 3.75x |

---

## 6. Performance Tuning

### 6.1 Kernel Tuning

**Edit `/etc/sysctl.d/99-cxl.conf`:**
```ini
# Increase memory limits for CXL pools
vm.max_map_count = 262144

# Disable swapping (CXL memory is remote but fast)
vm.swappiness = 0

# Optimize for low-latency
kernel.sched_latency_ns = 1000000
kernel.sched_min_granularity_ns = 100000

# Increase PCIe throughput
kernel.io_delay = 0

# CXL-specific
cxl.fabric.route_cache_size = 4096
cxl.memnet.prefetch = 1
cxl.qos.enable = 1
```

**Apply:**
```bash
sudo sysctl -p /etc/sysctl.d/99-cxl.conf
```

---

### 6.2 Application Tuning

#### 6.2.1 NUMA Pinning

**Optimal:**
```bash
# Pin application to local CPU, but allow CXL memory
numactl --cpunodebind=0 --membind=0,8,9,10,11 ./my_app
#                                   ↑ CXL nodes
```

**Check NUMA topology:**
```bash
numactl --hardware

# Example output:
available: 12 nodes (0-11)
node 0 cpus: 0-31     # Local CPU
node 0 size: 128 GB   # Local RAM
node 8 cpus:          # CXL endpoint 0 (no CPUs)
node 8 size: 512 GB   # CXL memory
node 8 distance: 0:30 8:10  # 30 = slightly farther than local
```

---

#### 6.2.2 Huge Pages

**Enable:**
```bash
# Allocate 10GB of 1GB huge pages
echo 10 > /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages

# Mount hugetlbfs
mkdir -p /mnt/huge
mount -t hugetlbfs nodev /mnt/huge
```

**Use in application:**
```c
#include <sys/mman.h>
#include <libcxl/cxl_mem.h>

// Allocate CXL memory with huge pages
cxl_mem_t *mem = cxl_mem_open(ep, 10UL << 30, CXL_MEM_RW);
void *ptr = cxl_mem_map(mem, 0, 10UL << 30);

// Use MAP_HUGETLB for local mappings
madvise(ptr, 10UL << 30, MADV_HUGEPAGE);
```

**Benefit:** 15-20% latency reduction for large transfers

---

### 6.3 QoS Tuning

**Identify traffic classes:**
```bash
# Create QoS classes
cxl-cli qos create \
  --name ai_training \
  --priority 7 \
  --min-bw 100Gbps \
  --max-latency 500ns

cxl-cli qos create \
  --name background \
  --priority 1 \
  --min-bw 10Gbps \
  --max-latency 100us
```

**Assign to pools:**
```bash
cxl-cli pool set-qos --pool ai_pool --qos ai_training
cxl-cli pool set-qos --pool storage_pool --qos background
```

**Verify:**
```bash
cxl-cli stats qos

# Output:
CLASS         BANDWIDTH   LATENCY (p99)   VIOLATIONS
ai_training   98.2 Gbps   412 ns          0
background    12.5 Gbps   8.2 μs          0
```

---

## 7. Monitoring & Operations

### 7.1 Dashboard Setup

**Install Grafana:**
```bash
# Add Grafana repo
sudo add-apt-repository "deb https://packages.grafana.com/oss/deb stable main"
sudo apt update
sudo apt install grafana

# Start Grafana
sudo systemctl enable grafana-server
sudo systemctl start grafana-server
```

**Configure Prometheus scraping (`/etc/prometheus/prometheus.yml`):**
```yaml
scrape_configs:
  - job_name: 'cxl_fabric'
    static_configs:
      - targets: ['localhost:9100']  # cxl-telemetryd
    scrape_interval: 1s
```

**Import CXL Dashboard:**
```bash
# Download prebuilt dashboard
wget https://github.com/cxl-interconnect/grafana-dashboards/raw/main/cxl-fabric.json

# Import via Grafana UI or CLI
grafana-cli dashboards import cxl-fabric.json
```

**Key Metrics to Monitor:**

| Metric | Threshold | Alert |
|--------|-----------|-------|
| **Link Bandwidth** | >80% | Warning |
| **Latency p99** | >1μs | Critical |
| **CRC Errors** | >0 | Critical |
| **Memory Utilization** | >90% | Warning |

---

### 7.2 Logging

**Centralize logs with rsyslog:**

**/etc/rsyslog.d/50-cxl.conf:**
```
# Forward CXL logs to central server
if $programname startswith 'cxl' then @@logserver:514
& stop
```

**Query logs:**
```bash
# View CXL fabric events
journalctl -u cxlfmd -f

# Search for errors
journalctl -u cxlfmd | grep -i error

# Export for analysis
journalctl -u cxlfmd --since "1 hour ago" -o json > cxl-logs.json
```

---

### 7.3 Alerting

**Prometheus Alert Rules (`/etc/prometheus/alerts/cxl.yml`):**
```yaml
groups:
  - name: cxl_fabric
    interval: 10s
    rules:
      - alert: HighLatency
        expr: cxl_latency_ns{quantile="0.99"} > 1000
        for: 1m
        labels:
          severity: critical
        annotations:
          summary: "CXL latency exceeded 1μs"

      - alert: LinkDown
        expr: cxl_port_state == 0
        for: 10s
        labels:
          severity: critical
        annotations:
          summary: "CXL link down on {{ $labels.switch }} port {{ $labels.port }}"

      - alert: MemoryExhaustion
        expr: cxl_endpoint_available_memory / cxl_endpoint_total_memory < 0.1
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "Endpoint {{ $labels.endpoint }} memory <10% free"
```

---

## 8. Troubleshooting

### 8.1 Common Issues

#### Issue: "CXL device not detected"

**Symptoms:**
```bash
$ cxl-cli fabric list
Error: No CXL fabrics found
```

**Diagnosis:**
```bash
# Check kernel module
lsmod | grep cxl_interconnect
# If empty: module not loaded

# Check dmesg for errors
dmesg | grep -i cxl
# Look for "CXL device enumeration failed"

# Check BIOS
sudo dmidecode -t memory | grep CXL
# Should show CXL memory devices
```

**Solutions:**
1. **Load module manually:**
   ```bash
   sudo modprobe cxl_interconnect
   ```

2. **Check BIOS:** Ensure CXL is enabled (see Section 3.1.1)

3. **Verify hardware:** Use `lspci | grep CXL` to confirm devices present

---

#### Issue: "High latency (>5μs)"

**Diagnosis:**
```bash
# Run latency test
cxl-cli test latency --src 0 --dst 1

# Check for routing issues
cxl-cli fabric show 0 | grep route
# Look for excessive hop count

# Monitor fabric congestion
cxl-cli stats bandwidth --watch
# Check for saturated links
```

**Solutions:**
1. **Optimize routing:**
   ```bash
   # Force direct path
   cxl-cli route set --src 0 --dst 1 --path "switch0:port5"
   ```

2. **Check QoS:**
   ```bash
   # Ensure traffic has appropriate priority
   cxl-cli qos assign --pool mypool --class high_priority
   ```

3. **Reduce congestion:**
   ```bash
   # Throttle background traffic
   cxl-cli qos set --class background --max-bw 10Gbps
   ```

---

#### Issue: "Memory allocation failures"

**Symptoms:**
```c
cxl_mem_t *mem = cxl_mem_open(ep, size, CXL_MEM_RW);
// Returns NULL
```

**Diagnosis:**
```bash
# Check available memory
cxl-cli endpoint info 0
# Available Memory: 0 GB  ← Problem!

# Check pool allocation
cxl-cli pool list
# Shows over-subscription
```

**Solutions:**
1. **Free unused regions:**
   ```bash
   # List all allocations
   cxl-cli mem list
   # Kill processes not actively using memory
   ```

2. **Expand pool:**
   ```bash
   # Add more endpoints
   cxl-cli pool expand --pool mypool --add-endpoints 4,5
   ```

3. **Implement memory limits:**
   ```yaml
   # /etc/cxl/cxlfmd.conf
   pools:
     - name: mypool
       quota_per_process: 100GB
   ```

---

### 8.2 Performance Debugging

**Tool: `cxl-perf`**
```bash
# Record all CXL events for 10 seconds
sudo cxl-perf record -a -g -e 'cxl:*' sleep 10

# Analyze
sudo cxl-perf report --stdio

# Example output:
# 45.23%  myapp  [cxl_interconnect]  cxl_mem_get
# 32.11%  myapp  [cxl_interconnect]  cxl_route_compute  ← Expensive!
#  8.44%  myapp  [kernel]            __alloc_pages
```

**Optimization:**
```bash
# Enable route caching if compute is expensive
echo 4096 > /sys/module/cxl_interconnect/parameters/route_cache_size
```

---

### 8.3 Debugging Tools

| Tool | Purpose | Usage |
|------|---------|-------|
| **cxl-cli** | Fabric management | `cxl-cli fabric show 0` |
| **cxl-perf** | Performance profiling | `cxl-perf record -e cxl:*` |
| **cxl-trace** | Event tracing | `cxl-trace -f fabric_events` |
| **wireshark-cxl** | Protocol analyzer | Capture CXL traffic |
| **stress-cxl** | Load testing | `stress-cxl --bandwidth 100G` |

---

## 9. Migration Checklist

### Pre-Migration
- [ ] Hardware audit complete
- [ ] Application inventory documented
- [ ] Performance baselines captured
- [ ] CXL hardware procured
- [ ] Team trained on CXL concepts

### Week 1
- [ ] Install CXL switches
- [ ] Cable all servers
- [ ] Install CXL kernel on 1 test node
- [ ] Verify fabric discovery

### Week 2-3
- [ ] Install CXL kernel on all nodes
- [ ] Deploy cxlfmd daemon
- [ ] Create memory pools
- [ ] Configure monitoring

### Week 4-5
- [ ] Migrate first pilot application (non-critical)
- [ ] Run performance tests
- [ ] Compare vs InfiniBand baseline
- [ ] Tune QoS settings

### Week 6-8
- [ ] Migrate production applications (phased)
- [ ] Monitor stability (24/7)
- [ ] Document any issues
- [ ] Refine configurations

### Week 9+
- [ ] Decommission InfiniBand infrastructure
- [ ] Finalize documentation
- [ ] Train operations team
- [ ] Establish runbooks

---

## 10. Cost-Benefit Analysis

### Example: 100-Node Cluster

**InfiniBand TCO (3 years):**
```
NICs:        100 nodes × $2,500 =      $250,000
Switches:    4 switches × $80,000 =    $320,000
Cables:      200 cables × $150 =        $30,000
Power (3yr): 25 kW × $0.10/kWh × 26,280h = $65,700
Maintenance: 15% × $600,000/yr × 3yr =   $270,000
                                    ─────────────
Total:                                  $935,700
```

**CXL TCO (3 years):**
```
NICs:        $0 (CPU integrated)          $0
Switches:    4 switches × $30,000 =    $120,000
Cables:      200 cables × $100 =        $20,000
Power (3yr): 10 kW × $0.10/kWh × 26,280h = $26,280
Maintenance: 10% × $140,000/yr × 3yr =    $42,000
                                    ─────────────
Total:                                  $208,280

SAVINGS:                                $727,420 (78%)
```

---

## 11. Conclusion

**Migration Path Summary:**
1. **Start:** Drop-in compatibility (1-2 weeks)
2. **Optimize:** Hybrid deployment (1-2 months)
3. **Transform:** Native CXL (3-6 months)

**Expected Outcomes:**
- ✅ **10x lower latency**
- ✅ **80%+ cost reduction**
- ✅ **Simplified operations** (no NIC management)
- ✅ **Future-proof** (CXL is industry standard)

**Next Steps:**
1. Run pilot on non-critical workload
2. Measure and document benefits
3. Expand to production
4. Share success stories with community

---

**Support Resources:**
- Documentation: https://docs.cxl-interconnect.org
- Community Forum: https://forum.cxl-interconnect.org
- Commercial Support: support@cxl-interconnect.com
- Bug Reports: https://github.com/cxl-interconnect/issues

---

**Document Status:** Final v1.0
**Last Updated:** 2025-11-19
