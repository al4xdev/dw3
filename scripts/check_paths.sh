#!/bin/sh
# check_paths.sh: nothing in the tree may carry a path from somebody's machine.
#
# A repository is judged by what it assumes. A home directory, a mount point or a drive letter with a real name in it
# works for exactly one person and silently breaks everybody else - and a machine-readable tree (docs an assistant
# reads, scripts a contributor runs) is where that damage spreads fastest, because the path looks like an example.
# Placeholders are fine and are what the docs use: <name>, <you>, $HOME, ~, /roms, C:\Users\<name>.
#
#   scripts/check_paths.sh      exit 1, printing the offending lines
#
# Excluded: this file (its patterns are the thing it searches for) and the game's own decompiled strings, where a
# "C:\DEVELOP\..." is data from the original disc, not a path of ours.
set -eu
cd "$(dirname "$0")/.."

# A home directory with a name (Linux, macOS), a WSL or mounted drive, a Windows profile, a personal scratch dir.
patterns='/home/[a-z0-9_][a-z0-9_.-]*/'
patterns="$patterns|/Users/[A-Za-z][A-Za-z0-9_.-]*/"
patterns="$patterns|/mnt/[c-z]/"
patterns="$patterns|C:\\\\Users\\\\[A-Za-z0-9]"
patterns="$patterns|/media/[a-z0-9_][a-z0-9_.-]*/"

if git grep -nIE "$patterns" -- . ':!scripts/check_paths.sh'; then
    echo >&2
    echo "check_paths: the lines above carry a path from a specific machine." >&2
    echo "Rewrite them as placeholders (<name>, \$HOME, /roms) or make them relative to the checkout." >&2
    exit 1
fi
echo "check_paths: clean"
