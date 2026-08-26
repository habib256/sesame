// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#pragma once
// =============================================================================
//  Io — ports d'entrées/sorties hors VDP/PSG :
//   - 0xDC/0xDD : manettes 1 & 2 (bits actifs à l'état BAS) + Reset/région
//   - 0x3F : contrôle E/S (broches TH, détection de région)
//  (Le port 0x3E — contrôle mémoire — est géré par le Bus, qui est le plan
//   mémoire.)
//   - 0xFC/0xFD : console de débogage SDSC (convention homebrew) — le texte
//     écrit sur 0xFD est accumulé et relayé sur stdout si `sdscEnabled`.
// =============================================================================
#include "Types.hpp"
#include <string>

class Io {
public:
    // Boutons manette : bits à 1 = APPUYÉ dans cette API publique
    // (l'inversion « actif bas » est faite en interne).
    enum Button : u8 {
        Up = 1 << 0, Down = 1 << 1, Left = 1 << 2, Right = 1 << 3,
        B1 = 1 << 4, B2 = 1 << 5,
    };

    void reset();

    u8   readPort(u8 port);        // 0xDC ou 0xDD
    void writeIoControl(u8 v);     // 0x3F
    void writeSdscControl(u8 v);   // 0xFC
    void writeSdscData(u8 v);      // 0xFD

    // État des manettes, poussé par le frontend.
    void setPad(int pad, u8 buttons);  // pad 0 ou 1, masque de Button

    bool sdscEnabled = false;          // headless : --sdsc l'active
    const std::string& sdscLog() const { return sdscText; }

private:
    u8 pad[2]{};        // masques Button (actifs à 1)
    u8 ioControl  = 0xFF;
    std::string sdscText;   // tout le texte SDSC reçu (aussi émis sur stdout)
};
