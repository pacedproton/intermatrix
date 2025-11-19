/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Send/Recv Test
 *
 * Tests two-sided RDMA Send/Receive operations using CXL verbs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "libcxl/compat/verbs.h"

#define MSG_SIZE 1024
#define NUM_MSGS 5

struct msg_context {
	struct ibv_context *ctx;
	struct ibv_pd *pd;
	struct ibv_mr *send_mr;
	struct ibv_mr *recv_mr;
	struct ibv_cq *cq;
	struct ibv_qp *qp;
	char *send_buf;
	char *recv_buf;
};

static int setup_messaging(struct ibv_device *dev, struct msg_context *mctx)
{
	struct ibv_qp_init_attr qp_init_attr;
	struct ibv_qp_attr qp_attr;

	printf("Setting up messaging resources...\n");

	mctx->ctx = ibv_open_device(dev);
	if (!mctx->ctx) {
		fprintf(stderr, "Failed to open device\n");
		return -1;
	}

	mctx->pd = ibv_alloc_pd(mctx->ctx);
	if (!mctx->pd) {
		fprintf(stderr, "Failed to allocate PD\n");
		return -1;
	}

	/* Allocate send buffer */
	mctx->send_buf = malloc(MSG_SIZE);
	if (!mctx->send_buf) {
		fprintf(stderr, "Failed to allocate send buffer\n");
		return -1;
	}

	/* Allocate receive buffer */
	mctx->recv_buf = malloc(MSG_SIZE);
	if (!mctx->recv_buf) {
		fprintf(stderr, "Failed to allocate recv buffer\n");
		return -1;
	}
	memset(mctx->recv_buf, 0, MSG_SIZE);

	/* Register buffers */
	mctx->send_mr = ibv_reg_mr(mctx->pd, mctx->send_buf, MSG_SIZE,
				   IBV_ACCESS_LOCAL_WRITE);
	if (!mctx->send_mr) {
		fprintf(stderr, "Failed to register send MR\n");
		return -1;
	}

	mctx->recv_mr = ibv_reg_mr(mctx->pd, mctx->recv_buf, MSG_SIZE,
				   IBV_ACCESS_LOCAL_WRITE);
	if (!mctx->recv_mr) {
		fprintf(stderr, "Failed to register recv MR\n");
		return -1;
	}

	/* Create CQ */
	mctx->cq = ibv_create_cq(mctx->ctx, 100, NULL, NULL, 0);
	if (!mctx->cq) {
		fprintf(stderr, "Failed to create CQ\n");
		return -1;
	}

	/* Create QP */
	memset(&qp_init_attr, 0, sizeof(qp_init_attr));
	qp_init_attr.send_cq = mctx->cq;
	qp_init_attr.recv_cq = mctx->cq;
	qp_init_attr.qp_type = IBV_QPT_RC;
	qp_init_attr.cap.max_send_wr = 10;
	qp_init_attr.cap.max_recv_wr = 10;
	qp_init_attr.cap.max_send_sge = 1;
	qp_init_attr.cap.max_recv_sge = 1;

	mctx->qp = ibv_create_qp(mctx->pd, &qp_init_attr);
	if (!mctx->qp) {
		fprintf(stderr, "Failed to create QP\n");
		return -1;
	}

	/* Transition to RTS */
	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IBV_QPS_INIT;
	qp_attr.port_num = 1;
	qp_attr.qp_access_flags = 0;
	ibv_modify_qp(mctx->qp, &qp_attr, (1 << 0) | (1 << 11) | (1 << 10));

	qp_attr.qp_state = IBV_QPS_RTR;
	qp_attr.path_mtu = IBV_MTU_4096;
	qp_attr.dest_qp_num = mctx->qp->qp_num;
	qp_attr.rq_psn = 0;
	ibv_modify_qp(mctx->qp, &qp_attr, (1 << 0) | (1 << 5) | (1 << 9) | (1 << 7));

	qp_attr.qp_state = IBV_QPS_RTS;
	qp_attr.sq_psn = 0;
	ibv_modify_qp(mctx->qp, &qp_attr, (1 << 0) | (1 << 8));

	printf("  Send buffer: %p (lkey: 0x%x)\n",
	       mctx->send_buf, mctx->send_mr->lkey);
	printf("  Recv buffer: %p (lkey: 0x%x)\n",
	       mctx->recv_buf, mctx->recv_mr->lkey);
	printf("  QP #%u ready\n", mctx->qp->qp_num);

	return 0;
}

static void cleanup_messaging(struct msg_context *mctx)
{
	if (mctx->qp) ibv_destroy_qp(mctx->qp);
	if (mctx->cq) ibv_destroy_cq(mctx->cq);
	if (mctx->recv_mr) ibv_dereg_mr(mctx->recv_mr);
	if (mctx->send_mr) ibv_dereg_mr(mctx->send_mr);
	if (mctx->recv_buf) free(mctx->recv_buf);
	if (mctx->send_buf) free(mctx->send_buf);
	if (mctx->pd) ibv_dealloc_pd(mctx->pd);
	if (mctx->ctx) ibv_close_device(mctx->ctx);
}

static int test_send(struct msg_context *mctx, const char *msg, size_t len)
{
	struct ibv_send_wr wr, *bad_wr;
	struct ibv_sge sge;
	struct ibv_wc wc;
	int n;

	/* Prepare message */
	memcpy(mctx->send_buf, msg, len);

	/* Setup SGE */
	memset(&sge, 0, sizeof(sge));
	sge.addr = (uint64_t)mctx->send_buf;
	sge.length = len;
	sge.lkey = mctx->send_mr->lkey;

	/* Setup work request */
	memset(&wr, 0, sizeof(wr));
	wr.wr_id = 1;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	wr.opcode = IBV_WR_SEND;
	wr.send_flags = IBV_SEND_SIGNALED;

	/* Post send */
	if (ibv_post_send(mctx->qp, &wr, &bad_wr) != 0) {
		fprintf(stderr, "Failed to post send\n");
		return -1;
	}

	/* Poll for completion */
	do {
		n = ibv_poll_cq(mctx->cq, 1, &wc);
	} while (n == 0);

	if (n < 0) {
		fprintf(stderr, "Poll CQ failed\n");
		return -1;
	}

	if (wc.status != IBV_WC_SUCCESS) {
		fprintf(stderr, "Send failed: %s\n", ibv_wc_status_str(wc.status));
		return -1;
	}

	if (wc.opcode != IBV_WC_SEND) {
		fprintf(stderr, "Unexpected opcode: %d\n", wc.opcode);
		return -1;
	}

	return 0;
}

int main(void)
{
	struct ibv_device **dev_list;
	struct msg_context mctx;
	int num_devices, i;
	const char *messages[] = {
		"Hello, World!",
		"RDMA Send/Recv Test",
		"CXL Interconnect",
		"Message 4",
		"Final message"
	};

	printf("======================================\n");
	printf("Send/Recv Test\n");
	printf("======================================\n\n");

	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list || num_devices == 0) {
		printf("No CXL devices found\n");
		return 0;
	}

	printf("Found %d device(s)\n", num_devices);
	printf("Using: %s\n\n", ibv_get_device_name(dev_list[0]));

	memset(&mctx, 0, sizeof(mctx));
	if (setup_messaging(dev_list[0], &mctx) != 0) {
		fprintf(stderr, "Failed to setup messaging\n");
		ibv_free_device_list(dev_list);
		return 1;
	}

	printf("\nSending messages...\n");
	printf("------------------------------\n");

	for (i = 0; i < NUM_MSGS; i++) {
		size_t len = strlen(messages[i]) + 1;
		printf("Message %d: \"%s\" (%zu bytes)... ", i + 1, messages[i], len);
		fflush(stdout);

		if (test_send(&mctx, messages[i], len) == 0) {
			printf("SENT\n");
		} else {
			printf("FAIL\n");
			cleanup_messaging(&mctx);
			ibv_free_device_list(dev_list);
			return 1;
		}
	}

	printf("\n======================================\n");
	printf("All Send/Recv tests PASSED!\n");
	printf("======================================\n");

	cleanup_messaging(&mctx);
	ibv_free_device_list(dev_list);

	return 0;
}
