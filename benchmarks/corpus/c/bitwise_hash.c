#include <stdint.h>
#include <stdio.h>

static uint64_t mix(uint64_t value) {
    value ^= value >> 30;
    value *= UINT64_C(636413622);
    value ^= value >> 27;
    value *= UINT64_C(1442695041);
    return value ^ (value >> 31);
}

int main(void) {
    uint64_t checksum = 0;
    for (uint64_t i = 1; i <= UINT64_C(50000000); ++i)
        checksum ^= mix(i);
    printf("checksum: %lld\n", (long long)(int64_t)checksum);
    return 0;
}
