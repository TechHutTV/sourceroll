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
assert(parsePreview('{"event":"preview","source":"screen","jpeg":"/9j/2Q=="}', 'screen') !== null)
assert(parsePreview('{"event":"preview","source":"screen","jpeg":"/9j/2Q=="}', 'camera') === null)
assert(parsePreview('{"event":"preview","source":"<img>","jpeg":"/9j/2Q=="}', 'screen') === null)
assert(parseStatus('{"recording":true,"state":"recording","sources":[{"id":"screen","state":"recording"}]}').screenRecording)
assert(!parseStatus('{"recording":true,"state":"validating","sources":[{"id":"screen","state":"recording"}]}').screenRecording)
assert(parseStatus('{"recording":true,"state":"recording","screenHealth":"recovering","sources":[{"id":"screen","state":"recording"}]}').screenRecording === false)
assert(parseStatus('{"recording":true,"state":"recording","screenHealth":"live","sources":[{"id":"screen-002","state":"recording"}]}').screenRecording)
assert(parseStatus('{"recording":true,"displayWarning":"OmaControl is open"}').displayWarning === 'OmaControl is open')
// Every reported source is checked on its own; anything malformed is dropped, not shown.
const many = parseStatus(JSON.stringify({recording: false, videoSources: [
    {id: 'screen', kind: 'screen', name: 'Screen', enabled: true, picker: true, previewState: 'live', selected: true, selecting: false, recording: false},
    {id: 'screen-2', kind: 'screen', name: '<b>Left</b> monitor', enabled: true, previewState: 'waiting', recording: true},
    {id: 'camera', kind: 'camera', name: 'Camera', enabled: false},
    {id: '../etc', kind: 'camera', name: 'bad id', enabled: true},
    {id: 'mic', kind: 'microphone', name: 'bad kind', enabled: true},
    'not an object', null]})).videoSources
assert(many.length === 3)
assert(many[1].id === 'screen-2' && many[1].name === '<b>Left</b> monitor' && many[1].recording === true && many[1].picker === false)
assert(many[2].enabled === false && many[2].previewState === 'inactive')
assert(parseStatus(JSON.stringify({recording: false, videoSources: Array(40).fill({id: 'camera', kind: 'camera'})})).videoSources.length === 16)
assert(parseStatus(JSON.stringify({recording: false, videoSources: [{id: 'camera', kind: 'camera', name: 'x'.repeat(500)}]})).videoSources[0].name.length === 80)
// An app from before sources were a list still gets one screen and one camera.
const legacy = parseStatus('{"recording":true,"state":"recording","screenEnabled":true,"screenPreviewState":"live","screenSelected":true,"sources":[{"id":"screen","state":"recording"}]}').videoSources
assert(legacy.length === 2 && legacy[0].id === 'screen' && legacy[0].enabled && legacy[0].recording && legacy[0].picker)
assert(legacy[1].id === 'camera' && legacy[1].enabled && !legacy[1].recording)
// The panel shows the picked source while it is on, else the first that is.
assert(monitored(many, 'screen-2') === 'screen-2')
assert(monitored(many, 'camera') === 'screen')
assert(monitored(many, 'missing') === 'screen')
assert(monitored([], 'screen') === '' && monitored(undefined, '') === '')
assert(find(many, 'screen-2').previewState === 'waiting' && find(many, 'nope').id === undefined)
assert(parsePreview('{"event":"preview","source":"screen-2","jpeg":"/9j/2Q=="}', 'screen-2') !== null)
assert(parsePreview('{"event":"preview","source":"screen-2","jpeg":"/9j/2Q=="}', 'screen') === null)
assert(parsePreview('{"event":"preview","source":"Screen 2","jpeg":"/9j/2Q=="}', 'Screen 2') === null)
assert(validId('camera-3') && !validId('') && !validId('-x') && !validId('a'.repeat(33)) && !validId(7))
// Text fields of the wrong type take their defaults; the entry and the rest of the status stay.
for (const junk of [{}, [1, 2], 7, true]) {
    const odd = parseStatus(JSON.stringify({recording: true, error: 'kept', videoSources: [
        {id: 'screen', kind: 'screen', name: junk, previewState: junk, enabled: true}, {id: 'camera', kind: 'camera', name: 'Cam', previewState: 'live'}]}))
    assert(odd !== null && odd.error === 'kept' && odd.videoSources.length === 2)
    assert(odd.videoSources[0].name === '' && odd.videoSources[0].previewState === 'inactive' && odd.videoSources[0].enabled)
    assert(odd.videoSources[1].name === 'Cam' && odd.videoSources[1].previewState === 'live')
}
// The widget speaks to the recorder it finds. A 0.6.0 recorder is addressed per source.
const same = a => JSON.stringify(a)
assert(same(switchCommand(many[0], false)) === same(['sourceroll', 'source', 'set', 'screen', '--enabled', 'false', '--json']))
assert(same(switchCommand(many[0], true)) === same(['sourceroll', 'choose-screen', 'screen', '--json']))
assert(same(switchCommand(many[1], true)) === same(['sourceroll', 'source', 'set', 'screen-2', '--enabled', 'true', '--json']))
assert(same(switchCommand(many[2], true)) === same(['sourceroll', 'source', 'set', 'camera', '--enabled', 'true', '--json']))
assert(same(chooseCommand(many[1])) === same(['sourceroll', 'choose-screen', 'screen-2', '--json']))
// A 0.5.0 recorder has no source commands: it gets its own switches, one per kind, and a picker without an id.
assert(legacy[0].legacy && legacy[1].legacy && !many[0].legacy)
assert(same(switchCommand(legacy[0], false)) === same(['sourceroll', 'configure', '--screen-enabled', 'false', '--json']))
assert(same(switchCommand(legacy[0], true)) === same(['sourceroll', 'choose-screen', '--json']))
assert(same(switchCommand(legacy[1], false)) === same(['sourceroll', 'configure', '--camera-enabled', 'false', '--json']))
assert(same(switchCommand(legacy[1], true)) === same(['sourceroll', 'configure', '--camera-enabled', 'true', '--json']))
assert(same(chooseCommand(legacy[0])) === same(['sourceroll', 'choose-screen', '--json']))
assert(switchCommand({id: '../x', kind: 'camera'}, true) === null && switchCommand(null, true) === null && chooseCommand({}) === null)
console.log('Plugin parsing and rich-text boundary tests passed.')
'''
with tempfile.NamedTemporaryFile(suffix='.js', mode='w') as f:
    f.write(script)
    f.flush()
    subprocess.run(['node', f.name], check=True)
