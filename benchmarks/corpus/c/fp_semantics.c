#include <stdio.h>

int main(int argc, char **argv) {
    (void)argv;
    double a = 1.5 + 2.25;
    double c = (a - 0.5) * 2.0;
    volatile double zero = 0.0;
    double nan = zero / zero;
    double pinf = 1.0 / zero;
    double ninf = -1.0 / zero;
    int sum = (c == 6.5) + (nan != nan) + (nan < 1.0) +
              (pinf > c) + (ninf < c) + (0.0 == -0.0);
    /* Runtime integer control participates in the validated result. */
    sum += argc - 1;
    printf("checksum: %d\n", sum);
    return 0;
}
