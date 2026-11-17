param(
    [string]$DriveLetter = "D"
)

$target = "$($DriveLetter):\"
if (-not (Test-Path $target)) {
    Write-Host "[-] Диск $target не найден! Вставьте флешку в ПК и укажите правильную букву диска." -ForegroundColor Red
    exit 1
}

Write-Host "[*] Копирование обновленных файлов прошивки CleanCam на $target..." -ForegroundColor Cyan

$srcDir = "$PSScriptRoot\..\sd_card_installer"
Copy-Item -Path "$srcDir\*" -Destination $target -Recurse -Force

# Remove any old update marker so the camera flashes on next boot
if (Test-Path "$target\.clean_v2_done") {
    Remove-Item -Path "$target\.clean_v2_done" -Force
}
if (Test-Path "$target\.clean_v3_done") {
    Remove-Item -Path "$target\.clean_v3_done" -Force
}

Write-Host "[+] Файлы успешно скопированы на флешку!" -ForegroundColor Green
Write-Host "[+] Извлеките флешку, вставьте в камеру и подайте питание." -ForegroundColor Green
Write-Host "[+] Камера автоматически прошьет разделы mtd3 и mtd4, перезагрузится, и всё заработает!" -ForegroundColor Green
