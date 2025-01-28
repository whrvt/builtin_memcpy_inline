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

#include "membase.h"

#ifndef __clang__
#error This file must be compiled with clang.
#endif

#define BASE_ALIGNMENT 16

#define FEAT_AVX512 3
#define FEAT_AVX2 2
#define FEAT_SSE2 1

#define AVX512_VECTOR_BITS 9
#define AVX2_VECTOR_BITS 8
#define SSE2_VECTOR_BITS 7

#define MEMCPY_STEP_FWD(d, s, n, size)           \
    do                                           \
    {                                            \
        if (n >= size)                           \
        {                                        \
            __builtin_memcpy_inline(d, s, size); \
            d += size;                           \
            s += size;                           \
            n -= size;                           \
        }                                        \
    } while (0)

#define MEMCPY_STEP_BWD(d, s, n, size)           \
    do                                           \
    {                                            \
        if (n >= size)                           \
        {                                        \
            d -= size;                           \
            s -= size;                           \
            __builtin_memcpy_inline(d, s, size); \
            n -= size;                           \
        }                                        \
    } while (0)

#define COPY_DIR(d, s, n, size, direction)  \
    do                                      \
    {                                       \
        if (likely(!direction))             \
            MEMCPY_STEP_FWD(d, s, n, size); \
        else                                \
            MEMCPY_STEP_BWD(d, s, n, size); \
    } while (0)

#define IMPLEMENT_MEMOP(maybe_inlineable, suffix, vector_size)                                        \
    NOBUILTIN [[gnu::aligned(vector_size)]]                                                           \
    static maybe_inlineable void *memop_##suffix(void *dst, const void *src, size_t n, int direction) \
    {                                                                                                 \
        char *d = (char *)dst + (unlikely(direction) ? n : 0);                                        \
        const char *s = (const char *)src + (unlikely(direction) ? n : 0);                            \
                                                                                                      \
        /* vector-sized copies in groups of 4 for better pipelining */                                \
        while (n >= 4 * vector_size)                                                                  \
        {                                                                                             \
            COPY_DIR(d, s, n, vector_size, direction);                                                \
            COPY_DIR(d, s, n, vector_size, direction);                                                \
            COPY_DIR(d, s, n, vector_size, direction);                                                \
            COPY_DIR(d, s, n, vector_size, direction);                                                \
        }                                                                                             \
                                                                                                      \
        /* remaining vectors */                                                                       \
        while (n >= vector_size)                                                                      \
        {                                                                                             \
            COPY_DIR(d, s, n, vector_size, direction);                                                \
        }                                                                                             \
                                                                                                      \
        /* remaining bytes */                                                                         \
        COPY_DIR(d, s, n, 32, direction);                                                             \
        COPY_DIR(d, s, n, 16, direction);                                                             \
        COPY_DIR(d, s, n, 8, direction);                                                              \
        COPY_DIR(d, s, n, 4, direction);                                                              \
        COPY_DIR(d, s, n, 2, direction);                                                              \
        COPY_DIR(d, s, n, 1, direction);                                                              \
                                                                                                      \
        return dst;                                                                                   \
    }

#ifndef __AVX512F__
#pragma clang attribute push(__attribute__((target("avx512f"))), apply_to = function)
#define has_avx512f cpu_supports(FEAT_AVX512)
#define inlineable_avx512f
#else
#define has_avx512f 1
#define inlineable_avx512f inline
#endif

IMPLEMENT_MEMOP(inlineable_avx512f, avx512, 1ULL << (AVX512_VECTOR_BITS - 3))

#ifndef __AVX512F__
#pragma clang attribute pop
#endif

#ifndef __AVX2__
#pragma clang attribute push(__attribute__((target("avx2"))), apply_to = function)
#define has_avx2 likely(cpu_supports(FEAT_AVX2))
#define inlineable_avx2
#else
#define has_avx2 1
#define inlineable_avx2 inline
#endif

IMPLEMENT_MEMOP(inlineable_avx2, avx2, 1ULL << (AVX2_VECTOR_BITS - 3))

#ifndef __AVX2__
#pragma clang attribute pop
#endif

#ifndef __SSE2__
#pragma clang attribute push(__attribute__((target("sse2"))), apply_to = function)
#define has_sse2 likely(cpu_supports(FEAT_SSE2))
#define inlineable_sse2
#else
#define has_sse2 1
#define inlineable_sse2 inline
#endif

IMPLEMENT_MEMOP(inlineable_sse2, sse2, 1ULL << (SSE2_VECTOR_BITS - 3))

#ifndef __SSE2__
#pragma clang attribute pop
#endif

NOBUILTIN
static inline void *memop_scalar(void *dst, const void *src, size_t n, int direction)
{
    char *d = (char *)dst + (unlikely(direction) ? n : 0);
    const char *s = (const char *)src + (unlikely(direction) ? n : 0);

    while (n >= 32)
    {
        COPY_DIR(d, s, n, 32, direction);
    }

    COPY_DIR(d, s, n, 16, direction);
    COPY_DIR(d, s, n, 8, direction);
    COPY_DIR(d, s, n, 4, direction);
    COPY_DIR(d, s, n, 2, direction);
    COPY_DIR(d, s, n, 1, direction);

    return dst;
}

NOBUILTIN NOINLINE 
void MEMAPI *memcpy_local(void *dst, const void *src, size_t n)
{
    if (has_avx512f)
        return memop_avx512(dst, src, n, 0);
    if (has_avx2)
        return memop_avx2(dst, src, n, 0);
    if (has_sse2)
        return memop_sse2(dst, src, n, 0);
    return memop_scalar(dst, src, n, 0);
}

NOBUILTIN NOINLINE 
void MEMAPI *memmove_local(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    if (d == s)
        return dst;

    if (likely(d < s || d >= s + n))
        return memcpy_local(dst, src, n);

    if (has_avx512f)
        return memop_avx512(dst, src, n, 1);
    if (has_avx2)
        return memop_avx2(dst, src, n, 1);
    if (has_sse2)
        return memop_sse2(dst, src, n, 1);
    return memop_scalar(dst, src, n, 1);
}
