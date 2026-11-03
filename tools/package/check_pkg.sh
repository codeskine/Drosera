#!/bin/bash
# Checks Drosera's installer package without installing it.
# usage: check_pkg.sh <package.pkg>
#   PLUGIN_HOST_TEST (default build/plugin_host_test_artefacts/Release/plugin_host_test) and
#   STATE_FIXTURE (default tests/fixtures/state-baseline.bin) load the packaged VST3.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: check_pkg.sh <package.pkg>" >&2
    exit 2
fi

repo=$(cd "$(dirname "$0")/../.." && pwd)
package=$1
host_test=${PLUGIN_HOST_TEST:-$repo/build/plugin_host_test_artefacts/Release/plugin_host_test}
fixture=${STATE_FIXTURE:-$repo/tests/fixtures/state-baseline.bin}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
expanded="$work/expanded"
pkgutil --expand-full "$package" "$expanded" > /dev/null

failures=0
fail() { echo "check_pkg: $1" >&2; failures=$((failures + 1)); }

# identifier, install location, bundle
expected=(
    "com.codeskine.drosera.vst3 /Library/Audio/Plug-Ins/VST3 Drosera.vst3"
    "com.codeskine.drosera.au /Library/Audio/Plug-Ins/Components Drosera.component"
    "com.codeskine.drosera.app /Applications Drosera.app")

components=("$expanded"/*.pkg)
if [ "${#components[@]}" -ne 3 ] || [ ! -d "${components[0]}" ]; then
    fail "expected 3 component packages, found ${#components[@]}"
fi

for row in "${expected[@]}"; do
    read -r identifier location bundle <<< "$row"
    found=""
    for component in "${components[@]}"; do
        [ -f "$component/PackageInfo" ] || continue
        if grep -q "identifier=\"$identifier\"" "$component/PackageInfo"; then found=$component; fi
    done
    if [ -z "$found" ]; then
        fail "no component package $identifier"
        continue
    fi
    grep -q "install-location=\"$location\"" "$found/PackageInfo" \
        || fail "$identifier does not install to $location"
    grep -q "<relocate>" "$found/PackageInfo" && fail "$identifier has a relocatable bundle"
    if [ ! -d "$found/Payload/$bundle" ]; then
        fail "$identifier does not hold $bundle"
        continue
    fi
    codesign --verify --strict "$found/Payload/$bundle" 2> "$work/codesign.err" \
        || fail "codesign rejects $bundle: $(tr '\n' ' ' < "$work/codesign.err")"
done

distribution="$expanded/Distribution"
grep -q 'hostArchitectures="arm64,x86_64"' "$distribution" || fail "Distribution does not list arm64 and x86_64"
grep -q 'min="11.0"' "$distribution" || fail "Distribution does not require macOS 11"

vst3=$(find "$expanded" -path '*/Payload/Drosera.vst3' -maxdepth 3 | head -1)
if [ -n "$vst3" ]; then
    "$host_test" "$vst3" "$fixture" > "$work/host.log" 2>&1 \
        || fail "plugin_host_test fails on the packaged VST3: $(tail -3 "$work/host.log" | tr '\n' ' ')"
fi

if [ "$failures" -ne 0 ]; then
    exit 1
fi
echo "check_pkg: $package is fine"
