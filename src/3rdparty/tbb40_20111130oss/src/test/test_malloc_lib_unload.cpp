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

#if _USRDLL

#include <stdlib.h> // for NULL
#include "harness_assert.h"
#define HARNESS_CUSTOM_MAIN 1
#define HARNESS_NO_PARSE_COMMAND_LINE 1
#include "harness.h"

const char *globalCallMsg = "A TBB allocator function call is resolved into wrong implementation.";

#if _WIN32||_WIN64
// must be defined in DLL to linker not drop the dependence to the DLL.
extern "C" {
    extern __declspec(dllexport) void *scalable_malloc(size_t);
    extern __declspec(dllexport) void scalable_free (void *);
    extern __declspec(dllexport) void safer_scalable_free (void *, void (*)(void*));
    extern __declspec(dllexport) void *scalable_realloc(void *, size_t);
    extern __declspec(dllexport) void *safer_scalable_realloc(void *, size_t, void *);
    extern __declspec(dllexport) void *scalable_calloc(size_t, size_t);
    extern __declspec(dllexport) int scalable_posix_memalign(void **, size_t, size_t);
    extern __declspec(dllexport) void *scalable_aligned_malloc(size_t, size_t);
    extern __declspec(dllexport) void *scalable_aligned_realloc(void *, size_t, size_t);
    extern __declspec(dllexport) void *safer_scalable_aligned_realloc(void *, size_t, size_t, void *);
    extern __declspec(dllexport) void scalable_aligned_free(void *);
    extern __declspec(dllexport) size_t scalable_msize(void *);
    extern __declspec(dllexport) size_t safer_scalable_msize (void *, size_t (*)(void*));
}
#endif

// Those functions must not be called instead of presented in dynamic library. 
extern "C" void *scalable_malloc(size_t)
{
    ASSERT(0, globalCallMsg);
    return NULL;
}
extern "C" void scalable_free (void *)
{
    ASSERT(0, globalCallMsg);
}
extern "C" void safer_scalable_free (void *, void (*)(void*)) 
{
    ASSERT(0, globalCallMsg);
}
extern "C" void *scalable_realloc(void *, size_t)
{
    ASSERT(0, globalCallMsg);
    return NULL;
}
extern "C" void *safer_scalable_realloc(void *, size_t, void *) 
{
    ASSERT(0, globalCallMsg);
    return NULL;
}
extern "C" void *scalable_calloc(size_t, size_t)
{
    ASSERT(0, globalCallMsg);
    return NULL;
}
extern "C" int scalable_posix_memalign(void **, size_t, size_t)
{
    ASSERT(0, globalCallMsg);
    return 0;
}
extern "C" void *scalable_aligned_malloc(size_t, size_t)
{
    ASSERT(0, globalCallMsg);
    return NULL;
}
extern "C" void *scalable_aligned_realloc(void *, size_t, size_t)
{
    ASSERT(0, globalCallMsg);
    return NULL;
}
extern "C" void *safer_scalable_aligned_realloc(void *, size_t, size_t, void *)
{
    ASSERT(0, globalCallMsg);
    return NULL;
}
extern "C" void scalable_aligned_free(void *)
{
    ASSERT(0, globalCallMsg);
}
extern "C" size_t scalable_msize(void *)
{
    ASSERT(0, globalCallMsg);
    return 0;
}
extern "C" size_t safer_scalable_msize (void *, size_t (*)(void*)) 
{
    ASSERT(0, globalCallMsg);
    return 0;
}

#else  // _USRDLL

#include <cstdlib>
#if _WIN32 || _WIN64
#include "tbb/machine/windows_api.h"
#else
#include <dlfcn.h>
#endif
#include "tbb/tbb_stddef.h"
#define HARNESS_NO_PARSE_COMMAND_LINE 1
#include "harness.h"
#include "harness_memory.h"

#if TBB_USE_DEBUG
#define SUFFIX1 "_debug"
#define SUFFIX2
#else
#define SUFFIX1
#define SUFFIX2 "_debug"
#endif /* TBB_USE_DEBUG */

#if _WIN32||_WIN64
#define PREFIX
#define EXT ".dll"
#else
#define PREFIX "lib"
#if __APPLE__
#define EXT ".dylib"
#elif __linux__
#define EXT __TBB_STRING(.so.TBB_COMPATIBLE_INTERFACE_VERSION)
#elif __FreeBSD__ || __NetBSD__ || __sun || _AIX
#define EXT ".so"
#else
#error Unknown OS
#endif
#endif

// Form the names of the TBB memory allocator binaries.
#define MALLOCLIB_NAME1 PREFIX "tbbmalloc" SUFFIX1 EXT
#define MALLOCLIB_NAME2 PREFIX "tbbmalloc" SUFFIX2 EXT

#if _WIN32 || _WIN64
#define LIBRARY_HANDLE HMODULE
#define LOAD_LIBRARY(name) LoadLibrary((name))
#else
#define LIBRARY_HANDLE void*
#define LOAD_LIBRARY(name) dlopen((name), RTLD_NOW|RTLD_GLOBAL)
#endif

extern "C" {
#if _WIN32||_WIN64
extern __declspec(dllimport)
#endif
void *scalable_malloc(size_t);
}

struct Run {
    void operator()( int /*id*/ ) const {
        void* (*malloc_ptr)(std::size_t);
        void (*free_ptr)(void*);
        
        void* (*aligned_malloc_ptr)(size_t size, size_t alignment);
        void  (*aligned_free_ptr)(void*);

        const char* actual_name;
        LIBRARY_HANDLE lib = LOAD_LIBRARY(actual_name = MALLOCLIB_NAME1);
        if (!lib)      lib = LOAD_LIBRARY(actual_name = MALLOCLIB_NAME2);
        if (!lib) {
            REPORT("Can't load " MALLOCLIB_NAME1 " or " MALLOCLIB_NAME2 "\n");
            exit(1);
        }
#if _WIN32 || _WIN64
        // casts at both sides are to soothe MinGW compiler
        (void *&)malloc_ptr = (void*)GetProcAddress(lib, "scalable_malloc");
        (void *&)free_ptr = (void*)GetProcAddress(lib, "scalable_free");
        (void *&)aligned_malloc_ptr = (void*)GetProcAddress(lib, "scalable_aligned_malloc");
        (void *&)aligned_free_ptr = (void*)GetProcAddress(lib, "scalable_aligned_free");
#else
        (void *&)malloc_ptr = dlsym(lib, "scalable_malloc");
        (void *&)free_ptr = dlsym(lib, "scalable_free");
        (void *&)aligned_malloc_ptr = dlsym(lib, "scalable_aligned_malloc");
        (void *&)aligned_free_ptr = dlsym(lib, "scalable_aligned_free");
#endif
        if (!malloc_ptr || !free_ptr)  {
            REPORT("Can't find scalable_(malloc|free) in %s \n", actual_name);
            exit(1);
        }

        for (size_t sz = 1024; sz <= 10*1024 ; sz*=10) {
            void *p1 = aligned_malloc_ptr(sz, 16);
            memset(p1, 0, sz);
            aligned_free_ptr(p1);
        }

        void *p = malloc_ptr(100);
        memset(p, 1, 100);
        free_ptr(p);

#if _WIN32 || _WIN64
        BOOL ret = FreeLibrary(lib);
        ASSERT(ret, "FreeLibrary must be successful");
        ASSERT(GetModuleHandle(actual_name),  
               "allocator library must not be unloaded");
#else
        int ret = dlclose(lib);
        ASSERT(ret == 0, "dlclose must be successful");
        ASSERT(dlsym(RTLD_DEFAULT, "scalable_malloc"),  
               "allocator library must not be unloaded");
#endif
    }
};

int TestMain () {
    int i;
    std::ptrdiff_t memory_leak;

    // warm-up run
    NativeParallelFor( 1, Run() );
    /* 1st call to GetMemoryUsage() allocate some memory,
       but it seems memory consumption stabilized after this.
     */
    GetMemoryUsage();
    std::size_t memory_in_use = GetMemoryUsage();
    ASSERT(memory_in_use == GetMemoryUsage(), 
           "Memory consumption should not increase after 1st GetMemoryUsage() call");

    // expect that memory consumption stabilized after several runs
    for (i=0; i<3; i++) {
        std::size_t memory_in_use = GetMemoryUsage();
        for (int j=0; j<10; j++)
            NativeParallelFor( 1, Run() );
        memory_leak = GetMemoryUsage() - memory_in_use;
        if (memory_leak == 0)  // possibly too strong?
            break;
    }
    if(3==i) {
        // not stabilized, could be leak
        REPORT( "Error: memory leak of up to %ld bytes\n", static_cast<long>(memory_leak));
        exit(1);
    }

    return Harness::Done;
}

#endif // _USRDLL
