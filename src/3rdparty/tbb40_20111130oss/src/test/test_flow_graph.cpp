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

#include "harness_graph.h"
#include "tbb/task_scheduler_init.h"

const int T = 4;
const int W = 4;

struct decrement_wait : NoAssign {

    tbb::flow::graph * const my_graph;
    bool * const my_done_flag;

    decrement_wait( tbb::flow::graph &h, bool *done_flag ) : my_graph(&h), my_done_flag(done_flag) {}

    void operator()(int i) const {
        Harness::Sleep(10*i);
        my_done_flag[i] = true;
        my_graph->decrement_wait_count();
    }
};

static void test_wait_count() {
   tbb::flow::graph h;
   for (int i = 0; i < T; ++i ) {
       bool done_flag[W];
       for (int j = 0; j < W; ++j ) {
           for ( int w = 0; w < W; ++w ) done_flag[w] = false;
           for ( int w = 0; w < j; ++w ) h.increment_wait_count();

           NativeParallelFor( j, decrement_wait(h, done_flag) );
           h.wait_for_all();
           for ( int w = 0; w < W; ++w ) {
              if ( w < j ) ASSERT( done_flag[w] == true, NULL );
              else ASSERT( done_flag[w] == false, NULL );
           }
       }
   }
}

const int F = 100;

#if __TBB_LAMBDAS_PRESENT
bool lambda_flag[F];
#endif
bool functor_flag[F];

struct set_functor {
    int my_i;
    set_functor( int i ) : my_i(i) {} 
    void operator()() { functor_flag[my_i] = true; }
};

struct return_functor {
    int my_i;
    return_functor( int i ) : my_i(i) {} 
    int operator()() { return my_i; }
};

static void test_run() {
    tbb::flow::graph h;
    for (int i = 0; i < T; ++i ) {

        // Create receivers and flag arrays
        #if __TBB_LAMBDAS_PRESENT
        harness_mapped_receiver<int> lambda_r;
        lambda_r.initialize_map( F, 1 );
        #endif
        harness_mapped_receiver<int> functor_r;
        functor_r.initialize_map( F, 1 );

        // Initialize flag arrays
        for (int j = 0; j < F; ++j ) {
            #if __TBB_LAMBDAS_PRESENT
            lambda_flag[j] = false;
            #endif
            functor_flag[j] = false;
        }

        for ( int j = 0; j < F; ++j ) {
            #if __TBB_LAMBDAS_PRESENT
                h.run( [=]() { lambda_flag[j] = true; } );  
                h.run( lambda_r, [=]() { return j; } );  
            #endif
            h.run( set_functor(j) );
            h.run( functor_r, return_functor(j) );
        } 
        h.wait_for_all();
        for ( int j = 0; j < F; ++j ) {
        #if __TBB_LAMBDAS_PRESENT
            ASSERT( lambda_flag[i] == true, NULL );
        #endif
            ASSERT( functor_flag[i] == true, NULL );
        }
        #if __TBB_LAMBDAS_PRESENT
        lambda_r.validate();
        #endif
        functor_r.validate();
    }
}

int TestMain() { 
    current_executors = 0;
    if( MinThread<1 ) {
        REPORT("number of threads must be positive\n");
        exit(1);
    }
    for( int p=MinThread; p<=MaxThread; ++p ) {
       tbb::task_scheduler_init init(p);
       test_wait_count();
       test_run();
   }
   return Harness::Done;
}

