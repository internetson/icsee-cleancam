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

# Helper functions for Wi-Fi auto-provisioning
clean_val() {
    echo "$1" | sed -e 's/^[ \t]*//' -e 's/[ \t]*$//' -e 's/^"//' -e 's/"$//' -e "s/^'//" -e "s/'$//"
}

parse_wifi_txt() {
    _file="$1"
    [ ! -f "$_file" ] && return 1

    W_SSID=""
    W_PASS=""

    # 1. Try key-value format (ssid=..., password=... / pass=... / psk=...)
    _raw_ssid=$(sed -e 's/\r//g' "$_file" | grep -i '^[[:space:]]*ssid[[:space:]]*=' | head -n1 | sed -e 's/^[[:space:]]*[^=]*=[[:space:]]*//')
    _raw_pass=$(sed -e 's/\r//g' "$_file" | grep -i -E '^[[:space:]]*(password|pass|psk)[[:space:]]*=' | head -n1 | sed -e 's/^[[:space:]]*[^=]*=[[:space:]]*//')

    if [ -n "$_raw_ssid" ]; then
        W_SSID=$(clean_val "$_raw_ssid")
        W_PASS=$(clean_val "$_raw_pass")
    else
        # 2. Try 2-line format (line 1: SSID, line 2: password)
        _lines=$(sed -e 's/\r//g' "$_file" | grep -v '^[[:space:]]*#' | grep -v '^[[:space:]]*$')
        _line1=$(echo "$_lines" | sed -n '1p')
        _line2=$(echo "$_lines" | sed -n '2p')
        W_SSID=$(clean_val "$_line1")
        W_PASS=$(clean_val "$_line2")
    fi

    [ -n "$W_SSID" ]
}

provision_wifi() {
    _sd_dir="$1"
    _target_conf="/mnt/mtd/Config/wpa.conf"

    _wifi_src=""
    for _candidate in "$_sd_dir/wifi.txt" "$_sd_dir/WIFI.TXT" "$_sd_dir/Wifi.txt"; do
        if [ -f "$_candidate" ]; then
            _wifi_src="$_candidate"
            break
        fi
    done

    [ -z "$_wifi_src" ] && return 1

    echo "[*] Found $_wifi_src on SD card, parsing..." >> $LOG_FILE
    if parse_wifi_txt "$_wifi_src"; then
        echo "[+] Wi-Fi credentials parsed: SSID='$W_SSID'" >> $LOG_FILE
        [ ! -d /mnt/mtd/Config ] && mount /dev/mtdblock5 /mnt/mtd 2>/dev/null
        mkdir -p /mnt/mtd/Config /mnt/mtd/Log 2>/dev/null

        if [ -n "$W_PASS" ]; then
            cat << EOF > "$_target_conf"
ctrl_interface=/var/run/wpa_supplicant
ctrl_interface_group=0
ap_scan=1

network={
	ssid="$W_SSID"
	key_mgmt=WPA-PSK
	pairwise=CCMP TKIP
	group=CCMP TKIP
	psk="$W_PASS"
}
EOF
        else
            cat << EOF > "$_target_conf"
ctrl_interface=/var/run/wpa_supplicant
ctrl_interface_group=0
ap_scan=1

network={
	ssid="$W_SSID"
	key_mgmt=NONE
}
EOF
        fi
        echo "ENABLE = 1" > /mnt/mtd/Config/WLan 2>/dev/null
        echo "StartupType:0" > /mnt/mtd/Log/WifiStartupType 2>/dev/null
        cp -f "$_wifi_src" /mnt/mtd/Config/.wifi_txt_last 2>/dev/null
        $BB sync 2>/dev/null || sync
        echo "[+] Saved Wi-Fi configuration to $_target_conf" >> $LOG_FILE
        return 0
    else
        echo "[-] Could not parse valid SSID from $_wifi_src" >> $LOG_FILE
        return 1
    fi
}

restore_default_accounts() {
    [ ! -d /mnt/mtd/Config ] && mount /dev/mtdblock5 /mnt/mtd 2>/dev/null
    mkdir -p /mnt/mtd/Config 2>/dev/null

    _need_restore=0
    if [ "$1" = "force" ] || [ ! -f /mnt/mtd/Config/Account1 ] || [ ! -f /mnt/mtd/Config/Account2 ]; then
        _need_restore=1
    elif ! grep -q '"Memo" : "admin account"' /mnt/mtd/Config/Account1 2>/dev/null; then
        _need_restore=1
    elif grep -q '"Token"' /mnt/mtd/Config/Account1 2>/dev/null; then
        _need_restore=1
    fi

    if [ "$_need_restore" = "1" ]; then
        echo "[*] Restoring clean default DVRIP accounts for xm_rtspd..." >> $LOG_FILE
        cat << 'EOF' > /mnt/mtd/Config/Account1
{
   "Groups" : [
      {
         "AuthorityList" : [
            "ShutDown", "ChannelTitle", "DefaultConfig", "Talk_01", "IPCCamera",
            "ImExport", "Monitor_01", "EncodeConfig", "Account", "SysInfo",
            "QueryLog", "DelLog", "SysUpgrade", "AutoMaintain", "GeneralConfig",
            "NetConfig", "AlarmConfig", "VideoConfig", "RecordConfig", "StorageManager",
            "Replay_01", "CommConfig", "PtzConfig", "PTZControl", "ElecPTZControl"
         ],
         "Memo" : "administrator group",
         "Name" : "admin"
      },
      {
         "AuthorityList" : [ "Monitor_01", "Replay_01" ],
         "Memo" : "user group",
         "Name" : "user"
      }
   ],
   "Users" : [
      {
         "AuthorityList" : [
            "ShutDown", "ChannelTitle", "DefaultConfig", "Talk_01", "IPCCamera",
            "ImExport", "Monitor_01", "EncodeConfig", "Account", "SysInfo",
            "QueryLog", "DelLog", "SysUpgrade", "AutoMaintain", "GeneralConfig",
            "NetConfig", "AlarmConfig", "VideoConfig", "RecordConfig", "StorageManager",
            "Replay_01", "CommConfig", "PtzConfig", "PTZControl", "ElecPTZControl"
         ],
         "BootTimes" : 100,
         "Group" : "admin",
         "Memo" : "admin account",
         "Name" : "admin",
         "Password" : "",
         "Reserved" : true,
         "Sharable" : true
      },
      {
         "AuthorityList" : [
            "ShutDown", "ChannelTitle", "DefaultConfig", "Talk_01", "IPCCamera",
            "ImExport", "Monitor_01", "EncodeConfig", "Account", "SysInfo",
            "QueryLog", "DelLog", "SysUpgrade", "AutoMaintain", "GeneralConfig",
            "NetConfig", "AlarmConfig", "VideoConfig", "RecordConfig", "StorageManager",
            "Replay_01", "CommConfig", "PtzConfig", "PTZControl", "ElecPTZControl"
         ],
         "BootTimes" : 100,
         "Group" : "admin",
         "Memo" : "camera account",
         "Name" : "camera",
         "Password" : "",
         "Reserved" : false,
         "Sharable" : true
      }
   ]
}
EOF
        cp -f /mnt/mtd/Config/Account1 /mnt/mtd/Config/Account2 2>/dev/null
        $BB sync 2>/dev/null || sync
    fi
}

# 2. Check if clean firmware flash is required
# Marker is stored in /mnt/mtd/.cleancam_done (on the CAMERA internal flash)
# so the same SD card can be used to flash multiple cameras sequentially!
[ ! -d /mnt/mtd/Config ] && mount /dev/mtdblock5 /mnt/mtd 2>/dev/null
NEED_FLASH=0
if [ ! -f "/mnt/mtd/.cleancam_done" ] || [ -f "$SD_DIR/force_flash" ]; then
    NEED_FLASH=1
    echo "[!] Partition update required for this camera (/mnt/mtd/.cleancam_done not found)." >> $LOG_FILE
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
        provision_wifi "$SD_DIR"
        restore_default_accounts force
        touch "/mnt/mtd/.cleancam_done"
        rm -f "$SD_DIR/.clean_v3_done" "$SD_DIR/force_flash" 2>/dev/null
        $BB sync 2>/dev/null || sync
        echo "[+] Flashing complete! Rebooting cleanly into new flash firmware..." >> $LOG_FILE
        sleep 1
        reboot -f
        exit 0
    else
        echo "[-] Flash failed, remaining alive with telnetd..." >> $LOG_FILE
    fi
fi

# Partitions are already flashed. Check if Wi-Fi or accounts need provisioning.
provision_wifi "$SD_DIR"
restore_default_accounts

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
