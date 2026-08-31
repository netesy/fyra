#include <stdio.h>

long long dot_product(int n) {
    long long sum = 0;
    for (int i = 0; i < n; i++) {
        long long a = i * 3 + 1;
        long long b = i * 7 + 2;
        sum += a * b;
    }
    return sum;
}

int main() {
    long long res = 0;
    for (int k = 0; k < 20; k++) {
        res += dot_product(5000000);
    }
    printf("checksum: %lld\n", res);
    return 0;
}
