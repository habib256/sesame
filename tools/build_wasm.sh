#!/bin/sh
# Copyright (C) 2026 VERHILLE Arnaud
# SPDX-License-Identifier: GPL-3.0-or-later
# Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
# (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.
#
# build_wasm.sh — construit le frontend WebAssembly dans web/ :
#   web/sesame.js + web/sesame.wasm (la page web/index.html est commitée).
# Requiert Emscripten (emcc) : https://emscripten.org (brew install emscripten).
#
# Servir ensuite en local :  python3 -m http.server -d web 8080
set -e
cd "$(dirname "$0")/.."

em++ -std=c++17 -O2 \
    -I src \
    src/core/Bus.cpp src/core/Machine.cpp src/core/Z80.cpp \
    src/core/Z80Disasm.cpp src/core/Vdp.cpp src/core/Psg.cpp \
    src/core/Ym2413.cpp src/core/Io.cpp src/core/Cartridge.cpp \
    src/wasm/main_wasm.cpp \
    -s WASM=1 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME=SesameModule \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXPORTED_FUNCTIONS=_malloc,_free \
    -s EXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPU8,HEAP16 \
    -o web/sesame.js

echo "OK: web/sesame.js + web/sesame.wasm"
