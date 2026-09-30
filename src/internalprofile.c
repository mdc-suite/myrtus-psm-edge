/* internalprofile.c -- the process that gets measured.
 *
 * It contains one backend and nothing else, so whatever is measured around it --
 * likwid on x86-64, the INA260 sampler on aarch64 (see profile01.c) -- is measuring
 * that backend. The backend's object file is linked in and exported with -rdynamic,
 * so the symbol is resolved out of the executable itself:
 *
 *     gcc -o internalprofile internalprofile.c LIB/enc_sXX_nYY.o -ldl -rdynamic
 *     ./internalprofile <slevel> <index> [seconds]
 *
 * Two modes:
 *   - without the third argument: ITER iterations, upstream behaviour, used by the
 *     x86-64 likwid path;
 *   - with it: the loop runs for that many seconds and then prints its own window
 *     and iteration count, which is what the INA260 path integrates over.
 *
 *     window <t0> <t1> <iterations>          (t0, t1: CLOCK_MONOTONIC, seconds)
 *
 * The timed mode exists because the backends span three orders of magnitude, from
 * 0.57 s to 145 s per ITER iterations: no fixed iteration count is both long enough
 * to measure the fastest and short enough to be practical on the slowest.
 *
 * al3monni mod: the timed mode, and the removal of upstream's dead code (an x86
 * rdtscp definition and an rdmsr helper, neither ever called from here).
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ITER  50000   /* fixed-count mode: iterations, as upstream */
#define BATCH 16      /* timed mode: iterations between two clock reads */

typedef void (*aes_fn)(unsigned char *key, unsigned char *pt, unsigned char *ct);

static void fill_random(unsigned char *buf, int n)
{
    int i;

    for (i = 0; i < n; i++)
        buf[i] = rand() & 0xff;
}

static double now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    unsigned char key[16], pt[16], ct[16];
    char symbol[64];
    aes_fn encrypt;
    void *self;
    int i;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <security level> <index> [seconds]\n", argv[0]);
        return 1;
    }

    srand(time(NULL));
    sprintf(symbol, "enc_s%02d_n%02d", atoi(argv[1]), atoi(argv[2]));

    if ((self = dlopen(NULL, RTLD_NOW | RTLD_GLOBAL)) == NULL) {
        fprintf(stderr, "%s\n", dlerror());
        return 1;
    }
    if ((encrypt = (aes_fn)dlsym(self, symbol)) == NULL) {
        fprintf(stderr, "%s\n", dlerror());
        return 2;
    }
    puts(symbol);

    if (argc > 3) {                       /* timed mode */
        double seconds = atof(argv[3]), t0, t1;
        unsigned long iterations = 0;

        t0 = now();
        do {
            for (i = 0; i < BATCH; i++) {
                fill_random(key, sizeof key);
                fill_random(pt, sizeof pt);
                encrypt(key, pt, ct);
            }
            iterations += BATCH;
            t1 = now();
        } while (t1 - t0 < seconds);

        printf("window %.9f %.9f %lu\n", t0, t1, iterations);
        return 0;
    }

    for (i = 0; i < ITER; i++) {          /* fixed-count mode */
        fill_random(key, sizeof key);
        fill_random(pt, sizeof pt);
        encrypt(key, pt, ct);
    }

    return 0;
}
