# CXL Interconnect Userspace Design
## Runtime, Middleware, and Management Tools

**Version:** 1.0
**Date:** 2025-11-19

---

## 1. Overview

This document specifies the userspace components that complement the kernel subsystem to provide a complete CXL interconnect solution. These components handle resource management, fabric orchestration, monitoring, and application APIs.

**Architecture:**

```
┌────────────────────────────────────────────────────────────┐
│                  Applications                              │
│    (AI/ML, HPC, Databases, Distributed Storage)           │
└────────────────────────────────────────────────────────────┘
                         ▲
                         │ libcxl API
┌────────────────────────┴────────────────────────────────────┐
│  libcxl - CXL Memory Semantic Library                       │
│  - cxl_mem.so     - cxl_verbs.so    - cxl_socket.so        │
└────────────────────────────────────────────────────────────┘
                         ▲
                         │ /dev/cxl, sysfs
┌────────────────────────┴────────────────────────────────────┐
│  Userspace Runtime Daemons                                  │
│  - cxlfmd (fabric mgmt)  - cxl-telemetryd  - cxl-qosd      │
└────────────────────────────────────────────────────────────┘
                         ▲
                         │ ioctl, sysfs, netlink
┌────────────────────────┴────────────────────────────────────┐
│           Linux Kernel CXL Subsystem                        │
└────────────────────────────────────────────────────────────┘
```

---

## 2. Core Library: libcxl

### 2.1 Library Structure

```
libcxl/
├── src/
│   ├── core/
│   │   ├── cxl_init.c          # Library initialization
│   │   ├── cxl_device.c        # Device enumeration
│   │   ├── cxl_fabric.c        # Fabric discovery
│   │   └── cxl_error.c         # Error handling
│   ├── memory/
│   │   ├── cxl_mem.c           # Memory semantic API
│   │   ├── cxl_alloc.c         # Allocator
│   │   ├── cxl_atomic.c        # Atomic operations
│   │   └── cxl_mmap.c          # mmap() support
│   ├── compat/
│   │   ├── cxl_verbs.c         # RDMA compatibility
│   │   ├── cxl_socket.c        # Socket emulation
│   │   └── cxl_mpi.c           # MPI integration
│   ├── qos/
│   │   ├── cxl_qos.c           # QoS control
│   │   └── cxl_bandwidth.c     # Bandwidth allocation
│   └── util/
│       ├── cxl_log.c           # Logging
│       └── cxl_config.c        # Configuration parser
├── include/
│   └── libcxl/
│       ├── cxl.h               # Main header
│       ├── cxl_mem.h           # Memory API
│       ├── cxl_verbs.h         # RDMA compat
│       └── cxl_types.h         # Data types
└── tests/
    ├── unit/                   # Unit tests
    └── integration/            # Integration tests
```

---

### 2.2 Memory Semantic API (`cxl_mem.h`)

#### 2.2.1 Core API

```c
/* libcxl/include/libcxl/cxl_mem.h */

#ifndef _LIBCXL_MEM_H
#define _LIBCXL_MEM_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

/**
 * cxl_context - Library context handle
 */
typedef struct cxl_context cxl_context_t;

/**
 * cxl_fabric - Fabric handle
 */
typedef struct cxl_fabric cxl_fabric_t;

/**
 * cxl_endpoint - Remote endpoint handle
 */
typedef struct cxl_endpoint cxl_endpoint_t;

/**
 * cxl_mem - Remote memory region handle
 */
typedef struct cxl_mem cxl_mem_t;

/**
 * Memory access modes
 */
enum cxl_mem_access {
    CXL_MEM_READ       = 0x01,
    CXL_MEM_WRITE      = 0x02,
    CXL_MEM_RW         = 0x03,
    CXL_MEM_ATOMIC     = 0x04,
    CXL_MEM_COHERENT   = 0x08,  /* Enable cache coherency */
};

/**
 * cxl_init - Initialize CXL library
 *
 * Returns: Context handle or NULL on failure
 */
cxl_context_t *cxl_init(void);

/**
 * cxl_cleanup - Cleanup CXL library
 */
void cxl_cleanup(cxl_context_t *ctx);

/**
 * cxl_fabric_open - Open CXL fabric
 * @ctx: Library context
 * @fabric_id: Fabric ID (or -1 for default)
 *
 * Returns: Fabric handle or NULL on failure
 */
cxl_fabric_t *cxl_fabric_open(cxl_context_t *ctx, int fabric_id);

/**
 * cxl_fabric_close - Close fabric
 */
void cxl_fabric_close(cxl_fabric_t *fabric);

/**
 * cxl_fabric_get_endpoints - Enumerate endpoints
 * @fabric: Fabric handle
 * @endpoints: Output array of endpoints
 * @max_count: Size of endpoints array
 *
 * Returns: Number of endpoints (or negative errno)
 */
int cxl_fabric_get_endpoints(cxl_fabric_t *fabric,
                              cxl_endpoint_t **endpoints,
                              size_t max_count);

/**
 * cxl_endpoint_get_info - Get endpoint information
 */
struct cxl_endpoint_info {
    uint32_t endpoint_id;
    uint64_t total_memory;
    uint64_t available_memory;
    int numa_node;
    bool supports_atomic;
    bool supports_coherent;
    char name[64];              /* e.g., "node3" */
};

int cxl_endpoint_get_info(cxl_endpoint_t *ep,
                          struct cxl_endpoint_info *info);

/**
 * cxl_mem_open - Open remote memory region
 * @ep: Target endpoint
 * @size: Size in bytes
 * @access: Access mode flags (CXL_MEM_*)
 *
 * Returns: Memory handle or NULL on failure
 */
cxl_mem_t *cxl_mem_open(cxl_endpoint_t *ep, size_t size, int access);

/**
 * cxl_mem_close - Close memory region
 */
void cxl_mem_close(cxl_mem_t *mem);

/**
 * cxl_mem_map - Map remote memory into local address space
 * @mem: Memory handle
 * @offset: Offset into region
 * @length: Length to map
 *
 * Returns: Pointer to mapped memory or NULL on failure
 *
 * The returned pointer can be used for direct load/store operations.
 */
void *cxl_mem_map(cxl_mem_t *mem, off_t offset, size_t length);

/**
 * cxl_mem_unmap - Unmap memory
 */
int cxl_mem_unmap(cxl_mem_t *mem, void *addr, size_t length);

/**
 * cxl_mem_put - Write data to remote memory (zero-copy)
 * @mem: Memory handle
 * @src: Source buffer
 * @length: Length in bytes
 * @offset: Offset into region
 *
 * Returns: 0 on success, negative errno on failure
 */
int cxl_mem_put(cxl_mem_t *mem, const void *src, size_t length, off_t offset);

/**
 * cxl_mem_get - Read data from remote memory (zero-copy)
 */
int cxl_mem_get(cxl_mem_t *mem, void *dest, size_t length, off_t offset);

/**
 * cxl_mem_copy - Copy data between remote regions
 * @dst: Destination memory
 * @src: Source memory
 * @length: Length in bytes
 * @dst_offset: Offset into destination
 * @src_offset: Offset into source
 *
 * Optimized for CXL-to-CXL copy (may use hardware acceleration).
 */
int cxl_mem_copy(cxl_mem_t *dst, cxl_mem_t *src,
                 size_t length, off_t dst_offset, off_t src_offset);

/**
 * cxl_mem_atomic_add - Atomic add on remote memory
 * @mem: Memory handle
 * @offset: Offset (must be 8-byte aligned)
 * @value: Value to add
 *
 * Returns: 0 on success, negative errno on failure
 */
int cxl_mem_atomic_add(cxl_mem_t *mem, off_t offset, int64_t value);

/**
 * cxl_mem_atomic_sub - Atomic subtract
 */
int cxl_mem_atomic_sub(cxl_mem_t *mem, off_t offset, int64_t value);

/**
 * cxl_mem_compare_swap - Atomic compare-and-swap
 * @mem: Memory handle
 * @offset: Offset (must be 8-byte aligned)
 * @old_val: Expected current value
 * @new_val: Value to write if match
 * @actual_old: [out] Actual old value
 *
 * Returns: 0 if swap succeeded, -EAGAIN if mismatch
 */
int cxl_mem_compare_swap(cxl_mem_t *mem, off_t offset,
                         uint64_t old_val, uint64_t new_val,
                         uint64_t *actual_old);

/**
 * cxl_mem_fence - Memory fence/barrier
 * @mem: Memory handle
 *
 * Ensures all previous memory operations are complete.
 */
void cxl_mem_fence(cxl_mem_t *mem);

/**
 * cxl_mem_get_latency - Get measured latency to endpoint
 * @mem: Memory handle
 *
 * Returns: Latency in nanoseconds
 */
uint64_t cxl_mem_get_latency(cxl_mem_t *mem);

/**
 * cxl_mem_get_bandwidth - Get available bandwidth
 * @mem: Memory handle
 *
 * Returns: Bandwidth in MB/s
 */
uint64_t cxl_mem_get_bandwidth(cxl_mem_t *mem);

#endif /* _LIBCXL_MEM_H */
```

---

#### 2.2.2 Implementation: Memory Mapping

```c
/* libcxl/src/memory/cxl_mmap.c */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "libcxl/cxl_mem.h"
#include "cxl_internal.h"

struct cxl_mem {
    cxl_endpoint_t *endpoint;
    int fd;                      /* /dev/cxl/fabricN/endpointM */
    size_t size;
    int access;

    /* Kernel region handle */
    uint64_t region_handle;

    /* Statistics */
    uint64_t read_count;
    uint64_t write_count;
};

/**
 * cxl_mem_open - Allocate and open remote memory region
 */
cxl_mem_t *cxl_mem_open(cxl_endpoint_t *ep, size_t size, int access)
{
    cxl_mem_t *mem;
    struct cxl_memnet_alloc_req req;
    struct cxl_memnet_alloc_resp resp;
    int ret;

    /* Validate parameters */
    if (!ep || size == 0)
        return NULL;

    /* Allocate handle */
    mem = calloc(1, sizeof(*mem));
    if (!mem)
        return NULL;

    mem->endpoint = ep;
    mem->size = size;
    mem->access = access;

    /* Open kernel device */
    char path[256];
    snprintf(path, sizeof(path), "/dev/cxl/fabric%d/endpoint%d",
             ep->fabric->fabric_id, ep->endpoint_id);

    mem->fd = open(path, O_RDWR | O_CLOEXEC);
    if (mem->fd < 0) {
        free(mem);
        return NULL;
    }

    /* Request kernel to allocate remote memory */
    memset(&req, 0, sizeof(req));
    req.size = size;
    req.access_flags = access;

    ret = ioctl(mem->fd, CXL_MEMNET_ALLOC, &req, sizeof(req),
                &resp, sizeof(resp));
    if (ret < 0) {
        close(mem->fd);
        free(mem);
        return NULL;
    }

    mem->region_handle = resp.region_handle;

    return mem;
}

/**
 * cxl_mem_map - Map remote memory into local address space
 */
void *cxl_mem_map(cxl_mem_t *mem, off_t offset, size_t length)
{
    void *addr;
    struct cxl_memnet_mmap_req req;

    if (!mem || offset + length > mem->size)
        return NULL;

    /* Set mmap offset hint for kernel */
    req.region_handle = mem->region_handle;
    req.offset = offset;
    req.length = length;

    if (ioctl(mem->fd, CXL_MEMNET_MMAP_PREPARE, &req) < 0)
        return NULL;

    /* mmap() the kernel-managed region */
    int prot = PROT_NONE;
    if (mem->access & CXL_MEM_READ)
        prot |= PROT_READ;
    if (mem->access & CXL_MEM_WRITE)
        prot |= PROT_WRITE;

    addr = mmap(NULL, length, prot, MAP_SHARED, mem->fd, offset);
    if (addr == MAP_FAILED)
        return NULL;

    /* Enable write-combining for non-coherent memory */
    if (!(mem->access & CXL_MEM_COHERENT)) {
        madvise(addr, length, MADV_SEQUENTIAL);
    }

    return addr;
}

/**
 * cxl_mem_put - Zero-copy write to remote memory
 */
int cxl_mem_put(cxl_mem_t *mem, const void *src, size_t length, off_t offset)
{
    struct cxl_memnet_io_req req;
    int ret;

    if (!mem || !src || offset + length > mem->size)
        return -EINVAL;

    if (!(mem->access & CXL_MEM_WRITE))
        return -EACCES;

    /* Use direct kernel ioctl (faster than mmap for small transfers) */
    req.region_handle = mem->region_handle;
    req.offset = offset;
    req.length = length;
    req.buf = (uint64_t)src;

    ret = ioctl(mem->fd, CXL_MEMNET_WRITE, &req);
    if (ret == 0)
        __atomic_add_fetch(&mem->write_count, 1, __ATOMIC_RELAXED);

    return ret;
}

/**
 * cxl_mem_atomic_add - Hardware atomic operation
 */
int cxl_mem_atomic_add(cxl_mem_t *mem, off_t offset, int64_t value)
{
    struct cxl_memnet_atomic_req req;

    if (!mem || (offset & 7)) /* Must be 8-byte aligned */
        return -EINVAL;

    if (!(mem->access & CXL_MEM_ATOMIC))
        return -EACCES;

    req.region_handle = mem->region_handle;
    req.offset = offset;
    req.op = CXL_ATOMIC_ADD;
    req.operand = value;

    return ioctl(mem->fd, CXL_MEMNET_ATOMIC, &req);
}
```

---

### 2.3 RDMA Compatibility Layer (`cxl_verbs.c`)

#### 2.3.1 API Translation

```c
/* libcxl/src/compat/cxl_verbs.c */

#include <infiniband/verbs.h>
#include "libcxl/cxl_mem.h"
#include "cxl_internal.h"

/**
 * Internal mapping: RDMA structures to CXL
 */
struct cxl_rdma_context {
    struct ibv_context verbs_ctx;   /* Must be first (for cast) */
    cxl_context_t *cxl_ctx;
    cxl_fabric_t *fabric;
};

struct cxl_rdma_qp {
    cxl_mem_t *remote_mem;
    uint32_t qp_num;
    enum ibv_qp_state state;
};

/**
 * ibv_open_device - Open CXL device as RDMA device
 *
 * Intercepts standard RDMA call and redirects to CXL.
 */
struct ibv_context *ibv_open_device(struct ibv_device *device)
{
    struct cxl_rdma_context *ctx;
    cxl_context_t *cxl_ctx;
    cxl_fabric_t *fabric;

    /* Initialize CXL */
    cxl_ctx = cxl_init();
    if (!cxl_ctx)
        return NULL;

    fabric = cxl_fabric_open(cxl_ctx, 0); /* Default fabric */
    if (!fabric) {
        cxl_cleanup(cxl_ctx);
        return NULL;
    }

    /* Allocate compatibility context */
    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        cxl_fabric_close(fabric);
        cxl_cleanup(cxl_ctx);
        return NULL;
    }

    ctx->cxl_ctx = cxl_ctx;
    ctx->fabric = fabric;

    /* Fill in verbs operations (stub) */
    ctx->verbs_ctx.ops.post_send = cxl_rdma_post_send;
    ctx->verbs_ctx.ops.post_recv = cxl_rdma_post_recv;
    ctx->verbs_ctx.ops.poll_cq = cxl_rdma_poll_cq;

    return &ctx->verbs_ctx;
}

/**
 * ibv_post_send - Post RDMA send work request
 *
 * Translates RDMA send to CXL memory write.
 */
static int cxl_rdma_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr,
                              struct ibv_send_wr **bad_wr)
{
    struct cxl_rdma_qp *cxl_qp = (struct cxl_rdma_qp *)qp;
    int ret;

    switch (wr->opcode) {
    case IBV_WR_RDMA_WRITE:
        /* RDMA Write → CXL memory store */
        ret = cxl_mem_put(cxl_qp->remote_mem,
                         wr->sg_list[0].addr,
                         wr->sg_list[0].length,
                         wr->wr.rdma.remote_addr);
        break;

    case IBV_WR_RDMA_READ:
        /* RDMA Read → CXL memory load */
        ret = cxl_mem_get(cxl_qp->remote_mem,
                         (void *)wr->sg_list[0].addr,
                         wr->sg_list[0].length,
                         wr->wr.rdma.remote_addr);
        break;

    case IBV_WR_ATOMIC_CMP_AND_SWP:
        /* Atomic CAS → CXL atomic */
        uint64_t old_val;
        ret = cxl_mem_compare_swap(cxl_qp->remote_mem,
                                   wr->wr.atomic.remote_addr,
                                   wr->wr.atomic.compare_add,
                                   wr->wr.atomic.swap,
                                   &old_val);
        break;

    default:
        ret = -EOPNOTSUPP;
    }

    if (ret) {
        *bad_wr = wr;
        return ret;
    }

    /* Post completion (immediate, since CXL is synchronous) */
    cxl_rdma_post_completion(cxl_qp, wr->wr_id, IBV_WC_SUCCESS);

    return 0;
}
```

**Key Benefit:** Existing RDMA applications (e.g., distributed databases using `libibverbs`) can run on CXL without modification by linking against this library.

---

### 2.4 Socket Emulation Layer (`cxl_socket.c`)

#### 2.4.1 LD_PRELOAD Wrapper

```c
/* libcxl/src/compat/cxl_socket.c */

#define _GNU_SOURCE
#include <sys/socket.h>
#include <netinet/in.h>
#include <dlfcn.h>
#include <stdio.h>
#include "libcxl/cxl_mem.h"

/**
 * Custom address family for CXL
 */
#define AF_CXL 200  /* Unused AF number */

struct sockaddr_cxl {
    sa_family_t scxl_family;     /* AF_CXL */
    uint32_t scxl_endpoint_id;   /* Target endpoint */
    uint32_t scxl_port;          /* Virtual port (for multiplexing) */
};

/**
 * CXL socket state
 */
struct cxl_socket {
    int fd;                      /* Fake FD (allocated from pool) */
    cxl_mem_t *mem;              /* Memory region */
    uint64_t read_offset;
    uint64_t write_offset;
    bool connected;
};

static struct cxl_socket cxl_sockets[1024];
static int (*real_socket)(int, int, int);
static int (*real_send)(int, const void *, size_t, int);
static int (*real_recv)(int, void *, size_t, int);

/**
 * Initialize wrappers (called automatically via __attribute__((constructor)))
 */
__attribute__((constructor))
static void cxl_socket_init(void)
{
    real_socket = dlsym(RTLD_NEXT, "socket");
    real_send = dlsym(RTLD_NEXT, "send");
    real_recv = dlsym(RTLD_NEXT, "recv");

    /* Initialize CXL library */
    cxl_init();
}

/**
 * socket() - Intercept socket creation
 */
int socket(int domain, int type, int protocol)
{
    /* Pass through non-CXL sockets */
    if (domain != AF_CXL)
        return real_socket(domain, type, protocol);

    /* Allocate CXL socket */
    for (int i = 0; i < 1024; i++) {
        if (!cxl_sockets[i].mem) {
            cxl_sockets[i].fd = 10000 + i; /* Fake FD */
            return cxl_sockets[i].fd;
        }
    }

    errno = EMFILE;
    return -1;
}

/**
 * connect() - Connect to remote CXL endpoint
 */
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    struct cxl_socket *sock = find_cxl_socket(sockfd);
    if (!sock)
        return real_connect(sockfd, addr, addrlen); /* Not CXL */

    const struct sockaddr_cxl *cxl_addr = (const struct sockaddr_cxl *)addr;

    /* Open CXL memory region to target endpoint */
    cxl_endpoint_t *ep = cxl_find_endpoint(cxl_addr->scxl_endpoint_id);
    if (!ep) {
        errno = EHOSTUNREACH;
        return -1;
    }

    sock->mem = cxl_mem_open(ep, 1UL << 30, CXL_MEM_RW); /* 1GB buffer */
    if (!sock->mem) {
        errno = ECONNREFUSED;
        return -1;
    }

    sock->connected = true;
    sock->read_offset = 0;
    sock->write_offset = 0;

    return 0;
}

/**
 * send() - Translate to CXL memory write
 */
ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    struct cxl_socket *sock = find_cxl_socket(sockfd);
    if (!sock)
        return real_send(sockfd, buf, len, flags);

    if (!sock->connected) {
        errno = ENOTCONN;
        return -1;
    }

    /* Write to remote memory */
    int ret = cxl_mem_put(sock->mem, buf, len, sock->write_offset);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    sock->write_offset += len;
    return len;
}

/**
 * recv() - Translate to CXL memory read
 */
ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    struct cxl_socket *sock = find_cxl_socket(sockfd);
    if (!sock)
        return real_recv(sockfd, buf, len, flags);

    if (!sock->connected) {
        errno = ENOTCONN;
        return -1;
    }

    /* Read from remote memory */
    int ret = cxl_mem_get(sock->mem, buf, len, sock->read_offset);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    sock->read_offset += len;
    return len;
}
```

**Usage:**
```bash
# Run existing socket-based app on CXL without recompilation
LD_PRELOAD=/usr/lib/libcxl_socket.so ./my_distributed_app
```

---

## 3. Fabric Manager Daemon (`cxlfmd`)

### 3.1 Daemon Architecture

```c
/* userspace/cxlfmd/main.c */

/**
 * cxlfmd - CXL Fabric Manager Daemon
 *
 * Responsibilities:
 * - Discover fabric topology
 * - Orchestrate resource allocation
 * - Enforce QoS policies
 * - Handle hotplug events
 * - Expose control APIs (D-Bus, REST)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <systemd/sd-daemon.h>
#include <systemd/sd-bus.h>
#include "cxlfmd.h"

struct cxlfmd_ctx {
    /* D-Bus connection */
    sd_bus *bus;

    /* Fabric state */
    struct cxl_fabric_state *fabric;

    /* Configuration */
    struct cxlfmd_config *config;

    /* Thread pool */
    struct cxlfmd_thread_pool *workers;

    /* Event loop */
    int epoll_fd;
};

int main(int argc, char **argv)
{
    struct cxlfmd_ctx *ctx;
    int ret;

    /* Daemonize */
    if (daemon(0, 0) < 0) {
        perror("daemon");
        return 1;
    }

    /* Initialize context */
    ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return 1;

    /* Load configuration */
    ctx->config = cxlfmd_load_config("/etc/cxl/cxlfmd.conf");
    if (!ctx->config) {
        fprintf(stderr, "Failed to load config\n");
        return 1;
    }

    /* Initialize CXL library */
    if (cxlfmd_fabric_init(ctx) < 0) {
        fprintf(stderr, "Failed to initialize fabric\n");
        return 1;
    }

    /* Start D-Bus service */
    ret = sd_bus_default_system(&ctx->bus);
    if (ret < 0) {
        fprintf(stderr, "Failed to connect to system bus: %s\n", strerror(-ret));
        return 1;
    }

    ret = sd_bus_add_object_vtable(ctx->bus, NULL,
                                   "/org/cxl/FabricManager",
                                   "org.cxl.FabricManager",
                                   cxlfmd_vtable,
                                   ctx);
    if (ret < 0)
        return 1;

    ret = sd_bus_request_name(ctx->bus, "org.cxl.FabricManager", 0);
    if (ret < 0)
        return 1;

    /* Start worker threads */
    cxlfmd_start_workers(ctx);

    /* Notify systemd we're ready */
    sd_notify(0, "READY=1");

    /* Main event loop */
    cxlfmd_run_event_loop(ctx);

    /* Cleanup */
    cxlfmd_cleanup(ctx);

    return 0;
}
```

---

### 3.2 Resource Orchestration

```c
/* userspace/cxlfmd/orchestrator.c */

/**
 * cxlfmd_allocate_pool - Create memory pool from endpoints
 * @ctx: Daemon context
 * @config: Pool configuration
 *
 * Aggregates memory from multiple endpoints into a named pool.
 */
int cxlfmd_allocate_pool(struct cxlfmd_ctx *ctx,
                         struct cxl_pool_config *config)
{
    struct cxl_memory_pool *pool;
    int ret;

    pool = calloc(1, sizeof(*pool));
    if (!pool)
        return -ENOMEM;

    strncpy(pool->name, config->name, sizeof(pool->name) - 1);
    pool->total_size = config->total_size;

    /* Allocate memory from specified endpoints */
    for (int i = 0; i < config->num_endpoints; i++) {
        uint32_t ep_id = config->endpoint_ids[i];
        cxl_endpoint_t *ep = cxlfmd_find_endpoint(ctx, ep_id);
        if (!ep) {
            cxlfmd_free_pool(pool);
            return -ENODEV;
        }

        size_t alloc_size = config->total_size / config->num_endpoints;
        cxl_mem_t *mem = cxl_mem_open(ep, alloc_size, CXL_MEM_RW);
        if (!mem) {
            cxlfmd_free_pool(pool);
            return -ENOMEM;
        }

        /* Add to pool */
        ret = cxl_pool_add_region(pool, mem);
        if (ret < 0) {
            cxl_mem_close(mem);
            cxlfmd_free_pool(pool);
            return ret;
        }
    }

    /* Register pool */
    list_add(&pool->node, &ctx->fabric->pools);

    /* Expose via D-Bus */
    cxlfmd_dbus_export_pool(ctx, pool);

    return 0;
}
```

---

### 3.3 D-Bus API

#### 3.3.1 Interface Definition

```xml
<!-- /usr/share/dbus-1/interfaces/org.cxl.FabricManager.xml -->

<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node>
  <interface name="org.cxl.FabricManager">
    <!-- Methods -->
    <method name="GetFabricInfo">
      <arg name="fabric_id" type="u" direction="in"/>
      <arg name="info" type="a{sv}" direction="out"/>
    </method>

    <method name="ListEndpoints">
      <arg name="fabric_id" type="u" direction="in"/>
      <arg name="endpoints" type="au" direction="out"/>
    </method>

    <method name="CreatePool">
      <arg name="name" type="s" direction="in"/>
      <arg name="size" type="t" direction="in"/>
      <arg name="endpoints" type="au" direction="in"/>
      <arg name="pool_id" type="u" direction="out"/>
    </method>

    <method name="AllocateMemory">
      <arg name="pool_id" type="u" direction="in"/>
      <arg name="size" type="t" direction="in"/>
      <arg name="handle" type="t" direction="out"/>
    </method>

    <!-- Signals -->
    <signal name="FabricTopologyChanged">
      <arg name="fabric_id" type="u"/>
      <arg name="generation" type="t"/>
    </signal>

    <signal name="EndpointAdded">
      <arg name="fabric_id" type="u"/>
      <arg name="endpoint_id" type="u"/>
    </signal>

    <signal name="EndpointRemoved">
      <arg name="fabric_id" type="u"/>
      <arg name="endpoint_id" type="u"/>
    </signal>
  </interface>
</node>
```

#### 3.3.2 Client Usage (Python)

```python
#!/usr/bin/env python3
import dbus

# Connect to D-Bus
bus = dbus.SystemBus()
proxy = bus.get_object('org.cxl.FabricManager', '/org/cxl/FabricManager')
fabric_mgr = dbus.Interface(proxy, 'org.cxl.FabricManager')

# Get fabric info
info = fabric_mgr.GetFabricInfo(0)  # Fabric 0
print(f"Switches: {info['num_switches']}")
print(f"Endpoints: {info['num_endpoints']}")

# List endpoints
endpoints = fabric_mgr.ListEndpoints(0)
for ep_id in endpoints:
    print(f"Endpoint {ep_id}")

# Create memory pool
pool_id = fabric_mgr.CreatePool(
    "ai_training_pool",
    2 * 1024**4,  # 2TB
    [0, 1, 2, 3]  # Endpoints
)
print(f"Created pool {pool_id}")

# Allocate memory
handle = fabric_mgr.AllocateMemory(pool_id, 100 * 1024**3)  # 100GB
print(f"Allocated memory handle: 0x{handle:x}")
```

---

## 4. Telemetry System (`cxl-telemetry`)

### 4.1 Metrics Collection

```c
/* userspace/cxl-telemetry/collector.c */

/**
 * cxl_telemetry_collector - Collect fabric metrics
 */
struct cxl_metrics {
    uint64_t timestamp_ns;

    /* Fabric-level */
    uint32_t num_switches;
    uint32_t num_endpoints;
    uint32_t active_links;

    /* Per-link bandwidth (GB/s) */
    struct {
        uint32_t switch_id;
        uint16_t port_id;
        double bandwidth_gbps;
        double utilization_pct;
    } links[256];

    /* Latency distribution (ns) */
    struct {
        uint32_t src_id;
        uint32_t dst_id;
        uint64_t p50;
        uint64_t p99;
        uint64_t p999;
    } latencies[1024];

    /* Error counters */
    uint64_t crc_errors;
    uint64_t retries;
    uint64_t link_failures;
};

/**
 * Collect metrics from sysfs
 */
int cxl_telemetry_collect(struct cxl_metrics *metrics)
{
    DIR *dir;
    struct dirent *ent;

    metrics->timestamp_ns = get_monotonic_ns();

    /* Scan /sys/class/cxl_fabric/fabric0/switches/ */
    dir = opendir("/sys/class/cxl_fabric/fabric0/switches");
    if (!dir)
        return -errno;

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;

        char path[512];
        uint32_t switch_id;
        sscanf(ent->d_name, "switch%u", &switch_id);

        /* Read port stats */
        for (int port = 0; port < 32; port++) {
            snprintf(path, sizeof(path),
                    "/sys/class/cxl_fabric/fabric0/switches/switch%u/ports/%d/stats/bytes_tx",
                    switch_id, port);

            uint64_t bytes_tx = read_sysfs_u64(path);

            /* Calculate bandwidth (delta / time) */
            // ... (implementation details)
        }
    }

    closedir(dir);
    return 0;
}
```

---

### 4.2 Prometheus Exporter

```c
/* userspace/cxl-telemetry/prometheus.c */

/**
 * cxl_prometheus_export - Export metrics in Prometheus format
 */
int cxl_prometheus_export(struct cxl_metrics *metrics, FILE *out)
{
    /* Fabric-level metrics */
    fprintf(out, "# HELP cxl_fabric_switches Number of switches\n");
    fprintf(out, "# TYPE cxl_fabric_switches gauge\n");
    fprintf(out, "cxl_fabric_switches{fabric=\"0\"} %u\n",
            metrics->num_switches);

    fprintf(out, "# HELP cxl_fabric_endpoints Number of endpoints\n");
    fprintf(out, "# TYPE cxl_fabric_endpoints gauge\n");
    fprintf(out, "cxl_fabric_endpoints{fabric=\"0\"} %u\n",
            metrics->num_endpoints);

    /* Per-link bandwidth */
    fprintf(out, "# HELP cxl_link_bandwidth_gbps Link bandwidth in GB/s\n");
    fprintf(out, "# TYPE cxl_link_bandwidth_gbps gauge\n");
    for (int i = 0; i < metrics->num_active_links; i++) {
        fprintf(out, "cxl_link_bandwidth_gbps{switch=\"%u\",port=\"%u\"} %.2f\n",
                metrics->links[i].switch_id,
                metrics->links[i].port_id,
                metrics->links[i].bandwidth_gbps);
    }

    /* Latency histograms */
    fprintf(out, "# HELP cxl_latency_ns Latency in nanoseconds\n");
    fprintf(out, "# TYPE cxl_latency_ns summary\n");
    for (int i = 0; i < metrics->num_latency_samples; i++) {
        fprintf(out, "cxl_latency_ns{src=\"%u\",dst=\"%u\",quantile=\"0.5\"} %lu\n",
                metrics->latencies[i].src_id,
                metrics->latencies[i].dst_id,
                metrics->latencies[i].p50);
        fprintf(out, "cxl_latency_ns{src=\"%u\",dst=\"%u\",quantile=\"0.99\"} %lu\n",
                metrics->latencies[i].src_id,
                metrics->latencies[i].dst_id,
                metrics->latencies[i].p99);
    }

    return 0;
}

/**
 * HTTP server for Prometheus scraping
 */
void cxl_prometheus_server(void)
{
    /* Simple HTTP server on port 9100 */
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(9100),
        .sin_addr.s_addr = INADDR_ANY,
    };

    bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(listen_fd, 10);

    while (1) {
        int client_fd = accept(listen_fd, NULL, NULL);

        /* Read HTTP request (ignore contents) */
        char buf[1024];
        read(client_fd, buf, sizeof(buf));

        /* Collect fresh metrics */
        struct cxl_metrics metrics;
        cxl_telemetry_collect(&metrics);

        /* Send HTTP response */
        FILE *f = fdopen(client_fd, "w");
        fprintf(f, "HTTP/1.1 200 OK\r\n");
        fprintf(f, "Content-Type: text/plain\r\n\r\n");
        cxl_prometheus_export(&metrics, f);
        fclose(f);
    }
}
```

**Usage:**
```bash
# Start telemetry daemon
cxl-telemetryd --port 9100 &

# Configure Prometheus
cat >> /etc/prometheus/prometheus.yml <<EOF
scrape_configs:
  - job_name: 'cxl_fabric'
    static_configs:
      - targets: ['localhost:9100']
EOF

# View in Grafana
# Import dashboard from grafana-dashboards/cxl-fabric.json
```

---

## 5. Command-Line Tools

### 5.1 `cxl-cli` - Fabric Management Tool

```bash
# List fabrics
$ cxl-cli fabric list
FABRIC  SWITCHES  ENDPOINTS  STATE
0       4         8          active
1       2         4          active

# Show fabric topology
$ cxl-cli fabric show 0
Fabric 0:
  Switches:
    - switch0 (Broadcom PCIe Gen6, 32 ports)
    - switch1 (Microchip PM8556, 24 ports)
  Endpoints:
    - endpoint0 (node0, 512GB, NUMA node 0)
    - endpoint1 (node1, 512GB, NUMA node 1)
  Routes:
    endpoint0 → endpoint1: switch0:port5 → switch1:port2 (2 hops, 380ns)

# Create memory pool
$ cxl-cli pool create \
    --name ai_pool \
    --size 2TB \
    --endpoints 0,1,2,3 \
    --qos high
Created pool 'ai_pool' (ID: 5)

# Allocate memory
$ cxl-cli mem alloc --pool ai_pool --size 100GB
Allocated 100GB at handle 0x1000000000

# Monitor bandwidth
$ cxl-cli stats bandwidth --watch
SWITCH  PORT  BW (GB/s)  UTIL (%)
0       5     95.2       76%
0       7     23.4       19%
1       2     88.1       70%

# Test latency
$ cxl-cli test latency --src 0 --dst 3
Latency: min=312ns, avg=385ns, max=421ns, p99=415ns
```

---

## 6. Configuration Management

### 6.1 Configuration File Format

```yaml
# /etc/cxl/cxlfmd.conf

fabric:
  id: 0
  discovery:
    method: auto              # auto, acpi, manual
    scan_interval: 5s         # Periodic rescan

  hotplug:
    enabled: true
    failover_timeout: 100ms   # Link failover time

memory_pools:
  - name: "ai_training_pool"
    size: 2TB
    endpoints: [0, 1, 2, 3]
    qos:
      priority: high
      min_bandwidth: 100Gbps
      max_latency: 500ns

  - name: "storage_cache_pool"
    size: 512GB
    endpoints: [4, 5]
    qos:
      priority: medium
      min_bandwidth: 50Gbps

qos_classes:
  - name: "realtime"
    max_latency: 500ns
    min_bandwidth: 100Gbps
    virtual_channel: 0

  - name: "bulk"
    max_latency: 10us
    min_bandwidth: 10Gbps
    virtual_channel: 1

security:
  isolation:
    enabled: true
    method: cgroup_v2         # Isolate by cgroup

  encryption:
    enabled: false            # Data-at-rest encryption (optional)
    algorithm: aes-256-gcm

logging:
  level: info                 # debug, info, warn, error
  output: /var/log/cxlfmd.log
  syslog: true

telemetry:
  enabled: true
  prometheus:
    port: 9100
  interval: 1s
```

---

## 7. Systemd Integration

### 7.1 Service Units

```ini
# /etc/systemd/system/cxlfmd.service

[Unit]
Description=CXL Fabric Manager Daemon
After=network.target
Wants=cxl-telemetryd.service

[Service]
Type=notify
ExecStart=/usr/sbin/cxlfmd
Restart=on-failure
RestartSec=5s

# Security hardening
CapabilityBoundingSet=CAP_SYS_RAWIO CAP_SYS_ADMIN
PrivateTmp=yes
NoNewPrivileges=yes

[Install]
WantedBy=multi-user.target
```

```ini
# /etc/systemd/system/cxl-telemetryd.service

[Unit]
Description=CXL Telemetry Collector
After=cxlfmd.service

[Service]
Type=simple
ExecStart=/usr/sbin/cxl-telemetryd --port 9100
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

---

## 8. Build System

### 8.1 Meson Build

```meson
# meson.build

project('intermatrix-cxl', 'c',
  version: '1.0.0',
  license: 'LGPL-2.1',
  default_options: ['c_std=gnu11', 'warning_level=2']
)

# Dependencies
libsystemd = dependency('libsystemd')
libcap = dependency('libcap')
threads = dependency('threads')

# libcxl shared library
libcxl_sources = files(
  'libcxl/src/core/cxl_init.c',
  'libcxl/src/memory/cxl_mem.c',
  'libcxl/src/memory/cxl_mmap.c',
  'libcxl/src/compat/cxl_verbs.c',
  'libcxl/src/compat/cxl_socket.c',
)

libcxl = shared_library('cxl',
  libcxl_sources,
  version: '1.0.0',
  install: true,
  dependencies: [threads]
)

# cxlfmd daemon
cxlfmd_sources = files(
  'userspace/cxlfmd/main.c',
  'userspace/cxlfmd/orchestrator.c',
  'userspace/cxlfmd/dbus.c',
)

executable('cxlfmd',
  cxlfmd_sources,
  install: true,
  install_dir: get_option('sbindir'),
  dependencies: [libsystemd, libcap],
  link_with: libcxl
)

# cxl-cli tool
executable('cxl-cli',
  'userspace/tools/cxl-cli.c',
  install: true,
  link_with: libcxl
)

# Tests
subdir('tests')
```

**Build:**
```bash
meson setup build
meson compile -C build
sudo meson install -C build
```

---

## 9. Testing

### 9.1 Unit Tests (pytest)

```python
# tests/integration/test_memory.py

import pytest
from pycxl import Fabric, Endpoint

def test_memory_alloc():
    """Test basic memory allocation"""
    fabric = Fabric(0)
    endpoints = fabric.endpoints()

    assert len(endpoints) > 0

    ep = endpoints[0]
    mem = ep.alloc(1024 * 1024)  # 1MB

    assert mem is not None
    assert mem.size == 1024 * 1024

def test_memory_write_read():
    """Test write and read operations"""
    fabric = Fabric(0)
    ep = fabric.endpoints()[0]
    mem = ep.alloc(4096)

    # Write
    data = b"Hello CXL!"
    mem.write(0, data)

    # Read
    result = mem.read(0, len(data))
    assert result == data

def test_atomic_operations():
    """Test atomic operations"""
    fabric = Fabric(0)
    ep = fabric.endpoints()[0]
    mem = ep.alloc(8, atomic=True)

    # Initial value
    mem.write_u64(0, 100)

    # Atomic add
    mem.atomic_add(0, 50)

    # Verify
    assert mem.read_u64(0) == 150
```

---

## 10. Documentation

### 10.1 Man Pages

```
cxl-cli(1)
cxlfmd(8)
cxl-telemetryd(8)
libcxl(3)
cxl_mem_open(3)
cxl_mem_map(3)
```

### 10.2 User Guides

- **Getting Started Guide:** Quick setup for first-time users
- **Administrator's Guide:** Fabric management and tuning
- **Developer's Guide:** Using libcxl API
- **Migration Guide:** Moving from InfiniBand/RDMA to CXL

---

**Document Status:** Ready for implementation
**Estimated LOC:** ~20,000 lines of userspace code
