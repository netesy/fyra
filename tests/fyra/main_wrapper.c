#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__attribute__((weak)) extern long long __top_level_wrapper__() {
    return 0;
}

typedef struct {
    long long cap;
    long long len;
    long long* data;
} FyraList;

__attribute__((weak)) void* lm_list_new() {
    FyraList* l = (FyraList*)malloc(sizeof(FyraList));
    l->cap = 16;
    l->len = 0;
    l->data = (long long*)malloc(sizeof(long long) * 16);
    return l;
}

__attribute__((weak)) void lm_list_append(FyraList* l, long long val) {
    if (!l) return;
    if (l->len >= l->cap) {
        l->cap *= 2;
        l->data = (long long*)realloc(l->data, sizeof(long long) * l->cap);
    }
    l->data[l->len++] = val;
}

__attribute__((weak)) long long lm_list_get(FyraList* l, long long idx) {
    if (!l || idx < 0 || idx >= l->len) return 0;
    return l->data[idx];
}

__attribute__((weak)) void lm_list_set(FyraList* l, long long idx, long long val) {
    if (!l || idx < 0 || idx >= l->len) return;
    l->data[idx] = val;
}

__attribute__((weak)) long long lm_list_len(FyraList* l) {
    if (!l) return 0;
    return l->len;
}

__attribute__((weak)) char* lm_box_string(char* s) {
    return s;
}

__attribute__((weak)) char* lm_str_concat(char* a, char* b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t la = strlen(a);
    size_t lb = strlen(b);
    char* res = (char*)malloc(la + lb + 1);
    strcpy(res, a);
    strcat(res, b);
    return res;
}

__attribute__((weak)) char* substring(char* str, long long start, long long len) {
    if (!str) return "";
    size_t slen = strlen(str);
    if (start < 0 || (size_t)start >= slen) return "";
    if (len < 0) len = 0;
    if ((size_t)(start + len) > slen) len = slen - start;
    char* res = (char*)malloc(len + 1);
    strncpy(res, str + start, len);
    res[len] = '\0';
    return res;
}

__attribute__((weak)) long long str_len(char* str) {
    if (!str) return 0;
    return (long long)strlen(str);
}

__attribute__((weak)) char* lm_to_string(long long v) {
    char* buf = (char*)malloc(32);
    snprintf(buf, 32, "%lld", v);
    return buf;
}

__attribute__((weak)) char* lm_rt_str_format(char* fmt, ...) {
    return fmt ? fmt : "";
}

__attribute__((weak)) void lm_print_int(long long v) {
    printf("%lld\n", v);
}

__attribute__((weak)) void lm_assert(long long cond, char* msg) {
    if (!cond) {
        if (msg) fprintf(stderr, "Assertion failed: %s\n", msg);
        else fprintf(stderr, "Assertion failed\n");
    }
}

__attribute__((weak)) int main() {
    __top_level_wrapper__();
    return 0;
}
