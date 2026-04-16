CC = gcc
CFLAGS = -pthread

all: scc_test ro_bench_unpadded

scc_test: scc_test.c
	$(CC) $(CFLAGS) scc_test.c -o scc_test

ro_bench_unpadded: ro_bench.c
	$(CC) $(CFLAGS) ro_bench.c -o ro_bench_unpadded

clean:
	rm -f scc_test ro_bench_unpadded

