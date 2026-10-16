"""Raw UDP checks against authoritative state, not the client's predicted view."""
import json
import math
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
            part = sock.recv(count - len(data))
            if not part:
                raise EOFError()
            data += part
        return data
    return json.loads(exact(struct.unpack('!I', exact(4))[0]))


def snapshot(data):
    assert len(data) <= 1200 and data[:4] == b'G2\x02\x04'
    cursor = 44

    def read(fmt):
        nonlocal cursor
        result = struct.unpack_from('!' + fmt, data, cursor)
        cursor += struct.calcsize('!' + fmt)
        return result[0] if len(result) == 1 else result

    match, tick, ack, elapsed, remaining, finished, teams = read('IIIffBB')
    scores = read('4H')
    players = {}
    for _ in range(read('B')):
        slot, identity, team, x, y, angle, hp, ammo, shot_id, stamina, flags = read('BQBfffBBIfB')
        kills, deaths, reload, respawn, protection, recovery, cooldown = read('7H')
        players[identity] = dict(slot=slot, team=team, x=x, y=y, hp=hp, ammo=ammo,
                                 stamina=stamina, flags=flags, kills=kills, deaths=deaths)
    bullets = read('B')
    assert cursor + bullets * 24 == len(data)
    return dict(match=match, tick=tick, ack=ack, time=elapsed, remaining=remaining,
                finished=finished, players=players, bullets=bullets, scores=scores)


server = subprocess.Popen([sys.argv[1], '--port', '0', '--udp-port', '0'], stdout=subprocess.PIPE, text=True)
clients = []
try:
    port = int(re.search(r':(\d+)$', server.stdout.readline().strip())[1])

    class Client:
        def __init__(self, name):
            self.tcp = socket.create_connection(('127.0.0.1', port), timeout=3)
            clients.append(self)
            self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.udp.settimeout(2)
            welcome = self.control('hello', name=name)
            self.id = welcome['playerId']
            self.prefix = b'G2\x02' + bytes([1]) + struct.pack('!Q', self.id) + bytes.fromhex(welcome['sessionToken'])
            self.udp.connect(('127.0.0.1', welcome['gameplayPort']))
            self.shot = 0
            self.seq = 0
            self.match = 0

        def control(self, kind, **values):
            for other in clients:
                while select.select([other.tcp], [], [], .005)[0]:
                    receive(other.tcp)
            body = json.dumps(dict(version=1, type=kind, **values)).encode()
            self.tcp.sendall(struct.pack('!I', len(body)) + body)
            return receive(self.tcp)

        def register(self):
            self.udp.send(self.prefix)
            assert self.udp.recv(1200) == self.prefix[:3] + bytes([2]) + self.prefix[4:]

        def input(self, x=0, y=0, flags=0, aim=(0, 0), reload=0, sequence=None, sock=None, prefix=None):
            if sequence is None:
                self.seq += 1
                sequence = self.seq
            if flags & 2:
                self.shot += 1
            body = struct.pack('!IBIIIBBffB', self.match, 1, sequence, reload, self.shot, x + 1, y + 1, *aim, flags)
            packet = (prefix or self.prefix)[:3] + bytes([3]) + (prefix or self.prefix)[4:] + body
            (sock or self.udp).send(packet)
            return packet

        def state(self):
            while True:
                data = self.udp.recv(1201)
                if data[:4] == b'G2\x02\x04':
                    result = snapshot(data)
                    if select.select([self.udp], [], [], 0)[0]:
                        continue
                    return result

    a = Client('Host')
    b = Client('Guest')
    a.register()
    lobby = a.control('create_lobby', teams=2, minutes=1)
    b.control('join_lobby', code=lobby['code'])
    assert a.control('start_match')['type'] == 'notice', 'UDP registration required'
    b.register()
    started = a.control('start_match')
    assert started['playing']
    a.match = b.match = started['match']
    initial = a.state()
    assert len(initial['players']) == 2, 'only connected players spawn'
    outsider = Client('Other lobby')
    other_lobby = outsider.control('create_lobby', teams=3, minutes=2)
    assert other_lobby['code'] != started['code']
    assert len(a.state()['players']) == 2, 'another lobby leaked into this arena'
    start_x = initial['players'][a.id]['x']

    #Neither a wrong token nor another UDP endpoint can control a session.
    impostor = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    impostor.connect(a.udp.getpeername())
    impostor.settimeout(.15)
    impostor.send(a.prefix)
    try:
        impostor.recv(1200)
        raise AssertionError('existing endpoint was stolen')
    except socket.timeout:
        pass
    a.input(1, sock=impostor)
    a.input(1, prefix=a.prefix[:-1] + bytes([a.prefix[-1] ^ 1]))
    time.sleep(.15)
    assert abs(a.state()['players'][a.id]['x'] - start_x) < .0001, 'spoofed input moved player'

    #One command moves for a short time, then missing input must stop it.
    a.input(1)
    time.sleep(.42)
    stopped = a.state()
    assert .05 < stopped['players'][a.id]['x'] - start_x < .18
    a.input(1, sequence=a.seq)  #duplicate must not refresh the input timeout
    time.sleep(.15)
    assert abs(a.state()['players'][a.id]['x'] - stopped['players'][a.id]['x']) < .0001

    #Fast client packets cannot speed up the fixed server simulation.
    before = a.state()
    for _ in range(12):
        a.input(1)
    time.sleep(.10)
    after = a.state()
    distance = after['players'][a.id]['x'] - before['players'][a.id]['x']
    assert distance <= (after['time'] - before['time']) * .601 + .001, 'server tick owns movement speed'

    #Fire toward a moving target's current position: damage must wait for travel.
    while a.state()['time'] < 1.1:
        pass
    state = a.state()
    target = state['players'][b.id]
    health = target['hp']
    a.input(flags=3, aim=(target['x'], target['y']))
    fired = a.state()
    while fired['bullets'] == 0:
        fired = a.state()
    assert fired['players'][b.id]['hp'] == health, 'damage happened at fire time'
    hit = fired
    deadline = time.monotonic() + 2
    while hit['players'][b.id]['hp'] == health and time.monotonic() < deadline:
        hit = a.state()
    assert hit['players'][b.id]['hp'] < health, 'travelling bullet never hit'
    a.input()
    ammo = hit['players'][a.id]['ammo']
    assert ammo < 30
    #Repeat one reload counter; it must refill only once after the server timer.
    deadline = time.monotonic() + 1.7
    while time.monotonic() < deadline:
        a.input(reload=1)
        reloaded = a.state()
    assert reloaded['players'][a.id]['ammo'] == 30
    assert reloaded['time'] > initial['time'] and reloaded['remaining'] < initial['remaining']

    if '--duration' in sys.argv:
        deadline = time.monotonic() + 65
        heartbeat = time.monotonic()
        next_input = time.monotonic()
        ended = reloaded
        while not ended['finished'] and time.monotonic() < deadline:
            ended = a.state()
            if time.monotonic() >= next_input:
                a.input()
                b.input()
                next_input = time.monotonic() + .15
            if time.monotonic() - heartbeat > 8:
                for peer in (a, b, outsider):
                    assert peer.control('ping')['type'] == 'pong'
                heartbeat = time.monotonic()
        assert ended['finished'] and abs(ended['time'] - 60) < .001 and ended['remaining'] == 0
        result = receive(a.tcp)
        assert result['type'] == 'match_results' and result['match'] == ended['match']
        assert result['winningTeam'] == -1 and result['scores'] == [0, 0, 0, 0]
        assert len(result['players']) == 2, 'results include only this match'
        a.input(1, flags=3)
        time.sleep(.12)
        frozen = a.state()
        assert frozen['time'] == ended['time'] and frozen['players'][a.id] == ended['players'][a.id]
        assert b.control('return_lobby')['type'] == 'notice'
        returned = a.control('return_lobby')
        assert not returned['playing']
        restarted = a.control('start_match')
        assert restarted['match'] != a.match, 'new match needs a new packet identity'
        a.match = b.match = restarted['match']
        while a.state()['match'] != a.match:
            pass
        print('Full one-minute match passed: expiry, frozen gameplay, host return and restart')

    b.tcp.close()
    clients.remove(b)
    b.udp.close()
    time.sleep(.12)
    assert len(a.state()['players']) == 1, 'disconnect did not remove player'
    impostor.close()
    print('UDP checks passed: session binding, spoof rejection, tick speed, timeout, delayed damage, reload and removal')
finally:
    for client in clients:
        client.tcp.close()
        client.udp.close()
    server.terminate()
    server.wait(timeout=5)
