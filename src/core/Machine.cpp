// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#include "Machine.hpp"

#include <cctype>
#include <cstring>

#include "StateIO.hpp"

Machine::Machine() {
    bus.attach(&cart, &bios, &vdp, &psg, &ym, &io);
    psg.setFmSource(&ym);  // la sortie FM est mixée par le PSG
    bios.savEnabled = false;  // un BIOS n'a pas de RAM de sauvegarde sur pile
    setRegion(Region::Ntsc);
}

void Machine::setRegion(Region r) {
    region_ = r;
    vdp.setRegion(r);
    psg.setClock(cpuClock());
}

void Machine::setModel(Model m) {
    model_ = m;
    vdp.setModel(m);
    bus.setModel(m);
}

bool Machine::loadRom(const std::string& path) {
    if (!cart.load(path))
        return false;
    // Auto-détection du modèle par l'extension : « .gg » = Game Gear.
    // (Le mode compatibilité SMS-sur-GG — cartouche .sms via adaptateur —
    // n'est pas couvert ici : une .sms reste émulée en Master System.)
    auto dot = path.rfind('.');
    std::string ext = (dot == std::string::npos) ? "" : path.substr(dot);
    for (char& c : ext)
        c = (char)tolower((unsigned char)c);
    setModel(ext == ".gg" ? Model::GameGear : Model::Sms);
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
    ym.reset();
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
        ym.runCycles(c);   // AVANT le PSG : il vient chercher la sortie FM
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

// -----------------------------------------------------------------------------
//  Save-states — en-tête « SESAMEST » + version + modèle + région, puis
//  l'état de chaque puce dans un ordre fixe (liste symétrique, StateIO.hpp).
// -----------------------------------------------------------------------------
namespace {
constexpr char kStateMagic[8] = {'S','E','S','A','M','E','S','T'};
constexpr u32  kStateVersion  = 4;  // v2 +YM2413 ; v3 +mappers ; v4 +TMS
}  // namespace

void Machine::serializeAll(StateIO& s) {
    cpu.serialize(s);
    bus.serialize(s);
    vdp.serialize(s);
    psg.serialize(s);
    ym.serialize(s);
    io.serialize(s);
    cart.serialize(s);
    bios.serialize(s);
    s.intv(lineCycles);
    s.u64v(frameCount);
}

bool Machine::saveState(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "error: cannot open state file '%s' for writing\n",
                     path.c_str());
        return false;
    }
    StateIO s(f, StateIO::Mode::Save);
    u32 version = kStateVersion;
    u8  model   = (u8)model_;
    u8  region  = (u8)region_;
    s.bytes((u8*)(void*)kStateMagic, sizeof(kStateMagic));
    s.u32v(version);
    s.u8v(model);
    s.u8v(region);
    serializeAll(s);
    const bool ok = s.ok();
    std::fclose(f);
    if (!ok)
        std::fprintf(stderr, "error: short write to state file '%s'\n",
                     path.c_str());
    return ok;
}

bool Machine::loadState(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "error: cannot open state file '%s'\n",
                     path.c_str());
        return false;
    }
    StateIO s(f, StateIO::Mode::Load);
    char magic[8] = {};
    u32  version  = 0;
    u8   model = 0, region = 0;
    s.bytes((u8*)magic, sizeof(magic));
    s.u32v(version);
    s.u8v(model);
    s.u8v(region);
    bool ok = s.ok();
    if (ok && std::memcmp(magic, kStateMagic, sizeof(magic)) != 0) {
        std::fprintf(stderr, "error: '%s' is not a Sesame state file\n",
                     path.c_str());
        ok = false;
    }
    if (ok && version != kStateVersion) {
        std::fprintf(stderr, "error: state file version %u (expected %u)\n",
                     version, kStateVersion);
        ok = false;
    }
    if (ok && (model != (u8)model_ || region != (u8)region_)) {
        std::fprintf(stderr,
                     "error: state file is for another machine "
                     "(model/region mismatch)\n");
        ok = false;
    }
    if (ok) {
        serializeAll(s);
        ok = s.ok();
        if (!ok)
            std::fprintf(stderr, "error: truncated state file '%s'\n",
                         path.c_str());
    }
    std::fclose(f);
    return ok;
}
