"""Call only the dedicated DCLD00001 install helper; never the older title's control service."""
import argparse
import hashlib
import json
from pathlib import Path
import socket
import urllib.request

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('operation', choices=('inject', 'health', 'status', 'install', 'shutdown'))
p.add_argument('--connection', type=Path, required=True)
p.add_argument('--helper-build', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--binloader-port', type=int, help='Explicit verified BIN port; used only for inject')
a = p.parse_args()
cfg = json.loads(a.connection.read_text(encoding='utf-8-sig'))
helper = json.loads((a.helper_build / 'build-evidence.json').read_text())
if helper['title'] != 'DCLD00001' or not helper['package'].startswith('/user/data/pkg/DCLD00001-'):
    p.error('Helper is not scoped to this application')
if a.output.exists():
    p.error('Refusing to overwrite an existing operation receipt')
if a.operation == 'inject':
    if not a.binloader_port:
        p.error('Injection requires the verified BIN loader port')
    payload = (a.helper_build / 'chronicle-install-helper.bin').read_bytes()
    if hashlib.sha256(payload).hexdigest() != helper['sha256']:
        p.error('Helper payload hash differs from its preserved build')
    with socket.create_connection((cfg['host'], a.binloader_port), timeout=5) as stream:
        stream.sendall(payload)
        stream.shutdown(socket.SHUT_WR)
    result = {'operation': 'inject', 'host': cfg['host'], 'title': helper['title'],
              'binloader_port': a.binloader_port, 'service_port': helper['service_port'],
              'bytes': len(payload), 'sha256': helper['sha256'], 'status': 'sent; health not yet checked'}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    raise SystemExit(0)
method = 'GET' if a.operation in ('health', 'status') else 'POST'
request = urllib.request.Request(
    f'http://{cfg["host"]}:{helper["service_port"]}/{a.operation}',
    data=b'' if method == 'POST' else None, method=method)
# The existing tiny server deliberately requires this exact header spelling.
request.headers = {'X-Cole-Token': cfg['token']}
with urllib.request.urlopen(request, timeout=30) as response:
    result = json.load(response)
a.output.parent.mkdir(parents=True, exist_ok=True)
record = {'operation': a.operation, 'host': cfg['host'], 'service_port': helper['service_port'],
          'title': helper['title'], 'package': helper['package'],
          'helper_sha256': helper['sha256'], 'result': result}
a.output.write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps(record, indent=2))
if a.operation in ('install', 'launch', 'stop') and int(result.get('last_result', '0'), 16) >= 0x80000000:
    raise SystemExit(1)
