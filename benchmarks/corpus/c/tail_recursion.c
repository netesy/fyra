#include <stdio.h>

long long tail_factorial(long long n, long long acc) {
    if (n <= 1) return acc;
    return tail_factorial(n - 1, acc * n);
}

int main() {
    long long sum = 0;
    for (int i = 0; i < 5000000; i++) {
        sum += tail_factorial(15, 1);
    }
    printf("checksum: %lld\n", sum);
    return 0;
}
