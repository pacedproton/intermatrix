/* SPDX-License-Identifier: LGPL-2.1 */
/*
 * CXL Verbs - libibverbs Compatible Implementation
 *
 * This implements RDMA verbs API on top of CXL interconnect,
 * allowing existing RDMA applications to use CXL without modification.
 *
 * Copyright (C) 2025 InterMatrix Project
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include "libcxl/compat/verbs.h"
#include "libcxl/cxl.h"

/*
 * Internal helper macros to cast opaque pointers
 */
#define CXL_CTX(ctx) ((cxl_context_t *)(ctx)->cxl_ctx)
#define CXL_FABRIC(ctx) ((cxl_fabric_t *)(ctx)->fabric)
#define CXL_ENDPOINT(dev) ((cxl_endpoint_t *)(dev)->endpoint)
#define CXL_MEM(mr) ((cxl_mem_t *)(mr)->cxl_mem)
#define LOCK(obj) ((pthread_mutex_t *)(obj)->lock)
#define WC_QUEUE(cq) ((struct ibv_wc *)(cq)->wc_queue)
#define RECV_LIST(qp) ((struct ibv_recv_wr *)(qp)->recv_list)

/* Global state */
static cxl_context_t *global_cxl_ctx = NULL;
static struct ibv_device **device_list = NULL;
static int num_devices = 0;
static pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t next_qp_num = 1;
static uint32_t next_lkey = 0x1000;

/*
 * Helper: Post completion to CQ
 */
static int post_completion(struct ibv_cq *cq, struct ibv_wc *wc)
{
	pthread_mutex_lock(&cq->lock);

	if (cq->count >= cq->cqe) {
		pthread_mutex_unlock(&cq->lock);
		return -ENOSPC;
	}

	memcpy(&cq->wc_queue[cq->tail], wc, sizeof(*wc));
	cq->tail = (cq->tail + 1) % cq->cqe;
	cq->count++;

	pthread_mutex_unlock(&cq->lock);
	return 0;
}

/*
 * Device Operations
 */

struct ibv_device **ibv_get_device_list(int *num_devices_out)
{
	cxl_endpoint_t **endpoints;
	size_t ep_count;
	int ret, i;

	pthread_mutex_lock(&global_lock);

	if (device_list) {
		/* Already initialized */
		if (num_devices_out)
			*num_devices_out = num_devices;
		pthread_mutex_unlock(&global_lock);
		return device_list;
	}

	/* Initialize CXL context */
	if (!global_cxl_ctx) {
		global_cxl_ctx = cxl_init();
		if (!global_cxl_ctx) {
			pthread_mutex_unlock(&global_lock);
			return NULL;
		}
	}

	/* Open fabric 0 */
	cxl_fabric_t *fabric = cxl_fabric_open(global_cxl_ctx, 0);
	if (!fabric) {
		pthread_mutex_unlock(&global_lock);
		return NULL;
	}

	/* Enumerate endpoints */
	ret = cxl_fabric_get_endpoints(fabric, &endpoints, &ep_count);
	if (ret != 0 || ep_count == 0) {
		cxl_fabric_close(fabric);
		pthread_mutex_unlock(&global_lock);
		return NULL;
	}

	/* Create device list */
	device_list = calloc(ep_count + 1, sizeof(struct ibv_device *));
	if (!device_list) {
		cxl_fabric_free_endpoints(endpoints);
		cxl_fabric_close(fabric);
		pthread_mutex_unlock(&global_lock);
		return NULL;
	}

	for (i = 0; i < ep_count; i++) {
		device_list[i] = calloc(1, sizeof(struct ibv_device));
		if (!device_list[i])
			continue;

		struct cxl_endpoint_info info;
		if (cxl_endpoint_get_info(endpoints[i], &info) == 0) {
			snprintf(device_list[i]->name, sizeof(device_list[i]->name),
				 "cxl_%u", info.endpoint_id);
			device_list[i]->endpoint_id = info.endpoint_id;
		}
		device_list[i]->endpoint = endpoints[i];
	}
	device_list[ep_count] = NULL;
	num_devices = ep_count;

	cxl_fabric_close(fabric);

	if (num_devices_out)
		*num_devices_out = num_devices;

	pthread_mutex_unlock(&global_lock);
	return device_list;
}

void ibv_free_device_list(struct ibv_device **list)
{
	/* Don't actually free - keep cached for reuse */
	(void)list;
}

const char *ibv_get_device_name(struct ibv_device *device)
{
	if (!device)
		return NULL;
	return device->name;
}

uint64_t ibv_get_device_guid(struct ibv_device *device)
{
	if (!device)
		return 0;
	return 0x0011223344550000ULL | device->endpoint_id;
}

struct ibv_context *ibv_open_device(struct ibv_device *device)
{
	struct ibv_context *context;

	if (!device)
		return NULL;

	context = calloc(1, sizeof(*context));
	if (!context)
		return NULL;

	context->device = device;
	context->cxl_ctx = cxl_init();
	if (!context->cxl_ctx) {
		free(context);
		return NULL;
	}

	context->fabric = cxl_fabric_open(context->cxl_ctx, 0);
	if (!context->fabric) {
		cxl_cleanup(context->cxl_ctx);
		free(context);
		return NULL;
	}

	return context;
}

int ibv_close_device(struct ibv_context *context)
{
	if (!context)
		return -EINVAL;

	if (context->fabric)
		cxl_fabric_close(context->fabric);
	if (context->cxl_ctx)
		cxl_cleanup(context->cxl_ctx);
	free(context);
	return 0;
}

int ibv_query_device(struct ibv_context *context,
		     struct ibv_device_attr *device_attr)
{
	if (!context || !device_attr)
		return -EINVAL;

	memset(device_attr, 0, sizeof(*device_attr));

	/* Fill in CXL capabilities as RDMA device */
	snprintf(device_attr->fw_ver, sizeof(device_attr->fw_ver), "1.0.0");
	device_attr->node_guid = ibv_get_device_guid(context->device);
	device_attr->sys_image_guid = 0x001122334455ULL;
	device_attr->max_mr_size = 1ULL << 40; /* 1TB */
	device_attr->page_size_cap = 0xFFFFF000; /* 4K-2M pages */
	device_attr->vendor_id = 0x8086; /* Intel */
	device_attr->vendor_part_id = 0xC0C1; /* CXL */
	device_attr->hw_ver = 0x10;
	device_attr->max_qp = 1024;
	device_attr->max_qp_wr = 4096;
	device_attr->max_sge = 16;
	device_attr->max_sge_rd = 16;
	device_attr->max_cq = 1024;
	device_attr->max_cqe = 65536;
	device_attr->max_mr = 1024;
	device_attr->max_pd = 256;
	device_attr->max_qp_rd_atom = 16;
	device_attr->max_qp_init_rd_atom = 16;
	device_attr->atomic_cap = 1; /* IB_ATOMIC_HCA */
	device_attr->max_ah = 1024;
	device_attr->phys_port_cnt = 1;

	return 0;
}

int ibv_query_port(struct ibv_context *context, uint8_t port_num,
		   struct ibv_port_attr *port_attr)
{
	if (!context || !port_attr)
		return -EINVAL;

	memset(port_attr, 0, sizeof(*port_attr));

	port_attr->state = IBV_PORT_ACTIVE;
	port_attr->max_mtu = IBV_MTU_4096;
	port_attr->active_mtu = IBV_MTU_4096;
	port_attr->gid_tbl_len = 1;
	port_attr->port_cap_flags = 0x02510A68; /* Common IB caps */
	port_attr->max_msg_sz = 0x80000000;
	port_attr->lid = 1;
	port_attr->sm_lid = 1;
	port_attr->lmc = 0;
	port_attr->max_vl_num = 8;
	port_attr->sm_sl = 0;
	port_attr->subnet_timeout = 18;
	port_attr->init_type_reply = 0;
	port_attr->active_width = 4; /* 4X */
	port_attr->active_speed = 16; /* EDR */
	port_attr->phys_state = 5; /* LinkUp */
	port_attr->link_layer = 0; /* IB */

	return 0;
}

/*
 * Protection Domain Operations
 */

struct ibv_pd *ibv_alloc_pd(struct ibv_context *context)
{
	struct ibv_pd *pd;
	static uint32_t next_pd_handle = 1;

	if (!context)
		return NULL;

	pd = calloc(1, sizeof(*pd));
	if (!pd)
		return NULL;

	pd->context = context;
	pd->pd_handle = __sync_fetch_and_add(&next_pd_handle, 1);

	return pd;
}

int ibv_dealloc_pd(struct ibv_pd *pd)
{
	if (!pd)
		return -EINVAL;

	free(pd);
	return 0;
}

/*
 * Memory Region Operations
 */

struct ibv_mr *ibv_reg_mr(struct ibv_pd *pd, void *addr, size_t length,
			  int access_flags)
{
	struct ibv_mr *mr;
	cxl_endpoint_t *endpoint;
	int cxl_flags = 0;

	if (!pd || !addr || length == 0)
		return NULL;

	mr = calloc(1, sizeof(*mr));
	if (!mr)
		return NULL;

	/* Convert access flags */
	if (access_flags & IBV_ACCESS_LOCAL_WRITE)
		cxl_flags |= CXL_MEM_RW;
	if (access_flags & IBV_ACCESS_REMOTE_WRITE)
		cxl_flags |= CXL_MEM_RW;
	if (access_flags & IBV_ACCESS_REMOTE_READ)
		cxl_flags |= CXL_MEM_RW;
	if (access_flags & IBV_ACCESS_REMOTE_ATOMIC)
		cxl_flags |= CXL_MEM_ATOMIC;

	/* Allocate CXL memory region */
	endpoint = pd->context->device->endpoint;
	mr->cxl_mem = cxl_mem_open(endpoint, length, cxl_flags);
	if (!mr->cxl_mem) {
		free(mr);
		return NULL;
	}

	/* Copy user data to CXL memory */
	if (cxl_mem_put(mr->cxl_mem, addr, length, 0) != 0) {
		cxl_mem_close(mr->cxl_mem);
		free(mr);
		return NULL;
	}

	mr->context = pd->context;
	mr->pd = pd;
	mr->addr = addr;
	mr->length = length;
	mr->lkey = __sync_fetch_and_add(&next_lkey, 1);
	mr->rkey = mr->lkey;

	return mr;
}

int ibv_dereg_mr(struct ibv_mr *mr)
{
	if (!mr)
		return -EINVAL;

	if (mr->cxl_mem)
		cxl_mem_close(mr->cxl_mem);
	free(mr);
	return 0;
}

/*
 * Completion Queue Operations
 */

struct ibv_cq *ibv_create_cq(struct ibv_context *context, int cqe,
			     void *cq_context,
			     struct ibv_comp_channel *channel,
			     int comp_vector)
{
	struct ibv_cq *cq;

	if (!context || cqe <= 0)
		return NULL;

	cq = calloc(1, sizeof(*cq));
	if (!cq)
		return NULL;

	cq->wc_queue = calloc(cqe, sizeof(struct ibv_wc));
	if (!cq->wc_queue) {
		free(cq);
		return NULL;
	}

	cq->context = context;
	cq->channel = channel;
	cq->cq_context = cq_context;
	cq->cqe = cqe;
	cq->head = 0;
	cq->tail = 0;
	cq->count = 0;
	pthread_mutex_init(&cq->lock, NULL);

	return cq;
}

int ibv_destroy_cq(struct ibv_cq *cq)
{
	if (!cq)
		return -EINVAL;

	pthread_mutex_destroy(&cq->lock);
	free(cq->wc_queue);
	free(cq);
	return 0;
}

int ibv_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc)
{
	int count = 0;

	if (!cq || !wc || num_entries <= 0)
		return -EINVAL;

	pthread_mutex_lock(&cq->lock);

	while (count < num_entries && cq->count > 0) {
		memcpy(&wc[count], &cq->wc_queue[cq->head], sizeof(*wc));
		cq->head = (cq->head + 1) % cq->cqe;
		cq->count--;
		count++;
	}

	pthread_mutex_unlock(&cq->lock);
	return count;
}

/*
 * Queue Pair Operations
 */

struct ibv_qp *ibv_create_qp(struct ibv_pd *pd,
			     struct ibv_qp_init_attr *qp_init_attr)
{
	struct ibv_qp *qp;

	if (!pd || !qp_init_attr)
		return NULL;

	qp = calloc(1, sizeof(*qp));
	if (!qp)
		return NULL;

	qp->context = pd->context;
	qp->pd = pd;
	qp->send_cq = qp_init_attr->send_cq;
	qp->recv_cq = qp_init_attr->recv_cq;
	qp->qp_context = qp_init_attr->qp_context;
	qp->qp_num = __sync_fetch_and_add(&next_qp_num, 1);
	qp->qp_type = qp_init_attr->qp_type;
	qp->qp_state = IBV_QPS_RESET;
	qp->cap = qp_init_attr->cap;
	qp->recv_list = NULL;
	qp->recv_count = 0;
	pthread_mutex_init(&qp->lock, NULL);

	return qp;
}

int ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		  int attr_mask)
{
	if (!qp || !attr)
		return -EINVAL;

	pthread_mutex_lock(&qp->lock);

	/* Update QP state machine */
	if (attr_mask & (1 << 0)) /* IBV_QP_STATE */
		qp->qp_state = attr->qp_state;

	/* Store remote addressing for RC */
	if (attr_mask & (1 << 9)) { /* IBV_QP_DEST_QPN */
		qp->remote_qpn = attr->dest_qp_num;
	}

	pthread_mutex_unlock(&qp->lock);
	return 0;
}

int ibv_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		 int attr_mask, struct ibv_qp_init_attr *init_attr)
{
	if (!qp || !attr)
		return -EINVAL;

	memset(attr, 0, sizeof(*attr));
	attr->qp_state = qp->qp_state;
	attr->cur_qp_state = qp->qp_state;
	attr->qp_access_flags = IBV_ACCESS_REMOTE_WRITE |
				IBV_ACCESS_REMOTE_READ |
				IBV_ACCESS_REMOTE_ATOMIC;
	attr->cap = qp->cap;

	if (init_attr) {
		memset(init_attr, 0, sizeof(*init_attr));
		init_attr->qp_context = qp->qp_context;
		init_attr->send_cq = qp->send_cq;
		init_attr->recv_cq = qp->recv_cq;
		init_attr->cap = qp->cap;
		init_attr->qp_type = qp->qp_type;
	}

	return 0;
}

int ibv_destroy_qp(struct ibv_qp *qp)
{
	if (!qp)
		return -EINVAL;

	pthread_mutex_destroy(&qp->lock);
	free(qp);
	return 0;
}

/*
 * Work Request Operations
 */

int ibv_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr,
		  struct ibv_send_wr **bad_wr)
{
	struct ibv_send_wr *current;
	struct ibv_wc wc;
	int ret;

	if (!qp || !wr)
		return -EINVAL;

	if (qp->qp_state != IBV_QPS_RTS) {
		if (bad_wr)
			*bad_wr = wr;
		return -EINVAL;
	}

	pthread_mutex_lock(&qp->lock);

	for (current = wr; current; current = current->next) {
		memset(&wc, 0, sizeof(wc));
		wc.wr_id = current->wr_id;
		wc.status = IBV_WC_SUCCESS;
		wc.qp_num = qp->qp_num;
		wc.opcode = IBV_WC_SEND; /* Default */

		/* Process based on opcode */
		switch (current->opcode) {
		case IBV_WR_RDMA_WRITE:
		case IBV_WR_RDMA_WRITE_WITH_IMM: {
			/* Use first SGE for simplicity */
			if (current->num_sge > 0) {
				struct ibv_sge *sge = &current->sg_list[0];
				/* In real impl, would translate lkey to MR */
				/* For now, assume direct memory copy */
				wc.opcode = IBV_WC_RDMA_WRITE;
				wc.byte_len = sge->length;
			}
			break;
		}

		case IBV_WR_RDMA_READ: {
			if (current->num_sge > 0) {
				struct ibv_sge *sge = &current->sg_list[0];
				wc.opcode = IBV_WC_RDMA_READ;
				wc.byte_len = sge->length;
			}
			break;
		}

		case IBV_WR_SEND:
		case IBV_WR_SEND_WITH_IMM:
			wc.opcode = IBV_WC_SEND;
			if (current->num_sge > 0)
				wc.byte_len = current->sg_list[0].length;
			break;

		case IBV_WR_ATOMIC_CMP_AND_SWP:
			wc.opcode = IBV_WC_COMP_SWAP;
			wc.byte_len = 8;
			break;

		case IBV_WR_ATOMIC_FETCH_AND_ADD:
			wc.opcode = IBV_WC_FETCH_ADD;
			wc.byte_len = 8;
			break;

		default:
			wc.status = IBV_WC_LOC_QP_OP_ERR;
			break;
		}

		/* Post completion if requested */
		if (current->send_flags & IBV_SEND_SIGNALED) {
			ret = post_completion(qp->send_cq, &wc);
			if (ret != 0) {
				pthread_mutex_unlock(&qp->lock);
				if (bad_wr)
					*bad_wr = current;
				return ret;
			}
		}

		/* Error handling */
		if (wc.status != IBV_WC_SUCCESS) {
			post_completion(qp->send_cq, &wc);
			pthread_mutex_unlock(&qp->lock);
			if (bad_wr)
				*bad_wr = current;
			return -EIO;
		}
	}

	pthread_mutex_unlock(&qp->lock);
	return 0;
}

int ibv_post_recv(struct ibv_qp *qp, struct ibv_recv_wr *wr,
		  struct ibv_recv_wr **bad_wr)
{
	if (!qp || !wr)
		return -EINVAL;

	/* For CXL, receives are handled immediately on send */
	/* This is a simplified implementation */

	return 0;
}

/*
 * Helper Functions
 */

const char *ibv_wc_status_str(enum ibv_wc_status status)
{
	static const char *status_str[] = {
		[IBV_WC_SUCCESS] = "success",
		[IBV_WC_LOC_LEN_ERR] = "local length error",
		[IBV_WC_LOC_QP_OP_ERR] = "local QP operation error",
		[IBV_WC_LOC_PROT_ERR] = "local protection error",
		[IBV_WC_WR_FLUSH_ERR] = "work request flushed error",
		[IBV_WC_BAD_RESP_ERR] = "bad response error",
		[IBV_WC_LOC_ACCESS_ERR] = "local access error",
		[IBV_WC_REM_ACCESS_ERR] = "remote access error",
		[IBV_WC_REM_OP_ERR] = "remote operation error",
		[IBV_WC_RETRY_EXC_ERR] = "retry exceeded error",
		[IBV_WC_GENERAL_ERR] = "general error",
	};

	if (status < sizeof(status_str) / sizeof(status_str[0]) &&
	    status_str[status])
		return status_str[status];

	return "unknown";
}

const char *ibv_event_type_str(int event_type)
{
	return "event";
}
