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
#define MAX_SAME_BANK 192

static int on_connect_request(struct rdma_cm_id *id);
static int on_connection(struct rdma_cm_id *id);
static int on_disconnect(struct rdma_cm_id *id);
static int on_event(struct rdma_cm_event *event);
static void send_region_base();
static void register_sender_receiver_memory();
static void populate_access_addresses();
static void intermediary_register_exchange_region();



struct context **s_ctx;
struct connection **connections;
size_t **access_addresses;
int num_qps;
int cur_qps = 0;
void *mapping;
int num_outstanding_reads; // DO NOT USE. REFACTOR


/* helper vars */
int num_unique_access_addresses;
int sender_num_outstanding_reads;
int receiver_num_outstanding_reads;

/* command arguments */
int num_sender_qps;
int num_receiver_qps;
int receiver_num_outstanding_reads_per_qp = 1;
int sender_num_outstanding_reads_per_qp;
int sender_repetitions;
/* end of command arguments */

int main(int argc, char **argv) {
    /* process command line args */
    if (argc != 5) exit(1);
    num_sender_qps = atoi(argv[1]);
    sender_num_outstanding_reads_per_qp = atoi(argv[2]);
    sender_repetitions = atoi(argv[3]);
    num_receiver_qps = atoi(argv[4]);
    /* end command line arguments processing */

    /* helper variables */
    num_qps = num_receiver_qps + num_sender_qps;
    receiver_num_outstanding_reads = num_receiver_qps * receiver_num_outstanding_reads_per_qp;
    sender_num_outstanding_reads = num_sender_qps * sender_num_outstanding_reads_per_qp / sender_repetitions;
    num_unique_access_addresses = receiver_num_outstanding_reads + sender_num_outstanding_reads;


    srand(time(NULL));

    // Pin
    cpu_set_t isol_core;
    CPU_ZERO(&isol_core);
    CPU_SET(MEASUREMENT_THREAD, &isol_core);
    sched_setaffinity(0, sizeof(cpu_set_t), &isol_core);
    //setpriority(PRIO_PROCESS, 0, -20);

    // Allocate access addresses
    access_addresses = malloc(num_unique_access_addresses*sizeof(size_t *));

    //--------------------------------------------------------------

    struct sockaddr_in6 addr;
    struct rdma_cm_event *event = NULL;
    struct rdma_cm_id *listener = NULL;
    struct rdma_event_channel *ec = NULL;
    uint16_t port = 49999;


    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);

    // Allocate context space and connections space
    s_ctx = malloc(num_qps*sizeof(struct context *));
    connections = malloc(num_qps*sizeof(struct connection *));
    for (int i = 0; i < num_qps; i++) {
        s_ctx[i] = NULL;
        connections[i] = malloc(sizeof(struct connection));
    }

    TEST_Z(ec = rdma_create_event_channel());
    TEST_NZ(rdma_create_id(ec, &listener, NULL, RDMA_PS_TCP));
    TEST_NZ(rdma_bind_addr(listener, (struct sockaddr *)&addr));
    TEST_NZ(rdma_listen(listener, 10));

    port = ntohs(rdma_get_src_port(listener));
    //printf("listening on port %d.\n", port);

    while (rdma_get_cm_event(ec, &event) == 0) {
        struct rdma_cm_event event_copy;

        memcpy(&event_copy, event, sizeof(*event));
        rdma_ack_cm_event(event);

        if (on_event(&event_copy)) break;
    }

    rdma_destroy_id(listener);
    rdma_destroy_event_channel(ec);

    return 0;
}

int on_event(struct rdma_cm_event *event) {
  int r = 0;

  if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST)
    r = on_connect_request(event->id);
  else if (event->event == RDMA_CM_EVENT_ESTABLISHED)
    r = on_connection(event->id);
  else if (event->event == RDMA_CM_EVENT_DISCONNECTED)
    r = on_disconnect(event->id);
  else
    die("on_event: unknown event.");

  return r;
}

int on_connect_request(struct rdma_cm_id *id) {
  struct rdma_conn_param cm_params;

  build_connection(id);
  build_params(&cm_params);
  TEST_NZ(rdma_accept(id, &cm_params));

  return 0;
}

int on_connection(struct rdma_cm_id *id) {
    printf("intermediary connected qp %d\n", cur_qps);
    
    // For each QP register memory region that contains the memory regions to read
    intermediary_register_exchange_region();
    cur_qps++;
    on_connect(id->context);

    if (num_qps == cur_qps) { // All QPs connected
        register_sender_receiver_memory();

        // Trash cache
        void *thrash;
        size_t thrash_size = (size_t) 1ULL<<27;
        thrash = malloc(thrash_size);
        assert(mapping != (void *) -1);
        for (int i = 0; i < thrash_size/sizeof(int); i++) {
            int *t = (int *)thrash + i;
            *t = 0;
        }
        memset(thrash, 0, thrash_size);

        // Send and wait for *sender* regions to be received
        send_region_base(connections[0], num_sender_qps * sender_num_outstanding_reads_per_qp);
        TEST_NZ(on_region_exchange_completion(IBV_WC_SEND));
        printf("Send and received sender regions\n");

        // Send and wait for *receiver* regions to be receiver
        send_region_base(connections[num_sender_qps], receiver_num_outstanding_reads);
        TEST_NZ(on_region_exchange_completion(IBV_WC_SEND));
        printf("Sent and received receiver regions\n");

        //Provide service for some time
        struct timespec service_period;
        get_rdtsc_timespec(&service_period);
        while(time_elapsed_in_sec(service_period) < 30); // offer service for 20 secs
        return 1;
    }
    return 0;
}

int on_disconnect(struct rdma_cm_id *id) {
    return 1;
}

void send_region_base(struct connection *conn, int num_reads) {
    // Send region thrugh connection
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;

    memset(&wr, 0, sizeof(wr));

    wr.wr_id = (uintptr_t)conn;
    wr.opcode = IBV_WR_SEND;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_SIGNALED;

    sge.addr = (uintptr_t)conn->bank_regions;
    sge.length = sizeof(struct ibv_mr);
    sge.lkey = conn->memory_registers_region->lkey;

    while (!conn->connected);

    TEST_NZ(ibv_post_send(conn->qp, &wr, &bad_wr));
}


void intermediary_register_exchange_region() {
    // Register region which is going to be used to exchange the memory regions
    int num_regions;
    if (cur_qps == 0) num_regions = num_sender_qps * sender_num_outstanding_reads_per_qp;
    else if (cur_qps == num_sender_qps) num_regions = num_receiver_qps * receiver_num_outstanding_reads_per_qp;
    else return;

    connections[cur_qps]->bank_regions = malloc(sizeof(struct ibv_mr));

    // Register array of MR's
    TEST_Z(connections[cur_qps]->memory_registers_region = ibv_reg_mr (
        s_ctx[cur_qps]->pd,
        connections[cur_qps]->bank_regions,
        sizeof(struct ibv_mr),
        IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE));
}

void register_sender_receiver_memory() {
    // Register the memory regions that the sender and the receiver can access
    unsigned long long gb_in_bytes = 1024ULL * 1024ULL * 1024ULL;
    unsigned long long sender_mapping_size = (sender_num_outstanding_reads / 8 + 1) * gb_in_bytes; // in B, 1 extra GB
    unsigned long long receiver_mapping_size = (receiver_num_outstanding_reads / 8 + 1) * gb_in_bytes; // in B, 1 extra GB

    void *sender_mapping = setupMapping(sender_mapping_size);
    void *receiver_mapping = setupMapping(receiver_mapping_size);
    // Sender memory regions
    struct ibv_mr *temp;
    TEST_Z(temp = ibv_reg_mr (
        s_ctx[0]->pd,
        sender_mapping,
        sender_mapping_size,
        IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE)); 
        memcpy(connections[0]->bank_regions,
            temp, sizeof(struct ibv_mr)); // copy for bookkeeping

    // Receiver memory regions
    TEST_Z(temp = ibv_reg_mr (
        s_ctx[num_sender_qps]->pd,
        receiver_mapping,
        receiver_mapping_size,
        IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE)); 
        memcpy(connections[num_sender_qps]->bank_regions,
            temp, sizeof(struct ibv_mr)); // copy for bookkeeping
}




