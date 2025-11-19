/* SPDX-License-Identifier: LGPL-2.1 */
/*
 * CXL Verbs - libibverbs Compatible Interface
 *
 * This provides RDMA verbs API compatibility for CXL interconnect,
 * allowing existing RDMA applications to use CXL without modification.
 *
 * Copyright (C) 2025 InterMatrix Project
 */

#ifndef _LIBCXL_VERBS_H
#define _LIBCXL_VERBS_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations - some structs are opaque */
struct cxl_context;
struct cxl_fabric;
struct cxl_endpoint;
struct cxl_mem;

/* Enums - must be defined before structs that use them */
enum ibv_port_state {
	IBV_PORT_NOP		= 0,
	IBV_PORT_DOWN		= 1,
	IBV_PORT_INIT		= 2,
	IBV_PORT_ARMED		= 3,
	IBV_PORT_ACTIVE		= 4,
	IBV_PORT_ACTIVE_DEFER	= 5
};

enum ibv_mtu {
	IBV_MTU_256		= 1,
	IBV_MTU_512		= 2,
	IBV_MTU_1024		= 3,
	IBV_MTU_2048		= 4,
	IBV_MTU_4096		= 5
};

enum ibv_mig_state {
	IBV_MIG_MIGRATED,
	IBV_MIG_REARM,
	IBV_MIG_ARMED
};

enum ibv_access_flags {
	IBV_ACCESS_LOCAL_WRITE		= (1 << 0),
	IBV_ACCESS_REMOTE_WRITE		= (1 << 1),
	IBV_ACCESS_REMOTE_READ		= (1 << 2),
	IBV_ACCESS_REMOTE_ATOMIC	= (1 << 3),
	IBV_ACCESS_MW_BIND		= (1 << 4),
	IBV_ACCESS_ZERO_BASED		= (1 << 5),
	IBV_ACCESS_ON_DEMAND		= (1 << 6)
};

enum ibv_qp_type {
	IBV_QPT_RC = 2,
	IBV_QPT_UC,
	IBV_QPT_UD,
	IBV_QPT_RAW_PACKET = 8,
	IBV_QPT_XRC_SEND = 9,
	IBV_QPT_XRC_RECV
};

enum ibv_qp_state {
	IBV_QPS_RESET,
	IBV_QPS_INIT,
	IBV_QPS_RTR,
	IBV_QPS_RTS,
	IBV_QPS_SQD,
	IBV_QPS_SQE,
	IBV_QPS_ERR,
	IBV_QPS_UNKNOWN
};

enum ibv_wc_status {
	IBV_WC_SUCCESS,
	IBV_WC_LOC_LEN_ERR,
	IBV_WC_LOC_QP_OP_ERR,
	IBV_WC_LOC_EEC_OP_ERR,
	IBV_WC_LOC_PROT_ERR,
	IBV_WC_WR_FLUSH_ERR,
	IBV_WC_MW_BIND_ERR,
	IBV_WC_BAD_RESP_ERR,
	IBV_WC_LOC_ACCESS_ERR,
	IBV_WC_REM_INV_REQ_ERR,
	IBV_WC_REM_ACCESS_ERR,
	IBV_WC_REM_OP_ERR,
	IBV_WC_RETRY_EXC_ERR,
	IBV_WC_RNR_RETRY_EXC_ERR,
	IBV_WC_LOC_RDD_VIOL_ERR,
	IBV_WC_REM_INV_RD_REQ_ERR,
	IBV_WC_REM_ABORT_ERR,
	IBV_WC_INV_EECN_ERR,
	IBV_WC_INV_EEC_STATE_ERR,
	IBV_WC_FATAL_ERR,
	IBV_WC_RESP_TIMEOUT_ERR,
	IBV_WC_GENERAL_ERR
};

enum ibv_wc_opcode {
	IBV_WC_SEND,
	IBV_WC_RDMA_WRITE,
	IBV_WC_RDMA_READ,
	IBV_WC_COMP_SWAP,
	IBV_WC_FETCH_ADD,
	IBV_WC_BIND_MW,
	IBV_WC_LOCAL_INV,
	IBV_WC_RECV = 1 << 7,
	IBV_WC_RECV_RDMA_WITH_IMM
};

enum ibv_wr_opcode {
	IBV_WR_RDMA_WRITE,
	IBV_WR_RDMA_WRITE_WITH_IMM,
	IBV_WR_SEND,
	IBV_WR_SEND_WITH_IMM,
	IBV_WR_RDMA_READ,
	IBV_WR_ATOMIC_CMP_AND_SWP,
	IBV_WR_ATOMIC_FETCH_AND_ADD,
	IBV_WR_LOCAL_INV,
	IBV_WR_BIND_MW,
	IBV_WR_SEND_WITH_INV,
};

enum ibv_send_flags {
	IBV_SEND_FENCE		= 1 << 0,
	IBV_SEND_SIGNALED	= 1 << 1,
	IBV_SEND_SOLICITED	= 1 << 2,
	IBV_SEND_INLINE		= 1 << 3,
	IBV_SEND_IP_CSUM	= 1 << 4
};

/* Address Handle Attributes */
struct ibv_ah_attr {
	uint8_t dlid;
	uint8_t src_path_bits;
	uint8_t sl;
	uint8_t static_rate;
	uint8_t is_global;
	uint8_t port_num;
};

/* Queue Pair Capabilities */
struct ibv_qp_cap {
	uint32_t		max_send_wr;
	uint32_t		max_recv_wr;
	uint32_t		max_send_sge;
	uint32_t		max_recv_sge;
	uint32_t		max_inline_data;
};

/* Main structures */
struct ibv_device {
	char name[64];
	void *endpoint;
	uint32_t endpoint_id;
};

struct ibv_context {
	struct ibv_device *device;
	void *cxl_ctx;
	void *fabric;
};

struct ibv_pd {
	struct ibv_context *context;
	uint32_t pd_handle;
};

struct ibv_mr {
	struct ibv_context *context;
	struct ibv_pd *pd;
	void *addr;
	size_t length;
	void *cxl_mem;
	uint32_t lkey;
	uint32_t rkey;
};

struct ibv_comp_channel {
	struct ibv_context *context;
	int fd;
};

struct ibv_cq {
	struct ibv_context *context;
	struct ibv_comp_channel *channel;
	void *cq_context;
	int cqe;
	void *lock;
	void *wc_queue;
	int head;
	int tail;
	int count;
};

struct ibv_srq {
	struct ibv_context *context;
	struct ibv_pd *pd;
};

struct ibv_qp {
	struct ibv_context *context;
	struct ibv_pd *pd;
	struct ibv_cq *send_cq;
	struct ibv_cq *recv_cq;
	void *qp_context;
	uint32_t qp_num;
	enum ibv_qp_type qp_type;
	enum ibv_qp_state qp_state;
	struct ibv_qp_cap cap;
	void *lock;
	void *recv_list;
	int recv_count;
	uint64_t remote_addr;
	uint32_t remote_qpn;
	uint32_t rkey;
};

struct ibv_ah {
	struct ibv_context *context;
	struct ibv_pd *pd;
};

/* Device attributes */
struct ibv_device_attr {
	char			fw_ver[64];
	uint64_t		node_guid;
	uint64_t		sys_image_guid;
	uint64_t		max_mr_size;
	uint64_t		page_size_cap;
	uint32_t		vendor_id;
	uint32_t		vendor_part_id;
	uint32_t		hw_ver;
	int			max_qp;
	int			max_qp_wr;
	int			device_cap_flags;
	int			max_sge;
	int			max_sge_rd;
	int			max_cq;
	int			max_cqe;
	int			max_mr;
	int			max_pd;
	int			max_qp_rd_atom;
	int			max_ee_rd_atom;
	int			max_res_rd_atom;
	int			max_qp_init_rd_atom;
	int			max_ee_init_rd_atom;
	int			atomic_cap;
	int			max_ee;
	int			max_rdd;
	int			max_mw;
	int			max_raw_ipv6_qp;
	int			max_raw_ethy_qp;
	int			max_mcast_grp;
	int			max_mcast_qp_attach;
	int			max_total_mcast_qp_attach;
	int			max_ah;
	int			max_fmr;
	int			max_map_per_fmr;
	int			max_srq;
	int			max_srq_wr;
	int			max_srq_sge;
	uint16_t		max_pkeys;
	uint8_t			local_ca_ack_delay;
	int			phys_port_cnt;
};

/* Port attributes */
struct ibv_port_attr {
	enum ibv_port_state	state;
	enum ibv_mtu		max_mtu;
	enum ibv_mtu		active_mtu;
	int			gid_tbl_len;
	uint32_t		port_cap_flags;
	uint32_t		max_msg_sz;
	uint32_t		bad_pkey_cntr;
	uint32_t		qkey_viol_cntr;
	uint16_t		pkey_tbl_len;
	uint16_t		lid;
	uint16_t		sm_lid;
	uint8_t			lmc;
	uint8_t			max_vl_num;
	uint8_t			sm_sl;
	uint8_t			subnet_timeout;
	uint8_t			init_type_reply;
	uint8_t			active_width;
	uint8_t			active_speed;
	uint8_t			phys_state;
	uint8_t			link_layer;
};

/* Scatter/Gather Entry */
struct ibv_sge {
	uint64_t	addr;
	uint32_t	length;
	uint32_t	lkey;
};

/* Send Work Request */
struct ibv_send_wr {
	uint64_t		wr_id;
	struct ibv_send_wr     *next;
	struct ibv_sge	       *sg_list;
	int			num_sge;
	enum ibv_wr_opcode	opcode;
	int			send_flags;
	uint32_t		imm_data;	/* network byte order */
	union {
		struct {
			uint64_t	remote_addr;
			uint32_t	rkey;
		} rdma;
		struct {
			uint64_t	remote_addr;
			uint64_t	compare_add;
			uint64_t	swap;
			uint32_t	rkey;
		} atomic;
		struct {
			struct ibv_ah  *ah;
			uint32_t	remote_qpn;
			uint32_t	remote_qkey;
		} ud;
	} wr;
};

/* Receive Work Request */
struct ibv_recv_wr {
	uint64_t		wr_id;
	struct ibv_recv_wr     *next;
	struct ibv_sge	       *sg_list;
	int			num_sge;
};

/* Work Completion */
struct ibv_wc {
	uint64_t		wr_id;
	enum ibv_wc_status	status;
	enum ibv_wc_opcode	opcode;
	uint32_t		vendor_err;
	uint32_t		byte_len;
	uint32_t		imm_data;	/* network byte order */
	uint32_t		qp_num;
	uint32_t		src_qp;
	int			wc_flags;
	uint16_t		pkey_index;
	uint16_t		slid;
	uint8_t			sl;
	uint8_t			dlid_path_bits;
};

/* Queue Pair Attributes */
struct ibv_qp_attr {
	enum ibv_qp_state	qp_state;
	enum ibv_qp_state	cur_qp_state;
	enum ibv_mtu		path_mtu;
	enum ibv_mig_state	path_mig_state;
	uint32_t		qkey;
	uint32_t		rq_psn;
	uint32_t		sq_psn;
	uint32_t		dest_qp_num;
	int			qp_access_flags;
	struct ibv_qp_cap	cap;
	struct ibv_ah_attr	ah_attr;
	struct ibv_ah_attr	alt_ah_attr;
	uint16_t		pkey_index;
	uint16_t		alt_pkey_index;
	uint8_t			en_sqd_async_notify;
	uint8_t			sq_draining;
	uint8_t			max_rd_atomic;
	uint8_t			max_dest_rd_atomic;
	uint8_t			min_rnr_timer;
	uint8_t			port_num;
	uint8_t			timeout;
	uint8_t			retry_cnt;
	uint8_t			rnr_retry;
	uint8_t			alt_port_num;
	uint8_t			alt_timeout;
};

struct ibv_qp_init_attr {
	void		       *qp_context;
	struct ibv_cq	       *send_cq;
	struct ibv_cq	       *recv_cq;
	struct ibv_srq	       *srq;
	struct ibv_qp_cap	cap;
	enum ibv_qp_type	qp_type;
	int			sq_sig_all;
};

/*
 * Device Operations
 */

/* Get list of available devices */
struct ibv_device **ibv_get_device_list(int *num_devices);

/* Free device list */
void ibv_free_device_list(struct ibv_device **list);

/* Get device name */
const char *ibv_get_device_name(struct ibv_device *device);

/* Get device GUID */
uint64_t ibv_get_device_guid(struct ibv_device *device);

/* Open device */
struct ibv_context *ibv_open_device(struct ibv_device *device);

/* Close device */
int ibv_close_device(struct ibv_context *context);

/* Query device */
int ibv_query_device(struct ibv_context *context,
		     struct ibv_device_attr *device_attr);

/* Query port */
int ibv_query_port(struct ibv_context *context, uint8_t port_num,
		   struct ibv_port_attr *port_attr);

/*
 * Protection Domain Operations
 */

/* Allocate protection domain */
struct ibv_pd *ibv_alloc_pd(struct ibv_context *context);

/* Deallocate protection domain */
int ibv_dealloc_pd(struct ibv_pd *pd);

/*
 * Memory Region Operations
 */

/* Register memory region */
struct ibv_mr *ibv_reg_mr(struct ibv_pd *pd, void *addr, size_t length,
			  int access_flags);

/* Deregister memory region */
int ibv_dereg_mr(struct ibv_mr *mr);

/*
 * Completion Queue Operations
 */

/* Create completion queue */
struct ibv_cq *ibv_create_cq(struct ibv_context *context, int cqe,
			     void *cq_context,
			     struct ibv_comp_channel *channel,
			     int comp_vector);

/* Destroy completion queue */
int ibv_destroy_cq(struct ibv_cq *cq);

/* Poll completion queue */
int ibv_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc);

/*
 * Queue Pair Operations
 */

/* Create queue pair */
struct ibv_qp *ibv_create_qp(struct ibv_pd *pd,
			     struct ibv_qp_init_attr *qp_init_attr);

/* Modify queue pair */
int ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		  int attr_mask);

/* Query queue pair */
int ibv_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		 int attr_mask, struct ibv_qp_init_attr *init_attr);

/* Destroy queue pair */
int ibv_destroy_qp(struct ibv_qp *qp);

/* Post send work request */
int ibv_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr,
		  struct ibv_send_wr **bad_wr);

/* Post receive work request */
int ibv_post_recv(struct ibv_qp *qp, struct ibv_recv_wr *wr,
		  struct ibv_recv_wr **bad_wr);

/*
 * Helper Functions
 */

/* Convert WC status to string */
const char *ibv_wc_status_str(enum ibv_wc_status status);

/* Get async event description */
const char *ibv_event_type_str(int event_type);

#ifdef __cplusplus
}
#endif

#endif /* _LIBCXL_VERBS_H */
