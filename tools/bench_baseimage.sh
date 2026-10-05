#!/usr/bin/env bash
# bench_baseimage.sh -- first start of the PSM container on the Kria KV260, repeated:
# stock ubuntu:22.04 against al3monni/kria-ubuntu:22.04.5 as base image (BASEIMAGE.md).
#
# Every run starts from a cold board: no repository, no images, no containers, no build
# cache, empty page cache. It clones the repository at a pinned commit, sets the base image,
# and times `docker compose up -d` while vmstat samples the whole board every second. Once
# the server is up it records the sizes, then runs test/test.sh as a functional check.
#
# The runs alternate (stock, kria, stock, ...) until each configuration has N valid runs.
# A failed run is logged as such and repeated, at most 3 attempts in a row. One row per run
# goes to <out>/results.csv, the raw logs to <out>/runs/. Stopped and started again, the
# campaign resumes from the valid runs already in the CSV, on the same commit.
#
# WARNING: every run deletes ~/myrtus-psm-edge and ALL the Docker images, containers,
# volumes and build cache on the board. Run a copy kept outside the repository.
#
#   bench_baseimage.sh [-y] [-n RUNS] [-c "stock kria"] [-o DIR] [-r COMMIT]
#   bench_baseimage.sh --summary [-o DIR]
#
#   -y         do not ask for confirmation (needed under nohup)
#   -n RUNS    valid runs per configuration (default 10)
#   -c CONFIGS configurations, in order (default "stock kria")
#   -o DIR     output directory (default ~/bench)
#   -r COMMIT  commit to pin (default: the head of main when the campaign starts)
#   --summary  mean, standard deviation, min and max of the valid runs, per configuration
#
# IDLE_S, COOLDOWN_S and the *_TIMEOUT_S variables below can be set from the environment.
#
# Needs Docker usable without sudo, and a sudoers rule for the page cache:
#   ubuntu ALL=(root) NOPASSWD: /usr/bin/tee /proc/sys/vm/drop_caches
# Start it detached from the SSH session, and follow it:
#   nohup ~/bench/bench_baseimage.sh -y > ~/bench/console.log 2>&1 &
#   tail -f ~/bench/campaign.log

set -u

REPO_URL=https://github.com/mdc-suite/myrtus-psm-edge.git
REPO_DIR=$HOME/myrtus-psm-edge
COMPOSE=compose-server.yml
CONTAINER=Test-server
IMAGE=myrtus-psm-edge-ssl-server:latest
STOCK_FROM='FROM ubuntu:22.04 AS build-env'
KRIA_FROM='FROM al3monni/kria-ubuntu:22.04.5 AS build-env'

IDLE_S=${IDLE_S:-5}                 # idle sampled before and after compose, left out of the stats
COOLDOWN_S=${COOLDOWN_S:-60}        # pause after reset and clone, before the measurement
READY_TIMEOUT_S=${READY_TIMEOUT_S:-1800}
TEST_TIMEOUT_S=${TEST_TIMEOUT_S:-1800}
NET_TIMEOUT_S=${NET_TIMEOUT_S:-1800}
MAX_ATTEMPTS=3

RUNS=10
CONFIGS="stock kria"
OUT=$HOME/bench
COMMIT=
YES=no
MODE=run

CSV_HEADER="run,started,config,base_image,commit,attempt,status,repo_kb,repo_nogit_kb,\
first_start_s,build_s,container_start_s,base_download_s,base_extract_s,apt_s,compile_s,export_s,\
ram_idle_mb,cpu_idle_pct,samples,cpu_mean_pct,cpu_peak_pct,cpu_core_s,iowait_mean_pct,iowait_peak_pct,\
ram_mean_mb,ram_mean_pct,ram_peak_mb,ram_peak_pct,base_disk_mb,base_content_mb,\
image_disk_mb,image_content_mb,container_rw_mb,container_virtual_mb,build_cache_mb,\
tests_rc,tests_pass,tests_fail"
# what --summary aggregates: every measured column
METRICS="first_start_s build_s container_start_s base_download_s base_extract_s apt_s compile_s \
export_s ram_idle_mb cpu_idle_pct samples cpu_mean_pct cpu_peak_pct cpu_core_s iowait_mean_pct iowait_peak_pct \
ram_mean_mb ram_mean_pct ram_peak_mb ram_peak_pct repo_kb repo_nogit_kb base_disk_mb \
base_content_mb image_disk_mb image_content_mb container_rw_mb container_virtual_mb \
build_cache_mb tests_pass tests_fail"

while [ $# -gt 0 ]; do
    case $1 in
        -y)        YES=yes ;;
        -n)        RUNS=${2:?-n needs a number}; shift ;;
        -c)        CONFIGS=${2:?-c needs a list}; shift ;;
        -o)        OUT=${2:?-o needs a directory}; shift ;;
        -r)        COMMIT=${2:?-r needs a commit}; shift ;;
        --summary) MODE=summary ;;
        -h|--help) sed -n '2,36s/^# \{0,1\}//p' "$0"; exit 0 ;;
        *)         echo "unknown option: $1 (see -h)" >&2; exit 2 ;;
    esac
    shift
done
CSV=$OUT/results.csv
declare -A R            # the fields of the current run, by column name
vm_pid=

log() { printf '%s  %s\n' "$(date '+%F %T')" "$*" | tee -a "$OUT/campaign.log"; }
die() { log "ABORT: $*"; exit 1; }

# --- parsers --------------------------------------------------------------------------

to_mb() {          # Docker size, SI units (512kB, 4.4MB, 7.69GB) -> MB
    awk -v s="$1" 'BEGIN {
        if (!match(s, /^[0-9.]+/)) exit
        v = substr(s, 1, RLENGTH); u = substr(s, RLENGTH + 1)
        f = u == "B" ? 1e-6 : (u == "kB" || u == "KB") ? 1e-3 : u == "MB" ? 1 : u == "GB" ? 1e3 : u == "TB" ? 1e6 : 0
        if (f) printf "%.3f", v * f }'
}

image_sizes() {    # image_sizes <`docker images` output> <name:tag> -> "<disk> <content>"
    awk -v ref="$2" '$1 == ref || $1 ":" $2 == ref {
        n = 0
        for (i = 2; i <= NF; i++) if ($i ~ /^[0-9.]+[kKMGT]?B$/) s[++n] = $i
        print s[1], s[2]; exit }' "$1"
}

build_phases() {   # BuildKit plain log -> "download extract apt compile export", seconds
    awk '
        /^#[0-9]+ / {
            id = $1; line = substr($0, length($1) + 2)
            if (!(id in hdr)) { hdr[id] = line; next }          # first line of a step: its title
            if (line ~ /^DONE [0-9.]+s$/) { done[id] = substr(line, 6) + 0; next }
            if (line ~ / done$/) {
                t = $(NF - 1); sub(/s$/, "", t)
                if (line ~ /^extracting sha256:/) ext[id] += t                          # sequential
                else if (line ~ /^sha256:[0-9a-f]+ .* \/ /) { if (t + 0 > dl[id]) dl[id] = t + 0 }  # parallel
            }
        }
        END {
            for (id in hdr) {
                h = hdr[id]
                if (h ~ /\] FROM /)                  { d += dl[id]; x += ext[id] }
                else if (h ~ /RUN DEBIAN_FRONTEND/)  a += done[id]
                else if (h ~ /RUN (gcc|make) /)      c += done[id]
                else if (h ~ /^exporting to image/)  e += done[id]
            }
            printf "%.1f %.1f %.1f %.1f %.1f\n", d, x, a, c, e
        }' "$1"
}

vmstat_stats() {   # vmstat_stats <vmstat log> <RAM MB> <cpus>
    # The statistics cover the samples of the compose window: skipped are the first line
    # (averages since boot) and the IDLE_S samples on each side. Nothing is subtracted:
    # every figure is the absolute load of the whole board. The idle samples before the
    # window are reported on their own (RAM and CPU of the idle board), for reference.
    awk -v total="$2" -v ncpu="$3" -v idle="$IDLE_S" '
        NR == 2 { for (i = 1; i <= NF; i++) c[$i] = i; next }
        $1 ~ /^[0-9]+$/ { n++; row[n] = $0 }
        function sample(k) {
            split(row[k], f, " ")
            used = total - f[c["free"]] - f[c["buff"]] - f[c["cache"]]
            cpu = f[c["us"]] + f[c["sy"]]; wa = f[c["wa"]]
        }
        END {
            for (k = 2; k <= idle + 1 && k <= n; k++) { sample(k); iu += used; ic += cpu; im++ }
            for (k = idle + 2; k <= n - idle; k++) {
                sample(k)
                su += used; sc += cpu; sw += wa; core += cpu / 100 * ncpu; m++
                if (used > pu) pu = used; if (cpu > pc) pc = cpu; if (wa > pw) pw = wa
            }
            if (m && im) printf "%.0f %.1f %d %.1f %d %.0f %.1f %d %.0f %.1f %d %.1f\n",
                iu / im, ic / im, m, sc / m, pc, core, sw / m, pw, su / m, 100 * su / m / total, pu, 100 * pu / total
        }' "$1"
}

assign() {         # assign "<column names>" "<values>"
    local -a names vals
    local i
    read -ra names <<< "$1"
    read -ra vals <<< "$2"
    for i in "${!names[@]}"; do R[${names[$i]}]=${vals[$i]:-}; done
}

emit() {           # append the current run to the CSV, in header order
    local k line=
    for k in ${CSV_HEADER//,/ }; do line+="${R[$k]:-},"; done
    echo "${line%,}" >> "$CSV"
}

valid() {          # valid runs of a configuration so far
    awk -F, -v c="$1" 'NR == 1 { for (i = 1; i <= NF; i++) h[$i] = i; next }
                       $h["config"] == c && $h["status"] == "ok"' "$CSV" | wc -l
}

# --- board ----------------------------------------------------------------------------

reset_board() {    # no repository, no images, containers, volumes or build cache
    if [ -f "$REPO_DIR/$COMPOSE" ]; then
        (cd "$REPO_DIR" && docker compose -f "$COMPOSE" down --rmi all -v) >/dev/null 2>&1
    fi
    docker rm -f "$CONTAINER" >/dev/null 2>&1
    rm -rf "$REPO_DIR"
    docker system prune -a --volumes -f >/dev/null 2>&1
    docker builder prune -a -f >/dev/null 2>&1
    [ -z "$(docker system df --format '{{.Type}} {{.TotalCount}}' | awk '$NF != 0')" ]
}

net_ok() {
    GIT_TERMINAL_PROMPT=0 git ls-remote "$REPO_URL" HEAD >/dev/null 2>&1 || return 1
    if command -v curl >/dev/null; then
        curl -s --max-time 15 -o /dev/null https://registry-1.docker.io/v2/
    else
        getent hosts registry-1.docker.io >/dev/null
    fi
}

net_wait() {       # wait for GitHub and Docker Hub, at most NET_TIMEOUT_S
    local t=0
    until net_ok; do
        [ $t -eq 0 ] && log "network down (GitHub or Docker Hub unreachable): waiting"
        [ $t -ge "$NET_TIMEOUT_S" ] && return 1
        sleep 30; t=$((t + 30))
    done
    [ $t -eq 0 ] || log "network back after $t s"
}

server_up() {      # wait for the banner's last line, "options:", as test/test.sh does
    local t=0
    while [ $t -lt "$READY_TIMEOUT_S" ]; do
        docker logs "$CONTAINER" 2>&1 | grep -q '^options:' && return 0
        [ "$(docker inspect -f '{{.State.Running}}' "$CONTAINER" 2>/dev/null)" = true ] || return 1
        sleep 10; t=$((t + 10))
    done
    return 1
}

# --- one run --------------------------------------------------------------------------

run_once() {       # run_once <config> <run> <attempt>: one cold first start, one CSV row
    local cfg=$1 run=$2 from base dir rc t0 t1 tc created sz
    case $cfg in
        stock) from=$STOCK_FROM ;;
        kria)  from=$KRIA_FROM ;;
        *)     die "unknown configuration: $cfg" ;;
    esac
    base=${from#FROM }; base=${base% AS *}
    dir=$OUT/runs/$(printf '%02d' "$run")_$cfg
    mkdir -p "$dir"
    R=([run]=$run [started]="$(date '+%F %T')" [config]=$cfg [base_image]=$base
       [commit]=${COMMIT:0:7} [attempt]=$3 [status]=ok)

    log "run $run: $cfg, attempt $3 -- reset"
    reset_board || die "the board is not clean after the reset: see docker system df"
    net_wait || die "network unreachable for $NET_TIMEOUT_S s: fix it, then start again to resume"

    log "run $run: clone at ${COMMIT:0:7}, base image $base"
    if ! { GIT_TERMINAL_PROMPT=0 git clone -q "$REPO_URL" "$REPO_DIR" &&
           git -C "$REPO_DIR" checkout -q "$COMMIT"; } > "$dir/clone.log" 2>&1; then
        R[status]=fail_clone; emit; return 1
    fi
    R[repo_kb]=$(du -sk "$REPO_DIR" | cut -f1)
    R[repo_nogit_kb]=$(du -sk --exclude=.git "$REPO_DIR" | cut -f1)
    grep -qxF "$STOCK_FROM" "$REPO_DIR/Dockerfile" ||
        die "the Dockerfile at ${COMMIT:0:7} has no line '$STOCK_FROM'"
    if [ "$cfg" = kria ]; then
        sed -i "s|^$STOCK_FROM\$|$KRIA_FROM|" "$REPO_DIR/Dockerfile"
        grep -qxF "$KRIA_FROM" "$REPO_DIR/Dockerfile" || die "could not set the Kria base image"
    fi
    git -C "$REPO_DIR" diff > "$dir/dockerfile.diff"

    # --- the measurement: from the launch of compose to its return
    log "run $run: cooldown $COOLDOWN_S s, then first start"
    sleep "$COOLDOWN_S"
    sync; echo 3 | sudo -n tee /proc/sys/vm/drop_caches >/dev/null
    vmstat -n -t -S M 1 > "$dir/vmstat.log" &
    vm_pid=$!
    sleep "$IDLE_S"
    t0=$(date +%s.%N)
    (cd "$REPO_DIR" && COMPOSE_PROGRESS=plain BUILDKIT_PROGRESS=plain \
        docker compose -f "$COMPOSE" up -d) > "$dir/compose.log" 2>&1
    rc=$?
    t1=$(date +%s.%N)
    sleep "$IDLE_S"
    kill "$vm_pid"; wait "$vm_pid" 2>/dev/null; vm_pid=

    R[first_start_s]=$(awk -v a="$t0" -v b="$t1" 'BEGIN { printf "%.1f", b - a }')
    created=$(docker inspect -f '{{.Created}}' "$CONTAINER" 2>/dev/null)
    if [ -n "$created" ]; then              # build | container start, split at its creation
        tc=$(date -d "$created" +%s.%N)
        R[build_s]=$(awk -v a="$t0" -v b="$tc" 'BEGIN { printf "%.1f", b - a }')
        R[container_start_s]=$(awk -v a="$tc" -v b="$t1" 'BEGIN { printf "%.1f", b - a }')
    fi
    assign "base_download_s base_extract_s apt_s compile_s export_s" "$(build_phases "$dir/compose.log")"
    assign "ram_idle_mb cpu_idle_pct samples cpu_mean_pct cpu_peak_pct cpu_core_s iowait_mean_pct iowait_peak_pct \
            ram_mean_mb ram_mean_pct ram_peak_mb ram_peak_pct" \
           "$(vmstat_stats "$dir/vmstat.log" "$MEM_MB" "$NCPU")"
    log "run $run: first start ${R[first_start_s]} s, compose exit $rc"
    if [ $rc -ne 0 ]; then R[status]=fail_compose; emit; return 1; fi

    # --- sizes, once the server is up (the backends are measured in silence before that)
    if ! server_up; then
        docker logs "$CONTAINER" > "$dir/server.log" 2>&1
        R[status]=fail_server; emit; return 1
    fi
    sz=$(docker ps -s --filter "name=^$CONTAINER\$" --format '{{.Size}}')    # 4.4MB (virtual 5.53GB)
    echo "$sz" > "$dir/container_size.txt"
    R[container_rw_mb]=$(to_mb "${sz%% *}")
    sz=${sz##*virtual }; R[container_virtual_mb]=$(to_mb "${sz%)}")
    docker images > "$dir/images.txt" 2>&1
    assign "image_disk_mb image_content_mb" "$(image_sizes "$dir/images.txt" "$IMAGE")"
    docker system df > "$dir/system_df.txt" 2>&1
    R[build_cache_mb]=$(docker system df --format '{{.Type}}|{{.Size}}' | awk -F'|' '$1 == "Build Cache" { print $2 }')
    # after a build the base image is not listed: pull it, its content is already local
    docker pull -q "$base" > "$dir/pull_base.log" 2>&1
    docker images > "$dir/images_base.txt" 2>&1
    assign "base_disk_mb base_content_mb" "$(image_sizes "$dir/images_base.txt" "$base")"
    for k in image_disk_mb image_content_mb build_cache_mb base_disk_mb base_content_mb; do
        R[$k]=$(to_mb "${R[$k]}")
    done

    # --- functional check
    log "run $run: server up, running test/test.sh"
    (cd "$REPO_DIR" && timeout "$TEST_TIMEOUT_S" test/test.sh) > "$dir/test.log" 2>&1
    R[tests_rc]=$?
    R[tests_pass]=$(grep -cx ' *PASS' "$dir/test.log")
    R[tests_fail]=$(grep -cx ' *FAIL' "$dir/test.log")
    docker logs "$CONTAINER" > "$dir/server.log" 2>&1
    [ "${R[tests_rc]}" -eq 0 ] || R[status]=fail_tests
    emit
    log "run $run: tests ${R[tests_pass]} passed, ${R[tests_fail]} failed -- ${R[status]}"
    [ "${R[status]}" = ok ]
}

# --- summary --------------------------------------------------------------------------

summary() {
    [ -f "$CSV" ] || { echo "no results in $CSV" >&2; exit 1; }
    awk -F, -v metrics="$METRICS" -v out="$OUT/summary.csv" '
        NR == 1 { for (i = 1; i <= NF; i++) h[$i] = i; nm = split(metrics, M, " "); next }
        $h["status"] != "ok" { failed++; next }
        {
            cf = $h["config"]; if (!(cf in runs)) order[++nc] = cf; runs[cf]++
            for (j = 1; j <= nm; j++) {
                v = $h[M[j]]; if (v == "") continue
                k = cf SUBSEP M[j]; n[k]++; s[k] += v; q[k] += v * v
                if (!(k in lo) || v + 0 < lo[k]) lo[k] = v + 0
                if (!(k in hi) || v + 0 > hi[k]) hi[k] = v + 0
            }
        }
        END {
            printf "valid runs:"; for (i = 1; i <= nc; i++) printf " %s %d", order[i], runs[order[i]]
            printf "; failed attempts: %d\n\n%-20s", failed + 0, "metric"
            for (i = 1; i <= nc; i++) printf "  %-34s", order[i] ": mean ± sd [min, max]"
            print ""
            print "config,metric,n,mean,sd,min,max" > out
            for (j = 1; j <= nm; j++) {
                printf "%-20s", M[j]
                for (i = 1; i <= nc; i++) {
                    k = order[i] SUBSEP M[j]
                    if (!n[k]) { printf "  %-34s", "-"; continue }
                    mean = s[k] / n[k]
                    var = n[k] > 1 ? (q[k] - s[k] * s[k] / n[k]) / (n[k] - 1) : 0
                    sd = var > 0 ? sqrt(var) : 0
                    printf "  %-34s", sprintf("%.1f ± %.1f [%.1f, %.1f]", mean, sd, lo[k], hi[k])
                    printf "%s,%s,%d,%.3f,%.3f,%.3f,%.3f\n", order[i], M[j], n[k], mean, sd, lo[k], hi[k] > out
                }
                print ""
            }
        }' "$CSV" | tee "$OUT/summary.txt"
    echo "(also in $OUT/summary.csv)"
}

# --- main -----------------------------------------------------------------------------

mkdir -p "$OUT"
if [ $MODE = summary ]; then summary; exit 0; fi

exec 9> "$OUT/.lock"
flock -n 9 || { echo "a campaign is already running on $OUT" >&2; exit 1; }
trap '[ -n "$vm_pid" ] && kill "$vm_pid" 2>/dev/null' EXIT

if [ $YES = no ]; then
    printf 'Every run deletes %s and ALL the Docker images, containers, volumes and build cache on this board. Continue? [y/N] ' "$REPO_DIR"
    read -r a
    [ "$a" = y ] || exit 1
fi

for c in git docker vmstat flock timeout du; do
    command -v $c >/dev/null || die "missing command: $c"
done
docker version >/dev/null 2>&1 || die "cannot reach Docker as $(id -un): is this user in the docker group?"
echo 3 | sudo -n tee /proc/sys/vm/drop_caches >/dev/null 2>&1 ||
    die "sudo asks a password for drop_caches: add the sudoers rule (see -h)"
MEM_MB=$(awk '/^MemTotal:/ { print int($2 / 1024) }' /proc/meminfo)
NCPU=$(nproc)

if [ -f "$OUT/commit" ]; then        # a campaign to resume: same commit
    pinned=$(cat "$OUT/commit")
    [ -z "$COMMIT" ] || [ "$COMMIT" = "$pinned" ] || die "$OUT is pinned to $pinned, not $COMMIT"
    COMMIT=$pinned
else
    if [ -z "$COMMIT" ]; then
        net_wait || die "network unreachable: cannot resolve the head of main"
        COMMIT=$(GIT_TERMINAL_PROMPT=0 git ls-remote "$REPO_URL" refs/heads/main | cut -f1)
    fi
    [ -n "$COMMIT" ] || die "cannot resolve the commit to pin"
    echo "$COMMIT" > "$OUT/commit"
fi
if [ -f "$CSV" ]; then
    [ "$(head -1 "$CSV")" = "$CSV_HEADER" ] || die "$CSV has other columns: use another -o"
else
    echo "$CSV_HEADER" > "$CSV"
fi

log "campaign: $RUNS valid runs each of $CONFIGS, commit ${COMMIT:0:7}, ${NCPU} CPUs, ${MEM_MB} MB RAM"
fails=0
while :; do
    next=
    for cfg in $CONFIGS; do              # the configuration with the fewest valid runs, in order
        done_n=$(valid "$cfg")
        [ "$done_n" -lt "$RUNS" ] || continue
        if [ -z "$next" ] || [ "$done_n" -lt "$least" ]; then next=$cfg; least=$done_n; fi
    done
    [ -n "$next" ] || break
    run=$(wc -l < "$CSV")                # header + rows so far = number of the next run
    if run_once "$next" "$run" $((fails + 1)); then
        fails=0
    else
        fails=$((fails + 1))
        [ $fails -lt $MAX_ATTEMPTS ] || die "$next failed $MAX_ATTEMPTS times in a row: see $OUT/runs"
        log "run $run failed (${R[status]}): repeating it"
    fi
done

log "campaign complete: final reset"
reset_board || log "the board is not clean after the final reset: see docker system df"
summary
