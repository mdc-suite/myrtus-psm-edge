# Porting logbook — myrtus-psm-edge

This logbook records the modifications that took **spdocker** from an x86-only prototype that did not run out of the box to a component validated on the **AMD/Xilinx Kria KV260** and on a **bare-metal x86-64** host. It explains what broke, why, and what was changed. It is not a usage guide: installation, build and validation instructions live in [`README.md`](README.md), and board bring-up in [`KRIA_KV260_DEPLOYMENT.md`](KRIA_KV260_DEPLOYMENT.md).

|  |  |
|---|---|
| **Repository** | https://github.com/mdc-suite/myrtus-psm-edge |
| **Upstream** | https://github.com/subhadeep-banik/spdocker, by Subhadeep Banik |
| **Target hardware** | Kria KV260 — Zynq UltraScale+ MPSoC, 4× Cortex-A53, aarch64, Ubuntu 22.04 IoT |
| **Hosts** | the board itself (native aarch64 build) · bare-metal x86-64 Linux (native amd64 build) |
| **Base image** | [`al3monni/kria-ubuntu:22.04.5`](https://hub.docker.com/r/al3monni/kria-ubuntu) on aarch64, `ubuntu:22.04` on x86-64, chosen automatically (§12) |
| **Status** | ✅ Validated on both architectures at `c0c0065`: eight backends registered and measured, byte-identical transfers on both security levels for files of every size, runtime switch working |

### How to read the modifications

Every modification in §3 and §4 carries a number (`M1` … `M20`), a level and the architecture it concerns.

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
4. [Port modifications](#4-port-modifications) — M4 … M20
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

*Superseded by M19: once the board measured through the INA260 (M12), likwid was no longer needed there, and it is no longer built on aarch64. Kept for the record.*

likwid's build ties compiler and architecture together: its default `COMPILER = GCC` means *GCC on x86*, and on aarch64 it tries to compile its x86 register-access layer. On arm64 builds the Dockerfile rewrites likwid's `config.mk` before compiling:

| Setting | Value | Why |
|---|---|---|
| `COMPILER` | `GCCARMv8` | selects the ARMv8 build and leaves out the x86 objects |
| `ACCESSMODE` | `perf_event` | the Linux interface to the ARM performance counters; the default access daemon is x86-only |
| `BUILDDAEMON`, `BUILDFREQ` | `false` | the MSR access daemon and the frequency daemon, which the ARM build does not need |

On ARM likwid can read the performance counters (cycles, instructions, caches) but no energy: that is why the board measures energy through the INA260 (M12, §9).

<details>
<summary>🟢 <b>M6 · Build likwid before copying the sources</b> — <code>both</code> · <code>Dockerfile</code> · <code>d01abde</code></summary>

Compiling likwid is the longest step of the build (about 370 s on the board). Its download and build were moved above the `COPY` of the sources, so editing a source file no longer invalidates the likwid layer: later builds reuse it and restart from the `COPY`, in seconds. Since M19 likwid is built on x86-64 only.

</details>

<details>
<summary>🟢 <b>M7 · Install <code>build-essential</code> instead of <code>gcc</code></b> — <code>both</code> · <code>Dockerfile</code> · <code>e5edc89</code></summary>

The Dockerfile installed `gcc` alone, which brings the compiler without the C library headers (`libc6-dev`) and without `make`. On a fuller base image these arrived as dependencies of other packages; on a minimal one the build failed. `build-essential` installs all three.

</details>

<details>
<summary>🟢 <b>M19 · Build likwid on x86-64 only</b> — <code>arm</code> · <code>Dockerfile</code> · <code>52c6544</code></summary>

Since M12 the board measures energy through the INA260, and `profile01.c` calls `likwid-perfctr` only on x86-64; on aarch64 likwid was downloaded and compiled (M5) but never run. The Dockerfile now builds it only when the target is not `arm64`. This takes about 370 s (M6) off a clean build on the board, and with them its dependence on likwid's download server; the x86-64 build is unchanged. Checked with a clean build on both architectures (§6a): on the board the step now takes 1.4 s, on x86-64 it builds likwid as before, and `test/test.sh -all` passes on both, with energy on x86-64 still measured through likwid.

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

**Validated:** both implementations produce the FIPS-197 vectors (`69c4e0d86a7b0430d8cdb78070b4c55a` for AES-128, `8ea2b7ca516745bfeafc49904b496089` for AES-256). Eight backends register on both architectures, and on the board f7 and f8 measure the same time and energy as before the change.

### 4.4 Energy measurement

### 🔴 M12 · Measure energy on the board through the INA260
`arm` · `src/ina260.h` (new), `src/profile01.c`, `src/internalprofile.c`, `src/gen.c` · `7debc15`

> [!IMPORTANT]
> The component chooses a backend from its measured time *and* energy. On x86 the energy comes from RAPL, which the Cortex-A53 does not have; without it the selection was stuck on one backend and crypto-agility did nothing. The board's INA260 power monitor now provides the energy, with a protocol built around what the sensor can resolve.

**The sensor.** The K26 module carries an INA260 power monitor, exposed by hwmon as `ina260_u14` (`/sys/class/hwmon/hwmonN/power1_input`, in microwatts, 10 mW per step, updated every ~2.2 ms). The `hwmonN` index changes across boots, so the code finds the device by name. The sensor measures the whole module: at rest it reads about 3.05 W, and one A53 core at full load adds only about 0.14 W. A single AES block is far below that resolution, so the backend has to run long enough, and an idle baseline has to be subtracted.

**Protocol, per backend** (the aarch64 branch of `profile01.c`). A sampler thread reads the sensor every 2 ms. It measures 2 s of idle, then 3 s of the backend looping on its own core (`internalprofile` reports the exact start and end of the loop), then 2 s of idle again. Net power is the run level minus the mean of the two baselines. Time and energy are scaled to 50 000 iterations, upstream's unit, so `db.yaml` keeps the x86 format and `synthesize` needed no change. The run lasts a fixed *time* rather than a fixed number of iterations because the backends span three orders of magnitude, from 0.57 s to 145 s per 50 000 iterations.

**Robustness.**
- *Estimator:* each window's level is the median of its 250 ms block means. A burst from another process spoils a few blocks without moving the median, and averaging within blocks keeps a resolution well below the sensor's 10 mW step.
- *Quality gate:* a measurement is accepted only if the two baselines, and the two halves of the run, agree within 15 mW; otherwise it is repeated, up to three times. Every attempt is logged in `power.csv`, next to `db.yaml`.

**Validation.** Before the protocol was used for selection, an 8-hour unattended campaign (283 measurements per backend) checked that its figures can be trusted: one measurement varies by 1–3% on energy, the differences between backends are real down to about 1%, temperature plays no role, and figures taken during a normal registration match isolated ones within 2.3%. The limits this leaves are discussed in §9.

> [!WARNING]
> **Do not poll the container while it measures.** A harness running `docker exec` every 5 s biased every measurement about 30% low: bursts in the baselines pass the gate and inflate the subtracted idle, while bursts in the run are rejected. `tools/bench_ina260.sh` waits by following the container log instead.

**Requirements and tools.** The container reads `/sys/class/hwmon` because compose runs it privileged, and a quiet board gives the best figures. `tools/ina260_test.c` checks the sensor on its own; `tools/bench_ina260.sh` runs unattended measurement campaigns.

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

<details>
<summary>🟢 <b>M20 · Stop the container at once</b> — <code>both</code> · <code>compose-server.yml</code> · <code>6890ce2</code></summary>

`docker compose down`, or Ctrl-C on an attached `up`, took 10 s and ended with `exited with code 137`. In a container the first process, PID 1, is `start.sh`, and Linux does not apply the default action of a signal to a PID 1 that has no handler for it: neither `start.sh` nor the server handles `SIGTERM`, so the stop request was ignored until Docker gave up after 10 s and sent `SIGKILL` (137 = 128 + 9). Nothing was lost, since every start rebuilds the state, but the stop was slow and looked like a crash. The behaviour came from upstream.

`init: true` in the compose file makes Docker's own minimal init (`docker-init`) PID 1. It passes `SIGTERM` on to `start.sh`, which is no longer PID 1 and terminates; the container stops at once with code 143 (128 + 15), the usual code of a container stopped with `SIGTERM`. Checked with Docker's `docker-init` in a separate PID namespace: a shell that waits on a foreground child, as `start.sh` waits on the server, ignored `SIGTERM` as PID 1 and stopped in 4 ms under `docker-init`.

</details>

### 4.6 Repository

#### 🟡 M16 · Clean the repository and give it a structure
`both` · repository layout, `src/`, `Dockerfile`, `.dockerignore` · `655b706`, `f9b0579`, `dbd62c2`, `2a464e4` … `f374710`

Once both architectures were validated, the repository was reduced to what the component needs.

- **Branches.** The port branch, `al3monni-test-arm`, became `main`. The old `main` (upstream's state, `70d30c4`) and `al3monni-test` (the x86 baseline, `ddb5f5f`) were deleted; both were already in the history of the new `main`, so no commit was lost.
- **Earlier clean-ups** (`dc78a00`, `bb4ce8c`, `fec03c9`, `baf99d5`) had already stopped tracking the files that registration regenerates at every start and removed upstream's older profiler.
- **Build artefacts** (`655b706`): 14 object files, 2 static libraries and 12 x86 test binaries committed inside the backend directories, an empty stray file, and `header.h`, which `reset` rewrites at every start (now in `.gitignore`).
- **Unused sources** (`f9b0579`), none referenced by a Makefile, the Dockerfile or an `#include`: old transfer utilities, two headers nobody includes, upstream's policy file `config.txt` (nothing reads it: the policy is the arguments of `synthesize`), stray files in f1/f4, the assembly listings and original sources in f2/f5, and the benchmark, tests and debug helpers of the bitsliced library in f3/f6.
- **Dead code in the sources** (`2a464e4`, `b1bd298`, `286472e`, `e297cdd`, `4b0a988`, `f374710`): unused functions, variables, macros and includes, commented-out code, the server's debug signal and the unused `cycles.h`, without changing what the component does. The client no longer links `encrypt02.c`, and the Makefiles keep only the flags they use. Each step was checked with the round trip and the runtime switch on both levels; the compiled code of `gen`, `register` and `synthesize` is unchanged, and `send` and `reset` now exit with 0.
- **Layout** (`dbd62c2`):

```
src/        the component: server, client, selection, registration, measurement, start.sh, Makefiles
backends/   f1 … f8
certs/      the server's test certificate and key
tools/      bench_ina260.sh, ina260_test.c
test/       rfile
```

The container keeps the flat `/app` the pipeline expects: the Dockerfile copies each directory into `/app` instead of `COPY . .`, so `start.sh`, `register`, `gen` and the backends' `config.txt` are unchanged. `send1.c` became `send.c`. The contents of `/app` were compared file by file with the previous image before the change. **Validated** on both architectures: eight backends registered and measured, both levels byte-identical.

### 4.7 Transfers

#### 🟡 M17 · Decrypt files of every size
`both` · `src/server_f.c`, `src/cltest.c` · `7440efc`, `ee3af93`

The client sends the ciphertext in 1024-byte records, followed by the 16-byte tag. The server cannot tell which record is the last, so it writes the first 1008 bytes of each full record at once and holds back the last 16, which may be the tag, until the next record arrives. Two sizes slipped through, both inherited from upstream and never seen because `rfile` is neither:
- **File size + 16 a multiple of 1024** (1008, 2032, … bytes). The tag fills the end of the last full record, and the closing branch wrote its decryption to the file and left its block in the GHASH: 16 extra bytes and `TAG MISMATCH`. The server now drops that block from the message instead.
- **Empty file.** The client started `outlen` at 1024 and sent 1024 uninitialised bytes before the tag, and the server trimmed a previous record that did not exist. The client now starts at 0, and the server trims only after a record.

**Validated** with 18 sizes from 0 bytes to 1 MiB, every boundary around 1024 and 2048 included, on both levels: on both architectures, 36 of 36 transfers byte-identical and without `TAG MISMATCH`. On x86-64, where the sizes were first run, 30 passed before the fix, and a client that alters one byte of the data or of the tag is flagged in all 36.

#### 🟡 M18 · Make the OpenSSL path (mode 0) work
`both` · `src/server_f.c` · `7440efc`

`./send <port> 0` switches a port from the registered backends to OpenSSL's AES-GCM. Only a manual `send` reaches it: `synthesize` never produces 0, and both ports start on a backend (M14). On that path `rfile` arrived as 9856 bytes, with 16 bytes missing from the end of every 1024-byte record, and the tag was never checked. Three defects, all from upstream:
- the record counter was never incremented, so the 16 bytes held back from each record were never written;
- the last record was decrypted together with its tag;
- the expected tag was never passed to OpenSSL (`EVP_CTRL_GCM_SET_TAG`), and the result of `EVP_DecryptFinal`, a failure every time, was ignored.

`encrypt02.c` can take a block back out of its GHASH (M17); OpenSSL cannot. This path therefore holds the last 16 bytes back still encrypted, and decrypts them only once the next record shows they are data. It passes the tag before `EVP_DecryptFinal` and reports `TAG MISMATCH` like the backend path.

**Validated** with the sizes of M17: on both architectures, 36 of 36 transfers byte-identical, all decrypted by OpenSSL. On x86-64, 18 passed before the fix, and a tampered transfer is flagged in 36 of 36, against none before.

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

The same steps on both architectures, after the prerequisites of §5. The README, *Build and run*, walks through them one by one.

```bash
git clone https://github.com/mdc-suite/myrtus-psm-edge.git
cd myrtus-psm-edge
sudo modprobe msr                                     # x86-64 only, after every boot (§5b)
docker compose -f compose-server.yml up -d --build    # build, then start in the background
docker logs -f Test-server                            # follow the start; Ctrl-C stops following, not the container
test/test.sh -all                                     # §7
docker compose -f compose-server.yml down             # stop and remove the container
```

The compose file sets `restart: unless-stopped`: the container comes back by itself after a reboot of the host or of Docker, until it is stopped with `down`, which returns at once since M20.

### 6a. Build time

Measured on 2 October 2026, at `d83cf3c`:

| Step | Kria KV260 | x86-64 (Ryzen 5 3500U) |
|---|---|---|
| Base image: download | 107 s (2.07 GB) | already present (30 MB) |
| Base image: unpack | 440 s | |
| Packages (`apt-get`) | 318 s | 60 s |
| likwid | not built: 1.4 s (M19) | 81 s |
| Component (`gcc`, `make`) | ~20 s | ~3 s |
| Export of the image | 77 s | 27 s |
| **Total** | **969 s, ~16 min** | **173 s, ~3 min** |

On the board the build started from an empty Docker (`docker system prune -a`): unpacking the base image onto the microSD is the longest step. On x86-64 it was `docker compose build --no-cache` with the base image already present; downloading `ubuntu:22.04` adds a few seconds. The packages depend on the network, which on the board goes through Windows ICS. Later builds reuse the cached layers and restart from the copy of the sources, in seconds (M6).

### 6b. What a successful start looks like

- `Registering Implementation in ./f1 … ./f8`, then `Done`
- `Creating Shared Library lib_enc.so` with **no `ld` error** below it
- `platform: debian-arm64` on the board, `platform: debian-amd64` on x86-64
- **no** `Error setting socket opts: Operation not permitted`: that means the container was not started privileged, i.e. not through compose

`Registering Implementation in ./fN` is printed whatever happens, because `start.sh` discards the output of each registration. Whether all eight registered is checked in §7, not read from the log.

---

## 7. Verification

Run these against a container that has finished its start pipeline. `test/test.sh` runs them on the target and reports, for each, the command, the expected and the obtained result (README, *Tests*): with no option the build checks and the round trip, with `-rapid` the build checks only, with `-all` also the runtime switch and the file sizes of M17 and M18. The commands below are the same checks by hand.

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
| Runtime switch (`synthesize -s 1 -t 0 -e 0`, then a transfer on 5545) | `Switching to enc_s01_n04`, `Starting with enc_s01_n04`, `cmp` identical | ✅ | ✅ |
| File sizes (18 sizes from 0 B to 1 MiB, both levels, backends and OpenSSL GCM) | 72 transfers identical, no `TAG MISMATCH` | ✅ since M17, M18 | ✅ since M17, M18 |
| AES instructions in `/proc/cpuinfo` | `aes pmull sha1 sha2` | ✅ | n/a |

The x86-64 column was validated on an AMD Ryzen 5 3500U (Zen+) with the prerequisites of §5b. The commands of the runtime-switch test are in the README, under *Tests*.

---

## 8. Selection mechanism and implementation map

One byte, the **mode**, encodes the choice: two bits of function class, two of security level, four of implementation index. `synthesize` sets the class (`01`, encryption); the server reads only the level and the index (`encrypt02.c`):

```c
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
- **The mode picks the implementation**, on the server only. Every backend computes one AES block (a key and 16 bytes in, 16 bytes out); the GCM mode around it (counter, GHASH, tag) is written once in `encrypt02.c` and calls the backend block by block. With `mode == 0` the server uses OpenSSL's GCM instead (M18).

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
- **Files of every size arrive intact**, and the OpenSSL path (mode 0) works and checks the tag (M17, M18).
- **The board no longer builds likwid**, which it never ran (M19).
- **The container stops at once**, instead of being killed after 10 s (M20).

### Open points

All secondary: none affects normal operation.

1. **Small upstream fixes.**
   - `register.c`: initialise `bool rval = 0;`; return a non-zero status when `collide()` refuses a backend, so a skipped backend is not reported as a success (M10); check that the `check%d.c` template exists *before* opening `test%d.c` for writing (§2a).
   - `start.sh`: keep the standard error of `register` instead of discarding it, so a refused registration, M13's messages included, reaches the container log.
   - `cltest.c`: reject any `-s` other than `0` and `1` (§7).
2. **`send` accepts a mode of the wrong level.** The signal handler writes any value into `mode`: `./send 5545 98` moves the low-level port to an AES-256 backend and reproduces exactly the failure M14 removed. `synthesize` never does this (§8). The handler could refuse a mode whose level does not match its port; that needs the port's level in a global, since `port` is local to `createserver`.
3. **Decrypted data is written before the tag is checked.** The server decrypts chunk by chunk and writes each one as it goes; the authentication tag is verified only at the end, in `dec_final` (by OpenSSL in mode 0). On a mismatch it prints `TAG MISMATCH`, keeps the file, and does not tell the client. With the right level (M14) the tag verifies and the file is correct, but an authenticated cipher should never release data it has not authenticated. Writing to a temporary name and renaming only after a successful `dec_final`, or deleting the file on a mismatch, would close it.
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
| 🟡 M16 · Repository cleanup and layout | `dc78a00`, `bb4ce8c`, `fec03c9`, `baf99d5`, `655b706`, `f9b0579`, `dbd62c2`, `2a464e4`, `b1bd298`, `286472e`, `e297cdd`, `4b0a988`, `f374710` |
| 🟡 M17 · Files of every size | `7440efc`, `ee3af93` |
| 🟡 M18 · OpenSSL path (mode 0) | `7440efc` |
| 🟢 M19 · likwid on x86-64 only | `52c6544` |
| 🟢 M20 · Stop at once (`init: true`) | `6890ce2` |
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
