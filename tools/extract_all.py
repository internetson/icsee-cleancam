import os
import zipfile
import io
import shutil
from PySquashfsImage import SquashFsImage

firmware_path = r'firmware\000949U5.1IPC_GK7201V300_G2-WR-V_S38_WIFI.6158M.Nat_V5.00.R02\General_IPC_GK7201V300_G2-WR-V_S38_WIFI.6158M.Nat_V5.04.C01.20260106_all.bin'
output_dir = 'extracted'

os.makedirs(output_dir, exist_ok=True)

with zipfile.ZipFile(firmware_path, 'r') as z:
    for name in z.namelist():
        print(f"Processing {name}...")
        raw = z.read(name)
        part_name = name.replace('.squashfs.img', '').replace('.bin.img', '').replace('.env.img', '')
        part_dir = os.path.join(output_dir, part_name)
        
        # Save raw binary
        raw_path = os.path.join(output_dir, name)
        with open(raw_path, 'wb') as f:
            f.write(raw)
            
        if name.endswith('.squashfs.img'):
            os.makedirs(part_dir, exist_ok=True)
            sqsh_data = raw[64:]
            try:
                img = SquashFsImage(io.BytesIO(sqsh_data))
                for item in img.root.riter():
                    rel_path = item.path.lstrip('/')
                    if not rel_path:
                        continue
                    full_path = os.path.join(part_dir, rel_path.replace('/', os.sep))
                    if item.is_dir:
                        os.makedirs(full_path, exist_ok=True)
                    elif item.is_file:
                        os.makedirs(os.path.dirname(full_path), exist_ok=True)
                        try:
                            content = item.read_bytes()
                            with open(full_path, 'wb') as f_out:
                                f_out.write(content)
                        except Exception as e:
                            print(f"  Error reading {item.path}: {e}")
                    elif item.is_symlink:
                        os.makedirs(os.path.dirname(full_path), exist_ok=True)
                        target = item.readlink()
                        with open(full_path + '.symlink', 'w', encoding='utf-8') as f_out:
                            f_out.write(str(target))
                print(f"  Successfully extracted {name} into {part_dir}")
            except Exception as e:
                print(f"Error unpacking {name}: {e}")

print("Extraction completed!")
