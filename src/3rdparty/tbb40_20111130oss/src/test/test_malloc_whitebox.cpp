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

#include "harness.h"
#include "harness_barrier.h"

// To not depends on ITT support stuff
#ifdef DO_ITT_NOTIFY
#undef DO_ITT_NOTIFY
#endif

#define protected public
#define private public
#include "../tbbmalloc/frontend.cpp"
#undef protected
#undef private
#include "../tbbmalloc/backend.cpp"
#include "../tbbmalloc/backref.cpp"
#include "../tbbmalloc/large_objects.cpp"
#undef __TBB_DYNAMIC_LOAD_ENABLED // to prevent loading dynamic TBBmalloc
#include "../tbbmalloc/tbbmalloc.cpp"

const int LARGE_MEM_SIZES_NUM = 10;
const size_t MByte = 1024*1024;

class AllocInfo {
    int *p;
    int val;
    int size;
public:
    AllocInfo() : p(NULL), val(0), size(0) {}
    explicit AllocInfo(int size) : p((int*)scalable_malloc(size*sizeof(int))),
                                   val(rand()), size(size) {
        ASSERT(p, NULL);
        for (int k=0; k<size; k++)
            p[k] = val;
    }
    void check() const {
        for (int k=0; k<size; k++)
            ASSERT(p[k] == val, NULL);
    }
    void clear() {
        scalable_free(p);
    }
};

class SimpleBarrier: NoAssign {
protected:
    static Harness::SpinBarrier barrier;
public:
    static void initBarrier(unsigned thrds) { barrier.initialize(thrds); }
};

Harness::SpinBarrier SimpleBarrier::barrier;

class TestLargeObjCache: public SimpleBarrier {
public:
    static int largeMemSizes[LARGE_MEM_SIZES_NUM];

    TestLargeObjCache( ) {}

    void operator()( int /*mynum*/ ) const {
        AllocInfo allocs[LARGE_MEM_SIZES_NUM];

        // push to maximal cache limit
        for (int i=0; i<2; i++) {
            const int sizes[] = { MByte/sizeof(int),
                                  (MByte-2*largeBlockCacheStep)/sizeof(int) };
            for (int q=0; q<2; q++) {
                size_t curr = 0;
                for (int j=0; j<LARGE_MEM_SIZES_NUM; j++, curr++)
                    new (allocs+curr) AllocInfo(sizes[q]);

                for (size_t j=0; j<curr; j++) {
                    allocs[j].check();
                    allocs[j].clear();
                }
            }
        }
        
        barrier.wait();

        // check caching correctness
        for (int i=0; i<1000; i++) {
            size_t curr = 0;
            for (int j=0; j<LARGE_MEM_SIZES_NUM-1; j++, curr++)
                new (allocs+curr) AllocInfo(largeMemSizes[j]);

            new (allocs+curr) 
                AllocInfo((int)(4*minLargeObjectSize +
                                2*minLargeObjectSize*(1.*rand()/RAND_MAX)));
            curr++;

            for (size_t j=0; j<curr; j++) {
                allocs[j].check();
                allocs[j].clear();
            }
        }
    }
};

int TestLargeObjCache::largeMemSizes[LARGE_MEM_SIZES_NUM];

#if MALLOC_CHECK_RECURSION

class TestStartupAlloc: public SimpleBarrier {
    struct TestBlock {
        void *ptr;
        size_t sz;
    };
    static const int ITERS = 100;
public:
    TestStartupAlloc() {}
    void operator()(int) const {
        TestBlock blocks1[ITERS], blocks2[ITERS];

        barrier.wait();

        for (int i=0; i<ITERS; i++) {
            blocks1[i].sz = rand() % minLargeObjectSize;
            blocks1[i].ptr = StartupBlock::allocate(blocks1[i].sz);
            ASSERT(blocks1[i].ptr && StartupBlock::msize(blocks1[i].ptr)>=blocks1[i].sz 
                   && 0==(uintptr_t)blocks1[i].ptr % sizeof(void*), NULL);
            memset(blocks1[i].ptr, i, blocks1[i].sz);
        }
        for (int i=0; i<ITERS; i++) {
            blocks2[i].sz = rand() % minLargeObjectSize;
            blocks2[i].ptr = StartupBlock::allocate(blocks2[i].sz);
            ASSERT(blocks2[i].ptr && StartupBlock::msize(blocks2[i].ptr)>=blocks2[i].sz 
                   && 0==(uintptr_t)blocks2[i].ptr % sizeof(void*), NULL);
            memset(blocks2[i].ptr, i, blocks2[i].sz);

            for (size_t j=0; j<blocks1[i].sz; j++)
                ASSERT(*((char*)blocks1[i].ptr+j) == i, NULL);
            Block *block = (Block *)alignDown(blocks1[i].ptr, blockSize);
            ((StartupBlock *)block)->free(blocks1[i].ptr);
        }
        for (int i=ITERS-1; i>=0; i--) {
            for (size_t j=0; j<blocks2[i].sz; j++)
                ASSERT(*((char*)blocks2[i].ptr+j) == i, NULL);
            Block *block = (Block *)alignDown(blocks2[i].ptr, blockSize);
            ((StartupBlock *)block)->free(blocks2[i].ptr);
        }
    }
};

#endif /* MALLOC_CHECK_RECURSION */

class BackRefWork: NoAssign {
    struct TestBlock {
        intptr_t   data;
        BackRefIdx idx;
    };
    static const int ITERS = 2*BR_MAX_CNT+2;
public:
    BackRefWork() {}
    void operator()(int) const {
        TestBlock blocks[ITERS];

        for (int i=0; i<ITERS; i++) {
            blocks[i].idx = BackRefIdx::newBackRef(/*largeObj=*/false);
            setBackRef(blocks[i].idx, &blocks[i].data);
        }
        for (int i=0; i<ITERS; i++)
            ASSERT((Block*)&blocks[i].data == getBackRef(blocks[i].idx), NULL);
        for (int i=ITERS-1; i>=0; i--)
            removeBackRef(blocks[i].idx);
    }
};

class FreeBlockPoolHit: NoAssign {
    // to trigger possible leak for both cleanup on pool overflow 
    // and on thread termination
    static const int ITERS = 2*FreeBlockPool::POOL_HIGH_MARK;
public:
    FreeBlockPoolHit() {}
    void operator()(int) const {
        void *objs[ITERS];

        for (int i=0; i<ITERS; i++)
            objs[i] = scalable_malloc(minLargeObjectSize-1);
        for (int i=0; i<ITERS; i++)
            scalable_free(objs[i]);

#ifdef USE_WINTHREAD
        // under Windows DllMain used to call mallocThreadShutdownNotification,
        // as we don't use it have to call the callback manually
        mallocThreadShutdownNotification(NULL);
#endif
    }
};

static size_t allocatedBackRefCount()
{
    size_t cnt = 0;
    for (int i=0; i<=backRefMaster->lastUsed; i++)
        cnt += backRefMaster->backRefBl[i]->allocatedCount;
    return cnt;
}

static void cleanObjectCache()
{
    defaultMemPool->extMemPool.hardCachesCleanup();
}

class TestInvalidBackrefs: public SimpleBarrier {
    static const int BACKREF_GROWTH_ITERS = 200*1024;

    static tbb::atomic<bool> backrefGrowthDone;
    static void *ptrs[BACKREF_GROWTH_ITERS];
public:
    TestInvalidBackrefs() {}
    void operator()(int id) const {

        if (!id) {
            backrefGrowthDone = false;
            barrier.wait();

            for (int i=0; i<BACKREF_GROWTH_ITERS; i++)
                ptrs[i] = scalable_malloc(minLargeObjectSize);
            backrefGrowthDone = true;
            for (int i=0; i<BACKREF_GROWTH_ITERS; i++)
                scalable_free(ptrs[i]);
        } else {
            void *p2 = scalable_malloc(minLargeObjectSize-1);
            char *p1 = (char*)scalable_malloc(minLargeObjectSize-1);
            LargeObjectHdr *hdr =
                (LargeObjectHdr*)(p1+minLargeObjectSize-1 - sizeof(LargeObjectHdr));
            hdr->backRefIdx.master = 7;
            hdr->backRefIdx.largeObj = 1;
            hdr->backRefIdx.offset = 2000;

            barrier.wait();

            while (!backrefGrowthDone) {
                scalable_free(p2);
                p2 = scalable_malloc(minLargeObjectSize-1);
            }
            scalable_free(p1);
            scalable_free(p2);
        }
    }
};

tbb::atomic<bool> TestInvalidBackrefs::backrefGrowthDone;
void *TestInvalidBackrefs::ptrs[BACKREF_GROWTH_ITERS];

void TestBackRef() {
    size_t beforeNumBackRef, afterNumBackRef;

    beforeNumBackRef = allocatedBackRefCount();
    for( int p=MaxThread; p>=MinThread; --p )
        NativeParallelFor( p, BackRefWork() );
    afterNumBackRef = allocatedBackRefCount();
    ASSERT(beforeNumBackRef==afterNumBackRef, "backreference leak detected");

    // lastUsed marks peak resource consumption. As we allocate below the mark,
    // it must not move up, otherwise there is a resource leak.
    int sustLastUsed = backRefMaster->lastUsed;
    NativeParallelFor( 1, BackRefWork() );
    ASSERT(sustLastUsed == backRefMaster->lastUsed, "backreference leak detected");
    
    // check leak of back references while per-thread small object pool is in use
    // warm up need to cover bootStrapMalloc call
    NativeParallelFor( 1, FreeBlockPoolHit() );
    beforeNumBackRef = allocatedBackRefCount();
    NativeParallelFor( 1, FreeBlockPoolHit() );
    afterNumBackRef = allocatedBackRefCount();
    ASSERT(beforeNumBackRef==afterNumBackRef, "backreference leak detected");

    // This is a regression test against race condition between backreference
    // extension and checking invalid BackRefIdx.
    // While detecting is object large or small, scalable_free 1st check for
    // large objects, so there is a chance to prepend small object with
    // seems valid BackRefIdx for large objects, and thus trigger the bug.
    TestInvalidBackrefs::initBarrier(MaxThread);
    NativeParallelFor( MaxThread, TestInvalidBackrefs() );
}

void *getMem(intptr_t /*pool_id*/, size_t &bytes)
{
    const size_t BUF_SIZE = 8*1024*1024;
    static char space[BUF_SIZE];
    static size_t pos;

    if (pos + bytes > BUF_SIZE)
        return NULL;

    void *ret = space + pos;
    pos += bytes;

    return ret;
}

int putMem(intptr_t /*pool_id*/, void* /*raw_ptr*/, size_t /*raw_bytes*/)
{
    return 0;
}

struct MallocPoolHeader {
    void  *rawPtr;
    size_t userSize;
};

void *getMallocMem(intptr_t /*pool_id*/, size_t &bytes)
{
    void *rawPtr = malloc(bytes+sizeof(MallocPoolHeader));
    void *ret = (void *)((uintptr_t)rawPtr+sizeof(MallocPoolHeader));

    MallocPoolHeader *hdr = (MallocPoolHeader*)ret-1;
    hdr->rawPtr = rawPtr;
    hdr->userSize = bytes;

    return ret;
}

int putMallocMem(intptr_t /*pool_id*/, void *ptr, size_t bytes)
{
    MallocPoolHeader *hdr = (MallocPoolHeader*)ptr-1;
    ASSERT(bytes == hdr->userSize, "Invalid size in pool callback.");
    free(hdr->rawPtr);

    return 0;
}

void TestPools() {
    rml::MemPoolPolicy pol;
    size_t beforeNumBackRef, afterNumBackRef;

    pol.pAlloc = getMem;
    pol.pFree = putMem;
    pol.granularity = 8;
    rml::MemoryPool *pool1 = pool_create(0, &pol);
    rml::MemoryPool *pool2 = pool_create(0, &pol);
    pool_destroy(pool1);
    pool_destroy(pool2);

    cleanObjectCache();
    beforeNumBackRef = allocatedBackRefCount();
    rml::MemoryPool *fixedPool = pool_create(0, &pol);

    pol.pAlloc = getMallocMem;
    pol.pFree = putMallocMem;
    pol.granularity = 8;
    rml::MemoryPool *mallocPool = pool_create(0, &pol);

/* check that large object cache (LOC) returns correct size for cached objects
   passBackendSz Byte objects are cached in LOC, but bypassed the backend, so
   memory requested directly from allocation callback.
   nextPassBackendSz Byte objects must fit to another LOC bin,
   so that their allocation/realeasing leads to cache cleanup.
   All this is expecting to lead to releasing of passBackendSz Byte object
   from LOC during LOC cleanup, and putMallocMem checks that returned size
   is correct.
*/
    const size_t passBackendSz = Backend::maxBinedSize+1,
        anotherLOCBinSz = minLargeObjectSize+1;
    for (int i=0; i<10; i++) { // run long enough to be cached
        void *p = pool_malloc(mallocPool, passBackendSz);
        ASSERT(p, "Memory was not allocated");
        pool_free(mallocPool, p);
    }
    // run long enough to passBackendSz allocation was cleaned from cache
    // and returned back to putMallocMem for size checking
    for (int i=0; i<1000; i++) {
        void *p = pool_malloc(mallocPool, anotherLOCBinSz);
        ASSERT(p, "Memory was not allocated");
        pool_free(mallocPool, p);
    }

    void *smallObj =  pool_malloc(fixedPool, 10);
    memset(smallObj, 1, 10);
    void *ptr = pool_malloc(fixedPool, 1024);
    memset(ptr, 1, 1024);
    void *largeObj = pool_malloc(fixedPool, minLargeObjectSize);
    memset(largeObj, 1, minLargeObjectSize);
    ptr = pool_malloc(fixedPool, minLargeObjectSize);
    memset(ptr, minLargeObjectSize, minLargeObjectSize);
    pool_malloc(fixedPool, 10*minLargeObjectSize); // no leak for unsuccesful allocations
    pool_free(fixedPool, smallObj);
    pool_free(fixedPool, largeObj);

    // provoke large object cache cleanup and hope no leaks occurs
    for (size_t sz=minLargeObjectSize; sz<1*1024*1024; sz+=largeBlockCacheStep) {
        ptr = pool_malloc(mallocPool, sz);
        memset(ptr, sz, sz);
        pool_free(mallocPool, ptr);
    }
    pool_destroy(mallocPool);
    pool_destroy(fixedPool);

    cleanObjectCache();
    afterNumBackRef = allocatedBackRefCount();
    ASSERT(beforeNumBackRef==afterNumBackRef, "backreference leak detected");
}

void TestObjectRecognition() {
    size_t headersSize = sizeof(LargeMemoryBlock)+sizeof(LargeObjectHdr);
    unsigned falseObjectSize = 113; // unsigned is the type expected by getObjectSize
    size_t obtainedSize;

    ASSERT(sizeof(BackRefIdx)==4, "Unexpected size of BackRefIdx");
    ASSERT(getObjectSize(falseObjectSize)!=falseObjectSize, "Error in test: bad choice for false object size");

    void* mem = scalable_malloc(2*blockSize);
    Block* falseBlock = (Block*)alignUp((uintptr_t)mem, blockSize);
    falseBlock->objectSize = falseObjectSize;
    char* falseSO = (char*)falseBlock + falseObjectSize*7;
    ASSERT(alignDown(falseSO, blockSize)==(void*)falseBlock, "Error in test: false object offset is too big");

    void* bufferLOH = scalable_malloc(2*blockSize + headersSize);
    LargeObjectHdr* falseLO = 
        (LargeObjectHdr*)alignUp((uintptr_t)bufferLOH + headersSize, blockSize);
    LargeObjectHdr* headerLO = (LargeObjectHdr*)falseLO-1;
    headerLO->memoryBlock = (LargeMemoryBlock*)bufferLOH;
    headerLO->memoryBlock->unalignedSize = 2*blockSize + headersSize;
    headerLO->memoryBlock->objectSize = blockSize + headersSize;
    headerLO->backRefIdx = BackRefIdx::newBackRef(/*largeObj=*/true);
    setBackRef(headerLO->backRefIdx, headerLO);
    ASSERT(scalable_msize(falseLO) == blockSize + headersSize,
           "Error in test: LOH falsification failed");
    removeBackRef(headerLO->backRefIdx);

    const int NUM_OF_IDX = BR_MAX_CNT+2;
    BackRefIdx idxs[NUM_OF_IDX];
    for (int cnt=0; cnt<2; cnt++) {
        for (int master = -10; master<10; master++) {
            falseBlock->backRefIdx.master = (uint16_t)master;
            headerLO->backRefIdx.master = (uint16_t)master;
        
            for (int bl = -10; bl<BR_MAX_CNT+10; bl++) {
                falseBlock->backRefIdx.offset = (uint16_t)bl;
                headerLO->backRefIdx.offset = (uint16_t)bl;

                for (int largeObj = 0; largeObj<2; largeObj++) {
                    falseBlock->backRefIdx.largeObj = largeObj;
                    headerLO->backRefIdx.largeObj = largeObj;

                    obtainedSize = safer_scalable_msize(falseSO, NULL);
                    ASSERT(obtainedSize==0, "Incorrect pointer accepted");
                    obtainedSize = safer_scalable_msize(falseLO, NULL);
                    ASSERT(obtainedSize==0, "Incorrect pointer accepted");
                }
            }
        }
        if (cnt == 1) {
            for (int i=0; i<NUM_OF_IDX; i++)
                removeBackRef(idxs[i]);
            break;
        }
        for (int i=0; i<NUM_OF_IDX; i++) {
            idxs[i] = BackRefIdx::newBackRef(/*largeObj=*/false);
            setBackRef(idxs[i], NULL);
        }
    }
    char *smallPtr = (char*)scalable_malloc(falseObjectSize);
    obtainedSize = safer_scalable_msize(smallPtr, NULL);
    ASSERT(obtainedSize==getObjectSize(falseObjectSize), "Correct pointer not accepted?");
    scalable_free(smallPtr);

    obtainedSize = safer_scalable_msize(mem, NULL);
    ASSERT(obtainedSize>=2*blockSize, "Correct pointer not accepted?");
    scalable_free(mem);
    scalable_free(bufferLOH);
}

class TestBackendWork: public SimpleBarrier {
    struct TestBlock {
        intptr_t   data;
        BackRefIdx idx;
    };
    static const int ITERS = 100;

    rml::internal::Backend *backend;
public:
    TestBackendWork(rml::internal::Backend *backend) : backend(backend) {}
    void operator()(int) const {
        barrier.wait();
        
        for (int i=0; i<ITERS; i++) {
            BlockI *block16K = backend->get16KBlock(1, /*startup=*/false);
            LargeMemoryBlock *lmb = backend->getLargeBlock(16*1024, /*startup=*/false);
            backend->put16KBlock(block16K, /*startup=*/false);
            backend->putLargeBlock(lmb);
        }
    }
};

void TestBackend()
{
    rml::MemPoolPolicy pol;

    pol.pAlloc = getMallocMem;
    pol.pFree = putMallocMem;
    pol.granularity = 8;
    rml::MemoryPool *mPool = pool_create(0, &pol);
    rml::internal::ExtMemoryPool *ePool = 
        &((rml::internal::MemoryPool*)mPool)->extMemPool;
    rml::internal::Backend *backend = &ePool->backend;

    for( int p=MaxThread; p>=MinThread; --p ) {
        TestBackendWork::initBarrier(p);
        NativeParallelFor( p, TestBackendWork(backend) );
    }

    BlockI *block = backend->get16KBlock(1, /*startup=*/false);
    backend->put16KBlock(block, /*startup=*/false);

    pool_destroy(mPool);
}

void TestBitMask()
{
    BitMask<256> mask;

    mask.reset();
    mask.set(10, 1);
    mask.set(5, 1);
    mask.set(1, 1);
    ASSERT(mask.getMinTrue(2) == 5, NULL);

    mask.reset();
    mask.set(0, 1);
    mask.set(64, 1);
    mask.set(63, 1);
    mask.set(200, 1);
    mask.set(255, 1);
    ASSERT(mask.getMinTrue(0) == 0, NULL);
    ASSERT(mask.getMinTrue(1) == 63, NULL);
    ASSERT(mask.getMinTrue(63) == 63, NULL);
    ASSERT(mask.getMinTrue(64) == 64, NULL);
    ASSERT(mask.getMinTrue(101) == 200, NULL);
    ASSERT(mask.getMinTrue(201) == 255, NULL);
    mask.set(255, 0);
    ASSERT(mask.getMinTrue(201) == -1, NULL);
}

int TestMain () {
    // backreference requires that initialization was done
    if(!isMallocInitialized()) doInitialization();
    // to succeed, leak detection must be the 1st memory-intensive test
    TestBackRef();
    TestPools();
    TestBackend();

#if MALLOC_CHECK_RECURSION
    for( int p=MaxThread; p>=MinThread; --p ) {
        TestStartupAlloc::initBarrier( p );
        NativeParallelFor( p, TestStartupAlloc() );
        ASSERT(!firstStartupBlock, "Startup heap memory leak detected");
    }
#endif

    for (int i=0; i<LARGE_MEM_SIZES_NUM; i++)
        TestLargeObjCache::largeMemSizes[i] = 
            (int)(minLargeObjectSize + 2*minLargeObjectSize*(1.*rand()/RAND_MAX));

    for( int p=MaxThread; p>=MinThread; --p ) {
        TestLargeObjCache::initBarrier( p );
        NativeParallelFor( p, TestLargeObjCache() );
    }

    TestObjectRecognition();
    TestBitMask();
    return Harness::Done;
}
