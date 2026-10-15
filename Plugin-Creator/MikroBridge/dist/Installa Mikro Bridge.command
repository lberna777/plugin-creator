#!/bin/bash
# Copia Mikro Bridge in Applicazioni, toglie la quarantena del download e lo apre.
set -e
cd "$(dirname "$0")"

osascript -e 'quit app "Mikro Bridge"' 2>/dev/null || true
rm -rf "/Applications/Mikro Bridge.app"
cp -R "Mikro Bridge.app" /Applications/
xattr -dr com.apple.quarantine "/Applications/Mikro Bridge.app" 2>/dev/null || true
open "/Applications/Mikro Bridge.app"

echo
echo "Mikro Bridge installato in Applicazioni."
