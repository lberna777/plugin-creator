#!/usr/bin/env bash
# VOCAL FORGE — build e installazione su macOS, in un comando solo.
#
#   ./build_mac.sh              build Release universale (arm64 + x86_64), AU + VST3 + Standalone
#   ./build_mac.sh --fast       build solo per il TUO processore: circa metà del tempo
#   ./build_mac.sh --clean      ributta via la cartella build e ricomincia
#
# Al termine i plugin sono già installati in ~/Library/Audio/Plug-Ins e validati con auval.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$HERE/build"
ARCHS="arm64;x86_64"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

for arg in "$@"; do
  case "$arg" in
    --fast)  ARCHS="$(uname -m)" ;;
    --clean) rm -rf "$BUILD_DIR" ;;
    *) echo "opzione sconosciuta: $arg"; exit 2 ;;
  esac
done

echo "==> Controllo gli strumenti"
if ! xcode-select -p >/dev/null 2>&1; then
  echo "    Mancano i Command Line Tools di Xcode. Lancio l'installer:"
  xcode-select --install || true
  echo "    Finita l'installazione, rilancia questo script."
  exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
  echo "    Manca CMake. Installalo con:  brew install cmake"
  echo "    (se non hai Homebrew: https://brew.sh)"
  exit 1
fi

echo "==> Configuro (architetture: $ARCHS)"
echo "    La prima volta scarico JUCE: qualche minuto, solo questa volta."
cmake -B "$BUILD_DIR" -S "$HERE" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="$ARCHS"

echo "==> Compilo con $JOBS processi"
cmake --build "$BUILD_DIR" --config Release -j "$JOBS"

echo "==> Installo"
AU_SRC="$(find "$BUILD_DIR" -name 'VOCAL FORGE.component' -maxdepth 6 -type d | head -1)"
VST3_SRC="$(find "$BUILD_DIR" -name 'VOCAL FORGE.vst3' -maxdepth 6 -type d | head -1)"
mkdir -p "$HOME/Library/Audio/Plug-Ins/Components" "$HOME/Library/Audio/Plug-Ins/VST3"
[ -n "$AU_SRC" ]   && rm -rf "$HOME/Library/Audio/Plug-Ins/Components/VOCAL FORGE.component" && cp -R "$AU_SRC"   "$HOME/Library/Audio/Plug-Ins/Components/"
[ -n "$VST3_SRC" ] && rm -rf "$HOME/Library/Audio/Plug-Ins/VST3/VOCAL FORGE.vst3"            && cp -R "$VST3_SRC" "$HOME/Library/Audio/Plug-Ins/VST3/"

echo "==> Sveglio il registro delle Audio Unit (Logic non rilegge i plugin da solo)"
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo "==> Validazione AU (è la stessa che fa Logic all'avvio)"
if auval -v aufx Vfrg Mypl > "$BUILD_DIR/auval.log" 2>&1; then
  echo "    auval: SUCCEEDED"
else
  echo "    auval ha segnalato un problema. Log completo: $BUILD_DIR/auval.log"
  tail -20 "$BUILD_DIR/auval.log"
  exit 1
fi

echo
echo "FATTO."
echo "  AU        ~/Library/Audio/Plug-Ins/Components/VOCAL FORGE.component"
echo "  VST3      ~/Library/Audio/Plug-Ins/VST3/VOCAL FORGE.vst3"
echo "  Standalone $(find "$BUILD_DIR" -name 'VOCAL FORGE.app' -maxdepth 6 -type d | head -1)"
echo
echo "Apri Logic, metti VOCAL FORGE sulla traccia vocale, scrivi che voce vuoi e premi FORGE."
