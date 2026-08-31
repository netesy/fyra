#include <stdio.h>

int test_reg_pressure_16(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7) {
    int v0 = a0 + a1;
    int v1 = a2 + a3;
    int v2 = a4 + a5;
    int v3 = a6 + a7;
    int v4 = v0 * v1;
    int v5 = v2 * v3;
    int v6 = v0 + v2;
    int v7 = v1 + v3;
    return (v4 + v5) * (v6 - v7);
}

int main() {
    long long sum = 0;
    for (int i = 1; i <= 50000000; i++) {
        sum += test_reg_pressure_16(i, i+1, i+2, i+3, i+4, i+5, i+6, i+7);
    }
    printf("checksum: %lld\n", sum);
    return 0;
}
