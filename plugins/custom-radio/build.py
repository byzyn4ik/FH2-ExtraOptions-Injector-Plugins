"""Build the Custom Radio plugin for Forza Horizon 2 Extra Options.

Usage: py plugins/custom-radio/build.py [--zig <zig.exe>] [--output out] [--test] [--game <FH2 folder>]
(the top-level build.py runs it with --output out/build/custom-radio)

Writes:
  <output>/mods/Custom Radio/mod.json
  <output>/mods/Custom Radio/plugins/CustomRadio.dll
  <output>/mods/Custom Radio/plugins/CustomRadio.ini
  <output>/mods/Custom Radio/plugins/CustomRadio/Media/UI/Textures/Anthem/HUD/HUD_RadioIcon_CustomRadio*.swatchbin
                                           (the station logos, from logo/*.png; mod.json names the folder as media)
  <output>/mods/Custom Radio/media/UI/Textures/Anthem/HUD/...  (the same logos, for Fast & Furious, where
                                           Language Support mounts a mod's media folder)
  <output>/obj/CustomRadio-test.dll        (the same code with test exports)
--test also runs tests/run_tests.py on the result: with the game executables
(Forza Horizon 2 and Fast & Furious) and Extra Options 0.4.4's release below
--game (default ~/FH2) when they are there (else those checks skip;
--game-exe, --ff-exe and --extra-options name them directly). Nothing is installed.
Zig 0.15.2: --zig, else the ZIG environment variable, else zig on PATH.

Like Extra Options' own plugins, the DLL is plain C without a C runtime of
its own: it links only import libraries made here from Windows' kernel32,
user32 and msvcrt (msacm32 is loaded when MP3s are decoded).
"""

import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "common"))
import build_common as bc  # noqa: E402

SRC = HERE / "src"
MOD_NAME = "Custom Radio"
DLL_NAME = "CustomRadio.dll"
INI_NAME = "CustomRadio.ini"
UNITS = ["support.c", "config.c", "hooks.c", "mp3.c", "media.c", "radio.c", "texture.c", "station_logo.c", "ff_tick.c", "plugin.c"]
SYSTEM_LIBRARIES = ["kernel32", "user32", "msvcrt"]
GAME_EXE = Path("Anthem_1.0.0.64_x64__8wekyb3d8bbwe/Mount/forza_durango_release_final.exe")
FF_EXE = Path("Forza Horizon 2 Presents Fast & Furious/Spire_1.0.0.6_x64__8wekyb3d8bbwe/Mount/"
              "forza_durango_release_final.exe")
EO_RELEASE = Path("src/eo-pr/upstream-0.4.4/FH2_Extra_Options_0.4.4-azimuth/Azimuth")
# The station logos (tools/make_logo.py draws them): source PNG -> the texture
# name the HUD's radio display asks for (src/station_logo.c).
LOGOS = {"my_music.png": "HUD_RadioIcon_CustomRadio", "moja_muzyka.png": "HUD_RadioIcon_CustomRadio_PL",
         "moya_muzyka.png": "HUD_RadioIcon_CustomRadio_UK", "badge.png": "HUD_RadioIcon_CustomRadio_Badge"}
LOGO_FOLDER = Path("CustomRadio/Media/UI/Textures/Anthem/HUD")  # below the mod's plugins folder
LOGO_PNGS = {"my_music.png": "Logo.png", "moja_muzyka.png": "Logo_PL.png", "moya_muzyka.png": "Logo_UK.png",
             "badge.png": "Logo_Badge.png"}

sys.path.insert(0, str(HERE / "tools"))
import logo_texture  # noqa: E402


def dll_exports(path):
    """Exported names of a DLL (for its import library)."""
    data = Path(path).read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    optional = pe + 24
    section_table = optional + struct.unpack_from("<H", data, pe + 20)[0]
    sections = [struct.unpack_from("<IIII", data, section_table + 40 * i + 8)
                for i in range(struct.unpack_from("<H", data, pe + 6)[0])]

    def offset(rva):
        for _, va, size, raw in sections:
            if va <= rva < va + size:
                return raw + rva - va
        raise ValueError(hex(rva))

    directory = offset(struct.unpack_from("<I", data, optional + 112)[0])
    count, names = struct.unpack_from("<I", data, directory + 24)[0], struct.unpack_from("<I", data, directory + 32)[0]
    table = offset(names)
    result = []
    for i in range(count):
        at = offset(struct.unpack_from("<I", data, table + 4 * i)[0])
        result.append(data[at:data.index(b"\0", at)].decode("ascii"))
    return result


def import_libraries(zig, build):
    system = Path(os.environ.get("WINDIR", r"C:\Windows")) / "System32"
    libraries = []
    for name in SYSTEM_LIBRARIES:
        definition = build / f"{name}.def"
        library = build / f"{name}.lib"
        text = f"LIBRARY {name}.dll\nEXPORTS\n" + "\n".join(dll_exports(system / f"{name}.dll")) + "\n"
        if not library.is_file() or not definition.is_file() or definition.read_text() != text:
            definition.write_text(text)
            bc.run([zig, "dlltool", "-m", "i386:x86-64", "-d", definition, "-l", library])
        libraries.append(library)
    return libraries


def version_resource(zig, build):
    """obj/version.res: the DLL's version resource, from mod/mod.json's version."""
    rc = bc.version_rc(build / "version.rc", bc.mod_version(HERE / "mod" / "mod.json"), DLL_NAME,
                       "Custom Radio (Extra Options plugin)", "Custom Radio for Forza Horizon 2")
    res = build / "version.res"
    bc.run([zig, "rc", "/nologo", "/fo", res, "--", rc])
    return res


def write_logos(plugins):
    """The logo PNGs as the game's textures (premultiplied BC7 swatchbins with its drop shadow, as its own logos), in the plugin's media folder."""
    folder = plugins / LOGO_FOLDER
    folder.mkdir(parents=True, exist_ok=True)
    written = []
    for source, texture in LOGOS.items():
        width, height, rgba = logo_texture.read_png(HERE / "logo" / source)
        if (width, height) != (196, 96):
            raise SystemExit(f"logo/{source} is {width}x{height}; the HUD's station logos are 196x96")
        path = folder / f"{texture}.swatchbin"
        pixels = logo_texture.shadow(logo_texture.premultiply(rgba), width, height)
        path.write_bytes(logo_texture.encode(pixels, width, height))
        written.append(path)
    return written


def build_dll(zig, build, libraries, output, test):
    include = zig.parent / "lib"
    objects = []
    for unit in UNITS:
        obj = build / (unit + (".test.obj" if test else ".obj"))
        command = [zig, "clang", "-target", "x86_64-w64-windows-gnu", "-c", "-ffreestanding", "-fno-builtin",
                   "-fno-stack-protector", "-fms-extensions", "-funwind-tables", "-mcx16", "-O2",
                   "-D__USE_MINGW_ANSI_STDIO=0", "-DUNICODE", "-D_UNICODE",
                   "-Wall", "-Wextra", "-Wno-missing-field-initializers", "-Werror",
                   "-isystem", include / "include", "-isystem", include / "libc" / "include" / "any-windows-any",
                   "-I", bc.INCLUDE]
        if test:
            command.append("-DCUSTOM_RADIO_TEST=1")
        bc.run(command + [SRC / unit, "-o", obj])
        objects.append(obj)
    bc.run([zig, "lld-link", "/dll", "/entry:DllMain", "/machine:x64", "/nodefaultlib", "/dynamicbase",
            "/nxcompat", "/Brepro", "/noimplib", "/out:" + str(output), *objects, *libraries])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--zig", help="zig.exe (0.15.2)")
    parser.add_argument("--output", type=Path, default=HERE / "out")
    parser.add_argument("--test", action="store_true", help="run the offline tests after building")
    parser.add_argument("--game", type=Path, default=Path.home() / "FH2", help="tests: the FH2 game folder")
    parser.add_argument("--game-exe", type=Path, help="tests: forza_durango_release_final.exe")
    parser.add_argument("--extra-options", type=Path, help="tests: Extra Options 0.4.4's release folder")
    parser.add_argument("--ff-exe", type=Path, help="tests: Fast & Furious' forza_durango_release_final.exe")
    options = parser.parse_args(argv)
    zig = bc.find_zig(options.zig)
    output = options.output.resolve()
    build = output / "obj"
    mod = output / "mods" / MOD_NAME
    plugins = mod / "plugins"
    for folder in (build, plugins):
        folder.mkdir(parents=True, exist_ok=True)
    libraries = import_libraries(zig, build) + [version_resource(zig, build)]
    build_dll(zig, build, libraries, plugins / DLL_NAME, test=False)
    build_dll(zig, build, libraries, build / "CustomRadio-test.dll", test=True)
    shutil.copy2(HERE / "mod" / "mod.json", mod / "mod.json")
    shutil.copy2(HERE / "mod" / INI_NAME, plugins / INI_NAME)
    shutil.rmtree(plugins / "CustomRadio", ignore_errors=True)
    logos = write_logos(plugins)
    # Fast & Furious: Language Support mounts a mod's media\ folder there (not mod.json's "media"),
    # so the logos are also in mods\Custom Radio\media (Forza Horizon 2 reads plugins\CustomRadio\Media).
    shutil.rmtree(mod / "media", ignore_errors=True)
    ff_media = mod / "media" / LOGO_FOLDER.relative_to("CustomRadio/Media")
    ff_media.mkdir(parents=True)
    for logo in logos:
        shutil.copy2(logo, ff_media / logo.name)
    # The logos' sources beside the plugin: the plugin makes them into the textures when it starts.
    for source, name in LOGO_PNGS.items():
        shutil.copy2(HERE / "logo" / source, plugins / name)
    for path in (mod / "mod.json", plugins / DLL_NAME, plugins / INI_NAME, *logos, build / "CustomRadio-test.dll"):
        print(path)
    if options.test:
        command = [sys.executable, str(HERE / "tests" / "run_tests.py"), "--output", str(output)]
        game_exe = options.game_exe or options.game / GAME_EXE
        release = options.extra_options or options.game / EO_RELEASE
        if game_exe.is_file():
            command += ["--game-exe", str(game_exe)]
        ff_exe = options.ff_exe or options.game / FF_EXE
        if ff_exe.is_file():
            command += ["--ff-exe", str(ff_exe)]
        if (release / "winmm.dll").is_file():
            command += ["--extra-options", str(release)]
        subprocess.run(command, check=True)


if __name__ == "__main__":
    main()
