"""Two real clients behind a router-like proxy: idle expiry and UDP port changes."""
from collections import deque
import json
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
            part = sock.recv(count - len(data))
            if not part:
                raise EOFError()
            data += part
        return data
    return json.loads(exact(struct.unpack('!I', exact(4))[0]))


def send(sock, message):
    body = json.dumps(message).encode()
    sock.sendall(struct.pack('!I', len(body)) + body)


class Mapping:
    def __init__(self, server_port, mode):
        self.front = self.open_socket()
        self.back = self.open_socket()
        self.old_ports = []
        self.server = ('127.0.0.1', server_port)
        self.mode = mode
        self.endpoint = None
        self.closed = False
        self.error = None
        self.last_client = self.first_snapshot = 0
        self.registrations = self.snapshots = self.inputs_after_change = 0
        self.changed = self.dropped_reply = False
        self.change = threading.Event()
        self.changed_event = threading.Event()
        self.thread = threading.Thread(target=self.run)
        self.thread.start()

    @staticmethod
    def open_socket():
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(('127.0.0.1', 0))
        return sock

    def remap(self):
        self.change.set()
        assert self.changed_event.wait(2), 'UDP proxy did not change its port'

    def run(self):
        try:
            while not self.closed:
                if self.change.is_set() or (self.mode == 'during_match' and self.first_snapshot
                        and time.monotonic() - self.first_snapshot > .3 and not self.changed):
                    #Keep the old socket bound, but blackhole its packets. Merely
                    #changing the port must not let clients silently take it over.
                    self.old_ports.append(self.back)
                    self.back = self.open_socket()
                    self.changed = True
                    self.change.clear()
                    self.changed_event.set()
                readable, _, _ = select.select([self.front, self.back], [], [], .02)
                for sock in readable:
                    try:
                        data, address = sock.recvfrom(2000)
                    except ConnectionResetError:
                        continue  #Windows reports ICMP when a test client exits
                    if sock is self.front:
                        self.endpoint = address
                        self.last_client = time.monotonic()
                        if data[:4] == b'G2\x02\x01':
                            self.registrations += 1
                        if self.changed and data[:4] == b'G2\x02\x03':
                            self.inputs_after_change += 1
                        self.back.sendto(data, self.server)
                    elif self.endpoint:
                        if time.monotonic() - self.last_client > 3:
                            continue  #outbound traffic is required to refresh NAT
                        if self.changed and not self.dropped_reply and data[:4] == b'G2\x02\x02':
                            self.dropped_reply = True
                            continue  #one lost registration reply must be harmless
                        if data[:4] == b'G2\x02\x04':
                            self.snapshots += 1
                            if not self.first_snapshot:
                                self.first_snapshot = time.monotonic()
                        self.front.sendto(data, self.endpoint)
        except (OSError, ValueError) as error:
            if not self.closed:
                self.error = error

    def close(self):
        self.closed = True
        self.thread.join(timeout=2)
        for sock in [self.front, self.back, *self.old_ports]:
            sock.close()


class Proxy:
    def __init__(self, port, mode):
        self.port, self.mode = port, mode
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen()
        self.listener.settimeout(.1)
        self.closed = False
        self.sockets, self.mappings, self.threads = [], [], []
        self.start_at = None
        self.thread = threading.Thread(target=self.accept)
        self.thread.start()

    def accept(self):
        while not self.closed:
            try:
                front, _ = self.listener.accept()
            except (OSError, socket.timeout):
                continue
            back = socket.create_connection(('127.0.0.1', self.port), timeout=3)
            back.settimeout(None)
            self.sockets.extend([front, back])
            for target, args in ((self.from_client, (front, back)), (self.from_server, (back, front))):
                thread = threading.Thread(target=target, args=args)
                self.threads.append(thread)
                thread.start()

    def from_client(self, front, back):
        try:
            while not self.closed:
                message = receive(front)
                if message['type'] == 'start_match':
                    if self.start_at is not None:
                        continue
                    self.start_at = time.monotonic() + 5.2
                    while not self.closed and time.monotonic() < self.start_at:
                        time.sleep(.02)
                send(back, message)
        except (OSError, EOFError, ValueError):
            pass

    def from_server(self, back, front):
        mapping = None
        try:
            while not self.closed:
                message = receive(back)
                if message['type'] == 'welcome':
                    mapping = Mapping(message['gameplayPort'], self.mode)
                    self.mappings.append(mapping)
                    message['gameplayPort'] = mapping.front.getsockname()[1]
                if (self.mode == 'match_start' and message['type'] == 'lobby'
                        and message['playing'] and not mapping.changed):
                    mapping.remap()
                send(front, message)
        except (OSError, EOFError, ValueError):
            pass

    def close(self):
        self.closed = True
        self.listener.close()
        self.thread.join(timeout=2)
        for sock in self.sockets:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            sock.close()
        for thread in self.threads:
            thread.join(timeout=2)
        for mapping in self.mappings:
            mapping.close()


def run(mode):
    server = subprocess.Popen([sys.argv[1], '--port', '0', '--udp-port', '0', '--no-voice'],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    actors, readers = [], []
    diagnostics = deque(maxlen=200)
    proxy = None

    def drain(stream):
        reader = threading.Thread(target=lambda: diagnostics.extend(iter(stream.readline, '')))
        reader.start()
        readers.append(reader)

    try:
        drain(server.stderr)
        port = int(re.search(r':(\d+)$', server.stdout.readline().strip())[1])
        drain(server.stdout)
        proxy = Proxy(port, mode)
        proxy_port = str(proxy.listener.getsockname()[1])
        host = subprocess.Popen([sys.argv[2], proxy_port, 'host'], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        actors.append(host)
        drain(host.stderr)
        lines = queue.Queue()
        reader = threading.Thread(target=lambda: [lines.put(line) for line in host.stdout])
        reader.start()
        readers.append(reader)
        code = re.match(r'CODE (\w+)', lines.get(timeout=5)).group(1)
        guest = subprocess.Popen([sys.argv[2], proxy_port, 'join', code], stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, text=True)
        actors.append(guest)
        drain(guest.stderr)
        guest.wait(timeout=14)
        guest_output = guest.stdout.read()
        host.wait(timeout=14)
        reader.join(timeout=2)
        host_output = ''.join(lines.queue)
        assert host.returncode == guest.returncode == 0, ''.join(diagnostics)
        assert 'PASS ' in host_output and 'PASS ' in guest_output, host_output + guest_output
        assert len(proxy.mappings) == 2
        for mapping in proxy.mappings:
            assert mapping.error is None, mapping.error
            assert mapping.registrations >= 3, 'No periodic lobby UDP keepalive'
            assert mapping.snapshots > 10, 'Snapshots did not reach the real client'
            if mode != 'idle':
                assert mapping.changed and mapping.dropped_reply
                assert mapping.inputs_after_change > 10, 'Inputs did not resume on the new UDP port'
        print(f'{mode}: both clients received snapshots and synchronized movement')
    finally:
        for actor in actors:
            if actor.poll() is None:
                actor.kill()
            actor.wait(timeout=3)
        if proxy:
            proxy.close()
        server.terminate()
        server.wait(timeout=4)
        for reader in readers:
            reader.join(timeout=2)
        for process in [server, *actors]:
            process.stdout.close()
            process.stderr.close()


for mode in ('idle', 'match_start', 'during_match'):
    run(mode)
