/* SPDX-License-Identifier: Apache-2.0 */
/*
 * RDMA Verbs Example using CXL
 *
 * This demonstrates how existing RDMA applications can run on CXL
 * without any code changes - just recompile and link with libcxl.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcxl/compat/verbs.h"

int main(void)
{
	struct ibv_device **dev_list;
	struct ibv_context *ctx;
	struct ibv_device_attr device_attr;
	struct ibv_port_attr port_attr;
	struct ibv_pd *pd;
	struct ibv_mr *mr;
	struct ibv_cq *cq;
	struct ibv_qp *qp;
	struct ibv_qp_init_attr qp_init_attr;
	struct ibv_qp_attr qp_attr;
	struct ibv_send_wr wr, *bad_wr;
	struct ibv_sge sge;
	struct ibv_wc wc;
	char buffer[1024];
	int num_devices, ret, i, n;

	printf("=====================================\n");
	printf("RDMA Verbs on CXL Example\n");
	printf("=====================================\n\n");

	/* Step 1: Get device list */
	printf("1. Getting device list...\n");
	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list || num_devices == 0) {
		printf("   No CXL devices found!\n");
		printf("   (This is expected if kernel module is not loaded)\n");
		return 0;
	}
	printf("   Found %d device(s)\n", num_devices);
	printf("   Device 0: %s\n\n", ibv_get_device_name(dev_list[0]));

	/* Step 2: Open device */
	printf("2. Opening device...\n");
	ctx = ibv_open_device(dev_list[0]);
	if (!ctx) {
		printf("   Failed to open device\n");
		ibv_free_device_list(dev_list);
		return 1;
	}
	printf("   Device opened successfully\n\n");

	/* Step 3: Query device capabilities */
	printf("3. Querying device attributes...\n");
	ret = ibv_query_device(ctx, &device_attr);
	if (ret == 0) {
		printf("   Firmware version: %s\n", device_attr.fw_ver);
		printf("   Max QPs:          %d\n", device_attr.max_qp);
		printf("   Max CQs:          %d\n", device_attr.max_cq);
		printf("   Max MRs:          %d\n", device_attr.max_mr);
		printf("   Atomic support:   %s\n\n",
		       device_attr.atomic_cap ? "Yes" : "No");
	}

	/* Step 4: Query port */
	printf("4. Querying port status...\n");
	ret = ibv_query_port(ctx, 1, &port_attr);
	if (ret == 0) {
		const char *state_str[] = {
			"NOP", "DOWN", "INIT", "ARMED", "ACTIVE", "ACTIVE_DEFER"
		};
		printf("   State:      %s\n", state_str[port_attr.state]);
		printf("   MTU:        %d bytes\n", 1 << (port_attr.active_mtu + 7));
		printf("   Max msg sz: %u bytes\n\n", port_attr.max_msg_sz);
	}

	/* Step 5: Allocate Protection Domain */
	printf("5. Allocating Protection Domain...\n");
	pd = ibv_alloc_pd(ctx);
	if (!pd) {
		printf("   Failed to allocate PD\n");
		ibv_close_device(ctx);
		ibv_free_device_list(dev_list);
		return 1;
	}
	printf("   PD allocated\n\n");

	/* Step 6: Register memory region */
	printf("6. Registering memory region...\n");
	for (i = 0; i < sizeof(buffer); i++)
		buffer[i] = i & 0xFF;

	mr = ibv_reg_mr(pd, buffer, sizeof(buffer),
			IBV_ACCESS_LOCAL_WRITE |
			IBV_ACCESS_REMOTE_WRITE |
			IBV_ACCESS_REMOTE_READ);
	if (!mr) {
		printf("   Failed to register MR\n");
		ibv_dealloc_pd(pd);
		ibv_close_device(ctx);
		ibv_free_device_list(dev_list);
		return 1;
	}
	printf("   MR registered: lkey=0x%x, rkey=0x%x\n\n", mr->lkey, mr->rkey);

	/* Step 7: Create Completion Queue */
	printf("7. Creating Completion Queue...\n");
	cq = ibv_create_cq(ctx, 100, NULL, NULL, 0);
	if (!cq) {
		printf("   Failed to create CQ\n");
		ibv_dereg_mr(mr);
		ibv_dealloc_pd(pd);
		ibv_close_device(ctx);
		ibv_free_device_list(dev_list);
		return 1;
	}
	printf("   CQ created (size: 100 entries)\n\n");

	/* Step 8: Create Queue Pair */
	printf("8. Creating Queue Pair...\n");
	memset(&qp_init_attr, 0, sizeof(qp_init_attr));
	qp_init_attr.send_cq = cq;
	qp_init_attr.recv_cq = cq;
	qp_init_attr.qp_type = IBV_QPT_RC; /* Reliable Connection */
	qp_init_attr.cap.max_send_wr = 10;
	qp_init_attr.cap.max_recv_wr = 10;
	qp_init_attr.cap.max_send_sge = 1;
	qp_init_attr.cap.max_recv_sge = 1;

	qp = ibv_create_qp(pd, &qp_init_attr);
	if (!qp) {
		printf("   Failed to create QP\n");
		ibv_destroy_cq(cq);
		ibv_dereg_mr(mr);
		ibv_dealloc_pd(pd);
		ibv_close_device(ctx);
		ibv_free_device_list(dev_list);
		return 1;
	}
	printf("   QP created (QP #%u, type: RC)\n\n", qp->qp_num);

	/* Step 9: Transition QP to Ready To Send (RTS) */
	printf("9. Transitioning QP to RTS state...\n");

	/* RESET -> INIT */
	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IBV_QPS_INIT;
	qp_attr.port_num = 1;
	qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
	ibv_modify_qp(qp, &qp_attr, (1 << 0) | (1 << 11) | (1 << 10));
	printf("   RESET -> INIT\n");

	/* INIT -> RTR (Ready To Receive) */
	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IBV_QPS_RTR;
	qp_attr.path_mtu = IBV_MTU_4096;
	qp_attr.dest_qp_num = qp->qp_num; /* Loopback for demo */
	qp_attr.rq_psn = 0;
	ibv_modify_qp(qp, &qp_attr, (1 << 0) | (1 << 5) | (1 << 9) | (1 << 7));
	printf("   INIT -> RTR\n");

	/* RTR -> RTS (Ready To Send) */
	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IBV_QPS_RTS;
	qp_attr.sq_psn = 0;
	ibv_modify_qp(qp, &qp_attr, (1 << 0) | (1 << 8));
	printf("   RTR  -> RTS\n");
	printf("   QP is now ready for data transfer!\n\n");

	/* Step 10: Post RDMA Write operation */
	printf("10. Posting RDMA Write operation...\n");
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
	if (ret != 0) {
		printf("   Failed to post send (ret=%d)\n", ret);
	} else {
		printf("   RDMA Write posted (wr_id=%llu, length=%u)\n",
		       (unsigned long long)wr.wr_id, sge.length);
	}

	/* Step 11: Poll for completion */
	printf("\n11. Polling for completion...\n");
	n = ibv_poll_cq(cq, 1, &wc);
	if (n > 0) {
		printf("   Got %d completion(s)\n", n);
		printf("   WR ID:      %llu\n", (unsigned long long)wc.wr_id);
		printf("   Status:     %s\n", ibv_wc_status_str(wc.status));
		printf("   Opcode:     RDMA_WRITE\n");
		printf("   Byte count: %u\n", wc.byte_len);
	} else {
		printf("   No completions available\n");
	}

	/* Cleanup */
	printf("\n12. Cleaning up...\n");
	ibv_destroy_qp(qp);
	ibv_destroy_cq(cq);
	ibv_dereg_mr(mr);
	ibv_dealloc_pd(pd);
	ibv_close_device(ctx);
	ibv_free_device_list(dev_list);

	printf("   Done!\n\n");
	printf("=====================================\n");
	printf("Example completed successfully!\n");
	printf("=====================================\n");

	return 0;
}
