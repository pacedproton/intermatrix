/* SPDX-License-Identifier: Apache-2.0 */
/*
 * RDMA Bandwidth Benchmark
 *
 * Measures RDMA Write/Read bandwidth with different message sizes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include "libcxl/compat/verbs.h"

#define MAX_SIZE (4 * 1024 * 1024)  /* 4MB */
#define ITERATIONS 1000

struct benchmark_context {
	struct ibv_context *ctx;
	struct ibv_pd *pd;
	struct ibv_mr *mr;
	struct ibv_cq *cq;
	struct ibv_qp *qp;
	char *buffer;
};

static double get_time_usec(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (double)tv.tv_sec * 1000000.0 + (double)tv.tv_usec;
}

static int setup_benchmark(struct ibv_device *dev, struct benchmark_context *bctx)
{
	struct ibv_qp_init_attr qp_init_attr;
	struct ibv_qp_attr qp_attr;

	bctx->ctx = ibv_open_device(dev);
	if (!bctx->ctx) {
		fprintf(stderr, "Failed to open device\n");
		return -1;
	}

	bctx->pd = ibv_alloc_pd(bctx->ctx);
	if (!bctx->pd) {
		fprintf(stderr, "Failed to allocate PD\n");
		return -1;
	}

	bctx->buffer = malloc(MAX_SIZE);
	if (!bctx->buffer) {
		fprintf(stderr, "Failed to allocate buffer\n");
		return -1;
	}
	memset(bctx->buffer, 0xAA, MAX_SIZE);

	bctx->mr = ibv_reg_mr(bctx->pd, bctx->buffer, MAX_SIZE,
			      IBV_ACCESS_LOCAL_WRITE |
			      IBV_ACCESS_REMOTE_WRITE |
			      IBV_ACCESS_REMOTE_READ);
	if (!bctx->mr) {
		fprintf(stderr, "Failed to register MR\n");
		return -1;
	}

	bctx->cq = ibv_create_cq(bctx->ctx, 1000, NULL, NULL, 0);
	if (!bctx->cq) {
		fprintf(stderr, "Failed to create CQ\n");
		return -1;
	}

	memset(&qp_init_attr, 0, sizeof(qp_init_attr));
	qp_init_attr.send_cq = bctx->cq;
	qp_init_attr.recv_cq = bctx->cq;
	qp_init_attr.qp_type = IBV_QPT_RC;
	qp_init_attr.cap.max_send_wr = 100;
	qp_init_attr.cap.max_recv_wr = 100;
	qp_init_attr.cap.max_send_sge = 1;
	qp_init_attr.cap.max_recv_sge = 1;

	bctx->qp = ibv_create_qp(bctx->pd, &qp_init_attr);
	if (!bctx->qp) {
		fprintf(stderr, "Failed to create QP\n");
		return -1;
	}

	/* Transition to RTS */
	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IBV_QPS_INIT;
	qp_attr.port_num = 1;
	qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
	ibv_modify_qp(bctx->qp, &qp_attr, (1 << 0) | (1 << 11) | (1 << 10));

	qp_attr.qp_state = IBV_QPS_RTR;
	qp_attr.path_mtu = IBV_MTU_4096;
	qp_attr.dest_qp_num = bctx->qp->qp_num;
	qp_attr.rq_psn = 0;
	ibv_modify_qp(bctx->qp, &qp_attr, (1 << 0) | (1 << 5) | (1 << 9) | (1 << 7));

	qp_attr.qp_state = IBV_QPS_RTS;
	qp_attr.sq_psn = 0;
	ibv_modify_qp(bctx->qp, &qp_attr, (1 << 0) | (1 << 8));

	return 0;
}

static void cleanup_benchmark(struct benchmark_context *bctx)
{
	if (bctx->qp) ibv_destroy_qp(bctx->qp);
	if (bctx->cq) ibv_destroy_cq(bctx->cq);
	if (bctx->mr) ibv_dereg_mr(bctx->mr);
	if (bctx->buffer) free(bctx->buffer);
	if (bctx->pd) ibv_dealloc_pd(bctx->pd);
	if (bctx->ctx) ibv_close_device(bctx->ctx);
}

static double benchmark_write(struct benchmark_context *bctx, size_t size, int iters)
{
	struct ibv_send_wr wr, *bad_wr;
	struct ibv_sge sge;
	struct ibv_wc wc;
	double start, end;
	int i, n;

	/* Warmup */
	for (i = 0; i < 10; i++) {
		memset(&sge, 0, sizeof(sge));
		sge.addr = (uint64_t)bctx->buffer;
		sge.length = size;
		sge.lkey = bctx->mr->lkey;

		memset(&wr, 0, sizeof(wr));
		wr.wr_id = i;
		wr.sg_list = &sge;
		wr.num_sge = 1;
		wr.opcode = IBV_WR_RDMA_WRITE;
		wr.send_flags = IBV_SEND_SIGNALED;
		wr.wr.rdma.remote_addr = (uint64_t)bctx->buffer + 1024;
		wr.wr.rdma.rkey = bctx->mr->rkey;

		ibv_post_send(bctx->qp, &wr, &bad_wr);
		do {
			n = ibv_poll_cq(bctx->cq, 1, &wc);
		} while (n == 0);
	}

	/* Benchmark */
	start = get_time_usec();

	for (i = 0; i < iters; i++) {
		memset(&sge, 0, sizeof(sge));
		sge.addr = (uint64_t)bctx->buffer;
		sge.length = size;
		sge.lkey = bctx->mr->lkey;

		memset(&wr, 0, sizeof(wr));
		wr.wr_id = i;
		wr.sg_list = &sge;
		wr.num_sge = 1;
		wr.opcode = IBV_WR_RDMA_WRITE;
		wr.send_flags = IBV_SEND_SIGNALED;
		wr.wr.rdma.remote_addr = (uint64_t)bctx->buffer + 1024;
		wr.wr.rdma.rkey = bctx->mr->rkey;

		if (ibv_post_send(bctx->qp, &wr, &bad_wr) != 0) {
			return -1;
		}

		do {
			n = ibv_poll_cq(bctx->cq, 1, &wc);
		} while (n == 0);

		if (wc.status != IBV_WC_SUCCESS) {
			return -1;
		}
	}

	end = get_time_usec();

	return end - start;
}

static double benchmark_read(struct benchmark_context *bctx, size_t size, int iters)
{
	struct ibv_send_wr wr, *bad_wr;
	struct ibv_sge sge;
	struct ibv_wc wc;
	double start, end;
	int i, n;

	/* Warmup */
	for (i = 0; i < 10; i++) {
		memset(&sge, 0, sizeof(sge));
		sge.addr = (uint64_t)bctx->buffer;
		sge.length = size;
		sge.lkey = bctx->mr->lkey;

		memset(&wr, 0, sizeof(wr));
		wr.wr_id = i;
		wr.sg_list = &sge;
		wr.num_sge = 1;
		wr.opcode = IBV_WR_RDMA_READ;
		wr.send_flags = IBV_SEND_SIGNALED;
		wr.wr.rdma.remote_addr = (uint64_t)bctx->buffer + 1024;
		wr.wr.rdma.rkey = bctx->mr->rkey;

		ibv_post_send(bctx->qp, &wr, &bad_wr);
		do {
			n = ibv_poll_cq(bctx->cq, 1, &wc);
		} while (n == 0);
	}

	/* Benchmark */
	start = get_time_usec();

	for (i = 0; i < iters; i++) {
		memset(&sge, 0, sizeof(sge));
		sge.addr = (uint64_t)bctx->buffer;
		sge.length = size;
		sge.lkey = bctx->mr->lkey;

		memset(&wr, 0, sizeof(wr));
		wr.wr_id = i;
		wr.sg_list = &sge;
		wr.num_sge = 1;
		wr.opcode = IBV_WR_RDMA_READ;
		wr.send_flags = IBV_SEND_SIGNALED;
		wr.wr.rdma.remote_addr = (uint64_t)bctx->buffer + 1024;
		wr.wr.rdma.rkey = bctx->mr->rkey;

		if (ibv_post_send(bctx->qp, &wr, &bad_wr) != 0) {
			return -1;
		}

		do {
			n = ibv_poll_cq(bctx->cq, 1, &wc);
		} while (n == 0);

		if (wc.status != IBV_WC_SUCCESS) {
			return -1;
		}
	}

	end = get_time_usec();

	return end - start;
}

static void print_results(const char *operation, size_t size, int iters, double time_usec)
{
	double bytes_total = (double)size * iters;
	double seconds = time_usec / 1000000.0;
	double bandwidth_mbps = (bytes_total / seconds) / (1024 * 1024);
	double latency_usec = time_usec / iters;

	printf("%-12s %8zu bytes: %8.2f MB/s, %8.2f us/op\n",
	       operation, size, bandwidth_mbps, latency_usec);
}

int main(void)
{
	struct ibv_device **dev_list;
	struct benchmark_context bctx;
	int num_devices;
	size_t sizes[] = {64, 256, 1024, 4096, 16384, 65536, 262144, 1048576};
	int num_sizes = sizeof(sizes) / sizeof(sizes[0]);
	int i, iters;
	double time_usec;

	printf("======================================\n");
	printf("RDMA Bandwidth Benchmark\n");
	printf("======================================\n\n");

	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list || num_devices == 0) {
		printf("No CXL devices found\n");
		return 0;
	}

	printf("Device: %s\n", ibv_get_device_name(dev_list[0]));
	printf("Max buffer: %d MB\n\n", MAX_SIZE / (1024 * 1024));

	memset(&bctx, 0, sizeof(bctx));
	if (setup_benchmark(dev_list[0], &bctx) != 0) {
		fprintf(stderr, "Failed to setup benchmark\n");
		ibv_free_device_list(dev_list);
		return 1;
	}

	printf("RDMA Write Bandwidth:\n");
	printf("=====================================\n");
	for (i = 0; i < num_sizes; i++) {
		/* Fewer iterations for large sizes */
		iters = (sizes[i] < 65536) ? ITERATIONS : (ITERATIONS / 10);

		time_usec = benchmark_write(&bctx, sizes[i], iters);
		if (time_usec < 0) {
			fprintf(stderr, "Benchmark failed\n");
			cleanup_benchmark(&bctx);
			ibv_free_device_list(dev_list);
			return 1;
		}
		print_results("Write", sizes[i], iters, time_usec);
	}

	printf("\nRDMA Read Bandwidth:\n");
	printf("=====================================\n");
	for (i = 0; i < num_sizes; i++) {
		iters = (sizes[i] < 65536) ? ITERATIONS : (ITERATIONS / 10);

		time_usec = benchmark_read(&bctx, sizes[i], iters);
		if (time_usec < 0) {
			fprintf(stderr, "Benchmark failed\n");
			cleanup_benchmark(&bctx);
			ibv_free_device_list(dev_list);
			return 1;
		}
		print_results("Read", sizes[i], iters, time_usec);
	}

	printf("\n======================================\n");
	printf("Benchmark completed successfully\n");
	printf("======================================\n");

	cleanup_benchmark(&bctx);
	ibv_free_device_list(dev_list);

	return 0;
}
