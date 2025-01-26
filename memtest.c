/*
 * string operation tests
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

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

void *memcpy_local(void *dst, const void *src, size_t n);
void *memmove_local(void *dst, const void *src, size_t n);

static int failed_tests = 0;
static int total_tests = 0;
static size_t page_size;

typedef void *(*stringop_fn)(void *, const void *, size_t);

static void test_failed(const char *op, const char *msg, size_t align1, size_t align2, size_t len,
                        const unsigned char *expected, const unsigned char *actual)
{
    printf("fail [%s]: %s (align1=%zu, align2=%zu, len=%zu)\n", op, msg, align1, align2, len);
    printf("first 32 bytes or up to len (expected vs actual):\n");
    size_t show_len = len < 32 ? len : 32;
    for (size_t i = 0; i < show_len; i++)
    {
        printf("%02x ", expected[i]);
    }
    printf("\nvs\n");
    for (size_t i = 0; i < show_len; i++)
    {
        printf("%02x ", actual[i]);
    }
    printf("\n\n");
    failed_tests++;
}

static void run_alignment_test(const char *op, stringop_fn fn, size_t align1, size_t align2, size_t len)
{
    const size_t guard_size = 64;
    const size_t max_align = 64;

    /* total size needs:
     * - front padding for alignment (max_align)
     * - front guard (guard_size)
     * - data region (len)
     * - back guard (guard_size)
     * - extra alignment slack (max_align) */
    const size_t total_size = max_align + guard_size + len + guard_size + max_align;

    unsigned char *s1_base = malloc(total_size);
    unsigned char *s2_base = malloc(total_size);
    unsigned char *guard1 = malloc(guard_size);
    unsigned char *guard2 = malloc(guard_size);

    if (!s1_base || !s2_base || !guard1 || !guard2)
    {
        fprintf(stderr, "memory allocation failed\n");
        exit(1);
    }

    /* fill entire buffers with a debug pattern to detect uninitialized reads */
    memset(s1_base, 0xDB, total_size);
    memset(s2_base, 0xDB, total_size);

    memset(guard1, 0xA5, guard_size);
    memset(guard2, 0xA5, guard_size);

    unsigned char *s1 = s1_base + max_align + guard_size;
    unsigned char *s2 = s2_base + max_align + guard_size;

    unsigned char *aligned_src = (unsigned char *)(((uintptr_t)s1 + align1) & ~(uintptr_t)0x3F);
    unsigned char *aligned_dst = (unsigned char *)(((uintptr_t)s2 + align2) & ~(uintptr_t)0x3F);

    /* initialize with some "weird-ish" pattern */
#pragma clang optimize off
    for (size_t i = 0; i < len; i++)
    {
        aligned_src[i] = (unsigned char)((i * 7 + 13) & 0xFF);
    }
#pragma clang optimize on

    memset(aligned_dst, 0xCC, len);

    /* setup guard regions */
    memcpy(aligned_src - guard_size, guard1, guard_size); /* front */
    memcpy(aligned_src + len, guard2, guard_size);        /* back */
    memcpy(aligned_dst - guard_size, guard1, guard_size); /* front */
    memcpy(aligned_dst + len, guard2, guard_size);        /* back */

    /* run the actual test with our string func */
    void *result = fn(aligned_dst, aligned_src, len);

    if (result != aligned_dst)
    {
        test_failed(op, "wrong return value", align1, align2, len, aligned_src, aligned_dst);
    }

    if (memcmp(aligned_src, aligned_dst, len) != 0)
    {
        test_failed(op, "content mismatch", align1, align2, len, aligned_src, aligned_dst);
    }

    if (memcmp(aligned_src - guard_size, guard1, guard_size) != 0 ||
        memcmp(aligned_dst - guard_size, guard1, guard_size) != 0)
    {
        test_failed(op, "front guard corrupted", align1, align2, len, guard1, aligned_dst - guard_size);
    }

    if (memcmp(aligned_src + len, guard2, guard_size) != 0 ||
        memcmp(aligned_dst + len, guard2, guard_size) != 0)
    {
        test_failed(op, "back guard corrupted", align1, align2, len, guard2, aligned_dst + len);
    }

    free(s1_base);
    free(s2_base);
    free(guard1);
    free(guard2);
    total_tests++;
}

static void run_overlap_test(const char *op, ssize_t offset, ssize_t len, stringop_fn memmove_fn)
{
    /* allocate with extra space for:
     * - front guard region
     * - padding for max possible offset
     * - main buffer
     * - back guard region */
    const size_t guard_size = 64;
    const size_t total_size = (2 * guard_size) + len + llabs(offset);

    unsigned char *buffer_base = malloc(total_size);
    unsigned char *guard = malloc(guard_size);

    if (!buffer_base || !guard)
    {
        fprintf(stderr, "memory allocation failed\n");
        exit(1);
    }

    memset(guard, 0xA5, guard_size);

    unsigned char *buffer = buffer_base + guard_size;

    unsigned char *src = (offset < 0) ? buffer + (-offset) : buffer;
    unsigned char *dst = (offset < 0) ? buffer : buffer + offset;

#pragma clang optimize off
    for (ssize_t i = 0; i < len; i++)
    {
        src[i] = (unsigned char)((i * 11 + 7) & 0xFF);
    }
#pragma clang optimize on

    memcpy(buffer_base, guard, guard_size);                           /* Front guard */
    memcpy(buffer_base + total_size - guard_size, guard, guard_size); /* Back guard */

    unsigned char *reference = malloc(len);
    if (!reference)
    {
        fprintf(stderr, "memory allocation failed\n");
        exit(1);
    }
    memcpy(reference, src, len);

    void *result = memmove_fn(dst, src, len);

    if (result != dst)
    {
        test_failed(op, "wrong return value for overlap test",
                    0, (offset < 0) ? -offset : offset, len, reference, dst);
    }

    if (memcmp(reference, dst, len) != 0)
    {
        test_failed(op, "content mismatch for overlap test",
                    0, (offset < 0) ? -offset : offset, len, reference, dst);
    }

    /* Check guard areas */
    if (memcmp(buffer_base, guard, guard_size) != 0 ||
        memcmp(buffer_base + total_size - guard_size, guard, guard_size) != 0)
    {
        test_failed(op, "guard area corrupted in overlap test",
                    0, (offset < 0) ? -offset : offset, len, guard, buffer_base);
    }

    free(buffer_base);
    free(guard);
    free(reference);
    total_tests++;
}

static void run_large_test(const char *op, stringop_fn fn)
{
    const size_t size = 1024 * 1024;
    const size_t aligned_size = (size + 63) & ~63;

    const size_t total_size = aligned_size + (2 * page_size);

    char *src_base = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    char *dst_base = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (src_base == MAP_FAILED || dst_base == MAP_FAILED)
    {
        fprintf(stderr, "failed to allocate memory for large test\n");
        return;
    }

    mprotect(src_base, page_size, PROT_NONE);
    mprotect(src_base + total_size - page_size, page_size, PROT_NONE);
    mprotect(dst_base, page_size, PROT_NONE);
    mprotect(dst_base + total_size - page_size, page_size, PROT_NONE);

    char *src = (char *)(((uintptr_t)(src_base + page_size) + 63) & ~63);
    char *dst = (char *)(((uintptr_t)(dst_base + page_size) + 63) & ~63);

#pragma clang optimize off
    for (size_t i = 0; i < size; i++)
    {
        src[i] = (i * 13 + 7) & 0xFF;
    }
#pragma clang optimize on

    void *result = fn(dst, src, size);

    if (result != dst)
    {
        printf("fail [%s]: large buffer test return value mismatch\n", op);
        failed_tests++;
    }

    if (memcmp(src, dst, size) != 0)
    {
        printf("fail [%s]: large buffer test content mismatch\n", op);
        failed_tests++;
    }

    munmap(src_base, total_size);
    munmap(dst_base, total_size);
    total_tests++;
}

static void test_operation(const char *op, stringop_fn fn)
{
    printf("\ntesting %s...\n", op);

    printf("running exhaustive small size tests...\n");
    for (size_t len = 1; len <= 64; len++)
    {
        /* Test every possible alignment combination up to 16 bytes */
        for (size_t align1 = 0; align1 < 16; align1++)
        {
            for (size_t align2 = 0; align2 < 16; align2++)
            {
                run_alignment_test(op, fn, align1, align2, len);
            }
        }
    }

    printf("\nrunning power-of-two size tests...\n");
    for (size_t i = 64; i <= 8192; i *= 2)
    {
        /* test critical alignments */
        size_t alignments[] = {0, 1, 7, 8, 15, 16, 31, 32};
        for (size_t j = 0; j < sizeof(alignments) / sizeof(alignments[0]); j++)
        {
            for (size_t k = 0; k < sizeof(alignments) / sizeof(alignments[0]); k++)
            {
                run_alignment_test(op, fn, alignments[j], alignments[k], i);
            }
        }

        run_alignment_test(op, fn, 0, 0, i - 1);
        run_alignment_test(op, fn, 0, 0, i + 1);
    }

    /* test near vector size boundaries */
    size_t vector_sizes[] = {16, 32, 64}; /* SSE2, AVX, AVX-512 */
    for (size_t i = 0; i < sizeof(vector_sizes) / sizeof(vector_sizes[0]); i++)
    {
        size_t vec_size = vector_sizes[i];
        for (size_t offset = 1; offset <= 4; offset++)
        {
            run_alignment_test(op, fn, 0, 0, vec_size - offset);
            run_alignment_test(op, fn, 0, 0, vec_size + offset);
            run_alignment_test(op, fn, offset, 0, vec_size);
            run_alignment_test(op, fn, 0, offset, vec_size);
        }
    }

    printf("\nrunning large buffer test...\n");
    run_large_test(op, fn);
}

static void test_memmove_overlaps(stringop_fn fn)
{
    printf("\ntesting memmove overlap cases...\n");

    size_t sizes[] = {1, 2, 3, 4, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        ssize_t size = sizes[i];

        for (ssize_t offset = 1; offset <= size; offset *= 2)
        {
            run_overlap_test("memmove", offset, size, fn);
            run_overlap_test("memmove", -offset, size, fn);

            /* Test offsets around this power of two */
            if (offset > 1)
            {
                run_overlap_test("memmove", offset - 1, size, fn);
                run_overlap_test("memmove", offset + 1, size, fn);
                run_overlap_test("memmove", -(offset - 1), size, fn);
                run_overlap_test("memmove", -(offset + 1), size, fn);
            }
        }

        run_overlap_test("memmove", 1, size, fn);           /* minimal forward overlap */
        run_overlap_test("memmove", -1, size, fn);          /* minimal backward overlap */
        run_overlap_test("memmove", size - 1, size, fn);    /* maximum forward overlap */
        run_overlap_test("memmove", -(size - 1), size, fn); /* maximum backward overlap */
    }
}

int main(int argc, char *argv[])
{
    unsigned int failed_temp = 0;
    page_size = sysconf(_SC_PAGESIZE);

    const char *test_type = (argc > 1) ? argv[1] : "all";

    if (strcmp(test_type, "memcpy") == 0 || strcmp(test_type, "all") == 0)
    {
        test_operation("memcpy", memcpy_local);
        failed_temp = failed_tests;
        if (!failed_temp)
            printf("\nall memcpy tests passed.\n");
    }

    if (strcmp(test_type, "memmove") == 0 || strcmp(test_type, "all") == 0)
    {
        test_operation("memmove", memmove_local);
        test_memmove_overlaps(memmove_local);
        failed_temp = failed_tests - failed_temp;
        if (!failed_temp)
            printf("\nall memmove tests passed.\n");
    }

    if (failed_tests == 0)
    {
        printf("\nall tests passed.\n");
        return 0;
    }
    else
    {
        printf("\n%d tests out of %d total failed.\n", failed_tests, total_tests);
        return 1;
    }
}
