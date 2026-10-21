"""An unavailable loopback UDP port produces useful, bounded Debug diagnostics."""
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading
import time


def receive(sock):
    def exact(count):
        data = b''
        while len(data) < count:
            part = sock.recv(count - len(data))
            if not part:
                raise EOFError('Client disconnected early')
            data += part
        return data
    return json.loads(exact(struct.unpack('!I', exact(4))[0]))


def send(sock, kind, **fields):
    body = json.dumps(dict(version=1, type=kind, **fields)).encode()
    sock.sendall(struct.pack('!I', len(body)) + body)


errors = []
with socket.socket() as listener, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
    listener.bind(('127.0.0.1', 0))
    listener.listen()
    listener.settimeout(5)
    udp.bind(('127.0.0.1', 0))
    closed_port = udp.getsockname()[1]
    udp.close()  #The OS returns an ICMP error to the connected gameplay socket.

    def serve():
        try:
            with listener.accept()[0] as client:
                client.settimeout(5)
                assert receive(client)['type'] == 'hello'
                send(client, 'welcome', playerId=1, sessionToken='a' * 64, rejoinToken='b' * 64,
                     gameplayPort=closed_port)
                ping = receive(client)
                assert ping['type'] == 'ping'
                assert receive(client)['type'] == 'players'
                #Several registration attempts should produce only one throttled warning.
                time.sleep(1.2)
                send(client, 'players', players=[])
                send(client, 'pong', requestId=ping['requestId'])
                assert client.recv(1) == b''
        except Exception as error:
            errors.append(error)

    server = threading.Thread(target=serve)
    server.start()
    try:
        result = subprocess.run([sys.argv[1], '127.0.0.1', str(listener.getsockname()[1])],
                                capture_output=True, text=True, timeout=8)
        assert result.returncode == 0, result.stdout + result.stderr
    finally:
        server.join(timeout=6)
    assert not server.is_alive() and not errors, errors

if sys.argv[2] == '1':
    warnings = [line for line in result.stderr.splitlines() if '[CLIENT UDP ERROR]' in line]
    assert len(warnings) == 1, result.stderr
    assert 'operation=' in warnings[0] and 'code=' in warnings[0] and 'reason=' in warnings[0]
    assert 'a' * 64 not in result.stderr and 'b' * 64 not in result.stderr
    print('Client UDP errors are logged once during repeated failures, without session tokens')
else:
    assert '[GFX DEBUG]' not in result.stderr, result.stderr
    assert b'[CLIENT UDP ERROR]' not in Path(sys.argv[1]).read_bytes()
    print('Release client keeps UDP diagnostics compiled out')
