/*
 * string operation benchmarks
 *
 * Copyright (C) 2025 William Horvath
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#pragma once

#include <stdlib.h>

#ifdef _WIN32
# define stdlib "ucrtbase.dll"
# define stdlib_fb "msvcrt.dll"
# ifdef __i386__
#  define memlib "./libmembase32-windows-msvc.dll"
# else
#  define memlib "./libmembase64-windows-msvc.dll"
# endif
#elif defined(MUSL)
# define stdlib "libc.so"
# define stdlib_fb stdlib
# ifdef __i386__
#  define memlib "./libmembase32-linux-musl.so"
# else
#  define memlib "./libmembase64-linux-musl.so"
# endif
#else
# define stdlib "libc.so.6"
# define stdlib_fb stdlib
# ifdef __i386__
#  define memlib "./libmembase32-linux-gnu.so"
# else
#  define memlib "./libmembase64-linux-gnu.so"
# endif
#endif

#ifdef _WIN32
#include <windows.h>
#include <malloc.h>
#include <stdint.h>

#define RTLD_NOW 0
typedef HMODULE dl_handle;
#define dlopen(path, mode) LoadLibraryA(path)
#define dlclose(handle) FreeLibrary(handle)
#define dlerror() "Windows error"

static inline void *dlsym(dl_handle handle, const char *symbol)
{
    FARPROC proc = GetProcAddress(handle, symbol);
    union
    {
        FARPROC proc;
        void *ptr;
    } cast = {proc};
    return cast.ptr;
}

static inline void *__aligned_alloc(size_t alignment, size_t size)
{
    return _aligned_malloc(size, alignment);
}

static inline void __aligned_free(void *ptr)
{
    _aligned_free(ptr);
}

#else
#include <dlfcn.h>
#include <time.h>

typedef void *dl_handle;

static inline void *__aligned_alloc(size_t alignment, size_t size)
{
    return aligned_alloc(alignment, size);
}

static inline void __aligned_free(void *ptr)
{
    free(ptr);
}

#endif

struct timespec_portable
{
    int64_t tv_sec;
    int64_t tv_nsec;
};

static inline void get_monotonic_time(struct timespec_portable *ts)
{
    __asm__ __volatile__ ( "mfence" : : : "memory" );
#ifdef _WIN32
    static LARGE_INTEGER freq;
    static int init = 0;
    LARGE_INTEGER count;

    if (!init)
    {
        QueryPerformanceFrequency(&freq);
        init = 1;
    }

    QueryPerformanceCounter(&count);

    ts->tv_sec = count.QuadPart / freq.QuadPart;
    ts->tv_nsec = ((count.QuadPart % freq.QuadPart) * 1000000000) / freq.QuadPart;
#else
    struct timespec native_ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &native_ts);
    ts->tv_sec = native_ts.tv_sec;
    ts->tv_nsec = native_ts.tv_nsec;
#endif
    __asm__ __volatile__ ( "mfence" : : : "memory" );
}

static inline double timespec_to_seconds(const struct timespec_portable *start,
                                         const struct timespec_portable *end)
{
    return (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1e9;
}
