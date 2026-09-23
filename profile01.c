/* profile01.c -- measure one backend and append its entry to db.yaml.
 *
 * Invoked by gen.c at the end of a registration as `./profile <slevel> <index> <cpu>`.
 * It runs internalprofile, which loops that backend and nothing else, measures it,
 * and writes the time and the energy of INA_ITER_REF iterations.
 *
 *   x86-64  : upstream path, unchanged -- internalprofile is wrapped in
 *             `likwid-perfctr -f -g ENERGY` and the figures are parsed out of its
 *             output ("Runtime unhalted [s]", "Energy Core [J]").
 *   aarch64 : likwid's ENERGY group is defined on x86 RAPL counters and cannot be
 *             read on the Cortex-A53, so the energy comes from the SOM's INA260
 *             power monitor instead. See ina260.h and LOGBOOK.md M-A14.
 *
 * al3monni mod: the aarch64 path, plus the removal of the dead upstream code that
 * used to sit here -- an inline profiling loop superseded by internalprofile.c, and
 * the x86 MSR helpers it needed.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__aarch64__)

#include "ina260.h"

#define INA_PERIOD_US   2000   /* sampling period; the INA260 updates every ~2.2 ms */
#define INA_SAMPLER_CPU 1      /* sampler core; the workload core is argv[3] */
#define INA_IDLE_S      2.0    /* idle baseline window, taken before and after, s */
#define INA_RUN_S       3.0    /* workload window, s */
#define INA_ITER_REF    50000  /* = ITER in internalprofile.c: db.yaml figures are
                                  per 50000 iterations, as on x86 */
#define INA_TOL_W       0.015  /* quality gate: max |idle before - idle after| and
                                  |run 1st half - 2nd half| */
#define INA_ATTEMPTS    3      /* attempts per backend when the gate fails */
#define INA_LOG         "power.csv"   /* per-window statistics, next to db.yaml */
#define INA_ABS(x)      ((x) < 0 ? -(x) : (x))

#define INA_LOG_COLUMNS \
    "name,attempt,status,iters,window_s," \
    "pre_n,pre_mean,pre_med,pre_rob,pre_sd,pre_min,pre_max," \
    "post_mean,post_med,post_rob,post_sd,h1_rob,h2_rob," \
    "run_n,run_mean,run_med,run_rob,run_sd,run_min,run_max," \
    "dev_mw,net_rob_w,net_med_w,net_mean_w"

#else  /* x86-64: pull the figures out of likwid's output */

static double get_double(const char *str)
{
    while (*str && !(isdigit((unsigned char)*str) ||
                     ((*str == '-' || *str == '+') && isdigit((unsigned char)str[1]))))
        str++;
    return strtod(str, NULL);
}

static void extract(const char *line, double *energy, double *runtime)
{
    if (strstr(line, "Runtime unhalted [s]")) *runtime = get_double(line);
    if (strstr(line, "Energy Core [J]"))      *energy  = get_double(line);
}

#endif

int main(int argc, char **argv)
{
    char exec[128], name[128], buffer[1024];
    double energy = 0, runtime = 0;
    cpu_set_t cpuset;
    int cpu;
    FILE *f;

    if (argc < 4) {
        fprintf(stderr, "usage: %s <security level> <index> <cpu>\n", argv[0]);
        return 1;
    }

    cpu = atoi(argv[3]);
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    sched_setaffinity(0, sizeof cpuset, &cpuset);   /* internalprofile inherits this */

    sprintf(name, "enc_s%02d_n%02d", atoi(argv[1]), atoi(argv[2]));

#if defined(__aarch64__)
    /* Idle baseline before and after a time-based run of the backend; the SOM power
     * over the loop's own window minus that baseline, scaled to INA_ITER_REF.
     *
     * The INA260 sees the whole module, so a burst from any other process lands in
     * the measurement. Two defences:
     *   - every window's power level is the median of its 250 ms block means
     *     (ina260_stats -> robust): a burst spoils a couple of blocks and leaves
     *     their median where it was, and unlike a plain sample median it is not
     *     quantised to the sensor's 10 mW step, which is most of the gap between
     *     two backends;
     *   - a quality gate: idle before/after and the two halves of the run must agree
     *     within INA_TOL_W and the net power must be positive, otherwise the
     *     measurement is repeated, up to INA_ATTEMPTS, keeping the cleanest attempt.
     *
     * db.yaml carries the robust figure; the median- and mean-based ones and the full
     * per-window statistics go to INA_LOG for later analysis.
     */
    {
        ina260_t s;
        ina260_stat pre, post, run, h1, h2;
        double b0, b1, a0, a1, w0, w1, wm, dev;
        double idle_rob, idle_med, idle_mean, net_rob, net_med, net_mean;
        double best = -1, wb = 0;
        double b_idle_rob = 0, b_idle_med = 0, b_idle_mean = 0;
        double b_net_rob = 0, b_net_med = 0, b_net_mean = 0, b_pre_sd = 0, b_run_sd = 0;
        unsigned long n, nb = 0;
        int k, kb = 0;
        FILE *plog;

        if (ina260_open(&s, INA_PERIOD_US, cpu == INA_SAMPLER_CPU ? 2 : INA_SAMPLER_CPU,
                        2 * INA_IDLE_S + INA_RUN_S + 5) < 0) {
            fprintf(stderr, "profile: INA260 not available\n");
            return 3;
        }
        sprintf(exec, "./internalprofile %d %d %g", atoi(argv[1]), atoi(argv[2]), INA_RUN_S);

        for (k = 1; k <= INA_ATTEMPTS; k++) {
            FILE *command;

            w0 = w1 = 0;
            n = 0;
            if (ina260_start(&s)) {
                fprintf(stderr, "profile: cannot start the INA260 sampler\n");
                ina260_close(&s);
                return 3;
            }
            usleep(100000);                             /* let the sampler settle */
            b0 = ina260_now();
            usleep((useconds_t)(INA_IDLE_S * 1e6));     /* idle before */
            b1 = ina260_now();

            if ((command = popen(exec, "r")) != NULL) {
                while (fgets(buffer, sizeof buffer, command) != NULL)
                    sscanf(buffer, "window %lf %lf %lu", &w0, &w1, &n);
                pclose(command);
            }

            a0 = ina260_now();
            usleep((useconds_t)(INA_IDLE_S * 1e6));     /* idle after */
            a1 = ina260_now();
            usleep(50000);
            ina260_stop(&s);

            if (n == 0 || s.overflow || !ina260_covers(&s, b0, a1)) {
                fprintf(stderr, "profile: INA260 measurement failed "
                                "(n=%lu, samples=%zu, overflow=%d)\n", n, s.n, s.overflow);
                ina260_close(&s);
                return 4;
            }

            wm = 0.5 * (w0 + w1);
            ina260_stats(&s, b0, b1, &pre);
            ina260_stats(&s, a0, a1, &post);
            ina260_stats(&s, w0, wm, &h1);
            ina260_stats(&s, wm, w1, &h2);
            ina260_stats(&s, w0, w1, &run);

            idle_rob  = 0.5 * (pre.robust + post.robust);
            idle_med  = 0.5 * (pre.median + post.median);
            idle_mean = 0.5 * (pre.mean   + post.mean);
            net_rob   = run.robust - idle_rob;
            net_med   = run.median - idle_med;
            net_mean  = run.mean   - idle_mean;

            dev = INA_ABS(pre.robust - post.robust) > INA_ABS(h1.robust - h2.robust)
                ? INA_ABS(pre.robust - post.robust) : INA_ABS(h1.robust - h2.robust);
            if (net_rob <= 0) dev = 1.0;   /* a busy core cannot draw less than idle */

            fprintf(stderr, "%s: attempt %d/%d: idle %.4f/%.4f W, run halves %.4f/%.4f W, "
                            "net %.4f W, deviation %.1f mW, sd %.1f/%.1f mW, "
                            "net median %.3f W, net mean %.4f W\n",
                    name, k, INA_ATTEMPTS, pre.robust, post.robust, h1.robust, h2.robust,
                    net_rob, dev * 1e3, pre.sd * 1e3, run.sd * 1e3, net_med, net_mean);

            if ((plog = fopen(INA_LOG, "a")) != NULL) {
                if (ftell(plog) == 0) fprintf(plog, "%s\n", INA_LOG_COLUMNS);
                fprintf(plog, "%s,%d,attempt,%lu,%.6f,"
                              "%zu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                              "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                              "%zu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                              "%.1f,%.6f,%.6f,%.6f\n",
                        name, k, n, w1 - w0,
                        pre.n, pre.mean, pre.median, pre.robust, pre.sd, pre.min, pre.max,
                        post.mean, post.median, post.robust, post.sd, h1.robust, h2.robust,
                        run.n, run.mean, run.median, run.robust, run.sd, run.min, run.max,
                        dev * 1e3, net_rob, net_med, net_mean);
                fclose(plog);
            }

            if (best < 0 || dev < best) {               /* keep the cleanest attempt */
                best        = dev;
                kb          = k;
                nb          = n;
                wb          = w1 - w0;
                b_idle_rob  = idle_rob;
                b_idle_med  = idle_med;
                b_idle_mean = idle_mean;
                b_net_rob   = net_rob;
                b_net_med   = net_med;
                b_net_mean  = net_mean;
                b_pre_sd    = pre.sd;
                b_run_sd    = run.sd;
            }
            if (dev < INA_TOL_W) break;
        }
        ina260_close(&s);

        if (best >= INA_TOL_W)
            fprintf(stderr, "%s: WARNING: no attempt within %.0f mW, keeping attempt %d (%.1f mW)\n",
                    name, INA_TOL_W * 1e3, kb, best * 1e3);

        runtime = wb             / nb * INA_ITER_REF;
        energy  = b_net_rob * wb / nb * INA_ITER_REF;   /* db.yaml: robust estimator */

        fprintf(stderr, "%s: N %lu in %.3f s, idle %.4f W, dP %.4f W -> time %.6f s, "
                        "energy %.6f J per %d it (attempt %d; median-based dP %.3f W, "
                        "mean-based dP %.4f W, energy %.6f J)\n",
                name, nb, wb, b_idle_rob, b_net_rob, runtime, energy, INA_ITER_REF,
                kb, b_net_med, b_net_mean, b_net_mean * wb / nb * INA_ITER_REF);

        if ((plog = fopen(INA_LOG, "a")) != NULL) {
            /* same columns; the kept row carries the combined baseline, so the post
               and halves fields stay empty */
            fprintf(plog, "%s,%d,kept,%lu,%.6f,,%.6f,%.6f,%.6f,%.6f,,,,,,,,,,"
                          "%.6f,%.6f,%.6f,%.6f,,,%.1f,%.6f,%.6f,%.6f\n",
                    name, kb, nb, wb,
                    b_idle_mean, b_idle_med, b_idle_rob, b_pre_sd,
                    b_net_mean + b_idle_mean, b_net_med + b_idle_med,
                    b_net_rob + b_idle_rob, b_run_sd,
                    best * 1e3, b_net_rob, b_net_med, b_net_mean);
            fclose(plog);
        }
    }
#else
    {
        FILE *command;

        sprintf(exec, "likwid-perfctr -f -g ENERGY ./internalprofile %d %d",
                atoi(argv[1]), atoi(argv[2]));
        if ((command = popen(exec, "r")) != NULL) {
            while (fgets(buffer, sizeof buffer, command) != NULL)
                extract(buffer, &energy, &runtime);
            pclose(command);
        }
    }
#endif

    if ((f = fopen("db.yaml", "a")) == NULL) {
        fprintf(stderr, "profile: cannot open db.yaml\n");
        return 5;
    }
    fprintf(f, "name:   %s\n", name);
    fprintf(f, "   -slevel: %d\n", atoi(argv[1]));
    fprintf(f, "   -index: %d\n",  atoi(argv[2]));
    fprintf(f, "   -unhalted time: %f\n", runtime);
    fprintf(f, "   -energy: %f\n",  energy);
    fprintf(f, "   -function: encryption\n");
    fprintf(f, "   -type: software\n\n");
    fclose(f);

    return 0;
}
