// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#pragma once
// =============================================================================
//  Bus — le Bus EST le plan mémoire de la Master System.
//  Espace mémoire : 0x0000-0xBFFF média actif (BIOS ou cartouche, via leur
//  mapper), 0xC000-0xDFFF RAM 8 Ko, miroir en 0xE000-0xFFFF. Les écritures
//  0xFFFC-0xFFFF touchent À LA FOIS le miroir RAM et les registres du mapper
//  (comportement réel ; chaque média a son propre mapper à l'écoute du bus).
//  Le port 0x3E (contrôle mémoire, bit à 1 = désactivé) choisit le média :
//  bit 7 extension, bit 6 cartouche, bit 5 carte, bit 4 work RAM, bit 3 BIOS,
//  bit 2 puce E/S. Le BIOS a priorité ; sans média actif, lecture = 0xFF
//  (bus flottant). Les slots carte et extension sont toujours vides.
//  Espace E/S (8 bits, décodage partiel par plages de 0x40) :
//    0x00-0x3F : pair -> contrôle mémoire (0x3E), impair -> contrôle E/S (0x3F)
//    0x40-0x7F : lecture pair -> VCounter, impair -> HCounter ; écriture -> PSG
//    0x80-0xBF : pair -> VDP données, impair -> VDP contrôle
//    0xC0-0xFF : lecture pair -> 0xDC, impair -> 0xDD ; 0xFC/0xFD -> SDSC
//  En Game Gear, les ports 0x00-0x06 sont soustraits à la règle pair/impair :
//  0x00 Start/région, 0x01-0x05 EXT/série, 0x06 stéréo PSG.
// =============================================================================
#include "Types.hpp"

class Cartridge;
class Vdp;
class Psg;
class Io;

class Bus {
public:
    void attach(Cartridge* cart, Cartridge* bios, Vdp* vdp, Psg* psg, Io* io);
    void reset();

    // Modèle de console : réglage matériel (Machine::setModel), survit au
    // reset. Conditionne le décodage des ports 0x00-0x06.
    void setModel(Model m) { model = m; }

    u8   read8(u16 addr);
    void write8(u16 addr, u8 v);

    u8   ioRead(u8 port);
    void ioWrite(u8 port, u8 v);

    u8 ram[0x2000]{};

private:
    // Média mappé en 0x0000-0xBFFF selon le port 0x3E, nullptr si aucun.
    Cartridge* activeMedium();

    Cartridge* cart = nullptr;
    Cartridge* bios = nullptr;
    Vdp* vdp = nullptr;
    Psg* psg = nullptr;
    Io*  io  = nullptr;

    u8 memControl = 0;  // port 0x3E (bit à 1 = désactivé) ; 0 = tout activé
    Model model = Model::Sms;
};
