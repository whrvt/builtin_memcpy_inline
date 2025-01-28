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

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "membench.h"

#ifndef SHARED
void *memcpy_local(void *dst, const void *src, size_t n);
void *memmove_local(void *dst, const void *src, size_t n);
#endif

#define ALIGNMENT_HEADER "transfer size : test case       |   best GB/s   worst GB/s   avg GB/s\n"
#define SEPARATOR        "--------------------------------|------------------------------------\n"

#define DEFAULT_TEST_DURATION_NS (500 * 1000 * 1000) /* 500ms (not even close to accurate) */

typedef void *(*stringop_fn)(void *, const void *, size_t);

struct perf_stats
{
    double total_gb;
    double count;
    double min_gb;
    double max_gb;
};

struct test_results
{
    struct perf_stats memcpy_aligned;
    struct perf_stats memcpy_unaligned;
    struct perf_stats memmove_forward;
    struct perf_stats memmove_backward;
    size_t total_tests;
};

struct test_case
{
    const char *name;
    union
    {
        struct
        {
            size_t src_align;
            size_t dst_align;
        };
        struct
        {
            size_t overlap_offset;
            int backwards;
        };
    };
};

struct lib_functions
{
    stringop_fn memcpy_fn;
    stringop_fn memmove_fn;
    const char *name;
    struct test_results results;
    dl_handle handle;
};

static struct lib_functions implementations[2] = {
    {
#ifndef SHARED
        .memcpy_fn = memcpy_local, .memmove_fn = memmove_local,
#endif
        .name = "our", .results = {0}, .handle = NULL},
    {
#ifndef SHARED
        .memcpy_fn = memcpy, .memmove_fn = memmove,
#endif
        .name = "stdlib", .results = {0}, .handle = NULL}};

#ifdef SHARED
static void load_functions(struct lib_functions *impl)
{

    const char *lib, *lib_fb, *memcpy_fn, *memmove_fn;
    if (strncmp(impl->name, "stdlib", strlen(impl->name)))
    {
        lib = stdlib;
        lib_fb = stdlib_fb;

        memcpy_fn = "memcpy";
        memmove_fn = "memmove";
    }
    else
    {
        lib = memlib;
        lib_fb = memlib;

        memcpy_fn = "memcpy_local";
        memmove_fn = "memmove_local";
    }

    impl->handle = dlopen(lib, RTLD_NOW);
    if (!impl->handle)
        impl->handle = dlopen(lib_fb, RTLD_NOW);

    if (!impl->handle)
    {
        printf("failed to load %s\n", lib_fb);
        exit(1);
    }

    void *memcpy_ptr = dlsym(impl->handle, memcpy_fn);
    void *memmove_ptr = dlsym(impl->handle, memmove_fn);

    impl->memcpy_fn = *(stringop_fn *)&memcpy_ptr;
    impl->memmove_fn = *(stringop_fn *)&memmove_ptr;

    if (!impl->memcpy_fn || !impl->memmove_fn)
    {
        printf("failed to load string function from %s\n", lib_fb);
        exit(1);
    }
}

static void cleanup_functions(struct lib_functions *impl)
{
    if (impl->handle)
    {
        dlclose(impl->handle);
        impl->handle = NULL;
    }
}
#endif

static void init_perf_stats(struct perf_stats *stats)
{
    stats->total_gb = 0;
    stats->count = 0;
    stats->min_gb = 0;
    stats->max_gb = 0;
}

static void init_test_results(struct test_results *results)
{
    init_perf_stats(&results->memcpy_aligned);
    init_perf_stats(&results->memcpy_unaligned);
    init_perf_stats(&results->memmove_forward);
    init_perf_stats(&results->memmove_backward);
    results->total_tests = 0;
}

static void print_measurement(const char *name, double best, double worst, double avg)
{
    printf("\n            \t%s\t| %8.2f   %8.2f   %8.2f", name, best, worst, avg);
}

static void init_test_buffer(unsigned char *buf, size_t size)
{
    static const unsigned char pattern[] = {
        0x55, 0xAA, 0x33, 0xCC, 0x66, 0x99, 0x0F, 0xF0,
        0xFF, 0x00, 0xA5, 0x5A, 0x3C, 0xC3, 0x69, 0x96};
    for (size_t i = 0; i < size; i++)
    {
        buf[i] = pattern[i % sizeof(pattern)];
    }
}

static double measure_throughput(void *dst, const void *src, size_t size, size_t iterations,
                                 stringop_fn mem_func)
{
    struct timespec_portable start, end;

    init_test_buffer((unsigned char *)src, size);

    get_monotonic_time(&start);

    for (size_t j = 0; j < iterations; j++)
    {
        mem_func(dst, src, size);
    }

    get_monotonic_time(&end);
    double elapsed = timespec_to_seconds(&start, &end);
    return ((double)size * iterations) / (elapsed * 1e9);
}

static void update_perf_stats(struct perf_stats *stats, double gb_per_sec)
{
    stats->total_gb += gb_per_sec;
    stats->count += 1;
    if (stats->count == 1 || gb_per_sec < stats->min_gb)
    {
        stats->min_gb = gb_per_sec;
    }
    if (stats->count == 1 || gb_per_sec > stats->max_gb)
    {
        stats->max_gb = gb_per_sec;
    }
}

static void run_test_cases(const struct test_case *cases, size_t num_cases,
                           size_t size, size_t iterations,
                           unsigned char *src_base, unsigned char *dst_base,
                           struct lib_functions *impl,
                           int is_memmove)
{
    printf("\n%s implementation:", impl->name);

    for (size_t i = 0; i < num_cases; i++)
    {
        const struct test_case *test = &cases[i];
        unsigned char *src = src_base + (is_memmove ? 64 : test->src_align);
        unsigned char *dst;

        if (is_memmove)
        {
            dst = test->backwards ? src + test->overlap_offset : dst_base + 64;
        }
        else
        {
            dst = dst_base + test->dst_align;
        }

        stringop_fn func = is_memmove ? impl->memmove_fn : impl->memcpy_fn;

        /* warmup phase */
        for (size_t w = 0; w < iterations / 10; w++)
        {
            func(dst, src, size);
            dst[0] ^= src[0];
        }

        double best_gbs = 0, worst_gbs = 0, total_gbs = 0;
        int valid_measurements = 0;

        for (int pass = 0; pass < 5; pass++)
        {
            double gb_per_sec = measure_throughput(dst, src, size, iterations, func);

            if (gb_per_sec > 0.1 && gb_per_sec < 300.0)
            {
                if (valid_measurements == 0)
                {
                    best_gbs = worst_gbs = gb_per_sec;
                }
                else
                {
                    if (gb_per_sec < worst_gbs)
                        worst_gbs = gb_per_sec;
                    if (gb_per_sec > best_gbs)
                        best_gbs = gb_per_sec;
                }
                total_gbs += gb_per_sec;
                valid_measurements++;
            }
        }

        if (valid_measurements > 0)
        {
            double avg_gbs = total_gbs / valid_measurements;
            print_measurement(test->name, best_gbs, worst_gbs, avg_gbs);

            struct perf_stats *stats;
            if (is_memmove)
            {
                stats = test->backwards ? &impl->results.memmove_backward
                                        : &impl->results.memmove_forward;
            }
            else
            {
                stats = (test->src_align == 64 && test->dst_align == 64)
                            ? &impl->results.memcpy_aligned
                            : &impl->results.memcpy_unaligned;
            }

            update_perf_stats(stats, avg_gbs);
            impl->results.total_tests++;
        }
        else
        {
            printf("\n            \t%s\t|    ERROR - no valid measurements.", test->name);
        }
    }
    printf("\n" SEPARATOR);
}

static size_t estimate_iterations(size_t size, uint64_t target_ns, double expected_gbs)
{
    if (expected_gbs <= 0.0)
        expected_gbs = 16.0;

    double time_per_iter_ns = (double)size / expected_gbs;
    size_t iterations = (size_t)((target_ns / time_per_iter_ns) / 5); /* 5 passes in each test */

    if (size >= 64 * 1024 * 1024)
        iterations /= 2;

    return iterations < 4 ? 4 : iterations;
}

int main(int argc, char **argv)
{
    static const struct test_case alignment_cases[] = {
        {"aligned    ", {.src_align = 64, .dst_align = 64}},
        {"src+1      ", {.src_align = 65, .dst_align = 64}},
        {"dst+1      ", {.src_align = 64, .dst_align = 65}},
        {"both+1     ", {.src_align = 65, .dst_align = 65}},
        {"worst-case ", {.src_align = 63, .dst_align = 63}}};

    struct test_case memmove_cases[] = {
        {"forward     ", {.overlap_offset = 0, .backwards = 0}},
        {"back 25%    ", {.overlap_offset = 0, .backwards = 1}},
        {"back 50%    ", {.overlap_offset = 0, .backwards = 1}},
        {"back 75%    ", {.overlap_offset = 0, .backwards = 1}},
        {"back 1-byte ", {.overlap_offset = 0, .backwards = 1}}};

    static const size_t bench_sizes[] = {
        64 * 1024,        /* 64KB - ~L1 cache size */
        256 * 1024,       /* 256KB - ~L2 cache size */
        2 * 1024 * 1024,  /* 2MB - ~L3 cache size */
        16 * 1024 * 1024, /* 16MB - out of cache */
        64 * 1024 * 1024, /* 64MB */
    };
#ifdef SHARED
    load_functions(&implementations[0]);
    load_functions(&implementations[1]);
#endif
    for (size_t i = 0; i < 2; i++)
    {
        init_test_results(&implementations[i].results);
    }

    uint64_t target_duration_ns = DEFAULT_TEST_DURATION_NS;
    double expected_gbs = 0.0;

    for (int i = 1; i < argc; i++)
    {
        if (strncmp(argv[i], "--duration=", 11) == 0)
        {
            double ms = strtod(argv[i] + 11, NULL);
            if (ms > 0)
            {
                target_duration_ns = (uint64_t)(ms * 1e6);
            }
        }
        else if (strncmp(argv[i], "--expected-gbs=", 15) == 0)
        {
            expected_gbs = strtod(argv[i] + 15, NULL);
        }
    }

    printf("\nrunning benchmarks (target duration: %.1f ms)...\n\n",
           target_duration_ns / 1e6);

    size_t max_size = bench_sizes[sizeof(bench_sizes) / sizeof(bench_sizes[0]) - 1];
    unsigned char *src_base = __aligned_alloc(64, max_size * 2 + 256);
    unsigned char *dst_base = __aligned_alloc(64, max_size * 2 + 256);

    if (!src_base || !dst_base)
    {
        printf("failed to allocate benchmark buffers.\n");
        return 1;
    }

    printf("memcpy alignment tests:\n%s%s", ALIGNMENT_HEADER, SEPARATOR);

    for (size_t i = 0; i < sizeof(bench_sizes) / sizeof(bench_sizes[0]); i++)
    {
        size_t size = bench_sizes[i];
        size_t iterations = estimate_iterations(size, target_duration_ns, expected_gbs);

        printf("\n%7.2f MB: ", size / (1024.0 * 1024.0));

        for (size_t impl = 0; impl < 2; impl++)
        {
            run_test_cases(alignment_cases,
                           sizeof(alignment_cases) / sizeof(alignment_cases[0]),
                           size, iterations, src_base, dst_base,
                           &implementations[impl], 0);
        }
    }

    printf("\n\nmemmove overlap tests:\n%s%s", ALIGNMENT_HEADER, SEPARATOR);

    for (size_t i = 0; i < sizeof(bench_sizes) / sizeof(bench_sizes[0]); i++)
    {
        size_t size = bench_sizes[i];
        size_t iterations = estimate_iterations(size, target_duration_ns, expected_gbs);

        printf("\n%7.2f MB: ", size / (1024.0 * 1024.0));

        for (size_t impl = 0; impl < 2; impl++)
        {
            /* calc overlaps based on current test size */
            memmove_cases[1].overlap_offset = size * 3 / 4; /* 25% back = 75% overlap */
            memmove_cases[2].overlap_offset = size / 2;     /* 50% back = 50% overlap */
            memmove_cases[3].overlap_offset = size / 4;     /* 75% back = 25% overlap */
            memmove_cases[4].overlap_offset = size - 1;     /* 1 byte from end */

            run_test_cases(memmove_cases,
                           sizeof(memmove_cases) / sizeof(memmove_cases[0]),
                           size, iterations, src_base, dst_base,
                           &implementations[impl], 1);
        }
    }

    printf("\nperformance summary:\n");
    printf("==================================================================\n");
    printf("relative performance (ours vs stdlib):\n");
    printf("  \t\t\t\t|  avg GB/s   min GB/s   max GB/s   vs stdlib\n");
    printf(SEPARATOR);

    const char *categories[] = {
        "memcpy (aligned)   ",
        "memcpy (unaligned) ",
        "memmove (forward)  ",
        "memmove (backward) "};

    struct perf_stats *custom_stats[] = {
        &implementations[0].results.memcpy_aligned,
        &implementations[0].results.memcpy_unaligned,
        &implementations[0].results.memmove_forward,
        &implementations[0].results.memmove_backward};

    struct perf_stats *stdlib_stats[] = {
        &implementations[1].results.memcpy_aligned,
        &implementations[1].results.memcpy_unaligned,
        &implementations[1].results.memmove_forward,
        &implementations[1].results.memmove_backward};

    for (int i = 0; i < 4; i++)
    {
        if (custom_stats[i]->count == 0 || stdlib_stats[i]->count == 0)
            continue;

        double custom_avg = custom_stats[i]->total_gb / custom_stats[i]->count;
        double stdlib_avg = stdlib_stats[i]->total_gb / stdlib_stats[i]->count;
        double ratio = custom_avg / stdlib_avg * 100.0;

        printf("  \t%s\t| %8.2f   %8.2f   %8.2f   %6.1f%%\n",
               categories[i],
               custom_avg,
               custom_stats[i]->min_gb,
               custom_stats[i]->max_gb,
               ratio);
    }

    printf("\n");
#ifdef SHARED
    cleanup_functions(&implementations[0]);
    cleanup_functions(&implementations[1]);
#endif
    __aligned_free(src_base);
    __aligned_free(dst_base);

    return 0;
}
