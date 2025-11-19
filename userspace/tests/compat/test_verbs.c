/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Verbs Compatibility Tests
 *
 * Tests libibverbs-compatible interface on CXL
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "libcxl/compat/verbs.h"
#include "../test_framework.h"

/* Test: Device enumeration */
TEST(verbs_device_list) {
	struct ibv_device **dev_list;
	int num_devices;

	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list) {
		printf(COLOR_YELLOW "SKIP" COLOR_RESET " (no devices)\\n");
		tests_passed++;
		return;
	}

	printf(COLOR_YELLOW "[%d devices]" COLOR_RESET " ", num_devices);
	ASSERT(num_devices > 0, "Should have at least one device");

	/* Verify first device has a name */
	ASSERT_NOT_NULL(dev_list[0], "First device should exist");
	const char *name = ibv_get_device_name(dev_list[0]);
	ASSERT_NOT_NULL(name, "Device should have a name");
	printf(COLOR_YELLOW "[%s]" COLOR_RESET " ", name);

	/* Get device GUID */
	uint64_t guid = ibv_get_device_guid(dev_list[0]);
	ASSERT(guid != 0, "GUID should be non-zero");

	ibv_free_device_list(dev_list);
	TEST_PASS();
}

/* Test: Device open/close */
TEST(verbs_device_open_close) {
	struct ibv_device **dev_list;
	struct ibv_context *ctx;
	int num_devices;

	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list || num_devices == 0) {
		printf(COLOR_YELLOW "SKIP" COLOR_RESET " (no devices)\\n");
		tests_passed++;
		return;
	}

	ctx = ibv_open_device(dev_list[0]);
	ASSERT_NOT_NULL(ctx, "Device open should succeed");

	int ret = ibv_close_device(ctx);
	ASSERT_EQ(ret, 0, "Device close should succeed");

	ibv_free_device_list(dev_list);
	TEST_PASS();
}

/* Test: Query device attributes */
TEST(verbs_query_device) {
	struct ibv_device **dev_list;
	struct ibv_context *ctx;
	struct ibv_device_attr attr;
	int num_devices, ret;

	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list || num_devices == 0) {
		printf(COLOR_YELLOW "SKIP" COLOR_RESET " (no devices)\\n");
		tests_passed++;
		return;
	}

	ctx = ibv_open_device(dev_list[0]);
	ASSERT_NOT_NULL(ctx, "Device open should succeed");

	ret = ibv_query_device(ctx, &attr);
	ASSERT_EQ(ret, 0, "Query device should succeed");

	/* Verify attributes */
	ASSERT(attr.max_qp > 0, "Should support QPs");
	ASSERT(attr.max_cq > 0, "Should support CQs");
	ASSERT(attr.max_mr > 0, "Should support MRs");
	printf(COLOR_YELLOW "[max_qp=%d, max_cq=%d]" COLOR_RESET " ",
	       attr.max_qp, attr.max_cq);

	ibv_close_device(ctx);
	ibv_free_device_list(dev_list);
	TEST_PASS();
}

/* Test: Query port attributes */
TEST(verbs_query_port) {
	struct ibv_device **dev_list;
	struct ibv_context *ctx;
	struct ibv_port_attr attr;
	int num_devices, ret;

	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list || num_devices == 0) {
		printf(COLOR_YELLOW "SKIP" COLOR_RESET " (no devices)\\n");
		tests_passed++;
		return;
	}

	ctx = ibv_open_device(dev_list[0]);
	ASSERT_NOT_NULL(ctx, "Device open should succeed");

	ret = ibv_query_port(ctx, 1, &attr);
	ASSERT_EQ(ret, 0, "Query port should succeed");

	/* Verify port is active */
	ASSERT_EQ(attr.state, IBV_PORT_ACTIVE, "Port should be active");
	ASSERT(attr.max_mtu >= IBV_MTU_1024, "MTU should be reasonable");
	printf(COLOR_YELLOW "[%s, MTU=%d]" COLOR_RESET " ",
	       attr.state == IBV_PORT_ACTIVE ? "ACTIVE" : "DOWN",
	       1 << (attr.active_mtu + 7));

	ibv_close_device(ctx);
	ibv_free_device_list(dev_list);
	TEST_PASS();
}

/* Test: Protection Domain allocation */
TEST(verbs_alloc_pd) {
	struct ibv_device **dev_list;
	struct ibv_context *ctx;
	struct ibv_pd *pd;
	int num_devices;

	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list || num_devices == 0) {
		printf(COLOR_YELLOW "SKIP" COLOR_RESET " (no devices)\\n");
		tests_passed++;
		return;
	}

	ctx = ibv_open_device(dev_list[0]);
	ASSERT_NOT_NULL(ctx, "Device open should succeed");

	pd = ibv_alloc_pd(ctx);
	ASSERT_NOT_NULL(pd, "PD allocation should succeed");

	int ret = ibv_dealloc_pd(pd);
	ASSERT_EQ(ret, 0, "PD deallocation should succeed");

	ibv_close_device(ctx);
	ibv_free_device_list(dev_list);
	TEST_PASS();
}

/* Test: Memory region registration */
TEST(verbs_reg_mr) {
	struct ibv_device **dev_list;
	struct ibv_context *ctx;
	struct ibv_pd *pd;
	struct ibv_mr *mr;
	char buffer[4096];
	int num_devices, ret, i;

	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list || num_devices == 0) {
		printf(COLOR_YELLOW "SKIP" COLOR_RESET " (no devices)\\n");
		tests_passed++;
		return;
	}

	ctx = ibv_open_device(dev_list[0]);
	ASSERT_NOT_NULL(ctx, "Device open should succeed");

	pd = ibv_alloc_pd(ctx);
	ASSERT_NOT_NULL(pd, "PD allocation should succeed");

	/* Initialize buffer */
	for (i = 0; i < sizeof(buffer); i++)
		buffer[i] = i & 0xFF;

	/* Register memory */
	mr = ibv_reg_mr(pd, buffer, sizeof(buffer),
			IBV_ACCESS_LOCAL_WRITE |
			IBV_ACCESS_REMOTE_WRITE |
			IBV_ACCESS_REMOTE_READ);
	ASSERT_NOT_NULL(mr, "MR registration should succeed");

	/* Verify keys */
	ASSERT(mr->lkey != 0, "lkey should be non-zero");
	ASSERT(mr->rkey != 0, "rkey should be non-zero");
	printf(COLOR_YELLOW "[lkey=0x%x]" COLOR_RESET " ", mr->lkey);

	ret = ibv_dereg_mr(mr);
	ASSERT_EQ(ret, 0, "MR deregistration should succeed");

	ibv_dealloc_pd(pd);
	ibv_close_device(ctx);
	ibv_free_device_list(dev_list);
	TEST_PASS();
}

/* Test: Completion Queue creation */
TEST(verbs_create_cq) {
	struct ibv_device **dev_list;
	struct ibv_context *ctx;
	struct ibv_cq *cq;
	int num_devices, ret;

	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list || num_devices == 0) {
		printf(COLOR_YELLOW "SKIP" COLOR_RESET " (no devices)\\n");
		tests_passed++;
		return;
	}

	ctx = ibv_open_device(dev_list[0]);
	ASSERT_NOT_NULL(ctx, "Device open should succeed");

	cq = ibv_create_cq(ctx, 100, NULL, NULL, 0);
	ASSERT_NOT_NULL(cq, "CQ creation should succeed");

	ret = ibv_destroy_cq(cq);
	ASSERT_EQ(ret, 0, "CQ destruction should succeed");

	ibv_close_device(ctx);
	ibv_free_device_list(dev_list);
	TEST_PASS();
}

/* Test: Queue Pair creation */
TEST(verbs_create_qp) {
	struct ibv_device **dev_list;
	struct ibv_context *ctx;
	struct ibv_pd *pd;
	struct ibv_cq *cq;
	struct ibv_qp *qp;
	struct ibv_qp_init_attr qp_attr;
	int num_devices, ret;

	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list || num_devices == 0) {
		printf(COLOR_YELLOW "SKIP" COLOR_RESET " (no devices)\\n");
		tests_passed++;
		return;
	}

	ctx = ibv_open_device(dev_list[0]);
	ASSERT_NOT_NULL(ctx, "Device open should succeed");

	pd = ibv_alloc_pd(ctx);
	ASSERT_NOT_NULL(pd, "PD allocation should succeed");

	cq = ibv_create_cq(ctx, 100, NULL, NULL, 0);
	ASSERT_NOT_NULL(cq, "CQ creation should succeed");

	/* Create RC QP */
	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.send_cq = cq;
	qp_attr.recv_cq = cq;
	qp_attr.qp_type = IBV_QPT_RC;
	qp_attr.cap.max_send_wr = 10;
	qp_attr.cap.max_recv_wr = 10;
	qp_attr.cap.max_send_sge = 1;
	qp_attr.cap.max_recv_sge = 1;

	qp = ibv_create_qp(pd, &qp_attr);
	ASSERT_NOT_NULL(qp, "QP creation should succeed");
	printf(COLOR_YELLOW "[QP #%u]" COLOR_RESET " ", qp->qp_num);

	ret = ibv_destroy_qp(qp);
	ASSERT_EQ(ret, 0, "QP destruction should succeed");

	ibv_destroy_cq(cq);
	ibv_dealloc_pd(pd);
	ibv_close_device(ctx);
	ibv_free_device_list(dev_list);
	TEST_PASS();
}

/* Test: QP state transitions */
TEST(verbs_qp_state_transitions) {
	struct ibv_device **dev_list;
	struct ibv_context *ctx;
	struct ibv_pd *pd;
	struct ibv_cq *cq;
	struct ibv_qp *qp;
	struct ibv_qp_init_attr qp_init_attr;
	struct ibv_qp_attr qp_attr;
	int num_devices, ret;

	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list || num_devices == 0) {
		printf(COLOR_YELLOW "SKIP" COLOR_RESET " (no devices)\\n");
		tests_passed++;
		return;
	}

	ctx = ibv_open_device(dev_list[0]);
	pd = ibv_alloc_pd(ctx);
	cq = ibv_create_cq(ctx, 100, NULL, NULL, 0);

	memset(&qp_init_attr, 0, sizeof(qp_init_attr));
	qp_init_attr.send_cq = cq;
	qp_init_attr.recv_cq = cq;
	qp_init_attr.qp_type = IBV_QPT_RC;
	qp_init_attr.cap.max_send_wr = 10;
	qp_init_attr.cap.max_recv_wr = 10;
	qp_init_attr.cap.max_send_sge = 1;
	qp_init_attr.cap.max_recv_sge = 1;

	qp = ibv_create_qp(pd, &qp_init_attr);
	ASSERT_NOT_NULL(qp, "QP creation should succeed");

	/* RESET -> INIT */
	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IBV_QPS_INIT;
	qp_attr.port_num = 1;
	qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE |
				  IBV_ACCESS_REMOTE_READ;
	ret = ibv_modify_qp(qp, &qp_attr, (1 << 0) | (1 << 11) | (1 << 10));
	ASSERT_EQ(ret, 0, "RESET->INIT should succeed");

	/* INIT -> RTR */
	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IBV_QPS_RTR;
	qp_attr.path_mtu = IBV_MTU_4096;
	qp_attr.dest_qp_num = qp->qp_num; /* Loopback */
	qp_attr.rq_psn = 0;
	ret = ibv_modify_qp(qp, &qp_attr, (1 << 0) | (1 << 5) | (1 << 9) | (1 << 7));
	ASSERT_EQ(ret, 0, "INIT->RTR should succeed");

	/* RTR -> RTS */
	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IBV_QPS_RTS;
	qp_attr.sq_psn = 0;
	ret = ibv_modify_qp(qp, &qp_attr, (1 << 0) | (1 << 8));
	ASSERT_EQ(ret, 0, "RTR->RTS should succeed");

	printf(COLOR_YELLOW "[RESET->INIT->RTR->RTS]" COLOR_RESET " ");

	ibv_destroy_qp(qp);
	ibv_destroy_cq(cq);
	ibv_dealloc_pd(pd);
	ibv_close_device(ctx);
	ibv_free_device_list(dev_list);
	TEST_PASS();
}

/* Test: RDMA Write operation */
TEST(verbs_rdma_write) {
	struct ibv_device **dev_list;
	struct ibv_context *ctx;
	struct ibv_pd *pd;
	struct ibv_cq *cq;
	struct ibv_qp *qp;
	struct ibv_mr *mr;
	struct ibv_qp_init_attr qp_init_attr;
	struct ibv_qp_attr qp_attr;
	struct ibv_send_wr wr, *bad_wr;
	struct ibv_sge sge;
	struct ibv_wc wc;
	char buffer[1024];
	int num_devices, ret, i, n;

	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list || num_devices == 0) {
		printf(COLOR_YELLOW "SKIP" COLOR_RESET " (no devices)\\n");
		tests_passed++;
		return;
	}

	ctx = ibv_open_device(dev_list[0]);
	pd = ibv_alloc_pd(ctx);
	cq = ibv_create_cq(ctx, 100, NULL, NULL, 0);

	/* Register memory */
	for (i = 0; i < sizeof(buffer); i++)
		buffer[i] = i & 0xFF;

	mr = ibv_reg_mr(pd, buffer, sizeof(buffer),
			IBV_ACCESS_LOCAL_WRITE |
			IBV_ACCESS_REMOTE_WRITE |
			IBV_ACCESS_REMOTE_READ);
	ASSERT_NOT_NULL(mr, "MR registration should succeed");

	/* Create and initialize QP */
	memset(&qp_init_attr, 0, sizeof(qp_init_attr));
	qp_init_attr.send_cq = cq;
	qp_init_attr.recv_cq = cq;
	qp_init_attr.qp_type = IBV_QPT_RC;
	qp_init_attr.cap.max_send_wr = 10;
	qp_init_attr.cap.max_recv_wr = 10;
	qp_init_attr.cap.max_send_sge = 1;
	qp_init_attr.cap.max_recv_sge = 1;

	qp = ibv_create_qp(pd, &qp_init_attr);

	/* Transition to RTS */
	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IBV_QPS_INIT;
	qp_attr.port_num = 1;
	qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
	ibv_modify_qp(qp, &qp_attr, (1 << 0) | (1 << 11) | (1 << 10));

	qp_attr.qp_state = IBV_QPS_RTR;
	qp_attr.path_mtu = IBV_MTU_4096;
	qp_attr.dest_qp_num = qp->qp_num;
	qp_attr.rq_psn = 0;
	ibv_modify_qp(qp, &qp_attr, (1 << 0) | (1 << 5) | (1 << 9) | (1 << 7));

	qp_attr.qp_state = IBV_QPS_RTS;
	qp_attr.sq_psn = 0;
	ibv_modify_qp(qp, &qp_attr, (1 << 0) | (1 << 8));

	/* Post RDMA Write */
	memset(&sge, 0, sizeof(sge));
	sge.addr = (uint64_t)buffer;
	sge.length = 256;
	sge.lkey = mr->lkey;

	memset(&wr, 0, sizeof(wr));
	wr.wr_id = 1;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	wr.opcode = IBV_WR_RDMA_WRITE;
	wr.send_flags = IBV_SEND_SIGNALED;
	wr.wr.rdma.remote_addr = 0x1000;
	wr.wr.rdma.rkey = mr->rkey;

	ret = ibv_post_send(qp, &wr, &bad_wr);
	ASSERT_EQ(ret, 0, "Post send should succeed");

	/* Poll for completion */
	n = ibv_poll_cq(cq, 1, &wc);
	ASSERT_EQ(n, 1, "Should get one completion");
	ASSERT_EQ(wc.status, IBV_WC_SUCCESS, "WC status should be success");
	ASSERT_EQ(wc.wr_id, 1, "WR ID should match");
	ASSERT_EQ(wc.opcode, IBV_WC_RDMA_WRITE, "Opcode should be RDMA_WRITE");

	printf(COLOR_YELLOW "[RDMA_WRITE: %u bytes]" COLOR_RESET " ", wc.byte_len);

	ibv_dereg_mr(mr);
	ibv_destroy_qp(qp);
	ibv_destroy_cq(cq);
	ibv_dealloc_pd(pd);
	ibv_close_device(ctx);
	ibv_free_device_list(dev_list);
	TEST_PASS();
}

/* Test: CQ polling */
TEST(verbs_poll_cq) {
	struct ibv_device **dev_list;
	struct ibv_context *ctx;
	struct ibv_pd *pd;
	struct ibv_cq *cq;
	struct ibv_qp *qp;
	struct ibv_mr *mr;
	struct ibv_qp_init_attr qp_init_attr;
	struct ibv_qp_attr qp_attr;
	struct ibv_send_wr wr, *bad_wr;
	struct ibv_sge sge;
	struct ibv_wc wc[10];
	char buffer[1024];
	int num_devices, ret, i, n;

	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list || num_devices == 0) {
		printf(COLOR_YELLOW "SKIP" COLOR_RESET " (no devices)\\n");
		tests_passed++;
		return;
	}

	ctx = ibv_open_device(dev_list[0]);
	pd = ibv_alloc_pd(ctx);
	cq = ibv_create_cq(ctx, 100, NULL, NULL, 0);

	memset(buffer, 0xAA, sizeof(buffer));
	mr = ibv_reg_mr(pd, buffer, sizeof(buffer),
			IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);

	memset(&qp_init_attr, 0, sizeof(qp_init_attr));
	qp_init_attr.send_cq = cq;
	qp_init_attr.recv_cq = cq;
	qp_init_attr.qp_type = IBV_QPT_RC;
	qp_init_attr.cap.max_send_wr = 10;
	qp_init_attr.cap.max_recv_wr = 10;
	qp_init_attr.cap.max_send_sge = 1;
	qp_init_attr.cap.max_recv_sge = 1;
	qp = ibv_create_qp(pd, &qp_init_attr);

	/* Transition to RTS */
	qp_attr.qp_state = IBV_QPS_INIT;
	qp_attr.port_num = 1;
	qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;
	ibv_modify_qp(qp, &qp_attr, (1 << 0) | (1 << 11) | (1 << 10));
	qp_attr.qp_state = IBV_QPS_RTR;
	qp_attr.path_mtu = IBV_MTU_4096;
	qp_attr.dest_qp_num = qp->qp_num;
	qp_attr.rq_psn = 0;
	ibv_modify_qp(qp, &qp_attr, (1 << 0) | (1 << 5) | (1 << 9) | (1 << 7));
	qp_attr.qp_state = IBV_QPS_RTS;
	qp_attr.sq_psn = 0;
	ibv_modify_qp(qp, &qp_attr, (1 << 0) | (1 << 8));

	/* Post multiple sends */
	memset(&sge, 0, sizeof(sge));
	sge.addr = (uint64_t)buffer;
	sge.length = 64;
	sge.lkey = mr->lkey;

	for (i = 0; i < 5; i++) {
		memset(&wr, 0, sizeof(wr));
		wr.wr_id = i;
		wr.sg_list = &sge;
		wr.num_sge = 1;
		wr.opcode = IBV_WR_SEND;
		wr.send_flags = IBV_SEND_SIGNALED;

		ret = ibv_post_send(qp, &wr, &bad_wr);
		ASSERT_EQ(ret, 0, "Post send should succeed");
	}

	/* Poll all completions */
	n = ibv_poll_cq(cq, 10, wc);
	ASSERT_EQ(n, 5, "Should get 5 completions");

	for (i = 0; i < n; i++) {
		ASSERT_EQ(wc[i].status, IBV_WC_SUCCESS, "Status should be success");
	}

	printf(COLOR_YELLOW "[Polled %d completions]" COLOR_RESET " ", n);

	ibv_dereg_mr(mr);
	ibv_destroy_qp(qp);
	ibv_destroy_cq(cq);
	ibv_dealloc_pd(pd);
	ibv_close_device(ctx);
	ibv_free_device_list(dev_list);
	TEST_PASS();
}

/* Test: WC status strings */
TEST(verbs_wc_status_str) {
	const char *str;

	str = ibv_wc_status_str(IBV_WC_SUCCESS);
	ASSERT_NOT_NULL(str, "Should return status string");
	ASSERT(strcmp(str, "success") == 0, "Should be 'success'");

	str = ibv_wc_status_str(IBV_WC_LOC_LEN_ERR);
	ASSERT_NOT_NULL(str, "Should return error string");

	TEST_PASS();
}

TEST_MAIN_BEGIN()
	TEST_SUITE_BEGIN("Verbs Compatibility Tests")
	RUN_TEST(verbs_device_list);
	RUN_TEST(verbs_device_open_close);
	RUN_TEST(verbs_query_device);
	RUN_TEST(verbs_query_port);
	RUN_TEST(verbs_alloc_pd);
	RUN_TEST(verbs_reg_mr);
	RUN_TEST(verbs_create_cq);
	RUN_TEST(verbs_create_qp);
	RUN_TEST(verbs_qp_state_transitions);
	RUN_TEST(verbs_rdma_write);
	RUN_TEST(verbs_poll_cq);
	RUN_TEST(verbs_wc_status_str);
	TEST_SUITE_END()
TEST_MAIN_END()
