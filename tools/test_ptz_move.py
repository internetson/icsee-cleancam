import urllib.request

def send_ptz(action, payload):
    soap = f"""<?xml version="1.0" encoding="utf-8"?>
<soap:Envelope xmlns:soap="http://www.w3.org/2003/05/soap-envelope" xmlns:tptz="http://www.onvif.org/ver20/ptz/wsdl" xmlns:tt="http://www.onvif.org/ver10/schema">
  <soap:Body>
    {payload}
  </soap:Body>
</soap:Envelope>"""
    req = urllib.request.Request(
        "http://10.10.11.155:8899/onvif/ptz_service",
        data=soap.encode("utf-8"),
        headers={"Content-Type": "application/soap+xml; charset=utf-8"}
    )
    with urllib.request.urlopen(req, timeout=5) as resp:
        return resp.status, resp.read().decode()

status, body = send_ptz("ContinuousMove", """
    <tptz:ContinuousMove>
      <tptz:ProfileToken>Profile_0</tptz:ProfileToken>
      <tptz:Velocity>
        <tt:PanTilt x="0.2" y="0.0"/>
      </tptz:Velocity>
    </tptz:ContinuousMove>
""")
print("ContinuousMove:", status, body[:200])

import time
time.sleep(0.5)

status, body = send_ptz("Stop", """
    <tptz:Stop>
      <tptz:ProfileToken>Profile_0</tptz:ProfileToken>
      <tptz:PanTilt>true</tptz:PanTilt>
      <tptz:Zoom>true</tptz:Zoom>
    </tptz:Stop>
""")
print("Stop:", status, body[:200])
