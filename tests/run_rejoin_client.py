import re
import subprocess
import sys

server = subprocess.Popen([sys.argv[1], '--port', '0', '--udp-port', '0'], stdout=subprocess.PIPE, text=True)
try:
    port = re.search(r':(\d+)$', server.stdout.readline().strip())[1]
    checked = subprocess.run([sys.argv[2], port], capture_output=True, text=True, timeout=25)
    assert checked.returncode == 0, checked.stdout + checked.stderr
    print(checked.stdout.strip())
finally:
    server.terminate()
    server.wait(timeout=5)
