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

#ifndef _TBB_malloc_Customize_H_
#define _TBB_malloc_Customize_H_

/* Thread shutdown notification callback */
/* redefine the name of the callback to meet TBB requirements
   for externally visible names of service functions */
#define mallocThreadShutdownNotification __TBB_mallocThreadShutdownNotification
#define mallocProcessShutdownNotification __TBB_mallocProcessShutdownNotification

extern "C" void mallocThreadShutdownNotification(void *);
extern "C" void mallocProcessShutdownNotification(void);

// customizing MALLOC_ASSERT macro
#include "tbb/tbb_stddef.h"
#define MALLOC_ASSERT(assertion, message) __TBB_ASSERT(assertion, message)

#ifndef MALLOC_DEBUG
#define MALLOC_DEBUG TBB_USE_DEBUG
#endif

#include "tbb/tbb_machine.h"

#if DO_ITT_NOTIFY
#include "tbb/itt_notify.h"
#define MALLOC_ITT_SYNC_PREPARE(pointer) ITT_NOTIFY(sync_prepare, (pointer))
#define MALLOC_ITT_SYNC_ACQUIRED(pointer) ITT_NOTIFY(sync_acquired, (pointer))
#define MALLOC_ITT_SYNC_RELEASING(pointer) ITT_NOTIFY(sync_releasing, (pointer))
#define MALLOC_ITT_SYNC_CANCEL(pointer) ITT_NOTIFY(sync_cancel, (pointer))
#else
#define MALLOC_ITT_SYNC_PREPARE(pointer) ((void)0)
#define MALLOC_ITT_SYNC_ACQUIRED(pointer) ((void)0)
#define MALLOC_ITT_SYNC_RELEASING(pointer) ((void)0)
#define MALLOC_ITT_SYNC_CANCEL(pointer) ((void)0)
#endif

//! Stripped down version of spin_mutex.
/** Instances of MallocMutex must be declared in memory that is zero-initialized.
    There are no constructors.  This is a feature that lets it be
    used in situations where the mutex might be used while file-scope constructors
    are running.

    There are no methods "acquire" or "release".  The scoped_lock must be used
    in a strict block-scoped locking pattern.  Omitting these methods permitted
    further simplification. */
class MallocMutex : tbb::internal::no_copy {
    __TBB_atomic_flag value;

public:
    class scoped_lock : tbb::internal::no_copy {
        __TBB_Flag unlock_value;
        MallocMutex& mutex;
    public:
        scoped_lock( MallocMutex& m ) : unlock_value(__TBB_LockByte(m.value)), mutex(m) {}
        scoped_lock( MallocMutex& m, bool block, bool *locked ) : mutex(m) {
            unlock_value = 1;
            if (block) {
                unlock_value = __TBB_LockByte(m.value);
                if (locked) *locked = true;
            } else {
                if (bool res = __TBB_TryLockByte(m.value)) {
                    unlock_value = 0;
                    if (locked) *locked = true;
                } else
                    if (locked) *locked = false;
            }
        }
        ~scoped_lock() {
            if (!unlock_value) __TBB_UnlockByte(mutex.value, unlock_value);
        }
    };
    friend class scoped_lock;
};

inline intptr_t AtomicIncrement( volatile intptr_t& counter ) {
    return __TBB_FetchAndAddW( &counter, 1 )+1;
}

inline uintptr_t AtomicAdd( volatile intptr_t& counter, intptr_t value ) {
    return __TBB_FetchAndAddW( &counter, value );
}

inline intptr_t AtomicCompareExchange( volatile intptr_t& location, intptr_t new_value, intptr_t comparand) {
    return __TBB_CompareAndSwapW( &location, new_value, comparand );
}

inline intptr_t FencedLoad( const volatile intptr_t &location ) {
    return __TBB_load_with_acquire(location);
}

inline void FencedStore( volatile intptr_t &location, intptr_t value ) {
    __TBB_store_with_release(location, value);
}

inline void SpinWaitWhileEq(const volatile intptr_t &location, const intptr_t value) {
    tbb::internal::spin_wait_while_eq(location, value);
}

inline intptr_t BitScanRev(uintptr_t x) {
    return !x? -1 : __TBB_Log2(x);
}

inline void AtomicOr(volatile void *operand, uintptr_t addend) {
    __TBB_AtomicOR(operand, addend);
}

inline void AtomicAnd(volatile void *operand, uintptr_t addend) {
    __TBB_AtomicAND(operand, addend);
}

#define USE_DEFAULT_MEMORY_MAPPING 1

// To support malloc replacement with LD_PRELOAD
#include "proxy.h"

#if MALLOC_LD_PRELOAD
#define malloc_proxy __TBB_malloc_proxy
extern "C" void * __TBB_malloc_proxy(size_t)  __attribute__ ((weak));
#else
const bool malloc_proxy = false;
#endif

namespace rml {
namespace internal {
    void init_tbbmalloc();
} } // namespaces

#define MALLOC_EXTRA_INITIALIZATION rml::internal::init_tbbmalloc()

#endif /* _TBB_malloc_Customize_H_ */
