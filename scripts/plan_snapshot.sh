#!/bin/sh
# plan_snapshot.sh: copy the .plan task files into the repository as working notes.
#
# The .plan tool keeps its directory at the workspace root and refuses a symlink there, so the live copy cannot live
# inside the checkout. This snapshot puts it there anyway - .gitignore ignores .plan/, so it is never committed, but the
# notes travel with the project folder instead of sitting in a directory that only the tool knows about.
#
# Run it after the plan changes (a staged revision, a closure). It is a copy, not a link: the tool's directory is the
# truth, and the file dates here say how fresh the snapshot is.
set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"        # the dw2003recomp checkout
LIVE="$(cd "$REPO/.." && pwd)/.plan"            # the tool's directory, beside the checkout
SNAP="$REPO/.plan"

if [ ! -d "$LIVE" ]; then
    echo "plan_snapshot: no $LIVE (nothing to snapshot)" >&2
    exit 1
fi
if [ "$(cd "$LIVE" && pwd -P)" = "$(cd "$SNAP" 2>/dev/null && pwd -P || echo)" ]; then
    echo "plan_snapshot: $LIVE is $SNAP already"
    exit 0
fi

mkdir -p "$SNAP"
# The state directory holds the tool's own database: a copy of a live SQLite file is not worth keeping.
rm -rf "$SNAP/closed" "$SNAP/staging" "$SNAP/tasks" "$SNAP/backlog" "$SNAP/blocked" "$SNAP/para-o-dono"
for d in closed staging tasks backlog blocked para-o-dono; do
    [ -d "$LIVE/$d" ] && cp -a "$LIVE/$d" "$SNAP/"
done
ls -1 "$SNAP"/*/*.md 2>/dev/null | wc -l | tr -d ' ' | {
    read -r n
    echo "plan_snapshot: $n plan files in $SNAP (ignored by git)"
}
