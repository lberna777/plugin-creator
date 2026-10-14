#!/bin/bash
# Installa C-60 (AU per Logic + VST3) nell'utente corrente. Funziona offline.
cd "$(dirname "$0")" || exit 1
set -e
AU="$HOME/Library/Audio/Plug-Ins/Components"
VST="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$AU" "$VST"
rm -rf "$AU/C-60.component" "$VST/C-60.vst3"
cp -R "C-60.component" "$AU/"
cp -R "C-60.vst3" "$VST/"
# plugin non firmato da Apple: togli la quarantena e firma localmente (ad-hoc)
xattr -dr com.apple.quarantine "$AU/C-60.component" "$VST/C-60.vst3" 2>/dev/null || true
codesign --force --deep -s - "$AU/C-60.component" >/dev/null 2>&1 || true
codesign --force --deep -s - "$VST/C-60.vst3" >/dev/null 2>&1 || true
# fai rileggere i plugin AU al sistema
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo ""
echo "C-60 installato."
echo "Apri Logic Pro: lo trovi negli effetti Audio Units > MyPlugins > C-60."
echo "Se Logic non lo vede: Logic Pro > Impostazioni > Gestione plug-in > Reimposta e riesegui scansione."
