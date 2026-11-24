"""Validate plugin data boundaries without loading the long-lived desktop shell."""
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
model = (root / 'plugin/Model.js').read_text().replace('.pragma library', '')
qml = '\n'.join(p.read_text() for p in (root / 'plugin').glob('*.qml'))
assert 'retainWhileLoading: true' in qml, 'Live preview must retain its previous frame during image loading'
for text_item in re.findall(r'\bText\s*\{([^{}]*)', qml):
    assert 'textFormat: Text.PlainText' in text_item, 'Plugin text must never auto-render markup'
script = model + r'''
function assert(value) { if (!value) throw new Error('assertion failed') }
assert(parseStatus('{"recording":true,"elapsedSeconds":17,"error":"<img src=\\"https://example.invalid/a\\">"}') !== null)
assert(parseStatus('not json') === null)
assert(parseStatus('{"recording":"true"}') === null)
assert(parseStatus('x'.repeat(65537)) === null)
assert(parseStatus('{"recording":true,"elapsedSeconds":1e99}').elapsed === 359999)
assert(!safeText('<img src="https://example.invalid/a">').includes('<'))
assert(safeText('&<"\'').includes('&amp;'))
assert(duration(83) === '01:23')
assert(parsePreview('{"event":"preview","jpeg":"/9j/2Q=="}').startsWith('data:image/jpeg;base64,'))
assert(parsePreview('{"event":"preview","jpeg":"https://example.invalid/a"}') === null)
assert(parsePreview('{"event":"preview","jpeg":"/9j/<img>"}') === null)
assert(parsePreview('x'.repeat(1000101)) === null)
assert(parseMeter('{"event":"meter","peakDb":0}') === 0)
assert(parseMeter('{"event":"meter","peakDb":-18}') === -18)
assert(parseMeter('{"event":"meter","peakDb":"<img>"}') === null)
assert(parseMeter('x'.repeat(513)) === null)
assert(parseDesktopMeter('{"event":"meter","desktopPeakDb":-12}') === -12)
assert(parseDesktopMeter('{"event":"meter","desktopPeakDb":"bad"}') === null)
console.log('Plugin parsing and rich-text boundary tests passed.')
'''
with tempfile.NamedTemporaryFile(suffix='.js', mode='w') as f:
    f.write(script)
    f.flush()
    subprocess.run(['node', f.name], check=True)
