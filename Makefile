.SUFFIXES:

.PHONY:
run_tests: test
	./test

CFLAGS=-std=c99 -pedantic -Wall

test: $(shell git ls-files)
	$(CC) $(CFLAGS) -o $@ -DDEBUG ring_log.c ring_log_arch_posix.c ring_log_config.c test.c

example: $(shell git ls-files)
	$(CC) $(CFLAGS) -o $@ -DDEBUG ring_log.c ring_log_arch_posix.c ring_log_config.c example.c
