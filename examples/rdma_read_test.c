/* SPDX-License-Identifier: Apache-2.0 */
/*
 * RDMA Read Test
 *
 * Tests RDMA Read operations using CXL verbs interface.
 * Demonstrates remote memory reading.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "libcxl/compat/verbs.h"

#define BUFFER_SIZE 8192
#define TEST_PATTERN 0xAB

struct rdma_context {
	struct ibv_context *ctx;
	struct ibv_pd *pd;
	struct ibv_mr *local_mr;
	struct ibv_mr *remote_mr;
	struct ibv_cq *cq;
	struct ibv_qp *qp;
	char *local_buf;
	char *remote_buf;
};

static int setup_context(struct ibv_device *dev, struct rdma_context *rctx)
{
	struct ibv_qp_init_attr qp_init_attr;
	struct ibv_qp_attr qp_attr;

	printf("Setting up RDMA context...\n");

	rctx->ctx = ibv_open_device(dev);
	if (!rctx->ctx) {
		fprintf(stderr, "Failed to open device\n");
		return -1;
	}

	rctx->pd = ibv_alloc_pd(rctx->ctx);
	if (!rctx->pd) {
		fprintf(stderr, "Failed to allocate PD\n");
		return -1;
	}

	/* Allocate local buffer (for reading into) */
	rctx->local_buf = malloc(BUFFER_SIZE);
	if (!rctx->local_buf) {
		fprintf(stderr, "Failed to allocate local buffer\n");
		return -1;
	}
	memset(rctx->local_buf, 0, BUFFER_SIZE);

	/* Allocate remote buffer (to read from) */
	rctx->remote_buf = malloc(BUFFER_SIZE);
	if (!rctx->remote_buf) {
		fprintf(stderr, "Failed to allocate remote buffer\n");
		return -1;
	}
	/* Fill with test pattern */
	memset(rctx->remote_buf, TEST_PATTERN, BUFFER_SIZE);

	/* Register both buffers */
	rctx->local_mr = ibv_reg_mr(rctx->pd, rctx->local_buf, BUFFER_SIZE,
				    IBV_ACCESS_LOCAL_WRITE |
				    IBV_ACCESS_REMOTE_READ);
	if (!rctx->local_mr) {
		fprintf(stderr, "Failed to register local MR\n");
		return -1;
	}

	rctx->remote_mr = ibv_reg_mr(rctx->pd, rctx->remote_buf, BUFFER_SIZE,
				     IBV_ACCESS_LOCAL_WRITE |
				     IBV_ACCESS_REMOTE_READ |
				     IBV_ACCESS_REMOTE_WRITE);
	if (!rctx->remote_mr) {
		fprintf(stderr, "Failed to register remote MR\n");
		return -1;
	}

	/* Create CQ */
	rctx->cq = ibv_create_cq(rctx->ctx, 100, NULL, NULL, 0);
	if (!rctx->cq) {
		fprintf(stderr, "Failed to create CQ\n");
		return -1;
	}

	/* Create QP */
	memset(&qp_init_attr, 0, sizeof(qp_init_attr));
	qp_init_attr.send_cq = rctx->cq;
	qp_init_attr.recv_cq = rctx->cq;
	qp_init_attr.qp_type = IBV_QPT_RC;
	qp_init_attr.cap.max_send_wr = 10;
	qp_init_attr.cap.max_recv_wr = 10;
	qp_init_attr.cap.max_send_sge = 1;
	qp_init_attr.cap.max_recv_sge = 1;

	rctx->qp = ibv_create_qp(rctx->pd, &qp_init_attr);
	if (!rctx->qp) {
		fprintf(stderr, "Failed to create QP\n");
		return -1;
	}

	/* Transition to RTS */
	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IBV_QPS_INIT;
	qp_attr.port_num = 1;
	qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
	ibv_modify_qp(rctx->qp, &qp_attr, (1 << 0) | (1 << 11) | (1 << 10));

	qp_attr.qp_state = IBV_QPS_RTR;
	qp_attr.path_mtu = IBV_MTU_4096;
	qp_attr.dest_qp_num = rctx->qp->qp_num;
	qp_attr.rq_psn = 0;
	ibv_modify_qp(rctx->qp, &qp_attr, (1 << 0) | (1 << 5) | (1 << 9) | (1 << 7));

	qp_attr.qp_state = IBV_QPS_RTS;
	qp_attr.sq_psn = 0;
	ibv_modify_qp(rctx->qp, &qp_attr, (1 << 0) | (1 << 8));

	printf("  Local buffer:  %p (lkey: 0x%x)\n",
	       rctx->local_buf, rctx->local_mr->lkey);
	printf("  Remote buffer: %p (rkey: 0x%x)\n",
	       rctx->remote_buf, rctx->remote_mr->rkey);
	printf("  QP #%u ready\n", rctx->qp->qp_num);

	return 0;
}

static void cleanup_context(struct rdma_context *rctx)
{
	if (rctx->qp) ibv_destroy_qp(rctx->qp);
	if (rctx->cq) ibv_destroy_cq(rctx->cq);
	if (rctx->remote_mr) ibv_dereg_mr(rctx->remote_mr);
	if (rctx->local_mr) ibv_dereg_mr(rctx->local_mr);
	if (rctx->remote_buf) free(rctx->remote_buf);
	if (rctx->local_buf) free(rctx->local_buf);
	if (rctx->pd) ibv_dealloc_pd(rctx->pd);
	if (rctx->ctx) ibv_close_device(rctx->ctx);
}

static int test_rdma_read(struct rdma_context *rctx, size_t size, size_t offset)
{
	struct ibv_send_wr wr, *bad_wr;
	struct ibv_sge sge;
	struct ibv_wc wc;
	int n, i;

	/* Clear local buffer */
	memset(rctx->local_buf, 0, BUFFER_SIZE);

	/* Setup SGE for local buffer */
	memset(&sge, 0, sizeof(sge));
	sge.addr = (uint64_t)rctx->local_buf + offset;
	sge.length = size;
	sge.lkey = rctx->local_mr->lkey;

	/* Setup RDMA Read work request */
	memset(&wr, 0, sizeof(wr));
	wr.wr_id = 2;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	wr.opcode = IBV_WR_RDMA_READ;
	wr.send_flags = IBV_SEND_SIGNALED;
	wr.wr.rdma.remote_addr = (uint64_t)rctx->remote_buf + offset;
	wr.wr.rdma.rkey = rctx->remote_mr->rkey;

	/* Post RDMA Read */
	if (ibv_post_send(rctx->qp, &wr, &bad_wr) != 0) {
		fprintf(stderr, "Failed to post RDMA Read\n");
		return -1;
	}

	/* Poll for completion */
	do {
		n = ibv_poll_cq(rctx->cq, 1, &wc);
	} while (n == 0);

	if (n < 0) {
		fprintf(stderr, "Poll CQ failed\n");
		return -1;
	}

	if (wc.status != IBV_WC_SUCCESS) {
		fprintf(stderr, "WC status: %s\n", ibv_wc_status_str(wc.status));
		return -1;
	}

	if (wc.opcode != IBV_WC_RDMA_READ) {
		fprintf(stderr, "Unexpected opcode: %d\n", wc.opcode);
		return -1;
	}

	/* Verify data */
	for (i = 0; i < (int)size; i++) {
		unsigned char expected = TEST_PATTERN;
		unsigned char actual = rctx->local_buf[offset + i];
		if (actual != expected) {
			fprintf(stderr, "Data mismatch at offset %zu+%d: expected 0x%02x, got 0x%02x\n",
				offset, i, expected, actual);
			return -1;
		}
	}

	return 0;
}

int main(void)
{
	struct ibv_device **dev_list;
	struct rdma_context rctx;
	int num_devices;
	struct {
		size_t size;
		size_t offset;
	} tests[] = {
		{64, 0},
		{256, 0},
		{1024, 0},
		{4096, 0},
		{512, 512},    /* With offset */
		{1024, 1024},  /* With offset */
	};
	int num_tests = sizeof(tests) / sizeof(tests[0]);
	int i;

	printf("======================================\n");
	printf("RDMA Read Test\n");
	printf("======================================\n\n");

	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list || num_devices == 0) {
		printf("No CXL devices found\n");
		return 0;
	}

	printf("Found %d device(s)\n", num_devices);
	printf("Using: %s\n\n", ibv_get_device_name(dev_list[0]));

	memset(&rctx, 0, sizeof(rctx));
	if (setup_context(dev_list[0], &rctx) != 0) {
		fprintf(stderr, "Failed to setup context\n");
		ibv_free_device_list(dev_list);
		return 1;
	}

	printf("\nRunning RDMA Read tests...\n");
	printf("------------------------------\n");

	for (i = 0; i < num_tests; i++) {
		printf("Test %d: Read %zu bytes at offset %zu... ",
		       i + 1, tests[i].size, tests[i].offset);
		fflush(stdout);

		if (test_rdma_read(&rctx, tests[i].size, tests[i].offset) == 0) {
			printf("PASS\n");
		} else {
			printf("FAIL\n");
			cleanup_context(&rctx);
			ibv_free_device_list(dev_list);
			return 1;
		}
	}

	printf("\n======================================\n");
	printf("All RDMA Read tests PASSED!\n");
	printf("======================================\n");

	cleanup_context(&rctx);
	ibv_free_device_list(dev_list);

	return 0;
}
