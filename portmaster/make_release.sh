#!/bin/bash
# make_release.sh: assemble the PortMaster release of this port, in the layout
# https://portmaster.games/packaging.html asks for, and check it before it is offered:
#
#   dist/dw3.zip
#     Digimon World 3.sh      the launcher (handheld/, versioned with the port)
#     port.json               the metadata (this directory)
#     README.md               the wiki page's notes (this directory)
#     gameinfo.xml            EmulationStation's entry (this directory)
#     screenshot.png          640x480, gameplay (this directory; capture it on the device)
#     dw3/                    the port: the binary, the menu and the scripts (handheld/)
#       licenses/             what the port borrows and its licences
#
# It refuses to build when one of those is missing, when the port directory's name differs from the port's, or when the
# binary is not the ARM 32-bit build the package ships. Nothing of the game's data is ever included: the user supplies
# the disc.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
NAME="dw3"                             # the port's name: dist/<name>.zip, ports/<name>/, ports/<name>/<name>/
SCRIPT="Digimon World 3.sh"
BINARY="$REPO/build/armhf-sdl2/dw2003"
DIST="$REPO/dist"
OUT="$DIST/$NAME"

fail() { echo "make_release: $*" >&2; exit 1; }

[ -f "$BINARY" ] || fail "the binary is not built: $BINARY (run tools/build_arm.sh)"
file "$BINARY" | grep -q "ELF 32-bit LSB.*ARM" || fail "$BINARY is not a 32-bit ARM ELF"
for f in port.json README.md gameinfo.xml screenshot.png; do
    [ -f "$HERE/$f" ] || fail "missing $HERE/$f (the screenshot is captured on the device, 640x480 of gameplay)"
done
[ -f "$REPO/handheld/$SCRIPT" ] || fail "missing $REPO/handheld/$SCRIPT"

echo "=== $NAME: assembling $OUT"
rm -rf "$OUT"
mkdir -p "$OUT/$NAME/licenses"
cp "$HERE/port.json" "$HERE/README.md" "$HERE/gameinfo.xml" "$HERE/screenshot.png" "$OUT/"
[ -f "$HERE/cover.png" ] && cp "$HERE/cover.png" "$OUT/"   # optional: the catalogue shows the screenshot without it
cp "$REPO/handheld/$SCRIPT" "$OUT/$SCRIPT"
chmod +x "$OUT/$SCRIPT"
# Everything in handheld/ that belongs on the card, by rule rather than by list: a list here drops the next file
# somebody adds (a LED daemon, a helper), and the zip would be silently incomplete. README.md is the repository's own
# document and the launcher sits at the package's root, above.
for f in "$REPO"/handheld/*; do
    case "$(basename "$f")" in
        README.md|"$SCRIPT"|__pycache__) continue ;;
    esac
    cp "$f" "$OUT/$NAME/"
done
chmod -x "$OUT/$NAME/README.txt" 2>/dev/null || true
chmod +x "$OUT/$NAME/dw3.sh" "$OUT/$NAME/dw3-tui.sh" "$OUT/$NAME/dw3-menu.py"
[ -f "$OUT/$NAME/ledd.py" ] && chmod +x "$OUT/$NAME/ledd.py"
cp "$BINARY" "$OUT/$NAME/dw2003"
chmod +x "$OUT/$NAME/dw2003"

# The licences of what the port contains or links against, kept as files in portmaster/licenses/ (a text to maintain,
# not a heredoc to keep in step): the project's own MIT, SDL2's zlib, and what the package borrows. The game's data is
# not here, and its decompilation is not redistributed in this zip either.
cp "$HERE"/licenses/* "$OUT/$NAME/licenses/"

cd "$DIST"
rm -f "$NAME.zip"
# python's zipfile rather than zip(1), which this machine does not have: the archive is the tree as it is, compressed.
python3 - "$NAME" <<'PY'
import os
import sys
import zipfile

name = sys.argv[1]
with zipfile.ZipFile(name + ".zip", "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, _dirs, files in os.walk(name):
        for f in sorted(files):
            path = os.path.join(root, f)
            info = zipfile.ZipInfo.from_file(path, path)
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(path, "rb") as src:
                z.writestr(info, src.read())
print("zip ok:", name + ".zip")
PY

echo "=== checks"
python3 - "$DIST/$NAME.zip" <<'PY'
import json
import sys
import zipfile

z = zipfile.ZipFile(sys.argv[1])
names = z.namelist()
with z.open("dw3/port.json") as f:
    port = json.load(f)
missing = [n for n in ("dw3/Digimon World 3.sh", "dw3/port.json", "dw3/README.md", "dw3/gameinfo.xml",
                       "dw3/screenshot.png", "dw3/dw3/dw2003", "dw3/dw3/dw3.sh", "dw3/dw3/dw3-menu.py")
           if n not in names]
if missing:
    sys.exit("missing from the zip: " + ", ".join(missing))
if port["name"] != "dw3.zip" or port["items"] != ["Digimon World 3.sh", "dw3"]:
    sys.exit("port.json does not describe this layout: %r / %r" % (port["name"], port["items"]))
banned = [n for n in names if n.lower().endswith((".bin", ".cue", ".iso", ".chd", ".mcd", ".img"))]
if banned:
    sys.exit("the package carries the game's data: " + ", ".join(banned))
print("zip ok: %d files, port.json %s, arch %s" % (len(names), port["name"], port["attr"]["arch"]))
PY

md5sum "$DIST/$NAME.zip"
du -h "$DIST/$NAME.zip"
echo "=== $NAME ready: publish it as ports/$NAME/ in PortMaster-MV-New (a decomp port) — see README.md"
