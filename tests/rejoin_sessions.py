"""Real sessions exercise voluntary leave, connection loss and server-held rejoin tickets."""
import json
import re
import select
import socket
import struct
import subprocess
import sys
import time


def receive(sock):
    def exact(count):
        data = b''
        while len(data) < count:
            chunk = sock.recv(count - len(data))
            if not chunk:
                raise EOFError('Connection closed')
            data += chunk
        return data
    return json.loads(exact(struct.unpack('!I', exact(4))[0]))


server = subprocess.Popen([sys.argv[1], '--port', '0', '--udp-port', '0'], stdout=subprocess.PIPE, text=True)
clients = []
try:
    port = int(re.search(r':(\d+)$', server.stdout.readline().strip())[1])

    class Client:
        def __init__(self, name):
            self.tcp = socket.create_connection(('127.0.0.1', port), timeout=3)
            self.events = []
            self.match = self.sequence = 0
            self.active = False
            self.state = None
            self.closed = False
            clients.append(self)
            welcome = self.request('hello', expected=('welcome',), name=name)
            self.id = welcome['playerId']
            self.ticket = welcome['rejoinToken']
            self.header = b'G2\x02\x01' + struct.pack('!Q', self.id) + bytes.fromhex(welcome['sessionToken'])
            self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.udp.settimeout(2)
            self.udp.connect(('127.0.0.1', welcome['gameplayPort']))
            self.udp.send(self.header)
            assert self.udp.recv(1200) == self.header[:3] + bytes([2]) + self.header[4:]

        def request(self, kind, expected=('lobby', 'notice', 'rejoin_denied'), **values):
            for client in clients:
                if client.closed:
                    continue
                while select.select([client.tcp], [], [], .002)[0]:
                    client.events.append(receive(client.tcp))
            body = json.dumps(dict(version=1, type=kind, **values)).encode()
            self.tcp.sendall(struct.pack('!I', len(body)) + body)
            while True:
                reply = receive(self.tcp)
                self.events.append(reply)
                if reply['type'] in expected:
                    return reply

        def leave(self):
            reply = self.request('leave_lobby', expected=('lobby_left',))
            self.active = False
            return reply

        def close(self):
            self.closed = True
            self.tcp.close()
            self.udp.close()

    def pump(seconds):
        end = time.monotonic() + seconds
        next_input = next_ping = time.monotonic()
        while time.monotonic() < end:
            now = time.monotonic()
            for client in clients:
                if client.closed:
                    continue
                if client.active and now >= next_input:
                    client.sequence += 1
                    body = struct.pack('!IBIIIBBffB', client.match, 1, client.sequence, 0, 0, 1, 1, 0, 0, 0)
                    client.udp.send(client.header[:3] + bytes([3]) + client.header[4:] + body)
                while select.select([client.udp], [], [], 0)[0]:
                    data = client.udp.recv(1201)
                    if data[:4] != b'G2\x02\x04':
                        continue
                    match, tick, ack, elapsed, remaining = struct.unpack_from('!IIIff', data, 44)
                    players = {}
                    cursor = 75
                    for _ in range(data[74]):
                        fields = struct.unpack_from('!BQBfffBBIfB7H', data, cursor)
                        players[fields[1]] = dict(team=fields[2], hp=fields[6], kills=fields[11], deaths=fields[12],
                                                  respawn=fields[14], protection=fields[15])
                        cursor += 47
                    client.state = dict(match=match, time=elapsed, remaining=remaining, players=players)
                while select.select([client.tcp], [], [], 0)[0]:
                    client.events.append(receive(client.tcp))
                if now >= next_ping:
                    body = json.dumps(dict(version=1, type='ping')).encode()
                    client.tcp.sendall(struct.pack('!I', len(body)) + body)
            if now >= next_input:
                next_input = now + .1
            if now >= next_ping:
                next_ping = now + 8
            time.sleep(.005)

    def start(minutes=3):
        host, guest, teammate = Client('Host'), Client('Guest'), Client('Teammate')
        room = host.request('create_lobby', teams=2, minutes=minutes)
        for client in (guest, teammate):
            assert client.request('join_lobby', code=room['code'])['type'] == 'lobby'
        match = host.request('start_match')
        assert match['playing']
        for client in (host, guest, teammate):
            client.active = True
            client.match = match['match']
        pump(.2)
        return host, guest, teammate, room['code']

    host, guest, teammate, code = start()
    original_team = guest.state['players'][guest.id]['team']
    guest.leave()
    assert any(event['type'] == 'rejoin_available' for event in guest.events)
    pump(.12)
    assert guest.id not in host.state['players'], 'leaving player remained targetable'
    assert guest.request('rejoin_match', token='a' * 64)['type'] == 'rejoin_denied'
    restored = guest.request('rejoin_match', token=guest.ticket)
    assert restored['type'] == 'lobby'
    guest.active = True
    guest.sequence = 0
    pump(.15)
    player = guest.state['players'][guest.id]
    assert player['team'] == original_team and player['hp'] == 0 and player['respawn'] > 0
    pump(3.1)
    assert guest.state['players'][guest.id]['hp'] == 100 and guest.state['players'][guest.id]['protection'] > 0

    #A new TCP/UDP connection gets a new session ID but restores the old player/team.
    old_id, ticket = guest.id, guest.ticket
    guest.close()
    pump(.15)
    resumed = Client('Different name')
    assert resumed.id != old_id
    restored = resumed.request('rejoin_match', token=ticket)
    assert restored['type'] == 'lobby'
    member = next(member for member in restored['members'] if member['playerId'] == resumed.id)
    assert member['name'] == 'Guest' and member['team'] == original_team
    resumed.match, resumed.active = host.match, True
    pump(.15)
    assert resumed.id in host.state['players'] and old_id not in host.state['players']
    assert resumed.state['players'][resumed.id]['hp'] == 0, 'connection loss bypassed respawn'
    intruder = Client('Intruder')
    assert intruder.request('rejoin_match', token=ticket)['type'] == 'rejoin_denied', 'ticket replay'
    assert intruder.request('join_lobby', code=code)['type'] == 'notice', 'normal late join bypassed reservation'

    resumed.leave()
    host.leave()
    pump(.1)
    assert any(event['type'] == 'lobby_closed' for event in teammate.events), 'host leaving did not end match'
    assert resumed.request('rejoin_match', token=resumed.ticket)['type'] == 'rejoin_denied', 'host exit did not revoke ticket'
    teammate.active = False

    #A match configured for two minutes is already below the cutoff after it starts.
    h2, g2, t2, _ = start(2)
    g2.leave()
    assert not any(event['type'] == 'rejoin_available' for event in g2.events)
    assert g2.request('rejoin_match', token=g2.ticket)['type'] == 'rejoin_denied'
    h2.leave()
    t2.active = False

    if '--expiry' in sys.argv:
        h3, g3, t3, _ = start(4)
        g3.leave()
        pump(60.3)
        assert g3.request('rejoin_match', token=g3.ticket)['type'] == 'rejoin_denied', '60-second ticket did not expire'
        h3.leave()
        t3.active = False
        print('Real 60-second reservation expiry passed')
    print('Rejoin checks passed: leave, disconnect, team/name preservation, respawn, token replay, cutoff and host exit')
finally:
    for client in clients:
        if not client.closed:
            client.close()
    server.terminate()
    server.wait(timeout=5)
