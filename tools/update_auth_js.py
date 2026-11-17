with open('build_clean/web-x/index.html', 'r', encoding='utf-8') as f:
    content = f.read()

start_marker = '        // --- AUTHENTICATION ---'
end_marker = '        // --- SYNCHRONIZED LIVE VIDEO STREAMING ---'

idx1 = content.find(start_marker)
idx2 = content.find(end_marker)

if idx1 != -1 and idx2 != -1:
    new_auth = '''        // --- AUTHENTICATION ---
        function checkAuth() {
            const tok = localStorage.getItem("cleancam_token") || "";
            fetch(`${API_BASE}/cgi-bin/auth?action=check&token=${encodeURIComponent(tok)}`)
                .then(r => r.json())
                .then(data => {
                    if (data && data.authenticated) {
                        unlockDashboard(data.username || localStorage.getItem("cleancam_user") || "admin");
                    } else {
                        showLoginModal();
                    }
                })
                .catch(() => showLoginModal());
        }

        function showLoginModal() {
            document.getElementById("login-modal").style.display = "flex";
            document.getElementById("main-dashboard").style.filter = "blur(4px)";
            document.getElementById("main-dashboard").style.pointerEvents = "none";
        }

        function unlockDashboard(username) {
            document.getElementById("login-modal").style.display = "none";
            document.getElementById("main-dashboard").style.filter = "none";
            document.getElementById("main-dashboard").style.pointerEvents = "auto";
            document.getElementById("user-display").innerText = username || "admin";

            startLiveStream();
            renderPresetSlots();
            loadPresetsList();
            loadAllSettings();
            fetchLightState();
            updateStatus();
        }

        function handleLogin(e) {
            e.preventDefault();
            const u = document.getElementById("login-user").value.trim();
            const p = document.getElementById("login-pass").value;
            const btn = document.getElementById("btn-login-submit");

            btn.disabled = true;
            btn.innerText = "Авторизация...";

            fetch(`${API_BASE}/cgi-bin/auth?action=login&user=${encodeURIComponent(u)}&pass=${encodeURIComponent(p)}`)
            .then(r => r.json())
            .then(data => {
                btn.disabled = false;
                btn.innerText = "Войти в систему";
                if (data.status === "ok") {
                    if (data.token) localStorage.setItem("cleancam_token", data.token);
                    if (data.username) localStorage.setItem("cleancam_user", data.username);
                    showToast("Успешный вход в CleanCam OS!");
                    unlockDashboard(data.username);
                } else {
                    showToast(data.message || "Неверный логин или пароль", "error");
                }
            })
            .catch(() => {
                btn.disabled = false;
                btn.innerText = "Войти в систему";
                showToast("Ошибка соединения с камерой", "error");
            });
        }

        function handleLogout() {
            const tok = localStorage.getItem("cleancam_token") || "";
            fetch(`${API_BASE}/cgi-bin/auth?action=logout&token=${encodeURIComponent(tok)}`)
                .finally(() => {
                    localStorage.removeItem("cleancam_token");
                    localStorage.removeItem("cleancam_user");
                    showToast("Выход выполнен");
                    showLoginModal();
                });
        }

        function openPasswordModal() { document.getElementById("pwd-modal").style.display = "flex"; }
        function closePasswordModal() { document.getElementById("pwd-modal").style.display = "none"; }

        function handleChangePassword(e) {
            e.preventDefault();
            const oldPass = document.getElementById("old-pass").value;
            const newPass = document.getElementById("new-pass").value;
            const confirmPass = document.getElementById("new-pass-confirm").value;

            if (newPass !== confirmPass) {
                showToast("Новые пароли не совпадают!", "error");
                return;
            }

            const btn = document.getElementById("btn-change-pwd");
            btn.disabled = true;
            btn.innerText = "Сохранение...";

            const tok = localStorage.getItem("cleancam_token") || "";
            fetch(`${API_BASE}/cgi-bin/auth?action=change_password&token=${encodeURIComponent(tok)}&old_pass=${encodeURIComponent(oldPass)}&new_pass=${encodeURIComponent(newPass)}`)
            .then(r => r.json())
            .then(data => {
                btn.disabled = false;
                btn.innerText = "Сохранить";
                if (data.status === "ok") {
                    if (data.token) localStorage.setItem("cleancam_token", data.token);
                    showToast(data.message || "Пароль успешно обновлен!");
                    closePasswordModal();
                    document.getElementById("old-pass").value = "";
                    document.getElementById("new-pass").value = "";
                    document.getElementById("new-pass-confirm").value = "";
                } else {
                    showToast(data.message || "Ошибка смены пароля", "error");
                }
            })
            .catch(() => {
                btn.disabled = false;
                btn.innerText = "Сохранить";
                showToast("Ошибка сети при смене пароля", "error");
            });
        }

'''
    updated = content[:idx1] + new_auth + content[idx2:]
    with open('build_clean/web-x/index.html', 'w', encoding='utf-8') as f:
        f.write(updated)
    print('Updated index.html successfully!')
else:
    print('Markers not found:', idx1, idx2)
