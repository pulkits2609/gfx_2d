"""Assemble the tested Windows executables, standalone ledger and portable Python."""
from datetime import datetime
from hashlib import sha256
import json
from pathlib import Path
import shutil
import subprocess
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
LEDGER = ROOT.parent / 'blockchainLedger'
CACHE = BUILD / 'all-in-one-runtime-cache'


def main():
    stage = BUILD / ('all-in-one-stage-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
    package = stage / 'GFX_2D_All_In_One'
    game = package / 'game'
    game.mkdir(parents=True)
    binaries = BUILD / 'udp-fix'
    for name in ('gfx.exe', 'gfx_server.exe'):
        shutil.copy2(binaries / name, game / name)
    shutil.copy2(BUILD / 'tick-debug' / 'gfx_server.exe', game / 'gfx_server_debug.exe')
    for name in ('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll'):
        shutil.copy2(Path('C:/mingw64/bin') / name, game / name)
    shutil.copytree(binaries / 'assets', game / 'assets')
    shutil.copytree(binaries / 'voice', game / 'voice')
    #Keep third-party notices alongside the runtimes we redistribute.
    with ZipFile(BUILD / 'gfx_2d-windows-x64-client-server-playtest2-2026-10-19.zip') as old:
        for name in old.namelist():
            if '/licenses/' in name and name.startswith('gfx_2d-windows-x64/licenses/'):
                target = game / Path(name).relative_to('gfx_2d-windows-x64')
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(old.read(name))
    runtime = package / 'runtime' / 'python'
    shutil.copytree(CACHE / 'runtime' / 'python', runtime, ignore=shutil.ignore_patterns('__pycache__'))
    shutil.copy2(CACHE / 'runtime-provenance.json', package / 'runtime' / 'provenance.json')
    server = package / 'ledger' / 'server'
    server.mkdir(parents=True)
    for name in ('app.py', 'ledger.py', 'snapshots.py', 'requirements.txt'):
        shutil.copy2(LEDGER / 'server' / name, server / name)
    shutil.copytree(LEDGER / 'frontend' / 'dist', package / 'ledger' / 'frontend' / 'dist')
    (package / 'tools').mkdir()
    shutil.copy2(ROOT / 'tools' / 'launch_everything.py', package / 'tools' / 'launch_everything.py')
    shutil.copy2(ROOT / 'Start Everything.bat', package / 'Start Everything.bat')
    (package / 'Start Client.bat').write_bytes(b'@echo off\r\ncd /d "%~dp0game"\r\nstart "" gfx.exe\r\n')
    instructions = '''GFX_2D all-in-one - Windows x64 - playtest.4

Extract the WHOLE ZIP into a writable folder. Run Start Everything.bat.
No installed Python, Node, compiler, pip or npm is needed.
Game TCP: 9999 by default. Game UDP: 9999 by default, or choose another port.
Voice: 9998, both TCP and UDP. Ticks: 60 by default, maximum 180.
Ledger website AND API: one TCP port, 8000 by default. No separate frontend server.
Choose the Debug game server to see detailed server logs.
The launcher opens separate server windows and optionally the browser and game.
Closing the launcher does not stop the servers. Stop them in their own windows.
Use Ctrl+C in the ledger console when finished so it can close cleanly.

Friends can use Start Client.bat and connect to your public/LAN IP and GAME TCP port.
They keep the complete game folder, including its voice subfolder and DLLs.
For remote ledger access: select ledger bind 0.0.0.0 and enter the public/LAN hostname.
Forward the selected game TCP/UDP, voice TCP/UDP, and ledger TCP ports as needed.
The launcher does not modify firewall or router settings.
After a match the host can upload results using the ledger address and port.
Ledger uploads remain optional and use the existing trusted-demo HTTP API.

This package starts a NEW ledger. Your existing records and keys are not included.
New records and the encryption key are stored under ledger/server/data.
Keep/back up that whole data folder; losing snapshot.key invalidates old downloads.
No group needs creating manually in voice_server: GFX manages its own voice groups.
If a port is occupied, the launcher stops before starting anything. Pick free ports.
Power users: Start Everything.bat --defaults --debug-server --game-tcp 9999 --game-udp 9999 --voice-port 9998 --tick-rate 60 --ledger-port 8000
'''
    (package / 'READ ME.txt').write_text(instructions, encoding='utf-8')
    versions = {}
    for name, folder in (('gfx', ROOT), ('ledger', LEDGER)):
        versions[name] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=folder, text=True).strip()
    versions['client'] = '0.1.0-playtest.4'
    (package / 'VERSION.json').write_text(json.dumps(versions, indent=2) + '\n')
    files = [p for p in package.rglob('*') if p.is_file()]
    assert not any(p.name in ('chain.jsonl', 'snapshot.key') for p in files)
    (package / 'SHA256SUMS.txt').write_text(''.join(
        sha256(p.read_bytes()).hexdigest() + '  ' + p.relative_to(package).as_posix() + '\n'
        for p in sorted(files)))
    archive = BUILD / 'gfx_2d-all-in-one-windows-x64-playtest4-2026-10-22.zip'
    with ZipFile(archive, 'w', ZIP_DEFLATED, compresslevel=6) as output:
        for path in sorted(package.rglob('*')):
            if path.is_file():
                output.write(path, path.relative_to(stage).as_posix())
    with ZipFile(archive) as output:
        assert output.testzip() is None
    digest = sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix('.zip.sha256').write_text(digest + '  ' + archive.name + '\n')
    print('Package folder: ' + str(package))
    print('ZIP: ' + str(archive))
    print('SHA256: ' + digest)


if __name__ == '__main__':
    main()
