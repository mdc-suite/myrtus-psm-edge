# myrtus-psm-edge — Privacy and Security Manager (Edge Layer)

Crypto-agile TLS prototype with eight runtime-selectable AES backends, running on ARM64 edge hardware and on x86-64. Component of the **MYRTUS** project (Horizon Europe, Grant No. 101135183).

|  |  |
|---|---|
| **Repository** | https://github.com/mdc-suite/myrtus-psm-edge |
| **Upstream** | https://github.com/subhadeep-banik/spdocker |
| **Target hardware** | AMD/Xilinx Kria KV260 — Zynq UltraScale+ MPSoC, 4× Cortex-A53, aarch64, Ubuntu 22.04 IoT |
| **Hosts** | the board itself (native aarch64 build) · bare-metal x86-64 Linux (native amd64 build) |
| **Base image** | [`al3monni/kria-ubuntu:22.04.5`](https://hub.docker.com/r/al3monni/kria-ubuntu) on aarch64, `ubuntu:22.04` on x86-64, chosen automatically |
| **Status** | ✅ Validated on both architectures: eight backends registered and measured, byte-identical round trips on both security levels |

> Derived from [spdocker](https://github.com/subhadeep-banik/spdocker) by Subhadeep Banik. Every modification made along the way, and why, is recorded in [`LOGBOOK.md`](LOGBOOK.md).

---

## What this is

A TLS client/server that ships **eight interchangeable AES implementations**. The implementation protecting a file transfer is not fixed at compile time: the server picks one at runtime, loads it from a shared library through `dlopen`/`dlsym`, and uses it for the authenticated encryption of the transfer.

That is what *crypto-agility* means here, and it is the reason this component sits in the MYRTUS edge layer as the Privacy and Security Manager: the security/performance/energy trade-off of a secure channel can be renegotiated while the node is running, rather than being frozen when the binary was built.

The eight backends span two security levels (AES-128 and AES-256) and four implementation strategies for each: reference C, an alternative C implementation, a bitsliced constant-time version, and one that uses the processor's own AES instructions.

---

## Build and run

The component runs in a Docker container, on either of two targets:

- the **Kria KV260** board (aarch64), the edge platform it is meant for;
- an **x86-64 PC running Ubuntu natively**, directly on the hardware. A virtual machine, WSL2 or Docker Desktop will not do: the energy of the backends cannot be measured there, and a backend that cannot be measured is not registered.

Each target builds the image for its own architecture, on the target itself. Steps 1 and 2 depend on the target; from step 3 on they are the same on both.

### 1. Prepare the target

**Kria KV260.** Follow [`KRIA_KV260_DEPLOYMENT.md`](KRIA_KV260_DEPLOYMENT.md). It takes the board from an empty microSD card to Ubuntu 22.04 with network access, SSH and Docker, with your user in the `docker` group. Keep a few GB free on the card for the images.

**x86-64 PC.** Ubuntu 22.04 or later, installed natively, with:

- **Docker Engine and its compose plugin**: `sudo apt install docker.io docker-compose-v2`;
- **your user in the `docker` group**, so that Docker commands work without `sudo`: `sudo usermod -aG docker $USER`, then reboot. Logging out is not always enough for the new group to apply. Afterwards `docker version` must answer without `sudo`;
- **Secure Boot disabled**, so that the processor's energy registers can be read: `cat /sys/kernel/security/lockdown` must print `[none]`. If the PC also boots Windows with BitLocker, have the recovery key at hand before changing the setting.

### 2. Open a terminal on the target

**Kria KV260.** From your PC, connect to the board with the address set during bring-up (§5 of the guide):

```bash
ssh ubuntu@192.168.137.50
```

**x86-64 PC.** Open a terminal on it, directly or over SSH, and load the kernel module through which the energy registers are read. It is needed again after every reboot:

```bash
sudo modprobe msr
```

### 3. Get the code

The first time:

```bash
git clone https://github.com/mdc-suite/myrtus-psm-edge.git
cd myrtus-psm-edge
```

Later, to update it:

```bash
cd myrtus-psm-edge
git pull
```

If the repository is private, clone `git@github.com:mdc-suite/myrtus-psm-edge.git` instead, with an SSH key that has access to it. On the board this is the deploy key of §7 of the bring-up guide.

### 4. Build and start the container

```bash
docker compose -f compose-server.yml up -d --build
```

This builds the image, starts the container in the background and gives the prompt back. The first build takes about 16 minutes on the board, more than half of them to download and unpack the 2 GB base image, and about 3 minutes on x86-64. Later builds reuse what is already built and take seconds.

### 5. Watch the start

```bash
docker logs -f Test-server
```

This shows the container's output as it runs. At every start the container registers the eight backends, measuring each one, which takes a few minutes on the board; then it starts the server. The start has succeeded when the output ends like this:

```
Resetting Initial Configuration
Registering Implementation in ./f1
...
Registering Implementation in ./f8
Done
Creating Shared Library lib_enc.so
Updating Paths
OpenSSL 3.0.2 15 Mar 2022 (Library: OpenSSL 3.0.2 15 Mar 2022)
built on: ...
platform: debian-arm64
options:  bn(64,64)
```

Check that no error follows `Creating Shared Library`, and that `platform` matches the target: `debian-arm64` on the board, `debian-amd64` on x86-64. Then press **Ctrl+C**. It stops showing the output; the container keeps running.

The `Registering Implementation` lines appear whatever happens. Whether all eight backends were registered is checked in the next step.

### 6. Run the tests

```bash
test/test.sh -all
```

This checks the build (eight backends registered and measured, the server listening), sends a file on both security levels and compares what arrives byte for byte, switches backend at runtime, and repeats the transfers with files of 18 sizes. For each test it prints the command, the expected and the obtained result. The run must end with `ALL 13 TESTS PASSED`. Without `-all` it runs a shorter subset; *Tests* explains every check.

### 7. Stop the container

```bash
docker compose -f compose-server.yml down
```

This stops and removes the container. Until you do, it keeps running, and it starts again by itself after a reboot of the target. Stopping it loses nothing: every start registers and measures the backends again.

---

## How it works

Everything runs inside one container. This section follows the execution from the moment the container starts to the moment a file is received, and then shows how the backend is switched at runtime.

### 1. Container start

`start.sh` is the container's entrypoint. It runs four steps:

```mermaid
flowchart LR
    A["reset"] --> B["register f1 … f8"]
    B --> C["gcc -shared: LIB/*.o → lib_enc.so"]
    C --> D["server on ports 5544 and 5545"]
```

1. **`reset`** empties `LIB/`, rewrites `header.h` with zero backends per level, and empties `db.yaml`.
2. **`register`** runs once per backend, `f1` to `f8`, in that order (next section).
3. **`gcc -shared`** links the registered objects into one library, `LIB/lib_enc.so`.
4. **`server`** starts, after `start.sh` has created `Downloads/`, where received files are saved.

### 2. Registering a backend

Each backend directory holds its source, its Makefile and a `config.txt` naming the security level, the source file and the AES function. `register -c ./fN/config.txt` takes the backend through a sequence of checks, and a backend that fails any of them is simply not registered:

```mermaid
flowchart LR
    A("compile fN,<br/>known-answer test") -- "pass" --> C("symbol clash<br/>with LIB/?")
    C -- "no" --> D["rename to<br/>enc_sXX_nYY"]
    D --> E("measure time<br/>and energy")
    E -- "ok" --> F["installed in LIB/"]
    A -. "fail" .-> X["not registered"]
    C -. "clash" .-> X
    E -. "fail" .-> X
```

- **Known-answer test.** `register` fills a template (`check1.c` for AES-128, `check2.c` for AES-256) with a call to the backend, compiles it with the backend's object and runs it: the backend must encrypt a fixed plaintext under a fixed key into the expected ciphertext. This is what proves that a backend computes AES correctly.
- **Symbol clash.** Every backend ends up in the same library, so `register` checks with `nm` that none of its symbols already exists in `LIB/`.
- **Rename.** `gen.c` wraps the source with `#define <function> enc_sXX_nYY` and recompiles it: `XX` is the security level and `YY` the next free index for that level, read from `header.h`. The registration order therefore *is* the numbering.
- **Measurement.** `profile01.c` measures the backend and appends its time and energy, per 50 000 encryptions, to `db.yaml`:
  - on **x86-64**, with `likwid-perfctr -g ENERGY` around 50 000 encryptions, reading the RAPL energy of the processor cores;
  - on the **board**, with the module's INA260 power monitor: 2 s of idle, 3 s of the backend looping, 2 s of idle again, and the energy above idle integrated over the run.
- **Install.** The object stays in `LIB/` only if the measurement succeeds; then the counter in `header.h` is incremented and the backend's prototype added. On any failure `header.h` is left as it was.

The result, for each level, is four backends numbered `n01` to `n04`:

| Level | `n01` | `n02` | `n03` | `n04` |
|---|---|---|---|---|
| 1 — AES-128 | f1, reference C | f2, alternative C | f3, bitsliced | f7, AES instructions |
| 2 — AES-256 | f4, reference C | f5, alternative C | f6, bitsliced | f8, AES instructions |

### 3. The server

The server prints the OpenSSL banner and forks into two processes, one per security level:

| Port | Level | Cipher expected from the client | Initial backend |
|---|---|---|---|
| 5544 | high | AES-256-GCM | `enc_s02_n02` (f5) |
| 5545 | low | AES-128-GCM | `enc_s01_n02` (f2) |

Each process keeps the backend it uses in a single byte, the **mode**: two bits for the function, two for the security level, four for the implementation index. `0x62` = `01 10 0010` means encryption, level 2, index 2, i.e. `enc_s02_n02`.

### 4. A file transfer

```mermaid
sequenceDiagram
    participant C as client
    participant S as server
    C->>S: TLS 1.3 handshake
    Note over C,S: both sides derive the same key and IV from the TLS session
    C->>S: file encrypted with OpenSSL AES-GCM, in 1024-byte chunks, then the 16-byte tag
    S->>S: decrypt with the GCM of encrypt02.c around the backend named by the mode
    S->>S: check the tag, save the file in Downloads/
```

1. The client (`./client -s <level> -i <address>:<port> -f <file>`) connects to the port of the chosen level: `-s 1` to 5544, `-s 0` to 5545.
2. After the TLS 1.3 handshake, both sides call `SSL_export_keying_material` and obtain the same 64 bytes: the key and the IV of the file encryption. Nothing else about the cipher is negotiated.
3. The client encrypts the file with OpenSSL's AES-GCM (256-bit on 5544, 128-bit on 5545) and sends it in 1024-byte chunks, followed by the 16-byte authentication tag. It prints `Entire File Sent <n> bytes`.
4. The server decrypts with its own GCM implementation (`encrypt02.c`), which calls the backend named by the mode for every AES block, verifies the tag at the end and saves the file as `Downloads/filename-ekm<N>`.

The client always uses OpenSSL and knows nothing about the mode. It does not need to: every backend of a level computes the same AES, so any of them decrypts what the client sends. **Changing implementation is invisible on the wire; changing level is not.**

### 5. Switching backend at runtime

This is the crypto-agility itself:

1. `./synthesize -f e -s <level> -t <0|1|2> -e <0|1|2>` reads `db.yaml`, keeps the four backends of the level, and picks the one closest to the requested time and energy (0 = lowest, 1 = middle, 2 = highest).
2. It calls `./send <port> <mode>`, which finds the server process listening on the port of that level and sends it the new mode with a `SIGUSR1` signal.
3. The server's signal handler stores the new mode. The backend is looked up again for every 1024-byte chunk, so the switch applies from the next chunk, even in the middle of a transfer, and lasts until the container restarts.

Nothing runs `synthesize` automatically: today the selection is a manual step.

---

## Tests

There are three layers of tests: the ones the pipeline runs by itself at every start, the checks you run once the container is up, and the end-to-end tests of transfers and backend switching. `test/test.sh` runs the last two for you.

> [!NOTE]
> Compose calls the service `ssl-server`, and the container it creates is `Test-server`. `docker compose` subcommands take the service name; `docker exec` and `docker logs` take the container name.

### Built into every start

| Test | Where | What a failure means |
|---|---|---|
| Known-answer test | `register`, per backend | the backend does not compute AES correctly: not registered |
| Symbol clash | `register`, per backend | the backend would overwrite another one in the library: not registered |
| Measurement | `profile01.c`, per backend | time or energy could not be measured: not registered |
| Quality gate (board only) | `profile01.c`, per measurement | the idle baselines or the two halves of the run disagree by more than 15 mW: the measurement is repeated, up to three times, and the cleanest attempt is kept |

A failure in any of them removes one backend and nothing else, silently. The checks below are how you notice.

### Running the checks: `test/test.sh`

Once the container has started (step 5 of *Build and run*), run the script on the target, from the repository:

```bash
test/test.sh          # the build checks (1-6 below) and the round trip on both levels
test/test.sh -rapid   # the build checks only
test/test.sh -all     # everything: also the runtime switch and the file sizes
```

For each test it prints the command it runs in the container, the expected and the obtained result, and `PASS` or `FAIL`; a summary closes the run.

```
[3/8] Symbols exported by lib_enc.so
      command:  nm -D LIB/lib_enc.so | awk '$2 == "T" && $3 ~ /^enc_s/ { print $3 }' | xargs
      expected: enc_s01_n01 enc_s01_n02 enc_s01_n03 enc_s01_n04 enc_s02_n01 enc_s02_n02 enc_s02_n03 enc_s02_n04
      obtained: enc_s01_n01 enc_s01_n02 enc_s01_n03 enc_s01_n04 enc_s02_n01 enc_s02_n02 enc_s02_n03 enc_s02_n04
      PASS
```

The exit status is `0` when every test passed, `1` when any failed, and `2` when the tests could not run: Docker unreachable, container stopped, or still registering its backends. Nothing is rebuilt or restarted, so the script can be run any number of times on the same container. `-all` switches backends while it runs and leaves both ports on their initial ones (`./send 5544 98`, `./send 5545 82`).

The sections below are the same checks one by one, with the commands to run them by hand. A command printed by the script runs in the container as it is: `docker exec -it Test-server sh`, then `cd /app`.

### After the start

**1. The binaries match the host.**

```bash
docker exec Test-server readelf -h /app/server | grep Machine
```

Expected: `AArch64` on the board, `Advanced Micro Devices X86-64` on x86-64.

**2. All eight backends are installed.**

```bash
docker exec Test-server ls -la /app/LIB
```

Expected: eight objects `enc_s01_n01` … `enc_s02_n04`, plus `lib_enc.so`. Eight is the number that matters: a backend refused by any of the tests above is missing here, and `register` does not report it.

**3. All eight symbols are exported.**

```bash
docker exec Test-server sh -c 'nm -D /app/LIB/lib_enc.so | grep enc_s'
```

Expected: eight entries, all of type `T`.

**4. The counters are right.**

```bash
docker exec Test-server sh -c 'grep "///" /app/header.h'
```

Expected: `///1-04` and `///2-04`, four backends at each security level.

**5. Every backend was measured.**

```bash
docker exec Test-server sh -c 'grep -c "^name" /app/db.yaml; grep -c "energy: 0.000000" /app/db.yaml'
```

Expected: `8` and `0`, eight entries and none with zero energy.

**6. The server is listening.**

```bash
docker exec Test-server sh -c 'lsof -i -P -n | grep LISTEN'
```

Expected: TCP `*:5544` and `*:5545`.

### Round trip on both security levels

This is the test that proves correctness. `rfile` is a 10000-byte file committed for the purpose. Send it once per level and compare what the server saved byte for byte:

```bash
for p in "1 5544" "0 5545"; do set -- $p
  docker exec Test-server sh -c "cd /app && rm -f Downloads/*; ./client -s $1 -i 127.0.0.1:$2 -f rfile 2>&1 | tail -1; sleep 3; for f in Downloads/*; do cmp rfile \$f && echo IDENTICAL -s $1 port $2; done"
done
docker logs Test-server 2>&1 | grep -E "Starting with|MISMATCH"
```

Expected, for each level: `Entire File Sent 10016 bytes` (10000 bytes of payload plus the 16-byte tag), then `IDENTICAL`. In the log, `Starting with enc_s02_n02` and `Starting with enc_s01_n02`, and no `TAG MISMATCH`.

A few things to know about this test:

- `-s` accepts only `1` and `0`. Any other value leaves the client without a cipher, and the server reports `TAG MISMATCH`.
- Run transfers one at a time. The two server processes draw the same sequence of random file names, so simultaneous transfers on the two ports can end up in the same file.
- `cmp` is the only trustworthy check. The client's `Entire File Sent` says nothing about what the server did with the data, the server's own "Received N bytes" line leaves out the last partial chunk, and the server keeps a file even when its tag does not verify (see *Known limitations*).

### Switching backend at runtime

This test exercises the crypto-agility: ask for the fastest and cheapest AES-128 backend, check that the server switches to it, and send a file through it.

```bash
docker exec Test-server sh -c 'cd /app && ./synthesize -f e -s 1 -t 0 -e 0'
docker logs --tail 3 Test-server
docker exec Test-server sh -c "cd /app && rm -f Downloads/*; ./client -s 0 -i 127.0.0.1:5545 -f rfile 2>&1 | tail -1; sleep 3; for f in Downloads/*; do cmp rfile \$f && echo IDENTICAL; done"
docker logs --tail 10 Test-server | grep "Starting with"
```

Expected:
- `synthesize` prints `enc_s01_n04` and `./send 5545 84` (84 = `0x54`: encryption, level 1, index 4), and `send` reports the process it signalled. The lines of `send` usually come out first: `synthesize` prints through a buffer that is emptied only when it exits;
- the server log shows `Recieved signal 84` and `Switching to enc_s01_n04`;
- the transfer is `IDENTICAL`, and the log reads `Starting with enc_s01_n04`.

The new backend stays in use until the container restarts. To go back to the initial one without restarting: `docker exec Test-server sh -c 'cd /app && ./send 5545 82'`.

### File sizes (`test/test.sh -all`)

`rfile` exercises one size only. This test sends 18 files, from 0 bytes to 1 MiB and packed around the 1024-byte record boundaries, on both levels: first through the registered backends, then through OpenSSL's GCM (mode 0, selected with `./send <port> 0`). Expected, in each of the four runs: 18 of 18 files identical and no `TAG MISMATCH`, and in mode 0 every transfer decrypted by OpenSSL. It covers the two defects fixed in M17 and M18 (`LOGBOOK.md` §4.7), which `rfile`'s size does not trigger.

---

## Known limitations

**Energy is measured differently on the two platforms, on purpose.** Each platform is measured with the finest instrument it offers: RAPL's per-core energy on x86-64, and on the board the INA260, which sees the whole module and gives the energy a backend draws above idle. The rankings of the backends agree across platforms; the joules cannot be compared. The selection is not affected, because `synthesize` compares backends only with each other, on one machine. `LOGBOOK.md` §9 explains the choice and its consequences.

**Energy differences below ~1% are not resolved on the board.** A single measurement varies by 1–3%. Backends further apart than that rank consistently; `enc_s02_n01` and `enc_s02_n02`, 0.1% apart, alternate between runs, which is the honest answer of the sensor.

**Measurement on the board wants a quiet board.** Anything else running on the module shows up in the reading. The code defends itself (robust estimator, quality gate), but for reference-grade figures stop the periodic system timers (`unattended-upgrades`, `anacron`, `dpkg-db-backup`, `logrotate`) for the duration, and never poll the container with `docker exec` while it measures. Every measurement attempt is logged in `power.csv`, next to `db.yaml`.

**Two cosmetic reporting bugs**, inherited from upstream: the server's received-byte counter drops the last partial chunk, and the transfer rate divides by an elapsed time that rounds to zero. Neither affects the data.

**The test certificate is not verified.** The server presents a self-signed certificate (`certs/certfile.crt`, valid until 18 January 2027), and the client does not check it. Its expiry will not break transfers, but the client does not authenticate the server either.

**Two weaknesses inherited from upstream, left open.** Neither affects normal operation. `./send` accepts a mode of the wrong security level: `./send 5545 98` puts the low-level port on an AES-256 backend, and every transfer on it breaks; `./synthesize` never does this. And the server writes decrypted data as it arrives and checks the authentication tag only at the end: on a mismatch it keeps the file and does not tell the client. `LOGBOOK.md` §10 describes both and how they could be closed.

---

## Base image

On aarch64 the container builds on [`al3monni/kria-ubuntu:22.04.5`](https://hub.docker.com/r/al3monni/kria-ubuntu), a snapshot of a Kria board's own root filesystem published to Docker Hub (`linux/arm64/v8`, ~2 GB compressed). On x86-64 it builds on stock `ubuntu:22.04`, the same release. The Dockerfile picks the base from the build architecture; there is no argument to pass.

Stock `ubuntu:22.04` is the same distribution but not the same userspace: AMD's Kria image carries board-specific tooling, such as `xmutil` and the platform-statistics utilities. Building on a frozen snapshot also means the toolchain does not depend on what happens to be installed on the board at build time.

Nothing about the application is baked into that snapshot: every build step lives in the tracked `Dockerfile`. The procedure for regenerating and republishing the image, including the exclusion mistakes that are easy to make, is in [`LOGBOOK.md`](LOGBOOK.md) §12.

---

## Credits and licence

This work is a port and extension of [spdocker](https://github.com/subhadeep-banik/spdocker) by Subhadeep Banik. See `LICENSE` for the original terms.

The bitsliced backends (`backends/f3`, `backends/f6`) come from [bitsliced-aes](https://github.com/conorpp/bitsliced-aes) by Conor Patrick, whose repository declares no licence.

The base image derives from AMD/Xilinx's Ubuntu 22.04 IoT image for Kria and contains Canonical- and AMD-licensed components, redistributed under their respective terms.

Funded by the European Union under Horizon Europe, Grant No. 101135183 (MYRTUS). Views and opinions expressed are those of the authors only and do not necessarily reflect those of the European Union.
