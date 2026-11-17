import keystone

ks = keystone.Ks(keystone.KS_ARCH_ARM, keystone.KS_MODE_ARM)
enc1, _ = ks.asm('.asciz "GET"')
print('GET:', bytes(enc1))
enc2, _ = ks.asm('.asciz "HTTP/1.0 200 OK\\r\\n\\r\\n"')
print('CRLF:', bytes(enc2))
