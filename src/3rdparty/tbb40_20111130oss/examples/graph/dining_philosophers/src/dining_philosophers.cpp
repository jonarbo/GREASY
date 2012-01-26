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

#if _MSC_VER
    // Suppress "decorated name length exceeded, name was truncated" warning
    #pragma warning (disable: 4503)
#endif

#include "tbb/flow_graph.h"
#include "tbb/task_scheduler_init.h"
#include "tbb/tick_count.h"
#include "tbb/atomic.h"
#include "tbb/spin_mutex.h"
#include <iostream>
#include "../../../common/utility/utility.h"
#include <cstdlib>
#include <cstdio>

#if _WIN32 || _WIN64
#include "windows.h"
#define SLEEP(a) Sleep(a*1000)
#else
#define SLEEP(a) sleep(a)
#endif

// Each philosopher is run as a task in the graph, and its first action is to execute
// a think(), and then call make_my_node().  make_my_node() will
//
//     1) allocate a join_node,
//     2) link the left and right chopstick queues to the join,
//     3) allocate a function_node with a node_body that calls eat_and_think().
//     4) link the join_node to the function_node.
//
// While the philosopher participates in the game, it will (when eat_and_think() is invoked)
//
//     a) eat (the funtion_node will be invoked only when both chopsticks are available)
//     b) if we are going to eventually eat again, we just try_put() the chopsticks
//        back into our queues.  If not, we disconnect our join_node from the queues,
//        and then place the chopsticks back.
//     c) If we are going to eat again, we think().
//
// philosopher_by_ref allows us to pass an object to g.run() that can be copied, but will
// retain a reference to our original philosopher.  If this wasn't done, g.run() would store a
// copy of philosopher, and the original wouldn't be used.

const int think_time = 1;
const int eat_time = 1;
const int num_times = 10;

tbb::tick_count t0;
bool verbose = false;

const char *names[] = { "Archimedes", "Bakunin", "Confucius", "Democritus", "Euclid"
                      , "Favorinus", "Geminus", "Heraclitus", "Ichthyas", "Jason of Nysa",
                          "Kant", "Lavrov", "Metrocles", "Nausiphanes", "Onatas", "Phaedrus",
                          "Quillot", "Russell", "Socrates", "Thales", "Udayana",
                          "Vernadsky", "Wittgenstein", "Xenophilus", "Yen Yuan", "Zenodotus"
};
const int NumPhilosophers = sizeof(names) / sizeof(char*);

// from seismic
int get_default_num_threads() {
    static int threads = 0;
    if ( threads == 0 )
        threads = tbb::task_scheduler_init::default_num_threads();
    return threads;
}

struct RunOptions {
    utility::thread_number_range threads;
    int number_of_philosophers;
    bool silent;
    RunOptions(utility::thread_number_range threads_, int number_of_philosophers_, bool silent_) :
        threads(threads_), number_of_philosophers(number_of_philosophers_), silent(silent_) { }
};

RunOptions ParseCommandLine(int argc, char *argv[]) {
    int auto_threads = get_default_num_threads();
    utility::thread_number_range threads(get_default_num_threads, auto_threads, auto_threads);
    int nPhilosophers = 5;
    bool verbose = false;
    char charbuf[100];
    std::sprintf(charbuf, "%d", NumPhilosophers);
    std::string pCount = "how many philosophers, from 2-";
    pCount += charbuf;

    utility::cli_argument_pack cli_pack;
    cli_pack.positional_arg(threads, "n-of_threads", "number of threads to use, a range of the form low[:high], where low and high are non-negative integers or 'auto' for the TBB default.")
            .positional_arg(nPhilosophers, "n-of-philosophers", pCount)
            .arg(verbose,"verbose","verbose output");
    utility::parse_cli_arguments(argc, argv, cli_pack);
    if(nPhilosophers < 2 || nPhilosophers > NumPhilosophers) {
        std::cout << "Number of philosophers (" << nPhilosophers << ") out of range [2:" << NumPhilosophers << "]\n";
        std::cout << cli_pack.usage_string(argv[0]) << std::flush;
        std::exit(1);
    }
    return RunOptions(threads, nPhilosophers,!verbose);
}


tbb::spin_mutex my_mutex;

class chopstick {};

class philosopher {
public:

    typedef tbb::flow::queue_node< chopstick > chopstick_buffer;
    typedef tbb::flow::join_node< std::tuple<chopstick, chopstick>, tbb::flow::reserving > join_type;

    philosopher( const char *name, tbb::flow::graph &the_graph, chopstick_buffer *left, chopstick_buffer *right ) : 
        my_name(name), my_graph(&the_graph), my_left_chopstick(left), my_right_chopstick(right),
        my_join(NULL), my_function_node(NULL), my_count(num_times) { }

    ~philosopher() {
        if(my_join) delete my_join;
        if(my_function_node) delete my_function_node;
    }

    void operator()();
    void check();

    void link_left_chopstick() { my_left_chopstick->register_successor( std::get<0>(my_join->inputs()) ); }
    void link_right_chopstick() { my_right_chopstick->register_successor( std::get<1>(my_join->inputs()) ); }
    const char *name() const { return my_name; }

private:

  friend std::ostream& operator<<(std::ostream& o, philosopher const &p);

  const char *my_name;
  tbb::flow::graph *my_graph;
  chopstick_buffer *my_left_chopstick;
  chopstick_buffer *my_right_chopstick;
  join_type *my_join;
  tbb::flow::function_node< join_type::output_type, tbb::flow::continue_msg, tbb::flow::rejecting > *my_function_node;
  int my_count;

  friend class node_body;

  void eat_and_think( );
  void eat( );
  void think( );
  void make_my_node();

};

class philosopher_by_ref {
    philosopher &my_guy;
public:
    philosopher_by_ref(philosopher &_my_guy) : my_guy(_my_guy) { }
    void operator()() {
        my_guy();
    };
};

std::ostream& operator<<(std::ostream& o, philosopher const &p) {
    o << "< philosopher[" << reinterpret_cast<uintptr_t>(const_cast<philosopher *>(&p)) << "] " << p.name() 
        << ", my_count=" << p.my_count;
    return o;
}

class node_body {
  philosopher& my_philosopher;
public:
  node_body( philosopher &p ) : my_philosopher(p) { }
  void operator()( philosopher::join_type::output_type ) {
    my_philosopher.eat_and_think();
  } 
};

void philosopher::operator()() {
  think();
  make_my_node(); 
} 

void philosopher::check() {
  if ( my_count != 0 ) {
    std::printf("ERROR: philosopher %s still had to run %d more times\n", name(), my_count);
    std::exit(1);
  } else {
    if(verbose) std::printf("%s done.\n", name());
  }
}

void philosopher::eat_and_think( ) { 
  eat();
  if(my_count < 0) abort();
  --my_count;

  if (my_count > 0) {
    my_left_chopstick->try_put( chopstick() );
    my_right_chopstick->try_put( chopstick() );
    think();
  } else {
    my_left_chopstick->remove_successor( std::get<0>(my_join->inputs()) );
    my_right_chopstick->remove_successor( std::get<1>(my_join->inputs()) );
    my_left_chopstick->try_put( chopstick() );
    my_right_chopstick->try_put( chopstick() );
    if(verbose) {
      tbb::spin_mutex::scoped_lock lock(my_mutex);
      std::printf("%s has left the building\n", name());
    }
 
  }
}

void philosopher::eat() { 
  if(verbose) {
    tbb::spin_mutex::scoped_lock lock(my_mutex);
    std::printf("%s eating\n", name());
  }
  SLEEP(eat_time); 
  if(verbose) {
    tbb::spin_mutex::scoped_lock lock(my_mutex);
    std::printf("%s done eating\n", name());
  }
}

void philosopher::think() { 
  if(verbose) {
    tbb::spin_mutex::scoped_lock lock(my_mutex);
    std::printf("%s thinking\n", name());
  }
  SLEEP(think_time); 
  if(verbose) {
    tbb::spin_mutex::scoped_lock lock(my_mutex);
    std::printf("%s done thinking\n", name());
  }
}

void philosopher::make_my_node() {
  my_join = new join_type(*my_graph);
  link_left_chopstick();
  link_right_chopstick();
  my_function_node = 
    new tbb::flow::function_node< join_type::output_type, tbb::flow::continue_msg , tbb::flow::rejecting >( *my_graph, 
                                                                         tbb::flow::serial, 
                                                                         node_body( *this ) );
  tbb::flow::make_edge( *my_join, *my_function_node );
}

typedef std::vector<philosopher> p_vector;
typedef std::vector<tbb::flow::queue_node< chopstick > > chopstick_places;

int main(int argc, char *argv[]) {
    int num_threads;
    int num_philosophers;

    RunOptions options = ParseCommandLine(argc, argv);
    num_philosophers = options.number_of_philosophers;
    verbose = !options.silent;

    for(num_threads = options.threads.first; num_threads <= options.threads.last; ++num_threads) {
    
        tbb::task_scheduler_init init(num_threads);

        tbb::flow::graph g;

        if(verbose) std::printf("\n%d philosophers with %d threads", num_philosophers, num_threads);

        if(verbose) std::printf("\n\n");
        t0 = tbb::tick_count::now();

        // create queues of (one) chopstick
        chopstick_places places(num_philosophers, tbb::flow::queue_node<chopstick>(g));
        for ( int i = 0; i < num_philosophers; ++i ) {
            places[i].try_put(chopstick());
        }
  
       p_vector philosophers;
       // must reserve the vector so no reallocation occurs (we're passing references to the vector elements)
       philosophers.reserve(num_philosophers);
       for ( int i = 0; i < num_philosophers; ++i ) {
           philosophers.push_back( philosopher( names[i], g, &(places[i]), &(places[(i+1)%num_philosophers]) ) );
           if(verbose) {
               tbb::spin_mutex::scoped_lock lock(my_mutex);
               std::cout << "Built philosopher " << philosophers[i] << std::endl;
           }
           g.run( philosopher_by_ref(philosophers[i]) );
       }

       g.wait_for_all();

       tbb::tick_count t1 = tbb::tick_count::now();
       if(verbose) utility::report_elapsed_time((t1-t0).seconds());

       for ( int i = 0; i < num_philosophers; ++i ) 
           philosophers[i].check();
   }

   return 0;
}
