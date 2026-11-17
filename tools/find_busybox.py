import urllib.request
import re

url = 'http://ftp.debian.org/debian/pool/main/b/busybox/'
req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
try:
    with urllib.request.urlopen(req) as resp:
        html = resp.read().decode('latin1')
        debs = [x for x in re.findall(r'busybox-static_[^"]*_armel\.deb', html)]
        print('Debian armel debs:', debs[-5:])
except Exception as e:
    print(e)
