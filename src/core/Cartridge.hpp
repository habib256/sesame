// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#pragma once
// =============================================================================
//  Cartridge — ROM + mapper. Trois types :
//   - SEGA (standard) : 0x0000-0x3FFF page 0 (le premier Ko 0x0000-0x03FF
//     est TOUJOURS la ROM page 0, non paginé), 0x4000-0x7FFF page 1,
//     0x8000-0xBFFF page 2 OU RAM de sauvegarde ; registres écrits à
//     0xFFFC (contrôle RAM) et 0xFFFD/E/F (pages), relayés par le Bus.
//   - CODEMASTERS : registres AUX ADRESSES 0x0000/0x4000/0x8000 (une par
//     fenêtre), premier Ko paginé comme le reste, pas de registre 0xFFFx ;
//     bit 7 de 0x4000 = RAM 8 Ko sur 0xA000-0xBFFF (Ernie Els Golf).
//     Auto-détecté par l'en-tête cartouche à 0x7FE0 (nombre de banques +
//     somme de contrôle).
//   - KOREAN : fenêtres 0/1 fixes, écriture à 0xA000 = page de la
//     fenêtre 2. Pas d'en-tête : détecté à l'exécution (première écriture
//     à 0xA000 alors que les registres Sega n'ont jamais été touchés).
//  RAM de sauvegarde Sega : persistée dans un fichier .sav à côté de la ROM
//  (chargé par load(), écrit par persistSaveRam()) quand savEnabled est vrai.
// =============================================================================
#include "Types.hpp"
#include <string>
#include <vector>

class StateIO;

class Cartridge {
public:
    enum class Mapper : u8 { Sega, Codemasters, Korean, Janggun };

    // Charge un fichier .sms ; tolère l'en-tête parasite de 512 octets des
    // vieux dumpers (taille % 0x4000 == 512). Retourne false si échec.
    bool load(const std::string& path);

    Mapper mapper() const { return mapperType; }

    void reset();

    // Save-state : registres du mapper + RAM cartouche (PAS la ROM, qui
    // vient du fichier chargé). Au chargement, la RAM est marquée modifiée
    // pour que la persistance .sav reparte de l'état restauré.
    void serialize(StateIO& s);

    u8   read(u16 addr);              // 0x0000-0xBFFF
    // Toute écriture CPU sur 0x0000-0xBFFF (relayée par le Bus) : RAM
    // cartouche, registres Codemasters (0x0000/0x4000/0x8000) ou page
    // coréenne (0xA000) selon le mapper.
    void write(u16 addr, u8 v);
    void writeMapper(u16 addr, u8 v); // 0xFFFC-0xFFFF (mapper Sega seulement)

    bool loaded() const { return !rom.empty(); }
    size_t romSize() const { return rom.size(); }

    // Empreinte de la ROM chargée (taille ^ somme des octets, calculée par
    // load()) : enregistrée dans l'en-tête des save-states pour refuser un
    // état pris sur une autre cartouche (rewind à travers un changement de
    // jeu, .state d'un autre titre). 0 si aucune ROM.
    u32 fingerprint() const { return romFp; }

    // Écrit la RAM de sauvegarde dans le .sav si elle a été modifiée depuis
    // le dernier appel (no-op sinon : appelable périodiquement sans coût).
    void persistSaveRam();

    // Persistance .sav : à régler AVANT load(). Le frontend GUI la laisse
    // active ; le headless (déterministe) ne l'active que sur --sav ; le
    // slot BIOS de Machine la désactive toujours.
    bool savEnabled = true;

    // EEPROM série 93C46 (cartouches de baseball) : à activer AVANT load()
    // (pas d'en-tête pour la détecter — clé `eeprom` de sesame.cfg,
    // --eeprom au headless). Mappée sur 0x8000-0xBFFF : écriture = lignes
    // DI/CLK/CS (bits 0-2), lecture = ligne DO (bit 0). 64 mots de 16 bits
    // persistés dans <rom>.eeprom quand savEnabled l'est aussi.
    bool eepromEnabled = false;

    std::vector<u8> rom;

private:
    Mapper mapperType = Mapper::Sega;
    u8 pageReg[3]{};      // pages ROM sélectionnées pour les 3 fenêtres
    // Janggun : quatre fenêtres de 8 Ko sur 0x4000-0xBFFF (registres aux
    // adresses 0x4000/0x6000/0x8000/0xA000 ; 0xFFFE/0xFFFF posent les
    // paires 16 Ko). Bit 6 d'une page = lecture à OCTETS MIROIRS
    // (bits inversés — particularité de cette cartouche).
    u8 pageReg8[4]{};
    u8 ramControl = 0;    // registre 0xFFFC (Sega)
    u8 cartRam[2][0x4000]{};  // 2 banques de RAM de sauvegarde possibles (Sega)
    bool cmRamEnabled = false;   // Codemasters : RAM 8 Ko sur 0xA000-0xBFFF
    u8 cmRam[0x2000]{};          // (volatile — pas de pile sur ces cartouches)
    bool segaRegsSeen = false;   // garde de l'heuristique coréenne
    // Garde de l'heuristique Janggun : 0xFFFD (fenêtre 0) n'existe PAS sur
    // le matériel Janggun, mais le boot d'un jeu Sega standard l'écrit
    // toujours — sa présence disqualifie une écriture parasite à 0x6000.
    bool fffdSeen = false;
    int romMask = 0;      // masque de page (nombre de pages arrondi puissance de 2)
    u32 romFp = 0;        // empreinte de la ROM (voir fingerprint())
    std::string savePath;     // <rom sans extension>.sav
    bool saveRamDirty = false;

    // --- État Microwire de la 93C46 -----------------------------------------
    struct Eeprom {
        u16 data[64]{};
        u8  phase = 0;         // 0 = attente start, 1 = commande, 2 = données
                               // entrantes, 3 = données sortantes
        u16 shiftIn = 0;
        int bits = 0;
        u8  op = 0, addr = 0;
        bool wral = false;     // écriture globale en cours (WRAL)
        u16 shiftOut = 0;
        int outBits = 0;
        bool writeEnabled = false;
        bool doLine = true;    // ligne DO (1 = prêt/repos)
        bool prevClk = false, cs = false;
    } ee;
    bool eeDirty = false;
    std::string eePath;        // <rom sans extension>.eeprom
    void eeLines(u8 v);        // pose DI/CLK/CS et avance la machine à états
    void eeClockIn(int di);    // un front montant d'horloge, CS haut
};
