import urllib.request

def query(name, url, body):
    soap = f'''<?xml version="1.0" encoding="UTF-8"?>
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
  <s:Body>{body}</s:Body>
</s:Envelope>'''.encode('utf-8')
    req = urllib.request.Request(url, data=soap, headers={'Content-Type': 'application/soap+xml'})
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            res = r.read().decode('utf-8')
            print(f"=== {name} ===")
            print(res)
    except Exception as e:
        print(f"=== {name} ERROR: {e} ===")

query("GetProfiles", "http://10.10.11.155:8899/onvif/media_service", '<GetProfiles xmlns="http://www.onvif.org/ver10/media/wsdl"/>')
query("GetConfigurationOptions", "http://10.10.11.155:8899/onvif/ptz_service", '<GetConfigurationOptions xmlns="http://www.onvif.org/ver20/ptz/wsdl"><ConfigurationToken>PTZ_0</ConfigurationToken></GetConfigurationOptions>')
query("GetServiceCapabilities (PTZ)", "http://10.10.11.155:8899/onvif/ptz_service", '<GetServiceCapabilities xmlns="http://www.onvif.org/ver20/ptz/wsdl"/>')
query("GetServiceCapabilities (Media)", "http://10.10.11.155:8899/onvif/media_service", '<GetServiceCapabilities xmlns="http://www.onvif.org/ver10/media/wsdl"/>')
