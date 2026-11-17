#!/usr/bin/env python3
"""
Xiongmai / ICSee Firmware Repacker & Builder
Wraps partition images with U-Boot 64-byte headers, updates InstallDesc,
calculates CRC32, and packages into update .bin file.
"""

import os
import sys
import struct
import zlib
import zipfile
import json
import argparse
import time

# U-Boot image constants
IH_MAGIC = 0x27051956
IH_NMLEN = 32

PARTITION_OFFSETS = {
    "u-boot.bin.img": {"load": 0x00000000, "ep": 0x00030000, "name": "uboot", "os": 5, "arch": 2, "type": 5, "comp": 1},
    "u-boot.env.img": {"load": 0x00030000, "ep": 0x00040000, "name": "env", "os": 5, "arch": 2, "type": 5, "comp": 1},
    "romfs-x.squashfs.img": {"load": 0x00040000, "ep": 0x00320000, "name": "romfs", "os": 5, "arch": 2, "type": 5, "comp": 1},
    "user-x.squashfs.img": {"load": 0x00320000, "ep": 0x00760000, "name": "usr", "os": 5, "arch": 2, "type": 5, "comp": 1},
    "web-x.squashfs.img": {"load": 0x00760000, "ep": 0x00780000, "name": "web", "os": 5, "arch": 2, "type": 5, "comp": 1},
    "custom-x.squashfs.img": {"load": 0x00780000, "ep": 0x007b0000, "name": "custom", "os": 5, "arch": 2, "type": 5, "comp": 1},
}

def make_uboot_image(raw_data, load_addr, ep_addr, name, os_type=5, arch=2, image_type=5, comp=1):
    """
    Creates a standard U-Boot 64-byte legacy image header + payload.
    """
    data_size = len(raw_data)
    data_crc = zlib.crc32(raw_data) & 0xffffffff
    timestamp = int(time.time())
    name_bytes = name.encode('latin1')[:IH_NMLEN-1].ljust(IH_NMLEN, b'\x00')

    # Header without header CRC
    header_pre = struct.pack(
        '>IIIIIIIBBBB32s',
        IH_MAGIC,
        0, # placeholder for header CRC
        timestamp,
        data_size,
        load_addr,
        ep_addr,
        data_crc,
        os_type,
        arch,
        image_type,
        comp,
        name_bytes
    )

    header_crc = zlib.crc32(header_pre) & 0xffffffff

    header_final = struct.pack(
        '>IIIIIIIBBBB32s',
        IH_MAGIC,
        header_crc,
        timestamp,
        data_size,
        load_addr,
        ep_addr,
        data_crc,
        os_type,
        arch,
        image_type,
        comp,
        name_bytes
    )

    return header_final + raw_data

def pack_firmware_zip(source_dir, output_bin_path):
    """
    Packs files from source_dir into an XM update .bin zip package.
    """
    install_desc_path = os.path.join(source_dir, "InstallDesc")
    if not os.path.exists(install_desc_path):
        print(f"[-] Error: InstallDesc not found in {source_dir}")
        return False

    with open(install_desc_path, 'r', encoding='utf-8') as f:
        desc = json.load(f)

    # Recalculate CRC for files
    print(f"[*] Repacking firmware from {source_dir}...")
    with zipfile.ZipFile(output_bin_path, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        for cmd in desc.get("UpgradeCommand", []):
            fname = cmd.get("FileName")
            fpath = os.path.join(source_dir, fname)
            if os.path.exists(fpath):
                print(f"  Adding {fname} ({os.path.getsize(fpath)} bytes)...")
                z.write(fpath, arcname=fname)
            else:
                print(f"  [!] Warning: {fname} listed in InstallDesc but not found on disk!")

        # Write InstallDesc
        z.write(install_desc_path, arcname="InstallDesc")

    print(f"[+] Successfully built: {output_bin_path} ({os.path.getsize(output_bin_path)} bytes)")
    return True

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Xiongmai / ICSee Firmware Repacker")
    parser.add_argument("--source", default="extracted", help="Directory containing extracted files and InstallDesc")
    parser.add_argument("--output", default="firmware_custom.bin", help="Output .bin file path")
    args = parser.parse_args()

    pack_firmware_zip(args.source, args.output)
