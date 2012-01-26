/*
    Copyright 2005-2011 Intel Corporation.  All Rights Reserved.

    This file is part of Threading Building Blocks.

    Threading Building Blocks is free software; you can redistribute it
    and/or modify it under the terms of the GNU General Public License
    version 2 as published by the Free Software Foundation.

    Threading Building Blocks is distributed in the hope that it will be
    useful, but WITHOUT ANY WARRANTY; without even the implied warranty
    of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Threading Building Blocks; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    As a special exception, you may use this file as part of a free software
    library without restriction.  Specifically, if other files instantiate
    templates or use macros or inline functions from this file, or you compile
    this file and link it with other files to produce an executable, this
    file does not by itself cause the resulting executable to be covered by
    the GNU General Public License.  This exception does not however
    invalidate any other reasons why the executable file might be covered by
    the GNU General Public License.
*/

#include "tbb/scalable_allocator.h"
#include "tbb/atomic.h"
#include "harness.h"
#include "harness_barrier.h"
#include "harness_tbb_independence.h"

template<typename T>
static inline T alignUp  (T arg, uintptr_t alignment) {
    return T(((uintptr_t)arg+(alignment-1)) & ~(alignment-1));
}

struct PoolSpace {
    size_t pos;
    int    regions;
    size_t bufSize;
    char  *space;

    static const size_t BUF_SIZE = 8*1024*1024;

    PoolSpace(size_t bufSize = BUF_SIZE) :
        pos(0), regions(0),
        bufSize(bufSize), space(new char[bufSize]) {
        memset(space, 0, bufSize);
    }
    ~PoolSpace() {
        delete []space;
    }
};

static PoolSpace *poolSpace;

struct MallocPoolHeader {
    void  *rawPtr;
    size_t userSize;
};

static tbb::atomic<int> liveRegions;

static void *getMallocMem(intptr_t /*pool_id*/, size_t &bytes)
{
    void *rawPtr = malloc(bytes+sizeof(MallocPoolHeader));
    if (!rawPtr)
        return NULL;
    void *ret = (void *)((uintptr_t)rawPtr+sizeof(MallocPoolHeader));

    MallocPoolHeader *hdr = (MallocPoolHeader*)ret-1;
    hdr->rawPtr = rawPtr;
    hdr->userSize = bytes;

    liveRegions++;

    return ret;
}

static int putMallocMem(intptr_t /*pool_id*/, void *ptr, size_t bytes)
{
    MallocPoolHeader *hdr = (MallocPoolHeader*)ptr-1;
    ASSERT(bytes == hdr->userSize, "Invalid size in pool callback.");
    free(hdr->rawPtr);

    liveRegions--;

    return 0;
}

void TestPoolReset()
{
    rml::MemPoolPolicy pol;
    pol.pAlloc = getMallocMem;
    pol.pFree = putMallocMem;
    pol.granularity = 8;

    rml::MemoryPool *pool = pool_create(0, &pol);
    for (int i=0; i<100; i++) {
        ASSERT(pool_malloc(pool, 8), NULL);
        ASSERT(pool_malloc(pool, 50*1024), NULL);
    }
    int regionsBeforeReset = liveRegions;
    pool_reset(pool);
    for (int i=0; i<100; i++) {
        ASSERT(pool_malloc(pool, 8), NULL);
        ASSERT(pool_malloc(pool, 50*1024), NULL);
    }
    ASSERT(regionsBeforeReset == liveRegions,
           "Expected no new regions allocation.");
    pool_destroy(pool);
    ASSERT(!liveRegions, "Expected all regions were released.");
}

class SharedPoolRun: NoAssign {
    static long                 threadNum;
    static Harness::SpinBarrier startB,
                                mallocDone;
    static rml::MemoryPool     *pool;
    static void               **crossThread,
                              **afterTerm;
public:
    static const int OBJ_CNT = 100;

    static void init(int num, rml::MemoryPool *pl, void **crThread, void **aTerm) {
        threadNum = num;
        pool = pl;
        crossThread = crThread;
        afterTerm = aTerm;
        startB.initialize(threadNum);
        mallocDone.initialize(threadNum);
    }

    void operator()( int id ) const {
        const int ITERS = 1000;
        void *local[ITERS];

        startB.wait();
        for (int i=id*OBJ_CNT; i<(id+1)*OBJ_CNT; i++) {
            afterTerm[i] = pool_malloc(pool, i%2? 8*1024 : 9*1024);
            memset(afterTerm[i], i, i%2? 8*1024 : 9*1024);
            crossThread[i] = pool_malloc(pool, i%2? 9*1024 : 8*1024);
            memset(crossThread[i], i, i%2? 9*1024 : 8*1024);
        }

        for (int i=1; i<ITERS; i+=2) {
            local[i-1] = pool_malloc(pool, 6*1024);
            memset(local[i-1], i, 6*1024);
            local[i] = pool_malloc(pool, 16*1024);
            memset(local[i], i, 16*1024);
        }
        mallocDone.wait();
        int myVictim = threadNum-id-1;
        for (int i=myVictim*OBJ_CNT; i<(myVictim+1)*OBJ_CNT; i++)
            pool_free(pool, crossThread[i]);
        for (int i=0; i<ITERS; i++)
            pool_free(pool, local[i]);
    }
};

long                 SharedPoolRun::threadNum;
Harness::SpinBarrier SharedPoolRun::startB,
                     SharedPoolRun::mallocDone;
rml::MemoryPool     *SharedPoolRun::pool;
void               **SharedPoolRun::crossThread,
                   **SharedPoolRun::afterTerm;

// single pool shared by different threads
void TestSharedPool()
{
    rml::MemPoolPolicy pol;
    pol.pAlloc = getMallocMem;
    pol.pFree = putMallocMem;
    pol.granularity = 8;

    rml::MemoryPool *pool = pool_create(0, &pol);

    void **crossThread = new void*[MaxThread * SharedPoolRun::OBJ_CNT];
    void **afterTerm = new void*[MaxThread * SharedPoolRun::OBJ_CNT];

    for (int p=MinThread; p<=MaxThread; p++) {
        SharedPoolRun::init(p, pool, crossThread, afterTerm);
        SharedPoolRun thr;

        void *hugeObj = pool_malloc(pool, 10*1024*1024);
        ASSERT(hugeObj, NULL);

        NativeParallelFor( p, thr );

        pool_free(pool, hugeObj);
        for (int i=0; i<p*SharedPoolRun::OBJ_CNT; i++)
            pool_free(pool, afterTerm[i]);
    }
    delete []afterTerm;
    delete []crossThread;

    pool_destroy(pool);
    ASSERT(!liveRegions, "Expected all regions were released.");
}

void *CrossThreadGetMem(intptr_t pool_id, size_t &bytes)
{
    if (poolSpace[pool_id].pos + bytes > poolSpace[pool_id].bufSize)
        return NULL;

    void *ret = poolSpace[pool_id].space + poolSpace[pool_id].pos;
    poolSpace[pool_id].pos += bytes;
    poolSpace[pool_id].regions++;

    return ret;
}

int CrossThreadPutMem(intptr_t pool_id, void* /*raw_ptr*/, size_t /*raw_bytes*/)
{
    poolSpace[pool_id].regions--;
    return 0;
}

class CrossThreadRun: NoAssign {
    static long number_of_threads;
    static Harness::SpinBarrier barrier;
    static rml::MemoryPool **pool;
    static char **obj;
public:
    static void initBarrier(unsigned thrds) { barrier.initialize(thrds); }
    static void init(long num) {
        number_of_threads = num;
        pool = new rml::MemoryPool*[number_of_threads];
        poolSpace = new PoolSpace[number_of_threads];
        obj = new char*[number_of_threads];
    }
    static void destroy() {
        for (long i=0; i<number_of_threads; i++)
            ASSERT(!poolSpace[i].regions, "Memory leak detected");
        delete []pool;
        delete []poolSpace;
        delete []obj;
    }
    CrossThreadRun() {}
    void operator()( int id ) const {
        rml::MemPoolPolicy pol;

        pol.pAlloc = CrossThreadGetMem;
        pol.pFree = CrossThreadPutMem;
        pol.granularity = 8;
        pool[id] = pool_create(id, &pol);
        const int objLen = 10*id;

        obj[id] = (char*)pool_malloc(pool[id], objLen);
        ASSERT(obj[id], NULL);
        memset(obj[id], id, objLen);

        {
            const size_t lrgSz = 2*16*1024;
            void *ptrLarge = pool_malloc(pool[id], lrgSz);
            ASSERT(ptrLarge, NULL);
            memset(ptrLarge, 1, lrgSz);

            // consume all small objects
            while (pool_malloc(pool[id], 5*1024))
                ;
            // releasing of large object can give a chance to allocate more
            pool_free(pool[id], ptrLarge);

            ASSERT(pool_malloc(pool[id], 5*1024), NULL);
        }

        barrier.wait();
        int myPool = number_of_threads-id-1;
        for (int i=0; i<10*myPool; i++)
            ASSERT(myPool==obj[myPool][i], NULL);
        pool_free(pool[myPool], obj[myPool]);
        pool_destroy(pool[myPool]);
    }
};

long CrossThreadRun::number_of_threads;
Harness::SpinBarrier CrossThreadRun::barrier;
rml::MemoryPool **CrossThreadRun::pool;
char **CrossThreadRun::obj;

// pools created, used and destored by different threads
void TestCrossThreadPools()
{
    for (int p=MinThread; p<=MaxThread; p++) {
        CrossThreadRun::initBarrier(p);
        CrossThreadRun::init(p);
        NativeParallelFor( p, CrossThreadRun() );
        for (int i=0; i<p; i++)
            ASSERT(!poolSpace[i].regions, "Region leak detected");
        CrossThreadRun::destroy();
    }
}

// buffer is too small to pool be created, but must not leak resourses
void TestTooSmallBuffer()
{
    poolSpace = new PoolSpace(8*1024);

    rml::MemPoolPolicy pol;
    pol.pAlloc = CrossThreadGetMem;
    pol.pFree = CrossThreadPutMem;
    pol.granularity = 8;
    pool_destroy( pool_create(0, &pol) );
    ASSERT(!poolSpace[0].regions, "No leaks.");

    delete poolSpace;
}

static void *fixedBufGetMem(intptr_t /*pool_id*/, size_t &bytes)
{
    static const size_t BUF_SZ = 8*1024*1024;
    static char buf[BUF_SZ];
    static bool used;

    if (used)
        return NULL;
    used = true;
    bytes = BUF_SZ;
    return buf;
}

void TestFixedBufferPool()
{
    void *ptrs[7];
    rml::MemPoolPolicy pol;

    pol.pAlloc = fixedBufGetMem;
    pol.pFree = NULL;
    pol.granularity = 8;
    rml::MemoryPool *pool = pool_create(0, &pol);

    void *largeObj = pool_malloc(pool, 7*1024*1024);
    ASSERT(largeObj, NULL);
    pool_free(pool, largeObj);

    for (int i=0; i<7; i++) {
        ptrs[i] = pool_malloc(pool, 1024*1024);
        ASSERT(ptrs[i], NULL);
    }
    for (int i=0; i<7; i++)
        pool_free(pool, ptrs[i]);

    largeObj = pool_malloc(pool, 7*1024*1024);
    ASSERT(largeObj, NULL);
    pool_free(pool, largeObj);

    pool_destroy(pool);
}

static size_t currGranularity;

static void *getGranMem(intptr_t /*pool_id*/, size_t &bytes)
{
    ASSERT(!(bytes%currGranularity), "Region size mismatch granularity.");
    return malloc(bytes);
}

static int putGranMem(intptr_t /*pool_id*/, void *ptr, size_t bytes)
{
    ASSERT(!(bytes%currGranularity), "Region size mismatch granularity.");
    free(ptr);
    return 0;
}

static void TestPoolGranularity()
{
    rml::MemPoolPolicy pol;
    const size_t grans[] = {4*1024, 2*1024*1024, 6*1024*1024, 10*1024*1024};

    pol.pAlloc = getGranMem;
    pol.pFree = putGranMem;
    for (unsigned i=0; i<sizeof(grans)/sizeof(grans[0]); i++) {
        pol.granularity = currGranularity = grans[i];
        rml::MemoryPool *pool = pool_create(0, &pol);
        for (int sz=500*1024; sz<16*1024*1024; sz+=101*1024) {
            void *p = pool_malloc(pool, sz);
            ASSERT(p, "Can't allocate memory in pool.");
            pool_free(pool, p);
        }
        pool_destroy(pool);
    }
}

int TestMain () {
    TestTooSmallBuffer();
    TestPoolReset();
    TestSharedPool();
    TestCrossThreadPools();
    TestFixedBufferPool();
    TestPoolGranularity();

    return Harness::Done;
}
