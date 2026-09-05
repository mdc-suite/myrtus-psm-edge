# myrtus-psm-edge — Privacy and Security Manager (Edge Layer)

Crypto-agile TLS prototype with eight runtime-selectable AES backends, running on ARM64 edge hardware. Component of the **MYRTUS** project (Horizon Europe, Grant No. 101135183).

**Repo:** https://github.com/mdc-suite/myrtus-psm-edge
**Upstream:** https://github.com/subhadeep-banik/spdocker
**Port branch:** `al3monni-test-arm` · **x86 baseline:** `al3monni-test` (`ddb5f5f`)
**Target hardware:** AMD/Xilinx Kria KR260 — Zynq UltraScale+ MPSoC, 4× Cortex-A53, aarch64
**Cross-build host:** Windows + WSL2 (Ubuntu) + Docker Desktop, `linux/arm64` under QEMU
**On-board host:** Ubuntu 22.04 IoT, Docker Engine, native aarch64 build
**Base image:** [`al3monni/kria-ubuntu:22.04.5`](https://hub.docker.com/r/al3monni/kria-ubuntu) (arm64/v8)
**Status:** ✅ **Validated on real silicon.** Builds, registers all 8 implementations, serves over TLS, and round-trips byte-identical (`cmp`) on the Kria KV260. Energy profiling is not yet implemented on ARM

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

**Energy profiling is disabled on ARM.** The upstream design selects a backend by measuring its energy cost, using likwid's `ENERGY` performance group. That group reads Intel/AMD RAPL model-specific registers, and the Cortex-A53 has no equivalent — so it cannot work on this platform, emulated or not. The profiling call is skipped on aarch64 rather than left to fail. Implementing power measurement on ARM, most likely through the on-SOM INA260 monitor, is the main piece of open work.

**Backend selection is therefore pinned.** With no measurements to compute from, the mode byte stays at its default of 98 and the server always uses `enc_s02_n02` (backend f5). The `dlopen`/`dlsym` machinery itself is fully functional; only the policy input that would drive it is missing.

**Two cosmetic reporting bugs**, both inherited from upstream and present on x86 too: the server's received-byte counter tallies whole 1024-byte chunks and drops the remainder, and the transfer rate divides by an elapsed time that rounds to zero. Neither affects the data — `cmp` is the check that matters.

**The x86-64 baseline has not been re-validated recently.** It last passed at commit `ddb5f5f`; the ARM branch has diverged considerably since, including a change of base image.

---

## Base image

The container builds on [`al3monni/kria-ubuntu:22.04.5`](https://hub.docker.com/r/al3monni/kria-ubuntu), a snapshot of a Kria board's own root filesystem published to Docker Hub (`linux/arm64/v8`, ~2 GB compressed).

Stock `ubuntu:22.04` is the same distribution but not the same userspace: AMD's Kria image carries board-specific tooling — `xmutil` and the platform-statistics utilities — that the planned power-measurement work needs. Building on a frozen snapshot also means the toolchain does not depend on what happens to be installed on the board at build time.

Nothing about the application is baked into that snapshot. Every build step lives in the tracked `Dockerfile` on top of it. The procedure for regenerating and republishing the image, including the exclusion mistakes that are easy to make, is documented in [`LOGBOOK.md`](LOGBOOK.md) §11.

---

## Credits and licence

This work is a port and extension of [spdocker](https://github.com/subhadeep-banik/spdocker) by Subhadeep Banik. See `LICENSE` for the original terms.

The base image derives from AMD/Xilinx's Ubuntu 22.04 IoT image for Kria and contains Canonical- and AMD-licensed components, redistributed under their respective terms.

Funded by the European Union under Horizon Europe, Grant No. 101135183 (MYRTUS). Views and opinions expressed are those of the authors only and do not necessarily reflect those of the European Union.
