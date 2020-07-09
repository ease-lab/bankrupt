//MIT License
//
//Copyright (c) 2020 Edinburgh Architecture and Systems (EASE) Lab @ University of Edinburgh
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:
//
//The above copyright notice and this permission notice shall be included in all
//copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//SOFTWARE.    

#define _GNU_SOURCE
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/time.h>
#include "time_rdtsc.h"

#define TEST_NZ(x) do { if ( (x)) {die("error: " #x " failed (returned non-zero)." );} } while (0)
#define TEST_Z(x)  do { if (!(x)) {die("error: " #x " failed (returned zero/null).");} } while (0)

#ifndef MESSAGE_SIZE
#define MESSAGE_SIZE 64
#endif
#ifndef MEASUREMENT_THREAD
#define MEASUREMENT_THREAD 2
#endif

extern int num_outstanding_reads;
extern struct context **s_ctx;
extern struct connection **connections;
extern int num_qps;
extern int cur_qps;
extern int num_outstanding_reads_per_qp;
struct ibv_cq *cq = NULL;
struct ibv_comp_channel *comp_channel = NULL;
struct ibv_pd *pd = NULL;

struct context {
  struct ibv_context *ctx;
  struct ibv_pd *pd;
};

struct connection {
  	struct rdma_cm_id *id;
  	struct ibv_qp *qp;

  	struct ibv_mr *bank_regions;
	struct ibv_mr *memory_registers_region;
    struct ibv_mr **bank_region_pointers; // for deregistering

  	int connected;
};

void die(const char *reason);

void build_connection(struct rdma_cm_id *id);
void build_params(struct rdma_conn_param *params);
void on_connect(void *context);
int on_region_exchange_completion(enum ibv_wc_opcode opcode);
void post_receive_same_bank_regions();
void destroy_client_connection(int num_qps, int num_outstanding_reads_per_qp);
inline long time_us(void);
inline void spin(long usecs);
static void build_context(struct ibv_context *verbs);
static void build_qp_attr(struct ibv_qp_init_attr *qp_attr);
static void register_common_memory(struct connection *conn);


void die(const char *reason) {
    fprintf(stderr, "%s\n", reason);
    exit(EXIT_FAILURE);
}


void build_connection(struct rdma_cm_id *id) {
    TEST_Z(cur_qps < num_qps);

    struct ibv_qp_init_attr qp_attr;

    build_context(id->verbs);
    build_qp_attr(&qp_attr);

    TEST_NZ(rdma_create_qp(id, s_ctx[cur_qps]->pd, &qp_attr));

    id->context = connections[cur_qps];

    connections[cur_qps]->id = id;
    connections[cur_qps]->qp = id->qp;

    connections[cur_qps]->connected = 0;
}

void build_context(struct ibv_context *verbs) {
    struct ibv_device_attr device_attr;
    if (s_ctx[cur_qps]) {
        if (s_ctx[cur_qps]->ctx != verbs)
        die("cannot handle events in more than one context.");
        return;
    }

    s_ctx[cur_qps] = (struct context *)malloc(sizeof(struct context));

    s_ctx[cur_qps]->ctx = verbs;

    if (!pd) {
        TEST_Z(pd = ibv_alloc_pd(s_ctx[cur_qps]->ctx));
    }
    s_ctx[cur_qps]->pd = pd;

    if (!comp_channel) { 
        TEST_Z(comp_channel = ibv_create_comp_channel(s_ctx[cur_qps]->ctx));
        TEST_Z(cq = ibv_create_cq(s_ctx[cur_qps]->ctx, 1000, NULL, comp_channel, 0));
    }
    TEST_NZ(ibv_req_notify_cq(cq, 0));
}

void build_params(struct rdma_conn_param *params) {
    memset(params, 0, sizeof(*params));

    params->initiator_depth = params->responder_resources = 16;
    params->rnr_retry_count = 7;
}

void build_qp_attr(struct ibv_qp_init_attr *qp_attr) {
    memset(qp_attr, 0, sizeof(*qp_attr));

    qp_attr->send_cq = cq;
    qp_attr->recv_cq = cq;
    qp_attr->qp_type = IBV_QPT_RC;

    qp_attr->cap.max_send_wr = 100;
    qp_attr->cap.max_recv_wr = 100;
    qp_attr->cap.max_send_sge = 1;
    qp_attr->cap.max_recv_sge = 1;
}

int on_region_exchange_completion(enum ibv_wc_opcode opcode) {
    struct ibv_wc wc;

    while (!ibv_poll_cq(cq, 1, &wc)) {};
    if (wc.status != IBV_WC_SUCCESS)
        die("on_completion: status is not IBV_WC_SUCCESS.");
    if (wc.opcode != opcode) {
        printf("oncompletion: wrong opcode\n");
        return 1;
    } else {
        //printf("region exchange done\n");
        return 0;
    }
}

void on_connect(void *context) {
    ((struct connection *)context)->connected = 1;
}

void post_receive_same_bank_regions() {
    struct connection *conn = connections[0];
    struct ibv_recv_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = (uintptr_t)conn;
    wr.next = NULL;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    sge.addr = (uintptr_t)conn->bank_regions;
    sge.length =    sizeof(struct ibv_mr);
    sge.lkey = conn->memory_registers_region->lkey;

    TEST_NZ(ibv_post_recv(conn->qp, &wr, &bad_wr));
}

void register_common_memory(struct connection *conn) {
    if (cur_qps != 0) return;

    connections[cur_qps]->bank_regions = malloc(sizeof(struct ibv_mr));

    // Register array of MR's
    TEST_Z(connections[cur_qps]->memory_registers_region = ibv_reg_mr (
        s_ctx[cur_qps]->pd,
        connections[cur_qps]->bank_regions,
        sizeof(struct ibv_mr),
        IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE));
}


inline void spin(long usecs) {
    long start;
    start = time_us();
    while (time_us() < start + usecs)
        asm volatile("pause");
}

inline long time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000000 + (long)tv.tv_usec;
}
