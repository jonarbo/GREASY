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

#ifndef _TBB_scheduler_common_H
#define _TBB_scheduler_common_H

#include "tbb/tbb_stddef.h"

#include <string.h>  // for memset, memcpy, memmove

#include "tbb_statistics.h"

#if TBB_USE_ASSERT > 1
#include <stdio.h>
#endif /* TBB_USE_ASSERT > 1 */

/* Temporarily change "private" to "public" while including "tbb/task.h".
   This hack allows us to avoid publishing internal types and methods
   in the public header files just for sake of friend declarations. */
#ifndef private
    #define private public
    #define undef_private
#endif

#include "tbb/task.h"
#include "tbb/tbb_exception.h"

#ifdef undef_private
    #undef private
#endif

#if __TBB_TASK_GROUP_CONTEXT
#include "tbb/spin_mutex.h"
#endif /* __TBB_TASK_GROUP_CONTEXT */

// This macro is an attempt to get rid of ugly ifdefs in the shared parts of the code.
// It drops the second argument depending on whether the controlling macro is defined.
// The first argument is just a convenience allowing to keep comma before the macro usage.
#if __TBB_TASK_GROUP_CONTEXT
    #define __TBB_CONTEXT_ARG(arg1, context) arg1, context
#else /* !__TBB_TASK_GROUP_CONTEXT */
    #define __TBB_CONTEXT_ARG(arg1, context) arg1
#endif /* !__TBB_TASK_GROUP_CONTEXT */

#if DO_TBB_TRACE
#include <cstdio>
#define TBB_TRACE(x) ((void)std::printf x)
#else
#define TBB_TRACE(x) ((void)(0))
#endif /* DO_TBB_TRACE */

#if _MSC_VER && !defined(__INTEL_COMPILER)
    // Workaround for overzealous compiler warnings
    // These particular warnings are so ubiquitous that no attempt is made to narrow
    // the scope of the warnings.
    #pragma warning (disable: 4100 4127 4312 4244 4267 4706)
#endif

namespace tbb {
namespace internal {

class generic_scheduler;

#if __TBB_TASK_PRIORITY
static const intptr_t num_priority_levels = 3;
static const intptr_t normalized_normal_priority = (num_priority_levels - 1) / 2;

inline intptr_t normalize_priority ( priority_t p ) {
    return intptr_t(p - priority_low) / priority_stride_v4;
}

static const priority_t priority_from_normalized_rep[num_priority_levels] = {
    priority_low, priority_normal, priority_high
};

inline void assert_priority_valid ( intptr_t& p ) {
    __TBB_ASSERT( p >= 0 && p < num_priority_levels, NULL );
}

inline intptr_t& priority ( task& t ) {
    return t.prefix().context->my_priority;
}
#endif /* __TBB_TASK_PRIORITY */

#if __TBB_TASK_GROUP_CONTEXT
//! Task group state change propagation global epoch
/** Together with generic_scheduler::my_context_state_propagation_epoch forms
    cross-thread signaling mechanism that allows to avoid locking at the hot path
    of normal execution flow.

    When a descendant task group context is registered or unregistered, the global
    and local epochs are compared. If they differ, a state change is being propagated,
    and thus registration/deregistration routines take slower branch that may block
    (at most one thread of the pool can be blocked at any moment). Otherwise the
    control path is lock-free and fast. **/
extern uintptr_t the_context_state_propagation_epoch;

//! Mutex guarding state change propagation across task groups forest.
/** Also protects modification of related data structures. **/
extern spin_mutex the_context_state_propagation_mutex;
#endif /* __TBB_TASK_GROUP_CONTEXT */

//! Alignment for a task object
const size_t task_alignment = 32;

//! Number of bytes reserved for a task prefix
/** If not exactly sizeof(task_prefix), the extra bytes *precede* the task_prefix. */
const size_t task_prefix_reservation_size = ((sizeof(internal::task_prefix)-1)/task_alignment+1)*task_alignment;

//! Definitions for bits in task_prefix::extra_state
enum task_extra_state {
    //! Tag for v1 tasks (i.e. tasks in TBB 1.0 and 2.0)
    es_version_1_task = 0,
    //! Tag for v3 tasks (i.e. tasks in TBB 2.1-2.2)
    es_version_3_task = 1,
    //! Tag for v3 task_proxy.
    es_task_proxy = 0x20,
    //! Set if ref_count might be changed by another thread.  Used for debugging.
    es_ref_count_active = 0x40,
    //! Set if the task has been stolen
    es_task_is_stolen = 0x80
};

//! Optimization hint to free_task that enables it omit unnecessary tests and code.
enum free_task_hint {
    //! No hint
    no_hint=0,
    //! Task is known to have been allocated by this scheduler
    local_task=1,
    //! Task is known to be a small task.
    /** Task should be returned to the free list of *some* scheduler, possibly not this scheduler. */
    small_task=2,
    //! Bitwise-OR of local_task and small_task.
    /** Task should be returned to free list of this scheduler. */
    small_local_task=3
};

//------------------------------------------------------------------------
// Debugging support
//------------------------------------------------------------------------

#if TBB_USE_ASSERT

static const uintptr_t venom =
#if __TBB_WORDSIZE == 8
        0xDDEEAADDDEADBEEF;
#else
        0xDEADBEEF;
#endif

/** Crazy conversion is necessary to shut up insane MS compiler. **/
template <typename T>
void poison_value ( T& val ) { val = *(T*)(uintptr_t*)&venom; }

/** Expected to be used in assertions only, thus no empty form is defined. **/
inline bool is_alive( uintptr_t v ) { return v != venom; }

/** Logically, this method should be a member of class task.
    But we do not want to publish it, so it is here instead. */
inline void assert_task_valid( const task& task ) {
    __TBB_ASSERT( &task!=NULL, NULL );
    __TBB_ASSERT( !is_poisoned(&task), NULL );
    __TBB_ASSERT( (uintptr_t)&task % task_alignment == 0, "misaligned task" );
    __TBB_ASSERT( (unsigned)task.state()<=(unsigned)task::recycle, "corrupt task (invalid state)" );
}

#else /* !TBB_USE_ASSERT */

/** In contrast to debug version poison_value() is a macro here because
    the variable used as its argument may be undefined in release builds. **/
#define poison_value(g) ((void)0)

inline void assert_task_valid( const task& ) {}

#endif /* !TBB_USE_ASSERT */

//------------------------------------------------------------------------
// Helpers
//------------------------------------------------------------------------

#if __TBB_TASK_GROUP_CONTEXT
inline bool ConcurrentWaitsEnabled ( task& t ) {
    return (t.prefix().context->my_version_and_traits & task_group_context::concurrent_wait) != 0;
}

inline bool CancellationInfoPresent ( task& t ) {
    return t.prefix().context->my_cancellation_requested != 0;
}

#if TBB_USE_CAPTURED_EXCEPTION
    inline tbb_exception* TbbCurrentException( task_group_context*, tbb_exception* src) { return src->move(); }
    inline tbb_exception* TbbCurrentException( task_group_context*, captured_exception* src) { return src; }
#else
    // Using macro instead of an inline function here allows to avoid evaluation of the
    // TbbCapturedException expression when exact propagation is enabled for the context.
    #define TbbCurrentException(context, TbbCapturedException) \
        context->my_version_and_traits & task_group_context::exact_exception    \
            ? tbb_exception_ptr::allocate()    \
            : tbb_exception_ptr::allocate( *(TbbCapturedException) );
#endif /* !TBB_USE_CAPTURED_EXCEPTION */

#define TbbRegisterCurrentException(context, TbbCapturedException) \
    if ( context->cancel_group_execution() ) {  \
        /* We are the first to signal cancellation, so store the exception that caused it. */  \
        context->my_exception = TbbCurrentException( context, TbbCapturedException ); \
    }

#define TbbCatchAll(context)  \
    catch ( tbb_exception& exc ) {  \
        TbbRegisterCurrentException( context, &exc );   \
    } catch ( std::exception& exc ) {   \
        TbbRegisterCurrentException( context, captured_exception::allocate(typeid(exc).name(), exc.what()) ); \
    } catch ( ... ) {   \
        TbbRegisterCurrentException( context, captured_exception::allocate("...", "Unidentified exception") );\
    }

#else /* !__TBB_TASK_GROUP_CONTEXT */

inline bool ConcurrentWaitsEnabled ( task& t ) { return false; }

#endif /* __TBB_TASK_GROUP_CONTEXT */

//------------------------------------------------------------------------
// arena_slot
//------------------------------------------------------------------------

struct arena_slot {
    //! Scheduler of the thread attached to the slot
    /** Marks the slot as busy, and is used to iterate through the schedulers belonging to this arena **/
    generic_scheduler* my_scheduler;

    // Task pool (the deque of task pointers) of the scheduler that owns this slot
    /** Also is used to specify if the slot is empty or locked:
         0 - empty
        -1 - locked **/
    task* *__TBB_atomic task_pool;

    //! Index of the first ready task in the deque.
    /** Modified by thieves, and by the owner during compaction/reallocation **/
    __TBB_atomic size_t head;

    //! Padding to avoid false sharing caused by the thieves accessing this slot
    char pad1[NFS_MaxLineSize - sizeof(size_t) - sizeof(task**) - sizeof(generic_scheduler*)];

    //! Index of the element following the last ready task in the deque.
    /** Modified by the owner thread. **/
    __TBB_atomic size_t tail;

    //! Hints provided for operations with the container of starvation-resistant tasks.
    /** Modified by the owner thread (during these operations). **/
    unsigned hint_for_push, hint_for_pop;

#if __TBB_STATISTICS
    //! Set of counters to accumulate internal statistics related to this arena
    statistics_counters *my_counters;
#endif /* __TBB_STATISTICS */
    //! Padding to avoid false sharing caused by the thieves accessing the next slot
    char pad2[NFS_MaxLineSize - sizeof(size_t) - 2*sizeof(unsigned)
#if __TBB_STATISTICS
              - sizeof(statistics_counters*)
#endif /* __TBB_STATISTICS */
             ];
}; // class arena_slot

} // namespace internal
} // namespace tbb

#endif /* _TBB_scheduler_common_H */
