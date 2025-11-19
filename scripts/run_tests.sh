#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
#
# Test Runner - Runs all test suites

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

echo -e "${BLUE}======================================${NC}"
echo -e "${BLUE}InterMatrix CXL Interconnect Test Suite${NC}"
echo -e "${BLUE}======================================${NC}"
echo ""

# Check if tests are built
if [ ! -d "$BUILD_DIR/tests" ]; then
    echo -e "${RED}Error: Tests not built${NC}"
    echo "Run: make tests"
    exit 1
fi

TOTAL_PASSED=0
TOTAL_FAILED=0
SUITES_RUN=0

# Function to run a test
run_test() {
    local test_binary=$1
    local test_name=$2

    if [ ! -x "$test_binary" ]; then
        echo -e "${YELLOW}SKIP${NC}: $test_name (not found)"
        return 0
    fi

    echo -e "${BLUE}Running: $test_name${NC}"
    if "$test_binary"; then
        echo -e "${GREEN}✓ $test_name passed${NC}"
        echo ""
        SUITES_RUN=$((SUITES_RUN + 1))
        return 0
    else
        echo -e "${RED}✗ $test_name failed${NC}"
        echo ""
        TOTAL_FAILED=$((TOTAL_FAILED + 1))
        return 1
    fi
}

# Check if kernel module is loaded
if lsmod | grep -q cxl_interconnect; then
    echo -e "${GREEN}✓ Kernel module loaded${NC}"
    MODULE_LOADED=true
else
    echo -e "${YELLOW}⚠ Kernel module not loaded${NC}"
    echo "  Some tests will be skipped"
    echo "  Load module with: sudo insmod build/kernel/cxl_interconnect.ko"
    MODULE_LOADED=false
fi
echo ""

# Run unit tests
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Unit Tests${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

run_test "$BUILD_DIR/tests/unit/test_libcxl_basic" "Basic Library Tests"
run_test "$BUILD_DIR/tests/unit/test_libcxl_fabric" "Fabric Tests"
run_test "$BUILD_DIR/tests/unit/test_libcxl_memory" "Memory Tests"

# Run integration tests
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Integration Tests${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

run_test "$BUILD_DIR/tests/integration/test_end_to_end" "End-to-End Tests"

# Run performance benchmarks
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Performance Benchmarks${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

run_test "$BUILD_DIR/tests/performance/benchmark_memory" "Memory Benchmarks"

# Summary
echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Test Summary${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo -e "Test suites run:  ${SUITES_RUN}"

if [ $TOTAL_FAILED -eq 0 ]; then
    echo -e "Result:           ${GREEN}ALL PASSED${NC}"
    echo ""
    exit 0
else
    echo -e "Suites failed:    ${RED}${TOTAL_FAILED}${NC}"
    echo -e "Result:           ${RED}FAILED${NC}"
    echo ""
    exit 1
fi
