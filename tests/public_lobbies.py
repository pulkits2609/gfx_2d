"""Public discovery stays on the selected server; private codes stay private."""
import json
import re
import select
import socket
import struct
import subprocess
import sys


def receive(sock):
    def exact(size):
        data = b''
        while len(data) < size:
            part = sock.recv(size - len(data))
            if not part:
                raise EOFError('Server disconnected')
            data += part
        return data
    return json.loads(exact(struct.unpack('!I', exact(4))[0]))


servers, clients, udp = [], [], []
try:
    ports = []
    for _ in range(2):
        server = subprocess.Popen([sys.argv[1], '--port', '0', '--udp-port', '0'], stdout=subprocess.PIPE, text=True)
        servers.append(server)
        ports.append(int(re.search(r':(\d+)$', server.stdout.readline().strip())[1]))

    def request(sock, kind, **values):
        for other in clients:
            while select.select([other], [], [], .005)[0]:
                receive(other)
        body = json.dumps(dict(version=1, type=kind, **values)).encode()
        sock.sendall(struct.pack('!I', len(body)) + body)
        return receive(sock)

    def connect(server=0):
        sock = socket.create_connection(('127.0.0.1', ports[server]), timeout=3)
        clients.append(sock)
        welcome = request(sock, 'hello', name='Player ' + str(len(clients)))
        gameplay = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.append(gameplay)
        gameplay.settimeout(3)
        gameplay.connect(('127.0.0.1', welcome['gameplayPort']))
        packet = b'G2\x02\x01' + struct.pack('!Q', welcome['playerId']) + bytes.fromhex(welcome['sessionToken'])
        gameplay.send(packet)
        assert gameplay.recv(1200) == packet[:3] + b'\x02' + packet[4:]
        return sock

    host, private_host, browser, other_server = connect(), connect(), connect(), connect(1)
    assert request(host, 'create_lobby', teams=2, minutes=5, public='yes')['type'] == 'notice'
    private = request(private_host, 'create_lobby', teams=3, minutes=5)
    room = request(host, 'create_lobby', teams=2, minutes=5, public=True)
    assert room['public'] and room['code'] == '', 'public lobby generated a join code'
    listing = request(browser, 'list_lobbies')['lobbies']
    assert len(listing) == 1 and listing[0]['lobbyId'] == room['lobbyId']
    assert 'code' not in listing[0] and private['code'] not in json.dumps(listing)
    assert request(other_server, 'list_lobbies')['lobbies'] == [], 'listing crossed servers'
    assert request(other_server, 'join_public', lobbyId=room['lobbyId'])['type'] == 'notice'
    assert request(browser, 'join_public', lobbyId=private['lobbyId'])['type'] == 'notice', 'private ID bypassed code'
    assert request(browser, 'join_lobby', code='')['type'] == 'notice'
    assert request(browser, 'join_public', lobbyId=-1)['type'] == 'notice'
    assert request(browser, 'join_public', lobbyId=room['lobbyId'])['type'] == 'lobby'
    assert request(browser, 'leave_lobby')['type'] == 'lobby_left'
    assert request(browser, 'list_lobbies')['lobbies'][0]['players'] == 1
    assert request(browser, 'join_public', lobbyId=room['lobbyId'])['type'] == 'lobby'
    extra = [connect() for _ in range(7)]
    for player in extra[:6]:
        assert request(player, 'join_public', lobbyId=room['lobbyId'])['type'] == 'lobby'
    assert request(extra[-1], 'list_lobbies')['lobbies'][0]['players'] == 8
    assert request(extra[-1], 'join_public', lobbyId=room['lobbyId'])['type'] == 'notice', 'full public lobby'
    for player in extra[:6]:
        assert request(player, 'leave_lobby')['type'] == 'lobby_left'
    assert request(host, 'start_match')['playing']
    assert request(private_host, 'list_lobbies')['lobbies'] == [], 'running match was listed'
    assert request(private_host, 'leave_lobby')['type'] == 'lobby_left'
    assert request(private_host, 'join_public', lobbyId=room['lobbyId'])['type'] == 'notice', 'late public join'
    offer = request(browser, 'leave_lobby')
    assert offer['type'] == 'rejoin_available' and offer['code'] == ''
    #The server sends the offer before the leave acknowledgement.
    assert receive(browser)['type'] == 'lobby_left'
    assert request(browser, 'rejoin_match', token=offer['token'])['type'] == 'lobby', 'public rejoin without a code'
    assert request(host, 'leave_lobby')['type'] == 'lobby_left'
    assert request(private_host, 'list_lobbies')['lobbies'] == []
    assert request(private_host, 'join_public', lobbyId=room['lobbyId'])['type'] == 'notice', 'closed public lobby'
    print('Public lobby checks passed: server isolation, no codes, privacy, membership and join locks')
finally:
    for sock in clients + udp:
        sock.close()
    for server in servers:
        server.terminate()
        server.wait(timeout=5)
