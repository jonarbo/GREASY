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

#include "tbb/tbb_config.h"

// Skip the test if no interoperability with cilkrts
#define __TBB_CILK_INTEROP   (__TBB_SURVIVE_THREAD_SWITCH && __INTEL_COMPILER>=1200)
// Skip the test when cilkrts did not have dlopen()/dlclose() start up feature
#define CILK_SYMBOLS_VISIBLE (_WIN32||_WIN64)
// The compiler does not add "-lcilkrts" linker option on some linux systems
#define CILK_LINKAGE_BROKEN  (__linux__ && __GNUC__<4 && __INTEL_COMPILER_BUILD_DATE <= 20110427)
// In U4, cilkrts incorrectly sends the interop notifications to TBB
#define CILK_NOTIFICATIONS_BROKEN ( __INTEL_COMPILER_BUILD_DATE == 20110427 )

#if __TBB_CILK_INTEROP && CILK_SYMBOLS_VISIBLE && !CILK_LINKAGE_BROKEN && !CILK_NOTIFICATIONS_BROKEN

#if _WIN32 || _WIN64
#include "tbb/machine/windows_api.h"
#else
#include <dlfcn.h>
#endif /* _WIN32 || _WIN64 */

#include "tbb/task_scheduler_init.h"
#include "tbb/task.h"

static const int N = 25;
static const int P_outer = 4;
static const int P_nested = 2;

#ifdef _USRDLL

#include <cilk/cilk.h>
#define HARNESS_CUSTOM_MAIN 1
#include "harness.h"
#undef HARNESS_CUSTOM_MAIN

#if _WIN32 || _WIN64
#define CILK_TEST_EXPORT extern "C" __declspec(dllexport)
#else
#define CILK_TEST_EXPORT extern "C"
#endif /* _WIN32 || _WIN64 */

bool g_sandwich = true; // have to be declare before #include "test_cilk_common.h"
#include "test_cilk_common.h"

CILK_TEST_EXPORT int CilkFib( int n )
{
    return TBB_Fib(n);
}

CILK_TEST_EXPORT void CilkShutdown()
{
    __cilkrts_end_cilk();
}

#else /* _USRDLL undefined */

#include "harness.h"

int SerialFib( int n ) {
    int a=0, b=1;
    for( int i=0; i<n; ++i ) {
        b += a;
        a = b-a;
    }
    return a;
}

int F = SerialFib(N);

typedef int (*CILK_CALL)(int);
CILK_CALL CilkFib = 0;

typedef void (*CILK_SHUTDOWN)();
CILK_SHUTDOWN CilkShutdown = 0;

class FibTask: public tbb::task {
    int n;
    int& result;
    /*override*/ task* execute() {
        if( n<2 ) {
            result = n;
        } else {

#if _WIN32 || _WIN64
            HMODULE hLib = LoadLibrary("test_cilk_dynamic_load_dll.dll");
            ASSERT( hLib, "failed to load test_cilk_dynamic_load_dll" );

            CilkFib = (CILK_CALL) GetProcAddress(hLib, "CilkFib");
            ASSERT( CilkFib, "failed to get address" );

            CilkShutdown = (CILK_SHUTDOWN) GetProcAddress(hLib, "CilkShutdown");
            ASSERT( CilkShutdown, "failed to get address" );
#else /* !WIN */
        #if __APPLE__
            #define LIBRARY_NAME(base) base".dylib"
        #else
            #define LIBRARY_NAME(base) base".so"
        #endif /* __APPLE__ */
            void *hLib = dlopen( LIBRARY_NAME("test_cilk_dynamic_load_dll"), RTLD_LAZY );
            ASSERT( hLib, "failed to load test_cilk_dynamic_load_dll" );

            CilkFib = (CILK_CALL) dlsym(hLib, "CilkFib");
            ASSERT( CilkFib, "failed to get address" );

            CilkShutdown = (CILK_SHUTDOWN) dlsym(hLib, "CilkShutdown");
            ASSERT( CilkShutdown, "failed to get address" );
#endif /* !WIN */

            int x, y;
            x = CilkFib(n-2);
            y = CilkFib(n-1);
            result = x+y;

            CilkShutdown();

#if _WIN32 || _WIN64
            FreeLibrary(hLib);
#else
            dlclose(hLib);
#endif
        }
        return NULL;
    }
public:
    FibTask( int& result_, int n_ ) : result(result_), n(n_) {}
};


int TBB_Fib( int n ) {
    if( n<2 ) {
        return n;
    } else {
        int result;
        tbb::task_scheduler_init init(P_nested);
        tbb::task::spawn_root_and_wait(*new( tbb::task::allocate_root()) FibTask(result,n) );
        return result;
    }
}

void RunSandwich() { 
    tbb::task_scheduler_init init(P_outer);
    int m = TBB_Fib(N);
    ASSERT( m == F, NULL );
}

int TestMain () {
    for ( int i = 0; i < 20; ++i )
        RunSandwich();
    return Harness::Done;
}

#endif /* _USRDLL */

#else /* !__TBB_CILK_INTEROP */

#include "harness.h"

int TestMain () {
    return Harness::Skipped;
}

#endif /* !__TBB_CILK_INTEROP */
