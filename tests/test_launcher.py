"""Launcher input and port checks must fail before starting any processes."""
import argparse
from pathlib import Path
import socket
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from launch_everything import check_ports, number


class LauncherTests(unittest.TestCase):
    def settings(self):
        sockets = []
        try:
            for _ in range(4):
                sock = socket.socket()
                sock.bind(('127.0.0.1', 0))
                sockets.append(sock)
            ports = [sock.getsockname()[1] for sock in sockets]
        finally:
            for sock in sockets:
                sock.close()
        return argparse.Namespace(bind='127.0.0.1', ledger_bind='127.0.0.1',
                                  game_tcp=ports[0], game_udp=ports[0],
                                  voice_port=ports[1], ledger_port=ports[2])

    def test_numeric_limits(self):
        self.assertEqual(number('65535'), 65535)
        self.assertEqual(number('180', 180), 180)
        for value in ('0', '-1', '65536', 'hello', '1.5', '１２'):
            with self.assertRaises(ValueError):
                number(value)
        with self.assertRaises(ValueError):
            number('181', 180)

    def test_game_can_share_a_number_between_tcp_and_udp(self):
        check_ports(self.settings())

    def test_duplicate_tcp_is_rejected(self):
        settings = self.settings()
        settings.ledger_port = settings.game_tcp
        with self.assertRaisesRegex(ValueError, 'Ledger TCP'):
            check_ports(settings)

    def test_duplicate_udp_is_rejected(self):
        settings = self.settings()
        settings.voice_port = settings.game_udp
        with self.assertRaises(ValueError):
            check_ports(settings)

    def test_existing_listener_is_left_alone(self):
        settings = self.settings()
        with socket.socket() as existing:
            existing.bind(('127.0.0.1', 0))
            existing.listen()
            settings.game_tcp = existing.getsockname()[1]
            with self.assertRaisesRegex(ValueError, 'Game TCP'):
                check_ports(settings)
            self.assertGreater(existing.fileno(), -1)


if __name__ == '__main__':
    unittest.main()
