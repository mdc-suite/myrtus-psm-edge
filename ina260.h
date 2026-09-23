/* ina260.h -- SOM power sampler for the Kria K26 on-module INA260.
 *
 * al3monni mod: energy measurement on aarch64. likwid's ENERGY group is
 * built on x86 RAPL and cannot be read on the Cortex-A53 (LOGBOOK M-A7, §9).
 * The only power source on the SOM is the INA260, exposed by hwmon as
 * power1_input in microwatts. It is a board-level figure (PS + PL + DDR),
 * so callers must subtract an idle baseline to isolate a workload.
 *
 * A background thread samples power1_input at a fixed period and stores
 * (timestamp, watts) pairs. ina260_energy() integrates them with the
 * trapezoidal rule over any [t0, t1] window. Timestamps are CLOCK_MONOTONIC,
 * which is system-wide: a child process (or a process in a container on the
 * same kernel) can report its own window and the parent integrates over it.
 *
 * Header-only, like cycles.h, so profile01.c still builds as a single file.
 * Link with -pthread.
 */
#ifndef INA260_H
#define INA260_H

#ifndef _GNU_SOURCE
#error "ina260.h needs _GNU_SOURCE defined before the first #include"
#endif

#include <dirent.h>
#include <math.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define INA260_HWMON_DIR "/sys/class/hwmon"
#define INA260_NAME      "ina260"   /* name prefix, matches "ina260_u14" */

/* Block length of the robust estimator: the window is cut into blocks of this
 * many seconds, each block is averaged and the median of those averages is
 * taken. A foreign burst spoils a couple of blocks and the median ignores
 * them, while averaging ~100 samples per block keeps the resolution well below
 * the INA260's 10 mW step, which a plain sample median cannot do. */
#define INA260_BLOCK_S 0.25

/* Statistics of the samples inside a window.
 *   mean   : trapezoid integral / duration -- fine resolution, not robust
 *   median : median of the samples -- robust, quantised to the 10 mW step
 *   robust : median of the block means -- robust AND fine resolution (use this) */
typedef struct {
    size_t n, blocks;
    double mean, median, robust, sd, min, max;
} ina260_stat;

typedef struct {
    int       fd;         /* power1_input, kept open between reads */
    long      period_ns;  /* sampling period */
    int       cpu;        /* core the sampler thread is pinned to, -1 = none */
    double   *t;          /* sample timestamps, s, CLOCK_MONOTONIC */
    double   *p;          /* sample power, W */
    double   *scratch;    /* sorting buffer for the median */
    size_t    n, cap;
    int       overflow;   /* set if the buffer filled up before stop */
    volatile int run;
    pthread_t th;
} ina260_t;

static inline double ina260_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* One raw read of power1_input, in microwatts; -1 on error. */
static inline long ina260_read_uw(int fd)
{
    char buf[32];
    ssize_t k;

    if (lseek(fd, 0, SEEK_SET) < 0) return -1;
    k = read(fd, buf, sizeof buf - 1);
    if (k <= 0) return -1;
    buf[k] = 0;
    return strtol(buf, NULL, 10);
}

static inline void ina260_close(ina260_t *s)
{
    if (s->fd >= 0) close(s->fd);
    free(s->t);
    free(s->p);
    free(s->scratch);
    s->fd = -1;
    s->t = s->p = s->scratch = NULL;
}

/* Find the INA260 by hwmon name (the hwmonN index is not stable across
 * boots), open power1_input and size the buffer for max_seconds of samples.
 * Returns 0 on success, -1 if the sensor is missing or unreadable. */
static inline int ina260_open(ina260_t *s, long period_us, int cpu, double max_seconds)
{
    char path[512], name[64];
    struct dirent *e;
    DIR *d;
    FILE *f;

    memset(s, 0, sizeof *s);
    s->fd = -1;

    if (!(d = opendir(INA260_HWMON_DIR))) return -1;
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "hwmon", 5)) continue;
        snprintf(path, sizeof path, INA260_HWMON_DIR "/%s/name", e->d_name);
        if (!(f = fopen(path, "r"))) continue;
        if (fgets(name, sizeof name, f) && !strncmp(name, INA260_NAME, strlen(INA260_NAME))) {
            snprintf(path, sizeof path, INA260_HWMON_DIR "/%s/power1_input", e->d_name);
            s->fd = open(path, O_RDONLY);
        }
        fclose(f);
        if (s->fd >= 0) break;
    }
    closedir(d);
    if (s->fd < 0) return -1;

    s->period_ns = period_us * 1000L;
    s->cpu = cpu;
    s->cap = (size_t)(max_seconds * 1e6 / period_us) + 64;
    s->t = malloc(s->cap * sizeof *s->t);
    s->p = malloc(s->cap * sizeof *s->p);
    s->scratch = malloc(s->cap * sizeof *s->scratch);
    if (!s->t || !s->p || !s->scratch || ina260_read_uw(s->fd) < 0) {
        ina260_close(s);
        return -1;
    }
    return 0;
}

/* Sampler thread: absolute-deadline loop, one read per period. Each sample
 * is stamped at the midpoint of its read (a read is one I2C transaction). */
static inline void *ina260_loop(void *arg)
{
    ina260_t *s = arg;
    struct timespec next;
    double a, b;
    long uw;

    if (s->cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(s->cpu, &set);
        pthread_setaffinity_np(pthread_self(), sizeof set, &set);
    }

    clock_gettime(CLOCK_MONOTONIC, &next);
    while (s->run) {
        a  = ina260_now();
        uw = ina260_read_uw(s->fd);
        b  = ina260_now();
        if (uw >= 0) {
            if (s->n < s->cap) {
                s->t[s->n] = 0.5 * (a + b);
                s->p[s->n] = uw * 1e-6;
                s->n++;
            } else {
                s->overflow = 1;
            }
        }
        next.tv_nsec += s->period_ns;
        while (next.tv_nsec >= 1000000000L) {
            next.tv_nsec -= 1000000000L;
            next.tv_sec++;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
    return NULL;
}

/* Start sampling into an empty buffer. Returns 0 on success. */
static inline int ina260_start(ina260_t *s)
{
    s->n = 0;
    s->overflow = 0;
    s->run = 1;
    return pthread_create(&s->th, NULL, ina260_loop, s);
}

static inline void ina260_stop(ina260_t *s)
{
    s->run = 0;
    pthread_join(s->th, NULL);
}

/* True if the samples span the whole [t0, t1] window. */
static inline int ina260_covers(const ina260_t *s, double t0, double t1)
{
    return s->n >= 2 && s->t[0] <= t0 && s->t[s->n - 1] >= t1;
}

/* Trapezoidal integral of P(t) over [t0, t1], in joules. Segments that
 * straddle the window edges are clipped, with P linearly interpolated at
 * the edge. Check ina260_covers() first. */
static inline double ina260_energy(const ina260_t *s, double t0, double t1)
{
    double e = 0, ta, tb, a, b, k;
    size_t i;

    for (i = 0; i + 1 < s->n; i++) {
        ta = s->t[i];
        tb = s->t[i + 1];
        if (tb <= t0 || ta >= t1 || tb <= ta) continue;
        k = (s->p[i + 1] - s->p[i]) / (tb - ta);
        a = ta < t0 ? t0 : ta;
        b = tb > t1 ? t1 : tb;
        e += 0.5 * ((s->p[i] + k * (a - ta)) + (s->p[i] + k * (b - ta))) * (b - a);
    }
    return e;
}

static inline int ina260_cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

/* Statistics of the samples in [t0, t1]; st->robust is the level to use. */
static inline void ina260_stats(ina260_t *s, double t0, double t1, ina260_stat *st)
{
    double bsum[256], sum = 0, sq = 0, var, len = t1 - t0;
    size_t bcnt[256], i, j, k = 0, nb;

    st->n = st->blocks = 0;
    st->mean = st->median = st->robust = st->sd = st->min = st->max = 0;

    for (i = 0; i < s->n; i++) {
        if (s->t[i] < t0 || s->t[i] > t1) continue;
        if (k == 0 || s->p[i] < st->min) st->min = s->p[i];
        if (k == 0 || s->p[i] > st->max) st->max = s->p[i];
        s->scratch[k++] = s->p[i];
        sum += s->p[i];
        sq  += s->p[i] * s->p[i];
    }
    if (!(st->n = k) || len <= 0) return;

    qsort(s->scratch, k, sizeof *s->scratch, ina260_cmp);
    st->median = (k % 2) ? s->scratch[k / 2]
                         : 0.5 * (s->scratch[k / 2 - 1] + s->scratch[k / 2]);
    var      = sq / k - (sum / k) * (sum / k);
    st->sd   = var > 0 ? sqrt(var) : 0;
    st->mean = ina260_energy(s, t0, t1) / len;

    /* median of the block means */
    nb = (size_t)(len / INA260_BLOCK_S);
    if (nb < 3)   nb = 3;
    if (nb > 255) nb = 255;
    for (j = 0; j < nb; j++) { bsum[j] = 0; bcnt[j] = 0; }
    for (i = 0; i < s->n; i++) {
        if (s->t[i] < t0 || s->t[i] > t1) continue;
        j = (size_t)((s->t[i] - t0) / len * nb);
        if (j >= nb) j = nb - 1;
        bsum[j] += s->p[i];
        bcnt[j]++;
    }
    k = 0;
    for (j = 0; j < nb; j++)
        if (bcnt[j]) s->scratch[k++] = bsum[j] / bcnt[j];
    if (!(st->blocks = k)) return;
    qsort(s->scratch, k, sizeof *s->scratch, ina260_cmp);
    st->robust = (k % 2) ? s->scratch[k / 2]
                         : 0.5 * (s->scratch[k / 2 - 1] + s->scratch[k / 2]);
}

#endif /* INA260_H */
