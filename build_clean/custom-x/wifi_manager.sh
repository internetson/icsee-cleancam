#!/bin/sh
# ==============================================================================
# CleanCam Wi-Fi Client & Fallback Access Point (AP) Manager
# ==============================================================================
export PATH="/bin:/sbin:/usr/bin:/usr/sbin:/mnt/custom:/var/tmp:$PATH"

WIFI_CONF="/mnt/mtd/Config/wpa.conf"
AP_CONF="/var/tmp/hostapd.conf"
DHCP_CONF="/var/tmp/udhcpd.conf"
STATUS_JSON="/var/tmp/wifi_status.json"

IFACE="eth2"
[ -d "/sys/class/net/wlan0" ] && IFACE="wlan0"

# Kill any rogue Xiongmai wireless daemons
killall -9 XmWlanDaemon XmWifiScan 2>/dev/null

mkdir -p /var/run/hostapd /var/tmp /var/lib/misc /mnt/mtd/Config /mnt/mtd/Log 2>/dev/null
touch /var/lib/misc/udhcpd.leases /var/tmp/udhcpd.leases 2>/dev/null

# Setup clean DHCP handler
if [ -f /mnt/custom/default.script ]; then
    cp /mnt/custom/default.script /var/tmp/udhcpc.script 2>/dev/null
    chmod 755 /var/tmp/udhcpc.script 2>/dev/null
    mount -o bind /var/tmp/udhcpc.script /usr/sbin/udhcpc.script 2>/dev/null
fi

DHCP_SCRIPT="/var/tmp/udhcpc.script"
[ ! -f "$DHCP_SCRIPT" ] && DHCP_SCRIPT="/mnt/custom/default.script"

clean_val() {
    echo "$1" | sed -e 's/^[ \t]*//' -e 's/[ \t]*$//' -e 's/^"//' -e 's/"$//' -e "s/^'//" -e "s/'$//"
}

parse_wifi_file() {
    _file="$1"
    [ ! -f "$_file" ] && return 1

    W_SSID=""
    W_PASS=""

    _raw_ssid=$(sed -e 's/\r//g' "$_file" | grep -i '^[[:space:]]*ssid[[:space:]]*=' | head -n1 | sed -e 's/^[[:space:]]*[^=]*=[[:space:]]*//')
    _raw_pass=$(sed -e 's/\r//g' "$_file" | grep -i -E '^[[:space:]]*(password|pass|psk)[[:space:]]*=' | head -n1 | sed -e 's/^[[:space:]]*[^=]*=[[:space:]]*//')

    if [ -n "$_raw_ssid" ]; then
        W_SSID=$(clean_val "$_raw_ssid")
        W_PASS=$(clean_val "$_raw_pass")
    else
        _lines=$(sed -e 's/\r//g' "$_file" | grep -v '^[[:space:]]*#' | grep -v '^[[:space:]]*$')
        _line1=$(echo "$_lines" | sed -n '1p')
        _line2=$(echo "$_lines" | sed -n '2p')
        W_SSID=$(clean_val "$_line1")
        W_PASS=$(clean_val "$_line2")
    fi

    [ -n "$W_SSID" ]
}

import_sd_wifi() {
    _sd_file=""
    for _candidate in /var/tmp/mmcblock0/wifi.txt /var/tmp/mmcblock0/WIFI.TXT /var/tmp/mmcblock0/Wifi.txt; do
        if [ -f "$_candidate" ]; then
            _sd_file="$_candidate"
            break
        fi
    done

    [ -z "$_sd_file" ] && return 1

    _last_file="/mnt/mtd/Config/.wifi_txt_last"
    if [ -f "$_last_file" ] && [ -f "$WIFI_CONF" ]; then
        if [ "$(cat "$_sd_file" 2>/dev/null)" = "$(cat "$_last_file" 2>/dev/null)" ]; then
            return 1
        fi
    fi

    if parse_wifi_file "$_sd_file"; then
        echo "[WiFi-Mgr] Auto-provisioning Wi-Fi from SD card ($_sd_file): SSID='$W_SSID'..."
        mkdir -p /mnt/mtd/Config /mnt/mtd/Log 2>/dev/null
        if [ -n "$W_PASS" ]; then
            cat << EOF > "$WIFI_CONF"
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
            cat << EOF > "$WIFI_CONF"
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
        cp -f "$_sd_file" "$_last_file" 2>/dev/null
        sync
        return 0
    fi
    return 1
}

save_status() {
    _mode="$1"
    _ip="$2"
    _ssid="$3"
    echo "{\"mode\":\"$_mode\",\"ip\":\"$_ip\",\"ssid\":\"$_ssid\",\"interface\":\"$IFACE\"}" > "$STATUS_JSON"
}

has_valid_ip() {
    IP=$(ifconfig $IFACE 2>/dev/null | grep 'inet addr' | grep -v '192.168.10.1' | awk -F: '{print $2}' | awk '{print $1}')
    [ -n "$IP" ]
}

start_client_mode() {
    echo "[WiFi-Mgr] Starting Client Mode for $IFACE..."
    killall -9 hostapd udhcpd XmWlanDaemon XmWifiScan 2>/dev/null
    sleep 1

    ifconfig $IFACE 0.0.0.0 up 2>/dev/null
    killall -9 wpa_supplicant udhcpc 2>/dev/null
    sleep 1

    echo "[WiFi-Mgr] Connecting to AP via wpa_supplicant..."
    wpa_supplicant -i $IFACE -c "$WIFI_CONF" -B 2>/dev/null
    sleep 3

    echo "[WiFi-Mgr] Requesting DHCP lease..."
    udhcpc -i $IFACE -s "$DHCP_SCRIPT" -b -R 2>/dev/null &

    IP=""
    for _try in 1 2 3 4 5 6 7 8; do
        sleep 2
        IP=$(ifconfig $IFACE 2>/dev/null | grep 'inet addr' | grep -v '192.168.10.1' | awk -F: '{print $2}' | awk '{print $1}')
        [ -n "$IP" ] && break
    done

    CUR_SSID=$(grep 'ssid=' "$WIFI_CONF" 2>/dev/null | head -n1 | cut -d'"' -f2)
    if [ -n "$IP" ]; then
        echo "[WiFi-Mgr] Connected! IP: $IP (SSID: $CUR_SSID)"
        save_status "Client" "$IP" "$CUR_SSID"
    else
        echo "[WiFi-Mgr] DHCP slow, waiting in background..."
    fi
}

start_ap_mode() {
    echo "[WiFi-Mgr] Starting Fallback Access Point Mode..."
    killall -9 wpa_supplicant udhcpc XmWlanDaemon XmWifiScan 2>/dev/null
    sleep 1

    # 1. Generate hostapd config (Open network CleanCam_Setup)
    cat << EOF > "$AP_CONF"
interface=$IFACE
driver=nl80211
ctrl_interface=/var/run/hostapd
ssid=CleanCam_Setup
channel=6
hw_mode=g
ieee80211n=1
ignore_broadcast_ssid=0
EOF

    # 2. Generate udhcpd config for 192.168.10.0/24 subnet
    cat << EOF > "$DHCP_CONF"
start 192.168.10.20
end 192.168.10.254
interface $IFACE
opt router 192.168.10.1
opt subnet 255.255.255.0
opt dns 192.168.10.1
lease_file /var/tmp/udhcpd.leases
EOF

    touch /var/tmp/udhcpd.leases
    ifconfig $IFACE down 2>/dev/null
    sleep 1
    ifconfig $IFACE 192.168.10.1 netmask 255.255.255.0 up
    iwconfig $IFACE mode managed 2>/dev/null
    sleep 1

    # Start hostapd & udhcpd
    killall -9 hostapd udhcpd 2>/dev/null
    hostapd "$AP_CONF" -B 2>/dev/null
    sleep 1

    if ! pidof hostapd >/dev/null 2>&1; then
        echo "[WiFi-Mgr] hostapd default failed, trying rtl_hostapd template..."
        cp -rf /usr/sbin/rtl_hostapd.conf /var/tmp/hostapd.conf 2>/dev/null
        sed -i "s/ssid=IPC_NUM/ssid=CleanCam_Setup/g" /var/tmp/hostapd.conf 2>/dev/null
        sed -i "s/wpa=2/wpa=0/g" /var/tmp/hostapd.conf 2>/dev/null
        hostapd /var/tmp/hostapd.conf -B 2>/dev/null
        sleep 1
    fi

    udhcpd "$DHCP_CONF" 2>/dev/null || udhcpd -fS /usr/sbin/udhcpd.conf &
    echo "[WiFi-Mgr] Access Point 'CleanCam_Setup' active at 192.168.10.1!"
    save_status "AccessPoint" "192.168.10.1" "CleanCam_Setup"
}

# Initial Boot Decision
import_sd_wifi
if [ -f "$WIFI_CONF" ] && grep -q 'ssid=' "$WIFI_CONF"; then
    start_client_mode
else
    echo "[WiFi-Mgr] No Wi-Fi credentials found. Launching AP Mode..."
    start_ap_mode
fi

# Watchdog Loop
FAIL_COUNT=0
while true; do
    sleep 15

    # Check if a new or updated wifi.txt was placed on SD card
    if import_sd_wifi; then
        echo "[WiFi-Mgr] New Wi-Fi configuration from SD card detected! Reconnecting..."
        start_client_mode
        FAIL_COUNT=0
        continue
    fi

    # Ensure web and telnet servers are alive
    if ! pidof xm_httpd >/dev/null 2>&1; then
        if [ -x /mnt/custom/xm_httpd ]; then
            /mnt/custom/xm_httpd 80 &
            /mnt/custom/xm_httpd 8080 &
        elif [ -x /var/tmp/xm_httpd ]; then
            /var/tmp/xm_httpd 80 &
            /var/tmp/xm_httpd 8080 &
        fi
    fi
    if ! pidof xm_telnetd >/dev/null 2>&1; then
        if [ -x /mnt/custom/xm_telnetd ]; then
            /mnt/custom/xm_telnetd &
        elif [ -x /var/tmp/xm_telnetd ]; then
            /var/tmp/xm_telnetd &
        fi
    fi

    # Check if web interface requested a new connection
    if [ -f "/var/tmp/wifi_trigger_connect" ]; then
        rm -f "/var/tmp/wifi_trigger_connect"
        start_client_mode
        FAIL_COUNT=0
        continue
    fi

    # In Client Mode: only trigger fallback if no valid IP for > 2 minutes (8 * 15s)
    if [ -f "$WIFI_CONF" ] && grep -q 'ssid=' "$WIFI_CONF" && ! pidof hostapd >/dev/null 2>&1; then
        if ! has_valid_ip; then
            FAIL_COUNT=$((FAIL_COUNT + 1))
            echo "[WiFi-Mgr] Client IP lost ($FAIL_COUNT/8), retrying DHCP..."
            udhcpc -i $IFACE -s "$DHCP_SCRIPT" -b -R 2>/dev/null &
            if [ $FAIL_COUNT -ge 8 ]; then
                echo "[WiFi-Mgr] Client unreachable for 120s. Switching to Fallback AP..."
                start_ap_mode
                FAIL_COUNT=0
            fi
        else
            FAIL_COUNT=0
        fi
    fi
done
