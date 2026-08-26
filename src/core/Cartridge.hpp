// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#pragma once
// =============================================================================
//  Cartridge — ROM + mapper Sega standard.
//  Plan vu du CPU : 0x0000-0x3FFF page 0 (le premier Ko 0x0000-0x03FF est
//  TOUJOURS la ROM page 0, non paginé), 0x4000-0x7FFF page 1,
//  0x8000-0xBFFF page 2 OU RAM de sauvegarde si activée.
//  Registres du mapper : écritures à 0xFFFC (contrôle RAM), 0xFFFD/E/F
//  (numéros de page 0/1/2) — relayées ici par le Bus.
//  RAM de sauvegarde : persistée dans un fichier .sav à côté de la ROM
//  (chargé par load(), écrit par persistSaveRam()) quand savEnabled est vrai.
// =============================================================================
#include "Types.hpp"
#include <string>
#include <vector>

class Cartridge {
public:
    // Charge un fichier .sms ; tolère l'en-tête parasite de 512 octets des
    // vieux dumpers (taille % 0x4000 == 512). Retourne false si échec.
    bool load(const std::string& path);

    void reset();

    u8   read(u16 addr);              // 0x0000-0xBFFF
    void write(u16 addr, u8 v);       // 0x8000-0xBFFF : RAM cartouche si activée
    void writeMapper(u16 addr, u8 v); // 0xFFFC-0xFFFF

    bool loaded() const { return !rom.empty(); }
    size_t romSize() const { return rom.size(); }

    // Écrit la RAM de sauvegarde dans le .sav si elle a été modifiée depuis
    // le dernier appel (no-op sinon : appelable périodiquement sans coût).
    void persistSaveRam();

    // Persistance .sav : à régler AVANT load(). Le frontend GUI la laisse
    // active ; le headless (déterministe) ne l'active que sur --sav ; le
    // slot BIOS de Machine la désactive toujours.
    bool savEnabled = true;

    std::vector<u8> rom;

private:
    u8 pageReg[3]{};      // pages ROM sélectionnées pour les 3 fenêtres
    u8 ramControl = 0;    // registre 0xFFFC
    u8 cartRam[2][0x4000]{};  // 2 banques de RAM de sauvegarde possibles
    int romMask = 0;      // masque de page (nombre de pages arrondi puissance de 2)
    std::string savePath;     // <rom sans extension>.sav
    bool saveRamDirty = false;
};
