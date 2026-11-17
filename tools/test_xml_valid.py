import xml.etree.ElementTree as ET

xml_text = '''<?xml version="1.0" encoding="UTF-8"?>
<SOAP-ENV:Envelope xmlns:SOAP-ENV="http://www.w3.org/2003/05/soap-envelope" xmlns:tt="http://www.onvif.org/ver10/schema" xmlns:trt="http://www.onvif.org/ver10/media/wsdl" xmlns:tptz="http://www.onvif.org/ver20/ptz/wsdl">
<SOAP-ENV:Body>
  <trt:GetProfilesResponse>
    <trt:Profiles token="Profile_0" fixed="true">
      <tt:Name>MainStream</tt:Name>
      <tt:VideoSourceConfiguration token="VSC_0">
        <tt:Name>VideoSourceConfig_0</tt:Name>
        <tt:UseCount>2</tt:UseCount>
        <tt:SourceToken>VideoSource_0</tt:SourceToken>
        <tt:Bounds x="0" y="0" width="1920" height="1080"/>
      </tt:VideoSourceConfiguration>
      <tt:VideoEncoderConfiguration token="VEC_0">
        <tt:Name>VideoEncoderConfig_0</tt:Name>
        <tt:UseCount>1</tt:UseCount>
        <tt:Encoding>H264</tt:Encoding>
        <tt:Resolution><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:Resolution>
        <tt:Quality>5.0</tt:Quality>
        <tt:RateControl><tt:FrameRateLimit>12</tt:FrameRateLimit><tt:EncodingInterval>1</tt:EncodingInterval><tt:BitrateLimit>2048</tt:BitrateLimit></tt:RateControl>
        <tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type><tt:IPv4Address>0.0.0.0</tt:IPv4Address></tt:Address><tt:Port>0</tt:Port><tt:TTL>1</tt:TTL><tt:AutoStart>false</tt:AutoStart></tt:Multicast>
        <tt:SessionTimeout>PT60S</tt:SessionTimeout>
      </tt:VideoEncoderConfiguration>
      <tt:PTZConfiguration token="PTZ_0">
        <tt:Name>PTZConfig_0</tt:Name>
        <tt:UseCount>2</tt:UseCount>
        <tt:NodeToken>Node_0</tt:NodeToken>
        <tt:DefaultAbsolutePantTiltPositionSpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:DefaultAbsolutePantTiltPositionSpace>
        <tt:DefaultAbsoluteZoomPositionSpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:DefaultAbsoluteZoomPositionSpace>
        <tt:DefaultRelativePanTiltTranslationSpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationGenericSpace</tt:DefaultRelativePanTiltTranslationSpace>
        <tt:DefaultRelativeZoomTranslationSpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/TranslationGenericSpace</tt:DefaultRelativeZoomTranslationSpace>
        <tt:DefaultContinuousPanTiltVelocitySpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace</tt:DefaultContinuousPanTiltVelocitySpace>
        <tt:DefaultContinuousZoomVelocitySpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace</tt:DefaultContinuousZoomVelocitySpace>
        <tt:DefaultPTZSpeed><tt:PanTilt x="1.0" y="1.0" space="http://www.onvif.org/ver10/tptz/PanTiltSpaces/GenericSpeedSpace"/><tt:Zoom x="1.0" space="http://www.onvif.org/ver10/tptz/ZoomSpaces/GenericSpeedSpace"/></tt:DefaultPTZSpeed>
        <tt:DefaultPTZTimeout>PT5S</tt:DefaultPTZTimeout>
      </tt:PTZConfiguration>
    </trt:Profiles>
    <trt:Profiles token="Profile_1" fixed="true">
      <tt:Name>SubStream</tt:Name>
      <tt:VideoSourceConfiguration token="VSC_1">
        <tt:Name>VideoSourceConfig_1</tt:Name>
        <tt:UseCount>2</tt:UseCount>
        <tt:SourceToken>VideoSource_0</tt:SourceToken>
        <tt:Bounds x="0" y="0" width="800" height="448"/>
      </tt:VideoSourceConfiguration>
      <tt:VideoEncoderConfiguration token="VEC_1">
        <tt:Name>VideoEncoderConfig_1</tt:Name>
        <tt:UseCount>1</tt:UseCount>
        <tt:Encoding>H264</tt:Encoding>
        <tt:Resolution><tt:Width>800</tt:Width><tt:Height>448</tt:Height></tt:Resolution>
        <tt:Quality>5.0</tt:Quality>
        <tt:RateControl><tt:FrameRateLimit>12</tt:FrameRateLimit><tt:EncodingInterval>1</tt:EncodingInterval><tt:BitrateLimit>1024</tt:BitrateLimit></tt:RateControl>
        <tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type><tt:IPv4Address>0.0.0.0</tt:IPv4Address></tt:Address><tt:Port>0</tt:Port><tt:TTL>1</tt:TTL><tt:AutoStart>false</tt:AutoStart></tt:Multicast>
        <tt:SessionTimeout>PT60S</tt:SessionTimeout>
      </tt:VideoEncoderConfiguration>
      <tt:PTZConfiguration token="PTZ_1">
        <tt:Name>PTZConfig_1</tt:Name>
        <tt:UseCount>2</tt:UseCount>
        <tt:NodeToken>Node_0</tt:NodeToken>
        <tt:DefaultAbsolutePantTiltPositionSpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:DefaultAbsolutePantTiltPositionSpace>
        <tt:DefaultAbsoluteZoomPositionSpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:DefaultAbsoluteZoomPositionSpace>
        <tt:DefaultRelativePanTiltTranslationSpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationGenericSpace</tt:DefaultRelativePanTiltTranslationSpace>
        <tt:DefaultRelativeZoomTranslationSpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/TranslationGenericSpace</tt:DefaultRelativeZoomTranslationSpace>
        <tt:DefaultContinuousPanTiltVelocitySpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace</tt:DefaultContinuousPanTiltVelocitySpace>
        <tt:DefaultContinuousZoomVelocitySpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace</tt:DefaultContinuousZoomVelocitySpace>
        <tt:DefaultPTZSpeed><tt:PanTilt x="1.0" y="1.0" space="http://www.onvif.org/ver10/tptz/PanTiltSpaces/GenericSpeedSpace"/><tt:Zoom x="1.0" space="http://www.onvif.org/ver10/tptz/ZoomSpaces/GenericSpeedSpace"/></tt:DefaultPTZSpeed>
        <tt:DefaultPTZTimeout>PT5S</tt:DefaultPTZTimeout>
      </tt:PTZConfiguration>
    </trt:Profiles>
  </trt:GetProfilesResponse>
</SOAP-ENV:Body>
</SOAP-ENV:Envelope>'''

root = ET.fromstring(xml_text)
print("SUCCESS: Valid XML!")
for p in root.findall('.//{http://www.onvif.org/ver10/media/wsdl}Profiles'):
    print(f"Profile: token={p.attrib.get('token')}, Name={p.find('{http://www.onvif.org/ver10/schema}Name').text}")
