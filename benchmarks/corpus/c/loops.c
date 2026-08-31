#include <stdio.h>

long long test_loop_sum(int n) {
    long long sum = 0;
    for (int i = 0; i < n; i++) {
        sum += (i * 2);
    }
    return sum;
}

int main() {
    long long sum = 0;
    for (int i = 1; i <= 50; i++) {
        sum += test_loop_sum(2000000);
    }
    printf("checksum: %lld\n", sum);
    return 0;
}
