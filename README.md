# myrtus-psm-edge — Privacy and Security Manager (Edge Layer)

Crypto-agile TLS prototype with eight runtime-selectable AES backends, benchmarked for performance and energy on ARM64 edge hardware. Component of the **MYRTUS** project (Horizon Europe, Grant No. 101135183).

**Repo:** https://github.com/mdc-suite/myrtus-psm-edge
**Upstream:** https://github.com/subhadeep-banik/spdocker
**Port branch:** `al3monni-test-arm` · **x86 baseline:** `al3monni-test` (`ddb5f5f`)
**Target hardware:** AMD/Xilinx Kria KR260 — Zynq UltraScale+ MPSoC, 4× Cortex-A53, aarch64
**Cross-build host:** Windows + WSL2 (Ubuntu) + Docker Desktop, `linux/arm64` under QEMU
**On-board host:** Ubuntu 22.04 IoT, Docker Engine, native aarch64 build
**Base image:** [`al3monni/kria-ubuntu:22.04.5`](https://hub.docker.com/r/al3monni/kria-ubuntu) (arm64/v8) — see §11
**Status:** ✅ **Validated on real silicon.** Builds, registers all 8 implementations,
serves over TLS, and round-trips byte-identical (`cmp`) on the Kria KR260.
⚠️ Energy profiling disabled — see §9.

> Derived from [spdocker](https://github.com/subhadeep-banik/spdocker) by Subhadeep Banik.
> This document reconstructs **every** modification required to take the project from the
> upstream x86-only state to a working, hardware-validated aarch64 deployment. Changes needed
> to build on x86 are marked **[baseline]**; ARM-specific changes **[arm]**; changes that
> emerged only on real hardware **[hw]**.
> Board bring-up (flashing, networking, Docker install) is documented separately in
> [`KRIA_KV260_DEPLOYMENT.md`](KRIA_KV260_DEPLOYMENT.md).

---

## 1. What this project is

A TLS client/server shipping **8 interchangeable AES implementations**. At runtime the server selects one, loads it from a shared library via `dlopen`/`dlsym`, and uses it as the AEAD for file transfer. Selection is nominally driven by measured throughput/energy against a policy file.

Container-start pipeline (`start.sh`):

```
reset  →  register ×8  →  gcc -shared ./LIB/*.o -o ./LIB/lib_enc.so  →  ./server
```

- `reset` — clears `LIB/`, resets `header.h` counters
- `register -c ./fN/config.txt` — compiles fN, runs a known-answer test (KAT), renames its internal symbols, drops `enc_sXX_nYY.o` into `LIB/`
- `server` — `dlopen("./LIB/lib_enc.so")`, then `dlsym("enc_s%02d_n%02d")` per the mode byte

The **registration order is the numbering contract** — client and server must agree on it, or the same mode byte selects different ciphers on each side (see §8).


Both the x86-64 baseline and the aarch64 port are validated end-to-end by a byte-identical `cmp` on a 10000-byte file transfer; the aarch64 path has been re-validated on the Kria KR260 itself, not only under emulation.

---

## 2. The two failure classes this port had to solve

**(a) Upstream out-of-the-box failure [baseline].** A fresh clone fails on *any* architecture because `check1.c` / `check2.c` (the KAT templates `register.c` reads) are missing from the repo. `register.c` truncates `test%d.c` via `fopen(...,"wb")` *before* checking the template exists, so the test binary ends up empty → `undefined reference to 'main'` → `TEST FAILED` → `LIB/` never populated → `gcc -shared ./LIB/*.o` matches nothing. `start.sh` hides all of it (`> /dev/null 2>&1` per register, unconditional `echo`). Fixed by reconstructing both templates (§3, M-B1/M-B2).

**(b) Architecture faults [arm].** Once it builds on x86, the aarch64 cross-build exposes: likwid compiling its x86 access layer; four copies of x86 `rdtscp` inline asm; a vestigial `-l_enc` link against a committed x86 `.so`; the likwid profiler hanging under QEMU and corrupting the manifest; and f7/f8 using x86 AES-NI intrinsics and flags. All addressed in §4, and subsequently confirmed on native aarch64 hardware — none of the fixes were artefacts of emulation.

A third class — **energy instrumentation** — turned out not to be a port bug at all but an
architectural dead end, and is treated separately in §9.

---

## 3. Baseline modifications (needed even on x86)

### M-B1 — `check1.c` [baseline, mandatory]
AES-128 KAT template, security level 1 (used by f1, f2, f3, f7). The marker line must be **exactly 4 spaces + `// insert_func here`** — `register.c` matches `strncmp(string,"    // insert_func here",23)`. Expected ciphertext vector baked into `compare()`.

### M-B2 — `check2.c` [baseline, mandatory]
Same, security level 2 (f4, f5, f6, f8). AES-256-ECB vector from NIST SP 800-38A. Identical structure to M-B1 with the 32-byte key and the AES-256 expected ciphertext.

### M-B3 — `compose-server.yml` path & X11 [baseline, mandatory]
Replace the author's hardcoded `build: /home/usi/scke/unified/` with `build: .`; delete the `volumes:` X11 mounts (`/tmp/.X11-unix`, `${HOME}/.Xauthority`) and the `DISPLAY` environment line — none exist under WSL2.

### M-B4 — `.dockerignore` [baseline]
Added to keep build context small and avoid copying host cruft into the image.

---

## 4. ARM-specific modifications

Each is a single, independently-validated change, in the order they surfaced during the cross-build. Filenames are relative to repo root. All were re-verified on the Kria board after the cross-build path was complete.

### M-A1 — likwid: build the ARMv8 target [arm] · `Dockerfile`
**Symptom:** the likwid layer fails compiling `access_x86_*.o` — its `make` defaults to `COMPILER = GCC`, which means *GCC-on-x86*; likwid couples compiler and architecture in one setting.
**Fix:** gate a `sed` on the arm64 build that switches likwid's `config.mk`:

```dockerfile
ARG TARGETARCH
RUN tar -xaf likwid-5.5.1.tar.gz \
 && cd likwid-5.5.1 \
 && if [ "$TARGETARCH" = "arm64" ]; then \
      sed -i -e 's|^COMPILER *=.*|COMPILER = GCCARMv8#NO SPACE|' \
             -e 's|^ACCESSMODE *=.*|ACCESSMODE = perf_event#NO SPACE|' \
             config.mk \
      && grep -E '^(COMPILER|ACCESSMODE)' config.mk ; \
    fi \
 && make && make install
```

- `GCCARMv8` makes likwid's top-level Makefile `filter-out` the x86 MSR/rdpmc objects and build `./GCCARMv8/` instead of `./GCC/`.
- `ACCESSMODE = perf_event` avoids the MSR access daemon (x86-only; MSRs are unavailable on both WSL2 and Kria). On the **real Cortex-A53 PMU** `perf_event` is also the correct mechanism to read cycles/instructions/cache counters.
- Rationale for patching rather than stubbing likwid: keeps `#include <likwid.h>` /
  `-llikwid` resolving and keeps the diff against the x86 baseline honest.

> **Scope of what this buys.** `likwid-perfctr` (performance counters) works on ARMv8 with
> this configuration. `likwid-powermeter` does **not**, on any ARM part — see §9.

### M-A2 — Dockerfile layer reorder (cache) [arm] · `Dockerfile` · commit `d01abde`
Moved the likwid `wget` + build block **above** `COPY . .`. The likwid layer is ~800 s under QEMU; before the reorder, any source edit busted it and every cycle paid the full cost. After: likwid is a stable early layer, and source edits resume from `COPY . .` (~seconds). This single reorder is what makes the edit/build loop tolerable.

On the board the same reorder still pays off, though less dramatically — the native A53 build of likwid is far faster than the emulated one, but it is still the longest layer and still worth keeping out of the edit loop.

### M-A3 — portable cycle counter [arm] · new file `cycles.h`
**Symptom:** four files define `rdtscp()` with x86 inline asm (`"=a"/"=d"/"c"` constraints) that aarch64 gcc cannot satisfy.
**Fix:** one portable header, architecture-branched, same name/signature so **no call site changes**:

```c
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
/* ARM generic timer. Counts at cntfrq_el0 (fixed, ~33 MHz on Zynq
   UltraScale+), NOT the CPU clock like x86 TSC — ticks are not
   directly comparable to x86 ticks. See cycle_freq(). */
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
```

Design notes: the x86 branch is byte-identical to the old code, so the x86 build is unaffected; `static inline` (not `extern inline`) avoids duplicate-symbol errors at link when `make server` compiles multiple TUs into one binary; the `isb` restores the partial serialization that x86's `rdtscp` provides and a bare `mrs` does not.

**Unit caveat — confirmed on hardware.** `cntvct_el0` counts generic-timer ticks at a fixed frequency, not CPU cycles. This is true under QEMU and equally true on real silicon: the counter does not track the A53 clock, does not scale with DVFS, and its ticks are roughly two orders of magnitude coarser than an x86 TSC tick. Any downstream code that assumes CPU-clock ticks will silently read wrong magnitudes. Harmless today because the profiling chain is inert (§9), but `cycle_freq()` is provided so the energy-layer rework has the conversion factor available.

### M-A4 — remove local rdtscp definitions [arm] · `server_f.c`, `profile.c`, `profile01.c`, `internalprofile.c`
Delete the six-line x86 `rdtscp` definition from each file and add `#include "cycles.h"` with the other includes. (`internalprofile.c` has the definition but no calls — remove it anyway for consistency.) Verify:

```bash
grep -rn '__asm__ volatile("rdtscp"' --include=*.c .   # must print nothing
grep -n  "cycles.h" server_f.c profile.c profile01.c internalprofile.c  # 4 lines
```

> **M-A6 (folded in).** Commit `587d1e8` fixes a typo introduced by this change: the four `#include "cycles.h"` lines were annotated with a shell-style `# al3monni mod` comment instead of C-style `// al3monni mod`, which the preprocessor rejects. It is recorded here rather than as a separate modification because it is a correction to M-A4, not an independent change — hence the gap between M-A5 and M-A7.

### M-A5 — drop vestigial `-l_enc` link flag [arm] · `Makefile`, `makeclient`
**Symptom:** `ld: skipping incompatible ./LIB//lib_enc.so when searching for -l_enc` → `cannot find -l_enc`. The committed `lib_enc.so` is x86-64; on aarch64 `ld` skips it, and an unresolved `-l` is a hard error.
**Fix:** remove only `-L$(LIBF) -l_enc` from `CFLAGS` in both files; **keep `-ldl`** (that's `dlopen`) and **keep `-rdynamic`** (exports the executable's symbols so the `RTLD_GLOBAL`-loaded library resolves back into it).

```make
# before
CFLAGS = -w -Wno-incompatible-pointer-types -lcrypto -lssl  -L$(LIBF) -l_enc -ldl -rdynamic
# after
CFLAGS = -w -Wno-incompatible-pointer-types -lcrypto -lssl -ldl -rdynamic
```

The symbols were never referenced at link time (everything goes through `dlsym`), so this produces a byte-identical binary on x86 while fixing the aarch64 link. `start.sh` regenerates `lib_enc.so` at runtime regardless.

### M-A7 — skip likwid profiling on aarch64 [arm] · `gen.c`
**Symptom (cross-build):** on aarch64 the profiler prints `Unsupported ARMv8 Processor` / `Cannot read performance group ENERGY` and, under QEMU, the `./profile` execution **hangs** inside likwid's `popen`. Worse: in `gen.c` the `header.h` manifest update (counter bump + new prototype) sits inside `if(!rt)`, while `rm header.h; mv header1.h header.h` runs **unconditionally** — so a failed or interrupted profiler run wipes the manifest and destroys the registration state.

**Fix:** on aarch64, skip the profiler run and force success so the manifest update still executes:

```c
   sprintf(com1, "./profile %d %d %d", slevel, h, cpu);
#if defined(__aarch64__)
   rt = 0;   // skip likwid profiling on aarch64: hangs under QEMU, and the
             // ENERGY group is unavailable on ARM regardless (see §9).
             // Force success so header.h still updates.
#else
   rt = system(com1);
#endif
```

The profiler binaries are still *built* (harmless); only their *execution* is skipped.

**Why the guard stays on real hardware.** The QEMU hang was the symptom that forced this change, but it was not the whole cause, and the fix is not an emulation workaround that the board makes obsolete. likwid's `ENERGY` performance group is built on Intel/AMD RAPL MSRs; the Cortex-A53 has no equivalent register interface, so the group cannot be read on any ARM part, emulated or not. Removing the guard on the board would therefore not restore profiling — it would only reintroduce a failing subprocess in the middle of the registration walk, with the manifest-corruption hazard above still armed. The guard is the correct permanent behaviour for this architecture, and the energy measurement it stands in for is addressed by an entirely different mechanism in §9.

### M-A8 — port f7 (AES-128) to ARMv8 Crypto Extensions [arm] · `f7/aes128.c`, `f7/Makefile`
**Symptom:** `gcc: error: unrecognized command-line option '-maes' / '-msse4.1'`, and the source uses `<wmmintrin.h>` AES-NI intrinsics (`_mm_aesenc_si128`, `_mm_aeskeygenassist_si128`, …).

**Fix:**
- Makefile flags: `-maes -msse4.1` → `-march=armv8-a+crypto`.
- Rewrote `aes128.c` using NEON crypto intrinsics: 9× `vaesmcq_u8(vaeseq_u8(state, rk_i))`, then a final `vaeseq_u8` + `veorq_u8` with `rk_10`.
- **Key semantics differ from x86.** `vaeseq_u8` XORs the round key at the *start* of the round (AddRoundKey → SubBytes → ShiftRows) and MixColumns is a separate `vaesmcq_u8`, whereas AES-NI's `_mm_aesenc_si128` XORs the key at the *end*. The round keys are therefore effectively shifted by one position relative to the x86 code — getting this wrong produces plausible-looking output that fails the KAT.
- Key expansion is done in scalar C (FIPS-197) rather than translating `_mm_aeskeygenassist` — safer, and yields the identical 176-byte schedule.
- **Validated:** KAT (`test1.c`) passes → output is bit-identical to the AES-NI reference, and re-verified natively on the A53.

### M-A9 — port f8 (AES-256) to ARMv8 Crypto Extensions [arm] · `f8/aes256.c`, `f8/Makefile`
Same approach as M-A8, adapted for AES-256: **14 rounds** (13× `vaese` + `vaesmc`, final `vaese` + `veorq` with `rk_14`) and a 240-byte scalar key schedule with the AES-256-specific **extra SubWord** applied on every word where `i % 8 == 4`. Makefile flags changed identically. **Validated:** KAT (`test2.c`) passes bit-identical.

### M-A10 — unique lookup-table names to avoid `collide()` [arm] · `f7/aes128.c`, `f8/aes256.c`
**Symptom:** f7 and f8 passed their KATs in isolation but produced **no object** in the full f1→f8 walk — `register` reported success (exit 0) yet `LIB/` held 6 objects instead of 8.

**Cause:** the scalar key-expansion code introduced by M-A8/M-A9 brought in globals named `sbox` and `Rcon`. `collide()` detects clashes via `nm --defined-only`, which lists local symbols too, so these were flagged as clashing with the identically-named tables already registered by f1 and f4. On a clash `register.c` skips `generate` entirely — no slot is taken, no counter bump happens, and the exit status stays 0, so the failure is completely silent. Note that `static` does **not** hide these symbols from `nm`.

**Fix:** rename per implementation — `sbox`/`Rcon` → `f7_sbox`/`f7_rcon` in f7, and `f8_sbox`/`f8_rcon` in f8. Logic unchanged; KATs still pass. After this the full walk produces **8 objects** with `///1-04` / `///2-04` counters.

This is the single most instructive failure in the port: a green exit status, a passing unit test, and a silently incomplete result. Always run `./reset` before a fresh walk, and always check the object count in `LIB/` rather than trusting `register`'s return value.

### M-A11 — pin compose platform to arm64 [arm] · `compose-server.yml`
**Symptom:** `docker compose ... up --build` produced a working server whose banner read `platform: debian-amd64` — compose builds for the **host** architecture by default (amd64 on the x86 dev box), silently ignoring the arm64 image previously built via `buildx --platform`. The container ran, registered its implementations and served correctly; only the banner revealed that none of the ARM work was being exercised.

**Fix:** add `platform: linux/arm64` under the `ssl-server` service:

```yaml
services:
  ssl-server:
    build: .
    platform: linux/arm64        # <-- M-A11: force ARM build/run
    container_name: Test-server
    ...
```

After this the compose banner reads `platform: debian-arm64`.

**On the board this pin is redundant.** Native aarch64 hardware builds arm64 by default, so the line neither helps nor hurts there — it is kept because the same compose file serves both paths, and removing it would silently re-break the cross-build. Treat the banner check as the authoritative test in either environment: it is the only place that reports what was *actually* built, as opposed to what was intended.

> **Note on `command:`** — the committed compose still lists `command: /app/server`, yet the running container clearly executes the full `start.sh` pipeline (register + serve), so the image's effective default command is `start.sh`. This works today; if a server ever comes up with an empty implementation table, set `command: /app/start.sh` explicitly.

> **Note on service vs. container name** — the compose *service* is `ssl-server`, the *container* is `Test-server`. `docker compose` subcommands take the former, `docker exec` and `docker logs` take the latter. They are not interchangeable, and mixing them up produces a confusing "no such service/container" error.

### M-A12 — `build-essential` instead of `gcc` [hw] · `Dockerfile` · `e5edc89`
**Symptom:** the build fails on a missing `libc6-dev`, and `make` is absent.
**Cause:** the Dockerfile installed `gcc` alone. On a minimal base that pulls the compiler binary without the C library headers it needs, and without `make` at all — dependencies that a fuller image would have satisfied incidentally.
**Fix:** replace `gcc` with `build-essential` in the `apt-get install` line. This is the class of failure that appears whenever the base gets leaner: packages that were previously present as transitive dependencies of something else have to be requested explicitly.

### M-A12 — `build-essential` instead of `gcc` [hw] · `Dockerfile` · `e5edc89`
**Symptom:** the build fails on a missing `libc6-dev`, and `make` is not found.
**Cause:** the Dockerfile installed `gcc` alone, which pulls the compiler binary without the C library headers it needs and without `make` at all. On a fuller base these arrived as transitive dependencies of something else; on a minimal one they have to be requested explicitly.
**Fix:** replace `gcc` with `build-essential` in the `apt-get install` line.

### M-A13 — align the base image with the board's Ubuntu release [hw] · `Dockerfile` · `aaf1af7`
Moved the base to Ubuntu 22.04, matching the release running on the Kria board (Ubuntu 22.04.5 IoT). Aligning the container's userspace with the host's avoids glibc and toolchain version skew between what the code is compiled against and what the board actually runs. Superseded by §11, which replaces the stock image with a snapshot of the board itself.

---

## 5. Host prerequisites

The project has two build paths. §5a is the dev-host cross-build, used to iterate quickly; §5b is the native build on the target. They share the same source tree and the same compose file.

### 5a. Cross-build host (WSL2 / Docker Desktop)

Two things the cross-build needs that a native Kria build will **not**:

1. **Docker Desktop → WSL Integration** enabled for the Ubuntu distro (`docker version` must respond from inside WSL).
2. **QEMU runtime handler** for executing arm64 images via `docker run`. buildx ships its own emulation, so the *build* works regardless, but `docker run` uses the host kernel's binfmt handlers, which a fresh WSL VM lacks — the tell is `exec format error` on any arm64 binary. Register once:

```bash
docker run --privileged --rm tonistiigi/binfmt --install arm64
```

This is per-WSL-VM and does **not** survive `wsl --shutdown`; re-run it if `exec format error` reappears.

Also needed here and not on the board: `docker buildx` for the `--platform linux/arm64` build.

### 5b. Native host (Kria KV260)

Board bring-up — flashing the OS, serial console, networking, Docker installation — is documented step by step in [`KRIA_KV260_DEPLOYMENT.md`](KRIA_KV260_DEPLOYMENT.md). Summarised, what must be true before building:

1. **Ubuntu 22.04 IoT** flashed to microSD and booted. Note that balenaEtcher's streaming decompression fails on the `.xz` image — decompress first with `unxz -kv`, then flash the raw `.img`.
2. **Docker Engine** installed on the board (not Docker Desktop, which has no aarch64 build).
3. **User in the `docker` group.** After `usermod -aG docker $USER` the new group membership does not apply to the current login — either re-establish the SSH session or keep using `sudo` until you do.
4. **Crypto extensions confirmed present**, rather than assumed from the datasheet:

```bash
grep -o 'aes\|pmull\|sha1\|sha2' /proc/cpuinfo | sort -u
```

All four must appear, or f7/f8 (M-A8/M-A9) will not have the instructions they compile against.

5. **Disk headroom on the microSD.** The base image alone (§6b) is several GB before any application layer is built on top.

Not needed on the board: buildx, binfmt/QEMU registration, and any `--platform` flag.

---

## 6. Full build & run

#### 6a. Cross-build on WSL2

Since the base image is itself the Kria arm64 rootfs (§6b), the cross-build pulls it through QEMU. This works — buildx handles the arm64 base natively — but the first build is substantially heavier than it was against a stock `ubuntu` base: several GB of image to fetch before any layer is compiled, all subsequent compilation emulated.

```bash
cd ~/myrtus/myrtus-psm-edge
git checkout al3monni-test-arm

# one-time per WSL VM (see §5a)
docker run --privileged --rm tonistiigi/binfmt --install arm64

# build the arm64 image (first build is slow: base pull + likwid under QEMU)
docker buildx build --platform linux/arm64 -t myrtus-psm-edge:arm64 --load .

# confirm the binaries are actually aarch64
docker run --rm --entrypoint readelf myrtus-psm-edge:arm64 -h server | grep Machine
#   -> Machine:  AArch64

# run via compose
sudo docker compose -f compose-server.yml up --build
```

The cross-build remains useful for catching compile errors without occupying the board, but the board is now the faster path for a full clean build.

### 6b. Native build on the Kria board

No buildx, no binfmt, no `--platform` — on native aarch64 all three are redundant.

```bash
git clone https://github.com/mdc-suite/myrtus-psm-edge.git
cd myrtus-psm-edge
git checkout al3monni-test-arm

sudo docker compose -f compose-server.yml up --build
```

The first build pulls the base image (several GB, §6b-note) and compiles likwid natively; subsequent builds resume from the `COPY . .` layer thanks to M-A2.

### Pass conditions in the log (both paths)

- `Registering Implementation in ./f1 … ./f8` followed by `Done`
- `Creating Shared Library lib_enc.so` with **no `ld` error** beneath it
- `platform: debian-arm64` — if this reads `amd64` on the cross-build, M-A11 did not take
- **no** `Error setting socket opts: Operation not permitted` — that means it was not launched privileged, i.e. not via compose

Background it once verified: `Ctrl-C`, then re-run with `-d`.

---

## 7. Verification

Run these against a container that has completed its startup pipeline. Commands take the *container* name `Test-server`, not the service name (see the note in M-A11). On the board, `sudo` is required until the SSH session has been re-established after `usermod -aG docker`.

```bash
# the binaries are actually aarch64
docker exec Test-server readelf -h /app/server | grep Machine     # -> AArch64

# 8 objects + the shared lib, all freshly timestamped
docker exec Test-server ls -la /app/LIB

# 8 exported symbols, all type T
docker exec Test-server sh -c 'nm -D /app/LIB/lib_enc.so | grep enc_s'

# counters must read 4 per level
docker exec Test-server sh -c 'grep "///" /app/header.h'          # ///1-04  ///2-04

# server listening
docker exec Test-server sh -c 'lsof -i -P -n | grep LISTEN'       # *:5544, *:5545
```

### End-to-end round trip

The authoritative correctness test is a file transfer compared byte for byte. `rfile` (10000 bytes of repeating ASCII) is committed in the repo for this purpose:

```bash
cmp rfile <received-file>     # no output means identical
```

`cmp` is the test that matters. The server's own byte counter is unreliable (§9, issue 3), so a plausible-looking log line is not evidence of a correct transfer; only `cmp` is.

### Reference baseline

| Check | Expected | aarch64 (QEMU) | Kria KR260 |
|---|---|---|---|
| OpenSSL banner | `platform: debian-arm64` | ✅ | ✅ |
| `LIB/` contents | 8 × `enc_s0*.o` + `lib_enc.so` | ✅ | ✅ |
| `nm -D` | `enc_s01_n01`..`n04`, `enc_s02_n01`..`n04`, all `T` | ✅ | ✅ |
| `header.h` | `///1-04`, `///2-04` | ✅ | ✅ |
| f1–f8 registration | exit 0, 0 symbol clashes, 0 KAT failures | ✅ | ✅ |
| Round-trip (10000 B) | `10016 bytes` on the wire (10000 + 16-byte AEAD tag) | ✅ | ✅ |
| `cmp` | identical | ✅ | ✅ |
| Crypto extensions in `/proc/cpuinfo` | `aes pmull sha1 sha2` | n/a | ✅ |

Everything that passed under emulation also passes on the silicon: no part of the port was an artefact of QEMU.

> **x86-64 baseline status.** The table above covers the aarch64 paths only. The x86-64 baseline was last validated end-to-end at commit `ddb5f5f` (branch `al3monni-test`) and has not been re-run since; the ARM work has diverged considerably from it in the meantime. Re-validating x86-64 against the current tree — in particular against the new base image — is open work.

---

## 8. Selection mechanism & implementation map

One byte encodes the choice (`encrypt02.c`):

```c
#define fbits(y)  (((y) & 0xc0) >> 6)   // function class
#define sbits(y)  (((y) & 0x30) >> 4)   // security level
#define ibits(y)   ((y) & 0x0f)         // implementation index
// fetch(mode): slevel = sbits(mode); num = ibits(mode);
//             sprintf(buf,"enc_s%02d_n%02d",slevel,num);
//             op = (function) dlsym(cx->handle, buf);
```

Default `mode = 98 = 0x62 = 0110 0010` → `sbits=2, ibits=2` → **`enc_s02_n02`** (= f5). Because profiling is disabled (M-A7, §9), `synthesize` never moves `mode` off 98 and the server always reports `Starting with enc_s02_n02`. This is not a bug — it is the honest consequence of having no energy measurements to select on. The `dlopen`/`dlsym` machinery is fully functional; only the *policy input* that would drive it is missing.

### Implementation map — the contract (post-port)

| dir | function | build flags (aarch64) | → symbol | notes |
|---|---|---|---|---|
| f1 | `aes128` | plain C | `enc_s01_n01` | reference AES-128 |
| f2 | `AES_enc` | plain C | `enc_s01_n02` | `.s` file present but **unused** by Makefile |
| f3 | `aes_ecb_encrypt` | `-DUNROLL_TRANSPOSE` (bitsliced) | `enc_s01_n03` | pure C, ports free |
| f7 | `aes128` | **`-march=armv8-a+crypto`** | `enc_s01_n04` | **M-A8/M-A10** — ported from AES-NI |
| f4 | `aes256` | plain C | `enc_s02_n01` | reference AES-256 |
| f5 | `AES256_enc` | plain C | `enc_s02_n02` | **default target**; `.s` present but unused |
| f6 | `aes256_ecb_encrypt` | bitsliced | `enc_s02_n03` | pure C, ports free |
| f8 | `aes256` | **`-march=armv8-a+crypto`** | `enc_s02_n04` | **M-A9/M-A10** — ported from AES-NI |

Registration order **is** the numbering — the table above is a contract, not a description. Reordering the `register` calls in `start.sh` renumbers the symbols and silently breaks agreement with any client built against the old order.

The f2/f5 `.s`-assembly concern from the original scope was resolved by observation: both Makefiles compile the C source directly and never assemble the `.s` files, so **no ARMv8 assembly rewrite was needed**.

Symbol coexistence works because `gen.c`→`generate` wraps each implementation: `#define <fn> enc_sXX_nYY` + `#include` → `gcc -E -P` → fully-preprocessed `source.c` → object renamed into `LIB/`. That is why eight implementations with (previously) identical internal function names can share one `.so` — and why the f7/f8 *global tables* still needed the M-A10 rename, since globals survive preprocessing untouched.

On the Kria, f7 and f8 are the two backends that exercise the A53's crypto extensions; the other six are portable C. That split is the whole point of the comparison the energy layer (§9) is meant to quantify.
---

## 9. Known issues & remaining work

### Expected on aarch64 (not port bugs)

| # | Symptom | Cause | Impact |
|---|---|---|---|
| 1 | `Cannot read performance group ENERGY` | likwid's energy path is built on x86 RAPL MSRs; the Cortex-A53 exposes no equivalent | **Energy profiling unavailable.** Registration still succeeds — `cp → LIB/` happens *before* profiling, and M-A7 skips the call entirely |
| 2 | `mode` stuck at `98` → always `enc_s02_n02` | consequence of #1: with no measurements, `synthesize` has nothing to compute a mode from | selection is pinned; does not affect correctness testing |
| 3 | `Recieved 9216 bytes` for a 10000 B file | the counter tallies 9×1024 chunks and drops the 784 B remainder | cosmetic — `cmp` proves the data is intact |
| 4 | `rate inf bps` | elapsed time rounds to 0 → division by zero | cosmetic |

Issues 1 and 2 are architectural, not defects introduced by the port; 3 and 4 are upstream reporting bugs present on x86 as well.

### Resolved by this port

- Upstream `check*.c` omission (M-B1/M-B2) — `LIB/` now populates on any architecture.
- `register` silently omitting slots on symbol clash — surfaced and fixed for f7/f8 (M-A10). Always run `./reset` before a fresh walk and count the objects in `LIB/` rather than trusting the exit status.
- likwid profiler hang and manifest corruption (M-A7).
- Full aarch64 validation on real silicon, not only under emulation (§7).

### Remaining work

**1. Power measurement on ARM — the main open item.** The selection logic this component is built around needs an energy figure per backend, and the mechanism that supplied it on x86 does not exist here. What is needed is a change to the measurement code so that a power figure can be obtained on aarch64 at all; the likely route is the board's on-SOM INA260 power monitor, but the approach is not settled and the alternatives have not been ruled out. Whatever mechanism is chosen has to deal with the fact that a single AES operation completes far below the resolution of any board-level power sensor, so the measurement will have to be taken over sustained runs and attributed back to individual backends rather than sampled per operation. Until this lands, issue #2 stands and backend selection remains pinned.

**2. Cycle-unit reconciliation.** `cntvct_el0` counts generic-timer ticks at a fixed frequency, not CPU cycles (M-A3). Any timing figure derived from it is in the wrong units for comparison against x86 results; `cycle_freq()` provides the conversion factor and must be applied when real measurements are wired up.

**3. x86-64 re-validation.** The baseline has not been re-run since `ddb5f5f` and the tree has moved considerably, including the base image change. See the note at the end of §7.

**4. Optional upstream fixes.** Initialise `bool rval = 0;` in `register.c`; return non-zero from `register` when `collide()` refuses, so a skipped implementation is not reported as success; check that the `check%d.c` template exists *before* opening `test%d.c` for writing.

---

## 10. Commit trail (branch `al3monni-test-arm`)

Oldest first. `git log --oneline --graph al3monni-test-arm` is the authoritative sequence.

| commit | change | modification |
|---|---|---|
| `70d30c4` | upstream state (`main`) | — |
| `ddb5f5f` | x86 baseline: first tranche of fixes making the project runnable at all | M-B1 … M-B4 |
| `9207e41` | Dockerfile: initial ARM support | M-A1 |
| `d01abde` | Dockerfile: likwid built after `COPY`, so subsequent builds resume from step 7 instead of step 4 | M-A2 |
| `c992b51` | arm64 build completes: `cycles.h` for `rdtscp`, vestigial `-l_enc` dropped | M-A3/A4/A5 |
| `587d1e8` | comment-syntax fix on the four `#include "cycles.h"` lines | M-A6 (folded into M-A4) |
| `af69ca9` | f1 runs — profiling skipped on aarch64 | M-A7 |
| `6fd2ea4` | f7: AES-NI → ARMv8 Crypto Extensions, scalar key expansion; KAT bit-identical | M-A8 |
| `4464e76` | f8: AES-256 AES-NI → ARMv8 Crypto Extensions, 240-byte scalar schedule | M-A9 |
| `dafd421` | f7/f8: `sbox`/`Rcon` renamed per implementation; all 8 register, 8 objects in `LIB/` | M-A10 |
| `9a902ab` | compose: `platform: linux/arm64` pinned; native aarch64 server, full f1–f8 manifest, no socket EPERM | M-A11 |
| `5faca48` | README update | — |
| `e5edc89` | Dockerfile: `build-essential` — `gcc` alone omitted `make` and `libc6-dev` | M-A12 |
| `aaf1af7` | Dockerfile: base image aligned to Ubuntu 22.04 | M-A13 |
| `ad1385f` | Dockerfile: base image to a Kria rootfs snapshot (`kria-base:22.04`) | superseded |
| `dc78a00` | `LIB/`: untrack stale x86-64 build artefacts, regenerated by `start.sh` at container start | — |
| `bb4ce8c` | add `.gitignore`; untrack generated `test1.c`/`test2.c` | — |
| `66baa82` | Dockerfile: base image to `al3monni/kria-ubuntu:22.04.5` — snapshot rebuilt from a freshly flashed, fully upgraded board after the first one was found to be missing `/tmp` and `/run` | §11 |

The ARM work falls into four phases: **make it build** (`9207e41` … `c992b51`), **make it register and run correctly** (`af69ca9` … `9a902ab`), **move it onto the board's own userspace** (`e5edc89` … `ad1385f`), and **clean up and correct the base** (`dc78a00` … `66baa82`)..

---

## 11. Base image [hw]

The container is layered on **[`al3monni/kria-ubuntu:22.04.5`](https://hub.docker.com/r/al3monni/kria-ubuntu)** — a snapshot of the board's own root filesystem, published to Docker Hub. `linux/arm64/v8`, ~2 GB compressed.

```dockerfile
FROM al3monni/kria-ubuntu:22.04.5
```

### Why not stock `ubuntu:22.04`

Same distribution, different userspace. AMD's Ubuntu 22.04 IoT image for Kria ships board-specific tooling — `xmutil` and the platform-statistics utilities among them — that the power-measurement work in §9 will need and that vanilla Ubuntu does not carry. Building on a snapshot of the board itself also decouples the container from whatever happens to be installed on the board at build time, so the toolchain survives a reflash.

### How the image is produced

The rootfs is tarred from a **freshly flashed and fully upgraded board, before Docker is installed** — the ordering matters, since a board with Docker already running carries an image store that would otherwise end up inside the snapshot.

```bash
sudo tar -cpf /home/ubuntu/kria-rootfs.tar \
  --exclude='./proc/*' --exclude='./sys/*' --exclude='./dev/*' \
  --exclude='./tmp/*'  --exclude='./run/*' \
  --exclude='./mnt/*'  --exclude='./media/*' \
  --exclude='./configfs/*' \
  --exclude='./home/ubuntu/kria-rootfs.tar' \
  --exclude='./var/lib/snapd' --exclude='./snap' \
  --exclude='./swapfile' --exclude='./lost+found' \
  -C / .

docker import /home/ubuntu/kria-rootfs.tar al3monni/kria-ubuntu:22.04.5
```

Three details that are easy to get wrong, each of which cost a build cycle:

**Exclude the contents, not the directory.** `--exclude='./tmp/*'` keeps `/tmp` in the archive as an empty directory; `--exclude='./tmp'` drops it entirely. The first version of this image was built the second way, and the result had no `/tmp` and no `/run` at all. The failure surfaces far from its cause: `apt-get update` reports a wall of GPG signature errors, because apt cannot create the temporary files it passes to `apt-key`. The real message is the last line — `Unable to mkstemp /tmp/... (2: No such file or directory)`.

**Exclusion paths must match the archive form.** With `-C / .` tar writes relative paths (`./sys/...`), so `--exclude=/sys/*` never matches and the whole of `/sys` is archived. Quote the patterns so the shell does not expand them before tar sees them.

**`./configfs` is Kria-specific.** The device-tree overlay interface is mounted at the root on this platform and must be excluded like any other kernel filesystem.

Verify before publishing, not after:

```bash
tar -tf kria-rootfs.tar './tmp/' './run/'        # both must be listed
tar -tf kria-rootfs.tar | grep -c '^./sys/'      # must be 0 or 1
docker inspect al3monni/kria-ubuntu:22.04.5 --format '{{.Architecture}}'   # arm64
docker run --rm al3monni/kria-ubuntu:22.04.5 sh -c 'ls -ld /tmp /run && apt-get update'
```

The last line is the real test: it exercises the exact path that failed on the first attempt.

### Using it

Treat the snapshot as a frozen versioned base. Every application build step belongs in the git-tracked `Dockerfile` layered on top; nothing gets baked into the snapshot, or the build stops being reproducible from source. The tag carries the Ubuntu point release, so a future refresh gets a new tag rather than silently replacing this one.

> **Provenance.** Derived from AMD/Xilinx's Ubuntu 22.04 IoT image for Kria; contains Canonical- and AMD-licensed components, redistributed under their respective terms.
