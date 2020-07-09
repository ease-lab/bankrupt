// MIT License
// 
// Copyright (c) 2020 Edinburgh Architecture and Systems (EASE) Lab @ University of Edinburgh
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "rdma-common.h"
#include "ownExperimentC.h"
#define PROBE_INTERVAL 500
#define SAMPLES 1000000

const int TIMEOUT_IN_MS = 500; /* ms */

static int on_addr_resolved(struct rdma_cm_id *id);
static int on_connection(struct rdma_cm_id *id);
static int on_disconnect(struct rdma_cm_id *id);
static int on_event(struct rdma_cm_event *event);
static int on_route_resolved(struct rdma_cm_id *id);
static void register_client_memory(struct connection *conn);
struct ibv_send_wr *send_remote_reads(struct connection *conn, int index);
static void* wait_reads_complete();
static void populate_access_addresses();



struct ibv_send_wr **reads, **bad_send_wr = NULL;
int num_outstanding_reads;
int cur_qps = 0;
struct ibv_wc work_complete[10000];
double ticks_per_ns;
/* command line args */
int num_outstanding_reads_per_qp = 1;
int num_qps;
/*---------------- *

/* measurement */
uint64_t starts[SAMPLES];
uint64_t ends[SAMPLES];
size_t **client_access_addresses;
void **same_bank_addresses;

/* connection */
struct rdma_event_channel *ec = NULL;
struct addrinfo *addr;
struct rdma_cm_id **ids;
struct context **s_ctx;
struct connection **connections;
/* ---------------- */

int main(int argc, char **argv) {
    struct rdma_cm_event *event = NULL;
    
    if (argc != 3) exit(1);
    num_qps = atoi(argv[2]);
    num_outstanding_reads = num_outstanding_reads_per_qp * num_qps;

    // pin
    cpu_set_t isol_core;
    CPU_ZERO(&isol_core);
    CPU_SET(MEASUREMENT_THREAD, &isol_core);
    sched_setaffinity(0, sizeof(cpu_set_t), &isol_core);
    //setpriority(PRIO_PROCESS, 0, -20);
    ticks_per_ns = calibrate_ticks();
    // Allocate IDs
    ids = malloc(num_qps*sizeof(struct rdma_cm_id *));
    for (int i = 0; i < num_qps; i++) ids[i] = NULL;

    // Allocate context space and connections space
    s_ctx = malloc(num_qps*sizeof(struct context *));
    connections = malloc(num_qps*sizeof(struct connection *));
    for (int i = 0; i < num_qps; i++) {
        s_ctx[i] = NULL;
        connections[i] = malloc(sizeof(struct connection));
    }
    bad_send_wr = malloc(num_qps*sizeof(struct ibv_send_wr *));

    // Allocate memory to read remote memory into
    client_access_addresses = malloc(num_outstanding_reads*sizeof(size_t *));
    for (int i = 0; i < num_outstanding_reads; i++) {
        client_access_addresses[i] = malloc(MESSAGE_SIZE);
        memset(client_access_addresses[i], 0 , MESSAGE_SIZE);
    }
    same_bank_addresses = malloc(num_outstanding_reads*sizeof(void *));
    // -----------------------------------------------------------
    // Establish connection
    TEST_NZ(getaddrinfo(argv[1], "49999", NULL, &addr));
    TEST_Z(ec = rdma_create_event_channel());

    // First QP
    TEST_NZ(rdma_create_id(ec, &(ids[cur_qps]), NULL, RDMA_PS_TCP));
    TEST_NZ(rdma_resolve_addr(ids[cur_qps], NULL, addr->ai_addr, TIMEOUT_IN_MS));

    while (rdma_get_cm_event(ec, &event) == 0) {
        struct rdma_cm_event event_copy;

        memcpy(&event_copy, event, sizeof(*event));
        rdma_ack_cm_event(event);

        if (on_event(&event_copy)) break;
    }
    rdma_destroy_event_channel(ec);

    return 0;
}

int on_connection(struct rdma_cm_id *id) {
    on_connect(id->context);
    if (++cur_qps < num_qps) {
        // Set up next QP
        TEST_NZ(rdma_create_id(ec, &(ids[cur_qps]), NULL, RDMA_PS_TCP));
        TEST_NZ(rdma_resolve_addr(ids[cur_qps], NULL, addr->ai_addr, TIMEOUT_IN_MS));
        return 0;
    } else {
        freeaddrinfo(addr);
        TEST_NZ(on_region_exchange_completion(IBV_WC_RECV));

        populate_access_addresses();
        // Start polling for completions in another thread
        pthread_t cq_poller_thread;
        pthread_create(&cq_poller_thread, NULL, wait_reads_complete, NULL);
        // Prepare reads
        spin(1000000);
        reads = malloc(num_qps * sizeof(struct ibv_send_wr *));
        for (int i = 0; i < num_qps; i++) {
            reads[i] = send_remote_reads(connections[i], i);
        }

        memset(ends, 0, SAMPLES*sizeof(uint64_t)); // verification
        // Start reading
        spin(4000000);
    uint64_t cur_sample = 0;
        struct timespec start;
        while (cur_sample < (uint64_t) SAMPLES) {
            uint64_t id = cur_sample % num_qps;
            reads[id]->wr_id = cur_sample;
            while (time_elapsed_in_ns(start) < PROBE_INTERVAL - 100);
            get_rdtsc_timespec(&start);
            starts[cur_sample] = RDTSC();
            ibv_post_send(connections[id]->qp, reads[id], &(bad_send_wr[id]));
            cur_sample++;
        }
        
        pthread_join(cq_poller_thread, NULL);
        return 1;
    }
}

int on_event(struct rdma_cm_event *event) {
  int r = 0;

  if (event->event == RDMA_CM_EVENT_ADDR_RESOLVED)
    r = on_addr_resolved(event->id);
  else if (event->event == RDMA_CM_EVENT_ROUTE_RESOLVED)
    r = on_route_resolved(event->id);
  else if (event->event == RDMA_CM_EVENT_ESTABLISHED)
    r = on_connection(event->id);
  else if (event->event == RDMA_CM_EVENT_DISCONNECTED)
    r = on_disconnect(event->id);
  else {
    //fprintf(stderr, "on_event: %d\n", event->event);
    die("on_event: unknown event!");
  }
  return r;
}

int on_addr_resolved(struct rdma_cm_id *id) {
  build_connection(id);
  register_common_memory(connections[cur_qps]);
  register_client_memory(connections[cur_qps]);
  if (cur_qps == 0) post_receive_same_bank_regions();
  TEST_NZ(rdma_resolve_route(id, TIMEOUT_IN_MS));

  return 0;
}

int on_route_resolved(struct rdma_cm_id *id) {
    struct rdma_conn_param cm_params;

    build_params(&cm_params);
    TEST_NZ(rdma_connect(id, &cm_params));
    return 0;
}

int on_disconnect(struct rdma_cm_id *id) {
    return 1; /* exit event loop */
}

struct ibv_send_wr *send_remote_reads(struct connection *conn, int index) {
    struct ibv_send_wr *wr, *begin, *previous_wr = NULL;
    struct ibv_sge *sge;

    for (int i = 0; i < num_outstanding_reads_per_qp; i++) {
        wr = malloc(sizeof(struct ibv_send_wr));
        sge = malloc(sizeof(struct ibv_sge));
        if (previous_wr) 
            previous_wr->next = wr;
        else
            begin = wr;
        previous_wr = wr;
        memset(wr, 0, sizeof(*wr));
        wr->opcode = IBV_WR_RDMA_READ;
        wr->sg_list = sge;
        wr->num_sge = 1;
        wr->send_flags = IBV_SEND_SIGNALED; // signal all
        wr->wr.rdma.remote_addr = (uintptr_t)same_bank_addresses[num_outstanding_reads_per_qp * index + i];
        wr->wr.rdma.rkey = connections[0]->bank_regions[0].rkey;

        sge->addr = (uintptr_t)client_access_addresses[num_outstanding_reads_per_qp * index + i];
        sge->length = MESSAGE_SIZE;
        sge->lkey = connections[index]->bank_region_pointers[i]->lkey;
    }
    return begin;
} 

    void* wait_reads_complete() {
    // pin
    cpu_set_t isol_core;
    CPU_ZERO(&isol_core);
    CPU_SET(4, &isol_core);
    sched_setaffinity(0, sizeof(cpu_set_t), &isol_core);


    uint64_t remaining = (uint64_t) SAMPLES;
    int r;
    while(remaining) {
        uint64_t receive_time = RDTSC();
        r = ibv_poll_cq(connections[0]->qp->send_cq, 1000, work_complete);
        for (int i = 0; i < r; i++) {
            ends[work_complete[i].wr_id] = receive_time;
        }
        remaining -=r;
    }
    // Print
    for (int i = 0; i < SAMPLES; i++) {
        printf("%.2f\n", (int)(ends[i]-starts[i])/ticks_per_ns);   
    }
    return NULL; 
}


void register_client_memory(struct connection *conn) {
    // Allocate space to store memory region pointers
    connections[cur_qps]->bank_region_pointers = malloc(num_outstanding_reads_per_qp*sizeof(struct ibv_mr *));
    for (int i = 0; i < num_outstanding_reads_per_qp; i++) {
        struct ibv_mr *temp;
        TEST_Z(temp = ibv_reg_mr (
            s_ctx[cur_qps]->pd,
            client_access_addresses[cur_qps * num_outstanding_reads_per_qp + i],
            MESSAGE_SIZE,
            IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE)); 
        connections[cur_qps]->bank_region_pointers[i] = temp; // save pointer for deallocation
    }

}

void populate_access_addresses() {
    int cur_total_count = 0;
    uint64_t base;
    unsigned long long mapping_size = (unsigned long long)(num_outstanding_reads / 8 + 1) * 1024ULL * 1024ULL * 1024ULL;

    void *mapping = connections[0]->bank_regions[0].addr;

    getRandomAddress(&base, mapping, mapping_size);
    uint64_t other;
    // Populate access addresses
    while (cur_total_count < num_outstanding_reads) {
        getRandomAddress(&other, mapping, mapping_size);
        if (base == other) continue;
        void *other_t = (size_t *) other;
        if ( getBRbits(base) == getBRbits(other)
            && !containsUpTo(same_bank_addresses, cur_total_count, other_t) 
            && getRowBits(base) != getRowBits(other)) {
            same_bank_addresses[cur_total_count++] = other_t;
        }
    }
}
