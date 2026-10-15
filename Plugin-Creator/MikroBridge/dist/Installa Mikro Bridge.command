#!/bin/bash
# Copia Mikro Bridge in Applicazioni, toglie la quarantena del download e lo apre.
set -e
cd "$(dirname "$0")"

osascript -e 'quit app "Mikro Bridge"' 2>/dev/null || true
rm -rf "/Applications/Mikro Bridge.app"
cp -R "Mikro Bridge.app" /Applications/
xattr -dr com.apple.quarantine "/Applications/Mikro Bridge.app" 2>/dev/null || true
open "/Applications/Mikro Bridge.app"

# Script controller per FL Studio: tasti della Mikro gia' mappati (Options > MIDI settings > Controller type "Mikro Bridge")
for SCRIPT in "device_MikroBridge.py" "../fl/device_MikroBridge.py"; do
    if [ -f "$SCRIPT" ]; then
        FL="$HOME/Documents/Image-Line/FL Studio/Settings/Hardware/Mikro Bridge"
        mkdir -p "$FL"
        cp "$SCRIPT" "$FL/device_MikroBridge.py"
        echo "Script per FL Studio installato in: $FL"
        break
    fi
done

echo
echo "Mikro Bridge installato in Applicazioni."
