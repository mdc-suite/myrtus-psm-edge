# Kria KV260 — board bring-up

Everything needed to take a Kria KV260 Starter Kit from an empty microSD to a board that builds and runs this project. Once you reach the end, continue with [`README.md`](README.md).

The board is a test target: it clones the repository, builds the container and runs it. Development happens elsewhere, so the board only ever needs to *read* the repository.

This guide uses a Windows PC to flash the card and to open the serial console; on Linux the same steps work with equivalent tools. The board gets its internet access either from your router or from the PC (§5).

---

## 1. What you need

- **Kria KV260 Vision AI Starter Kit** and its 12 V power supply
- **microSD card**, 32 GB or larger
- **USB-A to micro-USB cable** for the serial console
- **Ethernet cable**, to your router or to the PC (§5)
- On the PC: [balenaEtcher](https://etcher.balena.io/), [PuTTY](https://www.putty.org/), and [7-Zip](https://www.7-zip.org/) to decompress the image

### Boot firmware

Ubuntu 22.04 needs updated Kria SOM boot firmware — 2022.1 or later is recommended, and the board may not boot at all with mismatched firmware. If this board has only ever run Ubuntu 20.04, update the firmware first following AMD's Kria wiki; if it has already booted 22.04 before, you can skip this.

---

## 2. Flash the OS image

Download the **Certified Ubuntu 22.04 LTS for AMD** image for Kria from <https://ubuntu.com/download/amd>. You want the 22.04 release, not 24.04 — the rest of this project is built against jammy.

The file arrives compressed as `.img.xz`. **Decompress it before flashing.** balenaEtcher advertises streaming decompression, but on this image it reliably fails partway through, and the failure is not always obvious — you can end up with a card that flashes "successfully" and does not boot.

On Windows, right-click the file and extract it with 7-Zip (*Extract Here*); on Linux, `unxz -kv <image-name>.img.xz` does the same and keeps the compressed original. Then point balenaEtcher at the resulting `.img` and write it to the microSD.

---

## 3. Connect the serial console

Insert the microSD into the board, connect the micro-USB cable to the PC, and connect Ethernet (§5). Do not power on yet.

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

## 5. Internet connection

The board needs internet access to install Docker and to download the repository and the container images. Connect its Ethernet port in one of two ways. In both, the board gets its address automatically: there is nothing to configure on the board.

### Through the router

Connect the board to your router with the Ethernet cable. This is the simplest option and does not depend on the PC.

### Through the PC (Windows Internet Connection Sharing)

Connect the board directly to the PC's Ethernet port. On Windows, open `ncpa.cpl`, right-click the adapter that has internet access (Wi-Fi, typically), choose **Properties → Sharing**, and enable *Allow other network users to connect through this computer's Internet connection*, selecting the Ethernet adapter connected to the board as the home networking connection. Windows gives the board an address in `192.168.137.x`.

**Known issue.** Internet Connection Sharing stops working after the PC restarts or sleeps, and after some time without traffic: the board gets no address, or it gets one but cannot reach the internet. The fix is to reset the sharing: in `ncpa.cpl`, untick the sharing box and apply, then tick it again and apply. If the board still has no address, unplug the Ethernet cable and plug it back in.

### Find the address and switch to SSH

In both cases, read the board's address from the serial console:

```bash
ip -4 addr show eth0
```

The address is on the `inet` line, for example `inet 192.168.1.20/24`. Note it down, then check that the board reaches the internet:

```bash
ping -c 2 archive.ubuntu.com
```

From now on you can work from the PC over SSH:

```bash
ssh ubuntu@<board-address>
```

The address can change, for example when the board or the router restarts. If SSH stops connecting, read the address again from the serial console.

**Keep the serial console available.** SSH depends on the network and on services that a package operation may restart; if a session dies mid-`dpkg`, the serial console is how you get back in to run `sudo dpkg --configure -a`.

---

## 6. Install Docker

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

## 7. Build and run the project

The board is ready. Continue with [`README.md`](README.md), *Build and run*, from step 2: connect over SSH with the address from §5, clone the repository, build and start the container, run the tests.

The first start takes about 5 minutes on the board and about 0.8 GB of the card; [`BASEIMAGE.md`](BASEIMAGE.md) has the measurements.

---

## 8. Troubleshooting

**Nothing appears in PuTTY.** Wrong COM port (use the second one), or flow control left at XON/XOFF. Press Enter a couple of times in case the board has already finished booting.

**The card flashed fine but the board does not boot.** Almost always either the `.xz` was flashed without decompressing first, or the SOM boot firmware predates 22.04 support.

**The board has no address, or no internet, through the PC.** The known issue of Internet Connection Sharing: reset the sharing (§5).

**SSH stops connecting.** The board's address has probably changed. Read it again from the serial console with `ip -4 addr show eth0`.

**`permission denied` from Docker.** The `docker` group membership has not been applied to this session yet — reconnect.
