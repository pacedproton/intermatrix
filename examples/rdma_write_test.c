/* SPDX-License-Identifier: Apache-2.0 */
/*
 * RDMA Write Test - Client/Server Pattern
 *
 * Tests RDMA Write operations using CXL verbs interface.
 * Demonstrates one-sided RDMA operations.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "libcxl/compat/verbs.h"

#define BUFFER_SIZE 4096
#define NUM_ITERATIONS 10

struct rdma_resources {
	struct ibv_context *ctx;
	struct ibv_pd *pd;
	struct ibv_mr *mr;
	struct ibv_cq *cq;
	struct ibv_qp *qp;
	char *buffer;
};

static int setup_resources(struct ibv_device *dev, struct rdma_resources *res)
{
	struct ibv_qp_init_attr qp_init_attr;
	struct ibv_qp_attr qp_attr;

	printf("Setting up RDMA resources...\n");

	/* Open device */
	res->ctx = ibv_open_device(dev);
	if (!res->ctx) {
		fprintf(stderr, "Failed to open device\n");
		return -1;
	}

	/* Allocate Protection Domain */
	res->pd = ibv_alloc_pd(res->ctx);
	if (!res->pd) {
		fprintf(stderr, "Failed to allocate PD\n");
		return -1;
	}

	/* Allocate and register memory */
	res->buffer = malloc(BUFFER_SIZE);
	if (!res->buffer) {
		fprintf(stderr, "Failed to allocate buffer\n");
		return -1;
	}
	memset(res->buffer, 0, BUFFER_SIZE);

	res->mr = ibv_reg_mr(res->pd, res->buffer, BUFFER_SIZE,
			     IBV_ACCESS_LOCAL_WRITE |
			     IBV_ACCESS_REMOTE_WRITE |
			     IBV_ACCESS_REMOTE_READ);
	if (!res->mr) {
		fprintf(stderr, "Failed to register MR\n");
		return -1;
	}

	/* Create Completion Queue */
	res->cq = ibv_create_cq(res->ctx, 100, NULL, NULL, 0);
	if (!res->cq) {
		fprintf(stderr, "Failed to create CQ\n");
		return -1;
	}

	/* Create Queue Pair */
	memset(&qp_init_attr, 0, sizeof(qp_init_attr));
	qp_init_attr.send_cq = res->cq;
	qp_init_attr.recv_cq = res->cq;
	qp_init_attr.qp_type = IBV_QPT_RC;
	qp_init_attr.cap.max_send_wr = 10;
	qp_init_attr.cap.max_recv_wr = 10;
	qp_init_attr.cap.max_send_sge = 1;
	qp_init_attr.cap.max_recv_sge = 1;

	res->qp = ibv_create_qp(res->pd, &qp_init_attr);
	if (!res->qp) {
		fprintf(stderr, "Failed to create QP\n");
		return -1;
	}

	/* Transition QP to RTS */
	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IBV_QPS_INIT;
	qp_attr.port_num = 1;
	qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
	ibv_modify_qp(res->qp, &qp_attr, (1 << 0) | (1 << 11) | (1 << 10));

	qp_attr.qp_state = IBV_QPS_RTR;
	qp_attr.path_mtu = IBV_MTU_4096;
	qp_attr.dest_qp_num = res->qp->qp_num;
	qp_attr.rq_psn = 0;
	ibv_modify_qp(res->qp, &qp_attr, (1 << 0) | (1 << 5) | (1 << 9) | (1 << 7));

	qp_attr.qp_state = IBV_QPS_RTS;
	qp_attr.sq_psn = 0;
	ibv_modify_qp(res->qp, &qp_attr, (1 << 0) | (1 << 8));

	printf("  QP #%u in RTS state\n", res->qp->qp_num);
	printf("  Buffer: %p, lkey: 0x%x, rkey: 0x%x\n",
	       res->buffer, res->mr->lkey, res->mr->rkey);

	return 0;
}

static void cleanup_resources(struct rdma_resources *res)
{
	if (res->qp) ibv_destroy_qp(res->qp);
	if (res->cq) ibv_destroy_cq(res->cq);
	if (res->mr) ibv_dereg_mr(res->mr);
	if (res->buffer) free(res->buffer);
	if (res->pd) ibv_dealloc_pd(res->pd);
	if (res->ctx) ibv_close_device(res->ctx);
}

static int test_rdma_write(struct rdma_resources *res, size_t size)
{
	struct ibv_send_wr wr, *bad_wr;
	struct ibv_sge sge;
	struct ibv_wc wc;
	int n, i;

	/* Prepare data to write */
	for (i = 0; i < (int)size; i++) {
		res->buffer[i] = (i & 0xFF);
	}

	/* Setup scatter-gather entry */
	memset(&sge, 0, sizeof(sge));
	sge.addr = (uint64_t)res->buffer;
	sge.length = size;
	sge.lkey = res->mr->lkey;

	/* Setup work request */
	memset(&wr, 0, sizeof(wr));
	wr.wr_id = 1;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	wr.opcode = IBV_WR_RDMA_WRITE;
	wr.send_flags = IBV_SEND_SIGNALED;
	wr.wr.rdma.remote_addr = (uint64_t)res->buffer + 1024; /* Offset */
	wr.wr.rdma.rkey = res->mr->rkey;

	/* Post send */
	if (ibv_post_send(res->qp, &wr, &bad_wr) != 0) {
		fprintf(stderr, "Failed to post send\n");
		return -1;
	}

	/* Poll for completion */
	do {
		n = ibv_poll_cq(res->cq, 1, &wc);
	} while (n == 0);

	if (n < 0) {
		fprintf(stderr, "Poll CQ failed\n");
		return -1;
	}

	if (wc.status != IBV_WC_SUCCESS) {
		fprintf(stderr, "WC status: %s\n", ibv_wc_status_str(wc.status));
		return -1;
	}

	return 0;
}

int main(void)
{
	struct ibv_device **dev_list;
	struct rdma_resources res;
	int num_devices, i;
	size_t test_sizes[] = {64, 256, 1024, 4096};
	int num_tests = sizeof(test_sizes) / sizeof(test_sizes[0]);

	printf("======================================\n");
	printf("RDMA Write Test\n");
	printf("======================================\n\n");

	/* Get device list */
	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list || num_devices == 0) {
		printf("No CXL devices found\n");
		printf("(Load kernel module: sudo insmod build/kernel/cxl_interconnect.ko)\n");
		return 0;
	}

	printf("Found %d device(s)\n", num_devices);
	printf("Using: %s\n\n", ibv_get_device_name(dev_list[0]));

	/* Setup resources */
	memset(&res, 0, sizeof(res));
	if (setup_resources(dev_list[0], &res) != 0) {
		fprintf(stderr, "Failed to setup resources\n");
		ibv_free_device_list(dev_list);
		return 1;
	}

	printf("\nRunning RDMA Write tests...\n");
	printf("------------------------------\n");

	/* Run tests with different sizes */
	for (i = 0; i < num_tests; i++) {
		size_t size = test_sizes[i];
		printf("Test %d: RDMA Write %zu bytes... ", i + 1, size);
		fflush(stdout);

		if (test_rdma_write(&res, size) == 0) {
			printf("PASS\n");
		} else {
			printf("FAIL\n");
			cleanup_resources(&res);
			ibv_free_device_list(dev_list);
			return 1;
		}
	}

	/* Run multiple iterations */
	printf("\nPerformance test: %d iterations of 4KB writes\n", NUM_ITERATIONS);
	for (i = 0; i < NUM_ITERATIONS; i++) {
		if (test_rdma_write(&res, 4096) != 0) {
			printf("Iteration %d FAILED\n", i + 1);
			cleanup_resources(&res);
			ibv_free_device_list(dev_list);
			return 1;
		}
		if ((i + 1) % 10 == 0) {
			printf("  Completed %d iterations\n", i + 1);
		}
	}
	printf("  All %d iterations PASSED\n", NUM_ITERATIONS);

	printf("\n======================================\n");
	printf("All RDMA Write tests PASSED!\n");
	printf("======================================\n");

	cleanup_resources(&res);
	ibv_free_device_list(dev_list);

	return 0;
}
