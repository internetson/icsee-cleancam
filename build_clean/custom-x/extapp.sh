#!/bin/sh
# ==============================================================================
# CleanCam OS Persistent Initialization Hook
# Runs automatically as root on boot from /mnt/custom/extapp.sh
# ==============================================================================

# Set active device hostname: CleanCam-<MAC_SUFFIX>
CAM_HOST="CleanCam"
for _if in eth2 wlan0 ra0 eth0; do
    if [ -f "/sys/class/net/$_if/address" ]; then
        _raw=$(cat "/sys/class/net/$_if/address" 2>/dev/null | sed 's/://g')
        _sub=$(echo "$_raw" | sed 's/.*\(....\)$/\1/' | sed 'y/abcdef/ABCDEF/')
        if [ -n "$_sub" ] && [ ${#_sub} -eq 4 ]; then
            CAM_HOST="CleanCam-$_sub"
            break
        fi
    fi
done
echo "$CAM_HOST" > /proc/sys/kernel/hostname 2>/dev/null

# 1. Prepare writable /etc via bind mount & unlock root
mkdir -p /var/tmp/etc 2>/dev/null
cp -a /etc/* /var/tmp/etc/ 2>/dev/null
mount -o bind /var/tmp/etc /etc 2>/dev/null
sed -i 's/^root:[^:]*:/root::/' /etc/passwd 2>/dev/null

# Setup dynamic linker path so iwconfig and other tools always find /mnt/web/lib
echo "/lib:/usr/lib:/mnt/web/lib" > /etc/ld-musl-arm.path 2>/dev/null

# 2. Block ALL Chinese cloud, telemetry & P2P domains in /etc/hosts
cat << 'EOF' > /etc/hosts
127.0.0.1 localhost
127.0.0.1 secu100.net
127.0.0.1 upgrade.secu100.net
127.0.0.1 test-upgrade.secu100.net
127.0.0.1 pub-tcs.secu100.net
127.0.0.1 pub-cfg.secu100.net
127.0.0.1 pub-pms-pic.secu100.net
127.0.0.1 pub-product-test-cfg.secu100.net
127.0.0.1 nat.secu100.net
127.0.0.1 tcs.secu100.net
127.0.0.1 caps.secu100.net
127.0.0.1 access-rps.secu100.net
127.0.0.1 access-dss.secu100.net
127.0.0.1 access-pms.secu100.net
127.0.0.1 access-alc.secu100.net
127.0.0.1 xmeye.net
127.0.0.1 cloud.xmeye.net
127.0.0.1 d.xmeye.net
127.0.0.1 umeye.cn
127.0.0.1 push.umeye.cn
127.0.0.1 jftech.com
127.0.0.1 viot.jftech.com
127.0.0.1 jftechws.com
127.0.0.1 upgrade.jftechws.com
127.0.0.1 aiotsecu.com
127.0.0.1 gwadar.cn
127.0.0.1 ntp.gwadar.cn
EOF

# Reject direct routing to Chinese server subnets & hardcoded cloud DNS
route add -net 165.154.0.0 netmask 255.255.0.0 reject 2>/dev/null
route add -net 178.170.192.0 netmask 255.255.240.0 reject 2>/dev/null
route add -net 188.72.108.0 netmask 255.255.255.0 reject 2>/dev/null
route add -net 156.229.0.0 netmask 255.255.0.0 reject 2>/dev/null
route add -net 112.124.0.0 netmask 255.255.0.0 reject 2>/dev/null
route add -net 66.253.35.0 netmask 255.255.255.0 reject 2>/dev/null
route add -net 87.119.19.0 netmask 255.255.255.0 reject 2>/dev/null
route add -net 159.138.0.0 netmask 255.255.0.0 reject 2>/dev/null
route add -net 42.96.0.0 netmask 255.255.0.0 reject 2>/dev/null
route add -net 43.110.0.0 netmask 255.255.0.0 reject 2>/dev/null
route add -net 54.84.0.0 netmask 255.255.0.0 reject 2>/dev/null
route add -net 114.114.114.0 netmask 255.255.255.0 reject 2>/dev/null
route add -net 223.5.5.0 netmask 255.255.255.0 reject 2>/dev/null

# Clean up any leftover Chinese cloud DNS cache files
rm -f /mnt/mtd/Config/CloudDNS.conf /mnt/mtd/Config/CloudAvailDNS.conf /mnt/mtd/Config/RPS*.txt 2>/dev/null
touch /mnt/mtd/Config/CloudDNS.conf /mnt/mtd/Config/CloudAvailDNS.conf 2>/dev/null

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
for b in xm_httpd xm_telnetd xm_ptz xm_cmd xm_rtspd xm_setcfg xm_snap xm_resetd xm_ha_osd xm_flasher wifi_manager.sh; do
    if [ -f "/mnt/custom/$b" ]; then
        cp "/mnt/custom/$b" "/var/tmp/$b" 2>/dev/null
        chmod 755 "/var/tmp/$b" 2>/dev/null
    fi
done
mkdir -p /var/tmp/cgi-bin 2>/dev/null
if [ -d /mnt/web/cgi-bin ]; then
    cp -a /mnt/web/cgi-bin/* /var/tmp/cgi-bin/ 2>/dev/null
    cp -a /mnt/web/cgi-bin/* /var/tmp/ 2>/dev/null
fi

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

# 12. Enforce 100% Offline Mode (disable all Xiongmai P2P/Cloud/NAT in App)
(
    sleep 3
    for _try in 1 2 3 4 5; do
        if [ -x /var/tmp/xm_setcfg ]; then
            /var/tmp/xm_setcfg 1040 '{"Name":"NetWork.Nat","NetWork.Nat":{"Addr":"127.0.0.1","DnsServer1":"0x0100007F","DnsServer2":"0x0100007F","NatEnable":false,"Port":0}}' >/dev/null 2>&1
            /var/tmp/xm_setcfg 1040 '{"Name":"NetWork.DisableXmServerConn","NetWork.DisableXmServerConn":{"DisableAll":true,"DisableDSS":true,"DisableNAT":true,"DisableRPS":true}}' >/dev/null 2>&1
            /var/tmp/xm_setcfg 1040 '{"Name":"NetWork.PMS","NetWork.PMS":{"Enable":false,"ServName":"127.0.0.1","Port":0}}' >/dev/null 2>&1
            /var/tmp/xm_setcfg 1040 '{"Name":"NetWork.OnlineUpgrade","NetWork.OnlineUpgrade":{"Enable":false,"AutoCheck":false,"ServerAddr":"127.0.0.1","ServerPort":0}}' >/dev/null 2>&1
        fi
        sleep 5
    done
) &
