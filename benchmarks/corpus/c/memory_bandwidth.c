#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint64_t exercise(size_t n, unsigned rounds) {
    uint64_t *a = calloc(n, sizeof(*a));
    uint64_t *b = calloc(n, sizeof(*b));
    uint64_t *c = calloc(n, sizeof(*c));
    if (!a || !b || !c) return 0;
    for (size_t i = 0; i < n; ++i) { a[i] = i + 1; b[i] = (i * 3) ^ 0x55; }
    for (unsigned r = 0; r < rounds; ++r) {
        for (size_t i = 0; i < n; ++i) c[i] = a[i];
        for (size_t i = 0; i < n; ++i) c[i] += b[i];
        for (size_t i = 0; i < n; ++i) a[i] = c[i] + 3 * b[i];
    }
    uint64_t sum = a[0] ^ a[n / 2] ^ a[n - 1] ^ c[n / 3];
    free(a); free(b); free(c); return sum;
}
int main(void) {
    uint64_t checksum = exercise(4096, 512) ^ exercise(131072, 24);
    printf("checksum: %lld\n", (long long)checksum);
}
