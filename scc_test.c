#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CACHE_LINE 64
#define ALIGN __attribute__((aligned(CACHE_LINE)))

#define ITERS 10000

static int g_failures = 0;

#define START(name) fprintf(stdout, "[INFO] strating %s\n", name)
#define PASS(name) fprintf(stdout, "[PASS] %s\n", name)
#define FAIL(name, ...) do { \
    fprintf(stdout, "[FAIL] %s: ", name); \
    fprintf(stdout, "\n"); \
    g_failures++; \
} while (0)

/** XXX(mfd) : I am not sure whether it is necessary or not, but used to avoid surprises. */
#define COMPILER_BARRIER() __asm__ volatile("" ::: "memory")

/* Spin for a random short burst so writer and reader interleave at varying
 * phases, maximising the chance of hitting a speculative window. */
static inline void spin(unsigned n) {
    volatile unsigned x = n;
    while (x--) __asm__ volatile("nop");
}

/* =========================================================================
 * TEST 1 — Read Only field collocated with high update field.
 *
 * 
 ========================================================================= */

typedef struct __attribute__((aligned(64)))
{
    atomic_int hot_field;   // heavily updated
    atomic_int ro_field;    // read-only from reader's perspective
} Shared;

Shared shared;


atomic_bool stop_flag = ATOMIC_VAR_INIT(false);
const int N = 100000ULL; // 10M

void* writer_thread(void* arg) {
    // const int N = 10000000ULL; // 10M
    fprintf(stdout, "[Writer] %#x", &shared);
    for (int i = 0; i < N && !atomic_load_explicit(&stop_flag, memory_order_relaxed); i++) {
        atomic_fetch_add_explicit(&shared.hot_field, 1, memory_order_relaxed);
    }
    atomic_store_explicit(&stop_flag, true, memory_order_release);
    return NULL;
}

void* reader_thread(void* arg) {
    int sum = 0;
    fprintf(stdout, "[Reader] %#x", &shared);
    while (!atomic_load_explicit(&stop_flag, memory_order_acquire)) {
        sum += atomic_load_explicit(&shared.ro_field, memory_order_relaxed);
    }
    printf("Reader sum = %u\n", sum);
    return NULL;
}

static int ro_unpadded() {
    const char *name = "read_only_unpadded";
    START(name);
    pthread_t t0, t1;

    atomic_store(&shared.hot_field, 0);
    atomic_store(&shared.ro_field, 1);
    atomic_store(&stop_flag, false);

    pthread_create(&t0, NULL, writer_thread, NULL);
    pthread_create(&t1, NULL, reader_thread, NULL);

    pthread_join(t0, NULL);
    pthread_join(t1, NULL);


    printf("Final hot_field = %u\n", atomic_load(&shared.hot_field));
    int final = atomic_load(&shared.hot_field);
    if (final != N) {
        FAIL(name, "Final incremenet got by the reader is %d, expected %d", final, N);
    } else {
        PASS(name);
    }
    return 0;
}



/* =========================================================================
 * TEST 2 — Streess test the abort rate and the recovery from mispeculation
 *
 * 
 ========================================================================= */

 typedef struct __attribute__((aligned(64)))
{
    atomic_int hot_field;   // heavily updated
    //  atomic_int ro_field;    // read-only from reader's perspective
} Shared1;

Shared1 shared1;


atomic_bool stop_flag1 = ATOMIC_VAR_INIT(false);

void* writer_thread1(void* arg) {
    // const int N = 10000000ULL; // 10M
    fprintf(stdout, "[Writer] %#x", &shared1);
    const int N = 100000ULL; // 10M
    for (int i = 0; i < N && !atomic_load_explicit(&stop_flag1, memory_order_relaxed); i++) {
        atomic_fetch_add_explicit(&shared1.hot_field, 1, memory_order_relaxed);
    }
    atomic_store_explicit(&stop_flag1, true, memory_order_release);
    return NULL;
}

void* reader_thread1(void* arg) {
    int sum = 0;
    fprintf(stdout, "[Reader] %#x", &shared1);
    while (!atomic_load_explicit(&stop_flag1, memory_order_acquire)) {
        sum += atomic_load_explicit(&shared1.hot_field, memory_order_relaxed);
    }
    printf("Reader sum = %u\n", sum);
    return NULL;
}

static int misspeculation_stress_test()
{
    const char * name = "misspeculation_stress_test";
    START(name);
    pthread_t t0, t1;

    atomic_store(&shared1.hot_field, 0);
    // atomic_store(&shared.ro_field, 1);
    atomic_store(&stop_flag1, false);

    pthread_create(&t0, NULL, writer_thread1, NULL);
    pthread_create(&t1, NULL, reader_thread1, NULL);

    pthread_join(t0, NULL);
    pthread_join(t1, NULL);

    printf("Final hot_field = %u\n", atomic_load(&shared1.hot_field));
    int final = atomic_load(&shared1.hot_field);
    if (final != N) {
        FAIL(name, "Final incremenet got by the reader is %d, expected %d", final, N);
    } else {
        PASS(name);
    }
    return 0;
}
 

/* =========================================================================
 * TEST 3 — Dependent branch on speculative load
 *
 * The reader branches on the loaded value.  If a stale value is used for the
 * branch, the wrong side executes and corrupts a separate counter.
 *
 * Layout: shared.flag is either 0 or 1.  Reader increments either
 * branch_taken[0]oor branch_taken[1].  At the end, branch_taken[0] +
 * branch_taken[1] must equal total_reads, regardless of how the counts are
 * split.  Additionally, the flag is only ever 0 or 1, so any branch that
 * increments branch_taken[2] indicates a stale/garbage value committed.
 * ====================================================================== */
static struct { volatile int flag; } ALIGN t2_shared;
static volatile int t2_stop;
static int64_t branch_taken[3]; /* [0]=flag==0, [1]=flag==1, [2]=impossible */
 
static void *t2_writer(void *arg) {
    for (int i = 0; i < ITERS && !t2_stop; i++) {
        t2_shared.flag = i & 1;
        spin(5);
    }
    t2_stop = 1;
    return NULL;
}
 
static void test_branch_on_load(void) {
    const char *name = "branch_on_load";
    START(name);
    t2_stop = 0;
    t2_shared.flag = 0;
    memset(branch_taken, 0, sizeof(branch_taken));
 
    pthread_t wt;
    pthread_create(&wt, NULL, t2_writer, NULL);
 
    int64_t total = 0;
    while (!t2_stop) {
        COMPILER_BARRIER();
        int f = t2_shared.flag;      /* speculative load candidate */
        COMPILER_BARRIER();
        /* Dependent branch — the side effect must match the loaded value. */
        if (f == 0)
            branch_taken[0]++;
        else if (f == 1)
            branch_taken[1]++;
        else
            branch_taken[2]++;       /* impossible if squash is correct */
        total++;
    }
 
    pthread_join(wt, NULL);
 
    if (branch_taken[2] != 0) {
        FAIL(name, "impossible branch taken %lld times (stale/garbage value committed)",
             (long long)branch_taken[2]);
    } else if (branch_taken[0] + branch_taken[1] != total) {
        FAIL(name, "branch counts %lld+%lld != total %lld",
             (long long)branch_taken[0], (long long)branch_taken[1],
             (long long)total);
    } else {
        PASS(name);
    }
}

/* =========================================================================
 * Main
 * ====================================================================== */
int main(void) {
    fprintf(stdout, "=== Speculative cache coherence correctness tests ===\n");
    fprintf(stdout, "    ITERS=%d per test\n\n", ITERS);
 
 
    ro_unpadded();
    misspeculation_stress_test();
    test_branch_on_load();

    if (g_failures > 0)
        fprintf(stdout, "\n === Results: %d failure(s) ===\n", g_failures);
    else
        fprintf(stdout, "====== ALL TESTS PASSED ======\n");
    
    return g_failures ? 1 : 0;
}