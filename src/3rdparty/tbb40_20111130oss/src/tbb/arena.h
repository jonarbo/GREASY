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

#ifndef _TBB_arena_H
#define _TBB_arena_H

#include "tbb/tbb_stddef.h"
#include "tbb/atomic.h"

#include "tbb/tbb_machine.h"

#if !__TBB_CPU_CTL_ENV_PRESENT
    #include <fenv.h>
    typedef fenv_t __TBB_cpu_ctl_env_t;
#endif /* !__TBB_CPU_CTL_ENV_PRESENT */

#include "scheduler_common.h"
#include "intrusive_list.h"
#include "task_stream.h"
#include "../rml/include/rml_tbb.h"
#include "mailbox.h"

namespace tbb {

class task_group_context;
class allocate_root_with_context_proxy;

namespace internal {

class governor;
class arena;
template<typename SchedulerTraits> class custom_scheduler;

//------------------------------------------------------------------------
// arena
//------------------------------------------------------------------------

class market;

//! arena data except the array of slots
/** Separated in order to simplify padding. 
    Intrusive list node base class is used by market to form a list of arenas. **/
struct arena_base : intrusive_list_node {
    //! Market owning this arena
    market* my_market;

    //! Maximal currently busy slot.
    atomic<unsigned> my_limit;

    //! Number of slots in the arena
    unsigned my_num_slots;

    //! Number of workers requested by the master thread owning the arena
    unsigned my_max_num_workers;

    //! Number of workers that are currently requested from the resource manager
    int my_num_workers_requested;

    //! Number of workers that have been marked out by the resource manager to service the arena
    unsigned my_num_workers_allotted;

    //! Number of threads in the arena at the moment
    /** Consists of the workers servicing the arena and one master until it starts 
        arena shutdown and detaches from it. Plays the role of the arena's ref count. **/
    atomic<unsigned> my_num_threads_active;

    //! FPU control settings of arena's master thread captured at the moment of arena instantiation.
    __TBB_cpu_ctl_env_t my_cpu_ctl_env;

#if __TBB_TRACK_PRIORITY_LEVEL_SATURATION
    int my_num_workers_present;
#endif /* __TBB_TRACK_PRIORITY_LEVEL_SATURATION */

    //! Current task pool state and estimate of available tasks amount.
    /** The estimate is either 0 (SNAPSHOT_EMPTY) or infinity (SNAPSHOT_FULL). 
        Special state is "busy" (any other unsigned value). 
        Note that the implementation of arena::is_busy_or_empty() requires 
        my_pool_state to be unsigned. */
    tbb::atomic<uintptr_t> my_pool_state;

#if __TBB_TASK_GROUP_CONTEXT
    //! Default task group context.
    /** Used by root tasks allocated directly by the master thread (not from inside
        a TBB task) without explicit context specification. **/
    task_group_context* my_master_default_ctx;
#endif /* __TBB_TASK_GROUP_CONTEXT */

#if __TBB_TASK_PRIORITY
    //! Highest priority of recently spawned or enqueued tasks.
    volatile intptr_t my_top_priority;

    //! Lowest normalized priority of available spawned or enqueued tasks.
    intptr_t my_bottom_priority;

    //! Tracks events that may bring tasks in offload areas to the top priority level.
    /** Incremented when arena top priority changes or a task group priority
        is elevated to the current arena's top level. **/
    uintptr_t my_reload_epoch;

    //! List of offloaded tasks abandoned by workers revoked by the market
    task* my_orphaned_tasks;

    //! Counter used to track the occurrence of recent orphaning and re-sharing operations.
    tbb::atomic<uintptr_t> my_abandonment_epoch;

    //! Task pool for the tasks scheduled via task::enqueue() method
    /** Such scheduling guarantees eventual execution even if
        - new tasks are constantly coming (by extracting scheduled tasks in 
          relaxed FIFO order);
        - the enqueuing thread does not call any of wait_for_all methods. **/
    task_stream my_task_stream[num_priority_levels];

    //! Highest priority level containing enqueued tasks
    /** It being greater than 0 means that high priority enqueued tasks had to be
        bypassed because all workers were blocked in nested dispatch loops and
        were unable to progress at then current priority level. **/
    tbb::atomic<intptr_t> my_skipped_fifo_priority;
#else /* !__TBB_TASK_PRIORITY */

    //! Task pool for the tasks scheduled via task::enqueue() method
    /** Such scheduling guarantees eventual execution even if
        - new tasks are constantly coming (by extracting scheduled tasks in 
          relaxed FIFO order);
        - the enqueuing thread does not call any of wait_for_all methods. **/
    task_stream my_task_stream;
#endif /* !__TBB_TASK_PRIORITY */

    //! Indicates if there is an oversubscribing worker created to service enqueued tasks.
    bool my_mandatory_concurrency;

#if TBB_USE_ASSERT
    //! Used to trap accesses to the object after its destruction.
    uintptr_t my_guard;
#endif /* TBB_USE_ASSERT */
}; // struct arena_base

class arena
#if (__GNUC__<4 || __GNUC__==4 && __GNUC_MINOR__==0) && !__INTEL_COMPILER
    : public padded<arena_base>
#else
    : private padded<arena_base>
#endif
{
private:
    friend class generic_scheduler;
    template<typename SchedulerTraits> friend class custom_scheduler;
    friend class governor;

    friend class market;
    friend class tbb::task_group_context;
    friend class allocate_root_with_context_proxy;
    friend class intrusive_list<arena>;

    typedef padded<arena_base> base_type;

    //! Constructor
    arena ( market&, unsigned max_num_workers );

    //! Allocate an instance of arena.
    static arena& allocate_arena( market&, unsigned max_num_workers );

    static int unsigned num_slots_to_reserve ( unsigned max_num_workers ) {
        return max(2u, max_num_workers + 1);
    }

    static int allocation_size ( unsigned max_num_workers ) {
        return sizeof(base_type) + num_slots_to_reserve(max_num_workers) * (sizeof(mail_outbox) + sizeof(arena_slot));
    }

#if __TBB_TASK_GROUP_CONTEXT
    //! Finds all contexts affected by the state change and propagates the new state to them.
    /** The propagation is relayed to the market because tasks created by one 
        master thread can be passed to and executed by other masters. This means 
        that context trees can span several arenas at once and thus state change
        propagation cannot be generally localized to one arena only. **/
    template <typename T>
    bool propagate_task_group_state ( T task_group_context::*mptr_state, task_group_context& src, T new_state );
#endif /* __TBB_TASK_GROUP_CONTEXT */

    //! Get reference to mailbox corresponding to given affinity_id.
    mail_outbox& mailbox( affinity_id id ) {
        __TBB_ASSERT( 0<id, "affinity id must be positive integer" );
        __TBB_ASSERT( id <= my_num_slots, "affinity id out of bounds" );

        return ((mail_outbox*)this)[-(int)id];
    }

    //! Completes arena shutdown, destructs and deallocates it.
    void free_arena ();

    typedef uintptr_t pool_state_t;

    //! No tasks to steal since last snapshot was taken
    static const pool_state_t SNAPSHOT_EMPTY = 0;

    //! At least one task has been offered for stealing since the last snapshot started
    static const pool_state_t SNAPSHOT_FULL = pool_state_t(-1);

    //! No tasks to steal or snapshot is being taken.
    static bool is_busy_or_empty( pool_state_t s ) { return s < SNAPSHOT_FULL; }

    //! The number of workers active in the arena.
    unsigned num_workers_active( ) {
        return my_num_threads_active - (my_slots[0].my_scheduler? 1 : 0);
    }

    //! If necessary, raise a flag that there is new job in arena.
    template<bool Spawned> void advertise_new_work();

    //! Check if there is job anywhere in arena.
    /** Return true if no job or if arena is being cleaned up. */
    bool is_out_of_work();

    //! Initiates arena shutdown.
    void close_arena ();

    //! Registers the worker with the arena and enters TBB scheduler dispatch loop
    void process( generic_scheduler& );

    //! Notification that worker or master leaves its arena
    inline void on_thread_leaving ();

#if __TBB_STATISTICS
    //! Outputs internal statistics accumulated by the arena
    void dump_arena_statistics ();
#endif /* __TBB_STATISTICS */

#if __TBB_TASK_PRIORITY
    //! Check if recent priority changes may bring some tasks to the current priority level soon
    /** /param tasks_present indicates presence of tasks at any priority level. **/
    inline bool may_have_tasks ( generic_scheduler*, arena_slot&, bool& tasks_present, bool& dequeuing_possible );
#endif /* __TBB_TASK_PRIORITY */

#if __TBB_COUNT_TASK_NODES
    //! Returns the number of task objects "living" in worker threads
    intptr_t workers_task_node_count();
#endif

    /** Must be the last data field */
    arena_slot my_slots[1];
}; // class arena

} // namespace internal
} // namespace tbb

#include "market.h"
#include "scheduler_common.h"

namespace tbb {
namespace internal {

inline void arena::on_thread_leaving () {
    // In case of using fire-and-forget tasks (scheduled via task::enqueue()) 
    // master thread is allowed to leave its arena before all its work is executed,
    // and market may temporarily revoke all workers from this arena. Since revoked
    // workers never attempt to reset arena state to EMPTY and cancel its request
    // to RML for threads, the arena object is destroyed only when both the last
    // thread is leaving it and arena's state is EMPTY (that is its master thread
    // left and it does not contain any work).
    //
    // A worker that checks for work presence and transitions arena to the EMPTY 
    // state (in snapshot taking procedure arena::is_out_of_work()) updates
    // arena::my_pool_state first and only then arena::my_num_workers_requested.
    // So the below check for work absence must be done against the latter field.
    //
    // Besides there is a time window between decrementing the active threads count
    // and checking if there is an outstanding request for workers. New worker 
    // thread may arrive during this window, finish whatever work is present, and
    // then shutdown the arena. This sequence may result in destructing the same
    // arena twice. So a local copy of the outstanding request value must be done
    // before decrementing active threads count.
    //
    int requested = __TBB_load_with_acquire(my_num_workers_requested);
    if ( --my_num_threads_active==0 && !requested ) {
        __TBB_ASSERT( !my_num_workers_requested, NULL );
        __TBB_ASSERT( my_pool_state == SNAPSHOT_EMPTY || !my_max_num_workers, NULL );
        close_arena();
    }
}

template<bool Spawned> void arena::advertise_new_work() {
    if( !Spawned ) { // i.e. the work was enqueued
        if( my_max_num_workers==0 ) {
            my_max_num_workers = 1;
            my_mandatory_concurrency = true;
            my_pool_state = SNAPSHOT_FULL;
            my_market->adjust_demand( *this, 1 );
            return;
        }
        // Local memory fence is required to avoid missed wakeups; see the comment below.
        // Starvation resistant tasks require mandatory concurrency, so missed wakeups are unacceptable.
        atomic_fence(); 
    }
    // Double-check idiom that, in case of spawning, is deliberately sloppy about memory fences.
    // Technically, to avoid missed wakeups, there should be a full memory fence between the point we 
    // released the task pool (i.e. spawned task) and read the arena's state.  However, adding such a 
    // fence might hurt overall performance more than it helps, because the fence would be executed 
    // on every task pool release, even when stealing does not occur.  Since TBB allows parallelism, 
    // but never promises parallelism, the missed wakeup is not a correctness problem.
    pool_state_t snapshot = my_pool_state;
    if( is_busy_or_empty(snapshot) ) {
        // Attempt to mark as full.  The compare_and_swap below is a little unusual because the 
        // result is compared to a value that can be different than the comparand argument.
        if( my_pool_state.compare_and_swap( SNAPSHOT_FULL, snapshot )==SNAPSHOT_EMPTY ) {
            if( snapshot!=SNAPSHOT_EMPTY ) {
                // This thread read "busy" into snapshot, and then another thread transitioned 
                // my_pool_state to "empty" in the meantime, which caused the compare_and_swap above 
                // to fail.  Attempt to transition my_pool_state from "empty" to "full".
                if( my_pool_state.compare_and_swap( SNAPSHOT_FULL, SNAPSHOT_EMPTY )!=SNAPSHOT_EMPTY ) {
                    // Some other thread transitioned my_pool_state from "empty", and hence became
                    // responsible for waking up workers.
                    return;
                }
            }
            // This thread transitioned pool from empty to full state, and thus is responsible for
            // telling RML that there is work to do.
            if( Spawned ) {
                if( my_mandatory_concurrency ) {
                    __TBB_ASSERT(my_max_num_workers==1, "");
                    // There was deliberate oversubscription on 1 core for sake of starvation-resistant tasks.
                    // Now a single active thread (must be the master) supposedly starts a new parallel region
                    // with relaxed sequential semantics, and oversubscription should be avoided.
                    // Demand for workers has been decreased to 0 during SNAPSHOT_EMPTY, so just keep it.
                    my_max_num_workers = 0;
                    my_mandatory_concurrency = false;
                    return;
                }
            }
            my_market->adjust_demand( *this, my_max_num_workers );
        }
    }
}

} // namespace internal
} // namespace tbb

#endif /* _TBB_arena_H */
