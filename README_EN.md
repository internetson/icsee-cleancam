# CleanCam OS

<p align="center">
  <b>Open-source custom firmware for Xiongmai / ICSee Wi-Fi cameras powered by Goke GK7201V300</b><br>
  <i>No Chinese cloud services, no telemetry, native dual-stream RTSP HEVC, full ONVIF 2.0 / PTZ, and modern bilingual HTML5 WebUI</i>
</p>

<p align="center">
  <a href="README.md">🇷🇺 Русский</a> | <b>🇬🇧 English</b>
</p>

---

## 📋 About the Project

**CleanCam OS** is an alternative, lightweight custom firmware for pan-tilt Wi-Fi IP cameras based on the **Goke GK7201V300** SoC (**SSV6158M** Wi-Fi chip / SC2336 / SC3336 CMOS sensors), sold under the **ICSee / Xiongmai (XM)** brand.

The factory stock firmware suffers from vendor lock-in to Chinese cloud services (XMeye), constant telemetry reporting, proprietary protocols, lack of a modern web interface (it relied on legacy ActiveX plugins in Internet Explorer), broken ONVIF compliance, and high latency.

**CleanCam OS fixes these issues completely:**
- 🚫 **100% Privacy:** completely stripped of Chinese cloud daemons, P2P trackers (`cloud.xmeye.net`, `push.umeye.cn`, `secu100.net`), and cellular telemetry.
- 🚀 **Native Dual-Stream RTSP Server (`xm_rtspd`):** direct capture of H.265/HEVC video frames from hardware encoder with zero latency and ultra-low CPU/RAM overhead.
- 🎥 **Full ONVIF 2.0 / PTZ:** seamless compatibility with **Frigate NVR** (autotracking, object detection), **Home Assistant**, **Shinobi**, **Agent DVR**, and **ONVIF Device Manager (ODM)**.
- 🌐 **Modern HTML5 WebUI (Ports 80 & 8080):** responsive single-page web app with **instant bilingual language switching (RU / EN)**, working on desktop browsers and mobile devices.
- 🕹️ **Pan-Tilt Control (PTZ):** on-screen virtual joystick, keyboard arrow navigation (`↑`, `↓`, `←`, `→` / `WASD`), and 8/16 camera position presets.
- 💡 **Lighting Management:** smart Dual-Light mode switching, manual control for white floodlight LED and IR night backlight.
- 🖱️ **Interactive OSD:** click and cycle date/time stamp and camera title directly on the live video preview in real time.
- 📶 **Built-in Wi-Fi Manager:** wireless airspace scanning, quick network selection, and connection switching directly from the browser.
- 🔘 **Hardware Reset Button Daemon (`xm_resetd`):** holding the physical reset button for 10 seconds resets credentials to default `admin/admin` and restores the fallback setup AP.
- 🔓 **Full Root Access:** unlocked Telnet on port 23 (login: `root`, no password).

---

## 📡 Network Services & Ports

| Protocol / Service | Port | Path / Parameters | Description |
|---|---|---|---|
| **WebUI (HTTP)** | `80` & `8080` | `http://<IP>/` | Camera management dashboard (user: `admin`, pass: `admin`) |
| **RTSP Main** | `554` | `rtsp://<IP>:554/live/ch0` | Main stream (1920x1080 @ 12 fps, HEVC/H.265) |
| **RTSP Sub** | `554` | `rtsp://<IP>:554/live/ch1` | Sub stream for NVR motion detection (800x448 @ 12 fps, HEVC/H.265) |
| **ONVIF SOAP** | `8899` | `http://<IP>:8899/onvif/device_service` | ONVIF Device, Media 1.0, PTZ 2.0 (auth: `admin/admin`) |
| **WS-Discovery** | `3702` (UDP) | Multicast | Network auto-discovery |
| **Telnet (Root)** | `23` | User: `root`, password: *none* | System Linux BusyBox shell |

---

## 🔌 HTTP REST API (for Home Assistant & Automations)

The camera exposes lightweight HTTP CGI endpoints for smart home automations:

### 1. Lighting Control (`/cgi-bin/light`)
* `GET /cgi-bin/light?action=white_on` - force turn on white LED spotlight.
* `GET /cgi-bin/light?action=white_off` - force turn off white LED spotlight.
* `GET /cgi-bin/light?action=ir_on` - force turn on IR illumination (night B/W mode).
* `GET /cgi-bin/light?action=ir_off` - force turn off IR illumination (day color mode).
* `GET /cgi-bin/light?action=auto` - return to automatic smart mode (Dual-Light).
* `GET /cgi-bin/light` - returns JSON status of white light and IR.

### 2. Pan-Tilt Mechanism Control (`/cgi-bin/ptz`)
* `GET /cgi-bin/ptz?action=up&speed=5` - tilt up
* `GET /cgi-bin/ptz?action=down&speed=5` - tilt down
* `GET /cgi-bin/ptz?action=left&speed=5` - pan left
* `GET /cgi-bin/ptz?action=right&speed=5` - pan right
* `GET /cgi-bin/ptz?action=stop` - stop movement

### 3. Hardware Snapshot in JPEG (`/cgi-bin/snapshot`)
* `GET /cgi-bin/snapshot` - captures an instant JPEG frame for Home Assistant cards or Telegram alerts.

### 4. System Status (`/cgi-bin/status`)
* `GET /cgi-bin/status` - returns JSON with IP, uptime, wireless mode, and system load.

---

## 💾 Installation via MicroSD Card

Flashing partitions `custom` (`mtd4`, 192 KB) and `web` (`mtd3`, 128 KB) is completely safe and performed in "one click" using a standard MicroSD card. Bootloader (`u-boot`) and kernel (`romfs`) partitions are never touched, ensuring **full anti-brick protection**.

### Step-by-Step Instructions:
1. Download **`cleancam_sd_installer.zip`** from [Releases](../../releases).
2. Format a MicroSD card (1 GB to 64 GB) in **FAT32**.
3. Extract all files from the archive directly to the root of the card:
   ```text
   [MicroSD Root]/
   ├── xm_autorun.sh       # Auto-deployment script
   ├── xm_flasher          # Autonomous SPI Flash flasher binary
   ├── busybox             # Command-line tools
   ├── custom-x.squashfs   # Services and configuration partition image (mtd4)
   └── web-x.squashfs      # WebUI and CGI scripts partition image (mtd3)
   ```
4. Insert the card into the **powered-off** camera.
5. Connect camera power. The `xm_autorun.sh` script will:
   * Launch Root Telnet on port 23.
   * Erase and reflash `/dev/mtd4` and `/dev/mtd3`.
   * Safely reboot into CleanCam OS (~20–30 seconds).
6. Remove the MicroSD card.
7. Open `http://<CAMERA_IP>/` in your browser (login: `admin`, password: `admin`).

> [!NOTE]
> If you forget your password or change your Wi-Fi router, hold down the physical **Reset** button for **10 seconds** - the camera will reset credentials to `admin/admin` and launch the fallback `CleanCam_Setup` AP.

---

## 🛠️ Local Build from Source

### Requirements:
* **Python** 3.8+
* **Zig** compiler (`0.11.0` - `0.14.0`) - used for cross-compiling ARM binaries (`arm-linux-musleabihf`).
* **squashfs-tools** (`mksquashfs` with `xz` compression):
  * Linux (Ubuntu/Debian): `sudo apt-get install squashfs-tools`
  * Windows: prebuilt tools are included in `tools/squashfs-tools/`

### Build Command:
Run the master build script from the root directory:

```bash
python build.py
```

### What the build script does:
1. Cross-compiles static ARM binaries with `zig cc`:
   * `xm_rtspd` (RTSP server + ONVIF daemon)
   * `xm_auth` (CGI session and auth manager)
   * `xm_resetd` (GPIO reset button daemon)
   * `xm_snap` (Hardware JPEG snapshot capturer)
   * `xm_setcfg` (Sensor and DVRIP parameter updater)
2. Compresses `build_clean/custom-x` into `custom-x.squashfs` (strict limit: 196,608 bytes).
3. Compresses `build_clean/web-x` into `web-x.squashfs` (strict limit: 131,072 bytes).
4. Sets standard file permissions (`rwxr-xr-x` for executables).
5. Packages legacy U-Boot images (`.squashfs.img`).
6. Generates the flash-ready **`dist/cleancam_sd_installer.zip`** package.

---

## 🚀 CI/CD Pipeline (GitHub Actions)

An automated GitHub Actions workflow is included (`.github/workflows/build-and-release.yml`):

1. **Automated Validation:** runs on every push and PR to `main`/`master`.
2. **Release Publishing on Git Tags:**
   Tag a version:
   ```bash
   git tag v1.0.0
   git push origin v1.0.0
   ```
   GitHub Actions will automatically:
   * Spin up an Ubuntu container.
   * Install `squashfs-tools` and `zig`.
   * Run `python3 build.py`.
   * Create a **GitHub Release** with `cleancam_sd_installer.zip` and standalone images attached.
3. **Manual Trigger:** use the **Run workflow** button on the Actions tab to trigger a build anytime.

---

## 📂 Repository Structure

```text
├── .github/workflows/       # GitHub Actions CI/CD release pipeline
├── build_clean/
│   ├── custom-x/            # Partition mtd4 root (scripts, configs, daemons)
│   └── web-x/               # Partition mtd3 root (bilingual HTML5 WebUI, CGI)
├── sd_card_installer/       # SD card deployment bundle
│   ├── xm_autorun.sh        # Flash script
│   ├── xm_flasher           # 10KB ARM SPI Flash flasher
│   └── busybox              # Static Busybox binary
├── src/                     # C source code for camera system daemons
│   ├── xm_rtspd.c           # Dual-stream RTSP (HEVC) & ONVIF 2.0 / PTZ daemon
│   ├── xm_auth.c            # Authentication & session CGI daemon
│   ├── xm_resetd.c          # Physical hardware reset button daemon (10s hold)
│   ├── xm_snap.c            # Hardware JPEG snapshot capture utility
│   └── xm_setcfg.c          # Sensor and DVRIP parameter updater
├── scripts/                 # Build helper scripts
│   ├── build_clean_images.py# SquashFS packaging and U-Boot image builder
│   └── build_xm_flasher.py  # Standalone ARM SPI Flash flasher assembler
├── tests/                   # Test suite and camera tools
│   ├── test_onvif.py        # Complete 17-test ONVIF verification suite
│   └── remote_exec.py       # Remote Telnet execution utility
├── build.py                 # Master cross-platform build script
├── .gitignore               # Ignored build outputs and dumps
├── README.md                # Russian documentation
└── README_EN.md             # English documentation (this file)
```

---

## ⚖️ License

Distributed under the MIT License. Use at your own risk. Authors are not responsible for any damage to your hardware.
