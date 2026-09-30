# myrtus-psm-edge — Privacy and Security Manager (Edge Layer)

Crypto-agile TLS prototype with eight runtime-selectable AES backends, running on ARM64 edge hardware. Component of the **MYRTUS** project (Horizon Europe, Grant No. 101135183).

**Repo:** https://github.com/mdc-suite/myrtus-psm-edge
**Upstream:** https://github.com/subhadeep-banik/spdocker
**Branch:** `main` (the port branch, formerly `al3monni-test-arm`) · **x86 baseline:** `ddb5f5f`, re-validated on the current tree
**Target hardware:** AMD/Xilinx Kria KV260 — Zynq UltraScale+ MPSoC, 4× Cortex-A53, aarch64
**Cross-build host:** Windows + WSL2 (Ubuntu) + Docker Desktop, `linux/arm64` under QEMU
**On-board host:** Ubuntu 22.04 IoT, Docker Engine, native aarch64 build
**x86-64 host:** bare-metal Ubuntu, Docker Engine, native amd64 build
**Base image:** [`al3monni/kria-ubuntu:22.04.5`](https://hub.docker.com/r/al3monni/kria-ubuntu) (arm64/v8) on aarch64, `ubuntu:22.04` on x86-64 — chosen automatically
**Status:** ✅ **Validated on real silicon, on both architectures.** Builds, registers all 8 implementations, serves over TLS, and round-trips byte-identical (`cmp`) on both security levels, on the Kria KV260 and on a bare-metal x86-64 host. Energy is measured on ARM through the on-SOM INA260 and on x86-64 through RAPL, so backend selection runs on real measurements on both.

> Derived from [spdocker](https://github.com/subhadeep-banik/spdocker) by Subhadeep Banik. The complete record of every modification made to port the project from x86-64 to aarch64 is in [`LOGBOOK.md`](LOGBOOK.md).

---

## What this is

A TLS client/server that ships **eight interchangeable AES implementations**. The cipher protecting the channel is not fixed at compile time: the server picks one at runtime, loads it from a shared library through `dlopen`/`dlsym`, and uses it as the AEAD for file transfer.

That is what *crypto-agility* means here, and it is the reason this component sits in the MYRTUS edge layer as the Privacy and Security Manager: the security/performance/energy trade-off of a secure channel can be renegotiated while the node is running, rather than being frozen when the binary was built.

The eight backends span two security levels and four implementation strategies each — reference C, an alternative C implementation, a bitsliced constant-time version, and one that uses the CPU's hardware AES instructions.

## How it works

Everything happens inside one container. On start, `start.sh` runs this pipeline:

```
reset  →  register ×8  →  gcc -shared ./LIB/*.o -o ./LIB/lib_enc.so  →  ./server
```

- **`reset`** clears `LIB/` and resets the counters in `header.h`
- **`register -c ./fN/config.txt`** compiles backend `fN`, runs a known-answer test against a NIST vector, renames its internal symbols to a unique name, and drops the object into `LIB/`
- the eight objects are linked into a single **`lib_enc.so`**
- **`server`** opens that library and resolves the backend it needs by symbol name

Two choices are made in two different places:

- **The security level is the algorithm**, fixed per port and chosen by the client: `./client -s 1` connects to port 5544 and encrypts with AES-256-GCM, `-s 0` connects to 5545 and encrypts with AES-128-GCM. The client always uses OpenSSL.
- **The implementation is chosen by the server** with a single **mode byte**: two bits select the security level, four the implementation index. The server maps it to a symbol name (`enc_s02_n02` and so on), calls `dlsym`, and runs its own GCM around that AES backend. Port 5544 starts on `enc_s02_n02`, port 5545 on `enc_s01_n02`.

Every backend of a level computes the same AES, so switching implementation is invisible to the client and can happen while the server runs: `./synthesize` picks the backend that best matches a time/energy policy from the measurements in `db.yaml`, and `./send <port> <mode>` delivers the new mode byte to the server process on that port.

The order in which the eight backends are registered *is* the numbering: it decides which directory a given mode byte selects. The client does not depend on it, since it never loads `lib_enc.so`.

### What lives where

| Path | Contents |
|---|---|
| `src/server_f.c`, `src/cltest.c` | server and client |
| `src/encrypt02.c` | the GCM mode around the selected backend, `dlopen`/`dlsym` and the mode-byte decoding |
| `src/register.c`, `src/gen.c`, `src/reset.c` | registration: compile a backend, run its KAT, check for symbol clashes, give it its unique name, install the object |
| `src/check1.c`, `src/check2.c` | KAT templates (AES-128 and AES-256) that `register` fills in per backend |
| `src/profile01.c`, `src/internalprofile.c`, `src/ina260.h` | measurement: likwid and RAPL on x86-64, the INA260 on aarch64 |
| `src/synthesize.c`, `src/send.c` | pick a backend from `db.yaml`, and deliver the new mode byte to the server |
| `src/cycles.h` | portable cycle counter — x86 `rdtscp` or ARM `cntvct_el0` |
| `src/start.sh` | the container entrypoint pipeline above |
| `backends/f1` … `backends/f8` | the eight AES backends, one directory each, with their own Makefile |
| `certs/` | the server's self-signed test certificate and its key |
| `test/rfile` | the 10000-byte input of the round-trip test |
| `tools/` | `bench_ina260.sh`, unattended measurement campaigns on the board, and `ina260_test.c`, a check of the power sensor |
| `Dockerfile`, `compose-server.yml` | image definition and orchestration |
| `LIB/` | created at build, populated at runtime — never committed |

Inside the container everything is flat in `/app`: the Dockerfile copies `src/`, `backends/`, `certs/`, `test/rfile` and `tools/ina260_test.c` there, so `backends/f1` becomes `/app/f1` and `test/rfile` becomes `/app/rfile`. Every path in the commands below is a path in the container.

---

## Requirements

### On the board (recommended path)

An AMD/Xilinx Kria KV260 running Ubuntu 22.04 IoT, with Docker Engine installed. Full bring-up instructions — flashing, serial console, networking, Docker — are in [`KRIA_KV260_DEPLOYMENT.md`](KRIA_KV260_DEPLOYMENT.md).

Check that your user is in the `docker` group (`docker version` should respond without `sudo`), and that the microSD has several GB free — the base image alone is around 2 GB compressed.

### On an x86-64 host (bare metal)

Needed only to measure energy on x86-64: WSL2, Docker Desktop and virtual machines hide the RAPL registers likwid reads. On bare-metal Linux with Docker Engine:

- **Secure Boot disabled.** With Secure Boot on, the kernel refuses raw MSR access and likwid cannot start its counters; `cat /sys/kernel/security/lockdown` must read `[none]`. On a machine that dual-boots Windows with BitLocker, have the recovery key ready before changing the setting.
- **The `msr` module loaded**, after every boot: `sudo modprobe msr` (or add `msr` to `/etc/modules-load.d/`).

`LOGBOOK.md` §5c has the checks, including how to confirm from inside the container that likwid reads the `ENERGY` group.

### On an x86 dev host (cross-build)

Windows + WSL2 + Docker Desktop, with WSL integration enabled for the Ubuntu distro. Because the base image is an arm64 rootfs, you also need the QEMU binfmt handler registered once per WSL VM:

```bash
docker run --privileged --rm tonistiigi/binfmt --install arm64
```

This does not survive `wsl --shutdown` — re-run it if you get `exec format error`.

The cross-build is useful for catching compile errors without occupying the board, but everything runs emulated and a clean build is slower than on the board itself.

Every host builds for its own architecture by default, and all eight backends build on both: `f7` and `f8` carry both AES implementations and pick one at compile time, AES-NI on x86-64 and the ARMv8 Crypto Extensions on the Kria.

---

## Build and run

### On the board

```bash
git clone https://github.com/mdc-suite/myrtus-psm-edge.git
cd myrtus-psm-edge

docker compose -f compose-server.yml up --build
```

A clean first build takes roughly ten minutes on the KV260 — most of it is compiling likwid. Later builds reuse that layer and finish in seconds.

To run it detached instead, use `-d` and read the logs with `docker compose -f compose-server.yml logs`.

### On an x86-64 host

The same command as on the board, with no build argument: the Dockerfile picks `ubuntu:22.04` as the base on x86-64.

```bash
sudo modprobe msr
docker compose -f compose-server.yml up --build
```

### Cross-build on the x86 dev host

Compose builds for the host by default, so the cross-build has to ask for arm64:

```bash
docker run --privileged --rm tonistiigi/binfmt --install arm64   # once per WSL VM
DOCKER_DEFAULT_PLATFORM=linux/arm64 docker compose -f compose-server.yml up --build
```

### What a successful start looks like

```
Resetting Initial Configuration
Registering Implementation in ./f1
...
Registering Implementation in ./f8
Done
Creating Shared Library lib_enc.so
Updating Paths
OpenSSL 3.0.2 15 Mar 2022 (Library: OpenSSL 3.0.2 15 Mar 2022)
platform: debian-arm64
```

Two things to check in that output: `Creating Shared Library` is not followed by an `ld` error, and the platform matches what you meant to build — `debian-arm64` on the board and on the cross-build, `debian-amd64` on an x86-64 host.

The `Registering Implementation` lines are printed whatever happens, because `start.sh` discards the output of each registration. Whether all eight backends registered is checked in the next section, not read from the log.

---

## Validation

The container is named `Test-server`. Note that `docker exec` takes the *container* name, while `docker compose` subcommands take the *service* name (`ssl-server`) — they are different.

### 1. The binaries match the host

```bash
docker exec Test-server readelf -h /app/server | grep Machine
```

Expected: `Machine: AArch64` on the board, `Advanced Micro Devices X86-64` on an x86-64 host.

### 2. All eight backends are installed

```bash
docker exec Test-server ls -la /app/LIB
```

Expected: eight objects `enc_s01_n01` … `enc_s02_n04`, plus `lib_enc.so`, all freshly timestamped.

Eight is the number that matters. A backend whose symbols clash with an already-registered one is skipped *silently* — `register` still exits 0 — so a short list here is the only signal you get.

### 3. All eight symbols are exported

```bash
docker exec Test-server sh -c 'nm -D /app/LIB/lib_enc.so | grep enc_s'
```

Expected: eight entries, all of type `T`.

### 4. The manifest counters are right

```bash
docker exec Test-server sh -c 'grep "///" /app/header.h'
```

Expected: `///1-04` and `///2-04` — four implementations registered at each security level.

### 5. Every backend was measured

```bash
docker exec Test-server sh -c 'grep -c "^name" /app/db.yaml; grep -c "energy: 0.000000" /app/db.yaml'
```

Expected: `8` and `0` — eight entries, none with a zero energy. A backend that could not be measured is not registered at all, so a count below eight here matches a short list in step 2.

### 6. The server is listening

```bash
docker exec Test-server sh -c 'lsof -i -P -n | grep LISTEN'
```

Expected: TCP `*:5544` and `*:5545`.

### 7. End-to-end round trip, on both security levels

This is the test that actually proves correctness. `rfile` is a 10000-byte file committed for the purpose (`test/rfile` in the repository). Send it once per level and compare what the server saved byte for byte:

```bash
for p in "1 5544" "0 5545"; do set -- $p
  docker exec Test-server sh -c "cd /app && rm -f Downloads/*; ./client -s $1 -i 127.0.0.1:$2 -f rfile 2>&1 | tail -1; sleep 3; for f in Downloads/*; do cmp rfile \$f && echo IDENTICAL -s $1 port $2; done"
done
```

Expected, for each level: `Entire File Sent 10016 bytes` — 10000 bytes of payload plus the 16-byte AEAD tag — then `IDENTICAL`. The server log (`docker logs Test-server`) should show `Starting with enc_s02_n02` and `Starting with enc_s01_n02`, and no `TAG MISMATCH`.

A few things to know about this test:

- `-s` accepts only `1` and `0`. Any other value leaves the client without a cipher, and the server reports `TAG MISMATCH`.
- The server saves each file as `Downloads/filename-ekm<N>` with a random `<N>`. Run transfers one at a time: the two ports draw the same sequence of names, so simultaneous transfers can end up in the same file.
- `cmp` is the only trustworthy check. The client's `Entire File Sent` says nothing about what the server did with the data, the server's own "Received N bytes" line under-counts by design, and the server keeps a file even when its tag does not verify (see below).

---

## Known limitations

**Energy on ARM is a board-level figure, not core energy.** likwid's `ENERGY` group reads Intel/AMD RAPL registers, which the Cortex-A53 does not have, so on aarch64 the energy comes from the SOM's INA260 power monitor instead: an idle baseline before and after, a three-second run of the backend, and the difference integrated over the run. The sensor sees the whole module — PS, PL and DDR — so what is measured is the *extra* power a backend draws, about 0.14 W on top of ~3.05 W at rest. It is the right quantity for comparing backends on this board and it is **not** comparable to the x86 RAPL figures, which are per-core energy. `LOGBOOK.md` M-A14 has the protocol and the measured characterisation.

**Energy differences below ~1% are not resolved.** A single measurement carries 1.3–3.4 mW of noise on that 0.14 W signal, roughly 1–3% on energy. Backends further apart than that rank consistently; `enc_s02_n01` and `enc_s02_n02`, which sit 0.1% apart, alternate between runs, and that is the honest result rather than a defect.

**Measurement wants a quiet board.** Anything else running on the module shows up in the reading. The code defends itself — power levels are taken as the median of 250 ms block means, and a quality gate repeats a measurement when the two baselines or the two halves of the run disagree by more than 15 mW — but for reference-grade numbers it is worth stopping the periodic system timers (`unattended-upgrades`, `anacron`, `dpkg-db-backup`, `logrotate`) for the duration. Each registration appends the full per-window statistics to `power.csv` next to `db.yaml`, so a suspicious figure can always be traced back.

**Two cosmetic reporting bugs**, both inherited from upstream and present on x86 too: the server's received-byte counter tallies whole 1024-byte chunks and drops the remainder, and the transfer rate divides by an elapsed time that rounds to zero. Neither affects the data — `cmp` is the check that matters.

**Energy on x86-64 is per-core and needs bare metal.** likwid reads RAPL's per-core domain, which exists only when the kernel exposes the MSRs: not under WSL2, Docker Desktop or a virtual machine, and not with Secure Boot on. The figures rank the backends the same way as the board does, but they are single measurements, not a characterisation like the board's, and joules cannot be compared across the two platforms.

**The test certificate is not verified.** The server presents a self-signed certificate (`certs/certfile.crt`, valid until 18 January 2027), and the client does not check it. Its expiry will therefore not break transfers, but it also means the client does not authenticate the server.

**Two weaknesses inherited from upstream, left open.** Neither affects normal operation. `./send` accepts a mode byte of the wrong security level, and `./send 5545 98` puts the low-level port on an AES-256 backend, which breaks every transfer on it; `./synthesize` never does this. And the server writes decrypted data as it arrives and checks the authentication tag only at the end: on a mismatch it keeps the file and does not tell the client, which an AEAD should never do. `LOGBOOK.md` §9 describes both and how they could be closed.

---

## Base image

On aarch64 the container builds on [`al3monni/kria-ubuntu:22.04.5`](https://hub.docker.com/r/al3monni/kria-ubuntu), a snapshot of a Kria board's own root filesystem published to Docker Hub (`linux/arm64/v8`, ~2 GB compressed). On x86-64 it builds on stock `ubuntu:22.04`, the same release. The Dockerfile picks the base from the build architecture; there is no argument to pass.

Stock `ubuntu:22.04` is the same distribution but not the same userspace: AMD's Kria image carries board-specific tooling — `xmutil` and the platform-statistics utilities — that the power-measurement work uses. Building on a frozen snapshot also means the toolchain does not depend on what happens to be installed on the board at build time.

Nothing about the application is baked into that snapshot. Every build step lives in the tracked `Dockerfile` on top of it. The procedure for regenerating and republishing the image, including the exclusion mistakes that are easy to make, is documented in [`LOGBOOK.md`](LOGBOOK.md) §11.

---

## Credits and licence

This work is a port and extension of [spdocker](https://github.com/subhadeep-banik/spdocker) by Subhadeep Banik. See `LICENSE` for the original terms.

The bitsliced backends (`backends/f3`, `backends/f6`) come from [bitsliced-aes](https://github.com/conorpp/bitsliced-aes) by Conor Patrick, whose repository declares no licence.

The base image derives from AMD/Xilinx's Ubuntu 22.04 IoT image for Kria and contains Canonical- and AMD-licensed components, redistributed under their respective terms.

Funded by the European Union under Horizon Europe, Grant No. 101135183 (MYRTUS). Views and opinions expressed are those of the authors only and do not necessarily reflect those of the European Union.
