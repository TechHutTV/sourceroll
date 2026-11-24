"""Validate plugin data boundaries without loading the long-lived desktop shell."""
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
model = (root / 'plugin/Model.js').read_text().replace('.pragma library', '')
qml = (root / 'plugin/BarWidget.qml').read_text()
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
console.log('Plugin parsing and rich-text boundary tests passed.')
'''
with tempfile.NamedTemporaryFile(suffix='.js', mode='w') as f:
    f.write(script)
    f.flush()
    subprocess.run(['node', f.name], check=True)
