import os, re

path = 'build_clean/web-x/index.html'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

# 1. CSS update: add button styles
css_target = ".copy-btn:hover { background: var(--accent); color: #000; }"
css_replace = """.copy-btn:hover { background: var(--accent); color: #000; }

        .btn-light-group { display: flex; gap: 4px; }
        .btn-light { background: #1c263d; border: 1px solid #283756; color: var(--text); padding: 5px 10px; border-radius: 6px; font-size: 0.74rem; cursor: pointer; transition: all 0.2s ease; white-space: nowrap; }
        .btn-light:hover { border-color: var(--accent); color: #fff; }
        .btn-light.active-mode { background: var(--accent); color: #000; font-weight: 700; border-color: var(--accent); box-shadow: 0 0 10px rgba(56, 189, 248, 0.4); }"""

if css_target in content:
    content = content.replace(css_target, css_replace, 1)
    print("[+] Added CSS styles")
else:
    print("[-] CSS target not found")

# 2. Add Quick Lights Card in tab-live
quick_lights_html = """            <!-- Quick Lights Bar -->
            <div class="card" style="margin-top: 10px; padding: 12px 16px;">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
                    <div class="card-title" style="font-size: 0.88rem;">💡 Быстрое освещение и Ночной режим (Spotlight & IR)</div>
                    <span style="font-size: 0.72rem; color: var(--text-muted);">Home Assistant / REST API: <code>/cgi-bin/light</code></span>
                </div>
                <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 10px;">
                    <!-- White Light Spotlight -->
                    <div style="background: rgba(15, 23, 42, 0.6); border: 1px solid #283756; border-radius: 8px; padding: 8px 12px; display: flex; justify-content: space-between; align-items: center;">
                        <div style="display: flex; align-items: center; gap: 8px;">
                            <span style="font-size: 1.15rem;">💡</span>
                            <div>
                                <div style="font-size: 0.82rem; font-weight: 600; color: #f8fafc;">Белый фонарь</div>
                                <div style="font-size: 0.68rem; color: var(--text-muted);">LED прожектор подсветки</div>
                            </div>
                        </div>
                        <div class="btn-light-group">
                            <button type="button" class="btn-light btn-light-white" data-mode="on" onclick="setLight('white', 'on')">Вкл</button>
                            <button type="button" class="btn-light btn-light-white" data-mode="off" onclick="setLight('white', 'off')">Выкл</button>
                            <button type="button" class="btn-light btn-light-white" data-mode="auto" onclick="setLight('white', 'auto')">Авто</button>
                        </div>
                    </div>

                    <!-- IR Backlight -->
                    <div style="background: rgba(15, 23, 42, 0.6); border: 1px solid #283756; border-radius: 8px; padding: 8px 12px; display: flex; justify-content: space-between; align-items: center;">
                        <div style="display: flex; align-items: center; gap: 8px;">
                            <span style="font-size: 1.15rem;">🌙</span>
                            <div>
                                <div style="font-size: 0.82rem; font-weight: 600; color: #f8fafc;">ИК подсветка</div>
                                <div style="font-size: 0.68rem; color: var(--text-muted);">Ночной режим / IR-Cut</div>
                            </div>
                        </div>
                        <div class="btn-light-group">
                            <button type="button" class="btn-light btn-light-ir" data-mode="on" onclick="setLight('ir', 'on')">Ночь (ИК)</button>
                            <button type="button" class="btn-light btn-light-ir" data-mode="off" onclick="setLight('ir', 'off')">День (Цвет)</button>
                            <button type="button" class="btn-light btn-light-ir" data-mode="auto" onclick="setLight('ir', 'auto')">Авто</button>
                        </div>
                    </div>
                </div>
            </div>

            <!-- Keyboard & Controls Hint Banner -->"""

banner_target = "<!-- Keyboard & Controls Hint Banner -->"
if banner_target in content:
    content = content.replace(banner_target, quick_lights_html, 1)
    print("[+] Added Quick Lights card to tab-live")
else:
    print("[-] Banner target not found")

# 3. Add Quick Lights to tab-night
night_target = """<div class="card-header">
                            <div class="card-title">🌙 Ночной режим, ИК-фильтр и LED-прожектор</div>
                            <button class="btn-scan" onclick="loadAllSettings()">🔄 Обновить</button>
                        </div>"""
night_replace = """<div class="card-header">
                            <div class="card-title">🌙 Ночной режим, ИК-фильтр и LED-прожектор</div>
                            <button class="btn-scan" onclick="loadAllSettings()">🔄 Обновить</button>
                        </div>

                        <!-- Quick triggers inside night tab -->
                        <div style="display: flex; gap: 12px; flex-wrap: wrap; background: rgba(15, 23, 42, 0.5); padding: 8px 12px; border-radius: 8px; margin-bottom: 12px; align-items: center; justify-content: space-between;">
                            <div style="display: flex; align-items: center; gap: 8px;">
                                <span>💡 <strong>Фонарь:</strong></span>
                                <div class="btn-light-group">
                                    <button type="button" class="btn-light btn-light-white" data-mode="on" onclick="setLight('white', 'on')">Вкл</button>
                                    <button type="button" class="btn-light btn-light-white" data-mode="off" onclick="setLight('white', 'off')">Выкл</button>
                                    <button type="button" class="btn-light btn-light-white" data-mode="auto" onclick="setLight('white', 'auto')">Авто</button>
                                </div>
                            </div>
                            <div style="display: flex; align-items: center; gap: 8px;">
                                <span>🌙 <strong>ИК подсветка:</strong></span>
                                <div class="btn-light-group">
                                    <button type="button" class="btn-light btn-light-ir" data-mode="on" onclick="setLight('ir', 'on')">Ночь (ИК)</button>
                                    <button type="button" class="btn-light btn-light-ir" data-mode="off" onclick="setLight('ir', 'off')">День (Цвет)</button>
                                    <button type="button" class="btn-light btn-light-ir" data-mode="auto" onclick="setLight('ir', 'auto')">Авто</button>
                                </div>
                            </div>
                        </div>"""

if night_target in content:
    content = content.replace(night_target, night_replace, 1)
    print("[+] Added Quick triggers to tab-night")
else:
    print("[-] Night target not found")

# 4. Add Home Assistant endpoints to tab-network
snap_box_target = """                        <label class="form-label">HTTP Snapshot (Прямой аппаратный снимок JPEG):</label>
                        <div class="url-box">
                            <span id="snap-url">http://camera_ip:8080/cgi-bin/snapshot</span>
                            <button class="copy-btn" onclick="copyText('snap-url')">Копировать</button>
                        </div>"""

ha_replace = """                        <label class="form-label">HTTP Snapshot (Прямой аппаратный снимок JPEG):</label>
                        <div class="url-box">
                            <span id="snap-url">http://camera_ip:8080/cgi-bin/snapshot</span>
                            <button class="copy-btn" onclick="copyText('snap-url')">Копировать</button>
                        </div>

                        <label class="form-label">Home Assistant REST API — Белый фонарь (Вкл / Выкл / Авто):</label>
                        <div class="url-box">
                            <span id="ha-white-api">http://camera_ip:8080/cgi-bin/light?white=on</span>
                            <button class="copy-btn" onclick="copyText('ha-white-api')">Копировать</button>
                        </div>

                        <label class="form-label">Home Assistant REST API — ИК подсветка (Вкл / Выкл / Авто):</label>
                        <div class="url-box">
                            <span id="ha-ir-api">http://camera_ip:8080/cgi-bin/light?ir=on</span>
                            <button class="copy-btn" onclick="copyText('ha-ir-api')">Копировать</button>
                        </div>

                        <label class="form-label">Home Assistant REST API — Статус подсветки (JSON):</label>
                        <div class="url-box">
                            <span id="ha-status-api">http://camera_ip:8080/cgi-bin/light</span>
                            <button class="copy-btn" onclick="copyText('ha-status-api')">Копировать</button>
                        </div>"""

if snap_box_target in content:
    content = content.replace(snap_box_target, ha_replace, 1)
    print("[+] Added Home Assistant endpoints to tab-network")
else:
    print("[-] Snapshot target not found")

# 5. Replace copyText with cross-browser HTTP-friendly fallback
copy_old = """        function copyText(elemId) {
            const txt = document.getElementById(elemId).innerText;
            navigator.clipboard.writeText(txt).then(() => {
                showToast("Ссылка скопирована в буфер обмена!");
            });
        }"""

copy_new = """        function copyText(elemId) {
            const el = document.getElementById(elemId);
            if (!el) return;
            const txt = (el.innerText || el.textContent || el.value || "").trim();
            if (!txt) return;

            function fallbackCopy(text) {
                const ta = document.createElement("textarea");
                ta.value = text;
                ta.setAttribute("readonly", "");
                ta.style.position = "fixed";
                ta.style.left = "-9999px";
                ta.style.opacity = "0";
                document.body.appendChild(ta);
                ta.focus();
                ta.select();
                let success = false;
                try {
                    success = document.execCommand("copy");
                } catch (err) {
                    success = false;
                }
                document.body.removeChild(ta);
                if (success) {
                    showToast("Скопировано: " + (text.length > 36 ? text.substring(0, 36) + "..." : text));
                } else {
                    prompt("Скопируйте вручную (Ctrl+C, Enter):", text);
                }
            }

            if (navigator.clipboard && window.isSecureContext) {
                navigator.clipboard.writeText(txt).then(() => {
                    showToast("Скопировано: " + (txt.length > 36 ? txt.substring(0, 36) + "..." : txt));
                }).catch(() => {
                    fallbackCopy(txt);
                });
            } else {
                fallbackCopy(txt);
            }
        }

        // --- QUICK LIGHT & IR API (Home Assistant) ---
        let currentLightState = { white: "auto", ir: "auto", brightness: 100 };

        async function fetchLightState() {
            try {
                const res = await fetch(`${API_BASE}/cgi-bin/light`, { cache: "no-store" });
                if (res.ok) {
                    const data = await res.json();
                    if (data.white) currentLightState.white = data.white;
                    if (data.ir) currentLightState.ir = data.ir;
                    if (data.brightness) currentLightState.brightness = data.brightness;
                    updateLightUI();
                }
            } catch (e) {
                console.error("fetchLightState error:", e);
            }
        }

        async function setLight(type, mode) {
            try {
                let url = `${API_BASE}/cgi-bin/light?${type}=${mode}`;
                const res = await fetch(url);
                if (res.ok) {
                    const data = await res.json();
                    if (data.white) currentLightState.white = data.white;
                    if (data.ir) currentLightState.ir = data.ir;
                    updateLightUI();
                    const msg = type === "white"
                        ? (mode === "on" ? "💡 Белый фонарь включен" : (mode === "off" ? "💡 Фонарь выключен" : "💡 Фонарь переведен в Авто"))
                        : (mode === "on" ? "🌙 ИК подсветка включена (Ночь)" : (mode === "off" ? "☀️ ИК выключена (День / Цвет)" : "🌙 ИК переведена в Авто"));
                    showToast(msg);
                }
            } catch (e) {
                showToast("Ошибка освещения: " + e.message);
            }
        }

        function updateLightUI() {
            document.querySelectorAll(".btn-light-white").forEach(btn => {
                btn.classList.toggle("active-mode", btn.dataset.mode === currentLightState.white);
            });
            document.querySelectorAll(".btn-light-ir").forEach(btn => {
                btn.classList.toggle("active-mode", btn.dataset.mode === currentLightState.ir);
            });
        }"""

if copy_old in content:
    content = content.replace(copy_old, copy_new, 1)
    print("[+] Replaced copyText with fallback and added light API functions")
else:
    print("[-] copy_old target not found")

# 6. Update URL generator in updateStatus()
url_old = """                        document.getElementById("snap-url").innerText = `http://${ip}:8080/cgi-bin/snapshot`;
                    }"""

url_new = """                        document.getElementById("snap-url").innerText = `http://${ip}:8080/cgi-bin/snapshot`;
                        const haW = document.getElementById("ha-white-api");
                        if (haW) haW.innerText = `http://${ip}:8080/cgi-bin/light?white=on`;
                        const haIr = document.getElementById("ha-ir-api");
                        if (haIr) haIr.innerText = `http://${ip}:8080/cgi-bin/light?ir=on`;
                        const haSt = document.getElementById("ha-status-api");
                        if (haSt) haSt.innerText = `http://${ip}:8080/cgi-bin/light`;
                    }"""

if url_old in content:
    content = content.replace(url_old, url_new, 1)
    print("[+] Updated updateStatus URLs")
else:
    print("[-] url_old target not found")

# 7. Add fetchLightState() on init / tab switch
switch_tab_target = """            if (tabId === "tab-osd-audio") {
                updateOsdPreviewMarkers();
            }"""

switch_tab_replace = """            if (tabId === "tab-osd-audio") {
                updateOsdPreviewMarkers();
            }
            if (tabId === "tab-live" || tabId === "tab-night") {
                fetchLightState();
            }"""

if switch_tab_target in content:
    content = content.replace(switch_tab_target, switch_tab_replace, 1)
    print("[+] Added fetchLightState to switchTab")
else:
    print("[-] switch_tab_target not found")

# Also call fetchLightState on login success
login_target = """            loadAllSettings();
            loadPresetsList();"""

login_replace = """            loadAllSettings();
            loadPresetsList();
            fetchLightState();"""

if login_target in content:
    content = content.replace(login_target, login_replace, 1)
    print("[+] Added fetchLightState to login handler")
else:
    print("[-] login_target not found")

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)

print("[+] index.html successfully updated!")
