import socket, time, sys, io, tarfile

def upload_tar(files_dict, camera_ip='10.10.11.155'):
    # Create tar.gz in memory
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode='w:gz') as tar:
        for arcname, filepath in files_dict.items():
            print(f"Adding {arcname} from {filepath}...")
            tar.add(filepath, arcname=arcname)
    tar_data = buf.getvalue()
    print(f"Total tar.gz size: {len(tar_data)} bytes")

    # Connect via telnet
    s = socket.socket()
    s.settimeout(5.0)
    s.connect((camera_ip, 23))
    time.sleep(0.3)
    try: s.recv(1024)
    except: pass

    # Execute command to receive tar stream
    cmd = "cd /var/tmp && tar -xzf -\n"
    s.sendall(cmd.encode())
    time.sleep(0.3)
    
    # Send tar data
    s.sendall(tar_data)
    time.sleep(0.5)
    s.close()

    time.sleep(1)
    # Verify with remote_exec
    from remote_exec import run_cmd
    print("Verification:")
    print(run_cmd("ls -la /var/tmp"))

if __name__ == '__main__':
    # Test uploading xm_flasher and custom-x.squashfs
    upload_tar({
        'xm_flasher': 'sd_card_installer/xm_flasher',
        'custom-x.squashfs': 'build_clean/custom-x.squashfs',
        'web-x.squashfs': 'build_clean/web-x.squashfs',
    })
