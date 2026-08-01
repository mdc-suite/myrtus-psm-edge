# spdocker — ARM64 (aarch64) Port Runbook

**Repo:** https://github.com/subhadeep-banik/spdocker
**Fork:** https://github.com/al3monni/spdocker
**Port branch:** `al3monni-test-arm`  ·  **x86 baseline tag:** `x86-baseline` (`ddb5f5f`)
**Target hardware:** Kria KV/KR260 — Zynq UltraScale+ MPSoC, Cortex-A53, aarch64
**Dev/validation host:** Windows + WSL2 (Ubuntu) + Docker Desktop, cross-building `linux/arm64` under QEMU
**Base image:** `ubuntu:26.04`, OpenSSL 3.5.5, gcc 15
**Status:** ✅ Builds, registers all 8 implementations, and runs as a native aarch64 server. Energy/profiling layer intentionally deferred to on-board work (see §9).

> This document supersedes the original x86 build runbook. It records **every** modification required to take the project from the upstream x86-only state to a working aarch64 container. Where a change was already needed just to build on x86 (the `check*.c` reconstruction, the compose path/X11 cleanup), it is marked **[baseline]**; ARM-specific changes are marked **[arm]**.

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

---

## 2. The two failure classes this port had to solve

**(a) Upstream out-of-the-box failure [baseline].** A fresh clone fails on *any* architecture because `check1.c` / `check2.c` (the KAT templates `register.c` reads) are missing from the repo. `register.c` truncates `test%d.c` via `fopen(...,"wb")` *before* checking the template exists, so the test binary ends up empty → `undefined reference to 'main'` → `TEST FAILED` → `LIB/` never populated → `gcc -shared ./LIB/*.o` matches nothing. `start.sh` hides all of it (`> /dev/null 2>&1` per register, unconditional `echo`). Fixed by reconstructing both templates (§3, M-B1/M-B2).

**(b) Architecture faults [arm].** Once it builds on x86, the aarch64 cross-build exposes: likwid compiling its x86 access layer; four copies of x86 `rdtscp` inline asm; a vestigial `-l_enc` link against a committed x86 `.so`; the likwid profiler hanging under QEMU and corrupting the manifest; and f7/f8 using x86 AES-NI intrinsics and flags. All addressed in §4.

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

Each is a single, independently-validated change, in the order they surfaced during the cross-build. Filenames are relative to repo root.

### M-A1 — likwid: build the ARMv8 target [arm]  ·  `Dockerfile`
**Symptom:** likwid layer fails compiling `access_x86_*.o` — its `make` defaults to `COMPILER = GCC`, which means *GCC-on-x86*; likwid couples compiler and architecture in one setting.
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
- `ACCESSMODE = perf_event` avoids the MSR access daemon (x86-only, and MSRs are unavailable on both WSL2 and Kria). On the **real Cortex-A53 PMU**, `perf_event` is also the correct mechanism to read cycles/instructions/cache counters — so this is the right long-term setting, not just a build dodge.
- Rationale for patching rather than stubbing likwid: keeps `#include <likwid.h>` / `-llikwid` resolving and keeps the diff against `x86-baseline` honest.

### M-A2 — Dockerfile layer reorder (cache) [arm]  ·  `Dockerfile`  ·  commit `d01abde`
Moved the likwid `wget` + build block **above** `COPY . .`. The likwid layer is ~800 s under QEMU; before the reorder, any source edit busted it and every cycle paid the full cost. After: likwid is a stable early layer, and source edits resume from `COPY . .` (~seconds). This single reorder is what makes the edit/build loop tolerable.

### M-A3 — portable cycle counter [arm]  ·  new file `cycles.h`
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

Design notes: x86 branch is byte-identical to the old code (x86 build unaffected); `static inline` (not `extern inline`) avoids duplicate-symbol at link when `make server` compiles multiple TUs into one binary; the `isb` restores the partial serialization x86's `rdtscp` gives that a bare `mrs` does not. **Unit caveat:** `cntvct_el0` ticks ≈100× slower than an x86 TSC; downstream code that assumes CPU-clock ticks will read wrong *magnitudes* silently. Harmless today (the profiling chain is dead — §7 #1), but `cycle_freq()` is provided so the energy-layer rework has the conversion factor ready.

### M-A4 — remove local rdtscp definitions [arm]  ·  `server_f.c`, `profile.c`, `profile01.c`, `internalprofile.c`
Delete the six-line x86 `rdtscp` definition from each file and add `#include "cycles.h"` with the other includes. (`internalprofile.c` has the definition but no calls — remove it anyway for consistency.) Verify:
```bash
grep -rn '__asm__ volatile("rdtscp"' --include=*.c .   # must print nothing
grep -n  "cycles.h" server_f.c profile.c profile01.c internalprofile.c  # 4 lines
```

### M-A5 — drop vestigial `-l_enc` link flag [arm]  ·  `Makefile`, `makeclient`
**Symptom:** `ld: skipping incompatible ./LIB//lib_enc.so when searching for -l_enc` → `cannot find -l_enc`. The committed `lib_enc.so` is x86-64; on aarch64 `ld` skips it and `-l` failing to resolve is a hard error.
**Fix:** remove only `-L$(LIBF) -l_enc` from `CFLAGS` in both files; **keep `-ldl`** (that's `dlopen`) and **keep `-rdynamic`** (exports the executable's symbols so the `RTLD_GLOBAL`-loaded lib resolves back into it).

```make
# before
CFLAGS = -w -Wno-incompatible-pointer-types -lcrypto -lssl  -L$(LIBF) -l_enc -ldl -rdynamic
# after
CFLAGS = -w -Wno-incompatible-pointer-types -lcrypto -lssl -ldl -rdynamic
```
The symbols were never referenced at link time (everything goes through `dlsym`), so this produces a **byte-identical binary on x86** and fixes ARM. `start.sh` regenerates `lib_enc.so` at runtime regardless.

### M-A7 — skip likwid profiling under emulation [arm]  ·  `gen.c`
**Symptom:** on aarch64 the profiler prints `Unsupported ARMv8 Processor` / `Cannot read performance group ENERGY` and, under QEMU, the `./profile` execution **hangs**. Worse: in `gen.c`, the `header.h` manifest update (counter bump + new prototype) sits inside `if(!rt)`, while `rm header.h; mv header1.h header.h` runs **unconditionally** — so a failed/interrupted profiler run wipes the manifest.
**Fix:** on aarch64, skip the profiler run and force success so the manifest update still executes:

```c
   sprintf(com1, "./profile %d %d %d", slevel, h, cpu);
#if defined(__aarch64__)
   rt = 0;   // skip likwid profiling on aarch64 (hangs under QEMU; MSR/energy
             // unavailable on Kria too). Force success so header.h updates.
#else
   rt = system(com1);
#endif
```

The profiler binaries are still *built* (harmless); only their *execution* is skipped. This both unblocks the f1→f8 walk and is the first concrete step of the energy-layer rework.

### M-A8 — port f7 (AES-128) to ARMv8 Crypto Extensions [arm]  ·  `f7/aes128.c`, `f7/Makefile`
**Symptom:** `gcc: error: unrecognized command-line option '-maes' / '-msse4.1'`, and the source uses `<wmmintrin.h>` AES-NI intrinsics (`_mm_aesenc_si128`, `_mm_aeskeygenassist_si128`, …).
**Fix:**
- Makefile flags: `-maes -msse4.1` → `-march=armv8-a+crypto`.
- Rewrote `aes128.c` using NEON crypto intrinsics: 9× `vaesmcq_u8(vaeseq_u8(state, rk_i))`, then a final `vaeseq_u8` + `veorq_u8` with `rk_10`. **Key semantics differ from x86:** `vaeseq_u8` XORs the round key at the *start* (AddRoundKey→SubBytes→ShiftRows) and MixColumns is a separate `vaesmcq_u8`, so keys are effectively "shifted by one" versus AES-NI. Key expansion is done in scalar C (FIPS-197) rather than translating `_mm_aeskeygenassist` — safer and yields the identical 176-byte schedule.
- **Validated:** KAT (`test1.c`) passes → output is bit-identical to the AES-NI reference.

### M-A9 — port f8 (AES-256) to ARMv8 Crypto Extensions [arm]  ·  `f8/aes256.c`, `f8/Makefile`
Same approach as M-A8, adapted for AES-256: **14 rounds** (13× `vaese+vaesmc`, final `vaese` + `veorq` with `rk_14`) and a 240-byte scalar key schedule with the AES-256-specific **extra SubWord** on every word where `i % 8 == 4`. Makefile flags changed identically. **Validated:** KAT (`test2.c`) passes bit-identical.

### M-A10 — unique lookup-table names to avoid `collide()` [arm]  ·  `f7/aes128.c`, `f8/aes256.c`
**Symptom:** f7/f8 passed their KATs in isolation but produced **no object** in the full f1→f8 walk — `register` reported success (exit 0) yet `LIB/` held 6 objects, not 8. Cause: the scalar ports introduced globals `sbox` / `Rcon`, which `collide()` (via `nm --defined-only`, which lists locals too) flags as clashing with the same-named tables already registered by f1/f4. On a clash, `register.c` skips `generate` entirely, so no slot is taken and no counter bump happens. (`static` does not hide them from `nm`.)
**Fix:** rename per implementation — `sbox`/`Rcon` → `f7_sbox`/`f7_rcon` in f7, `f8_sbox`/`f8_rcon` in f8. Logic unchanged; KATs still pass. After this, the full walk produces **8 objects** with `///1-04` / `///2-04` counters.

### M-A11 — pin compose platform to arm64 [arm]  ·  `compose-server.yml`
**Symptom:** `docker compose ... up --build` produced a working server whose banner read `platform: debian-amd64` — compose builds for the **host** architecture by default (amd64 on the x86 dev box), ignoring the arm64 image built via `buildx --platform`.
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

> **Note on `command:`** — the committed compose still lists `command: /app/server`, yet the running container clearly executes the full `start.sh` pipeline (register + serve) — so the image's effective default command is `start.sh`. This works today; if you ever see a server come up with an empty implementation table, set `command: /app/start.sh` explicitly.

---

## 5. Host prerequisites (WSL2 / Docker Desktop, cross-build)

Two things the cross-build needs that a native Kria build will **not**:

1. **Docker Desktop → WSL Integration** enabled for the Ubuntu distro (`docker version` must respond from inside WSL).
2. **QEMU runtime handler** for executing arm64 images via `docker run`. buildx ships its own emulation (so the *build* works), but `docker run` uses the host kernel's binfmt handlers, which a fresh WSL VM lacks — the tell is `exec format error` on any arm64 binary. Register once:
   ```bash
   docker run --privileged --rm tonistiigi/binfmt --install arm64
   ```
   This is per-WSL-VM and does **not** survive `wsl --shutdown`; re-run it if `exec format error` reappears. None of this exists on the Kria board (arm64 is native).

---

## 6. Full build & run (cross-build on WSL2)

```bash
cd ~/spdocker
git checkout al3monni-test-arm

# one-time per WSL VM (see §5)
docker run --privileged --rm tonistiigi/binfmt --install arm64

# build the arm64 image (first build: ~800 s for likwid under QEMU; then cached)
docker buildx build --platform linux/arm64 -t spdocker:arm64 --load .

# confirm the binaries are actually aarch64
docker run --rm --entrypoint readelf spdocker:arm64 -h server | grep Machine
#   -> Machine:  AArch64

# run via compose (privileged context supplies NET_ADMIN/NET_RAW for socket opts)
sudo docker compose -f compose-server.yml up --build
```

**Pass conditions in the log:**
- `Registering Implementation in ./f1 … ./f8` then `Done`
- `Creating Shared Library lib_enc.so` with **no `ld` error** under it
- `platform: debian-arm64` (not amd64 — that means M-A11 didn't take)
- **no `Error setting socket opts: Operation not permitted`** (that means it wasn't launched privileged / not via compose)

Background it once verified: `Ctrl-C`, then add `-d`.

---

## 7. Verification

```bash
# 8 objects + the shared lib, all freshly timestamped
docker exec Test-server ls -la /app/LIB

# 8 exported symbols, all type T
docker exec Test-server sh -c 'nm -D /app/LIB/lib_enc.so | grep enc_s'

# counters must read 4 per level
docker exec Test-server sh -c 'grep "///" /app/header.h'          # ///1-04  ///2-04

# server listening
docker exec Test-server sh -c 'lsof -i -P -n | grep LISTEN'       # *:5544, *:5545
```

### aarch64 reference baseline

| Check | Expected |
|---|---|
| `readelf -h` on any binary | `Machine: AArch64` |
| OpenSSL banner | `platform: debian-arm64` |
| `LIB/` | 8 × `enc_s0*.o` + `lib_enc.so` |
| `nm -D` | `enc_s01_n01`..`n04`, `enc_s02_n01`..`n04`, all `T` |
| `header.h` | `///1-04`, `///2-04` |
| f1–f8 registration | exit 0, **0 symbol clashes, 0 KAT failures** each |
| Round-trip (10000 B file) | `10016 bytes` on the wire (10000 + 16-byte AEAD tag) |
| `cmp` | `IDENTICAL` |

---

## 8. Selection mechanism & implementation map (updated for ARM)

One byte encodes the choice (`encrypt02.c`):

```c
#define fbits(y)  (((y) & 0xc0) >> 6)   // function class
#define sbits(y)  (((y) & 0x30) >> 4)   // security level
#define ibits(y)   ((y) & 0x0f)         // implementation index
// fetch(mode): slevel = sbits(mode); num = ibits(mode);
//             sprintf(buf,"enc_s%02d_n%02d",slevel,num);
//             op = (function) dlsym(cx->handle, buf);
```

Default `mode = 98 = 0x62 = 0110 0010` → `sbits=2, ibits=2` → **`enc_s02_n02`** (= f5). Profiling is disabled (§7 #1 below), so `synthesize` never moves `mode` off 98 — the server always reports `Starting with enc_s02_n02`. Not a bug; it's the honest consequence of having no energy counters.

**Implementation map — the contract (post-port):**

| dir | function | build flags (aarch64) | → symbol | notes |
|---|---|---|---|---|
| f1 | `aes128` | plain C | `enc_s01_n01` | reference AES-128 |
| f2 | `AES_enc` | plain C | `enc_s01_n02` | `.s` file present but **unused** by Makefile |
| f3 | `aes_ecb_encrypt` | `-DUNROLL_TRANSPOSE` (bitsliced) | `enc_s01_n03` | pure C, ports free |
| f7 | `aes128` | **`-march=armv8-a+crypto`** (NEON crypto) | `enc_s01_n04` | **M-A8/M-A10** — ported from AES-NI |
| f4 | `aes256` | plain C | `enc_s02_n01` | reference AES-256 |
| f5 | `AES256_enc` | plain C | `enc_s02_n02` | **default target**; `.s` present but unused |
| f6 | `aes256_ecb_encrypt` | bitsliced | `enc_s02_n03` | pure C, ports free |
| f8 | `aes256` | **`-march=armv8-a+crypto`** (NEON crypto) | `enc_s02_n04` | **M-A9/M-A10** — ported from AES-256 AES-NI |

Registration order **is** the numbering. The f2/f5 `.s`-assembly concern from the original scope was resolved by observation: both Makefiles compile the C source directly and never assemble the `.s` files — **no ARMv8 assembly rewrite was needed.**

Symbol coexistence works because `gen.c`→`generate` wraps each implementation: `#define <fn> enc_sXX_nYY` + `#include` → `gcc -E -P` → fully-preprocessed `source.c` → object renamed into `LIB/`. That is why 8 implementations with (previously) identical internal names could share one `.so` — and why the f7/f8 *global* tables still needed the M-A10 rename, since those globals survive preprocessing.

---

## 9. Known issues & remaining work

### Expected on aarch64 (not port bugs)
| # | Symptom | Cause | Impact |
|---|---|---|---|
| 1 | `Unsupported ARMv8 Processor` / `Cannot read performance group ENERGY` | No PMU under QEMU; likwid MSR/energy path unavailable on WSL2 **and** Kria | **Profiling dead.** Registration still succeeds — `cp → LIB/` happens *before* profiling, and M-A7 skips the hang |
| 2 | `mode` stuck at `98` → always `enc_s02_n02` | consequence of #1 (empty measurements → `synthesize` can't compute mode) | selection is pinned; cosmetic for correctness testing |
| 3 | `Recieved 9216 bytes` for a 10000 B file | counter tallies 9×1024 chunks, drops the 784 B remainder | cosmetic — `cmp` proves data intact |
| 4 | `rate inf bps` | elapsed time rounds to 0 → div-by-zero | cosmetic |

### Resolved by this port
- Upstream `check*.c` omission (M-B1/M-B2) — `LIB/` now populates.
- `register` silently overwriting/omitting slots on symbol clash — surfaced and fixed for f7/f8 (M-A10); always run `./reset` before a fresh walk.
- likwid profiler hang + manifest corruption under QEMU (M-A7).

### Remaining (on-board / future)
1. **Energy measurement layer** — the real work left. likwid MSR/energy is permanently unavailable; on Kria, options are `perf_event` on the A53 PMU (cycles/instructions/cache — M-A1 already targets this) and/or hardware power rails (INA226/PMBus) for actual energy. Until then, selection-by-throughput is the fallback and `mode` stays pinned (issue #2).
2. **Cycle-unit reconciliation** — `cntvct_el0` (~33 MHz fixed) vs x86 TSC (~CPU-clock); use `cycle_freq()` (M-A3) when wiring real measurements so downstream magnitudes are correct.
3. **Deploy & validate natively on the Kria board** — no QEMU, no binfmt, native arm64. Re-run §7 verification on hardware; issue #1's PMU errors should change once `perf_event` can read the real A53 PMU.
4. **Optional upstream fixes** — initialise `bool rval = 0;` in `register.c`; return non-zero from `register` when `collide()` refuses; check the `check%d.c` template exists *before* opening `test%d.c` for writing.

---

## 10. Commit trail (branch `al3monni-test-arm`)

Representative commits for the ARM work (most recent last):

- `d01abde` — Dockerfile: build likwid after `COPY . .` reorder so source edits resume from later stage, saving exec time (M-A2)
- `c992b51` — arm64 build completes: `cycles.h` for rdtscp; drop vestigial `-l_enc` from link (M-A3/A4/A5)
- `587d1e8` — bugfix (comment syntax / cleanup, M-A6)
- *this session* — `gen.c` aarch64 profile skip (M-A7); f7/f8 ports to ARMv8 crypto (M-A8/A9); f7/f8 `sbox`/`Rcon` rename (M-A10); compose `platform: linux/arm64` (M-A11)

Baseline reconstruction (`check1.c`, `check2.c`, compose path/X11, `.dockerignore`) lives on the x86 baseline history, tagged `x86-baseline` (`ddb5f5f`).