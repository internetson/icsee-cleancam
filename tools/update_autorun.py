with open('D:/xm_autorun.sh', 'r', encoding='utf-8') as f:
    c = f.read()

# Replace custom update condition
old_cond1 = "if ! grep -q 'XmServices_Mgr' /mnt/custom/extapp.sh 2>/dev/null; then"
new_cond1 = "if [ ! -f /mnt/custom/xm_telnetd ] || ! grep -q 'xm_telnetd' /mnt/custom/extapp.sh 2>/dev/null; then"
c = c.replace(old_cond1, new_cond1)

# Replace web update condition
old_cond2 = "if [ ! -f /mnt/web/cgi-bin/camera_ctrl ]; then"
new_cond2 = "if ! grep -q 'XmMotor' /mnt/web/cgi-bin/ptz 2>/dev/null; then"
c = c.replace(old_cond2, new_cond2)

# Add netstat and motor info at the end
old_end = 'ps >> $LOG_FILE 2>&1\necho "-------------------------" >> $LOG_FILE'
new_end = 'ps >> $LOG_FILE 2>&1\necho "-------------------------" >> $LOG_FILE\necho "--- NETSTAT ---" >> $LOG_FILE\n$BB netstat -tlpn >> $LOG_FILE 2>&1\necho "--- MOTOR DRIVER ---" >> $LOG_FILE\ncat /proc/XmMotor >> $LOG_FILE 2>&1'
c = c.replace(old_end, new_end)

# Also ensure LF line endings
c = c.replace('\r\n', '\n')

with open('D:/xm_autorun.sh', 'wb') as f:
    f.write(c.encode('utf-8'))
print('Updated D:/xm_autorun.sh successfully!')
