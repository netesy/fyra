#include <stdio.h>
#include <stdlib.h>

long long g_iterations = 0;

void print_checksum(long long sum) {
    printf("checksum: %lld\n", sum);
}

long long get_loop_count(int argc, char** argv, long long default_count) {
    if (argc > 1) {
        long long val = atoll(argv[1]);
        if (val > 0) return val;
    }
    return default_count;
}
