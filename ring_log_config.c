#include "ring_log.h"

// For each log, just specify the filename (`.fn`):
log_t logs[] = {
    { .fn = "log_a" }
};

// The total log size to be shared among all of the logs defined above.
#define LOGS_PARTITION_SIZE 200
const int logs_partition_size = LOGS_PARTITION_SIZE;

// Leave this alone!
#define N_LOGS (sizeof(logs) / sizeof(logs[0]))
const int n_logs = N_LOGS;

// We want to have some free space, so that when bad blocks crop up the fs can
// replace them with some of the free blocks.
const int log_size = LOGS_PARTITION_SIZE * .8 / N_LOGS;

// ring_log can't assume that the underlying FS can make sparse files. So at
// ring_log_init -time, it'll fill up the log with (mostly) filler_byte. For
// some storage technologies (Flash), the choice here can make a big difference
// in terms of wear.
const uint8_t filler_byte = 0;
