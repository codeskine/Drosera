#!/bin/bash
# Builds Drosera's installer package: one component package per format, combined under distribution.xml.
# usage: build_pkg.sh <artefacts dir> <version> <output.pkg>
#   <artefacts dir> holds VST3/Drosera.vst3, AU/Drosera.component and Standalone/Drosera.app,
#   like build/Drosera_artefacts/Release.
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "usage: build_pkg.sh <artefacts dir> <version> <output.pkg>" >&2
    exit 2
fi

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
resources="$here/resources"
artefacts=$1
version=$2
output=$3

# format, bundle, identifier, install location
formats=(
    "VST3 Drosera.vst3 com.codeskine.drosera.vst3 /Library/Audio/Plug-Ins/VST3"
    "AU Drosera.component com.codeskine.drosera.au /Library/Audio/Plug-Ins/Components"
    "Standalone Drosera.app com.codeskine.drosera.app /Applications")

missing=0
for row in "${formats[@]}"; do
    read -r format bundle _ _ <<< "$row"
    if [ ! -d "$artefacts/$format/$bundle" ]; then
        echo "build_pkg: missing $artefacts/$format/$bundle" >&2
        missing=1
    fi
done
[ "$missing" -eq 0 ] || exit 1

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/components" "$work/screens"

for row in "${formats[@]}"; do
    read -r format bundle _ _ <<< "$row"
    mkdir -p "$work/root-$format"
    ditto "$artefacts/$format/$bundle" "$work/root-$format/$bundle"
done

# Task 3 adds sign_and_notarize here, on the staged bundles.

licences=(
    LICENSE
    LICENSE-AudreyII
    THIRD_PARTY_LICENSES.md
    third_party/DaisySP/LICENSE
    assets/fonts/OFL-Inter.txt
    assets/fonts/OFL-Cormorant.txt)
{
    cat "$resources/readme-header.html"
    for licence in "${licences[@]}"; do
        echo "<h3>$licence</h3>"
        echo "<pre>"
        sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' "$repo/$licence"
        echo "</pre>"
    done
    echo "</body></html>"
} > "$work/screens/readme.html"
cp "$resources/welcome.html" "$resources/conclusion.html" "$work/screens/"

for row in "${formats[@]}"; do
    read -r format bundle identifier location <<< "$row"
    sed "s/@BUNDLE@/$bundle/" "$resources/components.plist" > "$work/components-$format.plist"
    scripts=()
    [ "$format" = AU ] && scripts=(--scripts "$resources/scripts/au")
    pkgbuild --quiet --root "$work/root-$format" --component-plist "$work/components-$format.plist" \
        --identifier "$identifier" --version "$version" --install-location "$location" ${scripts[@]+"${scripts[@]}"} \
        "$work/components/Drosera-$format.pkg"
done

sed "s/@VERSION@/$version/g" "$resources/distribution.xml" > "$work/distribution.xml"
mkdir -p "$(dirname "$output")"
productbuild --quiet --distribution "$work/distribution.xml" --resources "$work/screens" \
    --package-path "$work/components" "$work/Drosera.pkg"
mv -f "$work/Drosera.pkg" "$output"
echo "build_pkg: wrote $output"
