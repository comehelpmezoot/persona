#!/usr/bin/env python3
"""Static sanity checks for the QML (there is no Qt in CI): balanced brackets, every asset path
exists in the original repo's Assets folder, every type used is defined, and every native symbol
that QML touches exists in the C++ registration."""
import os, re, sys, glob

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
qml_dir = os.path.join(root, 'qml')
assets = sys.argv[1] if len(sys.argv) > 1 else None
errors = 0
def err(msg):
    global errors; errors += 1; print('ERROR', msg)

files = sorted(glob.glob(os.path.join(qml_dir, '*.qml')))
local_types = {os.path.splitext(os.path.basename(f))[0] for f in files}

def strip_strings_and_comments(s):
    out = []; i = 0; n = len(s)
    while i < n:
        c = s[i]
        if c == '/' and i + 1 < n and s[i+1] == '/':
            while i < n and s[i] != '\n': i += 1
        elif c == '/' and i + 1 < n and s[i+1] == '*':
            j = s.find('*/', i + 2); i = n if j < 0 else j + 2
        elif c in '"\'':
            q = c; i += 1
            while i < n and s[i] != q:
                if s[i] == '\\': i += 1
                i += 1
            i += 1; out.append('""')
        else:
            out.append(c); i += 1
    return ''.join(out)

builtin = set('''Item Rectangle Text Image Canvas Column Row Repeater Timer Connections Behavior NumberAnimation
SequentialAnimation PauseAnimation ScriptAction ColorAnimation SpringAnimation MouseArea HoverHandler TapHandler
FocusScope TextInput ListView ScrollBar ScrollView MultiEffect ShaderEffect ShaderEffectSource Gradient GradientStop
Translate Scale Rotation Matrix4x4 RowLayout MediaPlayer VideoOutput Window QtObject Instantiator Binding
Component Loader Flickable Layout Qt Math Date String Array Object JSON console Number Boolean Error
ParallelAnimation CavaMonitor''' .split())

used_native = set()
for f in files:
    raw = open(f, encoding='utf-8').read()
    s = strip_strings_and_comments(raw)
    name = os.path.basename(f)
    for a, b in ['{}', '()', '[]']:
        if s.count(a) != s.count(b): err(f'{name}: unbalanced {a}{b}: {s.count(a)} vs {s.count(b)}')
    # imports
    for m in re.finditer(r'^import\s+(\S+)', raw, re.M):
        mod = m.group(1)
        if mod in ('Quickshell', 'Quickshell.Wayland') or mod.startswith('qs.') or mod.startswith('Quickshell'):
            err(f'{name}: leftover Quickshell import {mod}')
    # types instantiated: "Name {" at statement start or after ':'
    for m in re.finditer(r'(?:^|[\s:])([A-Z][A-Za-z0-9_]*)\s*\{', s):
        t = m.group(1)
        if t in builtin or t in local_types: continue
        if t in ('Scope',): continue
        # inline components declared in this file
        if re.search(r'component\s+' + t + r'\b', raw): continue
        err(f'{name}: unknown type "{t}"')
    # native singletons used
    for sym in re.findall(r'\b(Shell|AudioCtl|BrightnessCtl|BatteryInfo|SysInfo|NetInfo|BluetoothInfo|PowerCtl|ScreenFilter|MediaCtl|Apps|CavaMonitor)\b', s):
        used_native.add((sym, name))
    # asset references
    if assets:
        for m in re.finditer(r'Shell\.asset\("([^"]+)"\s*\)', raw):
            p = m.group(1)
            if not os.path.exists(os.path.join(assets, p)): err(f'{name}: missing asset "{p}"')
    # shaders
    for m in re.finditer(r'qrc:/shaders/([^"]+)', raw):
        if not os.path.exists(os.path.join(root, 'shaders', m.group(1).replace('.qsb', ''))): err(f'{name}: missing shader source for {m.group(0)}')
    # leftovers from the Linux config
    for bad in ['Wlr', 'LazyLoader', 'Variants', 'hyprctl', 'Qt.resolvedUrl("../', 'PwObjectTracker', 'Mpris', 'DesktopEntries']:
        if bad in s: err(f'{name}: leftover Linux-ism "{bad}"')

# member usage on native singletons must exist in the C++ headers
hdr = ''.join(open(p).read() for p in glob.glob(os.path.join(root, 'src', '*.h')))
members = {
 'Shell': r'(?<![\w.])Shell\.(\w+)', 'AudioCtl': r'(?<![\w.])AudioCtl\.(\w+)', 'BrightnessCtl': r'(?<![\w.])BrightnessCtl\.(\w+)',
 'BatteryInfo': r'(?<![\w.])BatteryInfo\.(\w+)', 'SysInfo': r'(?<![\w.])SysInfo\.(\w+)', 'NetInfo': r'(?<![\w.])NetInfo\.(\w+)',
 'BluetoothInfo': r'(?<![\w.])BluetoothInfo\.(\w+)', 'PowerCtl': r'(?<![\w.])PowerCtl\.(\w+)', 'ScreenFilter': r'(?<![\w.])ScreenFilter\.(\w+)',
 'MediaCtl': r'(?<![\w.])MediaCtl\.(\w+)', 'Apps': r'(?<![\w.])Apps\.(\w+)'}
alltext = '\n'.join(strip_strings_and_comments(open(f, encoding='utf-8').read()) for f in files)
for sym, pat in members.items():
    for member in sorted(set(re.findall(pat, alltext))):
        if not re.search(r'\b' + member + r'\b', hdr): err(f'{sym}.{member} is used in QML but not declared in any C++ header')
# Shell.config.<key>
for key in sorted(set(re.findall(r'Shell\.config\.(\w+)', alltext))):
    if f'"{key}"' not in open(os.path.join(root, 'src', 'main.cpp')).read(): err(f'Shell.config.{key} has no default in main.cpp')

qmldir = open(os.path.join(qml_dir, 'qmldir')).read()
for t in sorted(t for t in local_types if t[0].isupper()):
    if not re.search(r'^(singleton\s+)?' + t + r'\s+1\.0\s+' + t + r'\.qml\s*$', qmldir, re.M): err(f'qmldir does not list {t}')
    is_single = 'pragma Singleton' in open(os.path.join(qml_dir, t + '.qml'), encoding='utf-8').read()
    if is_single != bool(re.search(r'^singleton\s+' + t + r'\s', qmldir, re.M)): err(f'qmldir singleton flag mismatch for {t}')
print(f'checked {len(files)} QML files; types: {len(local_types)}; native symbols used: {len({s for s,_ in used_native})}')
print('RESULT:', 'FAILED' if errors else 'all checks passed')
sys.exit(1 if errors else 0)
