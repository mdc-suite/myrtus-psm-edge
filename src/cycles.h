#ifndef CYCLES_H
#define CYCLES_H

#if defined(__x86_64__) || defined(__i386__)

static inline __attribute__((always_inline)) unsigned long rdtscp(void)
{
   unsigned long a, d, c;
   __asm__ volatile("rdtscp" : "=a" (a), "=d" (d), "=c" (c));
   return (a | (d << 32));
}

#elif defined(__aarch64__)

/* ARM generic timer. NOTE: counts at cntfrq_el0 (fixed, ~33 MHz on
   Zynq UltraScale+), NOT at the CPU clock like x86 TSC. Ticks here are
   not comparable to x86 ticks. See cycle_freq() below. */
static inline __attribute__((always_inline)) unsigned long rdtscp(void)
{
   unsigned long val;
   __asm__ volatile("isb" ::: "memory");
   __asm__ volatile("mrs %0, cntvct_el0" : "=r" (val));
   return val;
}

static inline unsigned long cycle_freq(void)
{
   unsigned long f;
   __asm__ volatile("mrs %0, cntfrq_el0" : "=r" (f));
   return f;
}

#else
#error "no cycle counter for this architecture"
#endif

#endif