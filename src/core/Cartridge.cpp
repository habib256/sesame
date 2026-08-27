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

namespace {
// Octets à bits inversés (pages « miroirs » du mapper Janggun).
struct BitRev {
    u8 t[256];
    BitRev() {
        for (int i = 0; i < 256; ++i) {
            u8 v = 0;
            for (int b = 0; b < 8; ++b)
                if (i & (1 << b)) v |= (u8)(0x80 >> b);
            t[i] = v;
        }
    }
};
const BitRev kBitRev;
}  // namespace

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
    eePath.clear();
    eeDirty = false;
    if (eepromEnabled && savEnabled) {
        eePath = savePath.substr(0, savePath.size() - 4) + ".eeprom";
        std::ifstream f(eePath, std::ios::binary);
        if (f)
            f.read(reinterpret_cast<char*>(ee.data), sizeof(ee.data));
    }

    reset();
    return true;
}

void Cartridge::persistSaveRam() {
    if (eeDirty && !eePath.empty()) {
        std::ofstream f(eePath, std::ios::binary | std::ios::trunc);
        if (f) {
            f.write(reinterpret_cast<const char*>(ee.data), sizeof(ee.data));
            if (f) eeDirty = false;
        }
    }
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
    pageReg8[0] = 2; pageReg8[1] = 3;   // Janggun : paires des pages 16 Ko
    pageReg8[2] = 4; pageReg8[3] = 5;
    ramControl = 0;
    cmRamEnabled = false;
    segaRegsSeen = false;
    // 93C46 : lignes au repos, contenu CONSERVÉ (c'est une EEPROM).
    ee.phase = 0; ee.bits = 0; ee.outBits = 0;
    ee.writeEnabled = false; ee.doLine = true;
    ee.prevClk = false; ee.cs = false; ee.wral = false;
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

    if (mapperType == Mapper::Janggun && addr >= 0x4000) {
        // Janggun : quatre fenêtres de 8 Ko, pages à octets miroirs si le
        // bit 6 de la page est levé.
        const int slot = (addr - 0x4000) >> 13;
        const u8  page = pageReg8[slot];
        const int mask8 = romMask * 2 + 1;
        const u8  byte = rom[(static_cast<size_t>(page & 0x3F & mask8) << 13) |
                             (addr & 0x1FFF)];
        return (page & 0x40) ? kBitRev.t[byte] : byte;
    }

    // EEPROM 93C46 : la fenêtre 0x8000-0xBFFF devient l'interface série
    // (ligne DO en bit 0, autres bits hauts).
    if (eepromEnabled && addr >= 0x8000)
        return static_cast<u8>(0xFE | (ee.doLine ? 1 : 0));

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

    if (mapperType == Mapper::Janggun) {
        switch (addr) {
        case 0x4000: pageReg8[0] = v; return;
        case 0x6000: pageReg8[1] = v; return;
        case 0x8000: pageReg8[2] = v; return;
        case 0xA000: pageReg8[3] = v; return;
        default: return;
        }
    }

    if (mapperType == Mapper::Korean) {
        if (addr == 0xA000)
            pageReg[2] = v;
        return;
    }

    // EEPROM 93C46 : les écritures dans la fenêtre posent les lignes
    // DI/CLK/CS (bits 0-2).
    if (eepromEnabled && addr >= 0x8000 && addr < 0xC000) {
        eeLines(v);
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
        return;
    }

    // Heuristique Janggun : l'adresse 0x6000 n'est un registre de page QUE
    // sur cette cartouche (8 Ko). On reprend les fenêtres 16 Ko courantes
    // en paires 8 Ko puis on applique l'écriture.
    if (addr == 0x6000 && rom.size() > 0x8000) {
        mapperType = Mapper::Janggun;
        pageReg8[0] = static_cast<u8>(pageReg[1] * 2);
        pageReg8[1] = v;
        pageReg8[2] = static_cast<u8>(pageReg[2] * 2);
        pageReg8[3] = static_cast<u8>(pageReg[2] * 2 + 1);
    }
}

void Cartridge::writeMapper(u16 addr, u8 v) {
    // Registres du mapper Sega, « sous » le miroir RAM (le Bus écrit les
    // deux). Les cartouches Codemasters et coréennes n'ont pas de logique
    // à ces adresses ; la Janggun y écoute ses paires 16 Ko.
    if (mapperType == Mapper::Janggun) {
        if (addr == 0xFFFE) {
            pageReg8[0] = static_cast<u8>(v * 2);
            pageReg8[1] = static_cast<u8>(v * 2 + 1);
        } else if (addr == 0xFFFF) {
            pageReg8[2] = static_cast<u8>(v * 2);
            pageReg8[3] = static_cast<u8>(v * 2 + 1);
        }
        return;
    }
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
//  EEPROM série 93C46 (protocole Microwire) — 64 mots de 16 bits.
//  Une commande = bit de START (1) + 2 bits d'opcode + 6 bits d'adresse,
//  décalés sur les fronts MONTANTS de CLK pendant que CS est haut :
//    READ  (10) : sort un 0 factice puis les 16 bits MSB en tête ;
//    WRITE (01) : reçoit 16 bits puis écrit (si EWEN) ;
//    ERASE (11) : mot à 0xFFFF ;
//    00 + adr 11xxxx = EWEN, 00xxxx = EWDS, 10xxxx = ERAL, 01xxxx = WRAL.
//  CS bas remet la machine à états en attente de start (DO au repos = 1).
// -----------------------------------------------------------------------------
void Cartridge::eeClockIn(int di) {
    switch (ee.phase) {
    case 0:                       // attente du bit de start
        if (di) {
            ee.phase = 1;
            ee.shiftIn = 0;
            ee.bits = 0;
        }
        break;
    case 1:                       // opcode + adresse (8 bits)
        ee.shiftIn = static_cast<u16>((ee.shiftIn << 1) | di);
        if (++ee.bits < 8)
            break;
        ee.op   = static_cast<u8>((ee.shiftIn >> 6) & 3);
        ee.addr = static_cast<u8>(ee.shiftIn & 0x3F);
        ee.bits = 0;
        ee.shiftIn = 0;
        ee.wral = false;
        switch (ee.op) {
        case 2:                   // READ
            ee.shiftOut = ee.data[ee.addr];
            ee.outBits = 16;
            ee.doLine = false;    // 0 factice avant les données
            ee.phase = 3;
            break;
        case 1:                   // WRITE
            ee.phase = 2;
            break;
        case 3:                   // ERASE
            if (ee.writeEnabled) {
                ee.data[ee.addr] = 0xFFFF;
                eeDirty = true;
            }
            ee.phase = 0;
            ee.doLine = true;     // prêt
            break;
        default:                  // 00 : sous-commande dans l'adresse
            switch ((ee.addr >> 4) & 3) {
            case 3: ee.writeEnabled = true;  ee.phase = 0; break;  // EWEN
            case 0: ee.writeEnabled = false; ee.phase = 0; break;  // EWDS
            case 2:                                                 // ERAL
                if (ee.writeEnabled) {
                    for (u16& w : ee.data) w = 0xFFFF;
                    eeDirty = true;
                }
                ee.phase = 0;
                break;
            case 1:                                                 // WRAL
                ee.wral = true;
                ee.phase = 2;
                break;
            }
            ee.doLine = true;
            break;
        }
        break;
    case 2:                       // 16 bits de données entrantes
        ee.shiftIn = static_cast<u16>((ee.shiftIn << 1) | di);
        if (++ee.bits < 16)
            break;
        if (ee.writeEnabled) {
            if (ee.wral)
                for (u16& w : ee.data) w = ee.shiftIn;
            else
                ee.data[ee.addr] = ee.shiftIn;
            eeDirty = true;
        }
        ee.phase = 0;
        ee.doLine = true;         // prêt (l'écriture réelle est instantanée)
        break;
    case 3:                       // 16 bits sortants (après le 0 factice)
        if (ee.outBits > 0) {
            ee.doLine = (ee.shiftOut & 0x8000) != 0;
            ee.shiftOut = static_cast<u16>(ee.shiftOut << 1);
            --ee.outBits;
        } else {
            ee.doLine = true;
            ee.phase = 0;
        }
        break;
    }
}

void Cartridge::eeLines(u8 v) {
    const bool cs  = (v & 4) != 0;
    const bool clk = (v & 2) != 0;
    const int  di  = v & 1;
    if (!cs) {
        // CS bas : machine à états au repos, DO relâchée.
        ee.phase = 0;
        ee.bits = 0;
        ee.outBits = 0;
        ee.doLine = true;
    } else if (clk && !ee.prevClk) {
        eeClockIn(di);            // front montant d'horloge
    }
    ee.cs = cs;
    ee.prevClk = clk;
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
    s.bytes(pageReg8, sizeof(pageReg8));
    s.u8v(ramControl);
    s.bytes(&cartRam[0][0], sizeof(cartRam));
    s.boolv(cmRamEnabled);
    s.bytes(cmRam, sizeof(cmRam));
    s.boolv(segaRegsSeen);
    for (u16& w : ee.data) s.u16v(w);
    s.u8v(ee.phase);
    s.u16v(ee.shiftIn);
    s.intv(ee.bits);
    s.u8v(ee.op);
    s.u8v(ee.addr);
    s.boolv(ee.wral);
    s.u16v(ee.shiftOut);
    s.intv(ee.outBits);
    s.boolv(ee.writeEnabled);
    s.boolv(ee.doLine);
    s.boolv(ee.prevClk);
    s.boolv(ee.cs);
    if (s.loading()) {
        saveRamDirty = true;
        eeDirty = true;
    }
}
