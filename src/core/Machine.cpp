// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#include "Machine.hpp"

Machine::Machine() {
    bus.attach(&cart, &bios, &vdp, &psg, &io);
    bios.savEnabled = false;  // un BIOS n'a pas de RAM de sauvegarde sur pile
    setRegion(Region::Ntsc);
}

void Machine::setRegion(Region r) {
    region_ = r;
    vdp.setRegion(r);
    psg.setClock(cpuClock());
}

bool Machine::loadRom(const std::string& path) {
    if (!cart.load(path))
        return false;
    reset();
    return true;
}

bool Machine::loadBios(const std::string& path) {
    if (!bios.load(path))
        return false;
    reset();
    return true;
}

void Machine::reset() {
    bus.reset();
    cart.reset();
    bios.reset();
    cpu.reset();
    vdp.reset();
    psg.reset();
    io.reset();
    lineCycles = 0;
    frameCount = 0;
}

// Lecture mémoire « froide » pour le désassembleur (pas d'effet de bord :
// tout l'espace mémoire SMS est sans effet de bord en lecture).
static u8 disasmRead(void* ctx, u16 addr) {
    return static_cast<Bus*>(ctx)->read8(addr);
}

void Machine::traceStep() {
    char mnemo[64];
    z80Disassemble(cpu.regs.pc, disasmRead, &bus, mnemo, sizeof(mnemo));
    const Z80::Regs& r = cpu.regs;
    fprintf(traceFile,
            "%08llu PC=%04X AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X "
            "SP=%04X %s\n",
            static_cast<unsigned long long>(cpu.cycles), r.pc, r.af, r.bc,
            r.de, r.hl, r.ix, r.iy, r.sp, mnemo);
}

void Machine::runFrame() {
    do {
        cpu.setIrqLine(vdp.irqPending());
        if (traceFile)
            traceStep();
        int c = cpu.step();
        psg.runCycles(c);
        lineCycles += c;
        while (lineCycles >= kCyclesPerLine) {
            lineCycles -= kCyclesPerLine;
            vdp.runLine();
            // Une interruption levée par cette ligne doit pouvoir être vue
            // par le CPU dès la prochaine instruction.
            cpu.setIrqLine(vdp.irqPending());
        }
    } while (!vdp.frameDone());
    frameCount++;
}
