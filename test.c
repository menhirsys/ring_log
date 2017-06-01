#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ring_log.h"

extern void debug_print(const char *);

extern const int logs_partition_size;

typedef struct {
    uint32_t len;
    uint32_t seq;
} test_entry_header_t;

void test_write_and_read_entries(int count) {
    // Write `count` entries between 1 and 100 bytes in length + header.
    printf("  writing %i entries..\n", count);
    //debug_print("log_a");
    char chars[] = "0123456789abcdef";
    for (int i = 0; i < count; i++) {
        test_entry_header_t header = {
            .len = 1 + (rand() % 50),
            .seq = i
        };
        ring_log_write_tail("log_a", &header, sizeof(header));
        for (int j = 0; j < header.len; j++) {
            ring_log_write_tail("log_a", &chars[j % (sizeof(chars) - 1)], 1);
        }
        ring_log_write_tail("log_a", "zzz", 3);
        ring_log_write_tail_complete("log_a");
        //printf("\nafter write entry (seq=%i) containing %i bytes:\n", header.seq, header.len);
        //debug_print("log_a");
    }

    // Read as many entries as are available. This might be less than the
    // number we wrote in, since the ring can only hold so many entries before
    // dropping the old entries.
    int last_seq = -1;
    int count_read = 0;
    while (ring_log_has_unread("log_a")) {
        // Read in the header.
        size_t read_total = 0;
        test_entry_header_t header;
        RING_LOG_EXPECT(ring_log_read_head("log_a", &header, sizeof(header), &read_total), sizeof(header));

        // Check that test_entry_header.seq are consecutive.
        if (last_seq != -1) {
            RING_LOG_EXPECT(header.seq - last_seq, 1);
        }
        //printf("seq=%i, len=%i\n", header.seq, header.len);
        last_seq = header.seq;

        // Read between 1 to 10 bytes at a time,
        char s[10];
        int got_z = 0;
        while (1) {
            int buffer_sz = 1 + (rand() % sizeof(s));
            int read_now = ring_log_read_head("log_a", &s, buffer_sz, &read_total);
            if (read_now == 0) {
                // .. until we have read in a complete entry.
                break;
            }
            // Check that the entry has the bytes that we expect.
            for (int j = 0; j < read_now; j++) {
                if (got_z || s[j] == 'z') {
                    RING_LOG_EXPECT(s[j], 'z');
                    got_z++;
                } else {
                    RING_LOG_EXPECT(s[j], chars[(read_total - read_now - sizeof(header) + j) % (sizeof(chars) - 1)]);
                }
            }
        }
        ring_log_read_head_success("log_a");
        count_read++;

        // Did we get the right number of sentinel z's?
        RING_LOG_EXPECT(got_z, 3);

        // Also check that the entry had the right length.
        RING_LOG_EXPECT(read_total, sizeof(header) + header.len + 3);
    }

    // If we wrote a ton of entries, we might have last the first N entries to
    // writeover, but we should have consecutive entries up to the last entry
    // we wrote.
    RING_LOG_EXPECT(last_seq, count - 1);

    printf("    .. read %i entries back out\n", count_read);
}

void test(void) {
    RING_LOG_EXPECT_NOT(ring_log_init(), 0);

    sanity_check_file_size("log_a");

    // Write a bunch of entries and see if we get (a subset of) them back, in
    // the right order.
    int entry_counts[] = {1, 3, 10, 10, 1000, 1000000, 10, 3, 1};
    for (int i = 0; i < sizeof(entry_counts) / sizeof(entry_counts[0]); i++) {
        test_write_and_read_entries(entry_counts[i]);
    }

    sanity_check_file_size("log_a");

    // Write an entry and check that it's there for reading.
    RING_LOG_EXPECT(ring_log_has_unread("log_a"), 0);
    ring_log_write_tail("log_a", "hello", 5);
    ring_log_write_tail_complete("log_a");
    RING_LOG_EXPECT_NOT(ring_log_has_unread("log_a"), 0);

    // Write a huge entry that wipes out the entry we wrote above.
    char garbage[] = "Garbage";
    int written = 0;
    while (written < logs_partition_size) {
        ring_log_write_tail("log_a", garbage, sizeof(garbage) - 1);
        written += sizeof(garbage) - 1;
    }
    ring_log_write_tail_complete("log_a");
    printf("  wrote huge entry with %i bytes\n", written);

    // Read in the (corrupted) huge entry.
    RING_LOG_EXPECT_NOT(ring_log_has_unread("log_a"), 0);
    size_t read_total = 0;
    char buffer[8];
    int read_now;
    while ((read_now = ring_log_read_head("log_a", &buffer, sizeof(buffer), &read_total))) {
        //printf("%.*s", read_now, &buffer);
    };
    ring_log_read_head_success("log_a");
    printf("  read out huge entry with %zi bytes\n", read_total);
    RING_LOG_EXPECT(ring_log_has_unread("log_a"), 0);

    // Is the structure still functional after that?
    for (int i = 0; i < sizeof(entry_counts) / sizeof(entry_counts[0]); i++) {
        test_write_and_read_entries(entry_counts[i]);
    }

    ring_log_deinit();
}

int main(void) {
    srand(10);

    // First time, start with a fresh ring log file.
    puts("pass 1: using a fresh ring log file");
    unlink("log_a");
    test();

    // Second time, try with an existing ring log file.
    puts("pass 2: using the old/existing ring log file");
    test();

    puts("success");

    return 0;
}
