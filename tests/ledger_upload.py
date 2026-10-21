"""Exercise the real asynchronous HTTP adapter without graphics or a live ledger."""
import json
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

mode = "saved"
requests = []


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        requests.append(request)
        assert self.path == "/api/blocks"
        assert request["match_id"] == "gfx-result-test"
        assert request["data"]["durationSeconds"] == 180
        if mode == "timeout":
            time.sleep(9)
        if mode == "oversize":
            data = b"x" * 65537
        elif mode == "deep":
            data = b'{"nested":' + b"[" * 30 + b"0" + b"]" * 30 + b"}"
        elif mode == "malformed":
            data = b"not JSON"
        else:
            block = {"match_id": request["match_id"], "hash": "a" * 64}
            if mode == "wrong-id":
                block["match_id"] = "different-match"
            if mode == "bad-hash":
                block["hash"] = "not-a-sha256"
            data = json.dumps({"block": block, "duplicate": mode == "duplicate"}).encode()
            if mode == "conflict":
                data = json.dumps({"error": "match_id already has different data"}).encode()
        try:
            self.send_response(409 if mode == "conflict" else 201)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass


server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
server.daemon_threads = True
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    for mode in ("saved", "duplicate", "conflict", "malformed", "wrong-id", "bad-hash", "oversize", "deep", "timeout"):
        result = subprocess.run([sys.argv[1], str(server.server_port), mode],
                                capture_output=True, text=True, timeout=12)
        assert result.returncode == 0, result.stdout + result.stderr
        reply = json.loads(result.stdout)
        assert reply["saved"] == (mode in ("saved", "duplicate")), (mode, reply)
        if mode == "timeout":
            assert "timed out" in reply["message"] and reply["polls"] > 100, reply
        print("PASS", mode)
    assert requests[0] == requests[1], "Retries must preserve the result payload and ID"
    for mode in ("missing-id", "invalid-address", "cancel"):
        result = subprocess.run([sys.argv[1], str(server.server_port), mode],
                                capture_output=True, text=True, timeout=5)
        assert result.returncode == 0, result.stdout + result.stderr
        if mode != "cancel":
            assert not json.loads(result.stdout)["saved"]
        print("PASS", mode)
finally:
    server.shutdown()
    server.server_close()
    thread.join(timeout=2)
