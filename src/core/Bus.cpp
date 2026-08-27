// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#include "Bus.hpp"

#include "StateIO.hpp"
#include "Cartridge.hpp"
#include "Vdp.hpp"
#include "Psg.hpp"
#include "Ym2413.hpp"
#include "Io.hpp"

void Bus::attach(Cartridge* c, Cartridge* b, Vdp* v, Psg* p, Ym2413* y,
                 Io* i) {
    cart = c; bios = b; vdp = v; psg = p; ym = y; io = i;
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
    if (addr < 0xC000) {
        // Toute écriture sous 0xC000 est relayée au média actif : selon le
        // mapper, c'est une écriture ROM ignorée, la RAM cartouche, un
        // registre Codemasters (0x0000/0x4000/0x8000) ou la page coréenne
        // (0xA000).
        if (Cartridge* m = activeMedium())
            m->write(addr, v);
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
        // Game Gear : ports dédiés 0x00-0x06 (Start/région, EXT, série).
        if (model == Model::GameGear && port <= 0x06)
            return io->readGgPort(port);
        // SMS2 : lecture non fiable ; on retourne 0xFF (bus flottant).
        return 0xFF;
    case 0x40:
        return (port & 1) ? vdp->hCounter() : vdp->vCounter();
    case 0x80:
        return (port & 1) ? vdp->readStatus() : vdp->readData();
    default:
        // 0xF2 : détection de l'unité FM (SMS japonaise) — relit les bits
        // 0-1 du contrôle audio quand l'unité est présente.
        if (port == 0xF2 && model == Model::Sms && ym)
            return ym->readControl();
        return io->readPort((port & 1) ? 0xDD : 0xDC);
    }
}

void Bus::ioWrite(u8 port, u8 v) {
    switch (port & 0xC0) {
    case 0x00:
        if (model == Model::GameGear && port <= 0x06) {
            // 0x06 : stéréo PSG ; 0x00-0x05 : Start/EXT/série, écritures
            // sans effet (0x00 est en lecture seule, le lien série n'est
            // pas émulé).
            if (port == 0x06) psg->writeStereo(v);
            return;
        }
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
        // SAUF l'unité FM (0xF0-0xF2, SMS japonaise) et la convention SDSC
        // (0xFC/0xFD) utilisée par le homebrew.
        if (model == Model::Sms && ym) {
            if (port == 0xF0) { ym->writeAddr(v); return; }
            if (port == 0xF1) { ym->writeData(v); return; }
            if (port == 0xF2) { ym->writeControl(v); return; }
        }
        if (port == 0xFC) io->writeSdscControl(v);
        else if (port == 0xFD) io->writeSdscData(v);
        return;
    }
}

// -----------------------------------------------------------------------------
//  Save-state — work RAM + contrôle mémoire (le modèle est un réglage
//  matériel, enregistré dans l'en-tête par Machine).
// -----------------------------------------------------------------------------
void Bus::serialize(StateIO& s) {
    s.bytes(ram, sizeof(ram));
    s.u8v(memControl);
}
