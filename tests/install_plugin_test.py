"""Run the widget installer against a scratch home folder and stand-in Omarchy commands."""
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
installer = root / 'bin/install-plugin'
STUB = '''#!{python}
import json, os, sys
with open(os.environ['STUB_LOG'], 'a') as log:
    log.write(json.dumps([os.path.basename(sys.argv[0])] + sys.argv[1:]) + '\\n')
'''


def install(scratch, layout=None, script=installer, omarchy=True):
    """Returns the exit code, the commands the installer ran, and the home folder it used."""
    home = scratch / 'home'
    commands = scratch / 'commands'
    log = scratch / 'log'
    for folder in (home, commands):
        shutil.rmtree(folder, ignore_errors=True)
        folder.mkdir(parents=True)
    log.unlink(missing_ok=True)
    for name in (['omarchy', 'omarchy-shell'] if omarchy else []):
        stub = commands / name
        stub.write_text(STUB.format(python=sys.executable))
        stub.chmod(0o755)
    if layout is not None:
        config = home / '.config/omarchy/shell.json'
        config.parent.mkdir(parents=True)
        config.write_text(json.dumps({'bar': {'layout': layout}}))
    environment = {'HOME': str(home), 'PATH': str(commands), 'STUB_LOG': str(log)}
    done = subprocess.run([sys.executable, '-B', str(script)], env=environment, capture_output=True, text=True)
    calls = [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []
    return done, calls, home


def enabling(calls):
    found = [call for call in calls if call[:3] == ['omarchy', 'plugin', 'enable']]
    assert len(found) == 1, calls
    return found[0]


def placed(home):
    layout = json.loads((home / '.config/omarchy/shell.json').read_text())['bar']['layout']
    return {section: [entry['id'] if isinstance(entry, dict) else entry for entry in entries] for section, entries in layout.items()}


with tempfile.TemporaryDirectory() as scratch:
    scratch = pathlib.Path(scratch)

    # A first install puts the widget on the right of the bar and copies its files.
    done, calls, home = install(scratch)
    assert done.returncode == 0, done.stderr
    assert calls[0] == ['omarchy', 'plugin', 'validate', str(root / 'plugin')], calls
    assert enabling(calls) == ['omarchy', 'plugin', 'enable', 'local.sourceroll', '--section', 'right', '--before', 'omarchy.tray']
    assert (home / '.config/omarchy/plugins/local.sourceroll/manifest.json').is_file()
    assert calls[-1] == ['omarchy', 'restart', 'shell'], calls

    # Installing again, as after an update, leaves the widget where it was moved to.
    bar = {'centerEnd': [{'id': 'omarchy.weather'}, {'id': 'local.sourceroll'}], 'right': [{'id': 'omarchy.tray'}]}
    done, calls, home = install(scratch, bar)
    assert done.returncode == 0, done.stderr
    assert enabling(calls) == ['omarchy', 'plugin', 'enable', 'local.sourceroll'], calls
    assert placed(home) == {'centerEnd': ['omarchy.weather', 'local.sourceroll'], 'right': ['omarchy.tray']}

    # The widget under its former name keeps its place under the new one.
    done, calls, home = install(scratch, {'left': [{'id': 'local.multitake'}, {'id': 'omarchy.menu'}]})
    assert done.returncode == 0, done.stderr
    assert enabling(calls) == ['omarchy', 'plugin', 'enable', 'local.sourceroll'], calls
    assert placed(home) == {'left': ['local.sourceroll', 'omarchy.menu']}

    # The shell also takes a bare ID as an entry. Those count as placed, and are renamed, the same way.
    done, calls, home = install(scratch, {'left': ['local.sourceroll', {'id': 'omarchy.menu'}]})
    assert done.returncode == 0, done.stderr
    assert enabling(calls) == ['omarchy', 'plugin', 'enable', 'local.sourceroll'], calls
    assert placed(home) == {'left': ['local.sourceroll', 'omarchy.menu']}
    done, calls, home = install(scratch, {'right': ['omarchy.tray', 'local.multitake']})
    assert done.returncode == 0, done.stderr
    assert enabling(calls) == ['omarchy', 'plugin', 'enable', 'local.sourceroll'], calls
    assert placed(home) == {'right': ['omarchy.tray', 'local.sourceroll']}

    # The package installs the script under /usr/share with a command linked to it;
    # run that way, it takes the widget files installed beside it.
    share = scratch / 'usr/share/sourceroll'
    (share / 'bin').mkdir(parents=True)
    shutil.copy2(installer, share / 'bin/install-plugin')
    shutil.copytree(root / 'plugin', share / 'plugin')
    command = scratch / 'usr/bin/sourceroll-install-widget'
    command.parent.mkdir()
    command.symlink_to('../share/sourceroll/bin/install-plugin')
    done, calls, home = install(scratch, script=command)
    assert done.returncode == 0, done.stderr
    assert calls[0] == ['omarchy', 'plugin', 'validate', str(share / 'plugin')], calls
    assert (home / '.config/omarchy/plugins/local.sourceroll/BarWidget.qml').is_file()

    # Without Omarchy it says so and touches nothing.
    done, calls, home = install(scratch, omarchy=False)
    assert done.returncode != 0 and 'Omarchy' in done.stderr, done
    assert not calls and not (home / '.config').exists() and not (home / '.local').exists()

print('Widget installer tests passed.')
