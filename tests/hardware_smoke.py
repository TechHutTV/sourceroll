"""Opt-in real camera/microphone test, including orderly application termination."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument('--record', action='store_true', help='Allow a two-second camera/microphone capture')
parser.add_argument('--binary', default=str(Path(__file__).resolve().parents[1] / 'build/sourceroll'))
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
    lock = Path(os.environ['XDG_RUNTIME_DIR']) / 'sourceroll.sock.lock'
    pid = int(lock.read_text().splitlines()[0])
    assert Path(f'/proc/{pid}/comm').read_text().strip().startswith('sourceroll')
    os.kill(pid, signal.SIGTERM)
    for _ in range(100):
        time.sleep(.2)
        if not Path(f'/proc/{pid}').exists():
            assert not call('status')['running']
            return
    raise AssertionError('Resident did not finalize and quit')

if call('status')['running']:
    raise SystemExit('Quit Source Roll first; the test will not disturb an existing session.')
original = call('settings')['settings']
sources = original.get('videoSources', [])
camera = next((s for s in sources if s['kind'] == 'camera'), None)
if camera is None:
    raise SystemExit('Add a camera source first; the test records the first one.')
text = lambda value: str(value).lower() if isinstance(value, bool) else str(value)
with tempfile.TemporaryDirectory(prefix='sourceroll-smoke-') as directory:
    try:
        # Record only the first camera and the microphone, whatever else is set up.
        for source in sources:
            call('source', 'set', source['id'], '--enabled', text(source is camera))
        call('source', 'set', camera['id'], '--encoding', 'h264')
        call('configure', '--microphone-enabled', 'true', '--desktop-enabled', 'false', '--directory', directory)
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
        assert sorted(s.get('source', s['id']) for s in manifest['sources']) == sorted([camera['id'], 'microphone'])
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
        # Put everything back, each step on its own, so one that fails or times out does not
        # leave the rest undone. The output folder is restored before the temporary one goes.
        failures = []
        def attempt(step, action):
            try:
                action()
            except Exception as error:
                failures.append(f'{step}: {error}')
        def stop_take():
            if call('status').get('recording'):
                call('stop')
                for _ in range(100):
                    time.sleep(.2)
                    if not call('status').get('recording'):
                        break
        def restore(source):
            options = ['--enabled', text(source['enabled'])]
            if source['kind'] == 'camera':
                options += ['--encoding', source['encoding']]
            call('source', 'set', source['id'], *options)
        attempt('stop the take', stop_take)
        for source in sources:
            attempt('restore ' + source['id'], lambda source=source: restore(source))
        attempt('restore audio switches and output folder', lambda: call(
            'configure', '--microphone-enabled', text(original['microphoneEnabled']),
            '--desktop-enabled', text(original['desktopEnabled']), '--directory', original['directory']))
        attempt('quit the recorder', quit_resident)
        if failures:
            print('Not everything was put back:\n  ' + '\n  '.join(failures), file=sys.stderr)
            if sys.exc_info()[0] is None:
                raise SystemExit(1)
print('Hardware smoke test passed; settings restored and recorder stopped.')
