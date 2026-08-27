// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

// =============================================================================
//  sesame-wasm — frontend WebAssembly (Emscripten), sans SDL ni GLFW :
//  la page web (web/index.html) pilote tout via cette petite API C —
//  canvas 2D pour l'image (putImageData + recadrage Game Gear), WebAudio
//  pour le son, clavier -> setPad. La ROM arrive en octets depuis un
//  <input type=file> et passe par le système de fichiers mémoire (MEMFS)
//  d'Emscripten pour réutiliser Machine::loadRom tel quel.
//
//  Build : tools/build_wasm.sh (emcc requis) -> web/sesame.js + .wasm.
// =============================================================================
#include "core/Machine.hpp"

#include <emscripten/emscripten.h>

#include <cstdio>
#include <string>

namespace {
Machine machine;
bool romLoaded = false;
}  // namespace

extern "C" {

// Charge une ROM depuis un tampon : écrite dans MEMFS sous son nom (pour
// l'auto-détection .sms/.gg/.sg), puis chargée par la voie normale.
// Retourne 1 si OK.
EMSCRIPTEN_KEEPALIVE
int sesame_load_rom(const u8* data, int size, const char* name) {
    std::string path = std::string("/") + (name && *name ? name : "rom.sms");
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return 0;
    std::fwrite(data, 1, (size_t)size, f);
    std::fclose(f);
    romLoaded = machine.loadRom(path);
    return romLoaded ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void sesame_set_region(int pal) {
    machine.setRegion(pal ? Region::Pal : Region::Ntsc);
}

EMSCRIPTEN_KEEPALIVE
int sesame_frames_per_second_x100() {
    return (machine.region() == Region::Pal) ? 4970 : 5992;
}

EMSCRIPTEN_KEEPALIVE
void sesame_run_frame() {
    if (romLoaded)
        machine.runFrame();
}

// Framebuffer RGBA complet (largeur de ligne fixe kWidth) et fenêtre
// visible : la page recadre elle-même (Game Gear : 160×144 centrée).
EMSCRIPTEN_KEEPALIVE const u8* sesame_frame_buffer() {
    return reinterpret_cast<const u8*>(machine.vdp.frameBuffer());
}
EMSCRIPTEN_KEEPALIVE int sesame_stride() { return Vdp::kWidth; }
EMSCRIPTEN_KEEPALIVE int sesame_view_x() {
    return machine.model() != Model::Sms ? Vdp::kGgOffsetX : 0;
}
EMSCRIPTEN_KEEPALIVE int sesame_view_y() {
    return machine.model() != Model::Sms
               ? (machine.vdp.height() - Vdp::kGgHeight) / 2 : 0;
}
EMSCRIPTEN_KEEPALIVE int sesame_view_w() {
    return machine.model() != Model::Sms ? Vdp::kGgWidth : Vdp::kWidth;
}
EMSCRIPTEN_KEEPALIVE int sesame_view_h() {
    return machine.model() != Model::Sms ? Vdp::kGgHeight
                                         : machine.vdp.height();
}
EMSCRIPTEN_KEEPALIVE int sesame_is_game_gear() {
    return machine.model() != Model::Sms ? 1 : 0;
}

// Audio : vide jusqu'à maxFrames trames stéréo entrelacées dans out.
EMSCRIPTEN_KEEPALIVE
int sesame_audio_read(s16* out, int maxFrames) {
    return machine.psg.readSamples(out, maxFrames);
}
EMSCRIPTEN_KEEPALIVE int sesame_sample_rate() { return Psg::kSampleRate; }

// Entrées : masque Io::Button (Start inclus pour la Game Gear native).
EMSCRIPTEN_KEEPALIVE
void sesame_set_pad(int pad, int mask) {
    machine.io.setPad(pad, (u8)mask);
}
EMSCRIPTEN_KEEPALIVE void sesame_press_pause() { machine.pressPause(); }
EMSCRIPTEN_KEEPALIVE void sesame_reset() { machine.reset(); }

}  // extern "C"
