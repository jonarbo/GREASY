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

#ifndef __TBB_tbbmalloc_internal_H
#define __TBB_tbbmalloc_internal_H 1


#include "TypeDefinitions.h" /* Also includes customization layer Customize.h */

#if USE_PTHREAD
    // Some pthreads documentation says that <pthreads.h> must be first header.
    #include <pthread.h>
    typedef pthread_key_t tls_key_t;
#elif USE_WINTHREAD
    #include "tbb/machine/windows_api.h"
    typedef DWORD tls_key_t;
#else
    #error Must define USE_PTHREAD or USE_WINTHREAD
#endif

#include <stdio.h>
#include <stdlib.h>
#include <limits.h> // for CHAR_BIT
#if MALLOC_CHECK_RECURSION
#include <new>        /* for placement new */
#endif

#if __sun || __SUNPRO_CC
#define __asm__ asm
#endif

extern "C" {
    void mallocThreadShutdownNotification(void*);
}

/********* Various compile-time options        **************/


#define MALLOC_TRACE 0

#if MALLOC_TRACE
#define TRACEF(x) printf x
#else
#define TRACEF(x) ((void)0)
#endif /* MALLOC_TRACE */

#define ASSERT_TEXT NULL

#define COLLECT_STATISTICS MALLOC_DEBUG && defined(MALLOCENV_COLLECT_STATISTICS)
#include "Statistics.h"

/********* End compile-time options        **************/

namespace rml {

#if !MEM_POLICY_DEFINED
typedef void *(*rawAllocType)(intptr_t pool_id, size_t &bytes);
typedef int   (*rawFreeType)(intptr_t pool_id, void* raw_ptr, size_t raw_bytes);

struct MemPoolPolicy {
    rawAllocType pAlloc;        // pAlloc and pFree must be thread-safe
    rawFreeType  pFree;
    size_t       granularity;   // granularity of pAlloc allocations
    void        *pReserved;     // reserved for future extensions
    size_t       szReserved;    // size of pReserved data
};
#endif

namespace internal {

/********** Various numeric parameters controlling allocations ********/

/*
 * blockSize - the size of a block, it must be larger than maxSegregatedObjectSize.
 *
 */
const uintptr_t blockSize = 16*1024;

/*
 * Difference between object sizes in large block bins
 */
const uint32_t largeBlockCacheStep = 8*1024;

/*
 * Large blocks cache cleanup frequency.
 * It should be power of 2 for the fast checking.
 */
const unsigned cacheCleanupFreq = 256;

 /*
  * The number of bins to cache large objects.
  */
const uint32_t numLargeBlockBins = 1024; // for 1024 max cached size is near 8MB

/********** End of numeric parameters controlling allocations *********/

class BlockI;
struct LargeMemoryBlock;
struct ExtMemoryPool;
struct MemRegion;
class FreeBlock;
class TLSData;
class Backend;
class MemoryPool;

class TLSKey {
    tls_key_t TLS_pointer_key;
public:
    TLSKey();
   ~TLSKey();
    TLSData* getThreadMallocTLS() const;
    void setThreadMallocTLS( TLSData * newvalue );
    TLSData* createTLS(MemoryPool *memPool, Backend *backend);
};

class CachedLargeBlocksL {
    LargeMemoryBlock *first,
                     *last;
    /* age of an oldest block in the list; equal to last->age, if last defined,
       used for quick cheching it without acquiring the lock. */
    uintptr_t     oldest;
    /* currAge when something was excluded out of list because of the age,
       not because of cache hit */
    uintptr_t     lastCleanedAge;
    /* Current threshold value for the blocks of a particular size.
       Set on cache miss. */
    intptr_t      ageThreshold;

    MallocMutex   lock;
    /* CachedBlocksList should be placed in zero-initialized memory,
       ctor not needed. */
    CachedLargeBlocksL();
public:
    inline bool push(ExtMemoryPool *extMemPool, LargeMemoryBlock* ptr);
    inline LargeMemoryBlock* pop(ExtMemoryPool *extMemPool);
    bool releaseLastIfOld(ExtMemoryPool *extMemPool, uintptr_t currAge);
    bool releaseAll(ExtMemoryPool *extMemPool);
};

class BackRefIdx { // composite index to backreference array
private:
    uint16_t master;      // index in BackRefMaster
    uint16_t largeObj:1;  // is this object "large"?
    uint16_t offset  :15; // offset from beginning of BackRefBlock
public:
    BackRefIdx() : master((uint16_t)-1) {}
    bool isInvalid() const { return master == (uint16_t)-1; }
    bool isLargeObject() const { return largeObj; }
    uint16_t getMaster() const { return master; }
    uint16_t getOffset() const { return offset; }

    // only newBackRef can modify BackRefIdx
    static BackRefIdx newBackRef(bool largeObj);
};

// Block header is used during block coalescing
// and must be preserved in used blocks.
class BlockI {
    intptr_t     blockState[2];
};

struct LargeMemoryBlock : public BlockI {
    LargeMemoryBlock *next,          // ptrs in list of cached blocks
                     *prev,
    // 2-linked list of pool's large objects
    // Used to destroy backrefs on pool destroy/reset (backrefs are global).
                     *gPrev,
                     *gNext;
    uintptr_t         age;           // age of block while in cache
    size_t            objectSize;    // the size requested by a client
    size_t            unalignedSize; // the size requested from getMemory
    // Is the block from getRawMem or from bin?
    // It can't be detected reliable via size, because sometimes
    // objects larger then Backend::maxBinedSize can be allocated from bins.
    bool              directRawMemCall;
    BackRefIdx        backRefIdx;    // cached here, used copy is in LargeObjectHdr
    void registerInPool(ExtMemoryPool *extMemPool);
    void unregisterFromPool(ExtMemoryPool *extMemPool);
};

// global state of blocks currently in processing
class ProcBlocks {
    // number of blocks currently removed from one bin and not returned
    intptr_t  blocksInProcessing;  // to another
    intptr_t  binsModifications;   // incremented on every bin modification
public:
    void consume() { AtomicIncrement(blocksInProcessing); }
    void pureSignal() { AtomicIncrement(binsModifications); }
    void signal() {
        MALLOC_ITT_SYNC_RELEASING(&blocksInProcessing);
        AtomicIncrement(binsModifications);
        intptr_t prev = AtomicAdd(blocksInProcessing, -1);
        MALLOC_ASSERT(prev > 0, ASSERT_TEXT);
    }
    intptr_t getNumOfMods() const { return FencedLoad(binsModifications); }
    // return true if need re-do the search
    bool waitTillSignalled(intptr_t startModifiedCnt) {
        intptr_t myBlocksNum = FencedLoad(blocksInProcessing);
        if (!myBlocksNum) {
            // no threads, but were bins modified since scanned?
            return startModifiedCnt != getNumOfMods();
        }
        MALLOC_ITT_SYNC_PREPARE(&blocksInProcessing);
        for (;;) {
            SpinWaitWhileEq(blocksInProcessing, myBlocksNum);
            if (myBlocksNum > blocksInProcessing)
                break;
            myBlocksNum = FencedLoad(blocksInProcessing);
        }
        MALLOC_ITT_SYNC_ACQUIRED(&blocksInProcessing);
        return true;
    }
};

class CoalRequestQ { // queue of free blocks that coalescing was delayed
    FreeBlock *blocksToFree;
public:
    FreeBlock *getAll(); // return current list of blocks and make queue empty
    void putBlock(FreeBlock *fBlock);
};

template<unsigned NUM>
class BitMask {
    static const int SZ = NUM/( CHAR_BIT*sizeof(uintptr_t)) + (NUM % sizeof(uintptr_t) ? 1:0);
    static const unsigned WORD_LEN = CHAR_BIT*sizeof(uintptr_t);
    uintptr_t mask[SZ];
public:
    void set(size_t idx, bool val) {
        MALLOC_ASSERT(idx<NUM, ASSERT_TEXT);

        size_t i = idx / WORD_LEN;
        int pos = WORD_LEN - idx % WORD_LEN - 1;
        if (val)
            AtomicOr(&mask[i], 1ULL << pos);
        else
            AtomicAnd(&mask[i], ~(1ULL << pos));
    }
    int getMinTrue(unsigned startIdx) const {
        size_t idx = startIdx / WORD_LEN;
        uintptr_t curr;
        int pos;

        if (startIdx % WORD_LEN) { // clear bits before startIdx
            pos = WORD_LEN - startIdx % WORD_LEN;
            curr = mask[idx] & ((1ULL<<pos) - 1);
        } else
            curr = mask[idx];

        for (int i=idx; i<SZ; i++, curr=mask[i]) {
            if (-1 != (pos = BitScanRev(curr)))
                return (i+1)*WORD_LEN - pos - 1;
        }
        return -1;
    }
    void reset() { for (int i=0; i<SZ; i++) mask[i] = 0; }
};

class MemExtendingSema {
    intptr_t     active;
public:
    bool wait() {
        bool rescanBins = false;
        // up to 3 threads can add more memory from OS simultaneously,
        // rest of threads have to wait
        for (;;) {
            intptr_t prevCnt = FencedLoad(active);
            if (prevCnt < 3) {
                intptr_t n = AtomicCompareExchange(active, prevCnt+1, prevCnt);
                if (n == prevCnt)
                    break;
            } else {
                SpinWaitWhileEq(active, prevCnt);
                rescanBins = true;
                break;
            }
        }
        return rescanBins;
    }
    void signal() { AtomicAdd(active, -1); }
};

class Backend {
public:
    static const unsigned minPower2 = 3+10;
    static const unsigned maxPower2 = 12+10;
    // 2^13 B, i.e. 8KB
    static const size_t minBinedSize = 1 << minPower2;
    // 2^22 B, i.e. 4MB
    static const size_t maxBinedSize = 1 << maxPower2;

    static const int freeBinsNum =
        (maxBinedSize-minBinedSize)/largeBlockCacheStep + 1;

    // Bin keeps 2-linked list of free blocks. It must be 2-linked
    // because during coalescing a block it's removed from a middle of the list.
    struct Bin {
        FreeBlock   *head;
        MallocMutex  tLock;

        void removeBlock(FreeBlock *fBlock);
        void reset() { head = 0; }
        size_t countFreeBlocks();
        void verify();
        bool empty() const { return !head; }
    };

    // array of bins accomplished bitmask for fast finding of non-empty bins
    class IndexedBins {
        BitMask<Backend::freeBinsNum> bitMask;
        Bin                           freeBins[Backend::freeBinsNum];
    public:
        FreeBlock *getBlock(int binIdx, ProcBlocks *procBlocks, size_t size,
                            bool res16Kaligned, bool alignedBin, bool wait,
                            int *resLocked);
        void lockRemoveBlock(int binIdx, FreeBlock *fBlock);
        void addBlock(int binIdx, FreeBlock *fBlock, size_t blockSz);
        bool tryAddBlock(int binIdx, FreeBlock *fBlock, size_t blockSz);
        int getMinNonemptyBin(unsigned startBin) const {
            int p = bitMask.getMinTrue(startBin);
            return p == -1 ? Backend::freeBinsNum : p;
        }
        void reset();
    };

private:
    ExtMemoryPool *extMemPool;
    // used for release every region on pool destroying
    MemRegion     *regionList;
    MallocMutex    regionListLock;

    CoalRequestQ   coalescQ; // queue of coalescing requests
    ProcBlocks     procBlocks;
    // semaphore protecting adding more more memory from OS
    MemExtendingSema memExtendingSema;

    // Using of maximal observed requested size allows descrease
    // memory consumption for small requests and descrease fragmentation
    // for workloads when small and large allocation requests are mixed.
    // TODO: decrease, not only increase it
    size_t         maxRequestedSize;
    void correctMaxRequestSize(size_t requestSize);

    size_t addNewRegion(size_t rawSize);
    size_t initRegion(MemRegion *region, size_t rawSize);
    void releaseRegion(MemRegion *region);

    void *genericGetBlock(int num, size_t size, bool res16Kaligned, bool startup);
    void genericPutBlock(FreeBlock *fBlock, size_t blockSz,
                         bool directRawMemCall);
    FreeBlock *getFromAlignedSpace(int binIdx, int num, size_t size, bool res16Kaligned, bool wait, int *locked);
    FreeBlock *getFromBin(int binIdx, int num, size_t size, bool res16Kaligned, int *locked);

    FreeBlock *doCoalesc(FreeBlock *fBlock, MemRegion **memRegion);
    void coalescAndPutList(FreeBlock *head, bool forceCoalescQDrop, bool doStat);
    bool scanCoalescQ(bool forceCoalescQDrop);
    void coalescAndPut(FreeBlock *fBlock, size_t blockSz);

    void removeBlockFromBin(FreeBlock *fBlock);

    void *getRawMem(size_t &size, bool useMapMem) const;
    void freeRawMem(void *object, size_t size, bool useMapMem) const;

public:
    bool bootstrap(ExtMemoryPool *extMemoryPool) {
        extMemPool = extMemoryPool;
        return addNewRegion(2*1024*1024);
    }
    bool reset();
    bool destroy();

    BlockI *get16KBlock(int num, bool startup) {
        BlockI *b = (BlockI*)
            genericGetBlock(num, blockSize, /*res16Kaligned=*/true, startup);
        MALLOC_ASSERT(isAligned(b, blockSize), ASSERT_TEXT);
        return b;
    }
    void put16KBlock(BlockI *block, bool /*startup*/) {
        genericPutBlock((FreeBlock *)block, blockSize,
                        /*directRawMemCall=*/false);
    }

    bool inUserPool() const;

    LargeMemoryBlock *getLargeBlock(size_t size, bool startup);
    void putLargeBlock(LargeMemoryBlock *lmb) {
        genericPutBlock((FreeBlock *)lmb, lmb->unalignedSize,
                        lmb->directRawMemCall);
    }

    void reportStat();

    enum {
        NO_BIN = -1
    };
private:
    static int sizeToBin(size_t size) {
        if (size >= maxBinedSize)
            return freeBinsNum-1;
        else if (size < minBinedSize)
            return NO_BIN;

        int bin = (size - minBinedSize)/largeBlockCacheStep;

        MALLOC_ASSERT(bin < freeBinsNum-1, "Invalid size.");
        return bin;
    }
    static bool toAlignedBin(FreeBlock *block, size_t size) {
        return isAligned((uintptr_t)block+size, blockSize) && size >= blockSize;
    }

    IndexedBins freeLargeBins,
                freeAlignedBins;
};

struct ExtMemoryPool {
    Backend           backend;

    intptr_t          poolId;
    // to find all large objects
    MallocMutex       largeObjLock;
    LargeMemoryBlock *loHead;
    // Callbacks to be used instead of MapMemory/UnmapMemory.
    rawAllocType      rawAlloc;
    rawFreeType       rawFree;
    size_t            granularity;
    TLSKey            tlsPointerKey;  // per-pool TLS key

    // bins with lists of recently freed large blocks cached for re-use
    CachedLargeBlocksL cachedLargeBlocks[numLargeBlockBins];

    bool doLOCacheCleanup(uintptr_t currAge);

    // i.e., not system default pool for scalable_malloc/scalable_free
    bool userPool() const { return rawAlloc; }

    bool fixedSizePool() const { return userPool() && !rawFree; }

     // true if something has beed released
    bool softCachesCleanup();
    bool release16KBCaches();
    bool hardCachesCleanup() {
        bool res = false;

        for (int i = numLargeBlockBins-1; i >= 0; i--)
            res |= cachedLargeBlocks[i].releaseAll(this);
        // TODO: to release all thread's pools, not just current thread
        res |= release16KBCaches();

        return res;
    }

    void reset() {
        // must reset LOC because all backrefs are cleaned,
        // because backrefs in LOC are invalid already
        hardCachesCleanup();
        backend.reset();
        tlsPointerKey.~TLSKey();
    }
    void destroy() {
        if (rawFree)
            backend.destroy();
        tlsPointerKey.~TLSKey();
    }
};

inline bool Backend::inUserPool() const { return extMemPool->userPool(); }

struct LargeObjectHdr {
    LargeMemoryBlock *memoryBlock;
    /* Backreference points to LargeObjectHdr.
       Duplicated in LargeMemoryBlock to reuse in subsequent allocations. */
    BackRefIdx       backRefIdx;
};

struct FreeObject {
    FreeObject  *next;
};

/******* A helper class to support overriding malloc with scalable_malloc *******/
#if MALLOC_CHECK_RECURSION

class RecursiveMallocCallProtector {
    // pointer to an automatic data of holding thread
    static void       *autoObjPtr;
    static MallocMutex rmc_mutex;
    static pthread_t   owner_thread;
/* Under FreeBSD 8.0 1st call to any pthread function including pthread_self
   leads to pthread initialization, that causes malloc calls. As 1st usage of
   RecursiveMallocCallProtector can be before pthread initialized, pthread calls
   can't be used in 1st instance of RecursiveMallocCallProtector.
   RecursiveMallocCallProtector is used 1st time in checkInitialization(),
   so there is a guarantee that on 2nd usage pthread is initialized.
   No such situation observed with other supported OSes.
 */
#if __FreeBSD__
    static bool        canUsePthread;
#else
    static const bool  canUsePthread = true;
#endif
/*
  The variable modified in checkInitialization,
  so can be read without memory barriers.
 */
    static bool mallocRecursionDetected;

    MallocMutex::scoped_lock* lock_acquired;
    char scoped_lock_space[sizeof(MallocMutex::scoped_lock)+1];

    static uintptr_t absDiffPtr(void *x, void *y) {
        uintptr_t xi = (uintptr_t)x, yi = (uintptr_t)y;
        return xi > yi ? xi - yi : yi - xi;
    }
public:

    RecursiveMallocCallProtector() : lock_acquired(NULL) {
        lock_acquired = new (scoped_lock_space) MallocMutex::scoped_lock( rmc_mutex );
        if (canUsePthread)
            owner_thread = pthread_self();
        autoObjPtr = &scoped_lock_space;
    }
    ~RecursiveMallocCallProtector() {
        if (lock_acquired) {
            autoObjPtr = NULL;
            lock_acquired->~scoped_lock();
        }
    }
    static bool sameThreadActive() {
        if (!autoObjPtr) // fast path
            return false;
        // Some thread has an active recursive call protector; check if the current one.
        // Exact pthread_self based test
        if (canUsePthread) {
            if (pthread_equal( owner_thread, pthread_self() )) {
                mallocRecursionDetected = true;
                return true;
            } else
                return false;
        }
        // inexact stack size based test
        const uintptr_t threadStackSz = 2*1024*1024;
        int dummy;
        return absDiffPtr(autoObjPtr, &dummy)<threadStackSz;
    }
    static bool noRecursion();
/* The function is called on 1st scalable_malloc call to check if malloc calls
   scalable_malloc (nested call must set mallocRecursionDetected). */
    static void detectNaiveOverload() {
        if (!malloc_proxy) {
#if __FreeBSD__
/* If !canUsePthread, we can't call pthread_self() before, but now pthread
   is already on, so can do it. False positives here lead to silent switching
   from malloc to mmap for all large allocations with bad performance impact. */
            if (!canUsePthread) {
                canUsePthread = true;
                owner_thread = pthread_self();
            }
#endif
            free(malloc(1));
        }
    }
};

#else

class RecursiveMallocCallProtector {
public:
    RecursiveMallocCallProtector() {}
    ~RecursiveMallocCallProtector() {}
};

#endif  /* MALLOC_CHECK_RECURSION */

bool isMallocInitializedExt();

extern const uint32_t minLargeObjectSize;
bool isLargeObject(void *object);
void* mallocLargeObject(ExtMemoryPool *extMemPool, size_t size, size_t alignment, bool startupAlloc = false);
void freeLargeObject(ExtMemoryPool *extMemPool, void *object);

unsigned int getThreadId();

bool initBackRefMaster(Backend *backend);
void removeBackRef(BackRefIdx backRefIdx);
void setBackRef(BackRefIdx backRefIdx, void *newPtr);
void *getBackRef(BackRefIdx backRefIdx);
void* getRawMemory(size_t size, bool useMapMem);
bool freeRawMemory(void *object, size_t size, bool useMapMem);

} // namespace internal
} // namespace rml

#endif // __TBB_tbbmalloc_internal_H
