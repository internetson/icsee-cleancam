import sys
sys.path.insert(0, 'tools')
from fast_tar_upload import upload_tar
from remote_exec import run_cmd

upload_tar({'light': 'build_clean/web-x/cgi-bin/light'})
print("--- Status ---")
print(run_cmd('chmod +x /var/tmp/light; QUERY_STRING="" /var/tmp/light'))

print("--- White Light ON ---")
print(run_cmd('QUERY_STRING="white=on" /var/tmp/light'))

print("--- White Light OFF ---")
print(run_cmd('QUERY_STRING="white=off" /var/tmp/light'))

print("--- IR ON ---")
print(run_cmd('QUERY_STRING="ir=on" /var/tmp/light'))

print("--- IR OFF ---")
print(run_cmd('QUERY_STRING="ir=off" /var/tmp/light'))

print("--- Auto Both ---")
print(run_cmd('QUERY_STRING="white=auto&ir=auto" /var/tmp/light'))
