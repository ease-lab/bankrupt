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

#ifndef TIME_RDTSC_H
#define TIME_RDTSC_H
#include <stdint.h> /* for uint64_t */
#include <time.h>  /* for struct timespec */
#include <stdio.h>
#include <assert.h>

#define ENABLE_STATIC_TICKS_PER_NS 1
#define RDTSC_TYPICAL_TICKS_PER_NS 2.2

double g_ticks_per_ns = 2.2;

// assembly code to read the TSC
static inline uint64_t RDTSC()
{
    volatile unsigned int hi, lo;
    __asm__ volatile("rdtscp" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

static const int NANO_SECONDS_IN_SEC = 1000000000;
// returns a static buffer of struct timespec with the time difference of
// ts1 and ts2 ts1 is assumed to be greater than ts2
static struct timespec *
timespec_diff(struct timespec *ts1, struct timespec *ts2)
{
    static struct timespec ts;
    ts.tv_sec = ts1->tv_sec - ts2->tv_sec;
    ts.tv_nsec = ts1->tv_nsec - ts2->tv_nsec;
    if (ts.tv_nsec < 0) {
        ts.tv_sec--;
        ts.tv_nsec += NANO_SECONDS_IN_SEC;
    }
    return &ts;
}

static double
calibrate_ticks()
{
    struct timespec begin_ts, end_ts;
    //#printf("Start RDTSC calibration: patience is a virtue\n");
    clock_gettime(CLOCK_MONOTONIC, &begin_ts);
    uint64_t begin = RDTSC();
    // do something CPU intensive
    for (volatile unsigned long long i = 0; i < 1000000000ULL; ++i);
    uint64_t end = RDTSC();
    clock_gettime(CLOCK_MONOTONIC, &end_ts);
    struct timespec *tmp_ts = timespec_diff(&end_ts, &begin_ts);
    uint64_t ns_elapsed = (uint64_t) (tmp_ts->tv_sec * 1000000000LL + tmp_ts->tv_nsec);
    g_ticks_per_ns = (double) (end - begin) / (double) ns_elapsed;
    //#printf("RDTSC calibration is done (ticks_per_ns: %.2f)\n", g_ticks_per_ns);
    return g_ticks_per_ns;
}

// Call once (it is not thread safe) before using RDTSC, has side effect of binding process to CPU1
static inline void
init_rdtsc(uint8_t auto_calibration, double ticks_per_ns)
{
    if(auto_calibration > 0)
        calibrate_ticks();
    else{
        assert(ticks_per_ns > 0);
        g_ticks_per_ns = ticks_per_ns;
    }
}

static inline void
get_timespec(struct timespec *ts, uint64_t nsecs)
{
    ts->tv_sec = nsecs / NANO_SECONDS_IN_SEC;
    ts->tv_nsec = nsecs % NANO_SECONDS_IN_SEC;
}

// ts will be filled with time converted from TSC reading
static inline void
get_rdtsc_timespec(struct timespec *ts)
{
    get_timespec(ts, (uint64_t) (RDTSC() / g_ticks_per_ns));
}

static inline double
time_elapsed_in_us(struct timespec start)
{
	struct timespec now, *diff;
    get_rdtsc_timespec(&now);
	diff = timespec_diff(&now, &start);
	return  diff->tv_sec * 1000000 + diff->tv_nsec / 1000;
}

static inline double
time_elapsed_in_ms(struct timespec start)
{
	struct timespec now, *diff;
    get_rdtsc_timespec(&now);
	diff = timespec_diff(&now, &start);
	return  diff->tv_sec * 1000 + diff->tv_nsec / 1000000;
}

static inline double
time_elapsed_in_sec(struct timespec start)
{
	struct timespec now, *diff;
    get_rdtsc_timespec(&now);
	diff = timespec_diff(&now, &start);
	return  diff->tv_sec + diff->tv_nsec / NANO_SECONDS_IN_SEC;
}

static inline double
time_elapsed_in_ns(struct timespec start)
{
    struct timespec now, *diff;
    get_rdtsc_timespec(&now);
    diff = timespec_diff(&now, &start);
    return  diff->tv_sec * 1000000000 + diff->tv_nsec / 1;
}
#endif //TIME_RDTSC_H
