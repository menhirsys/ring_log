#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ring_log.h"

int main(void) {
    unlink("log_a");

    if (!ring_log_init()) {
        puts("ring_log_init failed");
        exit(1);
    }

    ring_log_write_tail("log_a", "one", 3);
    ring_log_write_tail_complete("log_a");

    ring_log_write_tail("log_a", "two", 3);
    ring_log_write_tail_complete("log_a");

    ring_log_write_tail("log_a", "three", 5);
    ring_log_write_tail_complete("log_a");

    while (ring_log_has_unread("log_a")) {
        int read_now;
        char buffer[8];
        size_t read_total = 0;
        printf("entry: ");
        while ((read_now = ring_log_read_head("log_a", &buffer, sizeof(buffer), &read_total))) {
            printf("%.*s", read_now, (char *)&buffer);
        };
        puts("");
        ring_log_read_head_success("log_a");
    }

    return 0;
}
