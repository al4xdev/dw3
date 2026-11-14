#!/usr/bin/env python3
"""check_release_contents.py: a release package holds our programs, the mods' manifests, licences and a README, and
nothing else (.github/workflows/release.yml runs it on every package before it is kept or published).

The player supplies the disc. A CI runner with the maintainers' data checkout has the disc image in iso/ and its files
in extracted/ while it builds, so a packaging mistake (a glob too wide, a mod that ships a file made from the disc)
could put game data on a public release page. Every file of a package must match the list below, so a new kind of
file fails here until someone decides it belongs in a release; a mod's directory may hold only its mod.json (a text
pack or texture made from the disc is game data). The size cap catches a disc track or an extracted movie under an
allowed name.

Usage: scripts/check_release_contents.py appimage DIR        an AppDir, or an extracted AppImage (squashfs-root)
       scripts/check_release_contents.py windows ZIP [SYMBOLS_ZIP]
       scripts/check_release_contents.py portmaster ZIP    the PortMaster ARM zip (dw3.zip)
       scripts/check_release_contents.py dist DIR            the release's assets: their names and all zips' contents
Exit 0 when every file is allowed, 1 otherwise (each offending path printed).
"""
import re
import sys
import zipfile
from pathlib import Path

# A program, a manifest or a licence is a few MB; the smallest disc track or movie is far larger.
MAX_FILE = 64 * 1024 * 1024
MOD = r"mods/[a-z0-9_]+/mod\.json"
LICENCE = r"LICENSES/[A-Za-z0-9._-]+\.txt"
APPIMAGE = [r"AppRun", r"\.DirIcon", r"dw2003recomp\.desktop", r"dw2003recomp\.svg", r"usr/bin/dw2003",
            r"usr/bin/dw2003-launcher", r"usr/bin/" + MOD, r"usr/share/applications/dw2003recomp\.desktop",
            r"usr/share/icons/hicolor/scalable/apps/dw2003recomp\.svg", LICENCE]
WINDOWS = [r"dw2003\.exe", r"dw2003-launcher\.exe", r"README\.txt", MOD, LICENCE]
SYMBOLS = [r"dw2003\.pdb", r"dw2003-launcher\.pdb", r"README\.txt"]
PORTMASTER = [r"Digimon World 3\.sh", r"port\.json", r"README\.md", r"gameinfo\.xml", r"screenshot\.png",
              r"cover\.png", r"dw3/dw2003", r"dw3/dw3-menu\.py", r"dw3/ledd\.py", r"dw3/probe\.sh",
              r"dw3/README\.txt", r"dw3/" + MOD, r"dw3/licenses/[A-Za-z0-9._-]+"]
VERSION = r"[A-Za-z0-9._+-]+"
ASSETS = [rf"dw2003-{VERSION}-x86_64\.AppImage", rf"dw2003-{VERSION}-x86_64\.debug", r"SHA256SUMS",
          rf"dw2003-{VERSION}-windows-x86_64\.zip", rf"dw2003-{VERSION}-windows-x86_64-symbols\.zip",
          r"SHA256SUMS-windows", r"dw3\.zip", r"SHA256SUMS-portmaster"]


def allowed(rel, patterns):
    return any(re.fullmatch(p, rel) for p in patterns)


def check_tree(root, patterns, what):
    bad = []
    files = [p for p in sorted(Path(root).rglob("*")) if p.is_file() or p.is_symlink()]
    for p in files:
        rel = p.relative_to(root).as_posix()
        if not allowed(rel, patterns):
            bad.append(f"{what}: {rel}: not a file a release holds")
        elif not p.is_symlink() and p.stat().st_size > MAX_FILE:
            bad.append(f"{what}: {rel}: {p.stat().st_size} bytes, over the {MAX_FILE} cap")
    if not files:
        bad.append(f"{what}: {root} is empty")
    return bad, len(files)


def check_zip(path, patterns, what, cap=True, top_dir=True):
    """Every entry (under the zip's top folder when top_dir=True, or at root for PortMaster) must be allowed."""
    bad, n = [], 0
    top = Path(path).name[:-len(".zip")]
    with zipfile.ZipFile(path) as z:
        for info in z.infolist():
            if info.is_dir():
                continue
            n += 1
            name = info.filename
            if top_dir:
                if not name.startswith(top + "/"):
                    bad.append(f"{what}: {name}: outside the zip's folder {top}/")
                    continue
                rel = name[len(top) + 1:]
            else:
                rel = name
            if not allowed(rel, patterns):
                bad.append(f"{what}: {name}: not a file a release holds")
            elif cap and info.file_size > MAX_FILE:
                bad.append(f"{what}: {name}: {info.file_size} bytes, over the {MAX_FILE} cap")
    if n == 0:
        bad.append(f"{what}: {path} has no files")
    return bad, n


def main(argv):
    if len(argv) < 3 or argv[1] not in ("appimage", "windows", "portmaster", "dist"):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    kind, bad, said = argv[1], [], []
    if kind == "appimage":
        b, n = check_tree(argv[2], APPIMAGE, "AppImage")
        bad += b
        said.append(f"AppImage: {n} files")
    elif kind == "windows":
        b, n = check_zip(argv[2], WINDOWS, "Windows zip")
        bad += b
        said.append(f"{Path(argv[2]).name}: {n} files")
        if len(argv) > 3:
            b, n = check_zip(argv[3], SYMBOLS, "symbols zip", cap=False)   # the PDBs: debug info, not played
            bad += b
            said.append(f"{Path(argv[3]).name}: {n} files")
    elif kind == "portmaster":
        b, n = check_zip(argv[2], PORTMASTER, "PortMaster zip", top_dir=False)
        bad += b
        said.append(f"{Path(argv[2]).name}: {n} files")
    else:
        dist = Path(argv[2])
        names = sorted(p.name for p in dist.iterdir())
        for name in names:
            if not allowed(name, ASSETS):
                bad.append(f"release: {name}: not an asset a release holds")
        for pattern in ASSETS:
            if not any(re.fullmatch(pattern, name) for name in names):
                bad.append(f"release: no asset matching {pattern}")
        for name in names:
            if name.endswith("-windows-x86_64.zip"):
                b, n = check_zip(dist / name, WINDOWS, name)
            elif name.endswith("-windows-x86_64-symbols.zip"):
                b, n = check_zip(dist / name, SYMBOLS, name, cap=False)
            elif name == "dw3.zip":
                b, n = check_zip(dist / name, PORTMASTER, name, top_dir=False)
            else:
                continue
            bad += b
            said.append(f"{name}: {n} files")
        said.append(f"{len(names)} assets")
    for line in bad:
        print(line, file=sys.stderr)
    if bad:
        print("check_release_contents: the files above must not be in a release (the player brings the disc; a new "
              "kind of file needs a decision and a line in this script)", file=sys.stderr)
        return 1
    print("check_release_contents: " + "; ".join(said) + ": only programs, manifests, licences and READMEs")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
