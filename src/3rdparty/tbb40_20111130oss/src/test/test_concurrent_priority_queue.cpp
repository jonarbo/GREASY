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

#include "tbb/concurrent_priority_queue.h"
#include "tbb/atomic.h"
#include "harness.h"
#include <functional>
#include <vector>

#if _MSC_VER==1500 && !__INTEL_COMPILER
    // VS2008/VC9 seems to have an issue; limits pull in math.h
    #pragma warning( push )
    #pragma warning( disable: 4985 )
#endif
#include <climits>
#if _MSC_VER==1500 && !__INTEL_COMPILER
    #pragma warning( pop )
#endif

#if __INTEL_COMPILER && (_WIN32 || _WIN64) && TBB_USE_DEBUG && _CPPLIB_VER<520
// The Intel Compiler has an issue that causes the Microsoft Iterator
// Debugging code to crash in vector::pop_back when it is called after a
// vector::push_back throws an exception.
// #define _HAS_ITERATOR_DEBUGGING 0 // Setting this to 0 doesn't solve the problem
                                     // and also provokes a redefinition warning
#define __TBB_ITERATOR_DEBUGGING_EXCEPTIONS_BROKEN
#endif

using namespace tbb;

const size_t MAX_ITER = 10000;
const int MAX_PRIO = 100000000;

tbb::atomic<unsigned int> counter;

class my_data_type {
public:
    int priority;
    char padding[tbb::internal::NFS_MaxLineSize - sizeof(int) % tbb::internal::NFS_MaxLineSize];
    my_data_type() {}
    my_data_type(int init_val) : priority(init_val) {}
    const my_data_type operator+(const my_data_type& other) const {
        return my_data_type(priority+other.priority);
    }
    bool operator==(const my_data_type& other) const {
        return this->priority == other.priority;
    }
};

const my_data_type DATA_MIN(INT_MIN);
const my_data_type DATA_MAX(INT_MAX);

class my_less {
public:
    bool operator()(const my_data_type d1, const my_data_type d2) const {
        return d1.priority<d2.priority;
    }
};

class my_throwing_type : public my_data_type {
public:
    static int throw_flag;
    my_throwing_type() : my_data_type() {}
    my_throwing_type(const my_throwing_type& src) : my_data_type(src) { 
        if (my_throwing_type::throw_flag) throw 42; 
        priority = src.priority;
    }
};

int my_throwing_type::throw_flag = 0;

typedef concurrent_priority_queue<my_throwing_type, my_less > cpq_ex_test_type;

template<typename T, typename C>
class FillBody : NoAssign {
    int nThread;
    T my_max, my_min;
    concurrent_priority_queue<T, C> *q;
    C less_than;
public:  
    FillBody(int nThread_, T max_, T min_, concurrent_priority_queue<T, C> *q_) : nThread(nThread_), my_max(max_), my_min(min_), q(q_) {}
    void operator()(const int threadID) const {
        T elem = my_min + T(threadID);
        for (size_t i=0; i<MAX_ITER; ++i) {
            // do some pushes
            q->push(elem);
            if (elem == my_max) elem = my_min;
            elem = elem + T(nThread);
        }
    }
};

template<typename T, typename C>
struct EmptyBody : NoAssign {
    int nThread;
    T my_max;
    concurrent_priority_queue<T, C> *q;
    C less_than;
public:
    EmptyBody(int nThread_, T max_, concurrent_priority_queue<T, C> *q_) : nThread(nThread_), my_max(max_), q(q_) {}
    void operator()(const int /*threadID*/) const {
        T elem(my_max), last;
        if (q->try_pop(last)) {
            ++counter;
            while(q->try_pop(elem)) {
                ASSERT(!less_than(last, elem), "FAILED pop/priority test in EmptyBody.");
                last = elem;
                elem = my_max;
                ++counter;
            }
        }
    }
};

template <typename T, typename C>
class FloggerBody : NoAssign {
    int nThread;
    concurrent_priority_queue<T, C> *q;
public:
    FloggerBody(int nThread_, concurrent_priority_queue<T, C> *q_) : 
        nThread(nThread_), q(q_) {}  
    void operator()(const int threadID) const {
        T elem = T(threadID);
        for (size_t i=0; i<MAX_ITER; ++i) {
            q->push(elem);
            (void) q->try_pop(elem);
        }
    }
};

void TestConstructorsDestructorsAccessors() {
    std::vector<int> v;
    std::allocator<int> a;
    concurrent_priority_queue<int, std::less<int> > *q, *qo;
    concurrent_priority_queue<int, std::less<int>, std::allocator<int>  > *qi;

    // Test constructors/destructors
    REMARK("Testing default constructor.\n");
    q = new concurrent_priority_queue<int, std::less<int> >();
    REMARK("Default constructor complete.\n");
    ASSERT(q->size()==0, "FAILED size test.");
    ASSERT(q->empty(), "FAILED empty test.");
    REMARK("Testing destructor.\n");
    delete q;
    REMARK("Destruction complete.\n");

    REMARK("Testing capacity constructor.\n");
    q = new concurrent_priority_queue<int, std::less<int> >(42);
    REMARK("Capacity constructor complete.\n");
    ASSERT(q->size()==0, "FAILED size test.");
    ASSERT(q->empty(), "FAILED empty test.");
    REMARK("Testing destructor.\n");
    delete q;
    REMARK("Destruction complete.\n");

    REMARK("Testing allocator constructor.\n");
    qi = new concurrent_priority_queue<int, std::less<int>, std::allocator<int> >(a);
    REMARK("Allocator constructor complete.\n");
    ASSERT(qi->size()==0, "FAILED size test.");
    ASSERT(qi->empty(), "FAILED empty test.");
    REMARK("Testing destructor.\n");
    delete qi;
    REMARK("Destruction complete.\n");

    REMARK("Testing capacity+allocator constructor.\n");
    qi = new concurrent_priority_queue<int, std::less<int>, std::allocator<int> >(42, a);
    REMARK("Capacity+allocator constructor complete.\n");
    ASSERT(qi->size()==0, "FAILED size test.");
    ASSERT(qi->empty(), "FAILED empty test.");
    REMARK("Testing destructor.\n");
    delete qi;
    REMARK("Destruction complete.\n");

    REMARK("Testing iterator filler constructor.\n");
    for (int i=0; i<42; ++i)
        v.push_back(i);
    q = new concurrent_priority_queue<int, std::less<int> >(v.begin(), v.end());
    REMARK("Iterator filler constructor complete.\n");
    ASSERT(q->size()==42, "FAILED vector/size test.");
    ASSERT(!q->empty(), "FAILED vector/empty test.");

    REMARK("Testing copy constructor.\n");
    qo = new concurrent_priority_queue<int, std::less<int> >(*q);
    REMARK("Copy constructor complete.\n");
    ASSERT(qo->size()==42, "FAILED vector/size test.");
    ASSERT(!qo->empty(), "FAILED vector/empty test.");
    REMARK("Testing destructor.\n");
    delete q;
    delete qo;
    REMARK("Destruction complete.\n");
}

void TestAssignmentClearSwap() {
    std::vector<int> v;
    concurrent_priority_queue<int, std::less<int> > *q, *qo;
    int e;

    for (int i=0; i<42; ++i)
        v.push_back(i);
    q = new concurrent_priority_queue<int, std::less<int> >(v.begin(), v.end());
    qo = new concurrent_priority_queue<int, std::less<int> >();

    REMARK("Testing assignment (1).\n");
    *qo = *q; 
    REMARK("Assignment complete.\n");
    ASSERT(qo->size()==42, "FAILED assignment/size test.");
    ASSERT(!qo->empty(), "FAILED assignment/empty test.");

    REMARK("Testing clear.\n");
    q->clear();
    REMARK("Clear complete.\n");
    ASSERT(q->size()==0, "FAILED clear/size test.");
    ASSERT(q->empty(), "FAILED clear/empty test.");

    for (size_t i=0; i<5; ++i)
        (void) qo->try_pop(e);

    REMARK("Testing assignment (2).\n");
    *q = *qo;
    REMARK("Assignment complete.\n");
    ASSERT(q->size()==37, "FAILED assignment/size test.");
    ASSERT(!q->empty(), "FAILED assignment/empty test.");

    for (size_t i=0; i<5; ++i)
        (void) qo->try_pop(e);

    REMARK("Testing swap.\n");
    q->swap(*qo);
    REMARK("Swap complete.\n");
    ASSERT(q->size()==32, "FAILED swap/size test.");
    ASSERT(!q->empty(), "FAILED swap/empty test.");
    ASSERT(qo->size()==37, "FAILED swap_operand/size test.");
    ASSERT(!qo->empty(), "FAILED swap_operand/empty test.");
    delete q;
    delete qo;
}

void TestSerialPushPop() {
    concurrent_priority_queue<int, std::less<int> > *q;
    int e=42, prev=INT_MAX;
    size_t count=0;

    q = new concurrent_priority_queue<int, std::less<int> >(MAX_ITER);
    REMARK("Testing serial push.\n");
    for (size_t i=0; i<MAX_ITER; ++i) {
        q->push(e);
        e = e*-1 + int(i);
    }
    REMARK("Pushing complete.\n");
    ASSERT(q->size()==MAX_ITER, "FAILED push/size test.");
    ASSERT(!q->empty(), "FAILED push/empty test.");
    
    REMARK("Testing serial pop.\n");
    while (!q->empty()) {
        ASSERT(q->try_pop(e), "FAILED pop test.");
        ASSERT(prev>=e, "FAILED pop/priority test.");
        prev = e;
        ++count;
        ASSERT(q->size()==MAX_ITER-count, "FAILED swap/size test.");
        ASSERT(!q->empty() || count==MAX_ITER, "FAILED swap/empty test.");
    }
    REMARK("Popping complete.\n");
    delete q;
}

template <typename T, typename C>
void TestParallelPushPop(int nThreads, T t_max, T t_min, C /*compare*/) {
    size_t qsize;

    concurrent_priority_queue<T, C> *q = new concurrent_priority_queue<T, C>(0);
    FillBody<T, C> filler(nThreads, t_max, t_min, q);
    EmptyBody<T, C> emptier(nThreads, t_max, q);
    counter = 0;
    REMARK("Testing parallel push.\n");
    NativeParallelFor(nThreads, filler);
    REMARK("Pushing complete.\n");
    qsize = q->size();
    ASSERT(q->size()==nThreads*MAX_ITER, "FAILED push/size test.");
    ASSERT(!q->empty(), "FAILED push/empty test.");
    
    REMARK("Testing parallel pop.\n");
    NativeParallelFor(nThreads, emptier);
    REMARK("Popping complete.\n");
    ASSERT(counter==qsize, "FAILED pop/size test.");
    ASSERT(q->size()==0, "FAILED pop/empty test.");

    q->clear();
    delete(q);
}

void TestExceptions() {
    const size_t TOO_LARGE_SZ = 1000000000;
    my_throwing_type elem;

    REMARK("Testing basic constructor exceptions.\n");
    // Allocate empty queue should not throw no matter the type
    try {
        my_throwing_type::throw_flag = 1;
        cpq_ex_test_type q;
    } catch(...) {
        ASSERT(false, "FAILED: allocating empty queue should not throw exception.\n");
    }
    // Allocate small queue should not throw for reasonably sized type
    try {
        my_throwing_type::throw_flag = 1;
        cpq_ex_test_type q(42);
    } catch(...) {
        ASSERT(false, "FAILED: allocating small queue should not throw exception.\n");
    }
    // Allocate a queue with too large initial size
    try {
        my_throwing_type::throw_flag = 0;
        cpq_ex_test_type q(TOO_LARGE_SZ);
        REMARK("FAILED: Huge queue did not throw exception.\n");
    } catch(...) {}

    cpq_ex_test_type *pq;
    try {
        my_throwing_type::throw_flag = 0;
        pq = NULL;
        pq = new cpq_ex_test_type(TOO_LARGE_SZ);
        REMARK("FAILED: Huge queue did not throw exception.\n");
        delete pq;
    } catch(...) {
        ASSERT(!pq, "FAILED: pq should not be touched when constructor throws.\n");
    }
    REMARK("Basic constructor exceptions testing complete.\n");
    REMARK("Testing copy constructor exceptions.\n");
    my_throwing_type::throw_flag = 0;
    cpq_ex_test_type src_q(42);
    elem.priority = 42;
    for (size_t i=0; i<42; ++i) src_q.push(elem);
    try {
        my_throwing_type::throw_flag = 1;
        cpq_ex_test_type q(src_q);
        REMARK("FAILED: Copy construct did not throw exception.\n");
    } catch(...) {}
    try {
        my_throwing_type::throw_flag = 1;
        pq = NULL;
        pq = new concurrent_priority_queue<my_throwing_type, my_less >(src_q);
        REMARK("FAILED: Copy construct did not throw exception.\n");
        delete pq;
    } catch(...) {
        ASSERT(!pq, "FAILED: pq should not be touched when constructor throws.\n");
    }
    REMARK("Copy constructor exceptions testing complete.\n");
    REMARK("Testing assignment exceptions.\n");
    // Assignment is copy-swap, so it should be exception safe
    my_throwing_type::throw_flag = 0;
    cpq_ex_test_type assign_q(24);
    try {
        my_throwing_type::throw_flag = 1;
        assign_q = src_q;
        REMARK("FAILED: Assign did not throw exception.\n");
    } catch(...) {
        ASSERT(assign_q.empty(), "FAILED: assign_q should be empty.\n");
    }
    REMARK("Assignment exceptions testing complete.\n");
#ifndef __TBB_ITERATOR_DEBUGGING_EXCEPTIONS_BROKEN
    REMARK("Testing push exceptions.\n");
    my_throwing_type::throw_flag = 0;
    pq = new cpq_ex_test_type(3);
    try {
        pq->push(elem);
        pq->push(elem);
        pq->push(elem);
    } catch(...) {
        ASSERT(false, "FAILED: Push should not throw exception... yet.\n");
    }
    try { // should crash on copy during expansion of vector
        my_throwing_type::throw_flag = 1;
        pq->push(elem);
        REMARK("FAILED: Push did not throw exception.\n");
    } catch(...) {
        ASSERT(!pq->empty(), "FAILED: pq should not be empty.\n");
        ASSERT(pq->size()==3, "FAILED: pq should be only three elements.\n");
        ASSERT(pq->try_pop(elem), "FAILED: pq is not functional.\n");
    }
    delete pq;

    my_throwing_type::throw_flag = 0;
    pq = new cpq_ex_test_type(3);
    try {
        pq->push(elem);
        pq->push(elem);
    } catch(...) {
        ASSERT(false, "FAILED: Push should not throw exception... yet.\n");
    }
    try { // should crash on push copy of element
        my_throwing_type::throw_flag = 1;
        pq->push(elem);
        REMARK("FAILED: Push did not throw exception.\n");
    } catch(...) {
        ASSERT(!pq->empty(), "FAILED: pq should not be empty.\n");
        ASSERT(pq->size()==2, "FAILED: pq should be only two elements.\n");
        ASSERT(pq->try_pop(elem), "FAILED: pq is not functional.\n");
    }
    delete pq;
    REMARK("Push exceptions testing complete.\n");
#endif
}

template <typename T, typename C>
void TestFlogger(int nThreads, T /*max*/, C /*compare*/) {
    REMARK("Testing queue flogger.\n");
    concurrent_priority_queue<T, C> *q = new concurrent_priority_queue<T, C> (0);
    NativeParallelFor(nThreads, FloggerBody<T, C >(nThreads, q));
    ASSERT(q->empty(), "FAILED flogger/empty test.");
    ASSERT(!q->size(), "FAILED flogger/size test.");
    REMARK("Flogging complete.\n");
    delete q;
}

void TestCpqOnNThreads(int nThreads) {
    std::less<int> int_compare;
    my_less data_compare;

    TestConstructorsDestructorsAccessors();
    TestAssignmentClearSwap();
    TestSerialPushPop(); 

    TestParallelPushPop(nThreads, INT_MAX, INT_MIN, int_compare);
    TestParallelPushPop(nThreads, (unsigned char)CHAR_MAX, (unsigned char)CHAR_MIN, int_compare);
    TestParallelPushPop(nThreads, DATA_MAX, DATA_MIN, data_compare);

    TestFlogger(nThreads, INT_MAX, int_compare);
    TestFlogger(nThreads, (unsigned char)CHAR_MAX, int_compare);
    TestFlogger(nThreads, DATA_MAX, data_compare);

#if TBB_USE_EXCEPTIONS && !__TBB_THROW_ACROSS_MODULE_BOUNDARY_BROKEN
    TestExceptions();
#else
    REPORT("Known issue: exception handling tests are skipped.\n");
#endif
}

int TestMain() {
    if (MinThread < 1)
        MinThread = 1;
    for (int p = MinThread; p <= MaxThread; ++p) {
        REMARK("Testing on %d threads.\n", p);
        TestCpqOnNThreads(p);
    }
    return Harness::Done;
}
