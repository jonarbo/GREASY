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

#ifndef harness_tbb_independence_H
#define harness_tbb_independence_H

#if __TBB_DYNAMIC_LOAD_ENABLED
#include "../tbb/tbb_assert_impl.h"
#endif

#if __linux__  && __ia64__

#define __TBB_NO_IMPLICIT_LINKAGE 1
#include "tbb/tbb_machine.h"

#include <pthread.h>

// Can't use Intel compiler intrinsic due to internal error reported by 10.1 compiler
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

int32_t __TBB_machine_fetchadd4__TBB_full_fence (volatile void *ptr, int32_t value)
{
    pthread_mutex_lock(&counter_mutex);
    int32_t result = *(int32_t*)ptr;
    *(int32_t*)ptr = result + value;
    pthread_mutex_unlock(&counter_mutex);
    return result;
}

int64_t __TBB_machine_fetchadd8__TBB_full_fence (volatile void *ptr, int64_t value)
{
    pthread_mutex_lock(&counter_mutex);
    int32_t result = *(int32_t*)ptr;
    *(int32_t*)ptr = result + value;
    pthread_mutex_unlock(&counter_mutex);
    return result;
}

void __TBB_machine_pause(int32_t /*delay*/) {  __TBB_Yield(); }

pthread_mutex_t cas_mutex = PTHREAD_MUTEX_INITIALIZER;

extern "C" int64_t __TBB_machine_cmpswp8__TBB_full_fence(volatile void *ptr, int64_t value, int64_t comparand)
{
    pthread_mutex_lock(&cas_mutex);
    int64_t result = *(int64_t*)ptr;
    if (result == comparand)
        *(int64_t*)ptr = value;
    pthread_mutex_unlock(&cas_mutex);
    return result;
}

#elif _WIN64 && defined(_M_X64) && !__MINGW64__

#define __TBB_NO_IMPLICIT_LINKAGE 1
#include "tbb/tbb_machine.h"

#if __TBB_DYNAMIC_LOAD_ENABLED
extern "C" {
void __TBB_machine_pause(__int32 /*delay*/ ) { __TBB_Yield(); }
}
#endif

#endif /* _WIN64 */

extern "C" void ITT_DoOneTimeInitialization() { }

#endif // harness_tbb_independence_H
