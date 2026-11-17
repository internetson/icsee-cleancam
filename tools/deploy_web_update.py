import sys, time
sys.path.insert(0, 'tools')
from fast_tar_upload import upload_tar
from remote_exec import run_cmd

print("[*] Uploading updated auth and index.html to live camera...")
upload_tar({
    'auth': 'build_clean/web-x/cgi-bin/auth',
    'index.html': 'build_clean/web-x/index.html',
    'web-x.squashfs': 'build_clean/web-x.squashfs'
})

print("[*] Applying live bind-mounts...")
cmds = """
chmod +x /var/tmp/auth
umount /mnt/web/cgi-bin/auth 2>/dev/null
mount -o bind /var/tmp/auth /mnt/web/cgi-bin/auth

umount /mnt/web/index.html 2>/dev/null
mount -o bind /var/tmp/index.html /mnt/web/index.html

# Flash persistent partition mtd3 (web, 131072 bytes)
if [ -x /var/tmp/xm_flasher ] && [ -f /var/tmp/web-x.squashfs ]; then
    echo "Flashing mtd3..."
    /var/tmp/xm_flasher /dev/mtd3 /var/tmp/web-x.squashfs 131072
fi
sync
"""
print(run_cmd(cmds))
print("[+] Applied!")
