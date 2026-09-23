# myrtus-psm-edge — Privacy and Security Manager (Edge Layer)

Crypto-agile TLS prototype with eight runtime-selectable AES backends, running on ARM64 edge hardware. Component of the **MYRTUS** project (Horizon Europe, Grant No. 101135183).

**Repo:** https://github.com/mdc-suite/myrtus-psm-edge
**Upstream:** https://github.com/subhadeep-banik/spdocker
**Port branch:** `al3monni-test-arm` · **x86 baseline:** `al3monni-test` (`ddb5f5f`)
**Target hardware:** AMD/Xilinx Kria KR260 — Zynq UltraScale+ MPSoC, 4× Cortex-A53, aarch64
**Cross-build host:** Windows + WSL2 (Ubuntu) + Docker Desktop, `linux/arm64` under QEMU
**On-board host:** Ubuntu 22.04 IoT, Docker Engine, native aarch64 build
**Base image:** [`al3monni/kria-ubuntu:22.04.5`](https://hub.docker.com/r/al3monni/kria-ubuntu) (arm64/v8)
**Status:** ✅ **Validated on real silicon.** Builds, registers all 8 implementations, serves over TLS, and round-trips byte-identical (`cmp`) on the Kria KV260. Energy is measured on ARM through the on-SOM INA260, so backend selection runs on real measurements.

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

Which backend gets used is decided by a single **mode byte**: two bits select the security level, four select the implementation index. The server maps that to a symbol name (`enc_s02_n02` and so on) and calls `dlsym`.

The order in which the eight backends are registered *is* the numbering. Client and server must agree on it, or the same mode byte will select different ciphers on each side.

### What lives where

| Path | Contents |
|---|---|
| `f1` … `f8` | the eight AES backends, one directory each, with their own Makefile and KAT |
| `register.c` | compiles a backend, runs its KAT, checks for symbol clashes, installs the object |
| `gen.c` | wraps a backend so its internal function name becomes the unique exported symbol |
| `encrypt02.c` | the `dlopen`/`dlsym` selection machinery and the mode-byte decoding |
| `server_f.c`, `cltest.c` | server and client |
| `check1.c`, `check2.c` | KAT templates (AES-128 and AES-256) that `register` fills in per backend |
| `cycles.h` | portable cycle counter — x86 `rdtscp` or ARM `cntvct_el0` |
| `start.sh` | the container entrypoint pipeline above |
| `Dockerfile`, `compose-server.yml` | image definition and orchestration |
| `LIB/` | created at build, populated at runtime — never committed |

---

## Requirements

### On the board (recommended path)

An AMD/Xilinx Kria KV260 running Ubuntu 22.04 IoT, with Docker Engine installed. Full bring-up instructions — flashing, serial console, networking, Docker — are in [`KRIA_KV260_DEPLOYMENT.md`](KRIA_KV260_DEPLOYMENT.md).

Check that your user is in the `docker` group (`docker version` should respond without `sudo`), and that the microSD has several GB free — the base image alone is around 2 GB compressed.

### On an x86 dev host (cross-build)

Windows + WSL2 + Docker Desktop, with WSL integration enabled for the Ubuntu distro. Because the base image is an arm64 rootfs, you also need the QEMU binfmt handler registered once per WSL VM:

```bash
docker run --privileged --rm tonistiigi/binfmt --install arm64
```

This does not survive `wsl --shutdown` — re-run it if you get `exec format error`.

The cross-build is useful for catching compile errors without occupying the board, but everything runs emulated and a clean build is slower than on the board itself.

To build for x86-64 *natively* instead, override the base image — the Kria one is arm64-only, and on x86-64 every `RUN` would fail with `exec format error`:

```bash
docker compose -f compose-server.yml build --build-arg BASE=ubuntu:22.04
```

All eight backends build there too: `f7` and `f8` carry both AES implementations and pick one at compile time, AES-NI on x86-64 and the ARMv8 Crypto Extensions on the Kria. Energy, however, is not measurable under WSL2 or Docker Desktop — likwid needs RAPL registers that a virtualised kernel does not expose.

---

## Build and run

### On the board

```bash
git clone https://github.com/mdc-suite/myrtus-psm-edge.git
cd myrtus-psm-edge
git checkout al3monni-test-arm

docker compose -f compose-server.yml up --build
```

A clean first build takes roughly ten minutes on the KV260 — most of it is compiling likwid. Later builds reuse that layer and finish in seconds.

To run it detached instead, use `-d` and read the logs with `docker compose -f compose-server.yml logs`.

### On the x86 dev host

```bash
docker run --privileged --rm tonistiigi/binfmt --install arm64   # once per WSL VM
docker buildx build --platform linux/arm64 -t myrtus-psm-edge:arm64 --load .
sudo docker compose -f compose-server.yml up --build
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

Three things to check in that output: all eight backends register, `Creating Shared Library` is not followed by an `ld` error, and the platform reads `debian-arm64` rather than `amd64`.

---

## Validation

The container is named `Test-server`. Note that `docker exec` takes the *container* name, while `docker compose` subcommands take the *service* name (`ssl-server`) — they are different.

### 1. The binaries are aarch64

```bash
docker exec Test-server readelf -h /app/server | grep Machine
```

Expected: `Machine: AArch64`

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

### 5. The server is listening

```bash
docker exec Test-server sh -c 'lsof -i -P -n | grep LISTEN'
```

Expected: TCP `*:5544` and `*:5545`.

### 6. End-to-end round trip

This is the test that actually proves correctness. `rfile` is a 10000-byte file committed for the purpose.

```bash
docker exec -it Test-server sh -c 'cd /app && ./client -s 1 -i 127.0.0.1:5544 -f rfile'
```

Expected: the handshake completes and the client reports `Entire File Sent 10016 bytes` — 10000 bytes of payload plus the 16-byte AEAD tag.

The server writes what it received into `/app/Downloads/`, under a filename derived from the TLS session's exported keying material. **That name changes on every connection**, so list the directory to find it:

```bash
docker exec Test-server sh -c 'cd /app && ls -l Downloads/'
```

Then compare byte for byte:

```bash
docker exec Test-server sh -c 'cd /app && cmp rfile Downloads/filename-ekm<N>'
```

No output means the files are identical. This is the only trustworthy check — the server's own "Received N bytes" line under-counts by design (see below), so it is not evidence of anything.

---

## Known limitations

**Energy on ARM is a board-level figure, not core energy.** likwid's `ENERGY` group reads Intel/AMD RAPL registers, which the Cortex-A53 does not have, so on aarch64 the energy comes from the SOM's INA260 power monitor instead: an idle baseline before and after, a three-second run of the backend, and the difference integrated over the run. The sensor sees the whole module — PS, PL and DDR — so what is measured is the *extra* power a backend draws, about 0.14 W on top of ~3.05 W at rest. It is the right quantity for comparing backends on this board and it is **not** comparable to the x86 RAPL figures, which are CPU-package energy. `LOGBOOK.md` M-A14 has the protocol and the measured characterisation.

**Energy differences below ~1% are not resolved.** A single measurement carries 1.3–3.4 mW of noise on that 0.14 W signal, roughly 1–3% on energy. Backends further apart than that rank consistently; `enc_s02_n01` and `enc_s02_n02`, which sit 0.1% apart, alternate between runs, and that is the honest result rather than a defect.

**Measurement wants a quiet board.** Anything else running on the module shows up in the reading. The code defends itself — power levels are taken as the median of 250 ms block means, and a quality gate repeats a measurement when the two baselines or the two halves of the run disagree by more than 15 mW — but for reference-grade numbers it is worth stopping the periodic system timers (`unattended-upgrades`, `anacron`, `dpkg-db-backup`, `logrotate`) for the duration. Each registration appends the full per-window statistics to `power.csv` next to `db.yaml`, so a suspicious figure can always be traced back.

**Two cosmetic reporting bugs**, both inherited from upstream and present on x86 too: the server's received-byte counter tallies whole 1024-byte chunks and drops the remainder, and the transfer rate divides by an elapsed time that rounds to zero. Neither affects the data — `cmp` is the check that matters.

**The x86-64 baseline is only partly re-validated.** Build and registration pass against the current tree — eight backends, all KATs — but the energy figures do not: likwid needs RAPL registers that WSL2 and Docker Desktop do not expose, so real x86-64 numbers need a bare-metal Linux host. The end-to-end round trip has not been re-run there since `ddb5f5f` either.

---

## Base image

The container builds on [`al3monni/kria-ubuntu:22.04.5`](https://hub.docker.com/r/al3monni/kria-ubuntu), a snapshot of a Kria board's own root filesystem published to Docker Hub (`linux/arm64/v8`, ~2 GB compressed).

Stock `ubuntu:22.04` is the same distribution but not the same userspace: AMD's Kria image carries board-specific tooling — `xmutil` and the platform-statistics utilities — that the power-measurement work uses. Building on a frozen snapshot also means the toolchain does not depend on what happens to be installed on the board at build time.

Nothing about the application is baked into that snapshot. Every build step lives in the tracked `Dockerfile` on top of it. The procedure for regenerating and republishing the image, including the exclusion mistakes that are easy to make, is documented in [`LOGBOOK.md`](LOGBOOK.md) §11.

---

## Credits and licence

This work is a port and extension of [spdocker](https://github.com/subhadeep-banik/spdocker) by Subhadeep Banik. See `LICENSE` for the original terms.

The base image derives from AMD/Xilinx's Ubuntu 22.04 IoT image for Kria and contains Canonical- and AMD-licensed components, redistributed under their respective terms.

Funded by the European Union under Horizon Europe, Grant No. 101135183 (MYRTUS). Views and opinions expressed are those of the authors only and do not necessarily reflect those of the European Union.
