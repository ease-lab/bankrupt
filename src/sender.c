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

const int TIMEOUT_IN_MS = 500; /* ms */

static int on_addr_resolved(struct rdma_cm_id *id);
static int on_connection(struct rdma_cm_id *id);
static int on_event(struct rdma_cm_event *event);
static int on_route_resolved(struct rdma_cm_id *id);
static void register_client_memory(struct connection *conn);
struct ibv_send_wr *send_remote_reads(struct connection *conn, int index);
static inline int wait_reads_complete();
static void populate_access_addresses();

struct ibv_send_wr **reads, **bad_send_wr = NULL;
int num_outstanding_reads;
struct ibv_wc work_complete[200];
size_t **client_access_addresses;
int cur_qps = 0;
void **same_bank_addresses;

/* command line args */
int num_outstanding_reads_per_qp;
int num_qps;
int number_sender_reps;
/* ----------------- */

/* connection management */
struct context **s_ctx;
struct connection **connections;
struct rdma_event_channel *ec = NULL;
struct addrinfo *addr;
struct rdma_cm_id **ids;
/* --------------------- */

int main(int argc, char **argv) {
  	struct rdma_cm_event *event = NULL;
  
  	if (argc != 5) {
        exit(1);
    }
    num_outstanding_reads_per_qp = atoi(argv[3]);
    num_qps = atoi(argv[2]);
    number_sender_reps = atoi(argv[4]);
    num_outstanding_reads = num_outstanding_reads_per_qp * num_qps;

    // pin
    cpu_set_t isol_core;
    CPU_ZERO(&isol_core);
    CPU_SET(MEASUREMENT_THREAD, &isol_core);
    sched_setaffinity(0, sizeof(cpu_set_t), &isol_core);

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
        memset(client_access_addresses[i], 0 , MESSAGE_SIZE); // For verification
    }

    same_bank_addresses = malloc(num_outstanding_reads*sizeof(void *));
    
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

int on_event(struct rdma_cm_event *event) {
  int r = 0;

  if (event->event == RDMA_CM_EVENT_ADDR_RESOLVED)
    r = on_addr_resolved(event->id);
  else if (event->event == RDMA_CM_EVENT_ROUTE_RESOLVED)
    r = on_route_resolved(event->id);
  else if (event->event == RDMA_CM_EVENT_ESTABLISHED)
    r = on_connection(event->id);
  else {
    die("on_event: unknown event!");
  }
  return r;
}

int on_addr_resolved(struct rdma_cm_id *id) {

  build_connection(id);
  register_common_memory(connections[cur_qps]);
  register_client_memory(connections[cur_qps]);
  if (cur_qps == 0) post_receive_same_bank_regions(); // Post receives for big memory region
  TEST_NZ(rdma_resolve_route(id, TIMEOUT_IN_MS));

  return 0;
}

int on_route_resolved(struct rdma_cm_id *id) {
    struct rdma_conn_param cm_params;

    //printf("route resolved.\n");
    build_params(&cm_params);
    TEST_NZ(rdma_connect(id, &cm_params));
    return 0;
}

int on_connection(struct rdma_cm_id *id) {
    double time_taken;

    printf("client connected %d\n", cur_qps);
    on_connect(id->context);
    if (++cur_qps < num_qps) {
        // next QP
        TEST_NZ(rdma_create_id(ec, &(ids[cur_qps]), NULL, RDMA_PS_TCP));
        TEST_NZ(rdma_resolve_addr(ids[cur_qps], NULL, addr->ai_addr, TIMEOUT_IN_MS));
        printf("connected qp %d\n", cur_qps);
        return 0;
    } else {
        freeaddrinfo(addr);
        printf("starting to wait for region exchange\n");
        TEST_NZ(on_region_exchange_completion(IBV_WC_RECV));
        printf("region exchange done\n");
        sleep(1);

        printf("Look here is the address\n");
        printf("%p\n", connections[0]->bank_regions[0].addr);
        populate_access_addresses();
        // Prepare reads
        reads = malloc(num_qps * sizeof(struct ibv_send_wr *));
        for (int i = 0; i < num_qps; i++) {
            reads[i] = send_remote_reads(connections[i], i);
        }
        printf("prepared reads\n");

        // Send remote reads
        uint64_t start, end;
        struct timespec start_send;
        int message;
        int message_length;
        int period = 13;
        struct timespec start_burst;

        // Tune period
        double tuning_rtts[5000];
        for (int i = 0; i < 5000; i++) {
            // Send burst
            get_rdtsc_timespec(&start_burst);
            for (int i = 0; i < num_qps; i++) {
                ibv_post_send(connections[i]->qp, reads[i], &(bad_send_wr[i]));
            }
            wait_reads_complete();
            tuning_rtts[i] = time_elapsed_in_us(start_burst);
        }

        double sum_rtts = 0;
        for (int i = 1000; i < 5000; i++) {
            sum_rtts += tuning_rtts[i];
        }
        period = (int) (sum_rtts / 4000);
        printf("Period is %d\n", period);
        // Send message repeatedly
        get_rdtsc_timespec(&start_send);
        while(time_elapsed_in_sec(start_send) < 10) {
            message = 0b11111111111;
            message_length = 11;
            while (message_length--) {
                if (message % 2) {
                    get_rdtsc_timespec(&start_burst);
                    for (int i = 0; i < num_qps; i++) {
                        ibv_post_send(connections[i]->qp, reads[i], &(bad_send_wr[i]));
                        
                    }
                    wait_reads_complete();
                    //printf("%.f\n", time_elapsed_in_us(start_burst));
                    while (time_elapsed_in_us(start_burst) < period);
                } else {
                    spin(period);
                }
                message /= 2;
            }
        }
        return 1;
    }
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
        wr->wr_id = (uint64_t)index;
        wr->opcode = IBV_WR_RDMA_READ;
        wr->sg_list = sge;
        wr->num_sge = 1;
        if (i == num_outstanding_reads_per_qp - 1)
            wr->send_flags = IBV_SEND_SIGNALED; // signal only last
        wr->wr.rdma.remote_addr = (uintptr_t)same_bank_addresses[num_outstanding_reads_per_qp * index + i];
        wr->wr.rdma.rkey = connections[0]->bank_regions[0].rkey;

        sge->addr = (uintptr_t)client_access_addresses[num_outstanding_reads_per_qp * index + i];
        sge->length = MESSAGE_SIZE;
        sge->lkey = connections[index]->bank_region_pointers[i]->lkey;
    }
    return begin;
} 

inline int wait_reads_complete() {
    int r = 0;
    while (r < num_qps) {
        r += ibv_poll_cq(connections[0]->qp->recv_cq, num_qps, work_complete);
    }
    return 0;
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
    int num_reps = number_sender_reps;
    int num_unique_outstanding_reads = num_outstanding_reads / num_reps;
    int cur_total_count = 0;
    uint64_t base;
    unsigned long long mapping_size = (unsigned long long)(num_unique_outstanding_reads / 8 + 1) * 1024ULL * 1024ULL * 1024ULL;
    void *mapping = connections[0]->bank_regions->addr;
    getRandomAddress(&base, mapping, mapping_size);
    uint64_t other;
    // Populate access addresses
    while (cur_total_count < num_unique_outstanding_reads) {
        getRandomAddress(&other, mapping, mapping_size);
        if (base == other) continue;
        void *other_t = (size_t *) other;
        if ( getBRbits(base) == getBRbits(other)
            && !containsUpTo(same_bank_addresses, cur_total_count, other_t) 
            && getRowBits(base) != getRowBits(other)) {
            same_bank_addresses[cur_total_count++] = other_t;
        }
    }

    while (cur_total_count < num_outstanding_reads) {
        same_bank_addresses[cur_total_count] = same_bank_addresses[cur_total_count % num_unique_outstanding_reads];
        cur_total_count++;
    }
}

