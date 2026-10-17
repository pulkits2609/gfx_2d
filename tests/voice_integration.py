"""Actual GFX/voice processes: ownership, binding, recovery, and failure isolation."""
import argparse
import json
from pathlib import Path
import queue
import re
import select
import socket
import struct
import subprocess
import threading
import time
import unittest


def wait_until(check, timeout=6):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = check()
        if result:
            return result
        time.sleep(.02)
    raise AssertionError('Timed out waiting for integration state')


def free_port():
    with socket.socket() as tcp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        tcp.bind(('127.0.0.1', 0))
        port = tcp.getsockname()[1]
        udp.bind(('127.0.0.1', port))
        return port


class Peer:
    def __init__(self, port, voice=False):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=3)
        self.socket.settimeout(None)
        self.voice = voice
        self.messages = queue.Queue()
        self.write_lock = threading.Lock()
        self.last = {}
        self.reader = threading.Thread(target=self.read)
        self.reader.start()

    def send(self, operation, **fields):
        message = dict(v=2, id=operation, op=operation, **fields) if self.voice else dict(version=1, type=operation, **fields)
        data = json.dumps(message).encode()
        with self.write_lock:
            self.socket.sendall(struct.pack('!I', len(data)) + data)

    def exact(self, size):
        data = b''
        while len(data) < size:
            part = self.socket.recv(size - len(data))
            if not part:
                raise EOFError()
            data += part
        return data

    def read(self):
        try:
            while True:
                size, = struct.unpack('!I', self.exact(4))
                if not 0 < size <= 65536:
                    raise ValueError('Invalid frame')
                message = json.loads(self.exact(size))
                if self.voice and message.get('event') == 'ping':
                    with self.write_lock:
                        data = json.dumps(dict(v=2, op='pong', nonce=message['data']['nonce'])).encode()
                        self.socket.sendall(struct.pack('!I', len(data)) + data)
                else:
                    self.messages.put(message)
        except (OSError, EOFError, ValueError):
            pass

    def receive(self, kind, predicate=lambda message: True, timeout=6):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            message = self.messages.get(timeout=max(.01, deadline - time.monotonic()))
            self.last[message.get('type', message.get('event', message.get('id')))] = message
            if message.get('type', message.get('event', message.get('id'))) == kind and predicate(message):
                return message
        raise AssertionError(f'Missing {kind}: {self.last}')

    def close(self):
        try:
            self.socket.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.socket.close()
        self.reader.join(timeout=3)
        if self.reader.is_alive():
            raise AssertionError('Peer reader did not stop')


class Proxy:
    """Only management uses this proxy; tests can interrupt that TCP connection."""
    def __init__(self, destination):
        self.destination = destination
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen()
        self.listener.settimeout(.1)
        self.port = self.listener.getsockname()[1]
        self.closed = False
        self.pause = False
        self.pairs = []
        self.workers = []
        self.worker = threading.Thread(target=self.run)
        self.worker.start()

    def run(self):
        while not self.closed:
            try:
                client, _ = self.listener.accept()
            except (socket.timeout, OSError):
                continue
            if self.pause:
                client.close()
                continue
            target = socket.create_connection(('127.0.0.1', self.destination), timeout=3)
            pair = (client, target)
            self.pairs.append(pair)
            worker = threading.Thread(target=self.relay, args=(pair,))
            self.workers.append(worker)
            worker.start()

    def relay(self, pair):
        try:
            while not self.closed:
                readable, _, _ = select.select(pair, [], [], .1)
                for source in readable:
                    data = source.recv(65536)
                    if not data:
                        return
                    pair[1 if source is pair[0] else 0].sendall(data)
        except (OSError, ValueError):
            pass
        finally:
            for sock in pair:
                sock.close()

    def cut(self):
        self.pause = True
        for pair in list(self.pairs):
            for sock in pair:
                try:
                    sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass

    def close(self):
        self.closed = True
        self.cut()
        self.listener.close()
        self.worker.join(timeout=3)
        for worker in self.workers:
            worker.join(timeout=3)


class VoiceIntegration(unittest.TestCase):
    def setUp(self):
        self.children, self.peers, self.udp = [], [], []
        self.addCleanup(self.cleanup)
        self.port = free_port()
        self.voice = subprocess.Popen([ARGS.voice_server, str(self.port)], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        self.children.append(self.voice)
        self.output = queue.Queue()
        self.voice_reader = threading.Thread(target=self.read_voice)
        self.voice_reader.start()
        self.response(lambda item: item.get('event') == 'server_ready')
        self.proxy = Proxy(self.port)
        self.addCleanup(self.proxy.close)
        self.start_gfx()

    def start_gfx(self):
        self.gfx = subprocess.Popen([ARGS.gfx_server, '--port', '0', '--udp-port', '0',
            '--voice-port', str(self.proxy.port)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.children.append(self.gfx)
        line = self.gfx.stdout.readline().strip()
        self.game_port = int(re.search(r':(\d+)$', line)[1])
        wait_until(lambda: self.operator('list')['data']['total_groups'] == 0)
        time.sleep(.25)  #Allow initial management hello/list reconciliation.

    def read_voice(self):
        for line in self.voice.stdout:
            try:
                self.output.put(json.loads(line))
            except ValueError:
                pass

    def response(self, predicate):
        deadline = time.monotonic() + 6
        while time.monotonic() < deadline:
            item = self.output.get(timeout=max(.01, deadline - time.monotonic()))
            if predicate(item):
                return item
        raise AssertionError('No voice operator response')

    def operator(self, command):
        self.voice.stdin.write(command + '\n')
        self.voice.stdin.flush()
        return self.response(lambda item: item.get('id') == 'operator')

    def player(self):
        peer = Peer(self.game_port)
        self.peers.append(peer)
        peer.send('hello', name='Player ' + str(len(self.peers)))
        peer.welcome = peer.receive('welcome')
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.append(udp)
        udp.settimeout(3)
        udp.connect(('127.0.0.1', peer.welcome['gameplayPort']))
        packet = b'G2\x02\x01' + struct.pack('!Q', peer.welcome['playerId']) + bytes.fromhex(peer.welcome['sessionToken'])
        udp.send(packet)
        self.assertEqual(udp.recv(1200), packet[:3] + b'\x02' + packet[4:])
        return peer

    def create(self, host, teams=2):
        host.send('create_lobby', teams=teams, minutes=3, public=True)
        return host.receive('lobby', lambda room: room['voice']['state'] in ('ready', 'unavailable')
            and (room['voice']['state'] == 'ready' or 'five voice groups' in room['voice']['reason']))

    def bind(self, player, room):
        config = room['voice']
        voice = Peer(self.port, voice=True)
        self.peers.append(voice)
        voice.send('hello', role='voice', name='voice', member_id=str(player.welcome['playerId']),
            server_instance=config['instance'], group_id=config['group'], channel_id=config['all'])
        hello = voice.receive('hello')
        self.assertTrue(hello['ok'], hello)
        voice.identity = hello['data']['session_id']
        player.send('voice_bind', session=voice.identity, instance=config['instance'], group=config['group'])
        joined = voice.receive('state')['data']
        self.assertEqual(joined['state'], 'joined')
        return voice

    def test_lifecycle_binding_and_operator_deletion(self):
        host, guest = self.player(), self.player()
        room = self.create(host, 4)
        original = room['voice']
        guest.send('join_public', lobbyId=room['lobbyId'])
        room = guest.receive('lobby')
        host_voice, guest_voice = self.bind(host, room), self.bind(guest, room)
        details = self.operator('show ' + str(original['group']))['data']
        self.assertEqual([p['team'] for p in details['participants']], [1, 2])
        guest.send('voice_transmit', group=original['group'], instance=original['instance'], channel='team')
        self.assertIn('until the match starts', guest.receive('voice_error')['message'])
        guest.send('choose_team', team=3)
        guest.receive('lobby', lambda item: item['members'][1]['team'] == 3)
        wait_until(lambda: self.operator('show ' + str(original['group']))['data']['participants'][1]['team'] == 4)
        host.send('set_ready', ready=True); guest.send('set_ready', ready=True)
        host.receive('lobby', lambda item: all(p['ready'] for p in item['members']))
        host.send('start_match')
        started = host.receive('lobby', lambda item: item['playing'])
        self.assertEqual(started['voice']['group'], original['group'])
        self.assertEqual(guest_voice.receive('state', lambda item: item['data']['phase'] == 'playing')['data']['transmit_channel_id'], original['teams'][3])
        #An operator deleting voice must leave the actual gameplay match running.
        self.operator('delete ' + str(original['group']))
        unavailable = guest.receive('lobby', lambda item: item['voice']['state'] == 'unavailable')
        self.assertTrue(unavailable['playing'])
        self.assertEqual(host_voice.receive('group_deleted')['data']['group_id'], original['group'])
        host.send('leave_lobby')
        guest.receive('lobby_closed')

    def test_five_groups_and_pending_reservations(self):
        hosts = [self.player() for _ in range(6)]
        for host in hosts:
            host.send('create_lobby', teams=2, minutes=3, public=True)
        rooms = []
        for host in hosts:
            rooms.append(host.receive('lobby', lambda item: item['voice']['state'] == 'ready'
                or 'five voice groups' in item['voice']['reason']))
        ready = [room for room in rooms if room['voice']['state'] == 'ready']
        self.assertEqual(len(ready), 5)
        self.assertEqual(self.operator('list')['data']['total_groups'], 5)
        hosts[0].send('leave_lobby'); hosts[0].receive('lobby_left')
        wait_until(lambda: self.operator('list')['data']['total_groups'] == 4)
        hosts[5].send('leave_lobby'); hosts[5].receive('lobby_left')
        self.assertEqual(self.create(hosts[5])['voice']['state'], 'ready')

    def test_manager_recovery_reconciles_departures_and_teams(self):
        host, guest = self.player(), self.player()
        room = self.create(host)
        guest.send('join_public', lobbyId=room['lobbyId']); room = guest.receive('lobby')
        host_voice, guest_voice = self.bind(host, room), self.bind(guest, room)
        self.proxy.cut()
        host.receive('lobby', lambda item: item['voice']['state'] == 'recovering')
        guest.send('leave_lobby'); guest.receive('lobby_left')
        host.send('choose_team', team=1)
        host.receive('lobby', lambda item: item['members'][0]['team'] == 1)
        self.proxy.pause = False
        recovered = host.receive('lobby', lambda item: item['voice']['state'] == 'ready')
        self.assertEqual(recovered['voice']['group'], room['voice']['group'])
        def reconciled():
            details = self.operator('show ' + str(room['voice']['group']))['data']
            return len(details['participants']) == 1 and details['participants'][0]['team'] == 2
        wait_until(reconciled)
        guest_voice.receive('membership_removed')
        host.send('leave_lobby'); host.receive('lobby_left')
        host_voice.receive('group_deleted')
        wait_until(lambda: self.operator('list')['data']['total_groups'] == 0)

    def test_restart_disables_existing_rooms_without_recreation(self):
        host = self.player()
        room = self.create(host)
        self.voice.stdin.write('shutdown\n'); self.voice.stdin.flush()
        self.voice.wait(timeout=6)
        self.voice_reader.join(timeout=3)
        replacement = subprocess.Popen([ARGS.voice_server, str(self.port)], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        self.children.append(replacement)
        self.voice = replacement
        self.output = queue.Queue()
        self.voice_reader = threading.Thread(target=self.read_voice); self.voice_reader.start()
        self.response(lambda item: item.get('event') == 'server_ready')
        unavailable = host.receive('lobby', lambda item: item['voice']['state'] == 'unavailable')
        self.assertEqual(unavailable['lobbyId'], room['lobbyId'])
        time.sleep(.5)
        self.assertEqual(self.operator('list')['data']['total_groups'], 0)
        host.send('leave_lobby'); host.receive('lobby_left')
        self.assertEqual(self.create(host)['voice']['state'], 'ready')

    def test_two_real_game_clients_with_background_voice(self):
        if not ARGS.client_driver or not ARGS.voice_client:
            self.skipTest('Client driver/runtime not supplied')
        clients = []
        for role in ('host', 'guest'):
            child = subprocess.Popen([ARGS.client_driver, str(self.game_port), role, ARGS.voice_client],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.children.append(child); clients.append(child)
        for child in clients:
            output, error = child.communicate(timeout=24)
            self.assertEqual(child.returncode, 0, output + error)
            self.assertIn('Voice client lifecycle passed', output)
        wait_until(lambda: self.operator('list')['data']['total_groups'] == 0)

    def cleanup(self):
        for peer in self.peers:
            peer.close()
        for udp in self.udp:
            udp.close()
        for child in reversed(self.children):
            if child.poll() is None:
                if child is self.voice:
                    try:
                        child.stdin.write('shutdown\n'); child.stdin.flush()
                        child.wait(timeout=6)
                    except (OSError, subprocess.TimeoutExpired):
                        child.kill(); child.wait(timeout=3)
                else:
                    child.kill(); child.wait(timeout=3)
            for stream in (child.stdin, child.stdout, child.stderr):
                if stream and not stream.closed:
                    stream.close()
        if hasattr(self, 'voice_reader'):
            self.voice_reader.join(timeout=3)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('gfx_server', type=lambda value: str(Path(value).resolve()))
    parser.add_argument('voice_server', type=lambda value: str(Path(value).resolve()))
    parser.add_argument('--client-driver', type=lambda value: str(Path(value).resolve()))
    parser.add_argument('--voice-client', type=lambda value: str(Path(value).resolve()))
    ARGS, other = parser.parse_known_args()
    unittest.main(argv=[__file__, *other])
