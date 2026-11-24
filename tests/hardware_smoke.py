"""Opt-in real camera/microphone test, including orderly application termination."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument('--record', action='store_true', help='Allow a two-second camera/microphone capture')
parser.add_argument('--binary', default=str(Path(__file__).resolve().parents[1] / 'build/multitake'))
args = parser.parse_args()
if not args.record:
    parser.error('Pass --record to authorize the hardware test')
args.binary = str(Path(args.binary).resolve())

def call(*command):
    return json.loads(subprocess.check_output([args.binary, *command, '--json'], text=True, timeout=15))

def quit_resident():
    state = call('status')
    if not state.get('running'):
        return
    lock = Path(os.environ['XDG_RUNTIME_DIR']) / 'multitake.sock.lock'
    pid = int(lock.read_text().splitlines()[0])
    assert Path(f'/proc/{pid}/comm').read_text().strip().startswith('multitake')
    os.kill(pid, signal.SIGTERM)
    for _ in range(100):
        time.sleep(.2)
        if not Path(f'/proc/{pid}').exists():
            assert not call('status')['running']
            return
    raise AssertionError('Resident did not finalize and quit')

if call('status')['running']:
    raise SystemExit('Quit Multitake first; the test will not disturb an existing session.')
original = call('settings')['settings']
with tempfile.TemporaryDirectory(prefix='multitake-smoke-') as directory:
    try:
        call('configure', '--camera-enabled', 'true', '--microphone-enabled', 'true',
             '--screen-enabled', 'false', '--desktop-enabled', 'false', '--encoding', 'h264', '--directory', directory)
        call('start')
        for _ in range(100):
            time.sleep(.2)
            status = call('status')
            if status['state'] in ('recording', 'error'):
                break
        assert status['state'] == 'recording', status
        output = Path(status['output'])
        time.sleep(2)
        quit_resident()
        manifest = json.loads((output / 'session.json').read_text())
        assert manifest['state'] == 'complete', manifest
        assert {s['id'] for s in manifest['sources']} == {'camera', 'microphone'}
        assert (output / 'timeline.otio').is_file()
        for source in manifest['sources']:
            assert source['state'] == 'saved', source
            assert source['durationSeconds'] > 1, source
            assert source['firstMonotonicUs'] > 0
            probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_streams', '-of', 'json', str(output / source['file'])], text=True))
            assert len(probe['streams']) == 1, probe
        print(json.dumps({'state': manifest['state'], 'sources': [
            {k:s[k] for k in ('id','file','durationSeconds','startOffsetSeconds')} for s in manifest['sources']]}, indent=2))
    finally:
        if call('status').get('recording'):
            call('stop')
            for _ in range(100):
                time.sleep(.2)
                if not call('status').get('recording'):
                    break
        restore = []
        for key, value in original.items():
            restore += ['--' + key, str(value).lower() if isinstance(value, bool) else str(value)]
        call('configure', *restore)
        quit_resident()
print('Hardware smoke test passed; settings restored and recorder stopped.')
