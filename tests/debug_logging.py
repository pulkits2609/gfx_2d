"""Exercise real disconnect paths; Debug reports them and Release stays quiet."""
from collections import deque
import json
from pathlib import Path
import queue
import re
import select
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
            chunk = sock.recv(count - len(data))
            if not chunk:
                raise EOFError('Control connection closed')
            data += chunk
        return data
    length, = struct.unpack('!I', exact(4))
    return json.loads(exact(length))


def send(sock, kind, **fields):
    body = json.dumps(dict(version=1, type=kind, **fields)).encode()
    sock.sendall(struct.pack('!I', len(body)) + body)


def until(sock, kind, predicate=lambda item: True):
    deadline = time.monotonic() + 4
    while time.monotonic() < deadline:
        item = receive(sock)
        if item['type'] == kind and predicate(item):
            return item
    raise AssertionError('Missing control event: ' + kind)


server = subprocess.Popen([sys.argv[1], '--port', '0', '--udp-port', '0', '--no-voice'],
    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
stdout, stderr = queue.Queue(), deque(maxlen=512)
output_reader = threading.Thread(target=lambda: [stdout.put(line) for line in server.stdout])
error_reader = threading.Thread(target=lambda: [stderr.append(line) for line in server.stderr])
output_reader.start(); error_reader.start()
clients, udp_sockets, tokens = [], [], []
try:
    startup = stdout.get(timeout=6)
    port = int(re.search(r':(\d+)$', startup.strip())[1])

    def connect(name):
        client = socket.create_connection(('127.0.0.1', port), timeout=3)
        client.settimeout(3)
        clients.append(client)
        send(client, 'hello', name=name)
        welcome = until(client, 'welcome')
        tokens.extend([welcome['sessionToken'], welcome['rejoinToken']])
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp_sockets.append(udp)
        udp.settimeout(3)
        udp.connect(('127.0.0.1', welcome['gameplayPort']))
        prefix = b'G2\x02\x01' + struct.pack('!Q', welcome['playerId']) + bytes.fromhex(welcome['sessionToken'])
        udp.send(prefix)
        assert udp.recv(1200) == prefix[:3] + b'\x02' + prefix[4:]
        return client, udp, prefix, welcome['playerId']

    host, host_udp, prefix, host_id = connect('Logging host')
    guest, guest_udp, guest_prefix, guest_id = connect('Silent guest')
    send(host, 'create_lobby', teams=2, minutes=3, public=True)
    room = until(host, 'lobby')
    send(guest, 'join_public', lobbyId=room['lobbyId'])
    until(guest, 'lobby')
    send(host, 'set_ready', ready=True)
    until(host, 'lobby', lambda item: item['members'][0]['ready'])
    send(guest, 'set_ready', ready=True)
    until(guest, 'lobby', lambda item: all(member['ready'] for member in item['members']))
    send(host, 'start_match')
    started = until(host, 'lobby', lambda item: item['playing'])
    #The host sends real inputs; the guest deliberately sends none. This must
    #produce a useful five-second input-timeout reason even with UDP keepalives.
    end = time.monotonic() + 6
    #A burst fills the bounded queue before the next tick can drain it.
    for sequence in range(1, 21):
        body = struct.pack('!IBIIIBBffB', started['match'], 1, sequence, 0, 0, 1, 1, 0, 0, 0)
        host_udp.send(prefix[:3] + b'\x03' + prefix[4:] + body)
    next_keepalive = 0
    guest_removed = False
    while time.monotonic() < end and not guest_removed:
        sequence += 1
        body = struct.pack('!IBIIIBBffB', started['match'], 1, sequence, 0, 0, 1, 1, 0, 0, 0)
        host_udp.send(prefix[:3] + b'\x03' + prefix[4:] + body)
        if time.monotonic() >= next_keepalive:
            guest_udp.send(guest_prefix)
            next_keepalive = time.monotonic() + 1
        if select.select([host], [], [], .03)[0]:
            item = receive(host)
            if item['type'] == 'lobby':
                guest_removed = any(member['playerId'] == guest_id and not member['connected'] for member in item['members'])
        time.sleep(.015)
    assert guest_removed, 'Silent guest was not disconnected by the real server timeout'
    send(host, 'leave_lobby')
    until(host, 'lobby_left')
    send(host, 'goodbye')
    until(host, 'goodbye')

    malformed = socket.create_connection(('127.0.0.1', port), timeout=3)
    clients.append(malformed)
    malformed.sendall(b'\0\0\0\0')
    assert malformed.recv(1) == b'', 'Malformed frame was accepted'
    host_udp.send(b'bad')
    time.sleep(.1)
finally:
    for client in clients:
        client.close()
    for udp in udp_sockets:
        udp.close()
    server.terminate()
    try:
        server.wait(timeout=4)
    except subprocess.TimeoutExpired:
        server.kill(); server.wait(timeout=3)
    output_reader.join(timeout=3); error_reader.join(timeout=3)
    server.stdout.close(); server.stderr.close()

logs = ''.join(stderr)
if sys.argv[2] == '1':
    for event in ('[CONNECT]', '[WELCOME]', '[UDP REGISTERED]', '[LOBBY CREATED]', '[LOBBY JOIN]',
                  '[MATCH START]', '[MATCH PLAYER]', '[FIRST SNAPSHOT]', '[FIRST INPUT]', '[GAMEPLAY]',
                  '[DISCONNECT]', '[LOBBY LEAVE]', '[LOBBY CLOSED]', '[UDP REJECT]'):
        assert event in logs, 'Missing debug event ' + event + '\n' + logs
    timeout = next(line for line in stderr if '[DISCONNECT]' in line and f'player={guest_id} ' in line)
    assert 'gameplay input timeout (5s)' in timeout and 'inputs=0 ' in timeout, timeout
    assert 'snapshot_errors=0' in timeout and 'last_udp_ms=' in timeout, timeout
    assert 'client sent goodbye' in logs and 'invalid TCP frame length: 0' in logs, logs
    overflow = next(line for line in stderr if '[UDP REJECT]' in line and 'input queue full' in line)
    for field in ('queue_depth=8', 'queue_limit=8', 'received_seq=', 'processed_seq='):
        assert field in overflow, overflow
    assert re.search(r'\[\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z\] \[GFX DEBUG\]', logs), logs
    assert all(token not in logs for token in tokens), 'An authentication/rejoin token was logged'
    print('Debug events, exact disconnect reasons and redaction checks passed')
    print('Example: ' + timeout.strip())
else:
    assert '[GFX DEBUG]' not in logs, 'Release printed debug diagnostics:\n' + logs
    assert b'[GFX DEBUG]' not in Path(sys.argv[1]).read_bytes(), 'Release retained debug logging text'
    print('Release completed the same network scenario without debug output')
