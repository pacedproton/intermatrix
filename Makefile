# SPDX-License-Identifier: GPL-2.0
# InterMatrix - CXL Interconnect Build System

# Project version
VERSION_MAJOR := 1
VERSION_MINOR := 0
VERSION_PATCH := 0
VERSION := $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH)

# Directories
KERNEL_DIR := kernel/drivers/cxl/interconnect
LIBCXL_DIR := userspace/libcxl
TOOLS_DIR := userspace/tools
EXAMPLES_DIR := examples

# Build directories
BUILD_DIR := build
INSTALL_PREFIX ?= /usr/local

# Kernel build
KDIR ?= /lib/modules/$(shell uname -r)/build

# Compiler flags
CFLAGS := -Wall -Wextra -O2 -g
CFLAGS += -I$(LIBCXL_DIR)/include
CFLAGS += -fPIC

LDFLAGS := -L$(BUILD_DIR)/lib

# Library
LIBCXL_NAME := libcxl
LIBCXL_SO := $(BUILD_DIR)/lib/$(LIBCXL_NAME).so.$(VERSION)
LIBCXL_SONAME := $(LIBCXL_NAME).so.$(VERSION_MAJOR)

LIBCXL_SRCS := $(shell find $(LIBCXL_DIR)/src -name '*.c')
LIBCXL_OBJS := $(patsubst $(LIBCXL_DIR)/src/%.c,$(BUILD_DIR)/libcxl/%.o,$(LIBCXL_SRCS))

# Tools
CXL_CLI := $(BUILD_DIR)/bin/cxl-cli

# Examples
EXAMPLES := $(BUILD_DIR)/bin/simple_memory \
            $(BUILD_DIR)/bin/verbs_example \
            $(BUILD_DIR)/bin/rdma_write_test \
            $(BUILD_DIR)/bin/rdma_read_test \
            $(BUILD_DIR)/bin/send_recv_test \
            $(BUILD_DIR)/bin/atomic_test \
            $(BUILD_DIR)/bin/rdma_bandwidth

# Tests
TEST_UNIT := $(BUILD_DIR)/tests/unit/test_libcxl_basic \
             $(BUILD_DIR)/tests/unit/test_libcxl_fabric \
             $(BUILD_DIR)/tests/unit/test_libcxl_memory

TEST_INTEGRATION := $(BUILD_DIR)/tests/integration/test_end_to_end

TEST_PERFORMANCE := $(BUILD_DIR)/tests/performance/benchmark_memory

TEST_COMPAT := $(BUILD_DIR)/tests/compat/test_verbs

ALL_TESTS := $(TEST_UNIT) $(TEST_INTEGRATION) $(TEST_PERFORMANCE) $(TEST_COMPAT)

.PHONY: all kernel userspace tools examples tests clean install help run-tests

all: kernel userspace tools examples tests

help:
	@echo "InterMatrix - CXL Interconnect Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build everything (kernel + userspace + tests)"
	@echo "  kernel       - Build kernel modules"
	@echo "  userspace    - Build libcxl library"
	@echo "  tools        - Build cxl-cli tool"
	@echo "  examples     - Build example programs"
	@echo "  tests        - Build test suites"
	@echo "  run-tests    - Build and run all tests"
	@echo "  clean        - Clean all build artifacts"
	@echo "  install      - Install to system (requires root)"
	@echo ""
	@echo "Testing:"
	@echo "  make run-tests"
	@echo ""
	@echo "Installation:"
	@echo "  sudo make install"
	@echo ""
	@echo "Loading kernel module:"
	@echo "  sudo insmod build/kernel/cxl_interconnect.ko"
	@echo ""
	@echo "Running example:"
	@echo "  ./build/bin/simple_memory"

# Kernel modules
kernel:
	@echo "Building kernel modules..."
	@mkdir -p $(BUILD_DIR)/kernel
	$(MAKE) -C $(KERNEL_DIR) KDIR=$(KDIR)
	@cp $(KERNEL_DIR)/cxl_interconnect.ko $(BUILD_DIR)/kernel/

kernel-clean:
	$(MAKE) -C $(KERNEL_DIR) clean

# Userspace library
userspace: $(LIBCXL_SO)

$(BUILD_DIR)/libcxl/%.o: $(LIBCXL_DIR)/src/%.c
	@echo "  CC    $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(LIBCXL_SO): $(LIBCXL_OBJS)
	@echo "  LD    $@"
	@mkdir -p $(BUILD_DIR)/lib
	@$(CC) -shared -Wl,-soname,$(LIBCXL_SONAME) -o $@ $^
	@ln -sf $(notdir $(LIBCXL_SO)) $(BUILD_DIR)/lib/$(LIBCXL_SONAME)
	@ln -sf $(LIBCXL_SONAME) $(BUILD_DIR)/lib/$(LIBCXL_NAME).so

# Tools
tools: $(CXL_CLI)

$(CXL_CLI): $(TOOLS_DIR)/cxl-cli.c $(LIBCXL_SO)
	@echo "  CC    $<"
	@mkdir -p $(BUILD_DIR)/bin
	@$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lcxl -Wl,-rpath,$(abspath $(BUILD_DIR)/lib)

# Examples
examples: $(EXAMPLES)

$(BUILD_DIR)/bin/simple_memory: $(EXAMPLES_DIR)/simple_memory.c $(LIBCXL_SO)
	@echo "  CC    $<"
	@mkdir -p $(BUILD_DIR)/bin
	@$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lcxl -Wl,-rpath,$(abspath $(BUILD_DIR)/lib)

$(BUILD_DIR)/bin/verbs_example: $(EXAMPLES_DIR)/verbs_example.c $(LIBCXL_SO)
	@echo "  CC    $<"
	@mkdir -p $(BUILD_DIR)/bin
	@$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lcxl -Wl,-rpath,$(abspath $(BUILD_DIR)/lib)

$(BUILD_DIR)/bin/rdma_write_test: $(EXAMPLES_DIR)/rdma_write_test.c $(LIBCXL_SO)
	@echo "  CC    $<"
	@mkdir -p $(BUILD_DIR)/bin
	@$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lcxl -Wl,-rpath,$(abspath $(BUILD_DIR)/lib)

$(BUILD_DIR)/bin/rdma_read_test: $(EXAMPLES_DIR)/rdma_read_test.c $(LIBCXL_SO)
	@echo "  CC    $<"
	@mkdir -p $(BUILD_DIR)/bin
	@$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lcxl -Wl,-rpath,$(abspath $(BUILD_DIR)/lib)

$(BUILD_DIR)/bin/send_recv_test: $(EXAMPLES_DIR)/send_recv_test.c $(LIBCXL_SO)
	@echo "  CC    $<"
	@mkdir -p $(BUILD_DIR)/bin
	@$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lcxl -Wl,-rpath,$(abspath $(BUILD_DIR)/lib)

$(BUILD_DIR)/bin/atomic_test: $(EXAMPLES_DIR)/atomic_test.c $(LIBCXL_SO)
	@echo "  CC    $<"
	@mkdir -p $(BUILD_DIR)/bin
	@$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lcxl -Wl,-rpath,$(abspath $(BUILD_DIR)/lib)

$(BUILD_DIR)/bin/rdma_bandwidth: $(EXAMPLES_DIR)/rdma_bandwidth.c $(LIBCXL_SO)
	@echo "  CC    $<"
	@mkdir -p $(BUILD_DIR)/bin
	@$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lcxl -Wl,-rpath,$(abspath $(BUILD_DIR)/lib)

# Tests
tests: $(ALL_TESTS)

# Unit tests
$(BUILD_DIR)/tests/unit/%: userspace/tests/unit/%.c $(LIBCXL_SO)
	@echo "  CC    $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lcxl -pthread -Wl,-rpath,$(abspath $(BUILD_DIR)/lib)

# Integration tests
$(BUILD_DIR)/tests/integration/%: userspace/tests/integration/%.c $(LIBCXL_SO)
	@echo "  CC    $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lcxl -pthread -Wl,-rpath,$(abspath $(BUILD_DIR)/lib)

# Performance tests
$(BUILD_DIR)/tests/performance/%: userspace/tests/performance/%.c $(LIBCXL_SO)
	@echo "  CC    $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lcxl -pthread -Wl,-rpath,$(abspath $(BUILD_DIR)/lib)

# Compatibility tests
$(BUILD_DIR)/tests/compat/%: userspace/tests/compat/%.c $(LIBCXL_SO)
	@echo "  CC    $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lcxl -pthread -Wl,-rpath,$(abspath $(BUILD_DIR)/lib)

# Run all tests
run-tests: tests
	@echo "======================================"
	@echo "Running InterMatrix Test Suite"
	@echo "======================================"
	@echo ""
	@echo "Unit Tests:"
	@for test in $(TEST_UNIT); do \
		echo "  Running $$test..."; \
		$$test || exit 1; \
	done
	@echo ""
	@echo "Integration Tests:"
	@for test in $(TEST_INTEGRATION); do \
		echo "  Running $$test..."; \
		$$test || exit 1; \
	done
	@echo ""
	@echo "Performance Tests:"
	@for test in $(TEST_PERFORMANCE); do \
		echo "  Running $$test..."; \
		$$test || exit 1; \
	done
	@echo ""
	@echo "Compatibility Tests:"
	@for test in $(TEST_COMPAT); do \
		echo "  Running $$test..."; \
		$$test || exit 1; \
	done
	@echo ""
	@echo "======================================"
	@echo "All tests passed!"
	@echo "======================================"

# Clean
clean: kernel-clean
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)

# Install
install: all
	@echo "Installing to $(INSTALL_PREFIX)..."

	# Install kernel module
	@install -D -m 644 $(BUILD_DIR)/kernel/cxl_interconnect.ko \
		$(INSTALL_PREFIX)/lib/modules/$(shell uname -r)/extra/cxl_interconnect.ko
	@depmod -a

	# Install library
	@install -D -m 755 $(LIBCXL_SO) $(INSTALL_PREFIX)/lib/$(notdir $(LIBCXL_SO))
	@ln -sf $(notdir $(LIBCXL_SO)) $(INSTALL_PREFIX)/lib/$(LIBCXL_SONAME)
	@ln -sf $(LIBCXL_SONAME) $(INSTALL_PREFIX)/lib/$(LIBCXL_NAME).so
	@ldconfig

	# Install headers
	@install -D -m 644 $(LIBCXL_DIR)/include/libcxl/cxl.h \
		$(INSTALL_PREFIX)/include/libcxl/cxl.h

	# Install tools
	@install -D -m 755 $(CXL_CLI) $(INSTALL_PREFIX)/bin/cxl-cli

	# Install examples
	@install -D -m 755 $(BUILD_DIR)/bin/simple_memory \
		$(INSTALL_PREFIX)/share/intermatrix/examples/simple_memory

	@echo "Installation complete!"
	@echo ""
	@echo "Next steps:"
	@echo "  1. Load kernel module:  sudo modprobe cxl_interconnect"
	@echo "  2. Create test fabric:  echo 1 > /sys/module/cxl_interconnect/parameters/create_fabric"
	@echo "  3. Run example:         $(INSTALL_PREFIX)/bin/cxl-cli fabric list"

# Uninstall
uninstall:
	@echo "Uninstalling from $(INSTALL_PREFIX)..."
	@rm -f $(INSTALL_PREFIX)/lib/modules/$(shell uname -r)/extra/cxl_interconnect.ko
	@rm -f $(INSTALL_PREFIX)/lib/$(LIBCXL_NAME).so*
	@rm -f $(INSTALL_PREFIX)/include/libcxl/cxl.h
	@rm -f $(INSTALL_PREFIX)/bin/cxl-cli
	@rm -rf $(INSTALL_PREFIX)/share/intermatrix
	@depmod -a
	@ldconfig
	@echo "Uninstall complete!"

# Testing
test: all
	@echo "Running tests..."
	@echo "Note: This requires kernel module to be loaded"
	@if lsmod | grep -q cxl_interconnect; then \
		echo "✓ Kernel module loaded"; \
	else \
		echo "✗ Kernel module not loaded"; \
		echo "  Run: sudo insmod build/kernel/cxl_interconnect.ko"; \
		exit 1; \
	fi
	@echo "Running fabric list test..."
	@$(CXL_CLI) fabric list || echo "No fabrics found (expected if no scan done)"
	@echo "All tests passed!"

.PHONY: kernel-clean
