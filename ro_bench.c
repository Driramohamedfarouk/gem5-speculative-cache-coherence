#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifndef PAD
#define PAD 0
#endif

typedef struct __attribute__((aligned(64)))
{
    atomic_int hot_field;   // heavily updated
#if PAD
    char padding[128];            // separate cache line when PAD=1
#endif
    atomic_int ro_field;    // read-only from reader's perspective
} Shared;

Shared shared;


atomic_bool stop_flag = ATOMIC_VAR_INIT(false);

void* writer_thread(void* arg) {
    // const int N = 10000000ULL; // 10M
    fprintf(stderr, "[Writer] %#x", &shared);
    const int N = 100000ULL; // 10M
    for (int i = 0; i < N && !atomic_load_explicit(&stop_flag, memory_order_relaxed); i++) {
        atomic_fetch_add_explicit(&shared.hot_field, 1, memory_order_relaxed);
    }
    atomic_store_explicit(&stop_flag, true, memory_order_release);
    return NULL;
}

void* reader_thread(void* arg) {
    int sum = 0;
    fprintf(stderr, "[Reader] %#x", &shared);
    while (!atomic_load_explicit(&stop_flag, memory_order_acquire)) {
        sum += atomic_load_explicit(&shared.ro_field, memory_order_relaxed);
    }
    printf("Reader sum = %u\n", sum);
    return NULL;
}

int main() {
    pthread_t t0, t1;

    atomic_store(&shared.hot_field, 0);
    atomic_store(&shared.ro_field, 1);
    atomic_store(&stop_flag, false);

    pthread_create(&t0, NULL, writer_thread, NULL);
    pthread_create(&t1, NULL, reader_thread, NULL);

    pthread_join(t0, NULL);
    pthread_join(t1, NULL);

    printf("Final hot_field = %u\n", atomic_load(&shared.hot_field));
    return 0;
}
