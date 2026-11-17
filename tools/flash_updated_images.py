import sys, time
sys.path.insert(0, 'tools')
from fast_tar_upload import upload_tar
from remote_exec import run_cmd

print("[*] Uploading xm_flasher and images to camera...")
upload_tar({
    'xm_flasher': 'sd_card_installer/xm_flasher',
    'custom-x.squashfs': 'build_clean/custom-x.squashfs',
    'web-x.squashfs': 'build_clean/web-x.squashfs'
})

print("[*] Flashing custom-x to /dev/mtd4...")
res1 = run_cmd('chmod +x /var/tmp/xm_flasher; /var/tmp/xm_flasher /dev/mtd4 /var/tmp/custom-x.squashfs 196608')
print("mtd4 flash result:\n", res1)

print("[*] Flashing web-x to /dev/mtd3...")
res2 = run_cmd('/var/tmp/xm_flasher /dev/mtd3 /var/tmp/web-x.squashfs 131072')
print("mtd3 flash result:\n", res2)

print("[*] Syncing filesystem...")
run_cmd('sync')
print("[+] Flash complete!")
