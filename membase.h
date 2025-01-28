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
#include <intrin.h>
#ifdef SHARED
#define MEMAPI __declspec(dllexport)
#else
#define MEMAPI
#endif
#else
#define MEMAPI __attribute__((visibility("default")))
#endif

#if defined(_WIN32) || !__has_builtin(__builtin_cpu_supports)

#if !__has_builtin(__cpuidex)
#if defined(_MSC_VER) && !defined(__clang__)
void __cpuidex(int info[4], int ax, int cx);
#pragma intrinsic(__cpuidex)
#else
#define __cpuidex(info, ax, cx) __asm__("cpuid" : "=a"(info[0]), "=b"(info[1]), "=c"(info[2]), "=d"(info[3]) : "a"(ax), "c"(cx))
#endif
#endif

#if !__has_builtin(__cpuid)
#if defined(_MSC_VER) && !defined(__clang__)
void __cpuid(int info[4], int ax);
#pragma intrinsic(__cpuid)
#else
#define __cpuid(info, ax) __cpuidex(info, ax, 0)
#endif
#endif

#endif

static inline int cpu_supports(const int featurelevel)
{
    static int cpu_featurelevel = -1;
    if (unlikely(cpu_featurelevel < 0))
    {
        cpu_featurelevel = 0;
#if defined(_WIN32) || !__has_builtin(__builtin_cpu_supports)
        int regs[4];
        int extended_regs[4];

        __cpuid(regs, 1);
        __cpuidex(extended_regs, 7, 0);

        const int edx_features = regs[3];
        const int ecx_features = regs[2];
        const int ebx_features = extended_regs[1];

        cpu_featurelevel += !!(edx_features & (1 << 26)) + (ecx_features & (1 << 28)) && (ebx_features & (1 << 5)) + !!(ebx_features & (1 << 16));
#else
        cpu_featurelevel += __builtin_cpu_supports("avx512f") + __builtin_cpu_supports("avx2") + __builtin_cpu_supports("sse2");
#endif
    }
    return (cpu_featurelevel >= featurelevel);
}

#ifndef SHARED
NOINLINE void MEMAPI *memcpy_local(void *dst, const void *src, size_t n);
NOINLINE void MEMAPI *memmove_local(void *dst, const void *src, size_t n);
#endif
