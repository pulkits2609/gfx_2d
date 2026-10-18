"""Two independent C++ client processes must see one another moving."""
import re
import json
import math
import subprocess
import sys

server = subprocess.Popen([sys.argv[1], '--port', '0', '--udp-port', '0', *sys.argv[3:]],
                          stdout=subprocess.PIPE, text=True)
processes = []
try:
    port = re.search(r':(\d+)$', server.stdout.readline().strip())[1]
    host = subprocess.Popen([sys.argv[2], port, 'host'], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    processes.append(host)
    line = host.stdout.readline().strip()
    assert line.startswith('CODE '), line
    guest = subprocess.Popen([sys.argv[2], port, 'join', line.split()[1]],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    processes.append(guest)
    states = []
    for process in processes:
        output, error = process.communicate(timeout=16)
        assert process.returncode == 0 and 'PASS ' in output, output + error
        if '--tick-rate' in sys.argv:
            rate = sys.argv[sys.argv.index('--tick-rate') + 1]
            assert f'ONLINE / {rate} TICK / PREDICTION' in output, 'Client did not use the server tick rate'
        states.append({entry['id']: entry for entry in json.loads(
            next(line[6:] for line in output.splitlines() if line.startswith('STATE ')))})
        print(output.strip())
    assert states[0].keys() == states[1].keys()
    for identity, first in states[0].items():
        second = states[1][identity]
        assert math.hypot(first['x'] - second['x'], first['y'] - second['y']) < .015, 'prediction failed to converge'
    assert server.poll() is None, 'server must run independently of both clients'
finally:
    for process in processes:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)
    server.terminate()
    server.wait(timeout=5)
