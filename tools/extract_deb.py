import urllib.request
import io
import tarfile

deb_url = 'http://ftp.debian.org/debian/pool/main/b/busybox/busybox-static_1.35.0-4+deb12u1+b1_armel.deb'
print('Downloading', deb_url)
req = urllib.request.Request(deb_url, headers={'User-Agent': 'Mozilla/5.0'})
with urllib.request.urlopen(req) as resp:
    deb_data = resp.read()

print('Downloaded deb size:', len(deb_data))

# A .deb file is an 'ar' archive containing debian-binary, control.tar.gz, data.tar.xz
# Parse ar archive:
# global header: !<arch>\n (8 bytes)
# header per file: 60 bytes: name(16), date(12), uid(6), gid(6), mode(8), size(10), `\n (2)
pos = 8
while pos < len(deb_data):
    hdr = deb_data[pos:pos+60]
    if len(hdr) < 60:
        break
    name = hdr[:16].strip().decode('ascii')
    size = int(hdr[48:58].strip().decode('ascii'))
    pos += 60
    content = deb_data[pos:pos+size]
    pos += size
    if pos % 2 != 0:
        pos += 1 # 2-byte alignment
    
    print('AR member:', name, size)
    if 'data.tar' in name:
        with tarfile.open(fileobj=io.BytesIO(content)) as tar:
            for member in tar.getmembers():
                if member.name == './bin/busybox':
                    print('Found busybox member:', member.name, member.size)
                    f = tar.extractfile(member)
                    bb_data = f.read()
                    with open('sd_card_installer/busybox', 'wb') as out:
                        out.write(bb_data)
                    print('Wrote sd_card_installer/busybox, size:', len(bb_data))
