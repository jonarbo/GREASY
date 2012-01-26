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

#include "tbbmalloc_internal.h"

/********* Allocation of large objects ************/


namespace rml {
namespace internal {

static struct LargeBlockCacheStat {
    uintptr_t age;
} loCacheStat;

static uintptr_t cleanupCacheIfNeed(ExtMemoryPool *extMemPool);

bool CachedLargeBlocksL::push(ExtMemoryPool *extMemPool, LargeMemoryBlock *ptr)
{   
    ptr->prev = NULL;
    ptr->age  = cleanupCacheIfNeed(extMemPool);

    MallocMutex::scoped_lock scoped_cs(lock);
    if (lastCleanedAge) {
        ptr->next = first;
        first = ptr;
        if (ptr->next) ptr->next->prev = ptr;
        if (!last) {
            MALLOC_ASSERT(0 == oldest, ASSERT_TEXT);
            oldest = ptr->age;
            last = ptr;
        }
        return true;
    } else {
        // 1st object of such size was released.
        // Not cache it, and remeber when this occurs
        // to take into account during cache miss.
        lastCleanedAge = ptr->age;
        return false;
    }
}

void LargeMemoryBlock::registerInPool(ExtMemoryPool *extMemPool)
{
    MallocMutex::scoped_lock scoped_cs(extMemPool->largeObjLock);
    gPrev = NULL;
    gNext = extMemPool->loHead;
    if (gNext)
        gNext->gPrev = this;
    extMemPool->loHead = this;
}

void LargeMemoryBlock::unregisterFromPool(ExtMemoryPool *extMemPool)
{
    MallocMutex::scoped_lock scoped_cs(extMemPool->largeObjLock);
    if (extMemPool->loHead == this)
        extMemPool->loHead = gNext;
    if (gNext)
        gNext->gPrev = gPrev;
    if (gPrev)
        gPrev->gNext = gNext;
}

LargeMemoryBlock *CachedLargeBlocksL::pop(ExtMemoryPool *extMemPool)
{   
    uintptr_t currAge = cleanupCacheIfNeed(extMemPool);
    LargeMemoryBlock *result=NULL;
    {
        MallocMutex::scoped_lock scoped_cs(lock);
        if (first) {
            result = first;
            first = result->next;
            if (first)  
                first->prev = NULL;
            else {
                last = NULL;
                oldest = 0;
            }
        } else {
            /* If cache miss occured, set ageThreshold to twice the difference 
               between current time and last time cache was cleaned. */
            ageThreshold = 2*(currAge - lastCleanedAge);
        }
    }
    return result;
}

bool CachedLargeBlocksL::releaseLastIfOld(ExtMemoryPool *extMemPool,
                                        uintptr_t currAge)
{
    LargeMemoryBlock *toRelease = NULL;
    bool released = false;
 
    /* oldest may be more recent then age, that's why cast to signed type
       was used. age overflow is also processed correctly. */
    if (last && (intptr_t)(currAge - oldest) > ageThreshold) {
        MallocMutex::scoped_lock scoped_cs(lock);
        // double check
        if (last && (intptr_t)(currAge - last->age) > ageThreshold) {
            do {
                last = last->prev;
            } while (last && (intptr_t)(currAge - last->age) > ageThreshold);
            if (last) {
                toRelease = last->next;
                oldest = last->age;
                last->next = NULL;
            } else {
                toRelease = first;
                first = NULL;
                oldest = 0;
            }
            MALLOC_ASSERT( toRelease, ASSERT_TEXT );
            lastCleanedAge = toRelease->age;
        }
        else 
            return false;
    }
    released = toRelease;

    while ( toRelease ) {
        LargeMemoryBlock *helper = toRelease->next;
        removeBackRef(toRelease->backRefIdx);
        if (extMemPool->userPool())
            toRelease->unregisterFromPool(extMemPool);
        extMemPool->backend.putLargeBlock(toRelease);
        toRelease = helper;
    }
    return released;
}

bool CachedLargeBlocksL::releaseAll(ExtMemoryPool *extMemPool)
{
    LargeMemoryBlock *toRelease = NULL;
    bool released = false;
 
    if (last) {
        MallocMutex::scoped_lock scoped_cs(lock);
        // double check
        if (last) {
            toRelease = first;
            last = NULL;
            first = NULL;
            oldest = 0;
        } 
        else 
            return false;
    }
    released = toRelease;

    while ( toRelease ) {
        LargeMemoryBlock *helper = toRelease->next;
        removeBackRef(toRelease->backRefIdx);
        if (extMemPool->userPool())
            toRelease->unregisterFromPool(extMemPool);
        extMemPool->backend.putLargeBlock(toRelease);
        toRelease = helper;
    }
    return released;
}

// release from cache blocks that are older than ageThreshold
bool ExtMemoryPool::doLOCacheCleanup(uintptr_t currAge)
{
    bool res = false;

    for (int i = numLargeBlockBins-1; i >= 0; i--)
        res |= cachedLargeBlocks[i].releaseLastIfOld(this, currAge);

    return res;
}

bool ExtMemoryPool::softCachesCleanup()
{
    // TODO: cleanup small objects as well
    return doLOCacheCleanup(FencedLoad((intptr_t&)loCacheStat.age));
}

static uintptr_t cleanupCacheIfNeed(ExtMemoryPool *extMemPool)
{
    /* loCacheStat.age overflow is OK, as we only want difference between 
     * its current value and some recent.
     *
     * Both malloc and free should increment loCacheStat.age, as in 
     * a different case multiple cached blocks would have same age,
     * and accuracy of predictors suffers.
     */
    uintptr_t currAge = (uintptr_t)AtomicIncrement((intptr_t&)loCacheStat.age);

    if ( 0 == currAge % cacheCleanupFreq )
        extMemPool->doLOCacheCleanup(currAge);

    return currAge;
}

static LargeMemoryBlock* getCachedLargeBlock(ExtMemoryPool *extMemPool, size_t size)
{
    MALLOC_ASSERT( size%largeBlockCacheStep==0, ASSERT_TEXT );
    LargeMemoryBlock *lmb = NULL;
    // blockSize is the minimal alignment and thus the minimal size of a large object.
    size_t idx = (size-minLargeObjectSize)/largeBlockCacheStep;
    if (idx<numLargeBlockBins) {
        lmb = extMemPool->cachedLargeBlocks[idx].pop(extMemPool);
        if (lmb) {
            MALLOC_ITT_SYNC_ACQUIRED(extMemPool->cachedLargeBlocks+idx);
            STAT_increment(getThreadId(), ThreadCommonCounters, allocCachedLargeBlk);
        }
    }
    return lmb;
}

void* mallocLargeObject(ExtMemoryPool *extMemPool, size_t size, size_t alignment,
                        bool startupAlloc)
{
    LargeMemoryBlock* lmb;
    size_t headersSize = sizeof(LargeMemoryBlock)+sizeof(LargeObjectHdr);
    // TODO: take into account that they are already largeObjectAlignment-alinged
    size_t allocationSize = alignUp(size+headersSize+alignment, largeBlockCacheStep);

    if (allocationSize < size) // allocationSize is wrapped around after alignUp
        return NULL;

    if (startupAlloc || !(lmb = getCachedLargeBlock(extMemPool, allocationSize))) {
        BackRefIdx backRefIdx;

        if ((backRefIdx = BackRefIdx::newBackRef(/*largeObj=*/true)).isInvalid()) 
            return NULL;
        // unalignedSize is set in result
        lmb = extMemPool->backend.getLargeBlock(allocationSize, startupAlloc);
        if (!lmb) {
            removeBackRef(backRefIdx);
            return NULL;
        }
        lmb->backRefIdx = backRefIdx;
        if (extMemPool->userPool())
            lmb->registerInPool(extMemPool);
        STAT_increment(getThreadId(), ThreadCommonCounters, allocNewLargeObj);
    }

    void *alignedArea = (void*)alignUp((uintptr_t)lmb+headersSize, alignment);
    LargeObjectHdr *header = (LargeObjectHdr*)alignedArea-1;
    header->memoryBlock = lmb;
    header->backRefIdx = lmb->backRefIdx;
    setBackRef(header->backRefIdx, header);
 
    lmb->objectSize = size;

    MALLOC_ASSERT( isLargeObject(alignedArea), ASSERT_TEXT );
    return alignedArea;
}

static bool freeLargeObjectToCache(ExtMemoryPool *extMemPool, LargeMemoryBlock* largeBlock)
{
    size_t size = largeBlock->unalignedSize;
    size_t idx = (size-minLargeObjectSize)/largeBlockCacheStep;
    if (idx<numLargeBlockBins) {
        MALLOC_ASSERT( size%largeBlockCacheStep==0, ASSERT_TEXT );
        MALLOC_ITT_SYNC_RELEASING(extMemPool->cachedLargeBlocks+idx);
        if (extMemPool->cachedLargeBlocks[idx].push(extMemPool, largeBlock)) {
            STAT_increment(getThreadId(), ThreadCommonCounters, cacheLargeBlk);
            return true;
        } else
            return false;
    }
    return false;
}

void freeLargeObject(ExtMemoryPool *extMemPool, void *object)
{
    LargeObjectHdr *header = (LargeObjectHdr*)object - 1;

    // overwrite backRefIdx to simplify double free detection
    header->backRefIdx = BackRefIdx();
    if (!freeLargeObjectToCache(extMemPool, header->memoryBlock)) {
        removeBackRef(header->memoryBlock->backRefIdx);
        if (extMemPool->userPool())
            header->memoryBlock->unregisterFromPool(extMemPool);
        extMemPool->backend.putLargeBlock(header->memoryBlock);
        STAT_increment(getThreadId(), ThreadCommonCounters, freeLargeObj);
    }
}

/*********** End allocation of large objects **********/

} // namespace internal
} // namespace rml

