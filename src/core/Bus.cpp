// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#include "Bus.hpp"
#include "Cartridge.hpp"
#include "Vdp.hpp"
#include "Psg.hpp"
#include "Io.hpp"

void Bus::attach(Cartridge* c, Cartridge* b, Vdp* v, Psg* p, Io* i) {
    cart = c; bios = b; vdp = v; psg = p; io = i;
}

void Bus::reset() {
    // Mise sous tension : tout activé. Le BIOS étant prioritaire, la console
    // démarre sur lui quand une image BIOS est chargée, sinon sur la cartouche.
    memControl = 0;
}

Cartridge* Bus::activeMedium() {
    if (!(memControl & 0x08) && bios && bios->loaded())
        return bios;
    if (!(memControl & 0x40) && cart && cart->loaded())
        return cart;
    return nullptr;  // carte (bit 5) et extension (bit 7) : slots vides
}

u8 Bus::read8(u16 addr) {
    if (addr < 0xC000) {
        Cartridge* m = activeMedium();
        return m ? m->read(addr) : 0xFF;  // aucun média actif : bus flottant
    }
    if (memControl & 0x10)
        return 0xFF;  // work RAM désactivée
    // RAM 8 Ko, miroir sur 0xC000-0xFFFF.
    return ram[addr & 0x1FFF];
}

void Bus::write8(u16 addr, u8 v) {
    if (addr < 0x8000)
        return;  // ROM : écriture ignorée
    if (addr < 0xC000) {
        if (Cartridge* m = activeMedium())
            m->write(addr, v);  // RAM cartouche éventuelle
        return;
    }
    if (!(memControl & 0x10))
        ram[addr & 0x1FFF] = v;
    // Les registres du mapper vivent « sous » le miroir RAM ; chaque média a
    // son propre mapper qui latche l'écriture, work RAM active ou non.
    if (addr >= 0xFFFC) {
        if (cart) cart->writeMapper(addr, v);
        if (bios) bios->writeMapper(addr, v);
    }
}

u8 Bus::ioRead(u8 port) {
    switch (port & 0xC0) {
    case 0x00:
        // SMS2 : lecture non fiable ; on retourne 0xFF (bus flottant).
        return 0xFF;
    case 0x40:
        return (port & 1) ? vdp->hCounter() : vdp->vCounter();
    case 0x80:
        return (port & 1) ? vdp->readStatus() : vdp->readData();
    default:
        return io->readPort((port & 1) ? 0xDD : 0xDC);
    }
}

void Bus::ioWrite(u8 port, u8 v) {
    switch (port & 0xC0) {
    case 0x00:
        if (port & 1) io->writeIoControl(v);
        else          memControl = v;  // 0x3E : sélection des médias
        return;
    case 0x40:
        psg->write(v);
        return;
    case 0x80:
        if (port & 1) vdp->writeControl(v);
        else          vdp->writeData(v);
        return;
    default:
        // La plage 0xC0-0xFF est en lecture seule sur le vrai matériel,
        // SAUF la convention SDSC (0xFC/0xFD) utilisée par le homebrew.
        if (port == 0xFC) io->writeSdscControl(v);
        else if (port == 0xFD) io->writeSdscData(v);
        return;
    }
}
