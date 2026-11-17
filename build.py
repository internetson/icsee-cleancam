#!/usr/bin/env python3
"""
CleanCam OS: Мастер-скрипт сборки прошивки для камер Xiongmai/ICSee (GK7201V300)
Поддерживает сборку под Windows и Linux (включая CI/CD GitHub Actions).
"""

import os
import sys
import shutil
import subprocess
import zipfile

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
SRC_DIR = os.path.join(BASE_DIR, "src")
SCRIPTS_DIR = os.path.join(BASE_DIR, "scripts")
BUILD_DIR = os.path.join(BASE_DIR, "build_clean")
DIST_DIR = os.path.join(BASE_DIR, "dist")
SD_INSTALLER_DIR = os.path.join(BASE_DIR, "sd_card_installer")

def find_zig():
    if "ZIG" in os.environ and shutil.which(os.environ["ZIG"]):
        return os.environ["ZIG"]
    if shutil.which("zig"):
        return "zig"
    for candidate in [
        os.path.join(BASE_DIR, "tools", "zig", "zig.exe"),
        os.path.join(BASE_DIR, "tools", "zig", "zig"),
    ]:
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None

def compile_binaries(zig_path):
    print("=" * 60)
    print("1. Компиляция бинарных файлов под ARM (musleabihf)")
    print("=" * 60)
    
    if not zig_path:
        print("[!] Zig компилятор не найден. Используются существующие предсобранные бинарники.")
        required_bins = [
            os.path.join(BUILD_DIR, "custom-x", "xm_rtspd"),
            os.path.join(BUILD_DIR, "custom-x", "xm_resetd"),
            os.path.join(BUILD_DIR, "custom-x", "xm_snap"),
            os.path.join(BUILD_DIR, "custom-x", "xm_setcfg"),
            os.path.join(BUILD_DIR, "web-x", "cgi-bin", "auth"),
        ]
        missing = [b for b in required_bins if not os.path.isfile(b)]
        if missing:
            print("[-] Ошибка: Отсутствуют бинарники и нет компилятора Zig:")
            for m in missing:
                print(f"    - {m}")
            sys.exit(1)
        print("[+] Все необходимые бинарники уже на месте.")
        return

    print(f"[*] Используется компилятор: {zig_path}")
    
    targets = [
        {
            "src": os.path.join(SRC_DIR, "xm_rtspd.c"),
            "out": os.path.join(BUILD_DIR, "custom-x", "xm_rtspd"),
            "extra_args": ["-lpthread"],
            "name": "xm_rtspd (RTSP + ONVIF демон)"
        },
        {
            "src": os.path.join(SRC_DIR, "xm_auth.c"),
            "out": os.path.join(BUILD_DIR, "web-x", "cgi-bin", "auth"),
            "extra_args": [],
            "name": "auth (CGI авторизации)"
        },
        {
            "src": os.path.join(SRC_DIR, "xm_resetd.c"),
            "out": os.path.join(BUILD_DIR, "custom-x", "xm_resetd"),
            "extra_args": [],
            "name": "xm_resetd (Демон кнопки Reset)"
        },
        {
            "src": os.path.join(SRC_DIR, "xm_snap.c"),
            "out": os.path.join(BUILD_DIR, "custom-x", "xm_snap"),
            "extra_args": [],
            "name": "xm_snap (Снимок кадра)"
        },
        {
            "src": os.path.join(SRC_DIR, "xm_setcfg.c"),
            "out": os.path.join(BUILD_DIR, "custom-x", "xm_setcfg"),
            "extra_args": [],
            "name": "xm_setcfg (Настройка параметров сенсора)"
        },
    ]

    for t in targets:
        print(f"[*] Компиляция {t['name']}...")
        os.makedirs(os.path.dirname(t["out"]), exist_ok=True)
        cmd = [
            zig_path, "cc",
            "-target", "arm-linux-musleabihf",
            "-O3", "-s", "-static",
            t["src"],
            "-o", t["out"]
        ] + t["extra_args"]
        res = subprocess.run(cmd)
        if res.returncode != 0:
            print(f"[-] Ошибка компиляции {t['src']}")
            sys.exit(1)
        sz = os.path.getsize(t["out"])
        print(f"    -> {t['out']} ({sz} байт)")

def build_images():
    print("\n" + "=" * 60)
    print("2. Сборка образов SquashFS и U-Boot")
    print("=" * 60)
    
    script = os.path.join(SCRIPTS_DIR, "build_clean_images.py")
    res = subprocess.run([sys.executable, script])
    if res.returncode != 0:
        print("[-] Ошибка при сборке образов SquashFS")
        sys.exit(1)

def package_dist():
    print("\n" + "=" * 60)
    print("3. Упаковка релизных артефактов в dist/")
    print("=" * 60)
    
    os.makedirs(DIST_DIR, exist_ok=True)
    
    # Файлы разделов
    images = [
        "custom-x.squashfs",
        "custom-x.squashfs.img",
        "web-x.squashfs",
        "web-x.squashfs.img",
    ]
    for img in images:
        src = os.path.join(BUILD_DIR, img)
        dst = os.path.join(DIST_DIR, img)
        if os.path.exists(src):
            shutil.copy2(src, dst)
            print(f"[+] Скопирован в dist/: {img} ({os.path.getsize(dst)} байт)")
    
    # Создание ZIP-архива для прошивки с MicroSD
    zip_path = os.path.join(DIST_DIR, "cleancam_sd_installer.zip")
    print(f"[*] Создание архива {zip_path}...")
    
    files_to_pack = [
        ("xm_autorun.sh", os.path.join(SD_INSTALLER_DIR, "xm_autorun.sh")),
        ("xm_flasher", os.path.join(SD_INSTALLER_DIR, "xm_flasher")),
        ("busybox", os.path.join(SD_INSTALLER_DIR, "busybox")),
        ("wifi.txt.example", os.path.join(SD_INSTALLER_DIR, "wifi.txt.example")),
        ("custom-x.squashfs", os.path.join(BUILD_DIR, "custom-x.squashfs")),
        ("web-x.squashfs", os.path.join(BUILD_DIR, "web-x.squashfs")),
    ]
    
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for arcname, fpath in files_to_pack:
            if os.path.exists(fpath):
                zf.write(fpath, arcname=arcname)
                print(f"    + {arcname} ({os.path.getsize(fpath)} байт)")
            else:
                print(f"    [!] Пропущен отсутствующий файл: {fpath}")
                
        # Добавляем краткую инструкцию прямо в архив
        readme_txt = """CleanCam OS - Установка с MicroSD карты
=======================================
1. Отформатируйте MicroSD карту (до 64 ГБ) в файловую систему FAT32.
2. Распакуйте ВСЕ файлы из этого архива прямо в корень карты памяти:
   - xm_autorun.sh
   - xm_flasher
   - busybox
   - wifi.txt.example
   - custom-x.squashfs
   - web-x.squashfs

3. (Опционально) Автонастройка Wi-Fi:
   Переименуйте wifi.txt.example в wifi.txt и укажите имя сети и пароль:
   ssid=ИмяСети
   password=Пароль
   Тогда камера сразу после прошивки подключится к вашей домашней сети!

4. Вставьте карту памяти в выключенную камеру.
5. Включите питание камеры. Скрипт xm_autorun.sh автоматически прошьет
   разделы mtd4 (custom) и mtd3 (web) и перезагрузит камеру (~15-30 секунд).
6. Извлеките карту памяти (или оставьте для автонастройки Wi-Fi).
7. Камера готова к работе:
   - Веб-интерфейс: http://<IP_КАМЕРЫ>/ (admin / admin)
   - RTSP Main:     rtsp://<IP_КАМЕРЫ>:554/live/ch0
   - RTSP Sub:      rtsp://<IP_КАМЕРЫ>:554/live/ch1
   - ONVIF порт:    8899 (admin / admin)
   - Telnet (Root): telnet <IP_КАМЕРЫ> 23 (логин root, без пароля)

Если wifi.txt не был создан, камера создаст точку доступа CleanCam_Setup (192.168.10.1).
"""
        zf.writestr("ИНСТРУКЦИЯ.txt", readme_txt.encode("utf-8"))
        print("    + ИНСТРУКЦИЯ.txt")

    print(f"[+] Готово! Архив для MicroSD карты: {zip_path} ({os.path.getsize(zip_path)} байт)")
    print("\n[+] Сборка CleanCam OS успешно завершена!")

def main():
    zig = find_zig()
    compile_binaries(zig)
    build_images()
    package_dist()

if __name__ == "__main__":
    main()
