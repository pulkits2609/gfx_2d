"""Run offscreen menu/HUD checks against an independent game server."""
import re
import subprocess
import sys

server = subprocess.Popen([sys.argv[1], '--port', '0', '--udp-port', '0', '--no-voice'],
                          stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
try:
    port = re.search(r':(\d+)$', server.stdout.readline().strip())[1]
    result = subprocess.run([sys.argv[2], port], capture_output=True, text=True, timeout=25)
    assert result.returncode == 0, result.stdout + result.stderr
    print(result.stdout.strip())
finally:
    server.terminate()
    server.wait(timeout=5)
    server.stdout.close()
