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

/** @file harness_graph.cpp     
    This contains common helper classes and functions for testing graph nodes
**/

#ifndef harness_graph_H
#define harness_graph_H

#include "harness.h"
#include "tbb/flow_graph.h"
#include "tbb/null_rw_mutex.h"
#include "tbb/atomic.h"
#include "tbb/concurrent_unordered_map.h"

// Needed conversion to and from continue_msg, but didn't want to add
// conversion operators to the class, since we don't want it in general,
// only in these tests.
template<typename InputType, typename OutputType>
struct convertor {
    static OutputType convert_value(const InputType &i) {
        return OutputType(i);
    }
};

template<typename InputType>
struct convertor<InputType,tbb::flow::continue_msg> {
    static tbb::flow::continue_msg convert_value(const InputType &/*i*/) {
        return tbb::flow::continue_msg(); 
    }
};

template<typename OutputType>
struct convertor<tbb::flow::continue_msg,OutputType> {
    static OutputType convert_value(const tbb::flow::continue_msg &/*i*/) {
        return OutputType();
    }
};

// helper for multioutput_function_node tests.
template<size_t N>
struct mof_helper {
    template<typename InputType, typename ports_type>
    static inline void output_converted_value(const InputType &i, ports_type &p) {
        std::get<N-1>(p).put(convertor<InputType,typename std::tuple_element<N-1,ports_type>::type::output_type>::convert_value(i));
        output_converted_value<N-1>(i, p);
    }
};

template<>
struct mof_helper<1> {
    template<typename InputType, typename ports_type>
    static inline void output_converted_value(const InputType &i, ports_type &p) {
        // just emit a default-constructed object
        std::get<0>(p).put(convertor<InputType,typename std::tuple_element<0,ports_type>::type::output_type>::convert_value(i));
    }
};

template< typename InputType, typename OutputType >
struct harness_graph_default_functor {
    static OutputType construct( InputType v ) {
        return OutputType(v);
    }
};

template< typename OutputType >
struct harness_graph_default_functor< tbb::flow::continue_msg, OutputType > {
    static OutputType construct( tbb::flow::continue_msg ) {
        return OutputType();
    }
};

template< typename InputType >
struct harness_graph_default_functor< InputType, tbb::flow::continue_msg > {
    static tbb::flow::continue_msg construct( InputType ) {
        return tbb::flow::continue_msg();
    }
};

template< >
struct harness_graph_default_functor< tbb::flow::continue_msg, tbb::flow::continue_msg > {
    static tbb::flow::continue_msg construct( tbb::flow::continue_msg ) {
        return tbb::flow::continue_msg();
    }
};

#if TBB_PREVIEW_GRAPH_NODES
template<typename InputType, typename OutputSet>
struct harness_graph_default_multioutput_functor {
    static const int N = std::tuple_size<OutputSet>::value;
    typedef typename tbb::flow::multioutput_function_node<InputType,OutputSet>::output_ports_type ports_type;
    static void construct(const InputType &i, ports_type &p) {
        mof_helper<N>::output_converted_value(i, p);
    }
};
#endif

static tbb::atomic<size_t> current_executors;

//! An executor that accepts InputType and generates OutputType
template< typename InputType, typename OutputType, typename M=tbb::null_rw_mutex >
struct harness_graph_executor {

    typedef OutputType (*function_ptr_type)( InputType v );

    static M mutex;
    static function_ptr_type fptr;
    static tbb::atomic<size_t> execute_count;
    static size_t max_executors;

    static inline OutputType func( InputType v ) {
        typename M::scoped_lock l( mutex );
        size_t c = current_executors.fetch_and_increment();
        ASSERT( max_executors == 0 || c <= max_executors, NULL ); 
        ++execute_count;
        OutputType v2 = (*fptr)(v);
        current_executors.fetch_and_decrement();
        return v2; 
    }

    struct functor {
        tbb::atomic<size_t> my_execute_count;
        functor() { my_execute_count = 0; }
        functor( const functor &f ) { my_execute_count = f.my_execute_count; }
        OutputType operator()( InputType i ) {
           typename M::scoped_lock l( harness_graph_executor::mutex );
           size_t c = current_executors.fetch_and_increment();
           ASSERT( harness_graph_executor::max_executors == 0 || c <= harness_graph_executor::max_executors, NULL ); 
           ++execute_count;
           my_execute_count.fetch_and_increment();
           OutputType v2 = (*harness_graph_executor::fptr)(i);
           current_executors.fetch_and_decrement();
           return v2; 
        }
    };

};

#if TBB_PREVIEW_GRAPH_NODES
//! A multioutput executor that accepts InputType and has only one Output of OutputType.
template< typename InputType, typename OutputTuple, typename M=tbb::null_rw_mutex >
struct harness_graph_multioutput_executor {
    typedef typename tbb::flow::multioutput_function_node<InputType,OutputTuple>::output_ports_type ports_type;
    typedef typename std::tuple_element<0,OutputTuple>::type OutputType;

    typedef void (*mfunction_ptr_type)( const InputType& v, ports_type &p );

    static M mutex;
    static mfunction_ptr_type fptr;
    static tbb::atomic<size_t> execute_count;
    static size_t max_executors;


    static inline void func( const InputType &v, ports_type &p ) {
        typename M::scoped_lock l( mutex );
        size_t c = current_executors.fetch_and_increment();
        ASSERT( max_executors == 0 || c <= max_executors, NULL ); 
        ASSERT(std::tuple_size<OutputTuple>::value == 1, NULL);
        ++execute_count;
        (*fptr)(v,p);
        current_executors.fetch_and_decrement();
    }

    struct functor {
        tbb::atomic<size_t> my_execute_count;
        functor() { my_execute_count = 0; }
        functor( const functor &f ) { my_execute_count = f.my_execute_count; }
        void operator()( const InputType &i, ports_type &p ) {
           typename M::scoped_lock l( harness_graph_multioutput_executor::mutex );
           size_t c = current_executors.fetch_and_increment();
           ASSERT( harness_graph_multioutput_executor::max_executors == 0 || c <= harness_graph_multioutput_executor::max_executors, NULL ); 
           ++execute_count;
           my_execute_count.fetch_and_increment();
           (*harness_graph_multioutput_executor::fptr)(i,p);
           current_executors.fetch_and_decrement();
        }
    };

};
#endif //  TBB_PREVIEW_GRAPH_NODES

template< typename InputType, typename OutputType, typename M >
M harness_graph_executor<InputType, OutputType, M>::mutex;

template< typename InputType, typename OutputType, typename M >
tbb::atomic<size_t> harness_graph_executor<InputType, OutputType, M>::execute_count;

template< typename InputType, typename OutputType, typename M >
typename harness_graph_executor<InputType, OutputType, M>::function_ptr_type harness_graph_executor<InputType, OutputType, M>::fptr
    = harness_graph_default_functor< InputType, OutputType >::construct;

template< typename InputType, typename OutputType, typename M >
size_t harness_graph_executor<InputType, OutputType, M>::max_executors = 0;

#if TBB_PREVIEW_GRAPH_NODES
// static vars for multioutput_function_node tests
template< typename InputType, typename OutputTuple, typename M >
M harness_graph_multioutput_executor<InputType, OutputTuple, M>::mutex;

template< typename InputType, typename OutputTuple, typename M >
tbb::atomic<size_t> harness_graph_multioutput_executor<InputType, OutputTuple, M>::execute_count;

template< typename InputType, typename OutputTuple, typename M >
typename harness_graph_multioutput_executor<InputType, OutputTuple, M>::mfunction_ptr_type harness_graph_multioutput_executor<InputType, OutputTuple, M>::fptr
    = harness_graph_default_multioutput_functor< InputType, OutputTuple >::construct;

template< typename InputType, typename OutputTuple, typename M >
size_t harness_graph_multioutput_executor<InputType, OutputTuple, M>::max_executors = 0;
#endif

//! Counts the number of puts received
template< typename T >
struct harness_counting_receiver : public tbb::flow::receiver<T>, NoCopy {

    tbb::atomic< size_t > my_count;
    T max_value;
    size_t num_copies;

    harness_counting_receiver() : num_copies(1) {
       my_count = 0;
    }

    void initialize_map( const T& m, size_t c ) {
       my_count = 0;
       max_value = m;
       num_copies = c;
    }

    /* override */ bool try_put( const T & ) {
      ++my_count;
      return true;
    }

    void validate() {
        size_t n = my_count;
        ASSERT( n == num_copies*max_value, NULL );
    }

};

//! Counts the number of puts received
template< typename T >
struct harness_mapped_receiver : public tbb::flow::receiver<T>, NoCopy {

    tbb::atomic< size_t > my_count;
    T max_value;
    size_t num_copies;
    typedef tbb::concurrent_unordered_map< T, tbb::atomic< size_t > > map_type;
    map_type *my_map;

    harness_mapped_receiver() : my_map(NULL) {
       my_count = 0;
    }

    ~harness_mapped_receiver() {
        if ( my_map ) delete my_map;
    }

    void initialize_map( const T& m, size_t c ) {
       my_count = 0;
       max_value = m;
       num_copies = c;
       if ( my_map ) delete my_map;
       my_map = new map_type;
    }

    /* override */ bool try_put( const T &t ) {
      if ( my_map ) {
          tbb::atomic<size_t> a;
          a = 1;
          std::pair< typename map_type::iterator, bool > r =  (*my_map).insert( typename map_type::value_type( t, a ) );
          if ( r.second == false ) {
              size_t v = r.first->second.fetch_and_increment();
              ASSERT( v < num_copies, NULL );
          }
      } else {
          ++my_count;
      }
      return true;
    }

    void validate() {
        if ( my_map ) {
            for ( size_t i = 0; i < (size_t)max_value; ++i ) {
                size_t n = (*my_map)[(int)i];
                ASSERT( n == num_copies, NULL );
            }
        } else {
            size_t n = my_count;
            ASSERT( n == num_copies*max_value, NULL );
        }
    }

};

//! Counts the number of puts received
template< typename T >
struct harness_counting_sender : public tbb::flow::sender<T>, NoCopy {

    typedef tbb::flow::receiver<T> successor_type;
    tbb::atomic< successor_type * > my_receiver;
    tbb::atomic< size_t > my_count;
    tbb::atomic< size_t > my_received;
    size_t my_limit;

    harness_counting_sender( ) : my_limit(~size_t(0)) {
       my_receiver = NULL;
       my_count = 0;
       my_received = 0;
    }

    harness_counting_sender( size_t limit ) : my_limit(limit) {
       my_receiver = NULL;
       my_count = 0;
       my_received = 0;
    }

    /* override */ bool register_successor( successor_type &r ) {
        my_receiver = &r;
        return true;
    }

    /* override */ bool remove_successor( successor_type &r ) {
        successor_type *s = my_receiver.fetch_and_store( NULL );
        ASSERT( s == &r, NULL );
        return true;
    }

    /* override */ bool try_get( T & v ) { 
        size_t i = my_count.fetch_and_increment();
        if ( i < my_limit ) {
           v = T( i );
           ++my_received;
           return true;
        } else {
           return false;
        }
    }

    bool try_put_once() {
        successor_type *s = my_receiver;
        size_t i = my_count.fetch_and_increment();
        if ( s->try_put( T(i) ) ) {
            ++my_received;
            return true;
        } else {
            return false;
        }
    }

    void try_put_until_false() {
        successor_type *s = my_receiver;
        size_t i = my_count.fetch_and_increment();

        while ( s->try_put( T(i) ) ) {
            ++my_received;
            i = my_count.fetch_and_increment();
        } 
    }

    void try_put_until_limit() {
        successor_type *s = my_receiver;

        for ( int i = 0; i < (int)my_limit; ++i ) { 
            ASSERT( s->try_put( T(i) ), NULL );
            ++my_received;
        } 
        ASSERT( my_received == my_limit, NULL );
    }

};

#endif


