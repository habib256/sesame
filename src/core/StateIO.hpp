// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#pragma once
// =============================================================================
//  StateIO — sérialiseur SYMÉTRIQUE pour les save-states.
//  Chaque puce expose serialize(StateIO&) qui liste ses champs UNE seule
//  fois : en mode Save ils sont écrits, en mode Load ils sont relus — la
//  symétrie rend impossible la désynchronisation écriture/lecture.
//  Format : little-endian explicite (portable), bool sur un octet.
//  Toute erreur d'E/S est cumulée dans ok() ; en Load, une lecture courte
//  laisse les champs restants inchangés et ok() passe à faux.
// =============================================================================
#include "Types.hpp"
#include <cstdio>

class StateIO {
public:
    enum class Mode { Save, Load };

    StateIO(FILE* file, Mode m) : f(file), mode(m) {}

    bool loading() const { return mode == Mode::Load; }
    bool ok() const { return good; }

    void bytes(u8* p, size_t n) {
        if (!good) return;
        if (mode == Mode::Save)
            good = std::fwrite(p, 1, n, f) == n;
        else
            good = std::fread(p, 1, n, f) == n;
    }

    void u8v(u8& v) { bytes(&v, 1); }
    void u16v(u16& v) {
        u8 b[2] = {(u8)(v & 0xFF), (u8)(v >> 8)};
        bytes(b, 2);
        if (loading()) v = (u16)(b[0] | (b[1] << 8));
    }
    void u32v(u32& v) {
        u8 b[4] = {(u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24)};
        bytes(b, 4);
        if (loading())
            v = (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) |
                ((u32)b[3] << 24);
    }
    void u64v(u64& v) {
        u32 lo = (u32)v, hi = (u32)(v >> 32);
        u32v(lo);
        u32v(hi);
        if (loading()) v = (u64)lo | ((u64)hi << 32);
    }
    void boolv(bool& v) {
        u8 b = v ? 1 : 0;
        u8v(b);
        if (loading()) v = (b != 0);
    }
    // Entiers signés : transportés en complément à deux sur leur taille.
    void s16v(s16& v) { u16v(reinterpret_cast<u16&>(v)); }
    void s32v(s32& v) { u32v(reinterpret_cast<u32&>(v)); }
    void intv(int& v) {
        s32 t = (s32)v;
        s32v(t);
        if (loading()) v = (int)t;
    }

private:
    FILE* f;
    Mode mode;
    bool good = true;
};
