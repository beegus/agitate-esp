#!/usr/bin/env python3
"""
Local dev server that mimics the ESP32-C3 WebUI API for testing data/index.html
without flashing hardware. Serves data/ as static files and implements the
same /api/* routes as src/WebUI.cpp: a dynamic /profiles directory listing
(no film/developer/profile data is hardcoded here, same as firmware) plus
workflow, motor-control, and Wi-Fi setup routes. Wi-Fi changes are only held
in memory. No WebSocket support; the frontend falls back to polling
/api/status, which this server also implements.

Usage:
    python3 scripts/dev_server.py [--port 8080]
"""

from __future__ import annotations

import argparse
import http.server
import ipaddress
import json
import math
import time
from pathlib import Path
from urllib.parse import urlsplit

DATA_DIR = Path(__file__).resolve().parent.parent / "data"
PROFILES_DIR = DATA_DIR / "profiles"

# Mirrors the ProcessStep table in src/Workflow.cpp: duration, then initial
# agitation seconds, interval seconds, and agitation seconds per interval.
STEP_SCHEDULE = [
    ("Pre-soak", 60, 10, 30, 4),
    ("Developer", 600, 10, 60, 5),
    ("Stop bath", 45, 45, 0, 0),
    ("Fixer", 300, 10, 60, 5),
]
STEP_TEMPLATE = [{"name": name, "durationSec": duration} for name, duration, *_ in STEP_SCHEDULE]
DEFAULT_FIXER = "Ilford Rapid Fixer (1+4)"
MIN_DEVELOPER_SEC = 30
MAX_DEVELOPER_MINUTES = 24 * 60
MAX_BODY_BYTES = 64 * 1024
MAX_START_FIELD_LENGTH = 64
MAX_WIFI_SSID_LENGTH = 32
MAX_WIFI_PASSWORD_LENGTH = 63


def agitation_wanted(step_index: int, elapsed_sec: int) -> bool:
    """Same rule as Workflow::agitationWanted."""
    _, _, initial, interval, duration = STEP_SCHEDULE[step_index]
    if elapsed_sec < initial:
        return True
    if interval == 0 or duration == 0:
        return False
    return elapsed_sec % interval < duration


class State:
    def __init__(self) -> None:
        self.steps = [dict(s) for s in STEP_TEMPLATE]
        self.step_index = 0
        self.running = False
        self.paused = False
        self.waiting_for_next = False
        self.waiting_for_start = False
        self.step_start = 0.0
        self.pause_start = 0.0
        self.paused_accumulated = 0.0
        self.film = ""
        self.developer = ""
        self.fixer = ""
        self.dilution = "Custom"
        self.wifi_ssid = ""

    def is_step_active(self) -> bool:
        return self.running and not self.paused and not self.waiting_for_next and not self.waiting_for_start

    def elapsed_step_sec(self) -> int:
        if not self.running:
            return 0
        effective_paused = self.paused_accumulated
        if self.paused:
            effective_paused += time.time() - self.pause_start
        return max(0, int(time.time() - self.step_start - effective_paused))

    def remaining_step_sec(self) -> int:
        if self.waiting_for_start:
            return 0
        duration = self.steps[self.step_index]["durationSec"]
        return max(0, duration - self.elapsed_step_sec())

    def mark_step_done(self) -> None:
        if not self.running or self.paused or self.waiting_for_next or self.waiting_for_start:
            return
        if self.elapsed_step_sec() >= self.steps[self.step_index]["durationSec"]:
            self.waiting_for_next = True

    def status(self) -> dict:
        self.mark_step_done()
        return {
            "running": self.running,
            "paused": self.paused,
            "waitingForNext": self.waiting_for_next,
            "waitingForStart": self.waiting_for_start,
            "motorActive": self.is_step_active() and agitation_wanted(self.step_index, self.elapsed_step_sec()),
            "stepIndex": self.step_index,
            "stepName": self.steps[self.step_index]["name"] if self.running else "Idle",
            "stepRemainingSec": self.remaining_step_sec() if self.running else 0,
            "stepDurationSec": self.steps[self.step_index]["durationSec"],
            "film": self.film,
            "developer": self.developer,
            "fixer": self.fixer,
            "dilution": self.dilution,
            "steps": self.steps,
        }


state = State()


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(DATA_DIR), **kwargs)

    def log_message(self, fmt, *args):
        # WebSocket is not supported in dev server; suppress noisy 404 retry logs
        # We check both self.path and the args (which contains the request line)
        path = getattr(self, "path", "")
        if path == "/ws" or any("/ws" in str(a) for a in args):
            return
        print(f"[dev_server] {self.address_string()} - {fmt % args}")

    def end_headers(self):
        # Force revalidation on every request so edited profile JSON files are
        # never served stale from the browser's HTTP cache, matching firmware.
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

    def _send_json(self, status: int, payload: dict) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json_body(self) -> dict:
        try:
            length = int(self.headers.get("Content-Length", 0) or 0)
        except ValueError:
            raise ValueError("invalid Content-Length header") from None
        if length <= 0:
            return {}
        if length > MAX_BODY_BYTES:
            raise ValueError("request body too large")
        raw = self.rfile.read(length)
        data = json.loads(raw.decode("utf-8")) if raw else {}
        if not isinstance(data, dict):
            raise ValueError("expected a JSON object")
        return data

    def _is_cross_origin(self) -> bool:
        origin_header = self.headers.get("Origin")
        if origin_header is None:
            return False

        try:
            origin = urlsplit(origin_header)
            request_host = urlsplit(f"//{self.headers.get('Host', '')}")
            origin_port = origin.port or 80
            request_port = request_host.port or 80
        except ValueError:
            return True

        if (
            origin.scheme != "http"
            or not origin.hostname
            or origin.username is not None
            or origin.password is not None
            or origin.path
            or origin.query
            or origin.fragment
            or request_host.hostname is None
            or request_host.username is not None
            or request_host.password is not None
            or request_host.path
        ):
            return True

        local_host, local_port = self.connection.getsockname()[:2]
        allowed_hosts = {str(local_host).lower()}
        try:
            if ipaddress.ip_address(local_host).is_loopback:
                allowed_hosts.add("localhost")
        except ValueError:
            pass

        return (
            origin.hostname.lower() not in allowed_hosts
            or origin.hostname.lower() != request_host.hostname.lower()
            or origin_port != local_port
            or request_port != local_port
        )

    def do_GET(self):  # noqa: N802
        if self.path == "/ws":
            self.send_response(404)
            self.end_headers()
            return

        if self.path == "/api/profiles":
            filenames = sorted(p.name for p in PROFILES_DIR.glob("*.json")) if PROFILES_DIR.is_dir() else []
            self._send_json(200, {"profiles": filenames})
            return

        if self.path == "/api/status":
            self._send_json(200, state.status())
            return

        if self.path == "/api/network":
            self._send_json(
                200,
                {
                    "configured": bool(state.wifi_ssid),
                    "connected": False,
                    "ssid": "",
                    "ip": "",
                    "hostname": "agitate-esp.local",
                    "apIp": "192.168.4.1",
                    "apSsid": "agitate-esp-DEV000",
                },
            )
            return

        if self.path == "/wifi":
            self.path = "/wifi.html"

        super().do_GET()

    def do_POST(self):  # noqa: N802
        if self._is_cross_origin():
            self._send_json(403, {"error": "cross-origin request rejected"})
            return

        if self.path == "/api/start":
            if state.running:
                if state.waiting_for_start:
                    state.waiting_for_start = False
                    state.paused = False
                    state.step_start = time.time()
                    state.pause_start = 0.0
                    state.paused_accumulated = 0.0
                elif state.paused:
                    state.paused = False
                    state.paused_accumulated += time.time() - state.pause_start
                    state.pause_start = 0.0
                self._send_json(200, state.status())
                return

            try:
                body = self._read_json_body()
            except (ValueError, UnicodeDecodeError) as exc:
                self._send_json(400, {"error": f"invalid request body: {exc}"})
                return
            fields = {
                "film": body.get("film", ""),
                "developer": body.get("developer", ""),
                "fixer": body.get("fixer", DEFAULT_FIXER),
                "dilution": body.get("dilution", "Custom"),
            }
            if any(
                not isinstance(value, str) or len(value) > MAX_START_FIELD_LENGTH
                for value in fields.values()
            ):
                self._send_json(
                    400,
                    {"error": "process fields must be strings of at most 64 characters"},
                )
                return

            state.film = fields["film"]
            state.developer = fields["developer"]
            state.fixer = fields["fixer"]
            state.dilution = fields["dilution"]
            custom_minutes = body.get("customTimeMinutes", 10) or 10
            try:
                custom_minutes = float(custom_minutes)
            except (TypeError, ValueError):
                custom_minutes = 10.0
            if not math.isfinite(custom_minutes) or custom_minutes <= 0:
                custom_minutes = 10.0
            custom_minutes = min(custom_minutes, MAX_DEVELOPER_MINUTES)
            dev_seconds = max(MIN_DEVELOPER_SEC, int(custom_minutes * 60 + 0.5))
            state.steps = [dict(s) for s in STEP_TEMPLATE]
            state.steps[1]["durationSec"] = dev_seconds
            state.running = True
            state.paused = False
            state.waiting_for_next = False
            state.waiting_for_start = False
            state.step_index = 0
            state.step_start = time.time()
            state.pause_start = 0.0
            state.paused_accumulated = 0.0
            self._send_json(200, state.status())
            return

        if self.path == "/api/next":
            if state.running:
                if state.step_index + 1 >= len(state.steps):
                    state.running = False
                    state.step_index = 0
                    state.paused = False
                    state.waiting_for_next = False
                    state.waiting_for_start = False
                    state.pause_start = 0.0
                    state.paused_accumulated = 0.0
                else:
                    state.step_index += 1
                    state.step_start = time.time()
                    state.paused = False
                    state.pause_start = 0.0
                    state.waiting_for_next = False
                    state.waiting_for_start = True
                    state.paused_accumulated = 0.0
            self._send_json(200, state.status())
            return

        if self.path == "/api/restart_step":
            if state.running:
                state.step_start = time.time()
                state.paused = False
                state.waiting_for_next = False
                state.waiting_for_start = False
                state.pause_start = 0.0
                state.paused_accumulated = 0.0
            self._send_json(200, state.status())
            return

        if self.path == "/api/pause":
            if state.running and not state.waiting_for_next and not state.waiting_for_start:
                if not state.paused:
                    state.paused = True
                    state.pause_start = time.time()
                else:
                    state.paused = False
                    state.paused_accumulated += time.time() - state.pause_start
                    state.pause_start = 0.0
            self._send_json(200, state.status())
            return

        if self.path == "/api/stop":
            state.running = False
            state.paused = False
            state.waiting_for_next = False
            state.waiting_for_start = False
            state.step_index = 0
            state.pause_start = 0.0
            state.paused_accumulated = 0.0
            self._send_json(200, state.status())
            return

        if self.path == "/api/motor/test":
            if state.running:
                self._send_json(409, {"error": "stop the process before testing the motor"})
                return
            self._send_json(200, state.status())
            return

        if self.path in ("/api/wifi", "/api/ap-password"):
            try:
                body = self._read_json_body()
            except (ValueError, UnicodeDecodeError) as exc:
                self._send_json(400, {"error": f"invalid request body: {exc}"})
                return
            password = body.get("password", "")
            if self.path == "/api/ap-password":
                if not isinstance(password, str) or not 8 <= len(password) <= MAX_WIFI_PASSWORD_LENGTH:
                    self._send_json(400, {"error": "Access point password must be 8-63 bytes"})
                    return
                self._send_json(202, {"saved": True, "message": "Saved (dev server: not applied)"})
                return
            ssid = body.get("ssid", "")
            if (
                not isinstance(ssid, str)
                or not isinstance(password, str)
                or not 1 <= len(ssid) <= MAX_WIFI_SSID_LENGTH
                or len(password) > MAX_WIFI_PASSWORD_LENGTH
                or 0 < len(password) < 8
            ):
                self._send_json(400, {"error": "SSID must be 1-32 bytes; password must be blank or 8-63 bytes"})
                return
            state.wifi_ssid = ssid
            self._send_json(202, {"saved": True, "message": "Credentials saved (dev server: not connected)"})
            return

        if self.path == "/api/wifi-forget":
            state.wifi_ssid = ""
            self._send_json(200, {"message": "Home Wi-Fi forgotten; the access point stays on"})
            return

        if self.path == "/api/motor/aggressive":
            if not state.running or state.paused or state.waiting_for_next or state.waiting_for_start:
                self._send_json(409, {"error": "resume the active step before aggressive agitation"})
                return
            self._send_json(200, state.status())
            return

        self._send_json(404, {"ok": False, "error": "not found"})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--host", default="127.0.0.1", help="Use 0.0.0.0 to expose the dev server on your LAN.")
    args = parser.parse_args()

    server = http.server.ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"agitate-esp dev server: http://{args.host}:{args.port} (serving {DATA_DIR})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
