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
EXAMPLES := $(BUILD_DIR)/bin/simple_memory

.PHONY: all kernel userspace tools examples clean install help

all: kernel userspace tools examples

help:
	@echo "InterMatrix - CXL Interconnect Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build everything (kernel + userspace)"
	@echo "  kernel       - Build kernel modules"
	@echo "  userspace    - Build libcxl library"
	@echo "  tools        - Build cxl-cli tool"
	@echo "  examples     - Build example programs"
	@echo "  clean        - Clean all build artifacts"
	@echo "  install      - Install to system (requires root)"
	@echo "  test         - Run test suite"
	@echo ""
	@echo "Installation:"
	@echo "  sudo make install"
	@echo ""
	@echo "Loading kernel module:"
	@echo "  sudo insmod build/kernel/cxl_interconnect.ko"
	@echo "  sudo mknod /dev/cxl_fabric0 c 240 0"
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
