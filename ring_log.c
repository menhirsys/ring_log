#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "ring_log.h"

extern log_t logs[];
extern const int n_logs;
extern const int log_size;
extern uint8_t filler_byte;

static int read_all(int fd, char *p, size_t len) {
    ssize_t have_read = 0;
    while (have_read < len) {
        ssize_t ret = read(fd, p + have_read, len - have_read);
        if (ret == -1) {
            if (errno != EINTR) {
                RING_LOG_ERROR("errno != EINTR");
                return 0;
            }
        } else if (ret == 0) {
            RING_LOG_ERROR("unexpected EOF");
            return 0;
        } else {
            have_read += ret;
        }
    }
    return 1;
}

static int write_all(int fd, char *p, size_t len) {
    ssize_t written = 0;
    while (written < len) {
        ssize_t ret = write(fd, p + written, len - written);
        if (ret == -1) {
            if (errno != EINTR) {
                RING_LOG_ERROR("errno != EINTR");
                return 0;
            }
        } else {
            written += ret;
        }
    }
    return 1;
}

static int seek_abs(log_t *log, off_t off) {
    if (off < 0) {
        RING_LOG_ERROR("off < 0");
        return 0;
    }
    if (off >= log_size) {
        RING_LOG_ERROR("off >= LOG_SIZE");
        return 0;
    }
    if (lseek(log->fd, off, SEEK_SET) == -1) {
        RING_LOG_ERROR("lseek failed");
        return 0;
    }
    return 1;
}

static int has_unread(log_t *log) {
    return log->file_header.head != log->file_header.tail;
}

// read_wrap reads up to `len` bytes into `p`. The reads will wrap around the
// end of the log, and skip over the file header. read_wrap will return the
// offset after the last byte read. If there is any kind of error, it will
// return -1.
static int read_wrap(log_t *log, char *p, size_t len) {
    // Find where we are in the file right now.
    off_t off = lseek(log->fd, 0, SEEK_CUR);
    if (off == -1) {
        RING_LOG_ERROR("lseek failed");
        return -1;
    }

    for (size_t i = 0; i < len || off < sizeof(file_header_t); ) {
        // Don't read from the file header.
        if (off >= sizeof(file_header_t)) {
            // Read one byte..
            ssize_t ret;
        retry:
            if (p == NULL) {
                char c;
                ret = read(log->fd, &c, 1);
            } else {
                ret = read(log->fd, p + i, 1);
            }
            if (ret == -1) {
                if (errno != EINTR) {
                    RING_LOG_ERROR("errno != EINTR");
                    return -1;
                }
                goto retry;
            } else if (ret == 0) {
                RING_LOG_ERROR("got unexpected EOF");
                return -1;
            } else if (ret != 1) {
                RING_LOG_ERROR("got ret != 1");
                return -1;
            }
            i++;
        }

        off_t new_off = (off + 1) % log_size;
        if (!seek_abs(log, new_off)) {
            RING_LOG_ERROR("seek_abs failed");
        }
        off = new_off;
    }

    return off;
}

// write_wrap writes (unless error) `len` bytes from `p`. The writes will wrap
// around the end of the log, and skip over the file header. If there is any
// error, write_wrap returns -1. Otherwise, it will return the offset after the
// last byte written.
static off_t write_wrap(log_t *log, int is_entry, const char *p, size_t len) {
    // Find where we are in the file right now.
    off_t off = lseek(log->fd, 0, SEEK_CUR);
    if (off == -1) {
        RING_LOG_ERROR("lseek failed");
        return -1;
    }

    for (size_t i = 0; i < len; ) {
        // Don't write over the file header.
        if (off >= sizeof(file_header_t)) {
            // If we've reached the head entry and `is_entry`, then take a
            // detour and first set the new head to the entry after current
            // head entry.
            if (is_entry) {
                if ((off == log->file_header.head) && has_unread(log)) {
                    // Figure out where the next entry starts and store that new head in the header.
                    entry_header_t entry_header;
                    RING_LOG_EXPECT_NOT(read_wrap(log, (void *)&entry_header, sizeof(entry_header)), -1);
                    uint16_t old_head = log->file_header.head;
                    log->file_header.head = read_wrap(log, NULL, entry_header.len);
                    RING_LOG_EXPECT_NOT(lseek(log->fd, 0, SEEK_SET), -1);
                    RING_LOG_EXPECT_NOT(write_all(log->fd, (void *)&(log->file_header), sizeof(log->file_header)), 0);

                    // Seek back to the voided (old) head so we can reuse that space.
                    RING_LOG_EXPECT(seek_abs(log, old_head), 1);
                }
            }

            // Write one byte..
            ssize_t ret;
        retry:
            ret = write(log->fd, p + i, 1);
            if (ret == -1) {
                if (errno != EINTR) {
                    RING_LOG_ERROR("errno != EINTR");
                    return -1;
                }
                goto retry;
            } else if (ret == 0) {
                RING_LOG_ERROR("got unexpected EOF");
                return -1;
            } else if (ret != 1) {
                RING_LOG_ERROR("got ret != 1");
                return -1;
            }
            i++;
        }

        // Maybe seek around to the start of the ring file.
        off_t new_off = (off + 1) % log_size;
        if (!seek_abs(log, new_off)) {
            RING_LOG_ERROR("seek_abs failed");
            return -1;
        }
        off = new_off;
    }

    return off;
}

int ring_log_init(void) {
    ring_log_arch_init();

    // For each of the logs,
    for (int i = 0; i < n_logs; i++) {

        // .. open the file.
        int fd = open(logs[i].fn, O_RDWR);
        if (fd == -1) {
            // If we have to create the file, first write out the header.
            fd = open(logs[i].fn, O_RDWR | O_CREAT | O_EXCL, 0666);
            if (fd == -1) {
                RING_LOG_ERROR("couldn't create ring log file");
                return 0;
            }
            logs[i].file_header.head = logs[i].file_header.tail = sizeof(logs[i].file_header);
            if (!write_all(fd, (void *)&logs[i].file_header, sizeof(logs[i].file_header))) {
                RING_LOG_ERROR("couldn't write ring log file header");
                return 0;
            }
            ssize_t written = sizeof(logs[i].file_header);

            // Then write enough zeroes to get the file to the right size.
            while (written < log_size) {
                ssize_t ret = write(fd, &filler_byte, 1);
                if (ret == -1) {
                    if (errno != EINTR) {
                        RING_LOG_ERROR("errno != EINTR");
                        return 0;
                    }
                } else {
                    written += ret;
                }
            }

            // Close the file, otherwise the fs might now actually save the file size.
            close(fd);
            fd = open(logs[i].fn, O_RDWR);
            if (fd == -1) {
                RING_LOG_ERROR("wasn't able to reopen ring log file");
                return 0;
            }
        }

        // Check that the file is the right size.
        if (lseek(fd, 0, SEEK_END) != log_size) {
            RING_LOG_ERROR("ring log file is not the right size");
            return 0;
        }

        // Read in the header and set up per-log variables.
        logs[i].fd = fd;
        if (lseek(logs[i].fd, 0, SEEK_SET) == -1) {
            RING_LOG_ERROR("lseek failed");
            return 0;
        }
        if (!read_all(logs[i].fd, (void *)&(logs[i].file_header), sizeof(logs[i].file_header))) {
            RING_LOG_ERROR("couldn't read ring log file header");
            return 0;
        }
        logs[i].new_tail_started = 0;
        logs[i].new_tail_failed = 0;
    }

    return 1;
}

void ring_log_deinit(void) {
    // Close each of the log files.
    for (int i = 0; i < n_logs; i++) {
        close(logs[i].fd);
    }

    ring_log_arch_deinit();
}

static int strcmp(const char *p1, const char *p2) {
    if (p1 == NULL || p2 == NULL) {
        RING_LOG_ERROR("NULL inputs");
        return -1;
    }

    while (1) {
        if (*p1 == '\0' && *p2 == '\0') {
            return 0;
        }
        if (*p1 != *p2) {
            return (int)*p2 - (int)*p1;
        }
        p1++;
        p2++;
    }

    return 0;
}

static log_t *lock_and_find_log(const char *log_fn) {
    // Lock: only one task works with the log at a time.
    ring_log_arch_take_mutex();

    // Find the fd for this log.
    for (int i = 0; i < n_logs; i++) {
        if (!strcmp(logs[i].fn, log_fn)) {
            return &logs[i];
        }
    }

    RING_LOG_ERROR("didn't find the struct corresponding to log");
    return NULL;
}

void ring_log_write_tail(const char *log_fn, const void *p, size_t len) {
    log_t *log = lock_and_find_log(log_fn);

    if (log->new_tail_failed) {
        // If we got an error earlier, stop here.
        goto exit;
    }

    // If a new tail entry has already been started,
    if (log->new_tail_started) {
        // .. then seek to the end of the new tail.
        if (!seek_abs(log, log->new_tail_end_offset)) {
            RING_LOG_ERROR("seek_abs failed");
            goto exit;
        }
    } else {
        // Otherwise, seek to the tail and start a new tail entry.
        if (!seek_abs(log, log->file_header.tail)) {
            RING_LOG_ERROR("seek_abs failed");
            goto exit;
        }
        log->new_tail_header.len = 0;
        log->new_tail_started = 1;
        log->new_tail_failed = 0;
        entry_header_t *tail_header = &(log->new_tail_header);
        if ((log->new_tail_end_offset = write_wrap(log, 1, (void *)tail_header, sizeof(*tail_header))) == -1) {
            log->new_tail_failed = 1;
            goto exit;
        }
    }

    // Write into the new tail.
    off_t off;
    if ((off = write_wrap(log, 1, p, len)) == -1) {
        log->new_tail_failed = 1;
        goto exit;
    }
    log->new_tail_end_offset = off;
    log->new_tail_header.len += len;

exit:
    ring_log_arch_free_mutex();
}

void ring_log_write_tail_complete(const char *log_fn) {
    log_t *log = lock_and_find_log(log_fn);

    // We didn't start a tail entry, so don't do anything.
    if (!log->new_tail_started) {
        goto exit;
    }

    log->new_tail_started = 0;

    // If we started a new tail entry, but we ran into an error, then just
    // abandon the new tail entry.
    if (log->new_tail_failed) {
        log->new_tail_failed = 0;
        goto exit;
    }

    // Update the size in the log entry's header.
    RING_LOG_EXPECT_NOT(seek_abs(log, log->file_header.tail), 0);
    RING_LOG_EXPECT_NOT(write_wrap(log, 0, (void *)&(log->new_tail_header), sizeof(log->new_tail_header)), -1);

    // Update the tail in the log's header.
    log->file_header.tail = log->new_tail_end_offset;
    RING_LOG_EXPECT_NOT(lseek(log->fd, 0, SEEK_SET), -1);
    RING_LOG_EXPECT_NOT(write_all(log->fd, (void *)&(log->file_header), sizeof(log->file_header)), 0);

exit:
    ring_log_arch_free_mutex();
}

int ring_log_has_unread(const char *log_fn) {
    log_t *log = lock_and_find_log(log_fn);

    int ret = has_unread(log);

    ring_log_arch_free_mutex();

    return ret;
}

int ring_log_read_head(const char *log_fn, void *p, size_t len, size_t *read_total) {
    log_t *log = lock_and_find_log(log_fn);

    // There is an entry to be read if head != tail.
    if (!has_unread(log)) {
        RING_LOG_ERROR("there is no entry to read, use ring_log_has_unread() first");
        goto fail;
    }

    // Seek to the head and read in the size of the entry.
    if (!seek_abs(log, log->file_header.head)) {
        RING_LOG_ERROR("seek_abs failed");
        goto fail;
    }
    entry_header_t entry_header;
    if (read_wrap(log, (void *)&entry_header, sizeof(entry_header)) == -1) {
        RING_LOG_ERROR("read_wrap failed");
        goto fail;
    }

    // If we haven't read in the whole entry,
    size_t remaining = entry_header.len - *read_total;
    if (remaining > 0) {
        // .. first, skip past stuff that we have read already.
        if (read_wrap(log, NULL, *read_total) == -1) {
            RING_LOG_ERROR("read_wrap failed");
            goto fail;
        }

        // Then read in as much of what is remaining as we have `len` for.
        size_t to_read = len < remaining ? len : remaining;
        if (read_wrap(log, p, to_read) == -1) {
            RING_LOG_ERROR("read_wrap failed");
            goto fail;
        }
        *read_total += to_read;

        ring_log_arch_free_mutex();

        return to_read;
    }

    ring_log_arch_free_mutex();
    return 0;
fail:
    ring_log_arch_free_mutex();
    return -1;
}

void ring_log_read_head_success(const char *log_fn) {
    log_t *log = lock_and_find_log(log_fn);

    // Check that there is an entry to be read at all.
    if (!has_unread(log)) {
        RING_LOG_ERROR("there is no entry to read, use ring_log_has_unread() first");
        goto exit;
    }

    // Seek to the head and read in the entry header.
    if (!seek_abs(log, log->file_header.head)) {
        RING_LOG_ERROR("seek_abs failed");
        goto exit;
    }
    entry_header_t entry_header;
    RING_LOG_EXPECT_NOT(read_wrap(log, (void *)&entry_header, sizeof(entry_header)), -1);

    // Figure out where the next entry starts and store that new head in the header.
    uint16_t next_head = read_wrap(log, NULL, entry_header.len);
    log->file_header.head = next_head;
    RING_LOG_EXPECT_NOT(lseek(log->fd, 0, SEEK_SET), -1);
    RING_LOG_EXPECT_NOT(write_all(log->fd, (void *)&(log->file_header), sizeof(log->file_header)), 0);

exit:
    ring_log_arch_free_mutex();
}

#ifdef DEBUG

void sanity_check_file_size(const char *log_fn) {
    log_t *log = lock_and_find_log(log_fn);

    off_t file_len = lseek(log->fd, 0, SEEK_END);
    RING_LOG_EXPECT_NOT(file_len, -1);
    if (file_len != log_size) {
        RING_LOG_ERROR("expected file length to be " xstr(LOG_SIZE));
    }

    ring_log_arch_free_mutex();
}

void debug_print(const char *log_fn) {
    log_t *log = lock_and_find_log(log_fn);

    off_t file_len = lseek(log->fd, 0, SEEK_END);
    RING_LOG_EXPECT_NOT(file_len, -1);
    lseek(log->fd, 0, SEEK_SET);
    for (off_t off = 0; off < file_len; off++) {
        char cell[16] = { 0 };
        if (off % 5 == 0) {
            snprintf(cell, sizeof(cell), "%lu:", off);
            printf("%16s", cell);
        }
        unsigned char c;
        RING_LOG_EXPECT(read(log->fd, &c, 1), 1);
        unsigned int ui = c;
        if (isalnum(c)) {
            snprintf(cell, sizeof(cell), "%c (%u)", c, ui);
        } else {
            snprintf(cell, sizeof(cell), "(%u)", ui);
        }
        printf("%16s", cell);
        if (off % 5 == 4) {
            putchar('\n');
        }
    }
    putchar('\n');

    ring_log_arch_free_mutex();
}

#endif
