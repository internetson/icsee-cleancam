import os, sys, subprocess, shutil, struct, zlib, time

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_DIR = os.path.join(BASE_DIR, "build_clean")

mksquashfs_env = shutil.which("mksquashfs")
if mksquashfs_env:
    MKSQUASHFS = mksquashfs_env
elif os.path.exists(os.path.join(BASE_DIR, "scripts", "squashfs-tools", "mksquashfs.exe")):
    MKSQUASHFS = os.path.join(BASE_DIR, "scripts", "squashfs-tools", "mksquashfs.exe")
elif os.path.exists(os.path.join(BASE_DIR, "tools", "squashfs-tools", "mksquashfs.exe")):
    MKSQUASHFS = os.path.join(BASE_DIR, "tools", "squashfs-tools", "mksquashfs.exe")
else:
    MKSQUASHFS = "mksquashfs"

unsquashfs_env = shutil.which("unsquashfs")
if unsquashfs_env:
    UNSQUASHFS = unsquashfs_env
elif os.path.exists(os.path.join(BASE_DIR, "scripts", "squashfs-tools", "unsquashfs.exe")):
    UNSQUASHFS = os.path.join(BASE_DIR, "scripts", "squashfs-tools", "unsquashfs.exe")
elif os.path.exists(os.path.join(BASE_DIR, "tools", "squashfs-tools", "unsquashfs.exe")):
    UNSQUASHFS = os.path.join(BASE_DIR, "tools", "squashfs-tools", "unsquashfs.exe")
else:
    UNSQUASHFS = "unsquashfs"

IH_MAGIC = 0x27051956
IH_NMLEN = 32

def make_uboot_image(raw_data, load_addr, ep_addr, name):
    data_size = len(raw_data)
    data_crc = zlib.crc32(raw_data) & 0xFFFFFFFF
    timestamp = int(time.time())
    name_bytes = name.encode("latin1")[: IH_NMLEN - 1].ljust(IH_NMLEN, b"\x00")

    header_pre = struct.pack(
        ">IIIIIIIBBBB32s",
        IH_MAGIC, 0, timestamp, data_size,
        load_addr, ep_addr, data_crc,
        5, 2, 5, 1, name_bytes
    )
    header_crc = zlib.crc32(header_pre) & 0xFFFFFFFF
    header_final = struct.pack(
        ">IIIIIIIBBBB32s",
        IH_MAGIC, header_crc, timestamp, data_size,
        load_addr, ep_addr, data_crc,
        5, 2, 5, 1, name_bytes
    )
    return header_final + raw_data

def build_custom():
    print("[*] Building custom-x.squashfs...")
    src_dir = os.path.join(BUILD_DIR, "custom-x")
    out_sqsh = os.path.join(BUILD_DIR, "custom-x.squashfs")
    out_img = os.path.join(BUILD_DIR, "custom-x.squashfs.img")
    pf_file = os.path.join(BUILD_DIR, "custom_pf.txt")

    with open(pf_file, "w") as f:
        f.write("extapp.sh m 755 0 0\n")
        f.write("wifi_manager.sh m 755 0 0\n")
        f.write("default.script m 755 0 0\n")
        f.write("xm_httpd m 755 0 0\n")
        f.write("xm_telnetd m 755 0 0\n")
        f.write("xm_ptz m 755 0 0\n")
        f.write("xm_cmd m 755 0 0\n")
        f.write("xm_rtspd m 755 0 0\n")
        f.write("xm_setcfg m 755 0 0\n")
        f.write("xm_snap m 755 0 0\n")
        f.write("xm_resetd m 755 0 0\n")

    if os.path.exists(out_sqsh): os.remove(out_sqsh)
    cmd = [
        MKSQUASHFS, src_dir, out_sqsh,
        "-comp", "xz",
        "-b", "524288",
        "-noappend",
        "-all-root",
        "-pf", pf_file
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print("[-] mksquashfs custom failed:", res.stderr)
        return False
    if os.path.exists(pf_file): os.remove(pf_file)

    sz = os.path.getsize(out_sqsh)
    print(f"[+] custom-x.squashfs built: {sz} bytes (max 196608 bytes)")
    if sz > 196608:
        print("[!] ERROR: custom-x exceeds mtd4 size!")
        return False

    with open(out_sqsh, "rb") as f:
        data = f.read()
    uimg = make_uboot_image(data, 0x00780000, 0x007b0000, "custom")
    with open(out_img, "wb") as f:
        f.write(uimg)
    return True

def build_web():
    print("[*] Building web-x.squashfs...")
    src_dir = os.path.join(BUILD_DIR, "web-x")
    out_sqsh = os.path.join(BUILD_DIR, "web-x.squashfs")
    out_img = os.path.join(BUILD_DIR, "web-x.squashfs.img")
    pf_file = os.path.join(BUILD_DIR, "web_pf.txt")

    with open(pf_file, "w") as f:
        f.write("iwlist m 755 0 0\n")
        f.write("cgi-bin/camera_ctrl m 755 0 0\n")
        f.write("cgi-bin/ptz m 755 0 0\n")
        f.write("cgi-bin/status m 755 0 0\n")
        f.write("cgi-bin/wifi_connect m 755 0 0\n")
        f.write("cgi-bin/wifi_scan m 755 0 0\n")
        f.write("cgi-bin/snapshot m 755 0 0\n")
        f.write("cgi-bin/auth m 755 0 0\n")
        f.write("cgi-bin/light m 755 0 0\n")

    if os.path.exists(out_sqsh): os.remove(out_sqsh)
    cmd = [
        MKSQUASHFS, src_dir, out_sqsh,
        "-comp", "xz",
        "-b", "524288",
        "-noappend",
        "-all-root",
        "-pf", pf_file
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print("[-] mksquashfs web failed:", res.stderr)
        return False
    if os.path.exists(pf_file): os.remove(pf_file)

    sz = os.path.getsize(out_sqsh)
    print(f"[+] web-x.squashfs built: {sz} bytes (max 131072 bytes)")
    if sz > 131072:
        print("[!] ERROR: web-x exceeds mtd3 size!")
        return False

    with open(out_sqsh, "rb") as f:
        data = f.read()
    uimg = make_uboot_image(data, 0x00760000, 0x00780000, "web")
    with open(out_img, "wb") as f:
        f.write(uimg)
    return True

if __name__ == "__main__":
    if build_custom() and build_web():
        print("[+] Both custom-x and web-x images built successfully!")
        sd_inst = os.path.join(BASE_DIR, "sd_card_installer")
        shutil.copy(os.path.join(BUILD_DIR, "custom-x.squashfs"), os.path.join(sd_inst, "custom-x.squashfs"))
        shutil.copy(os.path.join(BUILD_DIR, "web-x.squashfs"), os.path.join(sd_inst, "web-x.squashfs"))
        print("[+] Synced squashfs images to sd_card_installer/")
        print("\nVerifying permissions of custom-x:")
        subprocess.run([UNSQUASHFS, "-ll", os.path.join(BUILD_DIR, "custom-x.squashfs")])
        print("\nVerifying permissions of web-x:")
        subprocess.run([UNSQUASHFS, "-ll", os.path.join(BUILD_DIR, "web-x.squashfs")])
