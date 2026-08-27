// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

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
#include "Ym2413.hpp"
#include "Z80.hpp"
#include <cstdio>
#include <string>

class StateIO;

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

    // Modèle émulé (SMS ou Game Gear). Auto-détecté par loadRom() sur
    // l'extension « .gg » ; propagé au VDP (CRAM 12 bits) et au Bus
    // (ports 0x00-0x06). Réglage matériel : survit au reset.
    void setModel(Model m);
    Model model() const { return model_; }

    // Exécute exactement une trame vidéo (jusqu'au frameDone() du VDP).
    void runFrame();

    // Save-states : fichier binaire versionné — en-tête « SESAMEST »,
    // version, modèle et région (le chargement les vérifie et refuse un
    // état pris sur une autre machine). À prendre en FRONTIÈRE de trame :
    // le framebuffer du VDP n'est pas sérialisé, la trame suivante le
    // reconstruit. Retourne false (avec message sur stderr) en cas d'échec ;
    // un chargement échoué peut laisser la machine dans un état mixte —
    // les frontends font un reset() dans ce cas.
    bool saveState(const std::string& path);
    bool loadState(const std::string& path);

    // Bouton Pause de la console = NMI. La Game Gear native n'a PAS de
    // bouton Pause : son Start est un simple bit lu sur le port 0x00
    // (Io::Button::Start), jamais un NMI. En mode compatibilité SMS
    // (adaptateur), on accorde le NMI : les jeux SMS l'attendent
    // (choix pragmatique documenté).
    void pressPause() {
        if (model_ != Model::GameGear)
            cpu.triggerNmi();
    }

    // Trace optionnelle : si non nul, une ligne par instruction y est écrite
    // (PC, registres, mnémonique) — format stable pour diff.
    FILE* traceFile = nullptr;

    Bus bus;
    Z80 cpu{bus};
    Vdp vdp;
    Psg psg;
    Ym2413 ym;   // unité FM (SMS japonaise) — muette tant que rien ne l'écrit
    Io  io;
    Cartridge cart;
    Cartridge bios;  // slot BIOS (mêmes ROM + mapper qu'une cartouche)

    u64 frameCount = 0;

private:
    Region region_ = Region::Ntsc;
    Model  model_  = Model::Sms;
    int lineCycles = 0;
    void traceStep();
    void serializeAll(StateIO& s);  // corps commun save/load (symétrique)
};
