For Linux systems, there is `logrotate`. For embedded systems, there's
`ring_log`.

Specify how many total logs you want to keep (in bytes), and then use
`ring_log`'s API to add new log entries.

`ring_log` will manage the on-disk log storage such that only the N most recent
log entries are kept. It will use the amount of disk space that you allow it
to, and no more.

How to use:
-

Copy over `ring_log.c`, `ring_log.h`, and the `ring_log_arch_*.c` matching your
OS.

Copy *and edit* `ring_log_config.c`. Important values such as the total log
size, the number of logs, etc, are defined there.

Start up `ring_log` and write a few entries (see `example.c`):

```
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
```

Read all the entries out:

```
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
```
