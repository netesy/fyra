#include <stdint.h>
#include <stdio.h>

int main(void) {
    uint32_t state = 1;
    int64_t checksum = 0;
    for (uint32_t i = 0; i < 20000000; ++i) {
        state = state * 1664525u + 1013904223u;
        if ((state & 15u) == 0u) {
            checksum += (int64_t)(state >> 8);
        } else if ((state & 3u) == 1u) {
            checksum -= (int64_t)(state & 65535u);
        } else {
            checksum += (state & 1u) ? 7 : 3;
        }
        if ((state & 0xffffu) == 0xace1u)
            checksum ^= (int64_t)i;
    }
    printf("checksum: %lld\n", (long long)checksum);
    return 0;
}
