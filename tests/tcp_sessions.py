"""Real sockets exercise split/combined TCP frames, limits and disconnect cleanup."""
import json
import re
import socket
import struct
import subprocess
import sys
import time


def frame(message):
    body = json.dumps(message).encode()
    return struct.pack('!I', len(body)) + body


def exact(connection, count):
    result = b''
    while len(result) < count:
        chunk = connection.recv(count - len(result))
        if not chunk:
            raise EOFError('Connection closed')
        result += chunk
    return result


def receive(connection):
    size = struct.unpack('!I', exact(connection, 4))[0]
    assert 0 < size <= 16384
    return json.loads(exact(connection, size))


def message(kind, **values):
    return dict(version=1, type=kind, **values)


server = subprocess.Popen([sys.argv[1], '--port', '0'], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True)
connections = []
try:
    line = server.stdout.readline().strip()
    match = re.search(r':(\d+)$', line)
    assert match, line
    port = int(match.group(1))

    def connect():
        connection = socket.create_connection(('127.0.0.1', port), timeout=3)
        connection.settimeout(3)
        connections.append(connection)
        return connection

    def hello(connection, name):
        connection.sendall(frame(message('hello', name=name)))
        welcome = receive(connection)
        assert welcome['type'] == 'welcome' and len(welcome['sessionToken']) == 64
        return welcome

    first = connect()
    #send the header and body in several writes, never assuming write == read
    greeting = frame(message('hello', name='Player One'))
    for chunk in (greeting[:1], greeting[1:3], greeting[3:8], greeting[8:]):
        first.sendall(chunk)
        time.sleep(.005)
    one = receive(first)
    second = connect()
    two = hello(second, 'Player Two')
    assert one['playerId'] != two['playerId']
    assert one['sessionToken'] != two['sessionToken']

    first.sendall(frame(message('ping', requestId=7)) + frame(message('players')))
    pong, listing = receive(first), receive(first)
    assert pong['type'] == 'pong' and pong['requestId'] == 7
    assert {entry['name'] for entry in listing['players']} == {'Player One', 'Player Two'}
    probe = subprocess.run([sys.argv[2], '127.0.0.1', str(port), 'Cpp Client'],
                           capture_output=True, text=True, timeout=12)
    assert probe.returncode == 0, probe.stdout + probe.stderr
    assert '3 connected players' in probe.stdout, probe.stdout

    second.sendall(frame(message('goodbye')))
    assert receive(second)['type'] == 'goodbye'
    assert second.recv(1) == b''
    second.close()
    time.sleep(.05)
    first.sendall(frame(message('players')))
    listing = receive(first)
    assert len(listing['players']) == 1, listing

    #invalid frames must affect only the offending connection
    for bad in (message('hello', name=''), message('hello', name='x'*25),
                message('hello', name=42), message('ping'),
                dict(version=2, type='hello', name='Wrong version'),
                dict(version=1, type=42)):
        connection = connect()
        connection.sendall(frame(bad))
        assert receive(connection)['type'] == 'error'
        assert connection.recv(1) == b''
        connection.close()

    for body in (b'{', b'\xff', b'[]'):
        connection = connect()
        connection.sendall(struct.pack('!I', len(body)) + body)
        assert receive(connection)['type'] == 'error'
        connection.close()

    for length in (0, 16385, 0xffffffff):
        connection = connect()
        connection.sendall(struct.pack('!I', length))
        assert connection.recv(1) == b''
        connection.close()

    truncated = connect()
    truncated.sendall(struct.pack('!I', 100) + b'{')
    truncated.close()
    slow = connect()
    slow.sendall(b'\0')
    slow.settimeout(7)
    assert slow.recv(1) == b'', 'partial header should time out'
    slow.close()

    flood = connect()
    hello(flood, 'Flood test')
    flood.sendall(b''.join(frame(message('ping')) for _ in range(35)))
    rejected = False
    try:
        for _ in range(35):
            if receive(flood)['type'] == 'error':
                rejected = True
                break
    except (EOFError, ConnectionError):
        rejected = True
    assert rejected, 'rate limit'
    flood.close()

    first.sendall(frame(message('ping', requestId=9)))
    assert receive(first)['requestId'] == 9
    first.close()
    time.sleep(.05)

    pending = [connect() for _ in range(64)]
    time.sleep(.1)
    excess = connect()
    assert excess.recv(1) == b'', 'session count limit'
    for connection in pending:
        connection.close()
    time.sleep(.1)
    final = connect()
    hello(final, 'Still running')
    assert server.poll() is None
    print('TCP session checks passed: real C++ client, split/coalesced frames, cleanup, invalid input, timeouts and limits')
finally:
    for connection in connections:
        connection.close()
    server.terminate()
    try:
        server.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        server.kill()
        server.communicate()
