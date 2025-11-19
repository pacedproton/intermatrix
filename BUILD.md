# Building InterMatrix CXL Interconnect

This document provides detailed build and installation instructions for the InterMatrix CXL Interconnect software stack.

## Prerequisites

### System Requirements

- **Operating System:** Linux (tested on Ubuntu 20.04+, Debian 11+, RHEL 8+)
- **Kernel Version:** 5.10+ (for development), 6.8+ (recommended)
- **Architecture:** x86_64 (ARM64 support planned)

### Build Dependencies

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    linux-headers-$(uname -r) \
    gcc \
    make \
    git \
    pkg-config
```

**RHEL/CentOS:**
```bash
sudo yum groupinstall "Development Tools"
sudo yum install kernel-devel-$(uname -r) gcc make git
```

## Building from Source

### Quick Build

```bash
# Clone repository
git clone https://github.com/intermatrix/cxl-interconnect.git
cd intermatrix

# Build everything
make all

# Expected output:
#   - build/kernel/cxl_interconnect.ko (kernel module)
#   - build/lib/libcxl.so (userspace library)
#   - build/bin/cxl-cli (management tool)
#   - build/bin/simple_memory (example program)
```

### Build Individual Components

**Kernel Module Only:**
```bash
make kernel
```

**Userspace Library Only:**
```bash
make userspace
```

**Tools:**
```bash
make tools
```

**Examples:**
```bash
make examples
```

### Clean Build

```bash
make clean    # Clean all build artifacts
```

## Installation

### System-Wide Installation

**Install (requires root):**
```bash
sudo make install
```

This installs:
- Kernel module to `/usr/local/lib/modules/$(uname -r)/extra/`
- Library to `/usr/local/lib/`
- Headers to `/usr/local/include/libcxl/`
- Tools to `/usr/local/bin/`
- Examples to `/usr/local/share/intermatrix/examples/`

**Uninstall:**
```bash
sudo make uninstall
```

### Custom Installation Prefix

```bash
make install INSTALL_PREFIX=/opt/cxl
```

## Running the Software

### 1. Load Kernel Module

**Load module:**
```bash
sudo insmod build/kernel/cxl_interconnect.ko
```

**Verify module is loaded:**
```bash
lsmod | grep cxl_interconnect
dmesg | tail -20
```

**Expected output:**
```
CXL Fabric Interconnect v1.0.0 initializing
CXL Fabric: Ready (SHIM mode for testing)
```

### 2. Create Test Fabric

The kernel module uses hardware shims for testing. To create a simulated fabric:

**Option A: Via sysfs (automatic on module load)**

The module automatically creates fabric0 and scans it.

**Option B: Manual scan**
```bash
# Check if fabric exists
ls /sys/class/cxl_fabric/

# Should show:
#   fabric0/
```

**Verify fabric topology:**
```bash
cat /sys/class/cxl_fabric/fabric0/num_switches
cat /sys/class/cxl_fabric/fabric0/num_endpoints
```

### 3. Use CLI Tool

**List fabrics:**
```bash
./build/bin/cxl-cli fabric list
```

**Show fabric details:**
```bash
./build/bin/cxl-cli fabric show 0
```

**List endpoints:**
```bash
./build/bin/cxl-cli endpoint list 0
```

**Run memory test:**
```bash
./build/bin/cxl-cli mem test 0 0
```

### 4. Run Example Program

```bash
./build/bin/simple_memory
```

**Expected output:**
```
CXL Interconnect - Simple Memory Example
=========================================

✓ Initialized libcxl v1.0.0
✓ Opened fabric 0 (2 switches, 4 endpoints)
✓ Using endpoint 0:
    Total memory:     512 GB
    Available memory: 512 GB
    NUMA node:        8
    Atomic ops:       Yes
    Cache coherent:   Yes

✓ Allocated 4KB remote memory region
✓ Mapped memory to local address space

Writing pattern to remote memory...
Reading back and verifying...
✓ Data verification successful!

Direct memory access example:
  Wrote: 0xDEADBEEFCAFEBABE at offset 0
  Wrote: 0x123456789ABCDEF0 at offset 8
  Read:  0xDEADBEEFCAFEBABE from offset 0
  Read:  0x123456789ABCDEF0 from offset 8

Performance characteristics:
  Latency:   ~350 ns
  Bandwidth: ~64000 MB/s

✓ Cleanup complete

Example completed successfully!
```

## Troubleshooting

### Module won't load

**Error:** `insmod: ERROR: could not insert module`

**Solution:**
```bash
# Check dmesg for errors
dmesg | tail -50

# Verify kernel headers match running kernel
uname -r
ls /lib/modules/$(uname -r)/build

# If headers missing:
sudo apt-get install linux-headers-$(uname -r)
```

### Library not found

**Error:** `error while loading shared libraries: libcxl.so`

**Solution:**
```bash
# Add to library path
export LD_LIBRARY_PATH=$PWD/build/lib:$LD_LIBRARY_PATH

# Or install system-wide
sudo make install
sudo ldconfig
```

### Permission denied accessing sysfs

**Error:** `Permission denied` when accessing `/sys/class/cxl_fabric/`

**Solution:**
```bash
# Run as root or add user to appropriate group
sudo chmod +r /sys/class/cxl_fabric/fabric0/*
```

### No fabrics found

**Issue:** `cxl-cli fabric list` shows no fabrics

**Solution:**
```bash
# Check if module is loaded
lsmod | grep cxl_interconnect

# Check sysfs
ls /sys/class/cxl_fabric/

# If missing, reload module
sudo rmmod cxl_interconnect
sudo insmod build/kernel/cxl_interconnect.ko
```

## Development

### Building with Debug Symbols

```bash
make CFLAGS="-O0 -g3 -DDEBUG" all
```

### Running with GDB

```bash
# Build with debug symbols
make clean && make CFLAGS="-O0 -g3" all

# Run under GDB
gdb --args ./build/bin/simple_memory
```

### Kernel Module Debugging

```bash
# Enable verbose kernel logging
sudo dmesg -n 8

# Reload module with debug
sudo rmmod cxl_interconnect
sudo insmod build/kernel/cxl_interconnect.ko dyndbg=+p

# View logs
sudo dmesg -w
```

### Memory Leak Detection

```bash
# Build with AddressSanitizer
make clean
make CFLAGS="-fsanitize=address -g" LDFLAGS="-fsanitize=address" all

# Run examples
./build/bin/simple_memory
```

## Testing

### Run Test Suite

```bash
make test
```

### Manual Testing

**Test 1: Fabric enumeration**
```bash
./build/bin/cxl-cli fabric list
./build/bin/cxl-cli endpoint list 0
```

**Test 2: Memory operations**
```bash
./build/bin/cxl-cli mem test 0 0
```

**Test 3: Example programs**
```bash
./build/bin/simple_memory
```

## Performance Benchmarking

The SHIM mode provides simulated performance metrics:
- **Latency:** ~350ns (simulated)
- **Bandwidth:** ~64 GB/s (simulated)

Real hardware would provide actual measurements based on:
- PCIe Gen5/6 speed
- CXL switch latency
- Optical cable propagation delay
- CPU memory controller performance

## Cross-Compilation

**For ARM64:**
```bash
export CROSS_COMPILE=aarch64-linux-gnu-
export ARCH=arm64
make all
```

## Next Steps

After successful build and installation:

1. **Explore the API:** See `userspace/libcxl/include/libcxl/cxl.h`
2. **Read Examples:** Check `examples/` directory
3. **Develop Applications:** Use libcxl in your programs
4. **Contribute:** See `CONTRIBUTING.md` (coming soon)

## Support

- **Documentation:** See `docs/` directory
- **Issues:** https://github.com/intermatrix/cxl-interconnect/issues
- **Discussions:** https://github.com/intermatrix/cxl-interconnect/discussions
- **Mailing List:** cxl-interconnect@lists.linux.dev (proposed)

## License

- Kernel code: GPL-2.0
- Userspace library: LGPL-2.1
- Tools and examples: Apache-2.0
- Documentation: CC BY-SA 4.0
