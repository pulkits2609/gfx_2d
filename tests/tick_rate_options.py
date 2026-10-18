"""Reject invalid tick settings and advertise the chosen rate to clients."""
from collections import deque
import json
import re
import socket
import struct
import subprocess
import sys
import threading

for value in ('0', '181', '-1', '60.5', 'abc', '4294967296'):
    result = subprocess.run([sys.argv[1], '--tick-rate', value], capture_output=True, text=True, timeout=4)
    assert result.returncode != 0 and 'Tick rate must be 1-180' in result.stderr, result.stderr

for rate in (None, 1, 180):
    args = [] if rate is None else ['--tick-rate', str(rate)]
    server = subprocess.Popen([sys.argv[1], '--port', '0', '--udp-port', '0', '--no-voice', *args],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    logs = deque(maxlen=40)
    reader = threading.Thread(target=lambda: logs.extend(iter(server.stderr.readline, '')))
    reader.start()
    try:
        port = int(re.search(r':(\d+)$', server.stdout.readline().strip())[1])
        with socket.create_connection(('127.0.0.1', port), timeout=3) as client:
            body = json.dumps(dict(version=1, type='hello', name='Tick test')).encode()
            client.sendall(struct.pack('!I', len(body)) + body)

            def exact(count):
                data = b''
                while len(data) < count:
                    part = client.recv(count - len(data))
                    assert part, 'Server closed before welcome'
                    data += part
                return data

            welcome = json.loads(exact(struct.unpack('!I', exact(4))[0]))
            assert welcome['tickRate'] == (30 if rate is None else rate), welcome
    finally:
        server.terminate()
        server.wait(timeout=4)
        reader.join(timeout=2)
        server.stdout.close()
        server.stderr.close()
print('Tick rate limits, default and welcome negotiation passed')
