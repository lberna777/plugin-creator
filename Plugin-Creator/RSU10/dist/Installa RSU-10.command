#!/bin/bash
# Installa RSU-10 (AU + VST3) nell'utente corrente e l'app autonoma in Applicazioni. Funziona offline.
cd "$(dirname "$0")" || exit 1
set -e
AU="$HOME/Library/Audio/Plug-Ins/Components"
VST="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$AU" "$VST"
rm -rf "$AU/RSU-10.component" "$VST/RSU-10.vst3" "/Applications/RSU-10.app"
cp -R "RSU-10.component" "$AU/"
cp -R "RSU-10.vst3" "$VST/"
cp -R "RSU-10.app" /Applications/ 2>/dev/null || true
# plugin non firmato da Apple: togli la quarantena e firma localmente (ad-hoc)
for P in "$AU/RSU-10.component" "$VST/RSU-10.vst3" "/Applications/RSU-10.app"; do
  [ -e "$P" ] || continue
  xattr -dr com.apple.quarantine "$P" 2>/dev/null || true
  codesign --force --deep -s - "$P" >/dev/null 2>&1 || true
done
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo ""
echo "RSU-10 installato."
echo "In FL Studio: Opzioni > Gestione plugin > Trova plugin, poi lo trovi tra i Generatori."
