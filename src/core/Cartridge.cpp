// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

// =============================================================================
//  Cartridge.cpp — chargement des ROM .sms et mapper Sega standard.
//
//  Rappels matériels (voir CLAUDE.md et SMS Power!) :
//   - Le premier Ko (0x0000-0x03FF) n'est JAMAIS paginé : il pointe toujours
//     sur le début physique de la ROM, quel que soit pageReg[0].
//   - Trois fenêtres de 16 Ko : 0x0000-0x3FFF, 0x4000-0x7FFF, 0x8000-0xBFFF,
//     pilotées par les registres écrits à 0xFFFD/E/F (relayés par le Bus).
//   - 0xFFFC contrôle la RAM de sauvegarde : bit 3 = RAM mappée sur
//     0x8000-0xBFFF, bit 2 = choix de la banque (0 ou 1).
// =============================================================================
#include "Cartridge.hpp"

#include "StateIO.hpp"
#include <fstream>

bool Cartridge::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        return false;

    std::streamoff size = f.tellg();
    if (size <= 0)
        return false;

    // Certains vieux dumpers ajoutaient un en-tête parasite de 512 octets :
    // on le détecte (taille = multiple de 16 Ko + 512) et on le saute.
    std::streamoff skip = 0;
    if (size % 0x4000 == 512) {
        skip = 512;
        size -= 512;
    }
    if (size <= 0)
        return false;

    rom.resize(static_cast<size_t>(size));
    f.seekg(skip, std::ios::beg);
    if (!f.read(reinterpret_cast<char*>(rom.data()), size))
        return false;

    // Une ROM plus petite qu'une page (16 Ko) est traitée comme une page
    // complète : on la complète par miroir d'elle-même.
    const size_t original = rom.size();
    while (rom.size() < 0x4000)
        rom.push_back(rom[rom.size() % original]);
    // Idem si la taille n'est pas un multiple exact de 16 Ko.
    while (rom.size() % 0x4000 != 0)
        rom.push_back(rom[rom.size() % original]);

    // Nombre de pages arrondi à la puissance de 2 supérieure : le masque de
    // page devient un simple ET binaire. On complète physiquement la ROM par
    // miroir jusqu'à cette taille pour que tout accès masqué reste valide.
    size_t pages = rom.size() / 0x4000;
    size_t pow2 = 1;
    while (pow2 < pages)
        pow2 <<= 1;
    while (rom.size() < pow2 * 0x4000)
        rom.push_back(rom[rom.size() % original]);
    romMask = static_cast<int>(pow2 - 1);

    // Détection du mapper Codemasters par l'en-tête cartouche à 0x7FE0 :
    // octet 0 = nombre de banques de 16 Ko, mots 0x7FE6/0x7FE8 = somme de
    // contrôle et son complément à 0x10000. Les trois conditions ensemble
    // rendent les faux positifs improbables.
    mapperType = Mapper::Sega;
    if (rom.size() >= 0x8000) {
        const unsigned banks = rom[0x7FE0];
        const unsigned sum = rom[0x7FE6] | (rom[0x7FE7] << 8);
        const unsigned inv = rom[0x7FE8] | (rom[0x7FE9] << 8);
        if (sum != 0 && ((sum + inv) & 0xFFFF) == 0 &&
            banks == rom.size() / 0x4000)
            mapperType = Mapper::Codemasters;
    }

    // Persistance de la RAM de sauvegarde : « <rom sans extension>.sav » à
    // côté de la ROM. Un .sav existant est rechargé (jeu commencé sur pile).
    savePath.clear();
    saveRamDirty = false;
    if (savEnabled) {
        savePath = path;
        const auto slash = savePath.find_last_of("/\\");
        const auto dot   = savePath.find_last_of('.');
        if (dot != std::string::npos &&
            (slash == std::string::npos || dot > slash))
            savePath.resize(dot);
        savePath += ".sav";

        std::ifstream sav(savePath, std::ios::binary);
        if (sav)
            sav.read(reinterpret_cast<char*>(cartRam[0]), sizeof(cartRam));
    }

    reset();
    return true;
}

void Cartridge::persistSaveRam() {
    if (!saveRamDirty || savePath.empty())
        return;
    std::ofstream sav(savePath, std::ios::binary | std::ios::trunc);
    if (!sav)
        return;  // disque plein / dossier en lecture seule : on réessaiera
    sav.write(reinterpret_cast<const char*>(cartRam[0]), sizeof(cartRam));
    if (sav)
        saveRamDirty = false;
}

void Cartridge::reset() {
    // État de mise sous tension du mapper : fenêtres 0/1/2 (0/1/0 pour
    // Codemasters), RAM désactivée. Le TYPE de mapper est une propriété de
    // la cartouche : il survit au reset (y compris un type coréen détecté
    // à l'exécution).
    pageReg[0] = 0;
    pageReg[1] = 1;
    pageReg[2] = (mapperType == Mapper::Codemasters) ? 0 : 2;
    ramControl = 0;
    cmRamEnabled = false;
    segaRegsSeen = false;
}

u8 Cartridge::read(u16 addr) {
    if (rom.empty())
        return 0xFF;  // pas de cartouche : bus flottant

    if (mapperType == Mapper::Codemasters) {
        // Codemasters : trois fenêtres pleines (le premier Ko est paginé
        // comme le reste) ; RAM 8 Ko optionnelle sur 0xA000-0xBFFF.
        if (addr >= 0xA000 && cmRamEnabled)
            return cmRam[addr & 0x1FFF];
        const int slot = addr >> 14;
        return rom[(static_cast<size_t>(pageReg[slot] & romMask) << 14) |
                   (addr & 0x3FFF)];
    }

    // Mappers Sega et coréen (même plan de lecture ; le coréen n'écrit
    // simplement jamais pageReg[0]/[1] ni le contrôle RAM).
    // 0x0000-0x03FF : TOUJOURS le début physique de la ROM, jamais paginé
    // (c'est là que vivent les vecteurs d'interruption du Z80).
    if (addr < 0x0400)
        return rom[addr];

    // 0x0400-0x3FFF : reste de la fenêtre 0.
    if (addr < 0x4000)
        return rom[(static_cast<size_t>(pageReg[0] & romMask) << 14) | (addr & 0x3FFF)];

    // 0x4000-0x7FFF : fenêtre 1.
    if (addr < 0x8000)
        return rom[(static_cast<size_t>(pageReg[1] & romMask) << 14) | (addr & 0x3FFF)];

    // 0x8000-0xBFFF : RAM de sauvegarde si activée (0xFFFC bit 3),
    // sinon fenêtre 2. Le bit 2 choisit la banque de RAM.
    if (ramControl & 0x08)
        return cartRam[(ramControl >> 2) & 1][addr & 0x3FFF];
    return rom[(static_cast<size_t>(pageReg[2] & romMask) << 14) | (addr & 0x3FFF)];
}

void Cartridge::write(u16 addr, u8 v) {
    if (mapperType == Mapper::Codemasters) {
        // Registres de page aux adresses 0x0000/0x4000/0x8000. Le bit 7 de
        // 0x4000 mappe la RAM 8 Ko sur 0xA000-0xBFFF (Ernie Els Golf).
        switch (addr) {
        case 0x0000: pageReg[0] = v; return;
        case 0x4000:
            cmRamEnabled = (v & 0x80) != 0;
            pageReg[1] = (u8)(v & 0x7F);
            return;
        case 0x8000: pageReg[2] = v; return;
        default:
            if (addr >= 0xA000 && addr < 0xC000 && cmRamEnabled)
                cmRam[addr & 0x1FFF] = v;
            return;
        }
    }

    if (mapperType == Mapper::Korean) {
        if (addr == 0xA000)
            pageReg[2] = v;
        return;
    }

    // Mapper Sega : seule la fenêtre 0x8000-0xBFFF est inscriptible, quand
    // la RAM cartouche y est mappée.
    if (addr >= 0x8000 && addr < 0xC000 && (ramControl & 0x08)) {
        cartRam[(ramControl >> 2) & 1][addr & 0x3FFF] = v;
        saveRamDirty = true;
        return;
    }

    // Heuristique coréenne (pas d'en-tête sur ces cartouches) : une
    // écriture à 0xA000 alors que les registres Sega 0xFFFD-0xFFFF n'ont
    // JAMAIS été touchés et que la RAM n'est pas mappée trahit le mapper
    // coréen — on bascule et on applique la page.
    if (addr == 0xA000 && !segaRegsSeen && rom.size() > 0x8000) {
        mapperType = Mapper::Korean;
        pageReg[2] = v;
    }
}

void Cartridge::writeMapper(u16 addr, u8 v) {
    // Registres du mapper Sega, « sous » le miroir RAM (le Bus écrit les
    // deux). Les cartouches Codemasters et coréennes n'ont pas de logique
    // à ces adresses.
    if (mapperType != Mapper::Sega)
        return;
    switch (addr) {
    case 0xFFFC: ramControl = v; break;  // contrôle RAM de sauvegarde
    case 0xFFFD: pageReg[0] = v; segaRegsSeen = true; break;
    case 0xFFFE: pageReg[1] = v; segaRegsSeen = true; break;
    case 0xFFFF: pageReg[2] = v; segaRegsSeen = true; break;
    default: break;
    }
}

// -----------------------------------------------------------------------------
//  Save-state — registres du mapper + RAM cartouche. La ROM n'est pas
//  sérialisée (elle vient du fichier) ; au chargement la RAM est marquée
//  modifiée pour que la persistance .sav reparte de l'état restauré.
// -----------------------------------------------------------------------------
void Cartridge::serialize(StateIO& s) {
    u8 type = (u8)mapperType;
    s.u8v(type);
    if (s.loading())
        mapperType = (Mapper)type;   // un type coréen détecté est restauré
    s.bytes(pageReg, sizeof(pageReg));
    s.u8v(ramControl);
    s.bytes(&cartRam[0][0], sizeof(cartRam));
    s.boolv(cmRamEnabled);
    s.bytes(cmRam, sizeof(cmRam));
    s.boolv(segaRegsSeen);
    if (s.loading())
        saveRamDirty = true;
}
