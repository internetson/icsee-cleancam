#!/bin/sh
# ==============================================================================
# CleanCam OS Auto-Deploy & Diagnostic Manager
# ==============================================================================

SD_DIR="/var/tmp/mmcblock0"
[ ! -d "$SD_DIR" ] && SD_DIR="$(cd "$(dirname "$0")" && pwd)"
LOG_FILE="$SD_DIR/wifi_setup.log"
BB="$SD_DIR/busybox"
[ ! -x "$BB" ] && BB="/bin/busybox"

echo "" >> $LOG_FILE
echo "==================================================" >> $LOG_FILE
echo "=== CleanCam Boot [$(date)] ===" >> $LOG_FILE
echo "==================================================" >> $LOG_FILE

# 1. ALWAYS start Root Telnet immediately so the device is reachable
if [ -x /usr/sbin/telnetd ]; then
    /usr/sbin/telnetd -p 23 -l /bin/sh &
elif [ -x "$BB" ]; then
    "$BB" telnetd -p 23 -l /bin/sh &
elif [ -x /bin/busybox ]; then
    /bin/busybox telnetd -p 23 -l /bin/sh &
fi

# 2. Check if clean v3 flash is required
NEED_FLASH=0
if [ ! -f "$SD_DIR/.clean_v3_done" ]; then
    NEED_FLASH=1
    echo "[!] Partition update required (.clean_v3_done not found)." >> $LOG_FILE
fi

if [ "$NEED_FLASH" = "1" ]; then
    echo "[*] Terminating camera services to free partitions..." >> $LOG_FILE
    killall -9 App XmServices_Mgr XmWlanDaemon wifi_manager.sh httpd xm_httpd xm_telnetd xm_resetd 2>/dev/null
    sleep 1
    
    umount -l /mnt/web 2>/dev/null
    umount -l /mnt/custom 2>/dev/null
    sleep 1
    
    chmod +x "$SD_DIR/xm_flasher" 2>/dev/null
    
    FLASH_OK=1
    # Flash custom (mtd4, 192KB = 196608 bytes)
    if [ -f "$SD_DIR/custom-x.squashfs" ] && [ -x "$SD_DIR/xm_flasher" ]; then
        echo "[*] Flashing /dev/mtd4 (custom, 196608 bytes)..." >> $LOG_FILE
        "$SD_DIR/xm_flasher" /dev/mtd4 "$SD_DIR/custom-x.squashfs" 196608 >> $LOG_FILE 2>&1
        RET=$?
        echo "custom flash exit code: $RET" >> $LOG_FILE
        [ $RET -ne 0 ] && FLASH_OK=0
    fi
    
    # Flash web (mtd3, 128KB = 131072 bytes)
    if [ -f "$SD_DIR/web-x.squashfs" ] && [ -x "$SD_DIR/xm_flasher" ]; then
        echo "[*] Flashing /dev/mtd3 (web, 131072 bytes)..." >> $LOG_FILE
        "$SD_DIR/xm_flasher" /dev/mtd3 "$SD_DIR/web-x.squashfs" 131072 >> $LOG_FILE 2>&1
        RET=$?
        echo "web flash exit code: $RET" >> $LOG_FILE
        [ $RET -ne 0 ] && FLASH_OK=0
    fi
    
    if [ "$FLASH_OK" = "1" ]; then
        touch "$SD_DIR/.clean_v3_done"
        $BB sync
        echo "[+] Flashing complete! Rebooting cleanly into new flash firmware..." >> $LOG_FILE
        sleep 1
        reboot -f
        exit 0
    else
        echo "[-] Flash failed, remaining alive with telnetd..." >> $LOG_FILE
    fi
fi

# Partitions are already flashed. Ensure services are running.
if [ -x /mnt/custom/extapp.sh ]; then
    echo "[*] Launching /mnt/custom/extapp.sh..." >> $LOG_FILE
    /mnt/custom/extapp.sh &
elif [ -e /dev/mtdblock4 ]; then
    mount /dev/mtdblock4 /mnt/custom 2>/dev/null
    mount /dev/mtdblock3 /mnt/web 2>/dev/null
    if [ -x /mnt/custom/extapp.sh ]; then
        /mnt/custom/extapp.sh &
    fi
fi

echo "--- SYSTEM STATUS ---" >> $LOG_FILE
echo "Network interfaces:" >> $LOG_FILE
ifconfig -a >> $LOG_FILE 2>&1
echo "Mounted partitions:" >> $LOG_FILE
mount | grep mtd >> $LOG_FILE 2>&1
echo "Processes:" >> $LOG_FILE
ps >> $LOG_FILE 2>&1
echo "----------------------" >> $LOG_FILE
echo "=== CleanCam Active ===" >> $LOG_FILE
