#include <stdio.h>

long long simd_loop_calc(int n) {
    long long sum = 0;
    for (int i = 0; i < n; i++) {
        long long a = i * 5 + 3;
        long long b = i * 2 + 7;
        sum += a * b + (a - b);
    }
    return sum;
}

int main() {
    long long res = 0;
    for (int k = 0; k < 20; k++) {
        res += simd_loop_calc(5000000);
    }
    printf("checksum: %lld\n", res);
    return 0;
}
