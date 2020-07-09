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
    
#include <assert.h>
    #include <fcntl.h>
    #include <linux/kernel-page-flags.h>
    #include <stdint.h>
    #include <sys/sysinfo.h>
    #include <sys/mman.h>
    #include <unistd.h>
    #include <string.h>
    #include <time.h>
    #include <stdlib.h>
    #include <sys/resource.h>
    #include <math.h>
    #include <stdio.h>

    #ifndef EXP_CONSTANTS
    #define EXP_CONSTANTS
    #define MM 156
    #define MATRIX_A 0xB5026F5AA96619E9ULL
    #define UM 0xFFFFFFFF80000000ULL /* Most significant 33 bits */
    #define LM 0x7FFFFFFFULL /* Least significant 31 bits */
    #define MAP_HUGE_1GB    (30 << MAP_HUGE_SHIFT)
    #define BACK_BITS 7 // orig 14
    #define FRONT_BITS 37 // orig 35

    #define NN 312

// Source for RNG: http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/emt64.html
    // -------------------------
    // 64-bit RNG
    /* The array for the state vector */
static unsigned long long mt[NN]; 
/* mti==NN+1 means mt[NN] is not initialized */
static int mti=NN+1; 

/* initializes mt[NN] with a seed */
void init_genrand64(unsigned long long seed)
{
    mt[0] = seed;
    for (mti=1; mti<NN; mti++) 
        mt[mti] =  (6364136223846793005ULL * (mt[mti-1] ^ (mt[mti-1] >> 62)) + mti);
}

/* initialize by an array with array-length */
/* init_key is the array for initializing keys */
/* key_length is its length */
void init_by_array64(unsigned long long init_key[],
             unsigned long long key_length)
{
    unsigned long long i, j, k;
    init_genrand64(19650218ULL);
    i=1; j=0;
    k = (NN>key_length ? NN : key_length);
    for (; k; k--) {
        mt[i] = (mt[i] ^ ((mt[i-1] ^ (mt[i-1] >> 62)) * 3935559000370003845ULL))
          + init_key[j] + j; /* non linear */
        i++; j++;
        if (i>=NN) { mt[0] = mt[NN-1]; i=1; }
        if (j>=key_length) j=0;
    }
    for (k=NN-1; k; k--) {
        mt[i] = (mt[i] ^ ((mt[i-1] ^ (mt[i-1] >> 62)) * 2862933555777941757ULL))
          - i; /* non linear */
        i++;
        if (i>=NN) { mt[0] = mt[NN-1]; i=1; }
    }

    mt[0] = 1ULL << 63; /* MSB is 1; assuring non-zero initial array */ 
}

/* generates a random number on [0, 2^64-1]-interval */
unsigned long long genrand64_int64(void)
{
    int i;
    unsigned long long x;
    static unsigned long long mag01[2]={0ULL, MATRIX_A};

    if (mti >= NN) { /* generate NN words at one time */

        /* if init_genrand64() has not been called, */
        /* a default initial seed is used     */
        if (mti == NN+1) 
            init_genrand64(5489ULL); 

        for (i=0;i<NN-MM;i++) {
            x = (mt[i]&UM)|(mt[i+1]&LM);
            mt[i] = mt[i+MM] ^ (x>>1) ^ mag01[(int)(x&1ULL)];
        }
        for (;i<NN-1;i++) {
            x = (mt[i]&UM)|(mt[i+1]&LM);
            mt[i] = mt[i+(MM-NN)] ^ (x>>1) ^ mag01[(int)(x&1ULL)];
        }
        x = (mt[NN-1]&UM)|(mt[0]&LM);
        mt[NN-1] = mt[MM-1] ^ (x>>1) ^ mag01[(int)(x&1ULL)];

        mti = 0;
    }
  
    x = mt[mti++];

    x ^= (x >> 29) & 0x5555555555555555ULL;
    x ^= (x << 17) & 0x71D67FFFEDA60000ULL;
    x ^= (x << 37) & 0xFFF7EEE000000000ULL;
    x ^= (x >> 43);

    return x;
}

/* generates a random number on [0, 2^63-1]-interval */
long long genrand64_int63(void)
{
    return (long long)(genrand64_int64() >> 1);
}

/* generates a random number on [0,1]-real-interval */
double genrand64_real1(void)
{
    return (genrand64_int64() >> 11) * (1.0/9007199254740991.0);
}

/* generates a random number on [0,1)-real-interval */
double genrand64_real2(void)
{
    return (genrand64_int64() >> 11) * (1.0/9007199254740992.0);
}

/* generates a random number on (0,1)-real-interval */
double genrand64_real3(void)
{
    return ((genrand64_int64() >> 12) + 0.5) * (1.0/4503599627370496.0);
}

    void getRandomAddress(uint64_t *virt, void *mapping, unsigned long long mapping_size) {
        size_t offset = (size_t)(genrand64_int64() % (mapping_size / (128))) * (128);
        *virt = (uint64_t) mapping + offset;
    }

        // ----------------------------------------------
    void *setupMapping(unsigned long long mapping_size) {
        void *mapping;
        mapping = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE,
                       MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB | MAP_HUGE_1GB, -1, 0);
        assert(mapping != (void *) -1);

        memset(mapping, 0, mapping_size);
        return mapping;
    }


     // ----------------------------------------------------------
    uint64_t getBRbits(uint64_t addr) { // BR stands for bank/rank
        return (addr << FRONT_BITS) >> (BACK_BITS + FRONT_BITS);
    }

    uint64_t getRowBits(uint64_t addr) {
        return addr >> (64 - FRONT_BITS);
    }

    int containsUpTo(void **arr, int index, volatile void *value) {
        for (int i = 0; i < index; ++i) {
            if (arr[i] == value) return 1;
        }
        return 0;
    }


#endif
