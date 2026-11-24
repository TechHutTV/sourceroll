"""Explicit hardware test: capture two seconds, verify output and graceful shutdown.

Run only when the camera/microphone are free. This stops the app it starts.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument('--record', action='store_true', help='Explicitly allow camera capture')
parser.add_argument('--binary', default=str(Path(__file__).resolve().parents[1] / 'build/source-record'))
args = parser.parse_args()
if not args.record:
    parser.error('Pass --record to authorize the short camera test')

def call(*command):
    return json.loads(subprocess.check_output([args.binary, *command, '--json'], text=True, timeout=15))

if call('status')['running']:
    raise SystemExit('Quit Source Record first; the test will not disturb an existing session.')
original = call('settings')['settings']
with tempfile.TemporaryDirectory(prefix='source-record-smoke-') as directory:
    try:
        call('configure', '--audio', 'none', '--encoding', 'h264', '--directory', directory)
        result = call('start')
        for _ in range(40):
            time.sleep(.2)
            status = call('status')
            if status['state'] in ('recording', 'error'):
                break
        assert status['state'] == 'recording', status
        time.sleep(2)
        pid = int((Path(os.environ['XDG_RUNTIME_DIR']) / 'source-record.sock.lock').read_text().splitlines()[0])
        os.kill(pid, signal.SIGTERM)
        for _ in range(50):
            time.sleep(.2)
            if not call('status')['running']:
                break
        assert not call('status')['running'], 'Resident failed to quit'
        probe = json.loads(subprocess.check_output([
            'ffprobe', '-v', 'error', '-show_entries',
            'stream=codec_name,width,height,r_frame_rate:format=duration,size', '-of', 'json', result['output']
        ], text=True))
        assert len(probe['streams']) == 1
        assert probe['streams'][0]['codec_name'] == 'h264'
        assert float(probe['format']['duration']) > 1
        print(json.dumps(probe, indent=2))
    finally:
        call('stop')
        call('configure', '--audio', original['audio'], '--encoding', original['encoding'], '--directory', original['directory'])
        pid = int((Path(os.environ['XDG_RUNTIME_DIR']) / 'source-record.sock.lock').read_text().splitlines()[0])
        os.kill(pid, signal.SIGTERM)

print('Hardware smoke test passed; settings restored and recorder stopped.')
