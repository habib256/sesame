// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

// =============================================================================
//  Psg.cpp — SN76489 (variante Sega intégrée au VDP).
//
//  Référence de comportement : MAME `sn76496.cpp` (variante SEGA_PSG) et la
//  doc SMS Power!. Points propres à la variante Sega :
//   - LFSR de bruit sur 16 bits, ré-armé à 0x8000 à chaque écriture du
//     registre de bruit ;
//   - bruit blanc : bit injecté = parité(bit0 XOR bit3) ; périodique : bit0 ;
//   - période de tonalité 0 ou 1 = sortie collée à +1 (les jeux SMS s'en
//     servent pour jouer des samples PCM via le registre de volume).
//
//  Chaîne audio : l'horloge puce est l'horloge CPU (3 579 545 Hz), divisée
//  par 16 en interne -> un « tick » à 223 721,5 Hz. runCycles() avance tick
//  par tick, moyenne les sorties entre deux échantillons 44 100 Hz (filtre
//  boîte, suffisant en v1) et pousse le résultat dans un anneau que le
//  frontend vide avec readSamples().
// =============================================================================
#include "Psg.hpp"

namespace {

// Diviseur interne : un tick PSG toutes les 16 périodes d'horloge.
// (L'horloge CPU/puce elle-même est un membre : 3 579 545 Hz NTSC ou
// 3 546 893 Hz PAL, poussée par Machine::setRegion.)
constexpr int kClockDiv = 16;

// Table d'atténuation logarithmique : 16 pas de 2 dB, 0xF = silence.
// valeur(i) = 8000 * 10^(-i/10). Amplitude max par canal ~8000, donc la
// somme des 4 canaux (32 000) tient dans un s16 sans écrêtage.
constexpr s16 kVolumeTable[16] = {
    8000, 6355, 5048, 4010, 3186, 2531, 2010, 1597,
    1268, 1008,  800,  636,  505,  401,  319,    0,
};

}  // namespace

void Psg::reset() {
    latchedChannel  = 0;
    latchedIsVolume = false;
    for (int i = 0; i < 4; ++i) {
        toneReg[i]     = 0;
        volume[i]      = 0xF;  // silence au démarrage
        toneCounter[i] = 0;
        toneOut[i]     = 1;
    }
    noiseLfsr   = 0x8000;
    noiseFF     = 0;
    clockAcc    = 0;
    resampleAcc = 0;
    sampleSum   = 0;
    sampleTicks = 0;
    ringR = ringW = 0;
}

void Psg::write(u8 v) {
    if (v & 0x80) {
        // Octet de VERROU : choisit canal + type, et porte les 4 bits bas
        // du registre visé.
        latchedChannel  = (v >> 5) & 3;
        latchedIsVolume = (v & 0x10) != 0;
        if (latchedIsVolume) {
            volume[latchedChannel] = v & 0x0F;
        } else if (latchedChannel == 3) {
            // Registre de bruit : 4 bits utiles, réécrits en entier ;
            // chaque écriture ré-arme le LFSR (comportement Sega).
            toneReg[3] = v & 0x0F;
            noiseLfsr  = 0x8000;
        } else {
            toneReg[latchedChannel] =
                (u16)((toneReg[latchedChannel] & 0x3F0) | (v & 0x0F));
        }
    } else {
        // Octet de DONNÉES : complète le registre verrouillé.
        if (latchedIsVolume) {
            volume[latchedChannel] = v & 0x0F;
        } else if (latchedChannel == 3) {
            // Le bruit n'a pas de bits hauts : les 4 bits bas sont réécrits.
            toneReg[3] = v & 0x0F;
            noiseLfsr  = 0x8000;
        } else {
            // Tonalité : bits 5-0 de la donnée -> bits 9-4 du registre.
            toneReg[latchedChannel] =
                (u16)((toneReg[latchedChannel] & 0x00F) | ((v & 0x3F) << 4));
        }
    }
}

// Un pas d'horloge PSG (= 16 cycles CPU).
void Psg::tick() {
    // --- Canaux 0-2 : ondes carrées. ---
    for (int ch = 0; ch < 3; ++ch) {
        int period = toneReg[ch] & 0x3FF;
        if (period <= 1) {
            // Période 0 ou 1 : sortie constamment à +1 (astuce samples SMS).
            toneOut[ch] = 1;
            continue;
        }
        if (--toneCounter[ch] <= 0) {
            toneCounter[ch] = (s16)period;
            toneOut[ch] ^= 1;
        }
    }

    // --- Canal 3 : bruit. ---
    int period;
    switch (toneReg[3] & 3) {
    case 0:  period = 0x10; break;
    case 1:  period = 0x20; break;
    case 2:  period = 0x40; break;
    default: period = toneReg[2] & 0x3FF; break;  // suit la période du canal 2
    }
    if (period < 1)
        period = 1;  // garde-fou quand le canal 2 est à 0
    if (--toneCounter[3] <= 0) {
        toneCounter[3] = (s16)period;
        // Le LFSR n'avance que sur le front MONTANT de la bascule /2,
        // comme sur la puce réelle (le bruit sort donc à période/2... x2).
        noiseFF ^= 1;
        if (noiseFF) {
            // Sortie = bit 0 ; décalage à droite ; bit injecté en bit 15 :
            //   blanc      : parité(bit0 XOR bit3)  (= bit0 ^ bit3)
            //   périodique : bit0
            u16 in = (toneReg[3] & 4) ? (u16)((noiseLfsr ^ (noiseLfsr >> 3)) & 1)
                                      : (u16)(noiseLfsr & 1);
            noiseLfsr = (u16)((noiseLfsr >> 1) | (in << 15));
        }
    }
}

// Sortie mixée instantanée : somme unipolaire des 4 canaux.
s16 Psg::mix() const {
    int sum = 0;
    for (int ch = 0; ch < 3; ++ch)
        if (toneOut[ch])
            sum += kVolumeTable[volume[ch]];
    if (noiseLfsr & 1)  // la sortie du bruit est le bit 0 du LFSR
        sum += kVolumeTable[volume[3]];
    return (s16)sum;  // max 4 * 8000 = 32000 < 32767
}

// Pousse un échantillon dans l'anneau. S'il est plein (le frontend ne vide
// pas assez vite), on écrase le PLUS ANCIEN : perdre du vieux son vaut mieux
// que bloquer l'émulation ou dériver.
void Psg::pushSample(s16 s) {
    int next = (ringW + 1) % kRingSize;
    if (next == ringR)
        ringR = (ringR + 1) % kRingSize;  // écrasement du plus ancien
    ring[ringW] = s;
    ringW = next;
}

void Psg::runCycles(int cpuCycles) {
    clockAcc += (u64)cpuCycles;
    while (clockAcc >= (u64)kClockDiv) {
        clockAcc -= (u64)kClockDiv;
        tick();

        // Rééchantillonnage ticks (~223,7 kHz NTSC / ~221,7 kHz PAL) ->
        // 44 100 Hz sans flottants : on additionne kSampleRate*kClockDiv
        // (= 705 600) par tick et on émet un échantillon chaque fois qu'on
        // dépasse l'horloge CPU. L'échantillon émis est la MOYENNE des
        // sorties accumulées depuis le précédent (~5 ticks), ce qui fait
        // office de filtre anti-repliement rudimentaire.
        sampleSum += mix();
        sampleTicks++;
        resampleAcc += (u32)(kSampleRate * kClockDiv);
        if (resampleAcc >= (u32)cpuClock) {
            resampleAcc -= (u32)cpuClock;
            pushSample((s16)(sampleSum / sampleTicks));
            sampleSum   = 0;
            sampleTicks = 0;
        }
    }
}

int Psg::readSamples(s16* out, int max) {
    int n = 0;
    while (n < max && ringR != ringW) {
        out[n++] = ring[ringR];
        ringR = (ringR + 1) % kRingSize;
    }
    return n;
}
