"""Lobby rules exercised through separate TCP connections to the real server."""
import json
import re
import select
import socket
import struct
import subprocess
import sys


def receive(sock):
    def exact(count):
        data = b''
        while len(data) < count:
            part = sock.recv(count - len(data))
            if not part:
                raise EOFError('Server disconnected')
            data += part
        return data
    return json.loads(exact(struct.unpack('!I', exact(4))[0]))


server = subprocess.Popen([sys.argv[1], '--port', '0'], stdout=subprocess.PIPE, text=True)
clients = []
try:
    port = int(re.search(r':(\d+)$', server.stdout.readline().strip())[1])

    def request(sock, kind, **values):
        #Drain broadcasts from previous commands before checking this reply.
        for other in clients:
            while select.select([other], [], [], .005)[0]:
                receive(other)
        body = json.dumps(dict(version=1, type=kind, **values)).encode()
        sock.sendall(struct.pack('!I', len(body)) + body)
        return receive(sock)

    def connect():
        sock = socket.create_connection(('127.0.0.1', port), timeout=3)
        clients.append(sock)
        welcome = request(sock, 'hello', name='Player ' + str(len(clients)))
        assert welcome['type'] == 'welcome'
        return sock, welcome['playerId']

    for teams, capacity in ((2, 4), (3, 3), (4, 2)):
        host, host_id = connect()
        assert request(host, 'create_lobby', teams=teams, minutes=0)['type'] == 'notice'
        lobby = request(host, 'create_lobby', teams=teams, minutes=5)
        code = lobby['code']
        assert len(code) == 6 and lobby['hostId'] == host_id
        assert request(host, 'start_match')['type'] == 'notice', 'no opponent'
        members = [host]
        for _ in range(teams * capacity - 1):
            player, _ = connect()
            joined = request(player, 'join_lobby', code=code.lower())
            assert joined['type'] == 'lobby'
            members.append(player)
        outsider, _ = connect()
        assert request(outsider, 'join_lobby', code='!!!!!!')['type'] == 'notice'
        assert request(outsider, 'join_lobby', code=code)['type'] == 'notice', 'full lobby'
        assert request(members[1], 'start_match')['type'] == 'notice', 'host only'
        assert request(members[1], 'choose_team', team=0)['type'] == 'notice', 'full team'
        started = request(host, 'start_match')
        assert started['playing']
        assert request(outsider, 'join_lobby', code=code)['type'] == 'notice', 'no late joins'
        assert request(host, 'choose_team', team=1)['type'] == 'notice', 'locked teams'
        assert request(host, 'leave_lobby')['type'] == 'lobby_left'
        assert request(outsider, 'join_lobby', code=code)['type'] == 'notice', 'host closes lobby'
        for sock in clients:
            sock.close()
        clients.clear()
    print('Lobby checks passed: codes, capacities, team choice, host start, join lock and host cleanup')
finally:
    for sock in clients:
        sock.close()
    server.terminate()
    server.wait(timeout=5)
