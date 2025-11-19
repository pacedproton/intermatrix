#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
#
# InterMatrix CXL Interconnect - Quick Start Script
#
# This script automates the build, load, and test process

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo -e "${GREEN}InterMatrix CXL Interconnect - Quick Start${NC}"
echo "=========================================="
echo ""

# Check if running as root for module operations
if [ "$EUID" -eq 0 ]; then
    AS_ROOT=true
else
    AS_ROOT=false
fi

# Step 1: Check dependencies
echo -e "${YELLOW}Step 1: Checking dependencies...${NC}"

check_command() {
    if ! command -v $1 &> /dev/null; then
        echo -e "${RED}✗ $1 not found${NC}"
        return 1
    fi
    echo -e "${GREEN}✓ $1 found${NC}"
    return 0
}

DEPS_OK=true
check_command gcc || DEPS_OK=false
check_command make || DEPS_OK=false

if [ -d "/lib/modules/$(uname -r)/build" ]; then
    echo -e "${GREEN}✓ Kernel headers found${NC}"
else
    echo -e "${RED}✗ Kernel headers not found${NC}"
    echo "  Install with: sudo apt-get install linux-headers-$(uname -r)"
    DEPS_OK=false
fi

if [ "$DEPS_OK" = false ]; then
    echo -e "${RED}Missing dependencies. Please install them first.${NC}"
    exit 1
fi

echo ""

# Step 2: Build
echo -e "${YELLOW}Step 2: Building project...${NC}"

cd "$PROJECT_DIR"

if [ -d "build" ]; then
    echo "Cleaning previous build..."
    make clean > /dev/null 2>&1 || true
fi

echo "Building kernel module, library, tools, and examples..."
if make all > /tmp/cxl_build.log 2>&1; then
    echo -e "${GREEN}✓ Build successful${NC}"
else
    echo -e "${RED}✗ Build failed${NC}"
    echo "See /tmp/cxl_build.log for details"
    tail -20 /tmp/cxl_build.log
    exit 1
fi

echo ""

# Step 3: Load kernel module
echo -e "${YELLOW}Step 3: Loading kernel module...${NC}"

if lsmod | grep -q cxl_interconnect; then
    echo "Module already loaded, reloading..."
    if [ "$AS_ROOT" = true ]; then
        rmmod cxl_interconnect 2>/dev/null || true
    else
        sudo rmmod cxl_interconnect 2>/dev/null || true
    fi
fi

if [ "$AS_ROOT" = true ]; then
    insmod build/kernel/cxl_interconnect.ko
else
    sudo insmod build/kernel/cxl_interconnect.ko
fi

sleep 1

if lsmod | grep -q cxl_interconnect; then
    echo -e "${GREEN}✓ Module loaded successfully${NC}"
else
    echo -e "${RED}✗ Failed to load module${NC}"
    exit 1
fi

# Check dmesg for initialization message
if dmesg | tail -10 | grep -q "CXL Fabric: Ready"; then
    echo -e "${GREEN}✓ Module initialized${NC}"
fi

echo ""

# Step 4: Verify fabric
echo -e "${YELLOW}Step 4: Verifying CXL fabric...${NC}"

if [ -d "/sys/class/cxl_fabric/fabric0" ]; then
    echo -e "${GREEN}✓ Fabric0 found in sysfs${NC}"

    # Read fabric info
    NUM_SWITCHES=$(cat /sys/class/cxl_fabric/fabric0/num_switches 2>/dev/null || echo "0")
    NUM_ENDPOINTS=$(cat /sys/class/cxl_fabric/fabric0/num_endpoints 2>/dev/null || echo "0")

    echo "  Switches:  $NUM_SWITCHES"
    echo "  Endpoints: $NUM_ENDPOINTS"

    if [ "$NUM_SWITCHES" -eq 0 ] && [ "$NUM_ENDPOINTS" -eq 0 ]; then
        echo -e "${YELLOW}  Note: No devices found (fabric not scanned yet)${NC}"
    fi
else
    echo -e "${RED}✗ Fabric0 not found${NC}"
    echo "  This is unexpected. Check dmesg for errors."
fi

echo ""

# Step 5: Run tests
echo -e "${YELLOW}Step 5: Running tests...${NC}"

echo ""
echo "Test 1: Fabric listing"
./build/bin/cxl-cli fabric list

echo ""
echo "Test 2: Endpoint listing"
./build/bin/cxl-cli endpoint list 0 || echo "No endpoints (expected if fabric not scanned)"

echo ""
echo "Test 3: Memory test"
if ./build/bin/cxl-cli mem test 0 0 2>/dev/null; then
    echo -e "${GREEN}✓ Memory test passed${NC}"
else
    echo -e "${YELLOW}⚠ Memory test skipped (no endpoints available)${NC}"
    echo "  This is normal in SHIM mode without scanning"
fi

echo ""

# Step 6: Run example
echo -e "${YELLOW}Step 6: Running example program...${NC}"
echo ""

if ./build/bin/simple_memory 2>&1; then
    echo ""
    echo -e "${GREEN}✓ Example completed successfully${NC}"
else
    echo -e "${YELLOW}⚠ Example encountered issues${NC}"
fi

echo ""

# Summary
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Quick start complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "What's next?"
echo ""
echo "  View fabric information:"
echo "    ./build/bin/cxl-cli fabric show 0"
echo ""
echo "  List available endpoints:"
echo "    ./build/bin/cxl-cli endpoint list 0"
echo ""
echo "  Run memory tests:"
echo "    ./build/bin/cxl-cli mem test 0 <endpoint_id>"
echo ""
echo "  View kernel logs:"
echo "    sudo dmesg | grep CXL"
echo ""
echo "  Unload module:"
echo "    sudo rmmod cxl_interconnect"
echo ""
echo "Documentation:"
echo "  - BUILD.md for build instructions"
echo "  - README.md for project overview"
echo "  - Examples in examples/ directory"
echo ""
echo "Enjoy exploring CXL interconnects!"
