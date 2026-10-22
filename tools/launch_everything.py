"""Start the separate game, voice and ledger applications from one Windows package."""
import argparse
import ipaddress
import os
from pathlib import Path
import socket
import subprocess
import sys
import time
import urllib.request
import webbrowser

ROOT = Path(__file__).resolve().parents[1]


def number(text, maximum=65535):
    if not text.isascii() or not text.isdecimal() or not 1 <= int(text) <= maximum:
        raise ValueError('Enter a whole number from 1 to ' + str(maximum))
    return int(text)


def ask_number(label, default, maximum=65535):
    while True:
        try:
            return number(input(label + ' [' + str(default) + ']: ').strip() or str(default), maximum)
        except ValueError as error:
            print(error)


def check_ports(settings):
    endpoints = [('Game TCP', socket.SOCK_STREAM, settings.bind, settings.game_tcp),
                 ('Game UDP', socket.SOCK_DGRAM, settings.bind, settings.game_udp),
                 ('Voice TCP', socket.SOCK_STREAM, '0.0.0.0', settings.voice_port),
                 ('Voice UDP', socket.SOCK_DGRAM, '0.0.0.0', settings.voice_port),
                 ('Ledger TCP', socket.SOCK_STREAM, settings.ledger_bind, settings.ledger_port)]
    held = []
    try:
        for label, kind, address, port in endpoints:
            sock = socket.socket(socket.AF_INET, kind)
            held.append(sock)
            if os.name == 'nt':
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            try:
                sock.bind((address, port))
            except OSError as error:
                raise ValueError(label + ' cannot use ' + address + ':' + str(port) + ': ' + str(error))
    finally:
        for sock in held:
            sock.close()


def wait_for_service(process, port, address='127.0.0.1'):
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError('A server exited during startup. Check its console or smoke-test log.')
        try:
            with socket.create_connection((address, port), timeout=.3):
                return
        except OSError:
            time.sleep(.1)
    raise RuntimeError('Server did not start on port ' + str(port))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--defaults', action='store_true', help='Use flags/defaults without prompting')
    parser.add_argument('--bind', default='0.0.0.0')
    parser.add_argument('--game-tcp', type=number, default=9999)
    parser.add_argument('--game-udp', type=number, default=9999)
    parser.add_argument('--voice-port', type=number, default=9998)
    parser.add_argument('--ledger-port', type=number, default=8000)
    parser.add_argument('--ledger-bind', default='127.0.0.1')
    parser.add_argument('--public-host', default='', help='Public/LAN IP or hostname used by friends')
    parser.add_argument('--tick-rate', type=lambda text: number(text, 180), default=60)
    parser.add_argument('--debug-server', action='store_true')
    parser.add_argument('--no-client', action='store_true')
    parser.add_argument('--no-browser', action='store_true')
    parser.add_argument('--smoke', action='store_true', help='Check startup and HTTP, then stop only these test processes')
    settings = parser.parse_args()
    if not settings.defaults:
        print('Press Enter to keep a default. Game and voice services accept network connections.')
        settings.game_tcp = ask_number('Game TCP port', settings.game_tcp)
        settings.game_udp = ask_number('Game UDP port', settings.game_tcp)
        settings.voice_port = ask_number('Voice TCP + UDP port', settings.voice_port)
        settings.tick_rate = ask_number('Server ticks per second', settings.tick_rate, 180)
        settings.ledger_port = ask_number('Ledger website + API TCP port', settings.ledger_port)
        settings.ledger_bind = input('Ledger listening IP [127.0.0.1; use 0.0.0.0 for friends]: ').strip() or settings.ledger_bind
        settings.public_host = input('Your LAN/public IP or hostname [blank for local use]: ').strip()
        settings.debug_server = input('Use the Debug game server for detailed logs? [Y/n]: ').strip().lower() != 'n'
        settings.no_client = input('Also open the local game client? [Y/n]: ').strip().lower() == 'n'
    ipaddress.IPv4Address(settings.bind)
    ipaddress.IPv4Address(settings.ledger_bind)
    if settings.public_host and not all(c.isascii() and (c.isalnum() or c in '.-') for c in settings.public_host):
        raise ValueError('Public host must be an IPv4 address or hostname, without a URL or port')

    game = ROOT / 'game'
    ledger = ROOT / 'ledger'
    if not game.is_dir():
        game = ROOT / 'build' / 'udp-fix'
        ledger = ROOT.parent / 'blockchainLedger'
    server = game / 'gfx_server.exe'
    if settings.debug_server:
        server = game / 'gfx_server_debug.exe'
        if not server.exists():
            server = ROOT / 'build' / 'tick-debug' / 'gfx_server.exe'
    voice = game / 'voice' / 'voice_server.exe'
    for required in (server, voice, game / 'gfx.exe', ledger / 'server' / 'app.py', ledger / 'frontend' / 'dist' / 'index.html'):
        if not required.is_file():
            raise FileNotFoundError('Missing packaged file: ' + str(required))
    #Check the Python dependency before opening any server windows.
    import cryptography.fernet
    check_ports(settings)

    game_command = [str(server), '--bind', settings.bind, '--port', str(settings.game_tcp),
                    '--udp-port', str(settings.game_udp), '--tick-rate', str(settings.tick_rate),
                    '--voice-host', '127.0.0.1', '--voice-port', str(settings.voice_port)]
    if settings.public_host:
        game_command += ['--voice-public-host', settings.public_host]
    ledger_command = [sys.executable, str(ledger / 'server' / 'app.py'), '--bind', settings.ledger_bind,
                      '--port', str(settings.ledger_port)]
    if settings.public_host:
        ledger_command += ['--allow-host', settings.public_host]
    ledger_address = settings.ledger_bind
    if ledger_address == '0.0.0.0':
        ledger_address = '127.0.0.1'
    game_address = settings.bind
    if game_address == '0.0.0.0':
        game_address = '127.0.0.1'
    processes, logs = [], []
    completed = False
    try:
        commands = [('voice', [str(voice), str(settings.voice_port)], game / 'voice', settings.voice_port, '127.0.0.1'),
                    ('ledger', ledger_command, ledger, settings.ledger_port, ledger_address),
                    ('game', game_command, game, settings.game_tcp, game_address)]
        for name, command, folder, port, address in commands:
            options = {}
            if settings.smoke:
                log_folder = ROOT / 'logs'
                log_folder.mkdir(exist_ok=True)
                log = (log_folder / (name + '-smoke.log')).open('w')
                logs.append(log)
                options = dict(stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
                if os.name == 'nt':
                    options['creationflags'] = subprocess.CREATE_NO_WINDOW
            elif os.name == 'nt':
                options['creationflags'] = subprocess.CREATE_NEW_CONSOLE
            process = subprocess.Popen(command, cwd=folder, **options)
            processes.append(process)
            wait_for_service(process, port, address)
            print(name.title() + ' server ready on ' + str(port), flush=True)
        url = 'http://' + ledger_address + ':' + str(settings.ledger_port)
        with urllib.request.urlopen(url + '/api/ledger', timeout=5) as response:
            if response.status != 200:
                raise RuntimeError('Ledger API startup check failed')
        with urllib.request.urlopen(url + '/', timeout=5) as response:
            if b'<html' not in response.read().lower():
                raise RuntimeError('Ledger viewer was not served')
        print('\nLedger viewer: ' + url)
        print('GFX results upload: address ' + ledger_address + ', port ' + str(settings.ledger_port))
        print('Friends connect in GFX to your LAN/public IP and TCP port ' + str(settings.game_tcp))
        if not settings.smoke:
            if not settings.no_browser:
                webbrowser.open(url)
            if not settings.no_client:
                subprocess.Popen([str(game / 'gfx.exe')], cwd=game)
            print('\nServers run in their own windows. Close those windows when finished.')
            print('Ledger records and its private key stay inside ledger/server/data. Keep that folder.')
            input('Press Enter to close this launcher; the servers will keep running. ')
        else:
            print('All-in-one startup smoke test passed.')
        completed = True
    finally:
        #Never stop another server already running on this machine.
        if settings.smoke or not completed:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=8)
        for log in logs:
            log.close()


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, ImportError) as error:
        print('Startup failed: ' + str(error), file=sys.stderr)
        sys.exit(1)
