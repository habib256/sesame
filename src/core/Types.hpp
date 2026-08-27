// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#pragma once
// Types de base partagés par tout le cœur.
#include <cstdint>

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8  = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

// Norme vidéo de la console : conditionne l'horloge CPU/PSG, le nombre de
// lignes par trame du VDP et la séquence du VCounter.
enum class Region : u8 {
    Ntsc,  // Japon / Amérique du Nord : 262 lignes, ~59,92 Hz, CPU 3 579 545 Hz
    Pal,   // Europe : 313 lignes, ~49,70 Hz, CPU 3 546 893 Hz
};

// Modèle de console émulé. La Game Gear partage l'essentiel de la SMS
// (Z80, VDP mode 4, PSG) mais diffère par : CRAM 12 bits (32 couleurs
// parmi 4096), fenêtre visible 160×144 centrée, ports 0x00-0x06 propres
// (bouton Start, stéréo PSG) et absence de NMI Pause.
enum class Model : u8 {
    Sms,       // Master System / Mark III
    GameGear,  // Game Gear (mode natif)
};
