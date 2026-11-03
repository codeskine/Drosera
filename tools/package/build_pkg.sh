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

# Signing takes all five variables or none: a half-configured CI must fail, not ship an unsigned package.
signing_vars=(DEVELOPER_ID_APPLICATION DEVELOPER_ID_INSTALLER APPLE_ID APPLE_TEAM_ID APPLE_APP_PASSWORD)
set_count=0
for var in "${signing_vars[@]}"; do [ -n "${!var:-}" ] && set_count=$((set_count + 1)); done
if [ "$set_count" -ne 0 ] && [ "$set_count" -ne "${#signing_vars[@]}" ]; then
    echo "build_pkg: signing needs all of ${signing_vars[*]}; only $set_count are set" >&2
    exit 1
fi
signing=$([ "$set_count" -ne 0 ] && echo 1 || echo 0)

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

# Each bundle on its own, never --deep: notarization needs the hardened runtime on every executable. Only the app
# takes entitlements; the plug-ins run with their host's.
sign_bundles() {
    local -a sign=(codesign --force --options runtime --timestamp --sign "$DEVELOPER_ID_APPLICATION")
    "${sign[@]}" "$work/root-VST3/Drosera.vst3"
    "${sign[@]}" "$work/root-AU/Drosera.component"
    "${sign[@]}" --entitlements "$resources/Drosera.entitlements" "$work/root-Standalone/Drosera.app"
}

notarize_and_staple() {
    local package=$1 result id status
    local -a credentials=(--apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD")
    result=$(xcrun notarytool submit "$package" "${credentials[@]}" --wait --output-format json) || true
    id=$(plutil -extract id raw -o - - <<< "$result" 2>/dev/null || true)
    status=$(plutil -extract status raw -o - - <<< "$result" 2>/dev/null || true)
    if [ "$status" != Accepted ]; then
        echo "build_pkg: notarization ended with '${status:-no status}': $result" >&2
        [ -n "$id" ] && xcrun notarytool log "$id" "${credentials[@]}" >&2
        exit 1
    fi
    xcrun stapler staple "$package"
}

[ "$signing" = 1 ] && sign_bundles

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
product_signing=()
[ "$signing" = 1 ] && product_signing=(--sign "$DEVELOPER_ID_INSTALLER" --timestamp)
productbuild --quiet --distribution "$work/distribution.xml" --resources "$work/screens" \
    --package-path "$work/components" ${product_signing[@]+"${product_signing[@]}"} "$work/Drosera.pkg"
[ "$signing" = 1 ] && notarize_and_staple "$work/Drosera.pkg"
mv -f "$work/Drosera.pkg" "$output"
echo "build_pkg: wrote $output"
