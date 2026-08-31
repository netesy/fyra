#include <stdio.h>

int test_basic_add(int a, int b) { return a + b; }
int test_basic_sub(int a, int b) { return a - b; }
int test_basic_mul(int a, int b) { return a * b; }
int test_basic_div(int a, int b) { return a / b; }
int test_basic_rem(int a, int b) { return a % b; }
int test_mixed(int a, int b, int c, int d) { return (a + b) * (c - d); }
int test_chained(int a, int b, int c, int d) { return ((a * b) + (c * d)) - (a + d); }
int test_constant(int a) { return ((a + 1) * 2) - 3; }

int main() {
    long long sum = 0;
    for (int i = 1; i <= 100000000; i++) {
        sum += test_mixed(i, i % 7, i % 11, i % 13);
        sum += test_chained(i % 5, i % 9, i % 17, i % 23);
        sum += test_constant(i);
    }
    printf("checksum: %lld\n", sum);
    return 0;
}
