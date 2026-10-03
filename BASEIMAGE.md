# Base image comparison on the Kria KV260: Kria Ubuntu vs stock Ubuntu

On aarch64 the container was built on `al3monni/kria-ubuntu:22.04.5`, a snapshot of the board's own root filesystem (Ubuntu 22.04.5 IoT with the Kria tooling). The first start of the container on the board was slow, so we compared it against the stock `ubuntu:22.04` image, the base already used on x86-64.

The only change between the two configurations is the `FROM` line of the arm64 stage:

```diff
-FROM al3monni/kria-ubuntu:22.04.5 AS base-arm64
+FROM ubuntu:22.04 AS base-arm64
```

**Outcome.** With the stock base the first start is 3.6× faster (4m51s instead of 17m24s), the image is 15× smaller, and the component behaves identically: `test/test.sh -all` passes in full, including the INA260 energy measurement of all eight backends. `xmutil` is not needed inside the container: the code reads the INA260 directly from `/sys/class/hwmon`, which the privileged container sees regardless of the base image.

## 1. Results

Board: AMD/Xilinx Kria KV260 (4× Cortex-A53, 3910 MB RAM, root filesystem on SD card). Repository at commit `6347f1f`. One run per configuration, from a cold state (§3).

### 1.1 Summary

| Parameter | Kria Ubuntu | Stock Ubuntu | Ratio |
|---|---|---|---|
| Repository (with / without `.git`) | 1.4 MB / 620 kB | 1.4 MB / 620 kB | = |
| Base image (on disk / compressed) | 7.33 GB / 2.07 GB | 109 MB / 29.7 MB | ~67× |
| Final image (on disk / compressed) | 7.69 GB / 2.17 GB | 512 MB / 132 MB | ~15× |
| Container (writable layer / virtual) | 4.4 MB / 5.53 GB | 512 kB / 381 MB | ~15× |
| Build cache | 7.72 GB | 513 MB | ~15× |
| Total on storage (image + build cache) | ~15.4 GB | ~1.0 GB | ~15× |
| **First start** | **17m24s (1044 s)** | **4m51s (291 s)** | **3.6×** |
| CPU, user + system (mean / peak) | 21.0 % / 72 % | 22.5 % / 60 % | ≈ |
| CPU time over the first start (estimate) | ~862 core·s | ~258 core·s | 3.3× |
| I/O wait (mean / peak) | 30.4 % / 99 % | 10.7 % / 77 % | ~3× |
| RAM used (mean) | 621 MB (15.9 %) | 570 MB (14.6 %) | −8 % |
| RAM used (peak) | 755 MB (19.3 %) | 629 MB (16.1 %) | −17 % |

### 1.2 First start, by phase

From the BuildKit output of the same runs, in seconds.

| Phase | Kria Ubuntu | Stock Ubuntu |
|---|---|---|
| Download of the base image | 108.2 (2.07 GB) | 2.8 (27.7 MB) |
| Extraction of the base image | **467.1** | 2.3 |
| `apt-get` layer | 331.2 | 166.8 |
| Compilation (`gcc`, `make`) | 12.5 | 10.2 |
| Other build steps (`COPY`, `WORKDIR`, …) | 4.8 | 5.4 |
| Export of the image | 77.3 | 71.9 |
| Image built (BuildKit total) | 1011.6 | 264.8 |
| Container start | 21.1 | 20.8 |
| **Total (`time`)** | **1044.2** | **290.7** |

### 1.3 Reading the results

- **The first start was storage-bound, not CPU-bound.** With the Kria base, unpacking a 2 GB layer onto the SD card took 467 s, 45 % of the whole start. During that phase the CPUs were mostly waiting on the disk: I/O wait reached 99 %.
- **Mean CPU utilisation is the same; total CPU work is not.** Both runs average about 22 % of the four cores, but the Kria run lasts 3.6× longer, so it spends about 3.3× more CPU time. With the stock base the same work is done in a shorter window.
- **The `apt-get` layer is also halved** (331 s → 167 s), even though the stock base starts from fewer installed packages. We did not investigate the cause further.
- **The application itself is small.** The layers the Dockerfile adds weigh about 360 MB on disk on both bases; with the Kria base they sat on top of 7.3 GB of board userspace that the component does not use.

## 2. What was measured

| Parameter | Tool | Definition |
|---|---|---|
| Repository size | `du -sh` on a fresh clone | With and without the `.git` directory |
| Base / final image | `docker images` | *On disk*: compressed blobs plus the unpacked snapshot, as stored by Docker's containerd image store. *Compressed*: the content as pulled from the registry. |
| Container | `docker ps -s` | *Writable layer*: what the running container wrote. *Virtual*: writable layer plus the unpacked image it runs on. |
| Build cache | `docker system df` | BuildKit cache left after the build |
| First start | `time docker compose -f compose-server.yml up -d` | From the launch of the command to its return, with nothing cached on the board: it includes the download of the base image, the build, the export of the image and the start of the container |
| CPU | `vmstat 1`, columns `us + sy` | Share of the four cores busy in user and kernel space, sampled every second |
| I/O wait | `vmstat 1`, column `wa` | Share of time the cores were idle waiting on I/O; reported separately from CPU |
| RAM | `vmstat -S M 1` | Used memory = total − free − buffers − cache, in MB; percentages are relative to the 3910 MB of the board |

**CPU, I/O wait and RAM are system-wide, absolute figures.** They cover the whole board, not only the container: during a first start most of the work (pulling, unpacking, building) is done by `dockerd`, `containerd` and BuildKit on the host, outside the container's cgroup, where `docker stats` would not see it.

## 3. Methodology

### 3.1 Cold state

Before each configuration everything that could shorten the first start was removed, so that each run starts from the same empty state:

```bash
docker compose -f compose-server.yml down --rmi all -v
rm -rf ~/myrtus-psm-edge
docker system prune -a --volumes -f     # images (base image included), containers, networks, volumes
docker builder prune -a -f              # BuildKit cache
docker system df                        # verified: everything at 0 B
git clone https://github.com/mdc-suite/myrtus-psm-edge.git
sync; echo 3 | sudo tee /proc/sys/vm/drop_caches   # empty the page cache
```

Both runs used the same board, SD card, network path and commit; the only difference was the `FROM` line shown above.

### 3.2 Sampling

`vmstat` ran in the background for the whole run, with 5 s of idle before and after the `compose` command:

```bash
vmstat -t -S M 1 > ~/vmstat_<config>.log &
VMSTAT_PID=$!
sleep 5
time docker compose -f compose-server.yml up -d
sleep 5
kill $VMSTAT_PID
```

### 3.3 Statistics

Mean and peak are computed over the samples inside the run window only. Excluded are the first `vmstat` line, which reports averages since boot rather than a one-second sample, and the 5 s of idle on each side, which are not part of the first start. This gives 1026 samples for the Kria run and 287 for the stock run.

**No baseline was subtracted from any figure.** The idle windows were left out of the averaging window, not subtracted from the samples: every value in §1 is the absolute load of the whole board, including the operating system and the Docker daemons. For reference, the RAM in use on the idle board just before each run (`free -m`) was 530 MB before the Kria run and 488 MB before the stock run.

The CPU time in §1.1 is an estimate derived from the mean: utilisation × 4 cores × window length.

```bash
awk 'NR==2{for(i=1;i<=NF;i++)c[$i]=i; next}
     $1~/^[0-9]+$/{n++; row[n]=$0}
     END{for(k=7;k<=n-5;k++){split(row[k],f," ");
       used=3910-f[c["free"]]-f[c["buff"]]-f[c["cache"]];
       cpu=f[c["us"]]+f[c["sy"]]; wa=f[c["wa"]];
       s_u+=used; s_c+=cpu; s_w+=wa; m++;
       if(used>p_u)p_u=used; if(cpu>p_c)p_c=cpu; if(wa>p_w)p_w=wa}
     printf "samples %d\nCPU us+sy  mean %.1f%%  peak %d%%\nIO wait    mean %.1f%%  peak %d%%\nRAM used   mean %.0f MB (%.1f%%)  peak %d MB (%.1f%%)\n",
       m, s_c/m, p_c, s_w/m, p_w, s_u/m, s_u/m/39.10, p_u, p_u/39.10}' ~/vmstat_<config>.log
```

### 3.4 Functional check

After the stock run, `test/test.sh -all` passed in full on the board: eight backends registered and measured, every energy figure in `db.yaml` non-zero (INA260 readings), round trip on both security levels, runtime switch and file sizes.

### 3.5 Limitations

- **One run per configuration.** The difference in the first start (3.6×) is far larger than the run-to-run variation we would expect, and is explained phase by phase in §1.2; the CPU and RAM figures should be read as indicative.
- **The download time depends on the network** (the board reaches the Internet through the laptop's connection sharing). The extraction, which dominates, is local to the board.
- **The window ends when the container has started.** The startup of the component inside the container (registration and measurement of the backends) runs the same code on both bases and is not included.
