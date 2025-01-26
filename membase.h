/*
 * memmove, memcpy implementation
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

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <immintrin.h>

#define NOBUILTIN [[clang::no_builtin("memcpy", "memmove", "memset", "memcmp")]]

#ifndef FORCEINLINE
#define FORCEINLINE __attribute__((always_inline)) inline
#endif

#ifndef NOINLINE
#ifdef __GNUC__
#define NOINLINE __attribute__((noinline))
#elif __has_declspec_attribute(noinline)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE
#endif
#endif

#ifndef likely
#define likely(x) __builtin_expect(x, 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(x, 0)
#endif

#ifdef _WIN32
#ifdef SHARED
#define MEMAPI __declspec(dllexport)
#else
#define MEMAPI
#endif
#else
#define MEMAPI __attribute__((visibility("default")))
#endif

#if defined(_WIN32) && !__has_builtin(__cpu_has_feature)
#include <intrin.h>

static int initialized = 0;

static struct
{
    unsigned char sse2;
    unsigned char avx2;
    unsigned char avx512f;
} cpu_features = {0};

static inline void cpufeat_init(void)
{
    int regs[4];
    int extended_regs[4];

    __cpuid(regs, 1);
    __cpuidex(extended_regs, 7, 0);

    const int edx_features = regs[3];
    const int ecx_features = regs[2];
    const int ebx_features = extended_regs[1];

    cpu_features.sse2 = !!(edx_features & (1 << 26));
    cpu_features.avx2 = (ecx_features & (1 << 28)) &&
                        (ebx_features & (1 << 5));
    cpu_features.avx512f = !!(ebx_features & (1 << 16));

    initialized = 1;
}

static FORCEINLINE int __builtin_cpu_supports(const char *feature)
{
    if (likely(initialized))
    switch (feature[0])
    {
    case 's':
        return cpu_features.sse2;
    case 'a':
        return feature[4] ? cpu_features.avx512f : cpu_features.avx2;
    default:
        return 0;
    }
    cpufeat_init();
    return __builtin_cpu_supports(feature);
}
#endif

#ifndef SHARED
void MEMAPI *memcpy_local(void *dst, const void *src, size_t n);
void MEMAPI *memmove_local(void *dst, const void *src, size_t n);
#endif
