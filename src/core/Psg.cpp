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
//  par tick et rééchantillonne vers 44 100 Hz par SYNTHÈSE À BANDE LIMITÉE
//  (technique popularisée par le « Blip Buffer » de Blargg, réimplémentée
//  ici) : les harmoniques d'une onde carrée dépassent la fréquence de
//  Nyquist de la sortie, et les échantillonner naïvement les replie dans le
//  spectre audible (aliasing). À la place, chaque TRANSITION d'amplitude
//  dépose dans un petit anneau la réponse indicielle d'un passe-bas idéal
//  fenêtré — une « marche adoucie » étalée sur kBlipTaps échantillons,
//  pré-calculée pour kBlipPhases positions sous-échantillon du front
//  (table générée par tools/make_blip_table.py). L'anneau contient des
//  DELTAS ; l'intégrateur blipSum reconstruit le signal filtré au moment
//  d'émettre chaque échantillon dans l'anneau de sortie (readSamples()).
// =============================================================================
#include "Psg.hpp"

#include "StateIO.hpp"

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

// Table des marches à bande limitée (kBlipStep, kBlipPhases, kBlipTaps,
// kBlipScaleBits) — générée, commitée dans le dépôt.
#include "PsgBlipTable.inc"

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
    stereoMask  = 0xFF;
    clockAcc    = 0;
    resampleAcc = 0;
    sampleIndex = 0;
    for (int s = 0; s < 2; ++s) {
        lastAmp[s] = 0;  // = mixSide() après reset (volumes à silence)
        blipSum[s] = 0;
        for (int i = 0; i < kBlipBufSize; ++i)
            blipBuf[s][i] = 0;
    }
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

// Sortie mixée instantanée d'une voie : somme unipolaire des canaux routés
// vers ce côté par le registre stéréo Game Gear (bits 0-3 = droite,
// bits 4-7 = gauche). En SMS le masque reste à 0xFF : les deux voies sont
// identiques, la sortie est un mono dupliqué.
s16 Psg::mixSide(int side) const {
    const int shift = (side == 0) ? 4 : 0;  // gauche : bits hauts
    int sum = 0;
    for (int ch = 0; ch < 3; ++ch)
        if (toneOut[ch] && (stereoMask & (1 << (ch + shift))))
            sum += kVolumeTable[volume[ch]];
    if ((noiseLfsr & 1) && (stereoMask & (1 << (3 + shift))))
        sum += kVolumeTable[volume[3]];
    return (s16)sum;  // max 4 * 8000 = 32000 < 32767
}

// Pousse une trame stéréo dans l'anneau. S'il est plein (le frontend ne
// vide pas assez vite), on écrase la PLUS ANCIENNE : perdre du vieux son
// vaut mieux que bloquer l'émulation ou dériver.
void Psg::pushFrame(s16 l, s16 r) {
    int next = (ringW + 1) % kRingFrames;
    if (next == ringR)
        ringR = (ringR + 1) % kRingFrames;  // écrasement du plus ancien
    ring[ringW * 2]     = l;
    ring[ringW * 2 + 1] = r;
    ringW = next;
}

// Dépose une transition d'amplitude d'une voie à la position temporelle
// courante : échantillon sampleIndex + resampleAcc/cpuClock. La marche
// s'étale sur les kBlipTaps échantillons À VENIR (front centré au milieu —
// le son sort avec kBlipTaps/2 échantillons de retard fixe, ~0,18 ms,
// imperceptible).
void Psg::addDelta(int side, int delta) {
    // Phase sous-échantillon du front, quantifiée sur kBlipPhases pas.
    int phase = (int)(((u64)resampleAcc * kBlipPhases) / (u32)cpuClock);
    for (int t = 0; t < kBlipTaps; ++t)
        blipBuf[side][(sampleIndex + (u64)t) & (kBlipBufSize - 1)] +=
            delta * kBlipStep[phase][t];
}

// L'échantillon sampleIndex vient d'être dépassé : plus aucune transition
// future ne peut le toucher (elles écrivent à partir de sampleIndex + 1).
// On intègre les deltas des deux voies, on libère les cases, et on émet.
void Psg::finalizeSample() {
    const int slot = (int)(sampleIndex & (kBlipBufSize - 1));
    s16 out[2];
    for (int s = 0; s < 2; ++s) {
        blipSum[s] += blipBuf[s][slot];
        blipBuf[s][slot] = 0;
        // Point fixe -> s16, arrondi au plus proche ; l'ondulation de Gibbs
        // peut dépasser transitoirement l'amplitude nominale, d'où l'écrêtage.
        s32 v = (blipSum[s] + (1 << (kBlipScaleBits - 1))) >> kBlipScaleBits;
        if (v < -32768) v = -32768;
        if (v > 32767)  v = 32767;
        out[s] = (s16)v;
    }
    pushFrame(out[0], out[1]);
}

void Psg::runCycles(int cpuCycles) {
    clockAcc += (u64)cpuCycles;
    while (clockAcc >= (u64)kClockDiv) {
        clockAcc -= (u64)kClockDiv;
        tick();

        // Seules les TRANSITIONS alimentent la synthèse : tant que la
        // sortie mixée d'une voie ne change pas, il n'y a rien à déposer.
        for (int s = 0; s < 2; ++s) {
            s16 amp = mixSide(s);
            if (amp != lastAmp[s]) {
                addDelta(s, amp - lastAmp[s]);
                lastAmp[s] = amp;
            }
        }

        // Cadence de sortie ticks (~223,7 kHz NTSC / ~221,7 kHz PAL) ->
        // 44 100 Hz sans flottants ni dérive : on additionne
        // kSampleRate*kClockDiv (= 705 600) par tick et on franchit une
        // borne d'échantillon chaque fois qu'on dépasse l'horloge CPU.
        resampleAcc += (u32)(kSampleRate * kClockDiv);
        if (resampleAcc >= (u32)cpuClock) {
            resampleAcc -= (u32)cpuClock;
            finalizeSample();
            sampleIndex++;
        }
    }
}

int Psg::readSamples(s16* out, int maxFrames) {
    int n = 0;
    while (n < maxFrames && ringR != ringW) {
        out[n * 2]     = ring[ringR * 2];
        out[n * 2 + 1] = ring[ringR * 2 + 1];
        ringR = (ringR + 1) % kRingFrames;
        ++n;
    }
    return n;
}

// -----------------------------------------------------------------------------
//  Save-state — registres et état de synthèse. L'anneau de sortie (audio en
//  vol vers le frontend) n'est pas sérialisé : il est vidé au chargement.
// -----------------------------------------------------------------------------
void Psg::serialize(StateIO& s) {
    s.u8v(stereoMask);
    s.u8v(latchedChannel);
    s.boolv(latchedIsVolume);
    for (int i = 0; i < 4; ++i) s.u16v(toneReg[i]);
    s.bytes(volume, sizeof(volume));
    for (int i = 0; i < 4; ++i) s.s16v(toneCounter[i]);
    s.bytes(toneOut, sizeof(toneOut));
    s.u16v(noiseLfsr);
    s.u8v(noiseFF);
    s.u64v(clockAcc);
    s.u32v(resampleAcc);
    s.u64v(sampleIndex);
    for (int v = 0; v < 2; ++v) {
        s.s16v(lastAmp[v]);
        s.s32v(blipSum[v]);
        for (int i = 0; i < kBlipBufSize; ++i) s.s32v(blipBuf[v][i]);
    }
    if (s.loading())
        ringR = ringW = 0;
}
