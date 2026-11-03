#!/bin/bash
# Proves that check_pkg.sh passes a good package and rejects broken ones, and that build_pkg.sh refuses bad input.
# usage: test_package.sh <artefacts dir>   (the folder holding VST3/, AU/ and Standalone/)
set -uo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: test_package.sh <artefacts dir>" >&2
    exit 2
fi

here=$(cd "$(dirname "$0")" && pwd)
artefacts=$(cd "$1" && pwd)
build="$here/build_pkg.sh"
check="$here/check_pkg.sh"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
failures=0

pass() { echo "PASS $1"; }
fail() { echo "FAIL $1: $2"; failures=$((failures + 1)); }

# Builds the three component packages with pkgbuild and a component plist that makes each bundle relocatable, and a
# product from them, leaving out the formats named after the output path.
plain_product() {
    local out=$1; shift
    local work="$tmp/plain-$RANDOM"
    mkdir -p "$work/components" "$work/resources"
    local -a rows=(
        "VST3 Drosera.vst3 com.codeskine.drosera.vst3 /Library/Audio/Plug-Ins/VST3"
        "AU Drosera.component com.codeskine.drosera.au /Library/Audio/Plug-Ins/Components"
        "Standalone Drosera.app com.codeskine.drosera.app /Applications")
    local row format bundle id location skip
    for row in "${rows[@]}"; do
        read -r format bundle id location <<< "$row"
        skip=0
        for left_out in "$@"; do [ "$left_out" = "$format" ] && skip=1; done
        [ "$skip" = 1 ] && continue
        mkdir -p "$work/root-$format"
        ditto "$artefacts/$format/$bundle" "$work/root-$format/$bundle"
        sed -e "s/@BUNDLE@/$bundle/" -e '/BundleIsRelocatable/{n;s/false/true/;}' \
            "$here/resources/components.plist" > "$work/components-$format.plist"
        pkgbuild --quiet --root "$work/root-$format" --component-plist "$work/components-$format.plist" \
            --identifier "$id" --version 0.0.0 --install-location "$location" "$work/components/Drosera-$format.pkg"
    done
    cp "$here"/resources/*.html "$work/resources/"
    echo "<html><body>test</body></html>" > "$work/resources/readme.html"
    local distribution="$work/distribution.xml"
    sed 's/@VERSION@/0.0.0/g' "$here/resources/distribution.xml" > "$distribution"
    for left_out in "$@"; do
        [ "$left_out" = "Standalone" ] \
            && sed -i '' -E -e '/<choice id="app"/,/<\/choice>/d' -e '/drosera\.app|choice="app"/d' "$distribution"
    done
    productbuild --quiet --distribution "$distribution" --resources "$work/resources" \
        --package-path "$work/components" "$out"
}

# good
good="$tmp/Drosera.pkg"
if "$build" "$artefacts" 0.0.0-test "$good" > "$tmp/good.log" 2>&1 && "$check" "$good" >> "$tmp/good.log" 2>&1; then
    pass good
else
    fail good "$(tail -5 "$tmp/good.log")"
fi

# spaces_and_rc
mkdir -p "$tmp/arte facts" "$tmp/out dir"
ditto "$artefacts" "$tmp/arte facts"
spaced="$tmp/out dir/Drosera-0.1.1-rc1-macos.pkg"
if "$build" "$tmp/arte facts" 0.1.1-rc1 "$spaced" > "$tmp/spaced.log" 2>&1 \
    && "$check" "$spaced" >> "$tmp/spaced.log" 2>&1; then
    pass spaces_and_rc
else
    fail spaces_and_rc "$(tail -5 "$tmp/spaced.log")"
fi

# readme_escapes_html
if [ -f "$good" ] && pkgutil --expand "$good" "$tmp/good-expanded" > /dev/null 2>&1 \
    && grep -q '&lt;https://www.gnu.org/licenses/&gt;' "$tmp/good-expanded/Resources/readme.html" \
    && ! grep -q '<https://www.gnu.org/licenses/>' "$tmp/good-expanded/Resources/readme.html"; then
    pass readme_escapes_html
else
    fail readme_escapes_html "the Read Me does not hold the escaped AGPL address"
fi

# missing_au
mkdir -p "$tmp/no-au"
ditto "$artefacts/VST3" "$tmp/no-au/VST3"
ditto "$artefacts/Standalone" "$tmp/no-au/Standalone"
if "$build" "$tmp/no-au" 0.0.0 "$tmp/no-au.pkg" > /dev/null 2> "$tmp/no-au.err"; then
    fail missing_au "build_pkg.sh succeeded without the AU"
elif ! grep -q 'Drosera.component' "$tmp/no-au.err"; then
    fail missing_au "the error does not name Drosera.component: $(cat "$tmp/no-au.err")"
elif [ -e "$tmp/no-au.pkg" ]; then
    fail missing_au "a package was written anyway"
else
    pass missing_au
fi

# relocatable_rejected
relocatable="$tmp/relocatable.pkg"
if ! plain_product "$relocatable" > "$tmp/relocatable.log" 2>&1; then
    fail relocatable_rejected "could not build the test package: $(tail -3 "$tmp/relocatable.log")"
elif "$check" "$relocatable" > /dev/null 2> "$tmp/relocatable.err"; then
    fail relocatable_rejected "check_pkg.sh accepted relocatable bundles"
elif ! grep -q 'relocatable' "$tmp/relocatable.err"; then
    fail relocatable_rejected "the error does not say relocatable: $(cat "$tmp/relocatable.err")"
else
    pass relocatable_rejected
fi

# broken_seal_rejected
mkdir -p "$tmp/broken"
ditto "$artefacts" "$tmp/broken"
printf 'x' >> "$tmp/broken/VST3/Drosera.vst3/Contents/Resources/moduleinfo.json"
if ! "$build" "$tmp/broken" 0.0.0 "$tmp/broken.pkg" > "$tmp/broken.log" 2>&1; then
    fail broken_seal_rejected "could not build the test package: $(tail -3 "$tmp/broken.log")"
elif "$check" "$tmp/broken.pkg" > /dev/null 2> "$tmp/broken.err"; then
    fail broken_seal_rejected "check_pkg.sh accepted a broken seal"
elif ! grep -q 'codesign' "$tmp/broken.err"; then
    fail broken_seal_rejected "the error does not mention codesign: $(cat "$tmp/broken.err")"
else
    pass broken_seal_rejected
fi

# two_components_rejected
two="$tmp/two.pkg"
if ! plain_product "$two" Standalone > "$tmp/two.log" 2>&1; then
    fail two_components_rejected "could not build the test package: $(tail -3 "$tmp/two.log")"
elif "$check" "$two" > /dev/null 2>&1; then
    fail two_components_rejected "check_pkg.sh accepted a package without the Standalone app"
else
    pass two_components_rejected
fi

[ "$failures" -eq 0 ]
