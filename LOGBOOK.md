# Porting logbook — myrtus-psm-edge

This logbook records the modifications that took **spdocker** from an x86-only prototype that did not run out of the box to a component validated on the **AMD/Xilinx Kria KV260** and on a **bare-metal x86-64** host. It explains what broke, why, and what was changed. It is not a usage guide: installation, build and validation instructions live in [`README.md`](README.md), and board bring-up in [`KRIA_KV260_DEPLOYMENT.md`](KRIA_KV260_DEPLOYMENT.md).

|  |  |
|---|---|
| **Repository** | https://github.com/mdc-suite/myrtus-psm-edge |
| **Upstream** | https://github.com/subhadeep-banik/spdocker, by Subhadeep Banik |
| **Target hardware** | Kria KV260 — Zynq UltraScale+ MPSoC, 4× Cortex-A53, aarch64, Ubuntu 22.04 IoT |
| **Hosts** | the board itself (native aarch64 build) · bare-metal x86-64 Linux (native amd64 build) |
| **Base image** | [`al3monni/kria-ubuntu:22.04.5`](https://hub.docker.com/r/al3monni/kria-ubuntu) on aarch64, `ubuntu:22.04` on x86-64, chosen automatically (§12) |
| **Status** | ✅ Validated on both architectures at `dbd62c2`: eight backends registered and measured, byte-identical round trips on both security levels |

### How to read the modifications

Every modification in §3 and §4 carries a number (`M1` … `M16`), a level and the architecture it concerns.

| Level | Meaning | How it looks |
|---|---|---|
| 🔴 **Major** | changes the way the component works; read these first | large title and a highlighted summary |
| 🟡 **Medium** | a real fix, but local to one part of the pipeline | smaller title |
| 🟢 **Minor** | housekeeping and small build fixes | collapsed; click to open |

Architecture tags: `arm` (aarch64 only), `x86` (x86-64 only), `both`.

### Contents

1. [What this project is](#1-what-this-project-is)
2. [What was broken](#2-what-was-broken)
3. [Baseline: making upstream run at all](#3-baseline-making-upstream-run-at-all) — M1 … M3
4. [Port modifications](#4-port-modifications) — M4 … M16
5. [Host prerequisites](#5-host-prerequisites)
6. [Build and run](#6-build-and-run)
7. [Verification](#7-verification)
8. [Selection mechanism and implementation map](#8-selection-mechanism-and-implementation-map)
9. [Energy measurement](#9-energy-measurement)
10. [Status: resolved and open points](#10-status-resolved-and-open-points)
11. [Commit map](#11-commit-map)
12. [Base image](#12-base-image)

---

## 1. What this project is

A TLS client/server that ships **eight interchangeable AES implementations**. At runtime the server selects one, loads it from a shared library via `dlopen`/`dlsym`, and uses it for the authenticated encryption of file transfers. The selection is driven by measured time and energy, against a policy given to `synthesize` (§8).

As the **Privacy and Security Manager** of the MYRTUS edge layer, the component demonstrates *crypto-agility*: the cipher implementation behind a secure channel is not fixed at compile time but chosen at runtime, so the security/performance/energy trade-off can be renegotiated as conditions on the node change.

On start, the container runs this pipeline (`start.sh`):

```
reset  →  register ×8  →  gcc -shared ./LIB/*.o -o ./LIB/lib_enc.so  →  ./server
```

- `reset` clears `LIB/` and resets the counters in `header.h`
- `register -c ./fN/config.txt` compiles backend `fN`, runs a known-answer test (KAT), measures its time and energy, gives it a unique symbol name and drops `enc_sXX_nYY.o` into `LIB/`
- `server` opens `./LIB/lib_enc.so` and resolves `enc_s%02d_n%02d` according to the mode byte

The **registration order is the numbering**: it decides which `fN` a given mode byte selects. The client does not depend on it, since it never loads `lib_enc.so`; everything that names a backend by number does (§8).

---

## 2. What was broken

**(a) Upstream did not run on any machine.** `register` checks every backend with a known-answer test built from a template: it reads `check1.c` (AES-128) or `check2.c` (AES-256), replaces the marker line `    // insert_func here` with a call to the backend, `fn(PT, Key, CT);`, writes the result to `test1.c` or `test2.c`, links it against the backend's object and runs it. The template's `main` encrypts a fixed plaintext with a fixed key and compares the result with the expected ciphertext.

The two templates were missing from the upstream repository, and `register` does not notice. It opens `test%d.c` for writing *before* checking that the template exists, so it produces an empty test file. The link fails with `undefined reference to 'main'`, the test is reported as `TEST FAILED`, the backend is rejected, and after eight rejections `LIB/` is empty and `gcc -shared ./LIB/*.o` has nothing to link. `start.sh` hides all of it: it sends each `register`'s output to `/dev/null` and prints `Registering Implementation …` regardless. M1 rebuilt the templates.

**(b) The aarch64 build failed in several places.** Once upstream ran on x86, building for aarch64 exposed:
- likwid compiling its x86 access layer (M5);
- a vestigial link against a committed x86 `lib_enc.so` (M9);
- f7 and f8 written with x86 AES-NI intrinsics and compiler flags (M10);
- the energy measurement, built on x86 RAPL registers that the Cortex-A53 does not have, and a failed measurement that wiped the registration state (M8, M12).

Every fix was validated natively on the board. Later, the x86 build turned out to have lost f7 and f8 along the way, and M11 put both instruction sets in one source.

---

## 3. Baseline: making upstream run at all

These modifications are needed on any architecture, x86 included.

### 🔴 M1 · Rebuild the missing known-answer test templates
`both` · `src/check1.c`, `src/check2.c` · `ddb5f5f`

> [!IMPORTANT]
> Without these two files no backend can register, on any machine (§2a). They were rebuilt from scratch around AES test vectors, verified against a reference implementation.

Each template is a small C program with three parts: the key and plaintext, a `compare()` that holds the expected ciphertext, and the marker line where `register` inserts the call to the backend.

| Template | Level | Used by | Test vector |
|---|---|---|---|
| `check1.c` | 1 — AES-128 | f1, f2, f3, f7 | key `848185df…6a698b17`, plaintext `ad40a896…d645db66` → `6f0fa916…9455d7d7` |
| `check2.c` | 2 — AES-256 | f4, f5, f6, f8 | key `603deb10…0914dff4`, plaintext `6bc1bee2…7393172a` → `f3eed1bd…3db181f8` (NIST SP 800-38A, ECB-AES256) |

Both vectors were checked against a reference AES implementation. One detail matters more than it looks: `register` finds the marker with `strncmp(string, "    // insert_func here", 23)`, so the line must start with **exactly four spaces**. With tabs or a different indentation the call is never inserted, the test compares an uninitialised buffer, and every backend fails.

<details>
<summary>🟢 <b>M2 · Make the compose file portable</b> — <code>both</code> · <code>compose-server.yml</code> · <code>ddb5f5f</code></summary>

The upstream compose file pointed to the author's machine (`build: /home/usi/scke/unified/`) and mounted an X11 display (`/tmp/.X11-unix`, `~/.Xauthority`, `DISPLAY`). The build context became `build: .`, and the X11 mounts and the `DISPLAY` variable were removed: the component has no graphical output, and neither the board nor a headless host has a display to mount.

</details>

<details>
<summary>🟢 <b>M3 · Add a <code>.dockerignore</code></b> — <code>both</code> · <code>.dockerignore</code> · <code>ddb5f5f</code></summary>

Keeps documentation, build artefacts and editor files out of the build context. It matters most on the board, where the context is sent to the Docker daemon at every `docker compose up --build` and storage is a microSD card. Since M16 object files are excluded at any depth (`**/*.o`).

</details>

---

## 4. Port modifications

Grouped by theme; within each theme the most important come first. Paths are the current ones (§4.6).

### 4.1 Build and image

### 🔴 M4 · Build for the host's architecture, with the right base for each
`both` · `Dockerfile`, `compose-server.yml` · `177bfef`

> [!IMPORTANT]
> The same `docker compose … up --build` works on the board and on x86-64, with no argument. The Dockerfile picks the base image from the architecture it is building for, and compose builds for the host.

**`Dockerfile`.** The base image on the board is a snapshot of the board's own root filesystem (§12), which exists only for arm64: on x86-64 every `RUN` would fail with `exec format error`. The base is therefore chosen from `TARGETARCH`, which BuildKit sets to the architecture being built, with one stage per architecture:

```dockerfile
ARG TARGETARCH
FROM al3monni/kria-ubuntu:22.04.5 AS base-arm64
FROM ubuntu:22.04 AS base-amd64
FROM base-${TARGETARCH} AS build-env
```

BuildKit builds only the stages the target depends on, so an x86-64 build never downloads the board image. `ubuntu:22.04` is the same release as the board image, so both architectures compile against the same toolchain and the same OpenSSL (3.0.2).

**`compose-server.yml`.** The service declares no `platform`, so compose builds and runs for the architecture of the host. Two things about this file are worth knowing:
- **The banner is the authoritative check of what was built.** The server prints `platform: debian-arm64` or `platform: debian-amd64` at start. It is the one place that reports what was *actually* built, as opposed to what was intended.
- **`command: /app/server` is ignored.** The image's `ENTRYPOINT` is `/app/start.sh`, so compose's `command` reaches `start.sh` as an argument, which it does not read. The full pipeline runs anyway.

**Validated:** on x86-64 the build log shows only the `base-amd64` stage and the banner reads `debian-amd64`; on the board, `debian-arm64`.

#### 🟡 M5 · Build likwid for ARMv8
`arm` · `Dockerfile` · `9207e41`

likwid's build ties compiler and architecture together: its default `COMPILER = GCC` means *GCC on x86*, and on aarch64 it tries to compile its x86 register-access layer. On arm64 builds the Dockerfile rewrites likwid's `config.mk` before compiling:

| Setting | Value | Why |
|---|---|---|
| `COMPILER` | `GCCARMv8` | selects the ARMv8 build and leaves out the x86 objects |
| `ACCESSMODE` | `perf_event` | the Linux interface to the ARM performance counters; the default access daemon is x86-only |
| `BUILDDAEMON`, `BUILDFREQ` | `false` | the MSR access daemon and the frequency daemon, which the ARM build does not need |

On ARM likwid can read the performance counters (cycles, instructions, caches) but no energy: that is why the board measures energy through the INA260 (M12, §9).

<details>
<summary>🟢 <b>M6 · Build likwid before copying the sources</b> — <code>both</code> · <code>Dockerfile</code> · <code>d01abde</code></summary>

Compiling likwid is the longest step of the build (about 370 s on the board). Its download and build were moved above the `COPY` of the sources, so editing a source file no longer invalidates the likwid layer: later builds reuse it and restart from the `COPY`, in seconds.

</details>

<details>
<summary>🟢 <b>M7 · Install <code>build-essential</code> instead of <code>gcc</code></b> — <code>both</code> · <code>Dockerfile</code> · <code>e5edc89</code></summary>

The Dockerfile installed `gcc` alone, which brings the compiler without the C library headers (`libc6-dev`) and without `make`. On a fuller base image these arrived as dependencies of other packages; on a minimal one the build failed. `build-essential` installs all three.

</details>

### 4.2 Registration pipeline

#### 🟡 M8 · A failed measurement no longer destroys the registration state
`both` · `src/gen.c` · `af69ca9`, `7debc15`

`header.h` is the registration manifest: for each security level it holds the number of registered backends, and for each backend its prototype. `gen.c` writes the updated version to `header1.h` and then replaces the old one. Upstream bumped the counter only if the measurement succeeded (`if (!rt)`), but ran `rm header.h; mv header1.h header.h` **unconditionally**. A failed measurement therefore replaced the manifest with an incomplete file and wiped the registration state. This surfaced on aarch64, where the first measurements could not work at all (§9).

Now the replacement happens only when the measurement succeeds. On failure `gen.c` discards `header1.h`, removes the half-registered object from `LIB/` and says so:

```c
if (!rt) {
    ...                                   // bump the counter, add the prototype
    system("rm header.h");                // only when profiling succeeded
    system("mv header1.h header.h");
} else {                                  // failed profile: nothing is registered
    fprintf(stderr, "profile failed (rt=%d): %s not registered, header.h unchanged\n", rt, app);
    remove("header1.h");
    sprintf(com1, "rm -f %s/%s.o", libf, app);
    system(com1);
}
```

A backend that cannot be measured is simply not registered. **Validated** with a stub `profile` returning 1 and then 0: on failure `header.h` keeps its counters and `LIB/` stays empty; on success the counter goes to `///1-01` and the object appears.

<details>
<summary>🟢 <b>M9 · Drop the link against a committed <code>lib_enc.so</code></b> — <code>arm</code> · <code>src/Makefile</code>, <code>src/makeclient</code> · <code>c992b51</code></summary>

Server and client were linked with `-L./LIB -l_enc` against a `lib_enc.so` committed to the repository and built for x86-64. On aarch64 `ld` skips it as incompatible and then fails with `cannot find -l_enc`. The link was never needed: every backend is reached at runtime through `dlsym`. The flag was removed; `-ldl` (for `dlopen`) and `-rdynamic` stay. The binary is identical on x86, and `start.sh` builds `lib_enc.so` at every start anyway.

</details>

### 4.3 Backends

### 🔴 M10 · Port f7 and f8 to the ARMv8 Crypto Extensions
`arm` · `backends/f7/aes128.c`, `backends/f8/aes256.c`, their Makefiles · `6fd2ea4`, `4464e76`, `dafd421`

> [!IMPORTANT]
> f7 (AES-128) and f8 (AES-256) are the two backends that use the processor's own AES instructions, the fastest and cheapest of the eight. Upstream wrote them for Intel's AES-NI; they were rewritten for the AES instructions of the Cortex-A53.

**Why they had to be rewritten.** Upstream's f7 and f8 use AES-NI intrinsics (`_mm_aesenc_si128`, `_mm_aeskeygenassist_si128`, from `<wmmintrin.h>`) and the flags `-maes -msse4.1`, which have no meaning on ARM: `gcc` rejects the flags and the header does not exist. The ARMv8 Crypto Extensions offer the same building blocks under different names, in `<arm_neon.h>`, compiled with `-march=armv8-a+crypto`.

**How they differ, and the trap in it.** Both instruction sets implement one AES round, but they split it differently:
- AES-NI's `_mm_aesenc_si128` does SubBytes, ShiftRows and MixColumns, and adds the round key **at the end**;
- ARM's `vaeseq_u8` adds the round key **at the start**, then does SubBytes and ShiftRows, and MixColumns is a separate instruction, `vaesmcq_u8`.

The round keys therefore shift by one position relative to the x86 code. A version that gets this wrong still produces plausible-looking output; only the known-answer test catches it.

**What the rewrite does.**
- **AES-128 (f7):** nine rounds of `vaesmcq_u8(vaeseq_u8(state, rk_i))`, then a final `vaeseq_u8` and an XOR (`veorq_u8`) with the last round key.
- **AES-256 (f8):** the same with thirteen full rounds and a final one.
- **Key expansion** is written in plain C, following FIPS-197, instead of translating `_mm_aeskeygenassist`: 176 bytes of round keys for AES-128, 240 for AES-256, with the extra SubWord that AES-256 applies to every word where `i % 8 == 4`.

**The silent clash that followed.** With the rewrite, f7 and f8 passed their tests in isolation but vanished from a full registration: `register` exited with 0, yet `LIB/` held six objects instead of eight. The C key expansion had brought in two global tables, `sbox` and `Rcon`, with the same names as tables already registered by f1 and f4. `register` checks for clashes with `nm --defined-only`, which lists local symbols too (`static` does not hide them), and on a clash it skips the backend without an error. The tables were renamed per backend (`f7_sbox`, `f7_rcon`, `f8_sbox`, `f8_rcon`).

The lesson applies to the whole pipeline: a zero exit status and a passing test are not enough, and the only reliable check is the number of objects in `LIB/` (§7).

**Validated:** both known-answer tests pass bit-identical on the Cortex-A53; a full registration produces eight objects, with counters `///1-04` and `///2-04`.

### 🔴 M11 · One source per backend, two instruction sets
`both` · `backends/f7/aes128.c`, `backends/f8/aes256.c`, their Makefiles · `13dd6af`

> [!IMPORTANT]
> M10 *replaced* the AES-NI code instead of adding to it, so f7 and f8 stopped building on x86-64. Each now carries both implementations and picks one when it is compiled.

On x86-64, `gcc` rejects `-march=armv8-a+crypto` and has no `<arm_neon.h>`, so a registration there produced six backends instead of eight. Each source now selects its implementation at compile time:

```c
#if defined(__x86_64__) || defined(__i386__)
#include <wmmintrin.h>        /* AES-NI */
...
#elif defined(__aarch64__)
#include <arm_neon.h>         /* ARMv8 Crypto Extensions */
...
#else
#error "f7/aes128.c needs AES instructions (AES-NI or ARMv8 CE)"
#endif
```

and each Makefile takes its flags from the machine that compiles it:

```make
ARCH         := $(shell uname -m)
ifeq ($(ARCH),aarch64)
CFLAGS       = -c -fPIC -march=armv8-a+crypto
else
CFLAGS       = -c -fPIC -maes -msse4.1
endif
```

`uname -m` gives the architecture of the machine that compiles, which is the right question here: registration compiles each backend inside the container, on the machine that will run it. `gen.c` copies the Makefile unchanged, so no other stage had to change. The x86 helper functions were made `static`: with both implementations in one file, their names would otherwise reach `lib_enc.so` twice, the same kind of clash as in M10.

**Validated:** both implementations produce the FIPS-197 vectors (`69c4e0d86a7b0430d8cdb78070b4c55a` for AES-128, `8ea2b7ca516745bfeafc49904b496089` for AES-256). Eight backends register on both architectures, and on the board f7 and f8 keep the figures of the M12 characterisation.

### 4.4 Energy measurement

### 🔴 M12 · Measure energy on the board through the INA260
`arm` · `src/ina260.h` (new), `src/profile01.c`, `src/internalprofile.c`, `src/gen.c` · `7debc15`

> [!IMPORTANT]
> The component chooses a backend from its measured time *and* energy. On x86 the energy comes from RAPL, which the Cortex-A53 does not have; without it the selection was stuck on one backend and crypto-agility did nothing. The board's INA260 power monitor now provides the energy, with a protocol built around what the sensor can and cannot resolve.

**The sensor.** The K26 module carries an INA260 power monitor, which the kernel exposes through hwmon as `ina260_u14`:

```
/sys/class/hwmon/hwmonN/name          -> ina260_u14
/sys/class/hwmon/hwmonN/power1_input  -> microwatts   (10 mW per step)
/sys/class/hwmon/hwmonN/curr1_input   -> milliamps
/sys/class/hwmon/hwmonN/in1_input     -> millivolts
```

The `hwmonN` index changes across boots, so the code finds the device **by name**. The value updates every ~2.2 ms (the INA260's default 1.1 ms conversion for current plus 1.1 ms for voltage), and it is the same figure `xmutil xlnx_platformstats -p` prints as *SOM total power*.

**What it can resolve.** It measures the whole module: processors, programmable logic and DDR. At rest the board draws about 3.05 W; one A53 core at full load adds about 0.14 W. What we want to measure is therefore about 5% of the reading, and a single AES block is far below the sensor's resolution. That dictates the protocol: nothing can be measured per operation, the workload has to run long enough, and an idle baseline has to be subtracted.

**Protocol, per backend** (the aarch64 branch of `profile01.c`):

1. start the sampler thread: one `power1_input` read every 2 ms, pinned to cpu1;
2. measure 2 s of **idle baseline**;
3. run `./internalprofile s n 3`, which loops the backend for 3 s on cpu3 and prints its own start time, end time and iteration count, so the energy is integrated over exactly the loop and not over process start-up;
4. measure 2 s of **idle baseline** again;
5. net power = run level − mean of the two baselines. Time and energy are scaled to 50 000 iterations, upstream's `ITER`, so `db.yaml` keeps the x86 format and `synthesize` needed no change.

The workload runs for a fixed *time*, not a fixed number of iterations, because the backends span three orders of magnitude (0.57 s to 145 s per 50 000 iterations): any fixed count would be too short for the fast ones or far too long for the slow ones.

**Estimator.** The power level of each window is the **median of its 250 ms block means**. A foreign process that burns power for part of a window shifts the plain mean by tens of mW; it spoils two or three blocks and leaves their median where it was. A median of the raw samples would be robust too, but it is quantised to the sensor's 10 mW step, which is most of the gap between two backends. Averaging ~125 samples per block brings the resolution well below one milliwatt.

**Quality gate.** A measurement is accepted only if the two baselines agree within 15 mW, the two halves of the run agree within 15 mW, and the net power is positive. Otherwise it is repeated, up to three times, keeping the cleanest attempt. On a quiet board the gate fires on about 2% of measurements.

**Logging.** Every attempt appends a line to `power.csv`, next to `db.yaml`, with the statistics of each window and the net power under all three estimators (mean, median, robust). `db.yaml` keeps only the robust figure.

**Characterisation.** 8 h unattended run, 283 measurements per backend, board otherwise idle, governor `performance`. These are the reference figures for this board:

| Backend | Time per 50 000 it. [s] | Net power [mW] | Energy per 50 000 it. [J] |
|---|---|---|---|
| `enc_s01_n01` | 1.4917 | 131.1 | 0.198 |
| `enc_s01_n02` | 1.3867 | 145.5 | 0.205 |
| `enc_s01_n03` | 110.27 | 139.0 | 15.6 |
| `enc_s01_n04` | 0.5736 | 145.7 | 0.085 |
| `enc_s02_n01` | 2.0313 | 130.5 | 0.267 |
| `enc_s02_n02` | 1.8161 | 144.6 | 0.267 |
| `enc_s02_n03` | 145.14 | 139.7 | 20.5 |
| `enc_s02_n04` | 0.7318 | 142.5 | 0.105 |

- **Repeatability:** one measurement has a standard deviation of 1.3–3.4 mW on net power, i.e. 1–3% on energy. Times reproduce to four digits.
- **The backends really differ:** the 14 mW gap between `n01` and `n02` is about 60 standard errors over 283 measurements.
- **Not thermal:** over 8 h, across 30.1–34.5 °C with the fan at constant speed, net power and temperature are uncorrelated (|r| ≤ 0.1).
- **Registration agrees with isolated runs:** energies measured during a full registration match isolated measurements within 2.3%.

The campaign was taken with the mean-based estimator, before the robust one; later registrations confirm each backend within a few mW.

> [!WARNING]
> **The quality gate filters disturbances asymmetrically.** A test harness that polled the container with `docker exec` every 5 s biased *every* registration low by about 45 mW (−31% on energy). A burst inside the run window makes the two halves disagree, so the attempt is retried; a burst in both baselines passes the gate and inflates the baseline that gets subtracted. The accepted attempts are therefore the ones that *underestimate*. Never poll the container during a measurement: `tools/bench_ina260.sh` waits by following the container log instead. Reproduced and fixed under controlled conditions: with polling active the old code measured 96 mW against a true 140 mW, the new one 140 mW.

**Requirements.** The container reads `/sys/class/hwmon`, which it can because `compose-server.yml` runs it privileged. For reference-grade figures the board should be otherwise idle; the timers that wake up on their own (`unattended-upgrades`, `anacron`, `dpkg-db-backup`, `logrotate`) are worth stopping during a campaign.

**Tools** (`tools/`). `ina260_test.c` checks the sampler on its own: sampling statistics, idle power and the power of one busy core. `bench_ina260.sh` runs unattended campaigns (round-robin measurements, idle tracking, a spin-loop reference, periodic full registrations) and writes a csv and a rolling summary.

What these figures can and cannot be compared with is explained in §9.

#### 🟡 M13 · Fail the x86 measurement instead of hanging
`x86` · `src/profile01.c` · `07ac3df`

On a bare-metal host with Secure Boot on, the container stopped at `Registering Implementation in ./f1` forever. The kernel's lockdown refuses the MSR writes likwid needs, and when likwid then fails to *start* its counters it exits without killing the program it had forked and paused (likwid 5.5.1). The paused child keeps the output pipe open, and `profile01.c` waits on it forever. Next to it sat a second defect: when likwid failed *before* forking, `profile` wrote a time and an energy of zero and the backend registered as if it had been measured.

`profile01.c` now runs a short probe first (`likwid-perfctr -g ENERGY -S 100ms`, which forks nothing and fails cleanly), and refuses to register a backend whose time or energy is not positive. Either way `gen.c` leaves it unregistered (M8). The Secure Boot setting itself is a host prerequisite (§5b). **Validated:** a normal registration is unchanged; with the `msr` module unloaded, registering f1 fails in seconds with an explicit message, and `header.h` and `db.yaml` stay untouched.

### 4.5 Runtime

### 🔴 M14 · Start each port on a backend of its own security level
`both` · `src/server_f.c` · `eb16eb4`

> [!IMPORTANT]
> Every transfer on the low security level was corrupted: the server decrypted AES-128 traffic with an AES-256 backend. Each port now starts on a backend of its own level.

**Symptom.** A transfer on the low level (`./client -s 0`, port 5545) wrote 10000 bytes that differed from the original from the first byte on, and the server log reported `TAG MISMATCH`. The high level (`-s 1`, port 5544) was correct. The behaviour was identical on both architectures and present in upstream: it was never a port regression, only never tested, since the old verification sent files on 5544 alone.

**Cause.** The server keeps the selected backend in one global, `volatile int mode = 98;`, and forks one process per port, so both started from 98 = `0x62` = `enc_s02_n02`, an AES-256 backend. On 5545 the client encrypts with OpenSSL's AES-128-GCM, using the first 16 bytes of the key both sides derive from the TLS session; the server decrypted with AES-256, using all 32. The keystream differs from the first block on. The server does prepare the right OpenSSL cipher for each port, but uses it only when `mode == 0`; the path through the registered backends follows `mode` alone and ignores the port.

Before the fix, moving 5545 to an AES-128 backend with the intended mechanism (`./send 5545 81`, i.e. `0x51` = `enc_s01_n01`) made the transfer byte-identical, which confirmed the cause.

**Fix.** At the top of `createserver(port)`:

```c
mode = (port == 5544) ? 0x62 : 0x52;   /* enc_s02_n02 / enc_s01_n02 */
```

Port 5544 keeps upstream's default; 5545 starts on its AES-128 counterpart, `enc_s01_n02` (f2). `send` and `synthesize` work as before.

**Validated:** both levels byte-identical on both architectures without any `send`; the log reads `Starting with enc_s02_n02` on 5544 and `Starting with enc_s01_n02` on 5545. Two related upstream weaknesses remain open (§10).

<details>
<summary>🟢 <b>M15 · Restore the round-trip input and create <code>Downloads/</code> at start</b> — <code>both</code> · <code>test/rfile</code>, <code>src/start.sh</code> · <code>f44d204</code>, <code>a7fc710</code></summary>

A clean-up of leftovers (`fec03c9`) also removed two things the runtime needs:
- **`rfile`**, the 10000-byte input of the round-trip test (§7), restored from history;
- **`Downloads/`**, where the server saves every received file. The server never creates it, and the directory existed only because a file inside it was tracked; without it every transfer was lost silently. `start.sh` now runs `mkdir -p Downloads` before starting the server.

</details>

### 4.6 Repository

#### 🟡 M16 · Clean the repository and give it a structure
`both` · repository layout, `Dockerfile`, `.dockerignore` · `655b706`, `f9b0579`, `dbd62c2`

Once both architectures were validated, the repository was reduced to what the component needs.

- **Branches.** The port branch, `al3monni-test-arm`, became `main`. The old `main` (upstream's state, `70d30c4`) and `al3monni-test` (the x86 baseline, `ddb5f5f`) were deleted; both were already in the history of the new `main`, so no commit was lost.
- **Earlier clean-ups** (`dc78a00`, `bb4ce8c`, `fec03c9`, `baf99d5`) had already stopped tracking the files that registration regenerates at every start and removed upstream's older profiler.
- **Build artefacts** (`655b706`): 14 object files, 2 static libraries and 12 x86 test binaries committed inside the backend directories, an empty stray file, and `header.h`, which `reset` rewrites at every start (now in `.gitignore`).
- **Unused sources** (`f9b0579`), none referenced by a Makefile, the Dockerfile or an `#include`: old transfer utilities, two headers nobody includes, upstream's policy file `config.txt` (nothing reads it: the policy is the arguments of `synthesize`), stray files in f1/f4, the assembly listings and original sources in f2/f5, and the benchmark, tests and debug helpers of the bitsliced library in f3/f6.
- **Layout** (`dbd62c2`):

```
src/        the component: server, client, selection, registration, measurement, start.sh, Makefiles
backends/   f1 … f8
certs/      the server's test certificate and key
tools/      bench_ina260.sh, ina260_test.c
test/       rfile
```

The container keeps the flat `/app` the pipeline expects: the Dockerfile copies each directory into `/app` instead of `COPY . .`, so `start.sh`, `register`, `gen` and the backends' `config.txt` are unchanged. `send1.c` became `send.c`. The contents of `/app` were compared file by file with the previous image before the change. **Validated** on both architectures: eight backends registered and measured, both levels byte-identical.

---

## 5. Host prerequisites

The component builds and runs natively in two places: on the board (§5a) and on a bare-metal x86-64 host (§5b). Both use the same source tree and the same compose file.

### 5a. Kria KV260

Board bring-up (flashing, serial console, networking, Docker) is described step by step in [`KRIA_KV260_DEPLOYMENT.md`](KRIA_KV260_DEPLOYMENT.md). Before building:

1. **Ubuntu 22.04 IoT** flashed to the microSD and booted. balenaEtcher's streaming decompression fails on the `.xz` image: decompress it first with `unxz -kv`, then flash the raw `.img`.
2. **Docker Engine** installed on the board.
3. **User in the `docker` group.** After `usermod -aG docker $USER` the new group does not apply to the current login: reconnect over SSH, or use `sudo` until you do.
4. **AES instructions present**, checked rather than assumed:

```bash
grep -o 'aes\|pmull\|sha1\|sha2' /proc/cpuinfo | sort -u
```

All four must appear, or f7 and f8 (M10) have no instructions to compile against.

5. **Space on the microSD.** The base image (§12) is about 2 GB compressed and several more unpacked, before the application layers.

### 5b. x86-64, bare metal

Bare metal is required to run the component on x86-64 at all: registration measures every backend and refuses one it cannot measure (M13), and WSL2, Docker Desktop and virtual machines expose no RAPL registers. In such environments the image builds, but no backend registers.

1. **Bare-metal Linux.** A dual boot or a spare machine is enough; a live USB without persistence loses Docker and the image at every reboot.
2. **Docker Engine** and the compose plugin (`docker.io`, `docker-compose-v2`), with the user in the `docker` group. On GNOME, logging out may not apply the new group; a reboot does.
3. **Secure Boot disabled.** With Secure Boot on, the kernel is in lockdown and refuses raw MSR access (M13). Check:

```bash
mokutil --sb-state                       # SecureBoot disabled
cat /sys/kernel/security/lockdown        # [none] integrity confidentiality
```

On a machine that dual-boots Windows with BitLocker or device encryption, have the recovery key ready before changing the setting: Windows will ask for it at the next boot.

4. **The `msr` module loaded**, after every boot:

```bash
sudo modprobe msr && ls /dev/cpu/0/msr
```

To load it at boot instead: `echo msr | sudo tee /etc/modules-load.d/msr.conf`.

5. **likwid reads the ENERGY group** from inside the container, before a full start:

```bash
docker compose -f compose-server.yml run --rm --entrypoint sh ssl-server \
  -c 'likwid-perfctr -f -g ENERGY -C 0 -S 1s'
```

The output must end with `Energy Core [J]` and `Energy PKG [J]` values. On AMD processors the kernel also exposes RAPL, under the name `intel-rapl`; likwid 5.5.1 recognises Zen+ (`AMD K17 (Zen+) architecture`).

---

## 6. Build and run

### 6a. On the board

```bash
git clone https://github.com/mdc-suite/myrtus-psm-edge.git
cd myrtus-psm-edge

docker compose -f compose-server.yml up --build
```

A clean build on the KV260 takes about ten minutes: ~135 s for the packages, ~370 s for likwid. Later builds reuse those layers and restart from the copy of the sources (M6).

### 6b. On x86-64

After the prerequisites of §5b, the same command, with no build argument (M4):

```bash
git clone https://github.com/mdc-suite/myrtus-psm-edge.git
cd myrtus-psm-edge

sudo modprobe msr
docker compose -f compose-server.yml up --build
```

### 6c. What a successful start looks like

- `Registering Implementation in ./f1 … ./f8`, then `Done`
- `Creating Shared Library lib_enc.so` with **no `ld` error** below it
- `platform: debian-arm64` on the board, `platform: debian-amd64` on x86-64
- **no** `Error setting socket opts: Operation not permitted`: that means the container was not started privileged, i.e. not through compose

`Registering Implementation in ./fN` is printed whatever happens, because `start.sh` discards the output of each registration. Whether all eight registered is checked in §7, not read from the log.

### 6d. Running in the background

`docker compose … up` stays attached to the terminal and shows the log, which is useful for a first check; `Ctrl-C` stops the container. Once the start looks right, run it **detached** with `-d`:

```bash
docker compose -f compose-server.yml up -d --build   # starts in the background and returns the prompt
docker logs -f Test-server                           # follow the log; Ctrl-C stops following, not the container
docker compose -f compose-server.yml down            # stop and remove the container
```

The compose file sets `restart: unless-stopped`: a detached container comes back by itself after a reboot of the host or of Docker, until it is stopped explicitly with `down`.

---

## 7. Verification

Run these against a container that has finished its start pipeline.

> [!NOTE]
> Compose calls the service `ssl-server`, and the container it creates is `Test-server`. `docker compose` subcommands take the service name; `docker exec` and `docker logs` take the container name. Mixing them up gives a "no such service/container" error.

```bash
# the binaries match the host
docker exec Test-server readelf -h /app/server | grep Machine     # AArch64 / Advanced Micro Devices X86-64

# 8 objects and the shared library
docker exec Test-server ls -la /app/LIB

# 8 exported symbols, all of type T
docker exec Test-server sh -c 'nm -D /app/LIB/lib_enc.so | grep enc_s'

# 4 backends per level
docker exec Test-server sh -c 'grep "///" /app/header.h'          # ///1-04  ///2-04

# 8 measured backends, none with zero energy
docker exec Test-server sh -c 'grep -c "^name" /app/db.yaml; grep -c "energy: 0.000000" /app/db.yaml'   # 8, 0

# the server is listening
docker exec Test-server sh -c 'lsof -i -P -n | grep LISTEN'       # *:5544, *:5545
```

### End-to-end round trip

The test that proves correctness is a file transfer compared byte for byte, **on both security levels**. `rfile` (10000 bytes, `test/rfile` in the repository) exists for this purpose.

| Level | Client | Port | Cipher |
|---|---|---|---|
| high | `-s 1` | 5544 | AES-256-GCM |
| low | `-s 0` | 5545 | AES-128-GCM |

```bash
for p in "1 5544" "0 5545"; do set -- $p
  docker exec Test-server sh -c "cd /app && rm -f Downloads/*; ./client -s $1 -i 127.0.0.1:$2 -f rfile 2>&1 | tail -1; sleep 3; for f in Downloads/*; do cmp rfile \$f && echo IDENTICAL -s $1 port $2; done"
done
docker logs Test-server 2>&1 | grep -E "Starting with|MISMATCH"
```

Expected, for each level: `Entire File Sent 10016 bytes` (10000 bytes of payload plus the 16-byte authentication tag) and `IDENTICAL`; in the log, `Starting with enc_s02_n02` and `Starting with enc_s01_n02`, and no `TAG MISMATCH`.

Three things to know about this test:
- **`-s` accepts only `0` and `1`.** Any other value leaves the client without a cipher, and the server reports `TAG MISMATCH`.
- **Run the transfers one at a time.** The server saves each file as `Downloads/filename-ekm<N>`, with `<N>` drawn from `rand()`. The two server processes seed it in the same second and draw the same sequence of names, so simultaneous transfers on the two ports can end up in the same file.
- **Only `cmp` counts.** `Entire File Sent` says nothing about what the server did with the data, the server's own "Received N bytes" line leaves out the last partial chunk, and the server keeps a file even when its tag does not verify (§10).

### Reference results

| Check | Expected | Kria KV260 | x86-64 (bare metal) |
|---|---|---|---|
| OpenSSL banner | `platform: debian-arm64` / `debian-amd64` | ✅ | ✅ |
| `LIB/` | 8 × `enc_s0*.o` and `lib_enc.so` | ✅ | ✅ |
| `nm -D` | `enc_s01_n01` … `enc_s02_n04`, all `T` | ✅ | ✅ |
| `header.h` | `///1-04`, `///2-04` | ✅ | ✅ |
| Registration f1–f8 | no symbol clash, no KAT failure | ✅ | ✅ |
| `db.yaml` | 8 entries, no zero energy | ✅ INA260 | ✅ RAPL |
| Round trip, high (`-s 1`, 5544) | `10016 bytes`, `cmp` identical | ✅ | ✅ |
| Round trip, low (`-s 0`, 5545) | `10016 bytes`, `cmp` identical | ✅ since M14 | ✅ since M14 |
| AES instructions in `/proc/cpuinfo` | `aes pmull sha1 sha2` | ✅ | n/a |

The x86-64 column was validated on an AMD Ryzen 5 3500U (Zen+) with the prerequisites of §5b.

---

## 8. Selection mechanism and implementation map

One byte, the **mode**, encodes the choice (`encrypt02.c`):

```c
#define fbits(y)  (((y) & 0xc0) >> 6)   // function class
#define sbits(y)  (((y) & 0x30) >> 4)   // security level
#define ibits(y)   ((y) & 0x0f)         // implementation index
// fetch(mode): slevel = sbits(mode); num = ibits(mode);
//             sprintf(buf,"enc_s%02d_n%02d",slevel,num);
//             op = (function) dlsym(cx->handle, buf);
```

For example 98 = `0x62` = `01 10 0010` → encryption, level 2, index 2 → **`enc_s02_n02`** (f5). Each port starts on a backend of its own level (M14): 5544 on `0x62` (`enc_s02_n02`, f5), 5545 on `0x52` (`enc_s01_n02`, f2).

### Algorithm and implementation

Two choices are made in two different places, and keeping them apart explains most of what the component does.

- **The security level is the algorithm**, fixed per port and chosen by the client with `-s`: `1` connects to 5544 and encrypts with AES-256-GCM, `0` connects to 5545 and encrypts with AES-128-GCM (`cltest.c`). The client always uses OpenSSL and knows nothing about the mode.
- **The mode picks the implementation**, on the server only. Every backend computes one AES block (a key and 16 bytes in, 16 bytes out); the GCM mode around it (counter, GHASH, tag) is written once in `encrypt02.c` and calls the backend block by block. With `mode == 0` the server uses OpenSSL's GCM instead.

Because all the backends of a level compute the same function, any of them works with the client: changing implementation is invisible on the wire, changing level is not. That is what makes the switch safe at runtime: `dec_update` looks up the backend from the mode for every 1024-byte chunk, so a new mode applies even in the middle of a transfer. Key and IV come from the TLS session on both sides (`SSL_export_keying_material`, 64 bytes: key 0–31, IV 32–47); nothing about the cipher is negotiated beyond the TLS handshake itself.

### Choosing and applying a backend

`synthesize -f e -s <level> -t <0|1|2> -e <0|1|2>` reads `db.yaml`, keeps the backends of that level, normalises their time and energy between minimum and maximum, and picks the one closest to the requested point (0 = minimum, 1 = middle, 2 = maximum). It then calls `./send <5544 + 2 − level> <64 + 16·level + index>`. `send` finds the process listening on that port with `lsof` and sends it `SIGUSR1` carrying the value, which the signal handler writes into `mode`.

Port and level come from the same number, so `synthesize` never selects across levels; a `send` issued by hand can (§10). Nothing runs `synthesize` automatically: today the selection is a manual step.

With real measurements in `db.yaml` (M12), all four backends of each level are reachable: `n04` at `-t 0 -e 0` and `n03` at `-t 2 -e 2`. The off-diagonal policies ("fast but expensive") are physically contradictory on the board, where energy is time multiplied by a nearly constant power; `synthesize` then returns the nearest point, `n01` or `n02`, which sit within ~5% of each other.

### Implementation map

The backend directories are under `backends/`.

| Dir | Function | Build flags (aarch64 / x86-64) | Symbol | Notes |
|---|---|---|---|---|
| f1 | `aes128` | plain C | `enc_s01_n01` | reference AES-128 |
| f2 | `AES_enc` | plain C | `enc_s01_n02` | initial backend of port 5545 (M14) |
| f3 | `aes_ecb_encrypt` | `-DUNROLL_TRANSPOSE` | `enc_s01_n03` | bitsliced, from [bitsliced-aes](https://github.com/conorpp/bitsliced-aes) |
| f7 | `aes128` | `-march=armv8-a+crypto` / `-maes -msse4.1` | `enc_s01_n04` | AES instructions, both sets in one source (M10, M11) |
| f4 | `aes256` | plain C | `enc_s02_n01` | reference AES-256 |
| f5 | `AES256_enc` | plain C | `enc_s02_n02` | upstream's default, initial backend of port 5544 |
| f6 | `aes256_ecb_encrypt` | `-DUNROLL_TRANSPOSE` | `enc_s02_n03` | bitsliced, adapted to AES-256 |
| f8 | `aes256` | `-march=armv8-a+crypto` / `-maes -msse4.1` | `enc_s02_n04` | AES instructions, both sets in one source (M10, M11) |

Registration order **is** the numbering, so this table is a contract, not a description. The client does not depend on it; what does is everything that names a backend by number: the initial modes in `server_f.c` (M14), any value passed to `send` by hand, and this table. Reordering the `register` calls in `start.sh` keeps every transfer correct, because the level does not change, but silently changes which implementation a given mode selects.

Eight implementations whose internal function names were originally identical can share one library because `gen.c` wraps each of them: `#define <fn> enc_sXX_nYY`, `#include` of the source, `gcc -E -P` into a fully preprocessed `source.c`, and the renamed object goes into `LIB/`. Global tables survive preprocessing untouched, which is why f7 and f8 needed their own table names (M10).

On the board, f7 and f8 are the two backends that use the A53's AES instructions; the other six are portable C. That split is exactly what the energy measurement (§9) makes visible.

---

## 9. Energy measurement

**The principle: every platform is measured with the finest instrument it offers.** This is a deliberate choice, not a compromise waiting for a fix. The two platforms offer different instruments, so they measure different quantities.

|  | x86-64 | Kria KV260 |
|---|---|---|
| **Instrument** | RAPL registers, read by `likwid-perfctr -g ENERGY` | INA260 power monitor on the module, read through hwmon (M12) |
| **Scope** | the processor cores (`Energy Core`) | the whole module: processors, programmable logic, DDR |
| **Method** | energy counter read around the run | power sampled every 2 ms and integrated, idle baseline subtracted |
| **Recorded in `db.yaml`** | core energy per 50 000 iterations | net energy over idle per 50 000 iterations |

**Why not likwid on the board.** likwid's `ENERGY` group is defined on x86 RAPL registers. The Cortex-A53's performance monitoring unit has no energy counter, and on ARM likwid can only read what `perf_event` exposes. On this module the INA260 is the only source of energy data.

### Consequences

These follow from the choice and from the hardware; they are properties of the measurement, not defects.

- **Joules are not comparable across platforms.** Core energy on x86-64 and module energy on the board are different quantities. What *is* comparable is the ranking of the backends, and it agrees: the AES-instruction backends (`n04`) are the fastest and cheapest on both, the bitsliced ones (`n03`) the slowest and most expensive.
- **The selection is not affected.** `synthesize` normalises time and energy among the backends of one level, on one machine, so each platform selects on its own consistent figures.
- **The board resolves differences down to about 1%.** A single measurement carries 1.3–3.4 mW of noise on a ~140 mW signal. Backends closer than that are not ranked reliably: `enc_s02_n01` and `enc_s02_n02`, 0.1% apart, alternate between registrations. That is the honest answer of the sensor.
- **The board measures a delta against idle.** Whatever else runs on the module lands in the measurement; the estimator and the quality gate (M12) defend against it, and a quiet board gives the best figures.
- **The quality gate threshold is a parameter.** At 15 mW it accepts a slow drift of the baseline during a measurement: in one registration the two baselines differed by 11 mW, which biased that backend about 7% low. A threshold of 8–10 mW would catch it, at the cost of more repeated measurements. 15 mW is the chosen trade-off.

---

## 10. Status: resolved and open points

### Resolved by the port

- **Upstream runs at all**: the missing known-answer test templates were rebuilt, and `LIB/` fills on any architecture (M1).
- **The aarch64 build works**, natively on the board (M5, M9, M10).
- **f7 and f8 are complete on both architectures**, with the processor's AES instructions on each (M10, M11), and no longer disappear silently from the registration (M10).
- **A failed measurement no longer wipes the registration state** (M8).
- **Energy is measured on the board**, through the INA260, and drives the selection: before, the selection was stuck on one backend (M12).
- **x86-64 runs on bare metal** with the same build command as the board, and measures energy through RAPL (M4, §5b).
- **The low security level is no longer corrupted** (M14).
- **The x86 measurement no longer hangs** when likwid cannot start its counters, and no longer registers zeros when it cannot measure (M13).
- **The round-trip test and the received-files directory are back** (M15).

### Open points

All secondary: none affects normal operation.

1. **Small upstream fixes.**
   - `register.c`: initialise `bool rval = 0;`; return a non-zero status when `collide()` refuses a backend, so a skipped backend is not reported as a success (M10); check that the `check%d.c` template exists *before* opening `test%d.c` for writing (§2a).
   - `start.sh`: keep the standard error of `register` instead of discarding it, so a refused registration, M13's messages included, reaches the container log.
   - `cltest.c`: reject any `-s` other than `0` and `1` (§7).
2. **`send` accepts a mode of the wrong level.** The signal handler writes any value into `mode`: `./send 5545 98` moves the low-level port to an AES-256 backend and reproduces exactly the failure M14 removed. `synthesize` never does this (§8). The handler could refuse a mode whose level does not match its port; that needs the port's level in a global, since `port` is local to `createserver`.
3. **Decrypted data is written before the tag is checked.** The server decrypts chunk by chunk and writes each one as it goes; the authentication tag is verified only at the end, in `dec_final`. On a mismatch it prints `TAG MISMATCH`, keeps the file, and does not tell the client. With the right level (M14) the tag verifies and the file is correct, but an authenticated cipher should never release data it has not authenticated. Writing to a temporary name and renaming only after a successful `dec_final`, or deleting the file on a mismatch, would close it.
4. **Test certificate.** `certs/certfile.crt` is self-signed and valid until 18 January 2027. The client does not verify it, so its expiry will not break transfers, but the client does not authenticate the server either. The fix is a certificate the client actually checks; renewing this one only moves the date.

---

## 11. Commit map

Each modification with the commits that implement it. `git log --oneline main` gives the full chronological history.

| Entry | Commits |
|---|---|
| Upstream state | `70d30c4` |
| 🔴 M1 · Known-answer test templates | `ddb5f5f` |
| 🟢 M2 · Portable compose file | `ddb5f5f` |
| 🟢 M3 · `.dockerignore` | `ddb5f5f` |
| 🔴 M4 · Build for the host's architecture | `177bfef` |
| 🟡 M5 · likwid for ARMv8 | `9207e41` |
| 🟢 M6 · likwid before the sources | `d01abde` |
| 🟢 M7 · `build-essential` | `e5edc89` |
| 🟡 M8 · Registration state protected | `af69ca9`, `7debc15` |
| 🟢 M9 · No link against `lib_enc.so` | `c992b51` |
| 🔴 M10 · f7/f8 on ARMv8 Crypto Extensions | `6fd2ea4`, `4464e76`, `dafd421` |
| 🔴 M11 · f7/f8 on both instruction sets | `13dd6af` |
| 🔴 M12 · Energy through the INA260 | `7debc15` |
| 🟡 M13 · x86 measurement fails instead of hanging | `07ac3df` |
| 🔴 M14 · Each port on its own level | `eb16eb4` |
| 🟢 M15 · `rfile` and `Downloads/` | `f44d204`, `a7fc710` |
| 🟡 M16 · Repository cleanup and layout | `dc78a00`, `bb4ce8c`, `fec03c9`, `baf99d5`, `655b706`, `f9b0579`, `dbd62c2` |
| Base image snapshot (§12) | `66baa82` |

---

## 12. Base image

On aarch64 the container is built on **[`al3monni/kria-ubuntu:22.04.5`](https://hub.docker.com/r/al3monni/kria-ubuntu)**, a snapshot of the board's own root filesystem published on Docker Hub (`linux/arm64/v8`, ~2 GB compressed). On x86-64 it is built on stock `ubuntu:22.04`, the same release. The Dockerfile chooses between them from the architecture being built (M4).

### Why a snapshot of the board

Stock Ubuntu 22.04 and AMD's Ubuntu 22.04 IoT image for Kria are the same distribution with a different userspace: the Kria image carries board-specific tooling, such as `xmutil` and the platform-statistics utilities, useful for checking the power sensor (M12). Building on a snapshot of the board also makes the container independent of whatever happens to be installed on the board at build time, so the toolchain survives a reflash.

### How the image is produced

The root filesystem is archived from a **freshly flashed and fully upgraded board, before Docker is installed**. The order matters: a board with Docker already running carries an image store that would otherwise end up inside the snapshot.

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

Three details are easy to get wrong, and each cost a build cycle:

- **Exclude the contents, not the directory.** `--exclude='./tmp/*'` keeps `/tmp` as an empty directory; `--exclude='./tmp'` drops it entirely. The first version of the image was built the second way and had no `/tmp` and no `/run`. The failure shows up far from its cause: `apt-get update` reports a wall of GPG signature errors, because apt cannot create its temporary files. The real message is the last line: `Unable to mkstemp /tmp/... (2: No such file or directory)`.
- **Exclusion paths must match the archive form.** With `-C / .` tar writes relative paths (`./sys/...`), so `--exclude=/sys/*` never matches and the whole of `/sys` gets archived. Quote the patterns, so the shell does not expand them before tar sees them.
- **`./configfs` is specific to the Kria.** The device-tree overlay interface is mounted at the root on this platform, and must be excluded like any other kernel filesystem.

Verify before publishing:

```bash
tar -tf kria-rootfs.tar './tmp/' './run/'        # both must be listed
tar -tf kria-rootfs.tar | grep -c '^./sys/'      # must be 0 or 1
docker inspect al3monni/kria-ubuntu:22.04.5 --format '{{.Architecture}}'   # arm64
docker run --rm al3monni/kria-ubuntu:22.04.5 sh -c 'ls -ld /tmp /run && apt-get update'
```

The last line is the real test: it exercises exactly the path that failed on the first attempt.

### Using it

Treat the snapshot as a frozen, versioned base. Every application build step belongs in the tracked `Dockerfile` on top of it; nothing gets baked into the snapshot, or the build stops being reproducible from source. The tag carries the Ubuntu point release, so a future refresh gets a new tag instead of silently replacing this one.

> **Provenance.** Derived from AMD/Xilinx's Ubuntu 22.04 IoT image for Kria; contains Canonical- and AMD-licensed components, redistributed under their respective terms.
