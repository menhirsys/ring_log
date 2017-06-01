#include <pthread.h>
#include <stdlib.h>

#include "ring_log.h"

static pthread_mutex_t lock;

void ring_log_arch_abort(void) {
    abort();
}

void ring_log_arch_init(void) {
    RING_LOG_EXPECT(pthread_mutex_init(&lock, NULL), 0);
}

void ring_log_arch_deinit(void) {
    RING_LOG_EXPECT(pthread_mutex_destroy(&lock), 0);
}

void ring_log_arch_take_mutex(void) {
    pthread_mutex_lock(&lock);
}

void ring_log_arch_free_mutex(void) {
    pthread_mutex_unlock(&lock);
}
