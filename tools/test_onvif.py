import urllib.request

def test_soap(action, body_tag):
    soap = f'''<?xml version="1.0" encoding="UTF-8"?>
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
  <s:Body>
    <{body_tag} xmlns="http://www.onvif.org/ver10/device/wsdl"/>
  </s:Body>
</s:Envelope>'''.encode('utf-8')
    req = urllib.request.Request(
        'http://10.10.11.155:8899/onvif/device_service',
        data=soap,
        headers={'Content-Type': 'application/soap+xml'}
    )
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.read().decode('utf-8')

print("--- GetDeviceInformation ---")
print(test_soap("GetDeviceInformation", "GetDeviceInformation"))
print("--- GetCapabilities ---")
print(test_soap("GetCapabilities", "GetCapabilities"))
print("--- GetProfiles ---")
print(test_soap("GetProfiles", "GetProfiles"))
print("--- GetStreamUri Profile_0 ---")
print(test_soap("GetStreamUri", "GetStreamUri"))

def test_stream_uri_sub():
    soap = '''<?xml version="1.0" encoding="UTF-8"?>
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
  <s:Body>
    <GetStreamUri xmlns="http://www.onvif.org/ver10/media/wsdl">
      <ProfileToken>Profile_1</ProfileToken>
    </GetStreamUri>
  </s:Body>
</s:Envelope>'''.encode('utf-8')
    req = urllib.request.Request(
        'http://10.10.11.155:8899/onvif/media_service',
        data=soap,
        headers={'Content-Type': 'application/soap+xml'}
    )
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.read().decode('utf-8')

print("--- GetStreamUri Profile_1 ---")
print(test_stream_uri_sub())

def test_ptz():
    soap = '''<?xml version="1.0" encoding="UTF-8"?>
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
  <s:Body>
    <ContinuousMove xmlns="http://www.onvif.org/ver20/ptz/wsdl">
      <Velocity>
        <PanTilt x="1.0" y="0.0"/>
      </Velocity>
    </ContinuousMove>
  </s:Body>
</s:Envelope>'''.encode('utf-8')
    req = urllib.request.Request(
        'http://10.10.11.155:8899/onvif/ptz_service',
        data=soap,
        headers={'Content-Type': 'application/soap+xml'}
    )
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.read().decode('utf-8')

print("--- ContinuousMove (PTZ) ---")
print(test_ptz())
