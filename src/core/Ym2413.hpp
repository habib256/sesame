// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#pragma once
// =============================================================================
//  Ym2413 — unité FM optionnelle des Master System japonaises (OPLL).
//  9 canaux mélodiques, 2 opérateurs par canal (modulateur -> porteuse),
//  15 instruments ROM + 1 instrument utilisateur (registres 0x00-0x07).
//  Ports CPU : 0xF0 (sélection de registre), 0xF1 (donnée), 0xF2 (contrôle
//  audio — les jeux détectent l'unité FM en relisant ce registre).
//
//  Implémentation maison (réf. : application manual YM2413, rétro-ingénierie
//  publique de la famille OPL, MAME `ym2413.cpp` en lecture de comportement) :
//   - tables log-sin/exponentielle du matériel OPL (précision d'origine) ;
//   - enveloppes ADSR par opérateur, sustain/percussif, key scaling ;
//   - vibrato et trémolo (LFO) par drapeaux de patch ;
//   - PAS ENCORE : mode rythme (canaux 6-8 percussions) — TODO.
//  Horloge puce = horloge CPU ; un échantillon natif tous les 72 cycles
//  (~49,7 kHz NTSC). Le PSG vient chercher la sortie via takeSample()
//  (moyenne des échantillons natifs entre deux trames 44,1 kHz — la FM,
//  à base de sinus, tolère très bien ce filtre boîte, contrairement aux
//  carrées du PSG).
// =============================================================================
#include "Types.hpp"

class StateIO;

class Ym2413 {
public:
    void reset();

    // Ports CPU.
    void writeAddr(u8 v) { regLatch = v & 0x3F; }  // 0xF0
    void writeData(u8 v);                          // 0xF1
    void writeControl(u8 v) { control = v; }       // 0xF2
    u8   readControl() const { return control & 0x03; }  // détection FM

    // Avance l'horloge (cycles CPU) ; produit les échantillons natifs.
    void runCycles(int cpuCycles);

    // Sortie mixée pour UNE trame 44,1 kHz : moyenne des échantillons
    // natifs accumulés depuis le dernier appel (appelé par le PSG).
    s16 takeSample();

    // Save-state.
    void serialize(StateIO& s);

private:
    static constexpr int kNumChannels = 9;
    static constexpr int kClockDiv    = 72;   // cycles CPU par échantillon natif

    // --- Un opérateur (modulateur ou porteuse) ------------------------------
    struct Op {
        u32 phase = 0;       // accumulateur de phase (19 bits utiles)
        int egLevel = 127;   // atténuation d'enveloppe 0..127 (pas de 0,375 dB)
        u8  egPhase = 0;     // 0=repos, 1=attaque, 2=decay, 3=sustain, 4=release
        s32 fb1 = 0, fb2 = 0;  // deux dernières sorties (rétroaction modulateur)
    };

    // --- Un canal mélodique -------------------------------------------------
    struct Channel {
        u16 fnum = 0;        // F-number 9 bits
        u8  block = 0;       // octave 0-7
        bool keyOn = false;
        bool sustainOn = false;  // bit SUS du registre 0x2n
        u8  inst = 0;        // instrument 0 = utilisateur, 1-15 = ROM
        u8  vol = 0;         // atténuation porteuse 4 bits (pas de 3 dB)
        Op  mod, car;
    };

    u8 regLatch = 0;
    u8 control  = 0;             // registre 0xF2
    u8 regs[0x40]{};             // miroir des registres écrits
    u8 userPatch[8]{};           // registres 0x00-0x07
    Channel ch[kNumChannels];

    u32 lfoCounter = 0;          // compteur global des LFO (vibrato/trémolo)
    u64 clockAcc = 0;            // accumulateur cycles CPU -> échantillons natifs
    s32 sampleSum = 0;           // somme des échantillons natifs...
    int sampleCount = 0;         // ...pour la moyenne de takeSample()

    void keyOnOff(int c, bool on);
    const u8* patchFor(int c) const;   // 8 octets du patch du canal
    int  opOutput(Op& o, const u8* patch, int opIdx, int c, int fmInput);
    void advanceEnvelope(Op& o, const u8* patch, int opIdx, int c);
    int  computeSample();              // un échantillon natif (somme des canaux)
};
