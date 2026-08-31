int fibonacci(int n) {
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

int leaf_arithmetic(int a, int b, int c, int d) {
    return (a + b) * (c - d);
}

int compare_branch(int a, int b) {
    if (a > b) return a - b;
    return b - a;
}

int helper(int x) {
    return x + 1;
}

int call_result(int x) {
    int a = helper(x);
    int b = helper(a);
    return b;
}

int live_across_calls(int x, int y) {
    int a = helper(x);
    int b = helper(y);
    return a + b;
}

int register_pressure(int a, int b, int c, int d, int e, int f, int g, int h) {
    int v1 = a + b;
    int v2 = c + d;
    int v3 = e + f;
    int v4 = g + h;
    int v5 = v1 * v2;
    int v6 = v3 * v4;
    return v5 + v6;
}

int loop_sum(int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += i * 2;
    }
    return sum;
}
