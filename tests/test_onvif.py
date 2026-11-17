import urllib.request
import xml.etree.ElementTree as ET

actions = [
    ("GetDeviceInformation", "http://10.10.11.155:8899/onvif/device_service", "http://www.onvif.org/ver10/device/wsdl"),
    ("GetSystemDateAndTime", "http://10.10.11.155:8899/onvif/device_service", "http://www.onvif.org/ver10/device/wsdl"),
    ("GetCapabilities", "http://10.10.11.155:8899/onvif/device_service", "http://www.onvif.org/ver10/device/wsdl"),
    ("GetServices", "http://10.10.11.155:8899/onvif/device_service", "http://www.onvif.org/ver10/device/wsdl"),
    ("GetProfiles", "http://10.10.11.155:8899/onvif/media_service", "http://www.onvif.org/ver10/media/wsdl"),
    ("GetStreamUri", "http://10.10.11.155:8899/onvif/media_service", "http://www.onvif.org/ver10/media/wsdl"),
    ("GetSnapshotUri", "http://10.10.11.155:8899/onvif/media_service", "http://www.onvif.org/ver10/media/wsdl"),
    ("GetVideoSources", "http://10.10.11.155:8899/onvif/media_service", "http://www.onvif.org/ver10/media/wsdl"),
    ("GetVideoSourceConfigurations", "http://10.10.11.155:8899/onvif/media_service", "http://www.onvif.org/ver10/media/wsdl"),
    ("GetVideoEncoderConfigurations", "http://10.10.11.155:8899/onvif/media_service", "http://www.onvif.org/ver10/media/wsdl"),
    ("GetVideoEncoderConfigurationOptions", "http://10.10.11.155:8899/onvif/media_service", "http://www.onvif.org/ver10/media/wsdl"),
    ("GetNodes", "http://10.10.11.155:8899/onvif/ptz_service", "http://www.onvif.org/ver20/ptz/wsdl"),
    ("GetConfigurations", "http://10.10.11.155:8899/onvif/ptz_service", "http://www.onvif.org/ver20/ptz/wsdl"),
    ("GetStatus", "http://10.10.11.155:8899/onvif/ptz_service", "http://www.onvif.org/ver20/ptz/wsdl"),
    ("GetPresets", "http://10.10.11.155:8899/onvif/ptz_service", "http://www.onvif.org/ver20/ptz/wsdl"),
    ("GetNetworkInterfaces", "http://10.10.11.155:8899/onvif/device_service", "http://www.onvif.org/ver10/device/wsdl"),
    ("GetScopes", "http://10.10.11.155:8899/onvif/device_service", "http://www.onvif.org/ver10/device/wsdl"),
]

all_ok = True
for name, url, ns in actions:
    soap = f'<?xml version="1.0" encoding="UTF-8"?><s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope"><s:Body><{name} xmlns="{ns}"/></s:Body></s:Envelope>'.encode()
    req = urllib.request.Request(url, data=soap, headers={'Content-Type': 'application/soap+xml'})
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            data = r.read().decode('utf-8')
            # Verify XML parsing
            ET.fromstring(data)
            print(f"[OK] {name:<35} -> HTTP {r.status}, XML valid ({len(data)} bytes)")
    except Exception as e:
        print(f"[FAIL] {name:<35} -> {e}")
        all_ok = False

if all_ok:
    print("\nALL ONVIF TESTS PASSED!")
