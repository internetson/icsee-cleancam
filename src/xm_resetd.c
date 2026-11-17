#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>
#include <string.h>

#define RESET_GPIO 0
#define HOLD_SECONDS 10
#define POLL_INTERVAL_MS 200
#define REQUIRED_TICKS (HOLD_SECONDS * 1000 / POLL_INTERVAL_MS)

static void export_gpio(int pin) {
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd >= 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", pin);
        write(fd, buf, strlen(buf));
        close(fd);
    }
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    fd = open(path, O_WRONLY);
    if (fd >= 0) {
        write(fd, "in", 2);
        close(fd);
    }
}

static void do_factory_reset(void) {
    // Visual feedback: turn on spotlight
    system("/mnt/custom/xm_setcfg 1040 '{\"Name\":\"Camera.WhiteLight\",\"Camera.WhiteLight\":{\"WorkMode\":\"Open\",\"Brightness\":100}}' >/dev/null 2>&1");
    system("echo 'ircut 1' > /proc/XmMotor 2>/dev/null; usleep 200000; echo 'ircut 0' > /proc/XmMotor 2>/dev/null;");

    // Remove user configs: web password (resets to admin/admin), camera settings, PTZ presets, Wi-Fi configs, and accounts
    system("rm -f /mnt/mtd/Config/web_auth.json /mnt/mtd/Config/CameraSettings.json /mnt/mtd/Config/CameraSetting.json /mnt/mtd/Config/preset_*.txt /mnt/mtd/Config/wpa.conf /mnt/mtd/Config/wpa_supplicant.conf /mnt/mtd/Config/WLan /mnt/mtd/Config/PresetPos /mnt/mtd/Config/Account1 /mnt/mtd/Config/Account2 /mnt/mtd/Config/.wifi_txt_last");
    system("sync");

    sleep(1);
    system("reboot");
    exit(0);
}

int main(void) {
    export_gpio(RESET_GPIO);

    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", RESET_GPIO);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open gpio value");
        return 1;
    }

    int hold_ticks = 0;
    char val = '1';

    while (1) {
        lseek(fd, 0, SEEK_SET);
        if (read(fd, &val, 1) == 1) {
            // Active low: '0' means button is pressed
            if (val == '0') {
                hold_ticks++;
                if (hold_ticks >= REQUIRED_TICKS) {
                    do_factory_reset();
                }
            } else {
                hold_ticks = 0;
            }
        }
        usleep(POLL_INTERVAL_MS * 1000);
    }

    close(fd);
    return 0;
}
