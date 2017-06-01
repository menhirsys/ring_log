#ifndef __RING_LOG_H__
#define __RING_LOG_H__

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct {
    uint16_t head;
    uint16_t tail;
} file_header_t;

typedef struct {
    uint16_t len;
} entry_header_t;

typedef struct {
    const char *fn;
    int fd;
    file_header_t file_header;
    int new_tail_started;
    int new_tail_failed;
    off_t new_tail_end_offset;
    entry_header_t new_tail_header;
} log_t;

#ifdef DEBUG

#include <stdio.h>
#include <stdlib.h>

#define str(s) #s
#define xstr(s) str(s)

#define RING_LOG_MSG(s) puts(__FILE__ ", L" xstr(__LINE__) ": " s "\n");

#define RING_LOG_ERROR(s) RING_LOG_MSG(s); ring_log_arch_abort()

#define RING_LOG_EXPECT(v, e) \
    do { \
        if ((v) != e) { \
            RING_LOG_ERROR("expected " xstr(e)); \
        } \
    } while(0);

#define RING_LOG_EXPECT_NOT(v, e) \
    do { \
        if ((v) == e) { \
            RING_LOG_ERROR("did not expect " xstr(e)); \
        } \
    } while(0);

void sanity_check_file_size(const char *);
void debug_print(const char *);

#else

#define RING_LOG_MSG(s)

#define RING_LOG_ERROR(s)

#define RING_LOG_EXPECT(v, e) (void)v; (void)e

#define RING_LOG_EXPECT_NOT(v, e) (void)v; (void)e

#endif

void ring_log_arch_abort(void);
void ring_log_arch_init(void);
void ring_log_arch_deinit(void);
void ring_log_arch_take_mutex(void);
void ring_log_arch_free_mutex(void);

int ring_log_init(void);
void ring_log_deinit(void);
void ring_log_write_tail(const char *, const void *, size_t);
void ring_log_write_tail_complete(const char *);
int ring_log_has_unread(const char *);
int ring_log_read_head(const char *, void *, size_t, size_t *);
void ring_log_read_head_success(const char *);

#endif
