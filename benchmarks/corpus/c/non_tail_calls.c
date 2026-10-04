#include <stdint.h>
#include <stdio.h>

static int64_t helper2(int16_t x) { return (int64_t)x + 11; }
__attribute__((noinline)) static int64_t helper(int16_t x, int64_t y) {
    int64_t a = helper2(x), v = ((a + y) * 3 ^ y) + 7;
    v = v * 5 - a;
    const int p[] = {17,19,23,29,31,37,41,43,47,53,59,61,67,71,73,79,83,89,97,101};
    for (int i = 0; i < 20; ++i) v = (i & 1) ? v + p[i] : v ^ p[i];
    return v;
}
int main(int argc, char **argv) {
    (void)argv;
    int16_t x = (int16_t)argc;
    int64_t a = helper(x, argc + 99);
    int64_t b = helper((int16_t)(x + 4), argc + 199);
    printf("checksum: %lld\n", (long long)(a + b + 1));
    return 0;
}
