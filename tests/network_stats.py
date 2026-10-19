"""Measure a delayed matching pong; ignore a wrong request ID."""
import json
import re
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
                raise EOFError('Probe disconnected early')
            data += part
        return data
    return json.loads(exact(struct.unpack('!I', exact(4))[0]))


def send(sock, kind, **fields):
    data = json.dumps(dict(version=1, type=kind, **fields)).encode()
    sock.sendall(struct.pack('!I', len(data)) + data)


errors = []
with socket.socket() as listener, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
    listener.bind(('127.0.0.1', 0))
    listener.listen()
    listener.settimeout(5)
    udp.bind(('127.0.0.1', 0))

    def serve():
        try:
            with listener.accept()[0] as client:
                client.settimeout(4)
                assert receive(client)['type'] == 'hello'
                send(client, 'welcome', playerId=1, sessionToken='a' * 64, rejoinToken='b' * 64,
                     gameplayPort=udp.getsockname()[1], tickRate=180)
                ping = receive(client)
                assert ping['type'] == 'ping'
                assert receive(client)['type'] == 'players'
                send(client, 'pong', requestId=ping['requestId'] + 1)
                send(client, 'players', players=[])
                #The wrong reply above must not complete the probe or record 0ms.
                time.sleep(.12)
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
        measured = float(re.search(r'Ping \(TCP RTT\): ([\d.]+) ms', result.stdout)[1])
        assert 100 <= measured < 2000, f'Incorrect measured latency: {measured}ms'
    finally:
        server.join(timeout=5)
    assert not server.is_alive() and not errors, errors
print('Measured TCP round-trip latency and mismatched-pong rejection passed')
