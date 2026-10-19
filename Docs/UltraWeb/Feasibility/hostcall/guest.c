// Guest side: N calls of each import, timed with the WASI monotonic clock.
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
__attribute__((import_module("env"), import_name("uc_noop"))) int uc_noop(int, int);
__attribute__((import_module("env"), import_name("uc_set_prop"))) int uc_set_prop(int, int, const char*, unsigned);
static double now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6; }
int main(int argc, char** argv) {
    int n = argc > 1 ? atoi(argv[1]) : 10000000;
    int acc = 0;
    double t = now_ms();
    for (int i = 0; i < n; i++) acc += uc_noop(i, 7);
    double noop = now_ms() - t;
    const char* label = "pretty red table";
    t = now_ms();
    for (int i = 0; i < n; i++) acc += uc_set_prop(i, 3, label, 16);
    double setp = now_ms() - t;
    t = now_ms();
    volatile int x = 0; for (int i = 0; i < n; i++) x += i ^ 7;
    double loop = now_ms() - t;
    printf("{\"calls\":%d,\"noop_ns\":%.1f,\"set_prop_ns\":%.1f,\"empty_loop_ns\":%.1f,\"acc\":%d}\n", n, noop * 1e6 / n, setp * 1e6 / n, loop * 1e6 / n, acc & 1);
    return 0;
}
