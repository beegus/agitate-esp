"""Development-server regression tests. Run: python3 -m unittest discover -s tests"""

from __future__ import annotations

import http.client
import json
import sys
import threading
import urllib.request
import unittest
from http.server import ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
import dev_server  # noqa: E402


class DevServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), dev_server.Handler)
        cls.port = cls.server.server_address[1]
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def post(self, path, body=None, origin=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        headers = {"Content-Type": "application/json"}
        if origin:
            headers["Origin"] = origin
        conn.request("POST", path, body=json.dumps(body) if body is not None else None, headers=headers)
        response = conn.getresponse()
        payload = json.loads(response.read() or b"{}")
        conn.close()
        return response.status, payload

    def test_cross_origin_post_rejected(self):
        status, _ = self.post("/api/stop", origin="https://evil.example")
        self.assertEqual(status, 403)

    def test_dns_rebinding_host_and_origin_rejected(self):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        conn.request(
            "POST",
            "/api/stop",
            headers={
                "Content-Type": "application/json",
                "Host": f"evil.example:{self.port}",
                "Origin": f"http://evil.example:{self.port}",
            },
        )
        response = conn.getresponse()
        payload = json.loads(response.read() or b"{}")
        conn.close()

        self.assertEqual(response.status, 403)
        self.assertIn("cross-origin", payload["error"])

    def test_same_origin_and_originless_posts_allowed(self):
        self.assertEqual(self.post("/api/stop", origin=f"http://127.0.0.1:{self.port}")[0], 200)
        self.assertEqual(self.post("/api/stop")[0], 200)

    def test_default_workflow_excludes_manual_wash_step(self):
        self.assertEqual(
            [step["name"] for step in dev_server.STEP_TEMPLATE],
            ["Pre-soak", "Developer", "Stop bath", "Fixer"],
        )

    def test_non_object_json_body_rejected(self):
        self.post("/api/stop")
        status, payload = self.post("/api/start", body=["not", "an", "object"])
        self.assertEqual(status, 400)
        self.assertIn("JSON object", payload["error"])

    def test_start_rejects_oversized_or_non_string_fields(self):
        status, payload = self.post("/api/start", body={"film": "x" * 65})
        self.assertEqual(status, 400)
        self.assertIn("64 characters", payload["error"])

        status, payload = self.post("/api/start", body={"developer": ["not", "text"]})
        self.assertEqual(status, 400)
        self.assertIn("64 characters", payload["error"])

    def test_agitation_schedule_matches_firmware_windows(self):
        developer = 1
        self.assertTrue(dev_server.agitation_wanted(developer, 9))
        self.assertFalse(dev_server.agitation_wanted(developer, 10))
        self.assertFalse(dev_server.agitation_wanted(developer, 59))
        self.assertTrue(dev_server.agitation_wanted(developer, 60))
        self.assertTrue(dev_server.agitation_wanted(developer, 64))
        self.assertFalse(dev_server.agitation_wanted(developer, 65))
        self.assertTrue(dev_server.agitation_wanted(2, 44))

    def test_wifi_setup_routes(self):
        status, payload = self.post("/api/wifi", body={"ssid": "home", "password": "short"})
        self.assertEqual(status, 400)
        status, _ = self.post("/api/wifi", body={"ssid": "home", "password": "longenough"})
        self.assertEqual(status, 202)
        status, _ = self.post("/api/ap-password", body={"password": "short"})
        self.assertEqual(status, 400)
        self.assertEqual(self.post("/api/wifi-forget")[0], 200)

        with urllib.request.urlopen(f"http://127.0.0.1:{self.port}/wifi", timeout=5) as response:
            self.assertIn(b"Home Wi-Fi setup", response.read())


if __name__ == "__main__":
    unittest.main()
