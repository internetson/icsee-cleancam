import urllib.request
soap = b'''<?xml version="1.0" encoding="UTF-8"?>
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
  <s:Body>
    <GetCapabilities xmlns="http://www.onvif.org/ver10/device/wsdl"/>
  </s:Body>
</s:Envelope>'''
req = urllib.request.Request('http://10.10.11.155:8899/onvif/device_service', data=soap, headers={'Content-Type': 'application/soap+xml'})
with urllib.request.urlopen(req, timeout=5) as r:
    print(r.read().decode('utf-8'))
