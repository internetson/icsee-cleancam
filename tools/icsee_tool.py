#!/usr/bin/env python3
"""
ICSee / Xiongmai Camera Management & Control Suite
Supports PTZ, Image Quality, Night Mode, Telnet Unlock, and REST/Web Interface.
Communicates via native DVRIP protocol (Port 34567).
"""

import sys
import socket
import struct
import json
import hashlib
import time
import argparse
from http.server import HTTPServer, SimpleHTTPRequestHandler
import urllib.parse
import os

class DvripClient:
    HEAD = 0x5a
    VERSION = 0x00

    MSG_LOGIN_REQ = 1000
    MSG_LOGIN_RSP = 1001
    MSG_LOGOUT_REQ = 1002
    MSG_KEEP_ALIVE = 1006
    MSG_SYS_CONFIG_GET = 1040
    MSG_SYS_CONFIG_SET = 1042
    MSG_PTZ_CONTROL = 1400
    MSG_MONITOR_REQ = 1412
    MSG_TELNET_REQ = 1550
    MSG_WHITE_LIGHT = 1474

    def __init__(self, host, port=34567, user="admin", password=""):
        self.host = host
        self.port = int(port)
        self.user = user
        self.password = password
        self.sock = None
        self.session_id = 0
        self.seq = 0

    @staticmethod
    def sofia_hash(password=""):
        if not password:
            return ""
        md5 = hashlib.md5(password.encode("utf-8")).digest()
        chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
        return "".join([chars[sum(x) % 62] for x in zip(md5[::2], md5[1::2])])

    def connect(self, timeout=5):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect((self.host, self.port))

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None

    def send_packet(self, msg_id, payload_dict):
        self.seq += 1
        payload_bytes = json.dumps(payload_dict).encode("utf-8") + b"\n\x00"
        header = struct.pack(
            "<BB2sIIBBHI",
            self.HEAD,
            self.VERSION,
            b"\x00\x00",
            self.session_id,
            self.seq,
            0,
            0,
            msg_id,
            len(payload_bytes),
        )
        self.sock.sendall(header + payload_bytes)

    def recv_packet(self):
        header_data = b""
        while len(header_data) < 20:
            chunk = self.sock.recv(20 - len(header_data))
            if not chunk:
                raise ConnectionError("Connection closed while reading header")
            header_data += chunk

        head, ver, res, session_id, seq, total_pkt, cur_pkt, msg_id, length = struct.unpack(
            "<BB2sIIBBHI", header_data
        )
        if head != self.HEAD:
            raise ValueError(f"Invalid packet magic: {hex(head)}")

        payload_data = b""
        while len(payload_data) < length:
            chunk = self.sock.recv(min(length - len(payload_data), 4096))
            if not chunk:
                break
            payload_data += chunk

        try:
            payload_str = payload_data.decode("utf-8", errors="replace").strip("\x00\n\r ")
            payload_json = json.loads(payload_str)
        except Exception:
            payload_json = {"raw": payload_data.decode("latin1", errors="replace")}

        return (session_id, seq, msg_id, payload_json)

    def login(self):
        pwd_hash = self.sofia_hash(self.password)
        login_payload = {
            "EncryptType": "MD5",
            "LoginType": "DVRIP-Web",
            "PassWord": pwd_hash,
            "UserName": self.user,
        }
        self.send_packet(self.MSG_LOGIN_REQ, login_payload)
        session_id, seq, msg_id, rsp = self.recv_packet()
        ret = rsp.get("Ret", 0)
        if ret == 100:
            self.session_id = rsp.get("SessionID", session_id)
            return True, rsp
        return False, rsp

    def ptz_move(self, direction, step=4, channel=0):
        """
        Direction: DirectionUp, DirectionDown, DirectionLeft, DirectionRight,
                   DirectionUpLeft, DirectionUpRight, DirectionDownLeft, DirectionDownRight
        """
        payload = {
            "Name": "OPPTZControl",
            "OPPTZControl": {
                "Command": direction,
                "Parameter": {"Channel": channel, "Step": step},
            },
        }
        self.send_packet(self.MSG_PTZ_CONTROL, payload)
        return self.recv_packet()

    def ptz_stop(self, channel=0):
        """Stop PTZ motion"""
        payload = {
            "Name": "OPPTZControl",
            "OPPTZControl": {
                "Command": "DirectionStop",
                "Parameter": {"Channel": channel, "Step": 0},
            },
        }
        self.send_packet(self.MSG_PTZ_CONTROL, payload)
        return self.recv_packet()

    def ptz_step(self, direction, step=4, duration=0.25):
        """Move for a short duration, then automatically stop"""
        self.ptz_move(direction, step)
        time.sleep(duration)
        self.ptz_stop()

    def get_config(self, config_name):
        payload = {"Name": config_name, "SessionID": hex(self.session_id)}
        self.send_packet(self.MSG_SYS_CONFIG_GET, payload)
        return self.recv_packet()

    def set_config(self, config_name, config_data):
        payload = {
            "Name": config_name,
            "SessionID": hex(self.session_id),
            config_name: config_data,
        }
        self.send_packet(self.MSG_SYS_CONFIG_SET, payload)
        return self.recv_packet()

    def unlock_telnet(self):
        """Trigger telnet enablement on port 23"""
        payload = {"Name": "OPTelnet", "OPTelnet": {"Enable": True}}
        try:
            self.send_packet(self.MSG_TELNET_REQ, payload)
            return self.recv_packet()
        except Exception as e:
            return None, None, None, {"error": str(e)}

    def set_image_params(self, day_nf=3, night_nf=3, dnc_thr=30, day_night_color="0x00000003"):
        """
        DayNightColor options:
        0x00000000 = Color only
        0x00000001 = Black and White (IR only)
        0x00000002 = Auto (IR-Cut switch based on ambient light)
        0x00000003 = Smart / Dual Light (Color with white LED on motion)
        """
        param = [{
            "RejectFlicker": "0x00000000",
            "Day_nfLevel": int(day_nf),
            "Night_nfLevel": int(night_nf),
            "DncThr": int(dnc_thr),
            "DayNightColor": day_night_color,
        }]
        return self.set_config("Camera.Param", param)

    def set_white_light(self, mode="Auto", work_period=60):
        """
        Mode: Auto, Open (Always On), Close (Off)
        """
        payload = {
            "Name": "OPWhiteLight",
            "OPWhiteLight": {
                "WorkMode": mode,
                "WorkPeriod": work_period
            }
        }
        self.send_packet(self.MSG_WHITE_LIGHT, payload)
        return self.recv_packet()


def start_rest_and_web_server(camera_ip, port=34567, user="admin", password="", web_port=8080):
    client = DvripClient(camera_ip, port, user, password)
    
    class WebHandler(SimpleHTTPRequestHandler):
        def do_GET(self):
            parsed = urllib.parse.urlparse(self.path)
            if parsed.path == "/" or parsed.path == "/index.html":
                # Serve web_ui/index.html
                web_ui_file = os.path.join(os.path.dirname(__file__), "..", "web_ui", "index.html")
                if os.path.exists(web_ui_file):
                    self.send_response(200)
                    self.send_header("Content-Type", "text/html; charset=utf-8")
                    self.end_headers()
                    with open(web_ui_file, "rb") as f:
                        self.wfile.write(f.read())
                    return
                else:
                    self.send_response(404)
                    self.end_headers()
                    self.wfile.write(b"web_ui/index.html not found")
                    return
            elif parsed.path == "/api/status":
                self.send_json({"camera_ip": camera_ip, "status": "online", "port": port})
                return
            super().do_GET()

        def do_POST(self):
            parsed = urllib.parse.urlparse(self.path)
            content_length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(content_length)
            try:
                data = json.loads(body.decode("utf-8")) if body else {}
            except Exception:
                data = {}

            if parsed.path == "/api/ptz":
                direction = data.get("direction", "Stop")
                step = int(data.get("step", 4))
                duration = float(data.get("duration", 0.3))
                try:
                    c = DvripClient(camera_ip, port, user, password)
                    c.connect()
                    ok, _ = c.login()
                    if not ok:
                        self.send_json({"error": "Login failed"}, 401)
                        c.close()
                        return
                    if direction.lower() == "stop":
                        c.ptz_stop()
                    else:
                        c.ptz_step(direction, step=step, duration=duration)
                    c.close()
                    self.send_json({"status": "ok", "direction": direction})
                except Exception as e:
                    self.send_json({"error": str(e)}, 500)
                return

            elif parsed.path == "/api/image":
                mode = data.get("night_mode", "0x00000003")
                dnc_thr = int(data.get("dnc_thr", 30))
                try:
                    c = DvripClient(camera_ip, port, user, password)
                    c.connect()
                    ok, _ = c.login()
                    if not ok:
                        self.send_json({"error": "Login failed"}, 401)
                        c.close()
                        return
                    c.set_image_params(dnc_thr=dnc_thr, day_night_color=mode)
                    c.close()
                    self.send_json({"status": "ok", "night_mode": mode})
                except Exception as e:
                    self.send_json({"error": str(e)}, 500)
                return

            elif parsed.path == "/api/light":
                mode = data.get("mode", "Auto")
                try:
                    c = DvripClient(camera_ip, port, user, password)
                    c.connect()
                    ok, _ = c.login()
                    if not ok:
                        self.send_json({"error": "Login failed"}, 401)
                        c.close()
                        return
                    c.set_white_light(mode=mode)
                    c.close()
                    self.send_json({"status": "ok", "light_mode": mode})
                except Exception as e:
                    self.send_json({"error": str(e)}, 500)
                return

            self.send_response(404)
            self.end_headers()

        def send_json(self, obj, code=200):
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps(obj).encode("utf-8"))

    server = HTTPServer(("0.0.0.0", web_port), WebHandler)
    print(f"[*] Web Control Dashboard started at http://localhost:{web_port}")
    print(f"[*] Connected to camera at {camera_ip}:{port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping server...")
        server.server_close()


def main():
    parser = argparse.ArgumentParser(description="ICSee / Xiongmai DVRIP Camera Controller")
    parser.add_argument("--ip", required=True, help="Camera IP address")
    parser.add_argument("--port", default=34567, type=int, help="Camera NetIP port (default 34567)")
    parser.add_argument("--user", default="admin", help="Username (default: admin)")
    parser.add_argument("--password", default="", help="Password (default: empty)")
    
    sub = parser.add_subparsers(dest="command")

    # PTZ
    ptz_p = sub.add_parser("ptz", help="PTZ Command")
    ptz_p.add_argument("direction", choices=[
        "DirectionUp", "DirectionDown", "DirectionLeft", "DirectionRight",
        "DirectionUpLeft", "DirectionUpRight", "DirectionDownLeft", "DirectionDownRight",
        "Stop"
    ])
    ptz_p.add_argument("--step", type=int, default=4, help="Movement step / speed (1-8)")
    ptz_p.add_argument("--duration", type=float, default=0.3, help="Duration in seconds before stopping")

    # Image
    img_p = sub.add_parser("image", help="Camera image settings")
    img_p.add_argument("--mode", choices=["color", "bw", "auto", "dual"], default="dual",
                       help="Night vision mode: color, bw (IR), auto, dual (smart white light)")
    img_p.add_argument("--threshold", type=int, default=30, help="Day/Night switch threshold")

    # Telnet
    sub.add_parser("telnet", help="Unlock / enable Telnet on port 23")

    # Web UI server
    web_p = sub.add_parser("web", help="Start local modern Web UI dashboard")
    web_p.add_argument("--web-port", type=int, default=8080, help="Local HTTP port (default: 8080)")

    args = parser.parse_args()

    if args.command == "web":
        start_rest_and_web_server(args.ip, args.port, args.user, args.password, args.web_port)
        return

    client = DvripClient(args.ip, args.port, args.user, args.password)
    try:
        print(f"Connecting to {args.ip}:{args.port}...")
        client.connect()
        ok, rsp = client.login()
        if not ok:
            print(f"[-] Login failed: {rsp}")
            return 1
        print("[+] Logged in successfully!")

        if args.command == "ptz":
            if args.direction == "Stop":
                client.ptz_stop()
                print("[+] PTZ stopped")
            else:
                client.ptz_step(args.direction, step=args.step, duration=args.duration)
                print(f"[+] PTZ moved {args.direction} (step={args.step}, duration={args.duration}s)")

        elif args.command == "image":
            modes = {
                "color": "0x00000000",
                "bw": "0x00000001",
                "auto": "0x00000002",
                "dual": "0x00000003",
            }
            client.set_image_params(dnc_thr=args.threshold, day_night_color=modes[args.mode])
            print(f"[+] Image night mode set to: {args.mode}")

        elif args.command == "telnet":
            res = client.unlock_telnet()
            print(f"[+] Telnet unlock response: {res}")
            print("[*] Try connecting: telnet " + args.ip)

    except Exception as e:
        print(f"[-] Error: {e}")
        return 1
    finally:
        client.close()

if __name__ == "__main__":
    sys.exit(main() or 0)
