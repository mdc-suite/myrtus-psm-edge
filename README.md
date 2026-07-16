# spdocker — Build & Validation Runbook

**Repo:** https://github.com/subhadeep-banik/spdocker
**Target host:** Windows + WSL2 + Docker Desktop (Kali or Ubuntu distro — irrelevant, see §7)
**Validated:** 2026-07-15, `ubuntu:26.04` base, OpenSSL 3.5.5, gcc 15

---

## 1. What this project is

A TLS client/server that ships **8 interchangeable AES implementations**. At runtime it selects one, loads it from a shared library via `dlopen`, and uses it as the AEAD for file transfer. The selection is meant to be driven by measured throughput/energy against a policy file.

Pipeline on container start (`start.sh`):

```
reset  →  register ×8  →  gcc -shared ./LIB/*.o -o ./LIB/lib_enc.so  →  ./server
```

- `reset` — clears `LIB/`, resets `header.h` counters
- `register -c ./fN/config.txt` — compiles fN, runs a known-answer test, renames symbols, drops `enc_sXX_nYY.o` into `LIB/`
- `server` — `dlopen("./LIB/lib_enc.so")`, `dlsym("enc_s%02d_n%02d")`

---

## 2. Root cause of the out-of-the-box failure

Fresh clone fails with:

```
/usr/bin/x86_64-linux-gnu-ld.bfd: cannot find ./LIB/*.o: No such file or directory
```

**`check1.c` and `check2.c` are missing from the repo.** They are the known-answer-test templates that `register` needs.

In `register.c`:

```c
sprintf(app,"check%d.c",slevel);   // template to READ
FILE *g=fopen(app,"rb");           //   → NULL when missing
sprintf(app1,"test%d.c",slevel);
FILE *h=fopen(app1,"wb");          // ← truncates test1.c to 0 bytes IMMEDIATELY
...
if(g!=NULL){ /* write test1.c from template */ }   // ← skipped
...
gcc test1.c ./f1/aes128.o ... -o test              // ← empty file, no main
```

Chain of consequences:

| Step | Result |
|---|---|
| `check1.c` missing | `g == NULL` |
| `fopen("test1.c","wb")` | committed `test1.c` destroyed, 0 bytes |
| `gcc test1.c ...` | `undefined reference to 'main'` |
| `system("./test")` | non-zero → `TEST FAILED` → `return 5` |
| `LIB/` | never populated |
| `gcc -shared ./LIB/*.o` | glob matches nothing, passed literally to `ld` |

`start.sh` hides all of this because every `register` call is `> /dev/null 2>&1`, and the `echo "Registering..."` lines print unconditionally. "Done" is printed even when all 8 failed.

**Everything else in the repo works.** This one omission causes the whole visible failure.

---

## 3. Required modifications

### M1 — `compose-server.yml` (mandatory)

*Why:* hardcoded absolute path from the author's machine; X11 mounts that don't exist in WSL.

```yaml
services:
  ssl-server:
    build: .                  # was: /home/usi/scke/unified/
    container_name: Test-server
    networks:
      - openssl-net
    ports:
      - "5544:5544"
      - "5545:5545"
    restart: unless-stopped
    command: /app/server
    tty: true
    stdin_open: true
    cap_add:
      - NET_ADMIN
      - CAP_NET_RAW
    privileged: true
    # DELETED: volumes: (/tmp/.X11-unix, ${HOME}/.Xauthority)
    # DELETED: environment: (DISPLAY=unix$DISPLAY)

networks:
  openssl-net:
    driver: bridge
```

### M2 — `check1.c` (mandatory) — the actual fix

*Why:* missing from repo. AES-128 known-answer test, slevel 1 (used by f1, f2, f3, f7).

The marker line must be **exactly 4 spaces + `// insert_func here`** — `register.c` does `strncmp(string,"    // insert_func here",23)`.

```c
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static void phex(uint8_t* str);

int compare(unsigned char *x){
unsigned char y[16]={0x6f, 0x0f, 0xa9, 0x16, 0x3f, 0x92, 0xc5, 0x60, 0x27, 0xea, 0x4d, 0x32, 0x94, 0x55, 0xd7, 0xd7};
  for(int i =0 ; i<16; i++)
  if(x[i]!=y[i]) return 1;
  return 0;
}

int main(void)
{
    unsigned char Key[16]={0x84 , 0x81 , 0x85 , 0xdf , 0xa9 , 0x51 , 0xf1 , 0x1e , 0x13 , 0x97 , 0x24 , 0x8a , 0x6a , 0x69 , 0x8b , 0x17};
    unsigned char PT[16]= {0xad , 0x40 , 0xa8 , 0x96 , 0xb1 , 0xc7 , 0xea , 0xa0 , 0x52 , 0xb1 , 0xa7 , 0x0b , 0xd6 , 0x45 , 0xdb , 0x66};
    unsigned char CT[16];
    
    // insert_func here
    
    
    if(compare(CT)!=0) return 1;
    else return 0;
}

static void phex(uint8_t* str)
{
    uint8_t len = 16;
    unsigned char i;
    for (i = 0; i < len; ++i)
        printf("%.2x", str[i]);
    printf("\n");
}
```

### M3 — `check2.c` (mandatory)

*Why:* same, for slevel 2 (f4, f5, f6, f8). Vector is NIST SP 800-38A AES-256-ECB.

Identical to M2 except `compare()` and `main()`:

```c
int compare(unsigned char *x){
unsigned char y[16]={0xF3, 0xEE, 0xD1, 0xBD, 0xB5, 0xD2, 0xA0, 0x3C, 0x06, 0x4B, 0x5A, 0x7E, 0x3D, 0xB1, 0x81, 0xF8};
  for(int i =0 ; i<16; i++)
  if(x[i]!=y[i]) return 1;
  return 0;
}

int main(void)
{
    unsigned char Key[32]={0x60, 0x3D, 0xEB, 0x10, 0x15, 0xCA, 0x71, 0xBE, 0x2B, 0x73, 0xAE, 0xF0, 0x85, 0x7D, 0x77, 0x81, 0x1F, 0x35, 0x2C, 0x07, 0x3B, 0x61, 0x08, 0xD7, 0x2D, 0x98, 0x10, 0xA3, 0x09, 0x14, 0xDF, 0xF4};
    unsigned char PT[16]= {0x6B, 0xC1, 0xBE, 0xE2, 0x2E, 0x40, 0x9F, 0x96, 0xE9, 0x3D, 0x7E, 0x11, 0x73, 0x93, 0x17, 0x2A};
    unsigned char CT[16];
    
    // insert_func here
    
    
    if(compare(CT)!=0) return 1;
    else return 0;
}
```

---

## 4. Clean-slate procedure

```bash
# 1. tear down container + image + network
cd ~/myrtus/spdocker 2>/dev/null && docker compose -f compose-server.yml down --rmi all -v
docker rm -f Test-server Test-client 2>/dev/null
docker rmi -f spdocker-ssl-server 2>/dev/null

# 2. drop build cache (this is what makes "CACHED" layers lie to you)
docker builder prune -af

# 3. verify nothing is left
docker ps -a | grep -i test
docker images | grep spdocker

# 4. fresh clone
cd ~ && rm -rf spdocker-clean
git clone https://github.com/subhadeep-banik/spdocker spdocker-clean
cd spdocker-clean
```

> `docker system prune -af` also works but nukes **all** unused images on the host, not just this project's.

---

## 5. Full build & run

```bash
cd ~/spdocker-clean

# --- validate compose ---
docker compose -f compose-server.yml config

# --- build (~85 s; likwid alone is ~70 s) ---
docker compose -f compose-server.yml up -d --build
docker logs -f Test-server
```

**Expected log — the pass condition:**

```
Resetting Initial Configuration
Registering Implementation in ./f1
...
Registering Implementation in ./f8
Done
Creating Shared Library lib_enc.so      ← NO ld error under this line
Updating Paths
OpenSSL 3.5.5 27 Jan 2026 ...
```

The single most important signal: **no `cannot find ./LIB/*.o`**.

---

## 6. Verification

```bash
# 6.1 — 8 objects + lib, all freshly timestamped
docker exec Test-server ls -la /app/LIB

# 6.2 — 8 exported symbols
docker exec Test-server sh -c 'nm -D /app/LIB/lib_enc.so | grep enc_s'

# 6.3 — counters must read 4 per level
docker exec Test-server sh -c 'grep "///" /app/header.h'      # ///1-04  ///2-04

# 6.4 — server listening
docker exec Test-server sh -c 'lsof -i -P -n | grep LISTEN'   # *:5544, *:5545

# 6.5 — round-trip
docker exec -it Test-server sh -c 'cd /app && ./client -s 1 -f rfile -i 127.0.0.1:5544'

# 6.6 — correctness
docker exec Test-server sh -c 'cmp /app/rfile /app/Downloads/filename-ekm* && echo IDENTICAL'
```

### Reference baseline (x86-64, verified)

| Check | Expected |
|---|---|
| `LIB/` | 8 × `enc_s0*.o` + `lib_enc.so` |
| `nm -D` | `enc_s01_n01`..`enc_s01_n04`, `enc_s02_n01`..`enc_s02_n04`, all `T` |
| `header.h` | `///1-04`, `///2-04` |
| Client | `Handshake Complete--Key material established` / `Entire File Sent 10016 bytes` |
| Server | `Starting with enc_s02_n02` / `File saved to ./Downloads/filename-ekmNNNNN` |
| `cmp` | `IDENTICAL` |

`10016 = 10000 + 16` (AEAD tag). `cd /app` is **mandatory** — `dlopen` uses the relative path `./LIB/lib_enc.so`.

---

## 7. Known issues — expected, not your fault

| # | Symptom | Cause | Impact |
|---|---|---|---|
| 1 | `Cannot get access to MSRs` ×8 | WSL2 kernel exposes no `/dev/cpu/*/msr`; `privileged` can't help | **Profiling dead.** Registration still succeeds — `cp → LIB/` happens *before* profiling |
| 2 | `db.yaml` overwritten with empty measurements | consequence of #1 | `synthesize` can't compute `mode` |
| 3 | `-s 1` ignored; always `enc_s02_n02` | `mode` stuck at compiled-in default `98` | see §8 |
| 4 | `Recieved 9216 bytes` for a 10000 B file | counter tallies 9×1024 chunks, drops the 784 B remainder | cosmetic — `cmp` proves data is intact |
| 5 | `rate inf bps` | elapsed time rounds to 0 → div-by-zero | cosmetic |
| 6 | `register` returns 0 even when it refuses | `main` falls off the end after `collide()` | why `start.sh` printed "Done" over 8 failures |
| 7 | `bool rval;` uninitialised in `register.c` | read at `if(rval==0)` when `LIB/` is empty | works at `-O0`; latent |
| 8 | First `register` run destroys `test1.c`/`test2.c` | `fopen(...,"wb")` before template check | irreversible without `check*.c` |
| 9 | `Symbol X already exists` | `collide()` vs. a non-empty `LIB/` | correct behaviour — run `./reset` first |

**Host distro is irrelevant.** Docker Desktop runs every Linux container in its own WSL2 VM. Kali vs. Ubuntu changes nothing; both talk to the same daemon and see the same container. Reinstalling the distro does not fix any of the above.

---

## 8. How selection actually works (`encrypt02.c`)

```c
#define fbits(y)  ((y) & 0xc0)>>6    // function class
#define sbits(y)  ((y) & 0x30)>>4    // security level
#define ibits(y)  ((y) & 0x0f)       // implementation index
#define fetch(mode)  slevel = sbits(mode);  num = ibits(mode);
                     sprintf(buf,"enc_s%02d_n%02d",slevel,num);
                     op = (function) dlsym(cx->handle, buf);
```

One byte encodes everything. Default `mode = 98 = 0x62`:

```
0x62 = 0110 0010  →  fbits=1, sbits=2, ibits=2  →  enc_s02_n02
```

That is exactly what the server reports. Since profiling (#1) blocks `synthesize`, `mode` never moves off 98 — hence issue #3. **This is not a bug you introduced**; it's the honest consequence of having no energy counters.

### Implementation map — the contract

| dir | function | build flags | → symbol | size |
|---|---|---|---|---|
| f1 | `aes128` | plain C | `enc_s01_n01` | 4952 |
| f2 | `AES_enc` | plain C | `enc_s01_n02` | 4560 |
| f3 | `aes_ecb_encrypt` | `-DUNROLL_TRANSPOSE` (bitsliced) | `enc_s01_n03` | 41408 |
| f7 | `aes128` | **`-maes -msse4.1`** (AES-NI) | `enc_s01_n04` | 2968 |
| f4 | `aes256` | plain C | `enc_s02_n01` | 5224 |
| f5 | `AES256_enc` | plain C | `enc_s02_n02` | 4632 |
| f6 | `aes256_ecb_encrypt` | bitsliced | `enc_s02_n03` | 41688 |
| f8 | `aes256` | **`-maes -msse4.1`** (AES-NI) | `enc_s02_n04` | 6392 |

Registration order **is** the numbering. Both endpoints must agree on it or the same `mode` byte selects different ciphers on each side.

### How symbol renaming works (`gen.c` → `generate`)

```
wrapper.c:  #define aes128 enc_s01_n01
            #include "./f1/aes128.c"
   ↓ gcc -E -P
source.c    (1.6 MB, fully preprocessed)
   ↓ sed 's/aes128.c/source.c/' Makefile > Makefile_new ; make
LIB/enc_s01_n01.o
```

This is why 8 implementations with identical internal symbols (`SubBytes`, `sbox`, `Rcon`…) can coexist in one `.so`.

---

## 9. Upstream issue worth filing

> `check1.c` and `check2.c` are referenced by `register.c` (`sprintf(app,"check%d.c",slevel)`) but are not present in the repository. Without them `register` truncates `test%d.c` to zero bytes via `fopen(...,"wb")`, the subsequent `gcc test%d.c` fails with `undefined reference to 'main'`, and `LIB/` is never populated — so `gcc -shared ./LIB/*.o` fails on a fresh clone. Because `start.sh` redirects `register` output to `/dev/null` and echoes success unconditionally, the failure is silent.

Suggested secondary fixes: initialise `bool rval = 0;`; return non-zero from `register` when `collide()` refuses; check the `check%d.c` template exists **before** opening `test%d.c` for writing.
