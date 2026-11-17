import socket, time

def test_telnet():
    s = socket.socket()
    s.settimeout(6.0)
    try:
        s.connect(('10.10.11.155', 23))
        s.sendall(b'\n')
        time.sleep(0.3)
        iac = s.recv(1024)
        print('IAC:', repr(iac))
        if b'\xff' in iac:
            s.sendall(b'\xff\xfc\x01\xff\xfc\x1f\xff\xfe\x01\xff\xfe\x03')
            time.sleep(0.3)
            prompt = s.recv(1024)
            print('Prompt:', repr(prompt))
        s.sendall(b'echo TELNET_ALIVE; ps\n')
        time.sleep(1.0)
        res = s.recv(4096)
        print('RES:\n', res.decode('latin1', errors='replace'))
    except Exception as e:
        print('Exception:', e)
    finally:
        s.close()

if __name__ == '__main__':
    test_telnet()
