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

#include <string.h>   /* for memset */
#include <errno.h>
#include "tbbmalloc_internal.h"

namespace rml {
namespace internal {

// If USE_MALLOC_FOR_LARGE_OBJECT is nonzero, then large allocations are done via malloc.
// Otherwise large allocations are done using the scalable allocator's block allocator.
// As of 06.Jun.17, using malloc is about 10x faster on Linux.
#if !_WIN32
#define USE_MALLOC_FOR_LARGE_OBJECT 1
#endif

/*********** Code to acquire memory from the OS or other executive ****************/

/*
  syscall/malloc can set non-zero errno in case of failure,
  but later allocator might be able to find memory to fulfil the request.
  And we do not want changing of errno by successful scalable_malloc call.
  To support this, restore old errno in (get|free)RawMemory, and set errno
  in frontend just before returning to user code.
  Please note: every syscall/libc call used inside scalable_malloc that
  sets errno must be protected this way, not just memory allocation per se.
*/

#if USE_DEFAULT_MEMORY_MAPPING
#include "MapMemory.h"
#else
/* assume MapMemory and UnmapMemory are customized */
#endif

#if USE_MALLOC_FOR_LARGE_OBJECT

// (get|free)RawMemory only necessary for the USE_MALLOC_FOR_LARGE_OBJECT case
void* getRawMemory (size_t size, bool useMapMem = false)
{
    void *object;

    if (useMapMem)
        object = MapMemory(size);
    else
#if MALLOC_CHECK_RECURSION
    if (RecursiveMallocCallProtector::noRecursion())
        object = ErrnoPreservingMalloc(size);
    else if ( rml::internal::original_malloc_found ) {
        int prevErrno = errno;
        object = (*rml::internal::original_malloc_ptr)(size);
        if (!object)
            errno = prevErrno;
    } else
        object = MapMemory(size);
#else
    object = MallocNoErrno(size);
#endif /* MALLOC_CHECK_RECURSION */

    return object;
}

bool freeRawMemory (void *object, size_t size, bool useMapMem)
{
    bool unmapFailed = false;

    if (useMapMem)
        unmapFailed = UnmapMemory(object, size);
    else
#if MALLOC_CHECK_RECURSION
    if (RecursiveMallocCallProtector::noRecursion())
        free(object);
    else if ( rml::internal::original_malloc_found )
        (*rml::internal::original_free_ptr)(object);
    else
        unmapFailed = UnmapMemory(object, size);
#else
    free(object);
#endif /* MALLOC_CHECK_RECURSION */

    return unmapFailed;
}

#else /* USE_MALLOC_FOR_LARGE_OBJECT */

void* getRawMemory (size_t size, bool = false) {
    int myErr = errno;
    void *ret = MapMemory(size);
    errno = myErr;
    return ret;
}

bool freeRawMemory (void *object, size_t size, bool) {
    return UnmapMemory(object, size);
}

#endif /* USE_MALLOC_FOR_LARGE_OBJECT */

void *Backend::getRawMem(size_t &size, bool useMapMem) const
{
    if (extMemPool->userPool()) {
        if (size_t rem = size % extMemPool->granularity) {
            size += extMemPool->granularity - rem;
        }
        MALLOC_ASSERT(0 == size % extMemPool->granularity, ASSERT_TEXT);

        return (*extMemPool->rawAlloc)(extMemPool->poolId, size);
    }
    return getRawMemory(size, useMapMem);
}

void Backend::freeRawMem(void *object, size_t size, bool useMapMem) const
{
    if (extMemPool->userPool())
        (*extMemPool->rawFree)(extMemPool->poolId, object, size);
    else
        freeRawMemory(object, size, useMapMem);
}

/********* End memory acquisition code ********************************/

// Protected object size. After successful locking returns size of locked block.
// and releasing requires setting block size.
class GuardedSize : tbb::internal::no_copy {
    uintptr_t value;
public:
    enum State {
        LOCKED,
        COAL_BLOCK,        // block is coalescing now
        MAX_LOCKED_VAL = COAL_BLOCK,
        LAST_REGION_BLOCK, // used to mark last block in region
        // values after this are "normal" block sizes
        MAX_SPEC_VAL = LAST_REGION_BLOCK
    };

    void initLocked() { value = LOCKED; }
    void makeCoalscing() {
        MALLOC_ASSERT(value == LOCKED, ASSERT_TEXT);
        value = COAL_BLOCK;
    }
    size_t tryLock(State state) {
        size_t szVal, sz;
        MALLOC_ASSERT(state <= MAX_LOCKED_VAL, ASSERT_TEXT);
        for (;;) {
            sz = FencedLoad((intptr_t&)value);
            if (sz <= MAX_LOCKED_VAL)
                break;
            szVal = AtomicCompareExchange((intptr_t&)value, state, sz);

            if (szVal==sz)
                break;
        }
        return sz;
    }
    void unlock(size_t size) {
        MALLOC_ASSERT(value <= MAX_LOCKED_VAL, "The lock is not locked");
        MALLOC_ASSERT(size > MAX_LOCKED_VAL, ASSERT_TEXT);
        FencedStore((intptr_t&)value, size);
    }
};

struct MemRegion {
    MemRegion *next,    // keep all regions in any pool to release all them on
              *prev;    // pool destroying, 2-linked list to release individual
                        // regions.
    size_t     allocSz, // got from poll callback
               blockSz; // initial and maximal inner block size
};

// this data must be unmodified while block is in use, so separate it
class BlockMutexes {
protected:
    GuardedSize myL,   // lock for me
                leftL; // lock for left neighbor
};

class FreeBlock : BlockMutexes {
public:
    static const size_t minBlockSize;

    FreeBlock    *prev,       // in 2-linked list related to bin
                 *next,
                 *nextToFree; // used to form a queue during coalescing
    // valid only when block is in processing, i.e. one is not free and not
    size_t        sizeTmp;    // used outside of backend
    int           myBin;      // bin that is owner of the block
    bool          aligned;
    bool          blockInBin; // this block in myBin already

    FreeBlock *rightNeig(size_t sz) const {
        MALLOC_ASSERT(sz, ASSERT_TEXT);
        return (FreeBlock*)((uintptr_t)this+sz);
    }
    FreeBlock *leftNeig(size_t sz) const {
        MALLOC_ASSERT(sz, ASSERT_TEXT);
        return (FreeBlock*)((uintptr_t)this - sz);
    }

    void initHeader() { myL.initLocked(); leftL.initLocked(); }
    void setMeFree(size_t size) { myL.unlock(size); }
    size_t trySetMeUsed(GuardedSize::State s) { return myL.tryLock(s); }

    void setLeftFree(size_t sz) { leftL.unlock(sz); }
    size_t trySetLeftUsed(GuardedSize::State s) { return leftL.tryLock(s); }

    size_t tryLockBlock() {
        size_t rSz, sz = trySetMeUsed(GuardedSize::LOCKED);

        if (sz <= GuardedSize::MAX_LOCKED_VAL)
            return false;
        rSz = rightNeig(sz)->trySetLeftUsed(GuardedSize::LOCKED);
        if (rSz <= GuardedSize::MAX_LOCKED_VAL) {
            setMeFree(sz);
            return false;
        }
        MALLOC_ASSERT(rSz == sz, ASSERT_TEXT);
        return sz;
    }
    void markCoalescing(size_t blockSz) {
        myL.makeCoalscing();
        rightNeig(blockSz)->leftL.makeCoalscing();
        sizeTmp = blockSz;
        nextToFree = NULL;
    }
    void markUsed() {
        myL.initLocked();
        rightNeig(sizeTmp)->leftL.initLocked();
        nextToFree = NULL;
    }
    static void markBlocks(FreeBlock *fBlock, int num, size_t size) {
        for (int i=1; i<num; i++) {
            fBlock = (FreeBlock*)((uintptr_t)fBlock + size);
            fBlock->initHeader();
        }
    }
};

// Last block in any region. Its "size" field is GuardedSize::LAST_REGION_BLOCK,
// This kind of blocks used to find region header
// and have a possibility to return region back to OS
struct LastFreeBlock : public FreeBlock {
    MemRegion *memRegion;
};

const size_t FreeBlock::minBlockSize = sizeof(FreeBlock);

void CoalRequestQ::putBlock(FreeBlock *fBlock)
{
    MALLOC_ASSERT(fBlock->sizeTmp >= FreeBlock::minBlockSize, ASSERT_TEXT);
    fBlock->markUsed();

    for (;;) {
        FreeBlock *myBlToFree = (FreeBlock*)FencedLoad((intptr_t&)blocksToFree);

        fBlock->nextToFree = myBlToFree;
        if (myBlToFree ==
            (FreeBlock*)AtomicCompareExchange((intptr_t&)blocksToFree,
                                              (intptr_t)fBlock,
                                              (intptr_t)myBlToFree))
            return;
    }
}

FreeBlock *CoalRequestQ::getAll()
{
    for (;;) {
        FreeBlock *myBlToFree = (FreeBlock*)FencedLoad((intptr_t&)blocksToFree);

        if (!myBlToFree)
            return NULL;
        else {
            if (myBlToFree ==
                (FreeBlock*)AtomicCompareExchange((intptr_t&)blocksToFree,
                                                  0, (intptr_t)myBlToFree))
                return myBlToFree;
            else
                continue;
        }
    }
}

// try to remove block from bin; if split result stay in the bin, not remove it
// but split the block
// alignedBin, if the bin is 16KB-aligned right side.
FreeBlock *Backend::IndexedBins::getBlock(int binIdx, ProcBlocks *procBlocks,
                size_t size, bool res16Kaligned, bool alignedBin, bool wait,
                int *binLocked)
{
    Bin *b = &freeBins[binIdx];
try_next:
    FreeBlock *fBlock = NULL;
    if (b->head) {
        bool locked;
        MallocMutex::scoped_lock scopedLock(b->tLock, wait, &locked);

        if (!locked) {
            if (binLocked) (*binLocked)++;
            return NULL;
        }

        for (FreeBlock *curr = b->head; curr; curr = curr->next) {
            size_t szBlock = curr->tryLockBlock();
            if (!szBlock) {
                goto try_next;
            }

            if (alignedBin || !res16Kaligned) {
                size_t splitSz = szBlock - size;
                // If we got a block as split result,
                // it must have a room for control structures.
                if (szBlock >= size && (splitSz >= FreeBlock::minBlockSize ||
                                        !splitSz))
                    fBlock = curr;
            } else {
                void *newB = alignUp(curr, blockSize);
                uintptr_t rightNew = (uintptr_t)newB + size;
                uintptr_t rightCurr = (uintptr_t)curr + szBlock;
                // appropriate size, and left and right split results
                // are either big enough or non-exitent
                if (rightNew <= rightCurr
                    && (newB==curr ||
                        (uintptr_t)newB-(uintptr_t)curr >= FreeBlock::minBlockSize)
                    && (rightNew==rightCurr ||
                        rightCurr - rightNew >= FreeBlock::minBlockSize))
                    fBlock = curr;
            }
            if (fBlock) {
                // consume must be called before result of removing from a bin
                // is visible externally.
                procBlocks->consume();
                if (alignedBin && res16Kaligned &&
                    Backend::sizeToBin(szBlock-size) == Backend::sizeToBin(szBlock)) {
                    // free remainder of fBlock stay in same bin,
                    // so no need to remove it from the bin
                    // TODO: add more "still here" cases
                    FreeBlock *newFBlock = fBlock;
                    // return block from right side of fBlock
                    fBlock = (FreeBlock*)((uintptr_t)newFBlock + szBlock - size);
                    MALLOC_ASSERT(isAligned(fBlock, blockSize), "Invalid free block");
                    fBlock->initHeader();
                    fBlock->setLeftFree(szBlock - size);
                    newFBlock->setMeFree(szBlock - size);

                    fBlock->sizeTmp = size;
                } else {
                    b->removeBlock(fBlock);
                    if (freeBins[binIdx].empty())
                        bitMask.set(binIdx, false);
                    fBlock->sizeTmp = szBlock;
                }
                break;
            } else { // block size is not valid, search for next block in the bin
                curr->setMeFree(szBlock);
                curr->rightNeig(szBlock)->setLeftFree(szBlock);
            }
        }
    }
    return fBlock;
}

void Backend::Bin::removeBlock(FreeBlock *fBlock)
{
    if (head == fBlock)
        head = fBlock->next;
    if (fBlock->prev)
        fBlock->prev->next = fBlock->next;
    if (fBlock->next)
        fBlock->next->prev = fBlock->prev;
}

void Backend::IndexedBins::addBlock(int binIdx, FreeBlock *fBlock, size_t blockSz)
{
    Bin *b = &freeBins[binIdx];

    fBlock->myBin = binIdx;
    fBlock->aligned = toAlignedBin(fBlock, blockSz);
    fBlock->prev = NULL;
    MallocMutex::scoped_lock scopedLock(b->tLock);
    fBlock->next = b->head;
    b->head = fBlock;
    if (fBlock->next)
        fBlock->next->prev = fBlock;
    bitMask.set(binIdx, true);
}

bool Backend::IndexedBins::tryAddBlock(int binIdx, FreeBlock *fBlock, size_t blockSz)
{
    bool locked;
    Bin *b = &freeBins[binIdx];

    fBlock->myBin = binIdx;
    fBlock->aligned = toAlignedBin(fBlock, blockSz);
    fBlock->prev = NULL;

    MallocMutex::scoped_lock scopedLock(b->tLock, /*wait=*/false, &locked);
    if (!locked)
        return false;
    fBlock->next = b->head;
    b->head = fBlock;
    if (fBlock->next)
        fBlock->next->prev = fBlock;
    bitMask.set(binIdx, true);
    return true;
}

void Backend::IndexedBins::reset()
{
    for (int i=0; i<Backend::freeBinsNum; i++)
        freeBins[i].reset();
    bitMask.reset();
}

void Backend::IndexedBins::lockRemoveBlock(int binIdx, FreeBlock *fBlock)
{
    MallocMutex::scoped_lock scopedLock(freeBins[binIdx].tLock);
    freeBins[binIdx].removeBlock(fBlock);
    if (freeBins[binIdx].empty())
        bitMask.set(binIdx, false);
}

// try to allocate num blocks of size Bytes from particular "generic" bin
// res16Kaligned is true if result must be 16KB alined
FreeBlock *Backend::getFromBin(int binIdx, int num, size_t size, bool res16Kaligned,
                               int *binLocked)
{
    FreeBlock *fBlock =
        freeLargeBins.getBlock(binIdx, &procBlocks, num*size, res16Kaligned,
                               /*alignedBin=*/false, /*wait=*/false, binLocked);
    if (fBlock) {
        if (res16Kaligned) {
            size_t fBlockSz = fBlock->sizeTmp;
            uintptr_t fBlockEnd = (uintptr_t)fBlock + fBlockSz;
            FreeBlock *newB = alignUp(fBlock, blockSize);
            FreeBlock *rightPart = (FreeBlock*)((uintptr_t)newB + num*size);

            // Space to use is in the middle,
            // ... return free right part
            if ((uintptr_t)rightPart != fBlockEnd) {
                rightPart->initHeader();  // to prevent coalescing rightPart with fBlock
                coalescAndPut(rightPart, fBlockEnd - (uintptr_t)rightPart);
            }
            // ... and free left part
            if (newB != fBlock) {
                newB->initHeader(); // to prevent coalescing fBlock with newB
                coalescAndPut(fBlock, (uintptr_t)newB - (uintptr_t)fBlock);
            }

            fBlock = newB;
            MALLOC_ASSERT(isAligned(fBlock, blockSize), ASSERT_TEXT);
        } else {
            if (size_t splitSz = fBlock->sizeTmp - num*size) {
                 // split block and return free right part
                FreeBlock *splitB = (FreeBlock*)((uintptr_t)fBlock + num*size);
                splitB->initHeader();
                coalescAndPut(splitB, splitSz);
            }
        }
        procBlocks.signal();
        FreeBlock::markBlocks(fBlock, num, size);
    }

    return fBlock;
}

// try to allocate size Byte block from any of 16KB-alined spaces.
// res16Kaligned is true if result must be 16KN alined
FreeBlock *Backend::getFromAlignedSpace(int binIdx, int num, size_t size,
                                        bool res16Kaligned, bool wait, int *binLocked)
{
    FreeBlock *fBlock =
        freeAlignedBins.getBlock(binIdx, &procBlocks, num*size, res16Kaligned,
                                 /*alignedBin=*/true, wait, binLocked);

    if (fBlock) {
        if (fBlock->sizeTmp != num*size) { // i.e., need to split the block
            FreeBlock *newAlgnd;
            size_t newSz;

            if (res16Kaligned) {
                newAlgnd = fBlock;
                fBlock = (FreeBlock*)((uintptr_t)newAlgnd + newAlgnd->sizeTmp
                                      - num*size);
                MALLOC_ASSERT(isAligned(fBlock, blockSize), "Invalid free block");
                fBlock->initHeader();
                newSz = newAlgnd->sizeTmp - num*size;
            } else {
                newAlgnd = (FreeBlock*)((uintptr_t)fBlock + num*size);
                newSz = fBlock->sizeTmp - num*size;
                newAlgnd->initHeader();
            }
            coalescAndPut(newAlgnd, newSz);
        }
        procBlocks.signal();
        MALLOC_ASSERT(!res16Kaligned || isAligned(fBlock, blockSize), ASSERT_TEXT);
        FreeBlock::markBlocks(fBlock, num, size);
    }
    return fBlock;
}

void Backend::correctMaxRequestSize(size_t requestSize)
{
    if (requestSize < maxBinedSize) {
        for (size_t oldMax = FencedLoad((intptr_t&)maxRequestedSize);
             requestSize > oldMax; ) {
            size_t val = AtomicCompareExchange((intptr_t&)maxRequestedSize,
                                               requestSize, oldMax);
            if (val == oldMax)
                break;
            oldMax = val;
        }
    }
}

// try to allocate size Byte block in available bins
// res16Kaligned is true if result must be 16KB aligned
void *Backend::genericGetBlock(int num, size_t size, bool res16Kaligned, bool startup)
{
    // after (soft|hard)CachesCleanup we can get memory in large bins,
    // while after addNewRegion only in ALGN_SPACE_BIN. This flag
    // is for large bins update status.
    bool largeBinsUpdated = true;
    void *block = NULL;
    const size_t totalReqSize = num*size;
    const int nativeBin = sizeToBin(totalReqSize);
    // If we found 2 or less locked bins, it's time to ask more memory from OS.
    // But nothing can be asked from fixed pool.
    int lockedBinsThreshold = extMemPool->fixedSizePool()? 0 : 2;

    correctMaxRequestSize(totalReqSize);
    scanCoalescQ(/*forceCoalescQDrop=*/false);

    for (;;) {
        const intptr_t startModifiedCnt = procBlocks.getNumOfMods();
        int numOfLockedBins;

        for (;;) {
            numOfLockedBins = 0;

            // TODO: try different bin search order
            if (res16Kaligned) {
                if (!block)
                    for (int i=freeAlignedBins.getMinNonemptyBin(nativeBin);
                         i<freeBinsNum;
                         i=freeAlignedBins.getMinNonemptyBin(i+1))
                        if (block = getFromAlignedSpace(i, num, size, /*res16Kaligned=*/true, /*wait=*/false, &numOfLockedBins))
                            break;
                if (!block && largeBinsUpdated)
                    for (int i=freeLargeBins.getMinNonemptyBin(nativeBin);
                         i<freeBinsNum; i=freeLargeBins.getMinNonemptyBin(i+1))
                        if (block = getFromBin(i, num, size, /*res16Kaligned=*/true, &numOfLockedBins))
                            break;
            } else {
                if (largeBinsUpdated)
                    for (int i=freeLargeBins.getMinNonemptyBin(nativeBin);
                         i<freeBinsNum; i=freeLargeBins.getMinNonemptyBin(i+1))
                        if (block = getFromBin(i, num, size, /*res16Kaligned=*/false, &numOfLockedBins))
                            break;
                if (!block)
                    for (int i=freeAlignedBins.getMinNonemptyBin(nativeBin);
                         i<freeBinsNum;
                         i=freeAlignedBins.getMinNonemptyBin(i+1))
                        if (block = getFromAlignedSpace(i, num, size, /*res16Kaligned=*/false, /*wait=*/false, &numOfLockedBins))
                            break;
            }
            if (block || numOfLockedBins<=lockedBinsThreshold)
                break;
        }
        if (block)
            break;

        largeBinsUpdated = scanCoalescQ(/*forceCoalescQDrop=*/true);
        largeBinsUpdated = extMemPool->softCachesCleanup() || largeBinsUpdated;
        if (!largeBinsUpdated) {
            size_t maxBinSize = 0;

            // Another thread is modifying backend while we can't get the block.
            // Wait while it leaves
            // and re-do the scan before other ways to extend the backend.
            if (procBlocks.waitTillSignalled(startModifiedCnt)
                // semaphore is protecting adding more more memory from OS
                || memExtendingSema.wait())
               continue;

            if (startModifiedCnt != procBlocks.getNumOfMods()) {
                memExtendingSema.signal();
                continue;
            }

            // To keep objects below maxBinedSize, region must be larger then that.
            // So trying to balance between too small regions (that leads to
            // fragmentation) and too large ones (that leads to excessive address
            // space consumption). If region is "quite large", allocate only one,
            // to prevent fragmentation. It supposely doesn't hurt perfromance,
            // because the object requested by user is large.
            const size_t regSz_sizeBased =
                alignUp(4*FencedLoad((intptr_t&)maxRequestedSize), 1024*1024);
            if (size == blockSize || regSz_sizeBased < maxBinedSize) {
                const size_t extendingRegionSize = maxBinedSize;
                for (unsigned idx=0; idx<4; idx++) {
                    size_t binSize =
                        addNewRegion(extendingRegionSize);
                    if (!binSize)
                        break;
                    if (binSize > maxBinSize)
                        maxBinSize = binSize;
                }
            } else {
                maxBinSize = addNewRegion(regSz_sizeBased);
            }
            memExtendingSema.signal();

            // size can be >= maxBinedSize, when getRawMem failed
            // for this allocation, and allocation in bins
            // is our last chance to fulfil the request.
            // Sadly, size is larger then max bin, so have to give up.
            if (maxBinSize && maxBinSize < size)
                return NULL;

            if (!maxBinSize) { // no regions have been added, try to clean cache
                if (extMemPool->hardCachesCleanup())
                    largeBinsUpdated = true;
                else {
                    if (procBlocks.waitTillSignalled(startModifiedCnt))
                        continue;
                    // OS can't give us more memory, but we have some in locked bins
                    if (lockedBinsThreshold && numOfLockedBins) {
                        lockedBinsThreshold = 0;
                        continue;
                    }
                    return NULL;
                }
            }
        }
    }
    return block;
}

LargeMemoryBlock *Backend::getLargeBlock(size_t size, bool startup)
{
    bool directRawMemCall = false;
    void *lmb;

    if (size >= maxBinedSize && !extMemPool->fixedSizePool()) {
        directRawMemCall = true;
        if (! (lmb = getRawMem(size, /*useMapMem=*/true))) {
            bool hardCleanupDone = false;

            // try to clean caches, hoping to find memory after it
            if (!extMemPool->softCachesCleanup()) {
                if (!extMemPool->hardCachesCleanup())
                    return NULL;
                hardCleanupDone = true;
            }
            if (! (lmb = getRawMem(size, /*useMapMem=*/true))) {
                if (hardCleanupDone || !extMemPool->hardCachesCleanup())
                    return NULL;
                if (! (lmb = getRawMem(size, /*useMapMem=*/true))) {
                    // our last chance is to get the block from bins
                    lmb = genericGetBlock(1, size, /*res16Kaligned=*/false,
                                          startup);
                    if (!lmb) return NULL;
                    directRawMemCall = false;
                }
            }
        }
    } else
        lmb = genericGetBlock(1, size, /*res16Kaligned=*/false, startup);

    LargeMemoryBlock *res = (LargeMemoryBlock*)lmb;
    if (res) {
        res->directRawMemCall = directRawMemCall;
        res->unalignedSize = size;
    }
    return res;
}

void Backend::removeBlockFromBin(FreeBlock *fBlock)
{
    if (fBlock->myBin != Backend::NO_BIN)
        if (fBlock->aligned)
            freeAlignedBins.lockRemoveBlock(fBlock->myBin, fBlock);
        else
            freeLargeBins.lockRemoveBlock(fBlock->myBin, fBlock);
}

void Backend::genericPutBlock(FreeBlock *fBlock, size_t blockSz,
                              bool directRawMemCall)
{
    if (blockSz >= maxBinedSize && !extMemPool->fixedSizePool()
        && directRawMemCall)
        freeRawMem(fBlock, blockSz, /*useMapMem=*/true);
    else {
        procBlocks.consume();
        coalescAndPut(fBlock, blockSz);
        procBlocks.signal();
    }
}

void Backend::releaseRegion(MemRegion *memRegion)
{
    {
        MallocMutex::scoped_lock lock(regionListLock);
        if (regionList == memRegion)
            regionList = memRegion->next;
        if (memRegion->next)
            memRegion->next->prev = memRegion->prev;
        if (memRegion->prev)
            memRegion->prev->next = memRegion->next;
    }
    freeRawMem(memRegion, memRegion->allocSz, /*useMapMem=*/true);
}

// coalesce fBlock with its neighborhood
FreeBlock *Backend::doCoalesc(FreeBlock *fBlock, MemRegion **mRegion)
{
    FreeBlock *resBlock = fBlock;
    size_t resSize = fBlock->sizeTmp;
    MemRegion *memRegion = NULL;

    fBlock->markCoalescing(resSize);
    resBlock->blockInBin = false;

    // coalesing with left neighbor
    size_t leftSz = fBlock->trySetLeftUsed(GuardedSize::COAL_BLOCK);
    if (leftSz != GuardedSize::LOCKED) {
        if (leftSz == GuardedSize::COAL_BLOCK) {
            coalescQ.putBlock(fBlock);
            return NULL;
        } else {
            FreeBlock *left = fBlock->leftNeig(leftSz);
            size_t lSz = left->trySetMeUsed(GuardedSize::COAL_BLOCK);
            if (lSz <= GuardedSize::MAX_LOCKED_VAL) {
                fBlock->setLeftFree(leftSz); // rollback
                coalescQ.putBlock(fBlock);
                return NULL;
            } else {
                MALLOC_ASSERT(lSz == leftSz, "Invalid header");
                left->blockInBin = true;
                resBlock = left;
                resSize += leftSz;
                resBlock->sizeTmp = resSize;
            }
        }
    }
    // coalesing with right neighbor
    FreeBlock *right = fBlock->rightNeig(fBlock->sizeTmp);
    size_t rightSz = right->trySetMeUsed(GuardedSize::COAL_BLOCK);
    if (rightSz != GuardedSize::LOCKED) {
        // LastFreeBlock is on the right side
        if (GuardedSize::LAST_REGION_BLOCK == rightSz) {
            right->setMeFree(GuardedSize::LAST_REGION_BLOCK);
            memRegion = static_cast<LastFreeBlock*>(right)->memRegion;
        } else if (GuardedSize::COAL_BLOCK == rightSz) {
            if (resBlock->blockInBin) {
                resBlock->blockInBin = false;
                removeBlockFromBin(resBlock);
            }
            coalescQ.putBlock(resBlock);
            return NULL;
        } else {
            size_t rSz = right->rightNeig(rightSz)->
                trySetLeftUsed(GuardedSize::COAL_BLOCK);
            if (rSz <= GuardedSize::MAX_LOCKED_VAL) {
                right->setMeFree(rightSz);  // rollback
                if (resBlock->blockInBin) {
                    resBlock->blockInBin = false;
                    removeBlockFromBin(resBlock);
                }
                coalescQ.putBlock(resBlock);
                return NULL;
            } else {
                MALLOC_ASSERT(rSz == rightSz, "Invalid header");
                removeBlockFromBin(right);
                resSize += rightSz;

                // Is LastFreeBlock on the right side of right?
                FreeBlock *nextRight = right->rightNeig(rightSz);
                size_t nextRightSz = nextRight->
                    trySetMeUsed(GuardedSize::COAL_BLOCK);
                if (nextRightSz > GuardedSize::MAX_LOCKED_VAL) {
                    if (nextRightSz == GuardedSize::LAST_REGION_BLOCK)
                        memRegion = static_cast<LastFreeBlock*>(nextRight)->memRegion;

                    nextRight->setMeFree(nextRightSz);
                }
            }
        }
    }
    if (memRegion) {
        MALLOC_ASSERT((uintptr_t)memRegion + memRegion->allocSz >=
                      (uintptr_t)right + sizeof(LastFreeBlock), ASSERT_TEXT);
        MALLOC_ASSERT((uintptr_t)memRegion < (uintptr_t)resBlock, ASSERT_TEXT);
        *mRegion = memRegion;
    } else
        *mRegion = NULL;
    resBlock->sizeTmp = resSize;
    return resBlock;
}

void Backend::coalescAndPutList(FreeBlock *list, bool forceCoalescQDrop, bool doStat)
{
    FreeBlock *helper;
    MemRegion *memRegion;
    int alignedSpaceIdx = -1;

    for (;list; list = helper) {
        helper = list->nextToFree;
        FreeBlock *toRet = doCoalesc(list, &memRegion);
        if (!toRet)
            continue;

        if (memRegion && memRegion->blockSz == toRet->sizeTmp
            && !extMemPool->fixedSizePool()) {
            // release the region, because there is no used blocks in it
            if (toRet->blockInBin)
                removeBlockFromBin(toRet);

            releaseRegion(memRegion);
        } else {
            size_t currSz = toRet->sizeTmp;
            int bin = sizeToBin(currSz);
            bool toAligned = toAlignedBin(toRet, currSz);
            bool needAddToBin = true;

            if (toRet->blockInBin) {
                // is it stay in same bin?
                if (toRet->myBin == bin && toRet->aligned == toAligned)
                    needAddToBin = false;
                else {
                    toRet->blockInBin = false;
                    removeBlockFromBin(toRet);
                }
            }

            // not stay in same bin, or bin-less, add it
            if (needAddToBin) {
                toRet->prev = toRet->next = toRet->nextToFree = NULL;
                toRet->myBin = NO_BIN;
                toRet->sizeTmp = 0;

                // If the block is too small to fit in any bin, keep it bin-less.
                // It's not a leak because the block later can be coalesced.
                if (currSz >= minBinedSize) {
                    if (!(toAligned?
                          freeAlignedBins.tryAddBlock(bin, toRet, currSz) :
                          freeLargeBins.tryAddBlock(bin, toRet, currSz))) {
                        toRet->sizeTmp = currSz;
                        coalescQ.putBlock(toRet);
                        continue;
                    }
                }
            }
            // Free (possibly coalesced) free block.
            // Adding to bin must be done before this point,
            // because after a block is free it can be coalesced, and
            // using its pointer became unsafe.
            // Remember that coalescing is not done under any global lock.
            toRet->setMeFree(currSz);
            toRet->rightNeig(currSz)->setLeftFree(currSz);
        }
    }
}

// Coalesce fBlock and add it back to a bin;
// processing delayed coalescing requests.
void Backend::coalescAndPut(FreeBlock *fBlock, size_t blockSz)
{
    fBlock->sizeTmp = blockSz;
    fBlock->nextToFree = NULL;

    coalescAndPutList(fBlock, /*forceCoalescQDrop=*/false, /*doStat=*/false);
}

bool Backend::scanCoalescQ(bool forceCoalescQDrop)
{
    FreeBlock *currCoalescList = coalescQ.getAll();

    if (currCoalescList)
        coalescAndPutList(currCoalescList, forceCoalescQDrop, /*doStat=*/true);
    return currCoalescList;
}

size_t Backend::initRegion(MemRegion *region, size_t rawSize)
{
    FreeBlock *fBlock = (FreeBlock *)((uintptr_t)region + sizeof(MemRegion));
    // right bound is 16KB-aligned, keep LastFreeBlock after it
    size_t blockSz =
        alignDown((uintptr_t)region + rawSize - sizeof(LastFreeBlock), blockSize)
        - ((uintptr_t)region + sizeof(MemRegion));
    if (blockSz < blockSize) return 0;
    MALLOC_ASSERT(isAligned((uintptr_t)fBlock+blockSz, blockSize), ASSERT_TEXT);

    fBlock->initHeader();
    fBlock->setMeFree(blockSz);

    LastFreeBlock *lastBl = static_cast<LastFreeBlock*>(fBlock->rightNeig(blockSz));
    lastBl->initHeader();
    lastBl->setMeFree(GuardedSize::LAST_REGION_BLOCK);
    lastBl->setLeftFree(blockSz);
    lastBl->myBin = NO_BIN;
    lastBl->memRegion = region;

    unsigned targetBin = sizeToBin(blockSz);
    region->allocSz = rawSize;
    region->blockSz = blockSz;
    if (toAlignedBin(fBlock, blockSz)) {
        freeAlignedBins.addBlock(targetBin, fBlock, blockSz);
    } else {
        freeLargeBins.addBlock(targetBin, fBlock, blockSz);
    }
    return blockSz;
}

size_t Backend::addNewRegion(size_t rawSize)
{
    size_t binSize;
    // to guarantee that header is not overwritten in used blocks
    MALLOC_ASSERT(sizeof(BlockMutexes) <= sizeof(BlockI), ASSERT_TEXT);
    // to guarantee that block length is not conflicting with
    // special values of GuardedSize
    MALLOC_ASSERT(FreeBlock::minBlockSize > GuardedSize::MAX_SPEC_VAL, ASSERT_TEXT);

    MemRegion *region = (MemRegion*)getRawMem(rawSize,  /*useMapMem=*/true);
    // it's rough estimation, but enough to do correct check after alignment
    if (!region || rawSize < sizeof(MemRegion)+blockSize) {
        return 0;
    }

    {
        region->prev = NULL;
        MallocMutex::scoped_lock lock(regionListLock);
        region->next = regionList;
        regionList = region;
        if (regionList->next)
            regionList->next->prev = regionList;
    }
    if (! (binSize = initRegion(region, rawSize))) {
        {
            MallocMutex::scoped_lock lock(regionListLock);
            if (regionList == region)
                regionList = region->next;
            if (region->next)
                region->next->prev = region->prev;
            if (region->prev)
                region->prev->next = region->next;
        }
        freeRawMem(region, rawSize, /*useMapMem=*/true);
        return 0;
    }
    procBlocks.pureSignal();

    return binSize;
}

bool Backend::reset()
{
    MemRegion *curr;

    MALLOC_ASSERT(extMemPool->userPool(), "Only user pool can be reset.");

    freeLargeBins.reset();
    freeAlignedBins.reset();

    for (curr = regionList; curr; curr = curr->next)
        if (!initRegion(curr, curr->allocSz))
            return false;
    return true;
}

bool Backend::destroy()
{
    MALLOC_ASSERT(extMemPool->userPool(), "Only user pool can be destroyed.");
    while (regionList) {
        MemRegion *helper = regionList->next;
        (*extMemPool->rawFree)(extMemPool->poolId, regionList,
                               regionList->allocSz);
        regionList = helper;
    }
    return true;
}

size_t Backend::Bin::countFreeBlocks()
{
    size_t cnt = 0;
    for (FreeBlock *fb = head; fb; fb = fb->next)
        cnt++;
    return cnt;
}


} } // namespaces
