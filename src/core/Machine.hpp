#pragma once
// =============================================================================
//  Machine — la carte mère : câble les puces et cadence une trame.
//  228 cycles CPU par ligne dans les deux normes ; NTSC : CPU 3 579 545 Hz,
//  262 lignes/trame (~59,92 Hz) ; PAL : CPU 3 546 893 Hz, 313 lignes
//  (~49,70 Hz). Boucle : exécuter le CPU, avancer le PSG, et à chaque
//  frontière de ligne faire avancer le VDP (rendu + interruptions).
// =============================================================================
#include "Bus.hpp"
#include "Cartridge.hpp"
#include "Io.hpp"
#include "Psg.hpp"
#include "Types.hpp"
#include "Vdp.hpp"
#include "Z80.hpp"
#include <cstdio>
#include <string>

class Machine {
public:
    static constexpr int kCpuClockNtsc  = 3579545;  // Hz
    static constexpr int kCpuClockPal   = 3546893;  // Hz
    static constexpr int kCyclesPerLine = 228;      // identique NTSC/PAL

    Machine();

    bool loadRom(const std::string& path);
    bool loadBios(const std::string& path);  // image BIOS, prioritaire au boot
    void reset();

    // Norme vidéo (réglage matériel : survit au reset). Propage l'horloge et
    // le format de trame au VDP et au PSG.
    void setRegion(Region r);
    Region region() const { return region_; }
    int cpuClock() const {
        return (region_ == Region::Pal) ? kCpuClockPal : kCpuClockNtsc;
    }

    // Exécute exactement une trame vidéo (jusqu'au frameDone() du VDP).
    void runFrame();

    // Bouton Pause de la console = NMI.
    void pressPause() { cpu.triggerNmi(); }

    // Trace optionnelle : si non nul, une ligne par instruction y est écrite
    // (PC, registres, mnémonique) — format stable pour diff.
    FILE* traceFile = nullptr;

    Bus bus;
    Z80 cpu{bus};
    Vdp vdp;
    Psg psg;
    Io  io;
    Cartridge cart;
    Cartridge bios;  // slot BIOS (mêmes ROM + mapper qu'une cartouche)

    u64 frameCount = 0;

private:
    Region region_ = Region::Ntsc;
    int lineCycles = 0;
    void traceStep();
};
