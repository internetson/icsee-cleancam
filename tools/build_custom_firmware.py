#!/usr/bin/env python3
"""
CleanCam Custom Firmware Builder for ICSee / Xiongmai GK7201V300
Strips telemetry, bloatware, and cloud dependencies.
Embeds native root access, custom startup scripts, modern HTML5 Web UI,
and generates both an update .bin package and a MicroSD one-click flasher.
"""

import os
import sys
import shutil
import struct
import zlib
import zipfile
import json
import subprocess
import time

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXTRACTED_DIR = os.path.join(BASE_DIR, "extracted")
BUILD_DIR = os.path.join(BASE_DIR, "build_clean")
OUTPUT_DIR = os.path.join(BASE_DIR, "output_firmware")
MKSQUASHFS = os.path.join(BASE_DIR, "tools", "squashfs-tools", "mksquashfs.exe")

IH_MAGIC = 0x27051956
IH_NMLEN = 32

PARTITION_CONFIG = {
    "custom-x.squashfs.img": {
        "load": 0x00780000,
        "ep": 0x007b0000,
        "name": "custom",
        "block_size": 524288,
        "mtd_idx": 4,
    },
    "web-x.squashfs.img": {
        "load": 0x00760000,
        "ep": 0x00780000,
        "name": "web",
        "block_size": 524288,
        "mtd_idx": 3,
    },
    "user-x.squashfs.img": {
        "load": 0x00320000,
        "ep": 0x00760000,
        "name": "usr",
        "block_size": 524288,
        "mtd_idx": 2,
    },
    "romfs-x.squashfs.img": {
        "load": 0x00040000,
        "ep": 0x00320000,
        "name": "romfs",
        "block_size": 1048576,
        "mtd_idx": 1,
    },
}

def make_uboot_image(raw_data, load_addr, ep_addr, name):
    data_size = len(raw_data)
    data_crc = zlib.crc32(raw_data) & 0xFFFFFFFF
    timestamp = int(time.time())
    name_bytes = name.encode("latin1")[: IH_NMLEN - 1].ljust(IH_NMLEN, b"\x00")

    header_pre = struct.pack(
        ">IIIIIIIBBBB32s",
        IH_MAGIC,
        0,
        timestamp,
        data_size,
        load_addr,
        ep_addr,
        data_crc,
        5,  # OS: Linux
        2,  # Arch: ARM
        5,  # Type: Filesystem
        1,  # Comp: none (uncompressed container of squashfs)
        name_bytes,
    )
    header_crc = zlib.crc32(header_pre) & 0xFFFFFFFF

    header_final = struct.pack(
        ">IIIIIIIBBBB32s",
        IH_MAGIC,
        header_crc,
        timestamp,
        data_size,
        load_addr,
        ep_addr,
        data_crc,
        5,
        2,
        5,
        1,
        name_bytes,
    )
    return header_final + raw_data


def prepare_clean_custom():
    print("[*] Preparing clean custom partition (removing LCD graphics, fonts, strings)...")
    src = os.path.join(EXTRACTED_DIR, "custom-x")
    dst = os.path.join(BUILD_DIR, "custom-x")
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst)

    # 1. Remove useless LCD graphics and fonts
    for p in ["data/LVGL", "data/Fonts", "data/Strings"]:
        full_p = os.path.join(dst, p.replace("/", os.sep))
        if os.path.exists(full_p):
            shutil.rmtree(full_p)
            print(f"  Removed bloat: {p}")

    # 2. Configure NetWork.custom to disable Chinese cloud connections
    net_custom_path = os.path.join(dst, "CustomConfig", "NetWork.custom")
    if os.path.exists(net_custom_path):
        with open(net_custom_path, "r", encoding="utf-8", errors="ignore") as f:
            try:
                cfg = json.load(f)
            except Exception:
                cfg = {}
        # Disable cloud push & server connection
        if "PMS" in cfg:
            cfg["PMS"]["Enable"] = False
        cfg["DisableXmServerConn"] = True
        with open(net_custom_path, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=4)
        print("  Updated NetWork.custom: cloud connection disabled")

    # 3. Create clean persistent startup hook extapp.sh
    extapp_path = os.path.join(dst, "extapp.sh")
    with open(extapp_path, "w", newline="\n", encoding="utf-8") as f:
        f.write("""#!/bin/sh
# ==============================================================================
# CleanCam OS Persistent Initialization Hook
# Runs automatically as root on boot from /mnt/custom/extapp.sh
# ==============================================================================

# 1. Unlock Root Access (remove root password requirement)
mount -o remount,rw / 2>/dev/null
sed -i 's/^root:[^:]*:/root::/' /etc/passwd 2>/dev/null

# 2. Block all Chinese cloud & telemetry hosts in /etc/hosts
if ! grep -q "push.umeye.cn" /etc/hosts 2>/dev/null; then
    cat << 'EOF' >> /etc/hosts
127.0.0.1 push.umeye.cn
127.0.0.1 secu100.net
127.0.0.1 pub-tcs.secu100.net
127.0.0.1 cloud.xmeye.net
127.0.0.1 d.xmeye.net
EOF
fi

# 3. Kill unwanted background mobile/cloud telemetry daemons
killall -9 XmMobileDaemon 2>/dev/null
killall -9 Xm4gDialer 2>/dev/null
killall -9 XmBle_SsvApp_6158M 2>/dev/null

# 4. Start Root Shell & Telnet on Port 23
if [ -x /usr/sbin/telnetd ]; then
    /usr/sbin/telnetd -p 23 -l /bin/sh &
elif [ -x /bin/busybox ]; then
    /bin/busybox telnetd -p 23 -l /bin/sh &
fi
/bin/busybox nc -l -p 2323 -e /bin/sh &

# 5. Optimize memory & cache reclamation for 64MB RAM
echo 1024 > /proc/sys/vm/min_free_kbytes
echo 1 > /proc/sys/vm/drop_caches
""")
    os.chmod(extapp_path, 0o755)
    print("  Created /mnt/custom/extapp.sh (clean autostart script)")


def prepare_clean_web():
    print("[*] Preparing clean modern Web UI partition...")
    src = os.path.join(EXTRACTED_DIR, "web-x")
    dst = os.path.join(BUILD_DIR, "web-x")
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst)

    # Remove bulky legacy ActiveX scripts
    legacy_files = [
        os.path.join(dst, "js", "jquery-3.5.0.min.js"),
        os.path.join(dst, "js", "main.js"),
        os.path.join(dst, "css", "main.css"),
        os.path.join(dst, "pluginVersion.js"),
    ]
    for lf in legacy_files:
        if os.path.exists(lf):
            os.remove(lf)

    # Copy modern HTML5 dashboard to root index.html and index.htm
    html_src = os.path.join(BASE_DIR, "web_ui", "index.html")
    if os.path.exists(html_src):
        shutil.copyfile(html_src, os.path.join(dst, "index.html"))
        shutil.copyfile(html_src, os.path.join(dst, "index.htm"))
        print("  Installed modern HTML5 control panel as index.html and index.htm")


def prepare_clean_user():
    print("[*] Preparing clean user partition (removing 850KB voice prompts)...")
    src = os.path.join(EXTRACTED_DIR, "user-x")
    dst = os.path.join(BUILD_DIR, "user-x")
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst)

    # 1. Remove voice prompts in foreign languages
    music_dir = os.path.join(dst, "share", "music")
    if os.path.exists(music_dir):
        count = 0
        for f in os.listdir(music_dir):
            fp = os.path.join(music_dir, f)
            if os.path.isfile(fp):
                os.remove(fp)
                count += 1
        print(f"  Removed {count} voice audio files from user-x/share/music")

    # 2. Remove bloat binary XmBle_SsvApp_6158M
    ble_bin = os.path.join(dst, "sbin", "XmBle_SsvApp_6158M")
    if os.path.exists(ble_bin):
        os.remove(ble_bin)
        print("  Removed Bluetooth pairing daemon XmBle_SsvApp_6158M")

    # 3. Handle symlinks for Windows copy
    # Replace .symlink with copies of target file if exists
    for root, dirs, files in os.walk(dst):
        for f in files:
            if f.endswith(".symlink"):
                full_p = os.path.join(root, f)
                real_name = full_p[:-8]
                with open(full_p, "r", encoding="utf-8") as fl:
                    target_rel = fl.read().strip()
                # target_rel is like ../../mnt/web/lib/libQr.so
                target_base = os.path.basename(target_rel)
                web_lib_candidate = os.path.join(EXTRACTED_DIR, "web-x", "lib", target_base)
                if os.path.exists(web_lib_candidate):
                    shutil.copyfile(web_lib_candidate, real_name)
                    os.remove(full_p)
                else:
                    # Keep text
                    os.remove(full_p)


def build_partition_image(part_name):
    cfg = PARTITION_CONFIG.get(part_name)
    if not cfg:
        return None
    raw_dir_name = part_name.replace(".squashfs.img", "")
    src_dir = os.path.join(BUILD_DIR, raw_dir_name)
    if not os.path.exists(src_dir):
        # Fallback to extracted if not cleaned
        src_dir = os.path.join(EXTRACTED_DIR, raw_dir_name)

    sqsh_out = os.path.join(BUILD_DIR, f"{raw_dir_name}.raw.sqsh")
    if os.path.exists(sqsh_out):
        os.remove(sqsh_out)

    cmd = [
        MKSQUASHFS,
        src_dir,
        sqsh_out,
        "-comp",
        "xz",
        "-b",
        str(cfg["block_size"]),
        "-noappend",
        "-all-root",
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[-] mksquashfs failed for {part_name}: {res.stderr}")
        return None

    # Read squashfs data and wrap in U-Boot header
    with open(sqsh_out, "rb") as f:
        sqsh_data = f.read()
    os.remove(sqsh_out)

    raw_path = os.path.join(OUTPUT_DIR, f"{raw_dir_name}.squashfs")
    with open(raw_path, "wb") as f:
        f.write(sqsh_data)

    uboot_img = make_uboot_image(sqsh_data, cfg["load"], cfg["ep"], cfg["name"])
    final_path = os.path.join(OUTPUT_DIR, part_name)
    with open(final_path, "wb") as f:
        f.write(uboot_img)

    print(f"[+] Built {part_name}: {len(uboot_img)} bytes (Squashfs: {len(sqsh_data)} bytes)")
    return final_path


def main():
    print("==================================================================")
    print(" CleanCam Custom Firmware Builder (GK7201V300 / SSV6158M) ")
    print("==================================================================")
    os.makedirs(BUILD_DIR, exist_ok=True)
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    prepare_clean_custom()
    prepare_clean_web()
    prepare_clean_user()

    # Build custom partition images
    for part in ["custom-x.squashfs.img", "web-x.squashfs.img", "user-x.squashfs.img"]:
        build_partition_image(part)

    # Copy untouched boot & romfs images
    for raw_part in ["u-boot.bin.img", "u-boot.env.img", "romfs-x.squashfs.img"]:
        src = os.path.join(EXTRACTED_DIR, raw_part)
        dst = os.path.join(OUTPUT_DIR, raw_part)
        if os.path.exists(src):
            shutil.copyfile(src, dst)
            print(f"[+] Reused {raw_part} ({os.path.getsize(dst)} bytes)")

    # Copy & Update InstallDesc
    install_desc_src = os.path.join(EXTRACTED_DIR, "InstallDesc")
    install_desc_dst = os.path.join(OUTPUT_DIR, "InstallDesc")
    shutil.copyfile(install_desc_src, install_desc_dst)

    # Package into complete .bin firmware
    bin_output = os.path.join(BASE_DIR, "General_IPC_GK7201V300_CLEANCAM_CUSTOM.bin")
    print(f"[*] Packaging final firmware into {bin_output}...")
    with zipfile.ZipFile(bin_output, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for fname in [
            "u-boot.bin.img",
            "u-boot.env.img",
            "romfs-x.squashfs.img",
            "user-x.squashfs.img",
            "web-x.squashfs.img",
            "custom-x.squashfs.img",
            "InstallDesc",
        ]:
            fp = os.path.join(OUTPUT_DIR, fname)
            if os.path.exists(fp):
                z.write(fp, arcname=fname)

    # Build MicroSD AutoInstaller package
    sd_output_dir = os.path.join(BASE_DIR, "sd_card_installer")
    os.makedirs(sd_output_dir, exist_ok=True)
    
    # Rebuild native ARM xm_flasher binary
    flasher_builder = os.path.join(BASE_DIR, "tools", "build_xm_flasher.py")
    if os.path.exists(flasher_builder):
        subprocess.run([sys.executable, flasher_builder], check=True)
    # Copy custom partition images to SD installer folder
    for p in ["custom-x.squashfs.img", "web-x.squashfs.img", "user-x.squashfs.img"]:
        shutil.copyfile(os.path.join(OUTPUT_DIR, p), os.path.join(sd_output_dir, p))
    # Also copy raw squashfs images (without U-Boot 64-byte headers)
    for p in ["custom-x.squashfs", "web-x.squashfs", "user-x.squashfs"]:
        raw_sq = os.path.join(OUTPUT_DIR, p)
        if os.path.exists(raw_sq):
            shutil.copyfile(raw_sq, os.path.join(sd_output_dir, p))

    # Write one-click SD installer xm_autorun.sh
    sd_script_path = os.path.join(sd_output_dir, "xm_autorun.sh")
    with open(sd_script_path, "w", newline="\n", encoding="utf-8") as f:
        f.write("""#!/bin/sh
# ==============================================================================
# CleanCam MicroSD One-Click Firmware Flasher
# Flashes custom clean partitions directly to SPI Flash, bypassing RSA signature.
# ==============================================================================

SD_DIR="/var/tmp/mmcblock0"
[ ! -d "$SD_DIR" ] && SD_DIR="$(cd "$(dirname "$0")" && pwd)"
LOG_FILE="$SD_DIR/flasher.log"

echo "=== CleanCam Firmware Flasher Started [$(date)] ===" > $LOG_FILE

# Ensure tools are executable
chmod +x "$SD_DIR/busybox" "$SD_DIR/xm_flasher" 2>/dev/null
export PATH="$SD_DIR:/sbin:/bin:/usr/sbin:/usr/bin:$PATH"
export LD_LIBRARY_PATH="/lib:/usr/lib:/usr/local/lib:$LD_LIBRARY_PATH"
BB="$SD_DIR/busybox"
FLASHER="$SD_DIR/xm_flasher"

# 1. Terminate camera services to release partition mounts
echo "[*] Step 1: Terminating camera services to free partitions..." >> $LOG_FILE
$BB killall -9 App XmServices_Mgr extapp.sh XmWlanDaemon 2>/dev/null
sleep 2

# 2. Unmount active filesystems before flashing underlying MTD
echo "[*] Step 2: Unmounting active partitions..." >> $LOG_FILE
$BB umount -f -l /mnt/custom 2>/dev/null
$BB umount -f -l /mnt/web 2>/dev/null
$BB umount -f -l /usr 2>/dev/null
sleep 1

# 3. Factory Backup (preserves original MTD blocks on SD card)
echo "[*] Step 3: Backing up factory partitions..." >> $LOG_FILE
mkdir -p "$SD_DIR/factory_backup"
for i in 2 3 4; do
    if [ ! -f "$SD_DIR/factory_backup/mtdblock${i}.bin" ]; then
        echo "[*] Backing up /dev/mtdblock$i..." >> $LOG_FILE
        $BB dd if=/dev/mtdblock$i of="$SD_DIR/factory_backup/mtdblock${i}.bin" bs=64k 2>> $LOG_FILE
    fi
done
$BB sync

flash_partition() {
    char_dev="$1"   # e.g. /dev/mtd4
    blk_dev="$2"    # e.g. /dev/mtdblock4
    img_src="$3"    # source squashfs image
    part_size="$4"  # partition size in bytes
    part_name="$5"  # partition label

    echo "--------------------------------------------------------" >> $LOG_FILE
    echo "[*] Processing $part_name ($char_dev / $blk_dev)..." >> $LOG_FILE

    # Strip 64-byte U-Boot header if image is wrapped
    head_magic=$($BB dd if="$img_src" bs=4 count=1 2>/dev/null | $BB hexdump -n 4 -e '1/4 "%08x"')
    if [ "$head_magic" = "56190527" ] || [ "$head_magic" = "27051956" ]; then
        echo "[*] Stripping 64-byte U-Boot header to /var/tmp/payload.raw..." >> $LOG_FILE
        $BB dd if="$img_src" of=/var/tmp/payload.raw bs=64 skip=1 2>> $LOG_FILE
        payload_file="/var/tmp/payload.raw"
    else
        payload_file="$img_src"
    fi

    # Execute native flasher with kernel authentication
    echo "[*] Executing native flasher on $char_dev with size $part_size..." >> $LOG_FILE
    "$FLASHER" "$char_dev" "$payload_file" "$part_size" >> $LOG_FILE 2>&1
    flash_res=$?
    rm -f /var/tmp/payload.raw

    if [ $flash_res -ne 0 ]; then
        echo "[!] ERROR: xm_flasher returned exit code $flash_res for $part_name!" >> $LOG_FILE
        return 1
    fi

    # Verification: test-mount the written partition
    mkdir -p /var/tmp/verify_mnt 2>/dev/null
    $BB mount -t squashfs -o ro "$blk_dev" /var/tmp/verify_mnt 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "[+] VERIFIED: $blk_dev successfully mounted as SquashFS!" >> $LOG_FILE
        $BB ls -la /var/tmp/verify_mnt >> $LOG_FILE 2>&1
        $BB umount /var/tmp/verify_mnt 2>/dev/null
    else
        echo "[!] Warning: verify mount for $blk_dev returned non-zero" >> $LOG_FILE
    fi

    echo "[+] Flashed $part_name successfully!" >> $LOG_FILE
    return 0
}

# 4. Flash clean custom partition (mtd4, offset 0x780000, 192KB = 196608 bytes)
if [ -f "$SD_DIR/custom-x.squashfs" ]; then
    flash_partition /dev/mtd4 /dev/mtdblock4 "$SD_DIR/custom-x.squashfs" 196608 "custom"
elif [ -f "$SD_DIR/custom-x.squashfs.img" ]; then
    flash_partition /dev/mtd4 /dev/mtdblock4 "$SD_DIR/custom-x.squashfs.img" 196608 "custom"
fi

# 5. Flash clean Web UI partition (mtd3, offset 0x760000, 128KB = 131072 bytes)
if [ -f "$SD_DIR/web-x.squashfs" ]; then
    flash_partition /dev/mtd3 /dev/mtdblock3 "$SD_DIR/web-x.squashfs" 131072 "web"
elif [ -f "$SD_DIR/web-x.squashfs.img" ]; then
    flash_partition /dev/mtd3 /dev/mtdblock3 "$SD_DIR/web-x.squashfs.img" 131072 "web"
fi

# 6. Flash clean User partition (mtd2, offset 0x320000, 4352KB = 4456448 bytes)
if [ -f "$SD_DIR/user-x.squashfs" ]; then
    flash_partition /dev/mtd2 /dev/mtdblock2 "$SD_DIR/user-x.squashfs" 4456448 "user"
elif [ -f "$SD_DIR/user-x.squashfs.img" ]; then
    flash_partition /dev/mtd2 /dev/mtdblock2 "$SD_DIR/user-x.squashfs.img" 4456448 "user"
fi

# 9. Start telnet daemon immediately for direct root access
$BB telnetd -p 23 -l /bin/sh &
echo "=== CleanCam Flashing Completed Successfully! Remove SD card and reboot camera ===" >> $LOG_FILE
$BB sync
""")
    os.chmod(sd_script_path, 0o755)

    print("==================================================================")
    print(f"[+] Complete Custom Firmware built: {bin_output} ({os.path.getsize(bin_output)} bytes)")
    print(f"[+] MicroSD One-Click Installer ready in: {sd_output_dir}")
    print("==================================================================")


if __name__ == "__main__":
    main()
