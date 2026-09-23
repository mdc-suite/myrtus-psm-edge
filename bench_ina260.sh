#!/bin/bash
# bench_ina260.sh -- unattended characterisation of the INA260 energy measurement
# on the Kria KV260. Run it on the BOARD (host side, not inside the container):
#
#   chmod +x bench_ina260.sh
#   nohup ./bench_ina260.sh 3 > /dev/null 2>&1 &      # 3 hours (default 3)
#   nohup ./bench_ina260.sh 15m 2 > /dev/null 2>&1 &  # 15 minutes, a walk every 2 cycles
#
# It survives the SSH session being closed. Results, in ~/ina260_bench/ :
#   ina260_bench_<date>.csv   one row per measurement (machine readable)
#   ina260_bench_<date>.txt   environment + log + summary, rewritten every cycle
# so both are readable at any time, not only when the run ends. Ctrl-C (or
# kill) writes the final summary before exiting.
#
# What it repeats, for the whole duration:
#   idle : board power at rest, sampled slowly           -> baseline drift
#   ref  : a plain spin loop on one core (ina260_test)   -> reference load, tells
#          common-mode board drift apart from per-backend differences
#   meas : all 8 backends round-robin, one measurement each, with board
#          temperatures and fan pwm                      -> spread and drift
#   walk : container restart + full registration walk, every WALK_EVERY cycles
#          -> walk-to-walk spread of db.yaml, which is what drives selection
#
# Re-run the summary alone on an existing csv:
#   ./bench_ina260.sh --summary ina260_bench_<date>.csv

CT=Test-server                  # container name
RUN_S=3                         # workload window per measurement, s
WALK_EVERY=${2:-6}              # full walk every N cycles (2nd argument)
IDLE_SAMPLES=20                 # idle samples per cycle, 0.5 s apart
REPO_DIR="$(cd "$(dirname "$0")" && pwd)"

CSV_HDR="ts,iso,phase,cycle,backend,idle_pre_w,idle_post_w,h1_w,h2_w,dev_mw,dp_w,time50k_s,energy50k_j,iters,window_s,attempts,t_lpd_c,t_fpd_c,t_pl_c,pwm"

# ---------------------------------------------------------------- summary ----
summarize() {
  awk -F, -v now="$(date +%H:%M:%S)" '
    function sd(s, s2, c,   m) { m = s / c; m = s2 / c - m * m; return m > 0 ? sqrt(m) : 0 }
    function pearson(c, sx, sy, sxy, sxx, syy,   den) {
        den = sqrt((c * sxx - sx * sx) * (c * syy - sy * sy))
        return den > 0 ? (c * sxy - sx * sy) / den : 0
    }
    NR == 1 { next }
    { if (t0 == "") t0 = $1; t1 = $1 }
    $3 == "meas" {
        b = $5
        if (!(b in seen)) { seen[b] = 1; ord[++no] = b }
        n[b]++; dp[b] += $11; dp2[b] += $11 * $11
        if (!(b in mn) || $11 < mn[b]) mn[b] = $11
        if (!(b in mx) || $11 > mx[b]) mx[b] = $11
        e[b] += $13; e2[b] += $13 * $13
        if ($16 > 1) retry[b]++
        sx[b] += $18; sy[b] += $11; sxy[b] += $18 * $11; sxx[b] += $18 * $18; syy[b] += $11 * $11
        md[$4, b] = $11
        if ($4 > maxcyc) maxcyc = $4
        if (tmin == "" || $18 < tmin) tmin = $18
        if (tmax == "" || $18 > tmax) tmax = $18
        if (pmin == "" || $20 < pmin) pmin = $20
        if (pmax == "" || $20 > pmax) pmax = $20
    }
    $3 == "ref" {
        r_n++; r_s += $11; r_s2 += $11 * $11
        r_sx += $18; r_sy += $11; r_sxy += $18 * $11; r_sxx += $18 * $18; r_syy += $11 * $11
        ref[$4] = $11
    }
    $3 == "idle" {
        i_n++; i_s += $6; i_s2 += $6 * $6
        if (i_first == "") i_first = $6
        i_last = $6
        if (i_mn == "" || $6 < i_mn) i_mn = $6
        if (i_mx == "" || $6 > i_mx) i_mx = $6
    }
    $3 == "walk" {
        w = $4; b = $5
        if (!(w in wseen)) { wseen[w] = 1; word[++nw] = w }
        if (!(b in wb)) { wb[b] = 1; bord[++nb] = b }
        we[w, b] = $13
        wn[b]++; ws[b] += $13; ws2[b] += $13 * $13
        if (!(b in wmn) || $13 < wmn[b]) wmn[b] = $13
        if (!(b in wmx) || $13 > wmx[b]) wmx[b] = $13
    }
    END {
        printf "\n================ SUMMARY (%s) ================\n", now
        printf "span %.2f h", (t1 - t0) / 3600
        if (tmin != "") printf "   FPD temp %.1f..%.1f C   fan pwm %s..%s", tmin, tmax, pmin, pmax
        printf "\n"

        if (no > 0) {
            printf "\n-- backends, round-robin (%d cycles) --\n", maxcyc
            printf "%-12s %4s | %7s %6s %7s %7s | %11s %6s | %6s | %7s %5s\n", \
                   "backend", "n", "dP mean", "sd", "min", "max [mW]", "E mean [J]", "cv", "r(T)", "dP/ref cv", "retry"
            for (k = 1; k <= no; k++) {
                b = ord[k]; m = dp[b] / n[b]
                rc = rn = rs = rs2 = 0
                for (c = 1; c <= maxcyc; c++)
                    if ((c, b) in md && (c in ref) && ref[c] > 0) { q = md[c, b] / ref[c]; rn++; rs += q; rs2 += q * q }
                if (rn > 1) rc = 100 * sd(rs, rs2, rn) / (rs / rn)
                printf "%-12s %4d | %7.1f %6.1f %7.1f %7.1f | %11.5f %5.1f%% | %+6.2f | %6.1f%% %5d\n", \
                    b, n[b], m * 1000, sd(dp[b], dp2[b], n[b]) * 1000, mn[b] * 1000, mx[b] * 1000, \
                    e[b] / n[b], 100 * sd(e[b], e2[b], n[b]) / (e[b] / n[b]), \
                    pearson(n[b], sx[b], sy[b], sxy[b], sxx[b], syy[b]), rc, retry[b] + 0
            }
            print  "dP sd vs dP/ref cv: if the ratio to the reference is much tighter than dP itself,\nthe scatter is common-mode board drift, not per-backend noise."
        }

        if (r_n > 0) {
            printf "\n-- spin-loop reference (%d runs) --\n", r_n
            printf "dP mean %.1f mW   sd %.1f mW   r(T) %+.2f\n", \
                1000 * r_s / r_n, 1000 * sd(r_s, r_s2, r_n), pearson(r_n, r_sx, r_sy, r_sxy, r_sxx, r_syy)
        }

        if (i_n > 0) {
            printf "\n-- idle baseline (%d samples sets) --\n", i_n
            printf "mean %.4f W   sd %.1f mW   min %.4f   max %.4f   first %.4f -> last %.4f W\n", \
                i_s / i_n, 1000 * sd(i_s, i_s2, i_n), i_mn, i_mx, i_first, i_last
        }

        if (nw > 0) {
            printf "\n-- full walks (db.yaml, %d walks) --\n", nw
            printf "%-12s %4s | %11s %10s %6s | %10s %10s\n", "backend", "n", "E mean [J]", "sd", "cv", "min", "max"
            for (k = 1; k <= nb; k++) {
                b = bord[k]
                printf "%-12s %4d | %11.5f %10.5f %5.1f%% | %10.5f %10.5f\n", \
                    b, wn[b], ws[b] / wn[b], sd(ws[b], ws2[b], wn[b]), \
                    100 * sd(ws[b], ws2[b], wn[b]) / (ws[b] / wn[b]), wmn[b], wmx[b]
            }
            printf "\nordering of the close pair, per walk:\n"
            for (lv = 1; lv <= 2; lv++) {
                a = sprintf("enc_s0%d_n01", lv); c2 = sprintf("enc_s0%d_n02", lv); f = g = 0
                for (k = 1; k <= nw; k++) {
                    w = word[k]
                    if (we[w, a] == "" || we[w, c2] == "") continue
                    if (we[w, a] + 0 < we[w, c2] + 0) f++; else g++
                }
                printf "  s0%d: n01 cheaper in %d walks, n02 cheaper in %d walks\n", lv, f, g
            }
        }
        printf "==============================================\n"
    }' "$1"
}

if [ "$1" = "--summary" ]; then
    [ -f "$2" ] || { echo "usage: $0 --summary <csv>"; exit 1; }
    summarize "$2"
    exit 0
fi

# ------------------------------------------------------------------ setup ----
DURATION=${1:-3}                # "3" = 3 hours, "15m" = 15 minutes
case "$DURATION" in
    *m) SECONDS_TOTAL=$(( ${DURATION%m} * 60 )) ;;
    *)  SECONDS_TOTAL=$(( DURATION * 3600 )) ;;
esac
STAMP=$(date +%Y%m%d_%H%M)
OUT_DIR="$HOME/ina260_bench"          # kept out of the repo, so git stays clean
mkdir -p "$OUT_DIR"
CSV="$OUT_DIR/ina260_bench_$STAMP.csv"
TXT="$OUT_DIR/ina260_bench_$STAMP.txt"
HEAD="$TXT.head"
LOG="$TXT.log"

log() { echo "[$(date +%H:%M:%S)] $*" >> "$LOG"; refresh; }
refresh() { cat "$HEAD" "$LOG" > "$TXT"; summarize "$CSV" >> "$TXT" 2>/dev/null; }

hwmon_by_name() {
    for h in /sys/class/hwmon/hwmon*; do
        case "$(cat "$h/name" 2>/dev/null)" in "$1"*) echo "$h"; return;; esac
    done
}

AMS=$(hwmon_by_name ams)
FAN=$(hwmon_by_name pwmfan)
INA=$(hwmon_by_name ina260)
[ -n "$INA" ] || { echo "INA260 not found under /sys/class/hwmon"; exit 1; }
INA_P="$INA/power1_input"

read_temps() {  # t_lpd,t_fpd,t_pl,pwm
    local a b c p
    a=$(awk '{printf "%.1f", $1/1000}' "$AMS/temp1_input" 2>/dev/null)
    b=$(awk '{printf "%.1f", $1/1000}' "$AMS/temp2_input" 2>/dev/null)
    c=$(awk '{printf "%.1f", $1/1000}' "$AMS/temp3_input" 2>/dev/null)
    p=$(cat "$FAN/pwm1" 2>/dev/null)
    echo "${a:--},${b:--},${c:--},${p:--}"
}

idle_measure() {  # mean_W,sd_mW -- slow sampling, negligible load of its own
    local k v
    for k in $(seq 1 "$IDLE_SAMPLES"); do
        read -r v < "$INA_P"
        echo "$v"
        sleep 0.5
    done | awk '{s+=$1; q+=$1*$1; n++} END{m=s/n; v=q/n-m*m; printf "%.4f,%.1f", m/1e6, 0.001*(v>0?sqrt(v):0)}'
}

parse_meas() {  # profile output -> idle_pre,idle_post,h1,h2,dev,dp,time,energy,iters,window,attempts
    awk '
        /: attempt / { n++; split($5, a, "/"); split($9, b, "/"); pre=a[1]; post=a[2]; h1=b[1]; h2=b[2]; dev=$15 }
        / dP /       { iters=$3; win=$5; dp=$11; t=$15; e=$18 }
        END { if (e == "") exit 1
              printf "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s", pre, post, h1, h2, dev, dp, t, e, iters, win, n }'
}

measure() {  # $1 backend dir, $2 cycle
    local b=$1 cyc=$2 s i out row temps
    s=${b:5:2}; i=${b:9:2}
    temps=$(read_temps)
    out=$(timeout 240 docker exec "$CT" sh -c "cd /tmp/bench/$b && rm -f db.yaml && ./profile $s $i $RUN_S" 2>&1)
    row=$(printf '%s\n' "$out" | parse_meas) || { log "measure $b FAILED: $(printf '%s' "$out" | tail -2 | tr '\n' ' ')"; return 1; }
    echo "$(date +%s),$(date -Is),meas,$cyc,$b,$row,$temps" >> "$CSV"
    printf '%s\n' "$out" | grep -q WARNING && log "WARNING on $b (cycle $cyc): $(printf '%s\n' "$out" | grep WARNING)"
    return 0
}

measure_ref() {  # spin-loop reference through ina260_test
    local cyc=$1 out temps idle dp
    [ "$REF_OK" = 1 ] || return 0
    temps=$(read_temps)
    out=$(timeout 120 docker exec "$CT" /tmp/ina260_test 2>&1) || return 1
    idle=$(printf '%s\n' "$out" | awk '/^idle/{print $3}')
    dp=$(printf '%s\n'   "$out" | awk '/^delta/{print $3}')
    [ -n "$dp" ] || return 1
    echo "$(date +%s),$(date -Is),ref,$cyc,spin,$idle,-,-,-,-,$dp,-,-,-,-,1,$temps" >> "$CSV"
}

run_walk() {  # container restart -> start.sh redoes reset + the 8 registrations
    local w=$1 c t0 temps
    log "walk $w: restarting the container"
    t0=$(date +%s)
    docker restart "$CT" > /dev/null 2>&1 || { log "walk $w: restart failed"; return 1; }
    # Wait by FOLLOWING the container log, never by polling it with docker exec:
    # each docker exec spawns processes inside the container, which lands in the
    # measurement windows. Worse, such a disturbance is filtered asymmetrically by
    # profile01.c's gate (a burst inside the run is retried, one that sits in both
    # baselines is accepted), so polling biased every walk of the 22-09 run low by
    # ~45 mW. Following the log is a host-side read that costs nothing.
    if ! timeout 900 docker logs -f --since 2s "$CT" 2>&1 | grep -q -m1 "Creating Shared Library"; then
        log "walk $w: timeout waiting for the walk to finish"
        return 1
    fi
    sleep 3
    temps=$(read_temps)
    docker exec "$CT" cat /app/db.yaml | awk -v ts="$(date +%s)" -v iso="$(date -Is)" -v w="$w" -v tp="$temps" '
        /^name/    { n = $2 }
        /unhalted/ { t = $3 }
        /-energy/  { printf "%s,%s,walk,%s,%s,-,-,-,-,-,%.6f,%s,%s,-,-,-,%s\n", ts, iso, w, n, $2/t, t, $2, tp }' >> "$CSV"
    log "walk $w: done in $(( $(date +%s) - t0 )) s"
    # the walk regenerated LIB/, so refresh the per-backend binaries
    docker exec "$CT" sh -c '
      cd /tmp/bench && for o in /app/LIB/enc_*.o; do
          n=$(basename $o .o); mkdir -p $n; cp profile $n/
          gcc -o $n/internalprofile /app/internalprofile.c $o -ldl -rdynamic
      done' >> "$LOG" 2>&1
}

# ------------------------------------------------------------------ start ----
echo "$CSV_HDR" > "$CSV"
: > "$LOG"
{
  echo "INA260 benchmark -- started $(date -Is), planned duration ${DURATION} (${SECONDS_TOTAL} s), walk every ${WALK_EVERY} cycles"
  echo
  echo "== environment"
  uname -a
  echo "repo      : $REPO_DIR"
  (cd "$REPO_DIR" && git rev-parse --short HEAD 2>/dev/null)
  (cd "$REPO_DIR" && git status --porcelain 2>/dev/null | head -20)
  echo "hwmon     : ams=$AMS fan=$FAN ina260=$INA"
  echo "governor  : $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo n/a)"
  echo "cpu freq  : $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null || echo n/a)"
  echo "uptime    : $(uptime)"
  echo "container : $(docker ps --filter name=$CT --format '{{.Names}} {{.Status}} {{.Image}}' 2>/dev/null)"
  echo
  echo "== log"
} > "$HEAD"

trap 'log "interrupted"; refresh; rm -f "$HEAD" "$LOG"; exit 0' INT TERM

if ! docker ps --format '{{.Names}}' | grep -qx "$CT"; then
    log "container $CT is not running: starting it"
    (cd "$REPO_DIR" && docker compose -f compose-server.yml up -d >> "$LOG" 2>&1)
    sleep 90
fi

log "building the measurement binaries in the container"
docker exec "$CT" sh -c '
  mkdir -p /tmp/bench && cd /tmp/bench &&
  gcc -o profile /app/profile01.c -pthread -lm &&
  for o in /app/LIB/enc_*.o; do
      n=$(basename $o .o)
      mkdir -p $n && cp profile $n/ &&
      gcc -o $n/internalprofile /app/internalprofile.c $o -ldl -rdynamic || exit 1
  done' >> "$LOG" 2>&1 || { log "build FAILED, aborting"; exit 1; }

BACKENDS=$(docker exec "$CT" sh -c 'cd /tmp/bench && ls -d enc_*' | tr -d '\r')
log "backends: $(echo $BACKENDS | tr '\n' ' ')"

REF_OK=0
if docker exec "$CT" sh -c 'cd /app && gcc -O2 -pthread -o /tmp/ina260_test ina260_test.c -lm' >> "$LOG" 2>&1; then
    REF_OK=1
    log "sampler self-test: $(docker exec "$CT" /tmp/ina260_test 2>&1 | tr '\n' '|')"
else
    log "ina260_test.c not available: skipping the spin-loop reference"
fi

# ------------------------------------------------------------------- loop ----
END=$(( $(date +%s) + SECONDS_TOTAL ))
cycle=0
walk=0
log "loop running until $(date -d "@$END" +'%H:%M' 2>/dev/null || echo "+${DURATION}")"

while [ "$(date +%s)" -lt "$END" ]; do
    cycle=$((cycle + 1))

    idle=$(idle_measure)
    echo "$(date +%s),$(date -Is),idle,$cycle,-,${idle%,*},-,-,-,${idle#*,},-,-,-,-,-,-,$(read_temps)" >> "$CSV"

    measure_ref "$cycle"

    for b in $BACKENDS; do
        [ "$(date +%s)" -lt "$END" ] || break
        measure "$b" "$cycle"
    done
    log "cycle $cycle done: idle ${idle%,*} W, load$(cut -d' ' -f1-3 /proc/loadavg | sed 's/^/ /')"

    if [ $((cycle % WALK_EVERY)) -eq 0 ] && [ "$(date +%s)" -lt "$END" ]; then
        walk=$((walk + 1))
        run_walk "$walk"
    fi
done

log "finished: $cycle cycles, $walk walks"
refresh
rm -f "$HEAD" "$LOG"
echo "results: $CSV and $TXT"
