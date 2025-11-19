/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Atomic Operations Test
 *
 * Tests RDMA atomic operations (Fetch-and-Add, Compare-and-Swap)
 * using CXL verbs interface.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "libcxl/compat/verbs.h"

struct atomic_context {
	struct ibv_context *ctx;
	struct ibv_pd *pd;
	struct ibv_mr *mr;
	struct ibv_cq *cq;
	struct ibv_qp *qp;
	uint64_t *counter;
};

static int setup_atomic_context(struct ibv_device *dev, struct atomic_context *actx)
{
	struct ibv_qp_init_attr qp_init_attr;
	struct ibv_qp_attr qp_attr;

	printf("Setting up atomic operations context...\n");

	actx->ctx = ibv_open_device(dev);
	if (!actx->ctx) {
		fprintf(stderr, "Failed to open device\n");
		return -1;
	}

	actx->pd = ibv_alloc_pd(actx->ctx);
	if (!actx->pd) {
		fprintf(stderr, "Failed to allocate PD\n");
		return -1;
	}

	/* Allocate counter (aligned for atomic ops) */
	if (posix_memalign((void **)&actx->counter, 8, sizeof(uint64_t)) != 0) {
		fprintf(stderr, "Failed to allocate counter\n");
		return -1;
	}
	*actx->counter = 0;

	/* Register memory with atomic access */
	actx->mr = ibv_reg_mr(actx->pd, actx->counter, sizeof(uint64_t),
			      IBV_ACCESS_LOCAL_WRITE |
			      IBV_ACCESS_REMOTE_READ |
			      IBV_ACCESS_REMOTE_WRITE |
			      IBV_ACCESS_REMOTE_ATOMIC);
	if (!actx->mr) {
		fprintf(stderr, "Failed to register MR\n");
		return -1;
	}

	/* Create CQ */
	actx->cq = ibv_create_cq(actx->ctx, 100, NULL, NULL, 0);
	if (!actx->cq) {
		fprintf(stderr, "Failed to create CQ\n");
		return -1;
	}

	/* Create QP */
	memset(&qp_init_attr, 0, sizeof(qp_init_attr));
	qp_init_attr.send_cq = actx->cq;
	qp_init_attr.recv_cq = actx->cq;
	qp_init_attr.qp_type = IBV_QPT_RC;
	qp_init_attr.cap.max_send_wr = 10;
	qp_init_attr.cap.max_recv_wr = 10;
	qp_init_attr.cap.max_send_sge = 1;
	qp_init_attr.cap.max_recv_sge = 1;

	actx->qp = ibv_create_qp(actx->pd, &qp_init_attr);
	if (!actx->qp) {
		fprintf(stderr, "Failed to create QP\n");
		return -1;
	}

	/* Transition to RTS */
	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IBV_QPS_INIT;
	qp_attr.port_num = 1;
	qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE |
				  IBV_ACCESS_REMOTE_READ |
				  IBV_ACCESS_REMOTE_ATOMIC;
	ibv_modify_qp(actx->qp, &qp_attr, (1 << 0) | (1 << 11) | (1 << 10));

	qp_attr.qp_state = IBV_QPS_RTR;
	qp_attr.path_mtu = IBV_MTU_4096;
	qp_attr.dest_qp_num = actx->qp->qp_num;
	qp_attr.rq_psn = 0;
	ibv_modify_qp(actx->qp, &qp_attr, (1 << 0) | (1 << 5) | (1 << 9) | (1 << 7));

	qp_attr.qp_state = IBV_QPS_RTS;
	qp_attr.sq_psn = 0;
	ibv_modify_qp(actx->qp, &qp_attr, (1 << 0) | (1 << 8));

	printf("  Counter: %p (rkey: 0x%x)\n", actx->counter, actx->mr->rkey);
	printf("  QP #%u ready\n", actx->qp->qp_num);

	return 0;
}

static void cleanup_atomic_context(struct atomic_context *actx)
{
	if (actx->qp) ibv_destroy_qp(actx->qp);
	if (actx->cq) ibv_destroy_cq(actx->cq);
	if (actx->mr) ibv_dereg_mr(actx->mr);
	if (actx->counter) free(actx->counter);
	if (actx->pd) ibv_dealloc_pd(actx->pd);
	if (actx->ctx) ibv_close_device(actx->ctx);
}

static int test_fetch_and_add(struct atomic_context *actx, uint64_t add_value)
{
	struct ibv_send_wr wr, *bad_wr;
	struct ibv_sge sge;
	struct ibv_wc wc;
	uint64_t result;
	int n;

	/* Local buffer for result */
	if (posix_memalign((void **)&result, 8, sizeof(uint64_t)) != 0) {
		return -1;
	}

	/* Setup SGE (not used for atomic but required) */
	memset(&sge, 0, sizeof(sge));
	sge.addr = (uint64_t)&result;
	sge.length = sizeof(uint64_t);
	sge.lkey = actx->mr->lkey;

	/* Setup atomic fetch-and-add work request */
	memset(&wr, 0, sizeof(wr));
	wr.wr_id = 1;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	wr.opcode = IBV_WR_ATOMIC_FETCH_AND_ADD;
	wr.send_flags = IBV_SEND_SIGNALED;
	wr.wr.atomic.remote_addr = (uint64_t)actx->counter;
	wr.wr.atomic.rkey = actx->mr->rkey;
	wr.wr.atomic.compare_add = add_value;

	/* Post atomic operation */
	if (ibv_post_send(actx->qp, &wr, &bad_wr) != 0) {
		fprintf(stderr, "Failed to post atomic op\n");
		free((void *)result);
		return -1;
	}

	/* Poll for completion */
	do {
		n = ibv_poll_cq(actx->cq, 1, &wc);
	} while (n == 0);

	if (n < 0 || wc.status != IBV_WC_SUCCESS) {
		fprintf(stderr, "Atomic op failed\n");
		free((void *)result);
		return -1;
	}

	if (wc.opcode != IBV_WC_FETCH_ADD) {
		fprintf(stderr, "Unexpected opcode: %d\n", wc.opcode);
		free((void *)result);
		return -1;
	}

	free((void *)result);
	return 0;
}

static int test_compare_and_swap(struct atomic_context *actx,
				  uint64_t compare, uint64_t swap)
{
	struct ibv_send_wr wr, *bad_wr;
	struct ibv_sge sge;
	struct ibv_wc wc;
	uint64_t result;
	int n;

	if (posix_memalign((void **)&result, 8, sizeof(uint64_t)) != 0) {
		return -1;
	}

	/* Setup SGE */
	memset(&sge, 0, sizeof(sge));
	sge.addr = (uint64_t)&result;
	sge.length = sizeof(uint64_t);
	sge.lkey = actx->mr->lkey;

	/* Setup compare-and-swap work request */
	memset(&wr, 0, sizeof(wr));
	wr.wr_id = 2;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	wr.opcode = IBV_WR_ATOMIC_CMP_AND_SWP;
	wr.send_flags = IBV_SEND_SIGNALED;
	wr.wr.atomic.remote_addr = (uint64_t)actx->counter;
	wr.wr.atomic.rkey = actx->mr->rkey;
	wr.wr.atomic.compare_add = compare;
	wr.wr.atomic.swap = swap;

	/* Post atomic operation */
	if (ibv_post_send(actx->qp, &wr, &bad_wr) != 0) {
		fprintf(stderr, "Failed to post CAS\n");
		free((void *)result);
		return -1;
	}

	/* Poll for completion */
	do {
		n = ibv_poll_cq(actx->cq, 1, &wc);
	} while (n == 0);

	if (n < 0 || wc.status != IBV_WC_SUCCESS) {
		fprintf(stderr, "CAS failed\n");
		free((void *)result);
		return -1;
	}

	if (wc.opcode != IBV_WC_COMP_SWAP) {
		fprintf(stderr, "Unexpected opcode: %d\n", wc.opcode);
		free((void *)result);
		return -1;
	}

	free((void *)result);
	return 0;
}

int main(void)
{
	struct ibv_device **dev_list;
	struct atomic_context actx;
	int num_devices, i;

	printf("======================================\n");
	printf("Atomic Operations Test\n");
	printf("======================================\n\n");

	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list || num_devices == 0) {
		printf("No CXL devices found\n");
		return 0;
	}

	printf("Found %d device(s)\n", num_devices);
	printf("Using: %s\n\n", ibv_get_device_name(dev_list[0]));

	memset(&actx, 0, sizeof(actx));
	if (setup_atomic_context(dev_list[0], &actx) != 0) {
		fprintf(stderr, "Failed to setup context\n");
		ibv_free_device_list(dev_list);
		return 1;
	}

	printf("\nRunning Fetch-and-Add tests...\n");
	printf("------------------------------\n");
	printf("Initial counter value: %lu\n", *actx.counter);

	for (i = 0; i < 5; i++) {
		uint64_t add = i + 1;
		printf("Test %d: Fetch-and-Add(%lu)... ", i + 1, add);
		fflush(stdout);

		if (test_fetch_and_add(&actx, add) == 0) {
			printf("PASS (counter now: %lu)\n", *actx.counter);
		} else {
			printf("FAIL\n");
			cleanup_atomic_context(&actx);
			ibv_free_device_list(dev_list);
			return 1;
		}
	}

	printf("\nRunning Compare-and-Swap tests...\n");
	printf("------------------------------\n");
	printf("Current counter value: %lu\n", *actx.counter);

	/* Test CAS success case */
	printf("Test 1: CAS(compare=%lu, swap=100)... ", *actx.counter);
	fflush(stdout);
	if (test_compare_and_swap(&actx, *actx.counter, 100) == 0) {
		printf("PASS (counter now: %lu)\n", *actx.counter);
	} else {
		printf("FAIL\n");
		cleanup_atomic_context(&actx);
		ibv_free_device_list(dev_list);
		return 1;
	}

	/* Test CAS with wrong compare value */
	printf("Test 2: CAS(compare=999, swap=200)... ");
	fflush(stdout);
	if (test_compare_and_swap(&actx, 999, 200) == 0) {
		printf("PASS (counter unchanged: %lu)\n", *actx.counter);
	} else {
		printf("FAIL\n");
		cleanup_atomic_context(&actx);
		ibv_free_device_list(dev_list);
		return 1;
	}

	printf("\n======================================\n");
	printf("All Atomic tests PASSED!\n");
	printf("Final counter value: %lu\n", *actx.counter);
	printf("======================================\n");

	cleanup_atomic_context(&actx);
	ibv_free_device_list(dev_list);

	return 0;
}
