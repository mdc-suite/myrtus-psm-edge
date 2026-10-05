# Base image comparison on the Kria KV260: Kria Ubuntu vs stock Ubuntu

On aarch64 the container was built on `al3monni/kria-ubuntu:22.04.5`, a snapshot of the board's own root filesystem (Ubuntu 22.04.5 IoT with the Kria tooling). The first start of the container on the board was slow, so we compared it against the stock `ubuntu:22.04` image, the base already used on x86-64.

The only change between the two configurations is the `FROM` line of the Dockerfile. The repository builds on stock Ubuntu since commit `2ac6d71`; for the Kria runs the benchmark script changes that line in its local clone:

```diff
-FROM ubuntu:22.04 AS build-env
+FROM al3monni/kria-ubuntu:22.04.5 AS build-env
```

**Outcome.** With the stock base the first start is 3.2× faster (294 s instead of 932 s, mean of 10 runs each), the image is 15× smaller, the space taken on the SD card 9.7× smaller, and the component behaves identically: `test/test.sh` passes in all 20 runs, including the INA260 energy measurement of all eight backends.

## 1. Results

Board: AMD/Xilinx Kria KV260 (4× Cortex-A53, 3910 MB RAM, root filesystem on SD card). Repository at commit `cff2910`. 10 runs per configuration, each from a cold board, on 5 October 2026 (§3). Figures are mean ± standard deviation over the 10 runs; the data of every run is in [`tools/bench_baseimage_results.csv`](tools/bench_baseimage_results.csv).

### 1.1 Summary

| Parameter | Kria Ubuntu | Stock Ubuntu | Ratio |
|---|---|---|---|
| Base image (on disk / compressed) | 7.33 GB / 2.07 GB | 109 MB / 29.7 MB | ~67× |
| Final image (on disk / compressed) | 7.69 GB / 2.17 GB | 512 MB / 132 MB | ~15× |
| Container (writable layer / virtual) | 4.4 MB / 5.53 GB | 4.4 MB / 384 MB | ~14× |
| Build cache | 7.71 GB | 513 MB | ~15× |
| Space taken on the SD card | 7.98 GB | 821 MB | 9.7× |
| **First start** | **931.9 ± 19.0 s (15m32s)** | **294.1 ± 4.3 s (4m54s)** | **3.2×** |
| CPU, user + system (mean / peak) | 18.1 ± 0.3 % / 68.3 % | 22.0 ± 0.3 % / 58.7 % | |
| CPU time over the first start | 661 ± 5 core·s | 256 ± 1 core·s | 2.6× |
| I/O wait (mean / peak) | 24.4 ± 0.5 % / 92.6 % | 11.2 ± 0.6 % / 76.8 % | 2.2× |
| RAM used (mean) | 585 ± 11 MB (14.9 %) | 593 ± 19 MB (15.2 %) | ≈ |
| RAM used (peak) | 687 ± 9 MB (17.6 %) | 658 ± 20 MB (16.8 %) | +4 % |
| Idle board before the run (RAM / CPU) | 524 ± 28 MB / 0.4 % | 510 ± 25 MB / 0.2 % | |

The sizes are the same in every run, within 0.1 %. The 95 % confidence interval of the mean first start is ±13.6 s for Kria and ±3.1 s for stock.

### 1.2 First start, by phase

From the BuildKit output of the same runs, in seconds, mean ± standard deviation.

| Phase | Kria Ubuntu | Stock Ubuntu |
|---|---|---|
| Download of the base image | 105.4 ± 0.6 (2.07 GB) | 2.4 ± 0.5 (27.7 MB) |
| Extraction of the base image | **386.0 ± 22.7** | 2.3 ± 0.0 |
| `apt-get` layer | 315.5 ± 7.3 | 170.8 ± 3.7 |
| Compilation (`gcc`, `make`) | 12.7 ± 0.8 | 11.5 ± 1.0 |
| Export of the image | 78.5 ± 3.8 | 72.0 ± 0.5 |
| Other build steps (by difference) | 17.0 | 15.8 |
| Build, up to the creation of the container | 915.0 ± 20.4 | 274.9 ± 3.9 |
| Container start | 16.9 ± 5.1 | 19.2 ± 0.7 |
| **Total** | **931.9 ± 19.0** | **294.1 ± 4.3** |

Two runs need a note; neither changes its total:

- **Run 13 (stock).** BuildKit did not print the closing `done` line of the layer download, so the CSV has 0.0 s. The progress lines show the 27.7 MB layer downloaded in 1.2 s, the value used above.
- **Run 6 (Kria).** About 12 s moved from the container start (4.3 s instead of ~17) to the export (89.2 s instead of ~77): probably an operation that usually runs when the container is created ran during the export instead. The boundary between build and container start is therefore not stable; the total is. Without run 6 the container start is 18.3 s for Kria, close to the 19.2 s of stock.

### 1.3 Reading the results

- **The first start is storage-bound, not CPU-bound.** With the Kria base, downloading and unpacking the 2 GB base takes 491 s, 53 % of the first start; the extraction onto the SD card alone takes 386 s. During it the cores are mostly waiting on the disk: I/O wait averages 24 %, with peaks of 93 % on average.
- **Where the 638 s go.** 76 % of the difference between the two configurations is the download and extraction of the base, 23 % the `apt-get` layer, which is 1.8× slower on the Kria base (316 s against 171 s). We did not investigate the `apt-get` difference further.
- **Lower utilisation, more work.** The mean CPU utilisation is lower with the Kria base (18 % against 22 %) because the cores wait on the SD card, but the run lasts 3.2× longer: it spends 2.6× more CPU time.
- **RAM is not affected.** The means differ by less than 2 % and the peaks by 4 %.
- **The results are repeatable.** The standard deviation of the first start is 1.5 % of the mean for stock and 2.0 % for Kria; the difference between the two is more than 30 times either. The slowest Kria run is the first of the campaign (run 2, 982 s); without it the Kria mean is 926 ± 7 s.
- **The application itself is small.** The layers the Dockerfile adds weigh about 100 MB compressed on both bases; with the Kria base they sit on top of 7.3 GB of board userspace that the component does not use.

## 2. What was measured

| Parameter | Tool | Definition |
|---|---|---|
| Base / final image | `docker images` | *On disk*: compressed blobs plus the unpacked snapshot, as stored by Docker's containerd image store. *Compressed*: the content as pulled from the registry. |
| Container | `docker ps -s`, once the server is up | *Writable layer*: what the running container wrote. *Virtual*: writable layer plus the unpacked image it runs on. |
| Build cache | `docker system df` | BuildKit cache left after the build |
| Space on the SD card | `df` on Docker's data directory, before and after the first start | What the first start wrote. Image and build cache share their snapshots, so their sizes above overlap; the filesystem counts the shared data once. |
| First start | `docker compose -f compose-server.yml up -d`, timed | From the launch of the command to its return, with nothing cached on the board: it includes the download of the base image, the build, the export of the image and the start of the container |
| Phases | BuildKit output (`plain`) | Duration of each build step; download and extraction from the lines of the base layer. The container start is the time from the creation of the container to the return of `compose`. |
| CPU | `vmstat 1`, columns `us + sy` | Share of the four cores busy in user and kernel space, sampled every second |
| I/O wait | `vmstat 1`, column `wa` | Share of time the cores were idle waiting on I/O; reported separately from CPU |
| RAM | `vmstat -S M 1` | Used memory = total − free − buffers − cache, in MB; percentages are relative to the 3910 MB of the board |

CPU, I/O wait and RAM cover the whole board, not only the container.

## 3. Methodology

### 3.1 Campaign

The runs were made by [`tools/bench_baseimage.sh`](tools/bench_baseimage.sh), started on the board from a copy kept outside the repository. It alternates the two configurations (stock, Kria, stock, …), so that any drift over the four and a half hours of the campaign is spread over both, until each has 10 valid runs. The repository commit is pinned for the whole campaign. A failed run would have been logged and repeated; none failed.

Every run goes through the same steps:

1. **Cold state.** Everything that could shorten the first start is removed, and the result is verified:

   ```bash
   docker compose -f compose-server.yml down --rmi all -v
   docker rm -f Test-server
   rm -rf ~/myrtus-psm-edge
   docker system prune -a --volumes -f     # images (base image included), containers, networks, volumes
   docker builder prune -a -f              # BuildKit cache
   docker system df                        # verified: everything at 0
   ```

2. **Clone** of the repository at the pinned commit; for a Kria run, the `FROM` line is changed.
3. **Pause** of 60 s, then the page cache is emptied (`sync; echo 3 > /proc/sys/vm/drop_caches`).
4. **First start**, sampled (§3.2).
5. **Sizes**, once the server inside the container is up: the eight backends are measured with the INA260 at start-up, on a quiet board.
6. **Functional check** with `test/test.sh`.

All runs used the same board, SD card, network path and commit; the only difference was the `FROM` line shown above.

### 3.2 Sampling

`vmstat` runs in the background for the whole first start, with 5 s of idle before and after the `compose` command:

```bash
vmstat -n -t -S M 1 > vmstat.log &
VMSTAT_PID=$!
sleep 5
docker compose -f compose-server.yml up -d      # timed
sleep 5
kill $VMSTAT_PID
```

### 3.3 Statistics

Within a run, mean and peak are computed over the samples of the `compose` window only: excluded are the first `vmstat` line, which reports averages since boot rather than a one-second sample, and the 5 s of idle on each side. This gives 285–297 samples per stock run and 899–968 per Kria run. The CPU time is the sum over the window of (user + system) / 100 × 4 cores × 1 s.

Every value in §1 is the absolute load of the whole board, including the operating system and the Docker daemons. The 5 s of idle before the window are reported on their own, as the idle board in §1.1, for reference.

Across runs, the figures are the mean and the sample standard deviation of the 10 valid runs of each configuration. The confidence intervals use Student's t with 9 degrees of freedom: mean ± 2.262 · sd / √10.

The computations are in `vmstat_stats()`, `build_phases()` and `summary()` of the script.

### 3.4 Functional check

In all 20 runs `test/test.sh` passed its 8 tests: eight backends registered and measured, every energy figure in `db.yaml` non-zero (INA260 readings), both ports listening, round trip on both security levels. Before the campaign, `test/test.sh -all` had also passed in full on the stock base, runtime switch and file sizes included.

## 4. Changes from the first version

The first version of this report was based on one run per configuration. The campaign revises three of its figures:

- **First start: 3.2×, not 3.6×.** The single Kria run (1044 s, extraction 467 s) was slower than all ten of the campaign (917–982 s, extraction 361–443 s).
- **Container writable layer: 4.4 MB on both bases.** The stock value of the first version (512 kB) had been read before the server finished starting; at the same point both are 4.4 MB.
- **Space on storage: 7.98 GB and 821 MB, measured.** The first version added image and build cache (15.4 GB and 1.0 GB), which share their snapshots.
