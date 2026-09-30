/* ina260_test.c -- standalone check of ina260.h.
 *
 * Samples an idle window, then keeps one core fully busy, and prints the
 * sampling statistics and the power/energy delta between the two.
 * Run it inside the container, where profile01.c will run:
 *
 *   gcc -O2 -Wall -pthread -o /tmp/ina260_test ina260_test.c -lm && /tmp/ina260_test
 */
#define _GNU_SOURCE
#include <math.h>
#include "ina260.h"

#define PERIOD_US   2000   /* ~ INA260 update interval (1.1 ms I + 1.1 ms V) */
#define SAMPLER_CPU 1
#define LOAD_CPU    3      /* cpu0 takes most IRQs */
#define IDLE_S      2.0
#define LOAD_S      3.0

static void pin_self(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    sched_setaffinity(0, sizeof set, &set);
}

int main(void)
{
    ina260_t s;
    volatile unsigned long spin = 0;
    double t0, t1, t2, e_idle, e_load, p_idle, p_load, d, dtmax = 0, sum = 0, sq = 0;
    size_t i, k = 0;

    pin_self(LOAD_CPU);
    if (ina260_open(&s, PERIOD_US, SAMPLER_CPU, IDLE_S + LOAD_S + 2) < 0) {
        fprintf(stderr, "ina260: sensor not found or unreadable\n");
        return 1;
    }
    if (ina260_start(&s)) {
        fprintf(stderr, "ina260: cannot start sampler thread\n");
        return 1;
    }

    usleep(100000);                          /* let the sampler settle */
    t0 = ina260_now();
    usleep((useconds_t)(IDLE_S * 1e6));      /* idle window */
    t1 = ina260_now();
    while (ina260_now() < t1 + LOAD_S)       /* one core busy */
        spin++;
    t2 = ina260_now();
    usleep(50000);
    ina260_stop(&s);

    if (s.overflow || !ina260_covers(&s, t0, t2)) {
        fprintf(stderr, "ina260: incomplete sampling (n=%zu, overflow=%d)\n", s.n, s.overflow);
        return 2;
    }

    for (i = 1; i < s.n; i++) {
        d = s.t[i] - s.t[i - 1];
        if (d > dtmax) dtmax = d;
    }
    for (i = 0; i < s.n; i++)
        if (s.t[i] >= t0 && s.t[i] <= t1) { sum += s.p[i]; sq += s.p[i] * s.p[i]; k++; }

    e_idle = ina260_energy(&s, t0, t1);
    e_load = ina260_energy(&s, t1, t2);
    p_idle = e_idle / (t1 - t0);
    p_load = e_load / (t2 - t1);

    printf("samples     : %zu  (mean dt %.3f ms, max dt %.3f ms)\n",
           s.n, (s.t[s.n - 1] - s.t[0]) / (s.n - 1) * 1e3, dtmax * 1e3);
    printf("idle        : %.3f W  (sample sd %.1f mW, %zu samples, %.2f s)\n",
           p_idle, 1e3 * sqrt(sq / k - (sum / k) * (sum / k)), k, t1 - t0);
    printf("1 core busy : %.3f W  (%.2f s)\n", p_load, t2 - t1);
    printf("delta       : %.3f W  ->  E_net %.3f J  (E_gross %.3f J)\n",
           p_load - p_idle, e_load - p_idle * (t2 - t1), e_load);

    ina260_close(&s);
    return 0;
}
