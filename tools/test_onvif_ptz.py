import urllib.request
import sys

soap = '''<?xml version="1.0" encoding="UTF-8"?>
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
  <s:Body>
    <GetStatus xmlns="http://www.onvif.org/ver20/ptz/wsdl">
      <ProfileToken>Profile_0</ProfileToken>
    </GetStatus>
  </s:Body>
</s:Envelope>'''.encode('utf-8')

req = urllib.request.Request(
    'http://10.10.11.155:8899/onvif/ptz_service',
    data=soap,
    headers={'Content-Type': 'application/soap+xml'}
)
with urllib.request.urlopen(req, timeout=5) as r:
    print('ONVIF GetStatus Response:\n', r.read().decode('utf-8'))
