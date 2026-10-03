#include <stdint.h>
#include <stdio.h>

int main(void) {
    int64_t checksum = 0;
    uint32_t outer_bound = 2000000;
    for (uint32_t i = 0; i < outer_bound; ++i) {
        uint32_t inner_bound = 32u + (i & 31u);
        for (uint32_t j = 0; j < inner_bound; ++j)
            checksum += (int64_t)((i ^ (j * 17u)) & 255u);
    }
    printf("checksum: %lld\n", (long long)checksum);
    return 0;
}
