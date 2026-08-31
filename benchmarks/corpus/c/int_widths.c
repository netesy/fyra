#include <stdio.h>
#include <stdint.h>

int64_t test_widths(int8_t a, uint8_t b, int16_t c, uint16_t d, int32_t e, uint32_t f, int64_t g, uint64_t h) {
    int64_t s1 = (int64_t)a + (int64_t)b;
    int64_t s2 = (int64_t)c + (int64_t)d;
    int64_t s3 = (int64_t)e + (int64_t)f;
    int64_t s4 = g + (int64_t)h;
    return s1 + s2 + s3 + s4;
}

int main() {
    long long sum = 0;
    for (int i = 1; i <= 50000000; i++) {
        sum += test_widths((int8_t)i, (uint8_t)i, (int16_t)i, (uint16_t)i, (int32_t)i, (uint32_t)i, (int64_t)i, (uint64_t)i);
    }
    printf("checksum: %lld\n", sum);
    return 0;
}
