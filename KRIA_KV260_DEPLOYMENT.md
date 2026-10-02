# Kria KV260 — board bring-up

Everything needed to take a Kria KV260 Starter Kit from an empty microSD to a board that builds and runs this project. Once you reach the end, continue with [`README.md`](README.md).

The board is a test target: it clones the repository, builds the container and runs it. Development happens elsewhere, so the board only ever needs to *read* the repository.

This guide assumes a Windows host, because the network setup below relies on Windows Internet Connection Sharing. On a Linux host the flashing and serial steps are equivalent; only the networking section differs.

---

## 1. What you need

- **Kria KV260 Vision AI Starter Kit** and its 12 V power supply
- **microSD card**, 32 GB or larger — the OS image plus the container images will use most of it
- **USB-A to micro-USB cable** for the serial console
- **Ethernet cable** between the board and the host PC
- On the host: [balenaEtcher](https://etcher.balena.io/), [PuTTY](https://www.putty.org/), and [7-Zip](https://www.7-zip.org/) to decompress the image

### Boot firmware

Ubuntu 22.04 needs updated Kria SOM boot firmware — 2022.1 or later is recommended, and the board may not boot at all with mismatched firmware. If this board has only ever run Ubuntu 20.04, update the firmware first following AMD's Kria wiki; if it has already booted 22.04 before, you can skip this.

---

## 2. Flash the OS image

Download the **Certified Ubuntu 22.04 LTS for AMD** image for Kria from <https://ubuntu.com/download/amd>. You want the 22.04 release, not 24.04 — the rest of this project is built against jammy.

The file arrives compressed as `.img.xz`. **Decompress it before flashing.** balenaEtcher advertises streaming decompression, but on this image it reliably fails partway through, and the failure is not always obvious — you can end up with a card that flashes "successfully" and does not boot.

On Windows, right-click the file and extract it with 7-Zip (*Extract Here*); on Linux, `unxz -kv <image-name>.img.xz` does the same and keeps the compressed original. Then point balenaEtcher at the resulting `.img` and write it to the microSD.

---

## 3. Connect the serial console

Insert the microSD into the board, connect the micro-USB cable to the host, and connect Ethernet. Do not power on yet.

Open Device Manager on Windows (`devmgmt.msc`) and look under **Ports (COM & LPT)**. Two COM ports will be listed. The console is on the **second** one — the higher-numbered of the pair.

Configure PuTTY:

| Setting | Value |
|---|---|
| Connection type | Serial |
| Serial line | the second COM port |
| Speed | 115200 |
| Data bits | 8 |
| Stop bits | 1 |
| Parity | None |
| **Flow control** | **None** |

Flow control is the one that catches people out: PuTTY defaults to XON/XOFF, and with that set you may see nothing at all. Save the session so you do not have to configure it again.

Open the session, then power on the board. Boot messages should start scrolling within a few seconds. The first boot is slower than later ones because the filesystem is expanded to fill the card.

---

## 4. First login

Log in as `ubuntu` with password `ubuntu`. You will immediately be asked to change it, and the sequence trips people up:

1. `Current password:` — this is still `ubuntu`
2. `New password:`
3. `Retype new password:`

Nothing is echoed as you type, not even asterisks. That is normal. The new password must be at least 8 characters or it is rejected as a bad password, and it also cannot be too close to the username or a dictionary word.

---

## 5. Networking over Windows ICS

The board gets its network through Internet Connection Sharing on the Windows host, which puts it on the `192.168.137.0/24` subnet.

On Windows, open `ncpa.cpl`, right-click the adapter that has internet access (Wi-Fi, typically), choose **Properties → Sharing**, and enable *Allow other network users to connect through this computer's Internet connection*, selecting the Ethernet adapter connected to the board as the home networking connection.

On the board, check the assigned address:

```bash
ip a show eth0
```

### Assign a static IP (recommended)

ICS also runs a DHCP server that hands the board an address, but it is not reliable: after a while — typically once Windows sleeps, reboots or changes network — it stops answering, the board's lease expires without being renewed, `eth0` drops its IPv4 address and SSH stops working.

To avoid this, give the board a fixed address in the ICS subnet **the first time you log in**, from the serial console. First find the name NetworkManager uses for the wired connection:

```bash
nmcli -t -f NAME,DEVICE con show
```

This prints `<connection name>:<device>`, e.g. `Wired connection 1:eth0`. The commands below take the connection name (the left part, quoted because of the spaces), not the device — passing `eth0` fails with `unknown connection 'eth0'`.

```bash
sudo nmcli con mod "Wired connection 1" \
  ipv4.method manual \
  ipv4.addresses 192.168.137.50/24 \
  ipv4.gateway 192.168.137.1 \
  ipv4.dns "192.168.137.1 8.8.8.8"
sudo nmcli con up "Wired connection 1"
```

NetworkManager stores this, so it survives reboots of both machines. `.50` is an arbitrary choice; the Windows side of ICS is always `.1`.

Confirm the board can actually reach the internet, not just the host:

```bash
ping -c 2 archive.ubuntu.com
```

The static address makes SSH independent of ICS's DHCP, but internet access still goes through ICS's NAT. **If the host is reachable but the internet is not**, the fix is to uncheck the sharing box in `ncpa.cpl`, apply, then re-check it and apply again. Restarting the `SharedAccess` service does *not* reliably fix this, and doing so requires a genuinely elevated PowerShell prompt anyway.

### Staying on DHCP

If you skip the static address, check whether one has been assigned with `ip a show eth0` — you are looking for an `inet 192.168.137.x`. If there is none, request one:

```bash
sudo dhclient -v eth0
```

If `dhclient` loops on `DHCPDISCOVER` with no reply, apply the same ICS toggle described above. Expect to repeat this whenever the lease is lost; the address is not guaranteed to be the same afterwards, and `arp -a` from PowerShell shows what is on the `192.168.137.x` subnet.

### Switching to SSH

Once the network is up you can leave the serial console behind for most work:

```bash
ssh ubuntu@192.168.137.50
```

(or whatever address you chose; on DHCP, the one currently assigned).

**Keep the serial console available.** SSH depends on the network and on services that a package operation may restart; if a session dies mid-`dpkg`, the serial console is how you get back in to run `sudo dpkg --configure -a`.

---

## 6. Install Docker

> If you intend to regenerate the project's base image from this board, take the rootfs snapshot **now, before installing Docker**. See [`LOGBOOK.md`](LOGBOOK.md) §12 — a board with Docker already running carries an image store that would otherwise end up inside the snapshot.

```bash
curl -fsSL https://get.docker.com | sudo sh
sudo usermod -aG docker $USER
```

The convenience script installs Docker Engine and the compose plugin.

The group change does **not** apply to your current session. Log out and back in, then confirm:

```bash
docker version
```

If you get `permission denied while trying to connect to the Docker API`, the session is still the old one — reconnect. You can keep working with `sudo` in the meantime.

---

## 7. Git access

The board only clones and pulls; it never pushes. What it needs depends on the repository's visibility:

- **Public repository:** nothing. Clone over HTTPS, as in §8.
- **Private repository:** give the board a key with **read-only** access to this repository alone, i.e. a *deploy key*.

To create the key on the board:

```bash
ssh-keygen -t ed25519 -C "kria-board"
cat ~/.ssh/id_ed25519.pub
```

Press Enter at both prompts to accept the default path and no passphrase. Copy the entire printed line — key type, key body and comment — into the repository's **Settings → Deploy keys → Add deploy key**, and leave *Allow write access* unchecked. The `.pub` file is the public half and is meant to be shared; the file without the extension is the private key and never leaves the board.

Verify:

```bash
ssh -T git@github.com
```

The first connection asks you to confirm GitHub's host key — type `yes` in full. Success is a greeting telling you that GitHub does not provide shell access; that message is the expected result, not an error.

---

## 8. Build and run the project

```bash
git clone https://github.com/mdc-suite/myrtus-psm-edge.git     # private repository: git@github.com:mdc-suite/myrtus-psm-edge.git
cd myrtus-psm-edge
docker compose -f compose-server.yml up --build
```

To test a newer version later, `git pull` and run the same `docker compose` command again. From here, follow [`README.md`](README.md) for what a successful start looks like and how to run the tests.

Expect the first build to pull a couple of GB for the base image and then to take a few minutes. Keep an eye on free space — `df -h /` — since the OS, the base image and the built application together will use a substantial share of a 32 GB card.

---

## 9. Check the power sensor

The energy measurement on ARM reads the SOM's INA260 (LOGBOOK M12). Two commands confirm it is exposed on the image you flashed:

```bash
for h in /sys/class/hwmon/hwmon*; do echo "$h: $(cat $h/name)"; done   # expect ina260_u14
sudo xmutil xlnx_platformstats -p                                      # "SOM total power"
```

The subcommand is `xlnx_platformstats`, not `platformstats`; the latter exists in older documentation and is rejected by the version on this image. The `hwmonN` index changes across boots, so the code looks the device up by name and you should too.

The container reads the same sysfs path directly, which works because `compose-server.yml` runs it privileged. Nothing needs to be bind-mounted.

For measurement campaigns the board should be otherwise idle. The timers that wake up on their own are worth stopping first, and re-enabling afterwards:

```bash
sudo systemctl stop unattended-upgrades.service anacron.timer dpkg-db-backup.timer logrotate.timer \
                    apt-daily.timer apt-daily-upgrade.timer man-db.timer motd-news.timer
```

---

## 10. Troubleshooting

**Nothing appears in PuTTY.** Wrong COM port (use the second one), or flow control left at XON/XOFF. Press Enter a couple of times in case the board has already finished booting.

**The card flashed fine but the board does not boot.** Almost always either the `.xz` was flashed without decompressing first, or the SOM boot firmware predates 22.04 support.

**`dhclient` never gets a lease.** Toggle the ICS checkbox off and on in `ncpa.cpl`.

**`permission denied` from Docker.** The `docker` group membership has not been applied to this session yet — reconnect.
