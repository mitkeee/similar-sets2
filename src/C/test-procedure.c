/*
 * File: test-procedure.c
 *
 * Standardized test procedure for set-trie with skip list nodes.
 * Reads dataset from file (sample.txt.mapd.sorted format),
 * builds set-trie using cskiplist-backed connectors,
 * outputs performance metrics (time, memory).
 *
 * Usage:
 *   test-procedure <datafile> [testfile] [hmg]
 *
 *   datafile  - dataset file (one set per line, space-separated ints)
 *   testfile  - optional query file (same format); if omitted, queries
 *               are read from stdin
 *   hmg       - Hamming distance for similarity search (default: 1)
 *
 * Output format:
 *   [CONFIG]  block_cap=128 simd=1
 *   [LOAD]    sets=30 time_ms=1.234 mem_kb=456
 *   [QUERY]   qnum=1 results=3 time_us=567.8
 *   [SUMMARY] queries=3 total_ms=1.701 avg_us=567.1 mem_kb=512
 *
 * Copyright (c) 2024-25, FAMNIT, University of Primorska
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>
#include "config.h"
#include "set.h"
#include "qesa.h"
#include "connector.h"
#include "set2.h"
#include "cskiplist.h"

/* ---------- Platform-specific timing and memory ---------- */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>

static LARGE_INTEGER qpc_freq;

static void timer_init(void) {
    QueryPerformanceFrequency(&qpc_freq);
}

static double timer_now_us(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)qpc_freq.QuadPart * 1e6;
}

static long get_mem_kb(void) {
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return (long)(pmc.WorkingSetSize / 1024);
    return 0;
}

#else /* POSIX */
#include <time.h>
#include <sys/resource.h>

static void timer_init(void) { /* no-op */ }

static double timer_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static long get_mem_kb(void) {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0)
        return ru.ru_maxrss; /* KB on Linux */
    return 0;
}
#endif

/* ---------- Helpers ---------- */

static void print_config(void) {
    int block_cap = CSL_BLOCK_CAP;
    int simd = 0;
#ifdef CSL_USE_SIMD
    simd = CSL_USE_SIMD;
#endif
    printf("[CONFIG]  block_cap=%d simd=%d\n", block_cap, simd);
}

/*
 * Count lines in a file (number of sets).
 */
static int count_lines(FILE *f) {
    int count = 0;
    char buf[4096];
    while (fgets(buf, sizeof(buf), f))
        count++;
    rewind(f);
    return count;
}

/* ---------- Phase 1: Load dataset ---------- */

static set2_node* load_dataset(const char *path, int *nsets, double *load_time_us) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "error: cannot open datafile '%s'\n", path);
        return NULL;
    }

    *nsets = count_lines(f);

    double t0 = timer_now_us();
    set2_node *st = set2_load(f);
    double t1 = timer_now_us();

    fclose(f);
    *load_time_us = t1 - t0;
    return st;
}

/* ---------- Phase 2: Run queries ---------- */

static void run_queries(FILE *f, set2_node *st, int hmg_dist) {

    int el = -1;
    char *lin = (char *)malloc(MAX_STRING_SIZE);
    char *tok_buf = (char *)malloc(INIT_STRING_SIZE);

    set *s1 = set_alloc();
    set *sp = set_alloc();
    qesa *q1 = qesa_alloc();

    int qnum = 0;
    double total_query_us = 0.0;

    while (fgets(lin, MAX_STRING_SIZE, f) != NULL) {

        /* skip blank lines */
        char *trimmed = strtrm(lin);
        if (trimmed[0] == '\0' || trimmed[0] == '\n')
            continue;

        /* parse set from line */
        set_reset(s1);
        char *tok = strtok(trimmed, " \n\f\r");
        if (!tok) continue;
        do {
            el = atoi(tok);
            set_insert(s1, el);
        } while ((tok = strtok(NULL, " \n\f\r")) != NULL);

        /* prepare for search */
        set_open(s1);
        set_reset(sp);
        qesa_reset(q1);
        int hmg = hmg_dist;

        /* timed similarity search */
        double t0 = timer_now_us();
        set2_simsearch_hmg(st, s1, sp, &hmg, q1);
        double t1 = timer_now_us();

        double elapsed_us = t1 - t0;
        total_query_us += elapsed_us;
        qnum++;

        int nresults = qesa_size(q1);
        printf("[QUERY]   qnum=%d results=%d time_us=%.1f\n",
               qnum, nresults, elapsed_us);
    }

    /* summary */
    long mem_kb = get_mem_kb();
    double avg_us = (qnum > 0) ? total_query_us / qnum : 0.0;
    printf("[SUMMARY] queries=%d total_ms=%.3f avg_us=%.1f mem_kb=%ld\n",
           qnum, total_query_us / 1000.0, avg_us, mem_kb);

    set_free(s1);
    set_free(sp);
    qesa_free(q1);
    free(lin);
    free(tok_buf);
}

/* ---------- Main ---------- */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr,
            "Usage: %s <datafile> [testfile] [hmg]\n"
            "\n"
            "  datafile  - dataset (one set per line, space-separated ints)\n"
            "  testfile  - query file (same format); stdin if omitted\n"
            "  hmg       - Hamming distance (default: 1)\n",
            argv[0]);
        return 1;
    }

    timer_init();

    const char *datafile = argv[1];
    const char *testfile = (argc >= 3) ? argv[2] : NULL;
    int hmg_dist = (argc >= 4) ? atoi(argv[3]) : 1;

    /* configuration banner */
    print_config();

    /* Phase 1: load dataset into set-trie */
    long mem_before = get_mem_kb();
    int nsets = 0;
    double load_us = 0.0;
    set2_node *st = load_dataset(datafile, &nsets, &load_us);
    if (!st) return 1;

    long mem_after = get_mem_kb();
    printf("[LOAD]    sets=%d time_ms=%.3f mem_kb=%ld (delta=%ld)\n",
           nsets, load_us / 1000.0, mem_after, mem_after - mem_before);

    /* Phase 2: run queries */
    FILE *qf = NULL;
    if (testfile) {
        qf = fopen(testfile, "r");
        if (!qf) {
            fprintf(stderr, "error: cannot open testfile '%s'\n", testfile);
            return 1;
        }
    } else {
        qf = stdin;
    }

    run_queries(qf, st, hmg_dist);

    if (testfile && qf)
        fclose(qf);

    return 0;
}
