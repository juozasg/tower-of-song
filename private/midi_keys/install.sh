#!/bin/sh
# Builds the plugin and installs it into this game:
#   plugins/midi_keys.vectaplugin  - what the editor and exports use
#   plugins/midi_keys.dylib|dll    - what the runtime loads (the editor extracts it too)
#   luau-api/midi_keys.luau        - types for autocompletion
set -eu
cd "$(dirname "$0")"
uv run bundle.py --release
GAME=../..
mkdir -p "$GAME/plugins" "$GAME/luau-api"
cp midi_keys.vectaplugin "$GAME/plugins/"
cp plugin.luau "$GAME/luau-api/midi_keys.luau"
case "$(uname -s)" in
    Darwin) cp target/aarch64-apple-darwin/release/libmidi_keys.dylib "$GAME/plugins/midi_keys.dylib" ;;
    Linux) cp target/x86_64-unknown-linux-gnu/release/libmidi_keys.so "$GAME/plugins/midi_keys.so" ;;
    *) cp target/x86_64-pc-windows-msvc/release/midi_keys.dll "$GAME/plugins/midi_keys.dll" ;;
esac
echo "Installed into $GAME/plugins"
