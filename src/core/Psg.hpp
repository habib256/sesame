// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#pragma once
// =============================================================================
//  Psg — Texas Instruments SN76489 (variante Sega intégrée au VDP).
//  3 canaux carrés + 1 canal bruit, atténuation 4 bits par canal.
//  Horloge puce = horloge CPU (3 579 545 Hz) ; diviseur interne /16.
//  Le cœur produit des échantillons s16 mono à kSampleRate dans un anneau
//  que le frontend vient vider (GUI : audio temps réel ; headless : WAV).
//  Rééchantillonnage par synthèse à bande limitée (voir Psg.cpp).
// =============================================================================
#include "Types.hpp"

class Psg {
public:
    static constexpr int kSampleRate = 44100;

    void reset();

    // Horloge CPU/puce en Hz (NTSC ou PAL) — pilote le rééchantillonnage.
    // Réglage matériel poussé par Machine::setRegion, survit au reset.
    void setClock(int hz) { cpuClock = hz; }

    // Écriture CPU (port 0x40-0x7F en écriture, canoniquement 0x7F).
    void write(u8 v);

    // Game Gear : registre stéréo (port 0x06) — bit n = canal n vers la
    // DROITE, bit n+4 = canal n vers la GAUCHE. Mémorisé seulement : la
    // sortie v1 reste mono (équivalent du haut-parleur interne, qui somme
    // les deux voies). TODO : sortie stéréo réelle (casque).
    void writeStereo(u8 v) { stereoMask = v; }

    // Avance l'horloge du nombre de cycles CPU écoulés et pousse les
    // échantillons produits dans l'anneau interne.
    void runCycles(int cpuCycles);

    // Vide jusqu'à `max` échantillons dans `out`, retourne le nombre écrit.
    int readSamples(s16* out, int max);

private:
    // Détails (compteurs de tonalité, LFSR de bruit, accumulateur de
    // rééchantillonnage, anneau) : voir Psg.cpp.
    int cpuClock = 3579545;  // Hz (NTSC par défaut)

    u8  stereoMask = 0xFF;  // GG : tous les canaux des deux côtés au reset
    u8  latchedChannel = 0;
    bool latchedIsVolume = false;
    u16 toneReg[4]{};      // 3 tonalités + contrôle bruit
    u8  volume[4]{};       // atténuations (0xF = silence)

    static constexpr int kRingSize = 8192;
    s16 ring[kRingSize]{};
    int ringR = 0, ringW = 0;

    s16 toneCounter[4]{};
    u8  toneOut[4]{};
    u16 noiseLfsr = 0x8000;
    u8  noiseFF = 0;    // bascule /2 en amont du LFSR (le LFSR n'avance
                        // que sur le front montant du diviseur de bruit)
    u64 clockAcc = 0;   // accumulateur cycles CPU -> ticks PSG (/16)

    // Rééchantillonnage ticks (223 721,5 Hz) -> kSampleRate par accumulateur
    // fractionnaire entier ; les transitions d'amplitude passent par une
    // synthèse à bande limitée (deltas + intégration, voir Psg.cpp).
    u32 resampleAcc = 0;
    u64 sampleIndex = 0;   // index absolu du prochain échantillon de sortie
    s16 lastAmp     = 0;   // dernière amplitude mixée vue par la synthèse
    s32 blipSum     = 0;   // intégrateur de sortie (point fixe kBlipScaleBits)
    static constexpr int kBlipBufSize = 32;  // > kBlipTaps + 1, puissance de 2
    s32 blipBuf[kBlipBufSize]{};             // anneau de deltas par échantillon

    void tick();             // un pas d'horloge PSG (16 cycles CPU)
    s16  mix() const;        // sortie mixée instantanée des 4 canaux
    void addDelta(int delta);   // dépose une transition à la position courante
    void finalizeSample();      // intègre et pousse l'échantillon terminé
    void pushSample(s16 s);  // pousse dans l'anneau (écrase si plein)
};
