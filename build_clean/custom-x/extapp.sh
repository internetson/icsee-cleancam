#!/bin/sh
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

# 3. Kill unwanted background cloud & conflicting wireless daemons
killall -9 XmWlanDaemon XmWifiScan XmMobileDaemon Xm4gDialer XmBle_SsvApp_6158M 2>/dev/null

# 4. Prepare ramfs & ensure clean accounts for DVRIP
mkdir -p /var/tmp /var/run/hostapd /var/lib/misc /mnt/mtd/Config /mnt/mtd/Log /var/jpeg 2>/dev/null
touch /var/lib/misc/udhcpd.leases /var/tmp/udhcpd.leases 2>/dev/null

# Ensure default admin account exists for xm_rtspd (empty password)
if [ ! -f /mnt/mtd/Config/Account1 ] || ! grep -q '"Memo" : "admin account"' /mnt/mtd/Config/Account1 2>/dev/null || grep -q '"Token"' /mnt/mtd/Config/Account1 2>/dev/null; then
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
    sync
fi

# Setup clean DHCP handler script
if [ -f /mnt/custom/default.script ]; then
    cp /mnt/custom/default.script /var/tmp/udhcpc.script 2>/dev/null
    chmod 755 /var/tmp/udhcpc.script 2>/dev/null
    mount -o bind /var/tmp/udhcpc.script /usr/sbin/udhcpc.script 2>/dev/null
fi

# Copy micro-binaries to ramfs to guarantee execution regardless of mount flags
for b in xm_httpd xm_telnetd xm_ptz xm_cmd xm_rtspd xm_setcfg xm_snap xm_resetd xm_ha_osd wifi_manager.sh; do
    if [ -f "/mnt/custom/$b" ]; then
        cp "/mnt/custom/$b" "/var/tmp/$b" 2>/dev/null
        chmod 755 "/var/tmp/$b" 2>/dev/null
    fi
done

# 5. Start Root Shell & Telnet on Port 23
killall -9 telnetd xm_telnetd 2>/dev/null
if [ -x /var/tmp/xm_telnetd ]; then
    /var/tmp/xm_telnetd &
elif [ -x /mnt/custom/xm_telnetd ]; then
    /mnt/custom/xm_telnetd &
fi

# 6. Start CleanCam Wi-Fi Client & Fallback AP Manager
killall -9 wifi_manager.sh 2>/dev/null
if [ -x /var/tmp/wifi_manager.sh ]; then
    /var/tmp/wifi_manager.sh &
elif [ -x /mnt/custom/wifi_manager.sh ]; then
    /mnt/custom/wifi_manager.sh &
fi

# 7. Start Web Server on BOTH Port 80 and Port 8080
killall -9 httpd xm_httpd 2>/dev/null
if [ -x /var/tmp/xm_httpd ]; then
    /var/tmp/xm_httpd 80 &
    /var/tmp/xm_httpd 8080 &
elif [ -x /mnt/custom/xm_httpd ]; then
    /mnt/custom/xm_httpd 80 &
    /mnt/custom/xm_httpd 8080 &
fi

# 8. Start Native RTSP & ONVIF Stream Server with Auto-Restart Watchdog (Port 554 & Port 8899)
killall -9 xm_rtspd 2>/dev/null
(
    while true; do
        if [ -x /var/tmp/xm_rtspd ]; then
            /var/tmp/xm_rtspd >/var/tmp/rtspd.log 2>&1
        elif [ -x /mnt/custom/xm_rtspd ]; then
            /mnt/custom/xm_rtspd >/var/tmp/rtspd.log 2>&1
        fi
        sleep 2
    done
) &

# 9. Start Hardware Reset Button Monitor (GPIO 0, 10s hold = factory reset)
killall -9 xm_resetd 2>/dev/null
if [ -x /var/tmp/xm_resetd ]; then
    /var/tmp/xm_resetd &
elif [ -x /mnt/custom/xm_resetd ]; then
    /mnt/custom/xm_resetd &
fi

# 10. Start Home Assistant OSD Daemon (Hardware OSD overlays)
killall -9 xm_ha_osd 2>/dev/null
(
    while true; do
        if [ -x /var/tmp/xm_ha_osd ]; then
            /var/tmp/xm_ha_osd >/var/tmp/ha_osd.log 2>&1
        elif [ -x /mnt/custom/xm_ha_osd ]; then
            /mnt/custom/xm_ha_osd >/var/tmp/ha_osd.log 2>&1
        fi
        sleep 5
    done
) &

# 11. Optimize memory & cache reclamation for 64MB RAM
echo 1024 > /proc/sys/vm/min_free_kbytes
echo 1 > /proc/sys/vm/drop_caches
