#!/bin/sh
# Renders the reference presets and checks them against reference/SHA256SUMS.
# usage: check_reference.sh <render_cli path>
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: check_reference.sh <render_cli path>" >&2
    exit 2
fi

here=$(cd "$(dirname "$0")" && pwd)
render_cli=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

"$render_cli" "$here/presets.txt" "$out" 20 > /dev/null
cd "$out"
shasum -a 256 -c "$here/reference/SHA256SUMS"
