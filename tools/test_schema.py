import urllib.request
import lxml.etree as ET

soap = '''<?xml version="1.0" encoding="UTF-8"?>
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
  <s:Body>
    <GetProfiles xmlns="http://www.onvif.org/ver10/media/wsdl"/>
  </s:Body>
</s:Envelope>'''.encode('utf-8')

req = urllib.request.Request('http://10.10.11.155:8899/onvif/media_service', data=soap, headers={'Content-Type': 'application/soap+xml'})
xml = urllib.request.urlopen(req).read()
doc = ET.fromstring(xml)
print(ET.tostring(doc, pretty_print=True).decode('utf-8'))
