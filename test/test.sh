#!/bin/sh
# test/test.sh -- check a running container against the expected results.
#
# Run on the target, from the repository, once the container has finished its start
# pipeline (docker logs Test-server shows the OpenSSL banner):
#
#   test/test.sh          build checks, then a round trip of rfile on both security levels
#   test/test.sh -rapid   build checks only
#   test/test.sh -all     everything: also a runtime switch, and files of 18 sizes on both
#                         levels, through the backends and through OpenSSL's GCM (mode 0)
#
# Each test prints the command it runs in the container (in /app), then the expected and
# the obtained result. Exit status: 0 all passed, 1 some failed, 2 the tests could not run.
# -all leaves both ports on their initial backends (5544: mode 98, 5545: mode 82).

C=Test-server
SIZES="0 1 15 16 17 1000 1007 1008 1009 1023 1024 1025 2031 2032 2033 3000 10000 1048576"
# wait until the server has written and closed the file it received (at most 60 s)
WAIT="timeout 60 sh -c 'until [ -e Downloads/* ] && ! lsof Downloads/* >/dev/null 2>&1; do sleep 0.1; done'"

case ${1:-} in
    "")        LEVEL=default; N=8 ;;
    -rapid)    LEVEL=rapid;   N=6 ;;
    -all)      LEVEL=all;     N=13 ;;
    -h|--help) sed -n '2,14s/^# \{0,1\}//p' "$0"; exit 0 ;;
    *)         echo "usage: $0 [-rapid | -all]" >&2; exit 2 ;;
esac

if [ -t 1 ]; then OK='\033[1;32m'; KO='\033[1;31m'; B='\033[1m'; X='\033[0m'; else OK=; KO=; B=; X=; fi
pass=0
fail=0
i=0

in_c()   { docker exec "$C" sh -c "cd /app && $1" 2>&1; }      # run a command in the container
logs()   { docker logs "$C" 2>&1 | tr -d '\r'; }                # the server's output
count()  { logs | grep -c "$1"; }
join()   { awk 'NR > 1 { printf ", " } { printf "%s", $0 } END { print "" }'; }
title()  { i=$((i + 1)); echo; printf "${B}[%d/%d] %s${X}\n" "$i" "$N" "$1"; }
line()   { printf "      %-9s %s\n" "$1" "$2"; }
verdict() {                                                     # verdict <expected> <obtained>
    line expected: "$1"
    line obtained: "$2"
    if [ "$1" = "$2" ]; then
        pass=$((pass + 1)); printf "      ${OK}PASS${X}\n"
    else
        fail=$((fail + 1)); printf "      ${KO}FAIL${X}\n"
    fi
}
check() {                                                       # check <title> <command> <expected>
    title "$1"
    line command: "$2"
    verdict "$3" "$(in_c "$2" | join)"
}
settle() {                       # settle <"File saved" count before> <transfers>: let the log catch up
    t=0
    while [ "$(count 'File saved')" -lt $(($1 + $2)) ] && [ $t -lt 50 ]; do sleep 0.2; t=$((t + 1)); done
}

# transfer <-s> <port>: send rfile, wait for the server, compare. Sets OUT, TAG and BACKEND.
transfer() {
    cmd="rm -f Downloads/*; ./client -s $1 -i 127.0.0.1:$2 -f rfile | tail -1; $WAIT; cmp -s rfile Downloads/* && echo identical || echo different"
    line command: "$cmd"
    line log: "docker logs $C | grep -E 'Starting with|From openssl|TAG MISMATCH' | tail -2"
    mm=$(count 'TAG MISMATCH')
    sv=$(count 'File saved')
    OUT=$(in_c "$cmd" | join)
    settle "$sv" 1
    if [ "$(count 'TAG MISMATCH')" -eq "$mm" ]; then TAG="tag verified"; else TAG="TAG MISMATCH"; fi
    BACKEND=$(logs | grep -o 'Starting with enc_s[0-9]*_n[0-9]*\|From openssl native GCM' | tail -1 |
              sed -e 's/Starting with //' -e 's/From openssl native GCM/OpenSSL GCM/')
}

# sizes <-s> <port> <native: 0|1>: every file of $SIZES on one level
sizes() {
    cmd="for n in $SIZES; do rm -f Downloads/*; ./client -s $1 -i 127.0.0.1:$2 -f /tmp/sizes/\$n >/dev/null 2>&1; $WAIT; cmp -s /tmp/sizes/\$n Downloads/* || printf '%s ' \$n; done"
    line command: "$cmd"
    line log: "docker logs $C | grep -c 'TAG MISMATCH'"
    mm=$(count 'TAG MISMATCH')
    sv=$(count 'File saved')
    nat=$(count 'openssl native')
    bad=$(in_c "$cmd" | xargs)
    settle "$sv" 18
    nbad=$(echo $bad | wc -w)
    got="$((18 - nbad)) of 18 identical"
    [ -n "$bad" ] && got="$got (failed at $bad bytes)"
    mm=$(($(count 'TAG MISMATCH') - mm))
    if [ $mm -eq 0 ]; then got="$got, tag verified on all"; else got="$got, $mm TAG MISMATCH"; fi
    want="18 of 18 identical, tag verified on all"
    if [ "$3" = 1 ]; then
        got="$got, $(($(count 'openssl native') - nat)) decrypted by OpenSSL"
        want="$want, 18 decrypted by OpenSSL"
    fi
    verdict "$want" "$got"
}

# --- can the tests run at all?
if ! docker version >/dev/null 2>&1; then
    echo "cannot reach Docker: is it running, and is this user in the docker group?" >&2
    exit 2
fi
if [ "$(docker inspect -f '{{.State.Running}}' "$C" 2>/dev/null)" != true ]; then
    echo "container $C is not running: start it with docker compose -f compose-server.yml up -d --build" >&2
    exit 2
fi
if ! in_c "lsof -i -P -n | grep -q ':5545 (LISTEN)'"; then
    echo "the server is not listening yet: the container is still registering the backends" >&2
    echo "(several minutes on the board). Wait for the OpenSSL banner in docker logs -f $C." >&2
    exit 2
fi

case $(uname -m) in
    aarch64|arm64) MACHINE=AArch64 ;;
    x86_64)        MACHINE="Advanced Micro Devices X86-64" ;;
    *)             MACHINE=$(uname -m) ;;
esac

printf "${B}myrtus-psm-edge tests -- level: %s, %d tests, container %s${X}\n" "$LEVEL" "$N" "$C"
echo "Commands run in the container, in /app. To repeat one by hand: docker exec -it $C sh, then cd /app."

# --- build
check "Binaries built for this host" \
      "readelf -h server | grep Machine | sed 's/.*: *//'" \
      "$MACHINE"
check "Backends and shared library in LIB/" \
      "ls LIB | xargs" \
      "enc_s01_n01.o enc_s01_n02.o enc_s01_n03.o enc_s01_n04.o enc_s02_n01.o enc_s02_n02.o enc_s02_n03.o enc_s02_n04.o lib_enc.so"
check "Symbols exported by lib_enc.so" \
      "nm -D LIB/lib_enc.so | awk '\$2 == \"T\" && \$3 ~ /^enc_s/ { print \$3 }' | xargs" \
      "enc_s01_n01 enc_s01_n02 enc_s01_n03 enc_s01_n04 enc_s02_n01 enc_s02_n02 enc_s02_n03 enc_s02_n04"
check "Backends per security level in header.h" \
      "grep '^///' header.h | xargs" \
      "///2-04 ///1-04 ///0-00"
check "Backends measured in db.yaml" \
      'echo $(grep -c "^name" db.yaml) entries, $(grep -c "energy: 0.000000" db.yaml) with zero energy' \
      "8 entries, 0 with zero energy"
check "Server listening on both ports" \
      "lsof -i -P -n | awk '\$1 == \"server\" && /LISTEN/ { print \$9 }' | sort -u | xargs" \
      "*:5544 *:5545"

# --- round trip
if [ $LEVEL != rapid ]; then
    for p in "1 5544 high AES-256" "0 5545 low AES-128"; do
        set -- $p
        title "Round trip, $3 level ($4, port $2): rfile, 10000 bytes"
        transfer "$1" "$2"
        line backend: "$BACKEND"
        verdict "Entire File Sent 10016 bytes, identical, tag verified" "$OUT, $TAG"
    done
fi

# --- runtime switch and file sizes
if [ $LEVEL = all ]; then
    title "Runtime switch: fastest and cheapest AES-128 backend, then a round trip on it"
    cmd="./synthesize -f e -s 1 -t 0 -e 0 | grep -o 'enc_s01_n[0-9]*' | head -1"
    line command: "$cmd"
    chosen=$(in_c "$cmd")
    sleep 0.5
    switched=$(logs | grep -o 'Switching to enc_s[0-9]*_n[0-9]*' | tail -1 | sed 's/Switching to //')
    transfer 0 5545
    line restore: "./send 5545 82"
    in_c "./send 5545 82" >/dev/null
    sleep 0.5
    verdict "chose enc_s01_n04, switched to enc_s01_n04, decrypted by enc_s01_n04, Entire File Sent 10016 bytes, identical, tag verified" \
            "chose $chosen, switched to $switched, decrypted by $BACKEND, $OUT, $TAG"

    setup="mkdir -p /tmp/sizes && for n in $SIZES; do head -c \$n /dev/urandom > /tmp/sizes/\$n; done"
    in_c "$setup" >/dev/null

    title "File sizes, registered backends, high level (port 5544)"
    line setup: "$setup"
    sizes 1 5544 0
    title "File sizes, registered backends, low level (port 5545)"
    sizes 0 5545 0

    in_c "./send 5544 0 >/dev/null; ./send 5545 0 >/dev/null"
    sleep 0.5
    title "File sizes, OpenSSL GCM (mode 0), high level (port 5544)"
    line setup: "./send 5544 0; ./send 5545 0"
    sizes 1 5544 1
    title "File sizes, OpenSSL GCM (mode 0), low level (port 5545)"
    sizes 0 5545 1
    line restore: "./send 5544 98; ./send 5545 82"
    in_c "./send 5544 98 >/dev/null; ./send 5545 82 >/dev/null; rm -rf /tmp/sizes"
fi
in_c "rm -f Downloads/*"

echo
if [ $fail -eq 0 ]; then
    printf "${OK}ALL %d TESTS PASSED${X}\n" "$N"
    exit 0
else
    printf "${KO}%d OF %d TESTS FAILED${X}\n" "$fail" "$N"
    exit 1
fi
