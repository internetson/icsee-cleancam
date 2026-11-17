import re
data = open('build_clean/web-x/index.html', encoding='utf-8').read()
for m in re.finditer(r'rtsp:[^\s"\'<]+', data):
    print(m.group(0))
