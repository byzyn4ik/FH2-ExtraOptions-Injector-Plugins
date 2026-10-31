"""Offline runtime-provider integration tests; never execute game instructions.

Maps/relocates the user's FH2 executable, calls the provider as the injector
does, and checks native patches, rejection, settings precedence and ownership.
Each scenario runs in a fresh process because game runtimes remain resident.
"""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

HERE = Path(__file__).resolve().parent
RUNTIME = HERE.parent
PLUGIN = RUNTIME.parent
GAME_EXE = Path('Anthem_1.0.0.64_x64__8wekyb3d8bbwe/Mount/forza_durango_release_final.exe')


def load_feature_tests():
    spec = importlib.util.spec_from_file_location('feature_tests', PLUGIN / 'tests/run_tests.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def relocate(image):
    pe = struct.unpack('<I', image.read(0x3c, 4))[0]
    old_base = struct.unpack('<Q', image.read(pe + 24 + 24, 8))[0]
    rva, size = struct.unpack('<II', image.read(pe + 24 + 112 + 5 * 8, 8))
    at, delta = 0, image.base - old_base
    while at < size:
        page, block = struct.unpack('<II', image.read(rva + at, 8))
        if block < 8 or at + block > size:
            raise ValueError('invalid relocation block')
        for i in range((block - 8) // 2):
            entry = struct.unpack('<H', image.read(rva + at + 8 + 2 * i, 2))[0]
            if entry >> 12 == 10:
                site = page + (entry & 0xfff)
                value = struct.unpack('<Q', image.read(site, 8))[0]
                image.write(site, struct.pack('<Q', (value + delta) & 0xffffffffffffffff))
        at += block


def digest(image):
    return hashlib.sha256(image.read(0, image.size)).hexdigest()


def check(condition, text):
    if not condition:
        raise AssertionError(text)
    print('  ok    ' + text, flush=True)


def options_type():
    text = (RUNTIME / 'src/extra_options.h').read_text(encoding='utf-8')
    body = re.search(r'typedef struct\s*\{([^{}]*)\}\s*ModOptions;', text).group(1)
    names = []
    for declaration in re.findall(r'\bint\s+([^;]+);', body):
        names.extend(name.strip() for name in declaration.split(','))

    class Options(ctypes.Structure):
        _fields_ = [(name, ctypes.c_int) for name in names]
    return Options


def prepare(output, scenario):
    root = output / 'test-runs/runtime' / scenario
    if root.exists():
        shutil.rmtree(root)
    plugins = root / 'mods/Xtra Options/plugins'
    plugins.mkdir(parents=True)
    shutil.copy2(output / 'tests/XtraRuntime.dll', plugins / 'XtraRuntime.dll')
    shutil.copy2(PLUGIN / 'mod/mod.json', plugins.parent / 'mod.json')
    shipped = output / 'mods/Xtra Options/plugins'
    for dll in shipped.glob('*.dll'):
        if dll.name not in ('XtraRuntime.dll', 'XtraFeatures.dll'):
            shutil.copy2(dll, plugins / dll.name)
    ini = (PLUGIN / 'mod/XtraOptions.ini').read_text(encoding='utf-8')
    if scenario == 'active-aero':
        ini = ini.replace('UnlockAeroTuning=0', 'UnlockAeroTuning=1')
    (plugins / 'XtraOptions.ini').write_text(ini, encoding='utf-8')
    # Active scenarios prove an existing launcher INI takes precedence over
    # the newly shipped plugin INI; settings are never rewritten by migration.
    (plugins / 'XtraRuntime.ini').write_text('[Global]\nEnabled=0\n', encoding='utf-8')
    if scenario != 'plugin-disabled':
        (root / 'FH2_ExtraOptions.ini').write_text('[Global]\nEnabled=1\n', encoding='utf-8')
    return root


def scenario(output, game, name):
    tests = load_feature_tests()
    image = tests.Image(game)
    relocate(image)
    root = prepare(output, name)
    path = root / 'mods/Xtra Options/plugins/XtraRuntime.dll'
    dll = ctypes.WinDLL(str(path))
    lines = []
    log = tests.LOG(lambda text: lines.append(text.decode('utf-8', 'replace')))
    v1 = tests.V1(ctypes.sizeof(tests.V1), 1, 1, image.size, image.base, dll._handle,
                  str(path.parent), str(path), b'0.4.4', log)
    v2 = tests.V2(ctypes.sizeof(tests.V2), 2, ctypes.pointer(v1), 1, b'fh2', b'1A96545B',
                  0x565501ed, None, 0, 0, b'0.1.0', None)

    class RuntimeContext(ctypes.Structure):
        _fields_ = [('size', ctypes.c_uint32), ('api_version', ctypes.c_uint32),
                    ('plugin', ctypes.POINTER(tests.V2)), ('game_executable', ctypes.c_wchar_p),
                    ('settings_directory', ctypes.c_wchar_p)]

    ctx = RuntimeContext(ctypes.sizeof(RuntimeContext), 1, ctypes.pointer(v2), str(game), str(root))
    call = dll.ForzaGameRuntimeInitialize
    call.argtypes, call.restype = [ctypes.POINTER(RuntimeContext)], ctypes.c_int
    status = dll.XtraRuntimeStatus
    status.restype = ctypes.c_char_p
    patches = json.loads((RUNTIME / 'src/patches.json').read_text(encoding='utf-8'))
    ownership = json.loads((RUNTIME / 'ownership.json').read_text(encoding='utf-8'))
    moved = [p for p in patches['patches'] if p['runtime_group'] in ownership['individual_plugin_groups']]

    # Ordinary v1/v2 loaders must not run the provider: in stock Extra Options
    # these features already exist. This is also how its own deferred loader
    # can harmlessly encounter XtraRuntime.dll a second time.
    before = digest(image)
    for export, value, context_type in [('FH2PluginInitialize', v1, tests.V1),
                                       ('ForzaPluginInitialize', v2, tests.V2)]:
        ordinary = getattr(dll, export)
        ordinary.argtypes, ordinary.restype = [ctypes.POINTER(context_type)], ctypes.c_int
        check(ordinary(ctypes.byref(value)) and digest(image) == before,
              f'{name}: {export} is harmless under an ordinary loader')
    check(not hasattr(dll, 'ForzaPluginEarlyInitialize'), f'{name}: provider has no recursive early entry point')

    if name == 'unknown':
        pe = struct.unpack('<I', image.read(0x3c, 4))[0]
        image.write(pe + 8, struct.pack('<I', 0x12345678))
        before = digest(image)
        check(call(ctypes.byref(ctx)) == 0 and digest(image) == before,
              'unknown: returns not-applicable without touching another game')
    elif name == 'wrong-package':
        v2.game, v2.game_id, v2.title_id = 2, b'fh2ff', b'5A9771A4'
        check(call(ctypes.byref(ctx)) == 0 and digest(image) == before,
              'wrong-package: authoritative package identity wins over matching FH2 PE stamps')
    elif name == 'invalid-file':
        ctx.game_executable = str(root / 'missing-game.exe')
        check(call(ctypes.byref(ctx)) == -1 and digest(image) == before,
              'invalid-file: claims failure before any game changes')
        check(call(ctypes.byref(ctx)) == -1, 'invalid-file: failed provider cannot silently retry')
    elif name == 'plugin-disabled':
        check(call(ctypes.byref(ctx)) == 1 and digest(image) == before,
              'plugin-disabled: plugin INI disables all runtime hooks when no legacy INI exists')
    elif name == 'owned-corrupt':
        patch = next(p for p in patches['patches'] if p['runtime_group'] == 'PAUSE_GARAGE')
        image.write(patch['rva'], b'\xfe')
        before = digest(image)
        check(call(ctypes.byref(ctx)) == -1 and digest(image) == before,
              'owned-corrupt: rejects altered owned bytes before any transaction writes')
    else:
        ini_before = (root / 'FH2_ExtraOptions.ini').read_bytes()
        check(call(ctypes.byref(ctx)) == 1, f'{name}: provider initializes the real mapped FH2 image ({status().decode()})')
        check((root / 'FH2_ExtraOptions.ini').read_bytes() == ini_before,
              f'{name}: legacy INI precedence preserves settings without rewriting them')
        for group in ('PAUSE_GARAGE', 'INTRO', 'DLC', 'VIP', 'ISLAND_CARS', 'PHOTO_CAMERA'):
            rows = [p for p in patches['patches'] if p['runtime_group'] == group]
            check(rows and all(image.read(p['rva'], len(bytes.fromhex(p['original']))).hex() == p['patched'] for p in rows),
                  f'{name}: {group} owned by the appropriate runtime/individual DLL')
        aero = [p for p in patches['patches'] if p['runtime_group'] == 'AERO_TUNING']
        check(all(image.read(p['rva'], len(bytes.fromhex(p['original']))).hex() ==
                  (p['patched'] if name == 'active-aero' else p['original']) for p in aero),
              f'{name}: Aero Tuning is independently enabled/disabled by its own INI')
        if name == 'active-aero':
            aero_module = ctypes.WinDLL(str(path.parent / 'AeroTuning.dll'))._handle
            for hook in patches['runtime_hooks']:
                if hook['runtime_group'] == 'AERO_TUNING':
                    thunk = image.read(hook['rva'], 12)
                    target = struct.unpack('<Q', thunk[2:10])[0]
                    check(thunk[:2] == b'\x48\xb8' and thunk[10:] == b'\xff\xe0' and
                          0 < target - aero_module < 0x1000000,
                          f"{name}: {hook['id']} points into AeroTuning.dll")
        # A runtime menu refresh must ignore every site owned by a feature
        # DLL, even an unrecognized value: ownership, not byte coincidence.
        marked = moved[0]
        image.write(marked['rva'], b'\xfe')
        snapshots = [(p['rva'], image.read(p['rva'], len(bytes.fromhex(p['original'])))) for p in moved]
        options = options_type()()
        read = dll.Fh2TestReadOptions
        read.argtypes, read.restype = [ctypes.c_wchar_p, ctypes.c_void_p], ctypes.c_int
        check(read(str(root), ctypes.byref(options)), f'{name}: reads unchanged runtime options')
        apply = dll.Fh2TestApply
        apply.argtypes, apply.restype = [ctypes.c_void_p, ctypes.c_void_p], ctypes.c_int
        check(apply(image.base, ctypes.byref(options)), f'{name}: subsequent runtime transaction ignores foreign sites')
        check(all(image.read(rva, len(value)) == value for rva, value in snapshots),
              f'{name}: runtime reapply never validates or restores individual plugin sites')
        after = digest(image)
        check(call(ctypes.byref(ctx)) == 1 and digest(image) == after,
              f'{name}: repeated provider call is idempotent')
    image.free()


def metadata():
    data = json.loads((RUNTIME / 'ownership.json').read_text(encoding='utf-8'))
    groups = set(re.findall(r'GROUP_([A-Z_]+)', (RUNTIME / 'src/extra_options.h').read_text(encoding='utf-8')))
    runtime, individual = set(data['runtime_groups']), set(data['individual_plugin_groups'])
    found = []
    for path in (PLUGIN / 'features').glob('*/feature.json'):
        found.extend(json.loads(path.read_text(encoding='utf-8'))['groups'])
    check(not runtime & individual and runtime | individual == groups,
          'every Extra Options group has exactly one ownership category')
    check(set(found) == individual and len(found) == len(set(found)),
          'each independently owned group belongs to exactly one feature DLL')
    source = (RUNTIME / 'src/patch_engine.c').read_text(encoding='utf-8')
    check(source.count('if (!patch_group_owned(patch->group)) continue;') == 4,
          'validation, page planning and writes all filter by ownership')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sdk', type=Path)
    parser.add_argument('--game', type=Path, default=Path.home() / 'FH2')
    parser.add_argument('--scenario')
    options = parser.parse_args()
    game = options.game / GAME_EXE if options.game.is_dir() else options.game
    if options.scenario:
        scenario(options.output.resolve(), game.resolve(), options.scenario)
        return
    metadata()
    if not game.is_file():
        # As every test that needs the game's files (CI has none): skip, not fail.
        print(f'  skip  runtime scenarios: the FH2 executable is not at {game} (--game)')
        return
    failures = []
    for name in ('unknown', 'wrong-package', 'invalid-file', 'plugin-disabled', 'owned-corrupt', 'active', 'active-aero'):
        run = subprocess.run([sys.executable, '-I', str(Path(__file__).resolve()), '--output', str(options.output.resolve()),
                              '--game', str(game.resolve()), '--scenario', name], capture_output=True, text=True, timeout=180)
        print(run.stdout, end='')
        if run.returncode:
            failures.append(name)
            print(run.stderr or f'{name}: subprocess exit {run.returncode:#x}')
    if failures:
        sys.exit('runtime tests failed: ' + ', '.join(failures))
    print('all runtime-provider tests passed')


if __name__ == '__main__':
    main()
