// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

// =============================================================================
//  Ym2413.cpp — synthèse FM OPLL, implémentation maison.
//
//  Principe : chaque canal a deux opérateurs sinusoïdaux ; la sortie du
//  MODULATEUR s'ajoute à la PHASE de la PORTEUSE (modulation de fréquence),
//  ce qui enrichit le spectre. Le modulateur peut se réinjecter lui-même
//  (rétroaction). Le timbre est défini par un « patch » de 8 octets — le
//  format des registres 0x00-0x07 (instrument utilisateur) ; les 15
//  instruments ROM utilisent le même format.
//
//  Le cœur numérique suit l'architecture OPL documentée par la
//  rétro-ingénierie publique : tout se calcule en LOGARITHME (les
//  atténuations s'ADDITIONNENT) puis repasse en linéaire par une table
//  exponentielle — exactement comme le silicium.
//    - kLogSin[i] = -log2(sin(...)) * 256 sur un quart d'onde ;
//    - kExp[i]    = (2^(i/256) - 1) * 1024 ;
//    - unité d'atténuation commune : 1/256 de doublement (6 dB / 256).
//      Pas d'enveloppe 0,375 dB = 16 unités ; TL 0,75 dB = 32 ; volume et
//      sustain-level 3 dB = 128.
//
//  Approximations assumées de la v1 (documentées, à resserrer au besoin) :
//    - cadences d'enveloppe : forme et ordre de grandeur du matériel
//      (attaque exponentielle, decay linéaire en dB), pas le cycle exact ;
//    - LFO vibrato ~6 Hz / trémolo ~3,7 Hz aux profondeurs nominales ;
//    - INSTRUMENTS ROM : jeu VÉRIFIÉ par la communauté (analyse du die,
//      doc andete, valeurs publiées dans emu2413/MIT) ;
//    - MODE RYTHME (reg 0x0E bit 5) : les canaux 6-8 deviennent cinq
//      percussions — grosse caisse (2 opérateurs FM), caisse claire,
//      charleston, tom et cymbale. Le vrai silicium mélange des bits de
//      phase des opérateurs 13/17 et un LFSR ; ici : tom = sinus pur,
//      caisse claire = carré de phase ⊕ bruit, charleston = bruit,
//      cymbale = carré du XOR des phases 7-mod/8-car — timbres plausibles,
//      pas cycle-exact (approximation documentée).
// =============================================================================
#include "Ym2413.hpp"

#include "StateIO.hpp"

#include <cmath>

namespace {

// --- Tables log-sin / exp du matériel OPL (générées au premier usage) --------
s16 kLogSin[256];
s16 kExp[256];
bool tablesReady = false;

void buildTables() {
    if (tablesReady) return;
    for (int i = 0; i < 256; ++i) {
        kLogSin[i] = (s16)std::lround(
            -std::log2(std::sin((i + 0.5) * M_PI / 512.0)) * 256.0);
        kExp[i] = (s16)std::lround((std::pow(2.0, i / 256.0) - 1.0) * 1024.0);
    }
    tablesReady = true;
}

// Atténuation (log2/256) -> amplitude linéaire ~0..4084.
inline int expLookup(int level) {
    if (level < 0) level = 0;
    if (level > 0x1FFF) return 0;  // sous le plancher : silence
    return ((kExp[(level & 0xFF) ^ 0xFF] | 0x400) << 1) >> (level >> 8);
}

// Sinus en log : index 10 bits (1024 = cycle complet), retourne l'amplitude
// signée après ajout de l'atténuation `att`. `rectified` : demi-onde
// redressée (bits DC/DM du patch, alternance sinus/zéro).
inline int opSine(int idx, int att, bool rectified) {
    idx &= 1023;
    int quarter = idx & 255;
    if (idx & 256) quarter = 255 - quarter;      // miroir du quart d'onde
    const bool negative = (idx & 512) != 0;
    if (negative && rectified)
        return 0;                                 // demi-onde supprimée
    const int mag = expLookup(kLogSin[quarter] + att);
    return negative ? -mag : mag;
}

// Multiplicateurs de fréquence (x2 pour représenter le facteur 1/2).
constexpr int kMult2[16] = {1, 2, 4, 6, 8, 10, 12, 14,
                            16, 18, 20, 20, 24, 24, 30, 30};

// Vibrato : onde 8 pas, profondeur proportionnelle au F-number.
constexpr int kPmWave[8] = {0, 3, 7, 3, 0, -3, -7, -3};

// Key scaling level : atténuation de base par F-number haut (unités 3 dB/8,
// table OPL classique), modulée par l'octave et le réglage KSL du patch.
constexpr int kKslBase[16] = {0, 24, 32, 37, 40, 43, 45, 47,
                              48, 50, 51, 52, 53, 54, 55, 56};

// --- Instruments ROM ---------------------------------------------------------
// Jeu de patches VÉRIFIÉ : valeurs du YM2413 issues de la rétro-ingénierie
// communautaire (documentation andete d'après l'analyse du die, jeu publié
// dans emu2413 de Mitsutaka Okazaki, licence MIT — merci à eux).
// Format = registres 0x00-0x07 : [mod AM|VIB|EGT|KSR|MULT, car idem,
// KSLm|TL, KSLc|DC|DM|FB, AR/DR mod, AR/DR car, SL/RR mod, SL/RR car].
constexpr u8 kRomPatch[15][8] = {
    {0x71, 0x61, 0x1E, 0x17, 0xD0, 0x78, 0x00, 0x17},  // 1  violon
    {0x13, 0x41, 0x1A, 0x0D, 0xD8, 0xF7, 0x23, 0x13},  // 2  guitare
    {0x13, 0x01, 0x99, 0x00, 0xF2, 0xC4, 0x21, 0x23},  // 3  piano
    {0x11, 0x61, 0x0E, 0x07, 0x8D, 0x64, 0x70, 0x27},  // 4  flûte
    {0x32, 0x21, 0x1E, 0x06, 0xE1, 0x76, 0x01, 0x28},  // 5  clarinette
    {0x31, 0x22, 0x16, 0x05, 0xE0, 0x71, 0x00, 0x18},  // 6  hautbois
    {0x21, 0x61, 0x1D, 0x07, 0x82, 0x81, 0x11, 0x07},  // 7  trompette
    {0x33, 0x21, 0x2D, 0x13, 0xB0, 0x70, 0x00, 0x07},  // 8  orgue
    {0x61, 0x61, 0x1B, 0x06, 0x64, 0x65, 0x10, 0x17},  // 9  cor
    {0x41, 0x61, 0x0B, 0x18, 0x85, 0xF0, 0x81, 0x07},  // 10 synthétiseur
    {0x33, 0x01, 0x83, 0x11, 0xEA, 0xEF, 0x10, 0x04},  // 11 clavecin
    {0x17, 0xC1, 0x24, 0x07, 0xF8, 0xF8, 0x22, 0x12},  // 12 vibraphone
    {0x61, 0x50, 0x0C, 0x05, 0xD2, 0xF5, 0x40, 0x42},  // 13 basse synthé
    {0x01, 0x01, 0x55, 0x03, 0xE9, 0x90, 0x03, 0x02},  // 14 basse acoustique
    {0x41, 0x41, 0x89, 0x03, 0xF1, 0xE4, 0xC0, 0x13},  // 15 guitare élec.
};

// Patches du mode rythme, mêmes sources (grosse caisse 2 opérateurs,
// charleston/caisse claire, tom/cymbale).
constexpr u8 kRhythmPatch[3][8] = {
    {0x01, 0x01, 0x18, 0x0F, 0xDF, 0xF8, 0x6A, 0x6D},  // grosse caisse
    {0x01, 0x01, 0x00, 0x00, 0xC8, 0xD8, 0xA7, 0x68},  // charleston / claire
    {0x05, 0x01, 0x00, 0x00, 0xF8, 0xAA, 0x59, 0x55},  // tom / cymbale
};

}  // namespace

void Ym2413::reset() {
    buildTables();
    regLatch = 0;
    control = 0;
    for (u8& r : regs) r = 0;
    for (u8& b : userPatch) b = 0;
    for (Channel& c : ch) c = Channel{};
    lfoCounter = 0;
    noiseLfsr = 1;
    clockAcc = 0;
    sampleSum = 0;
    sampleCount = 0;
}

const u8* Ym2413::patchFor(int c) const {
    return ch[c].inst == 0 ? userPatch : kRomPatch[ch[c].inst - 1];
}

void Ym2413::keyOnOp(Op& o) {
    // Key ON d'un opérateur : phase remise à zéro, enveloppe en attaque.
    o.phase = 0;
    o.egLevel = 127;
    o.egPhase = 1;
}

void Ym2413::keyOffOp(Op& o) {
    if (o.egPhase != 0)
        o.egPhase = 4;   // relâchement
}

void Ym2413::keyOnOff(int c, bool on) {
    Channel& chan = ch[c];
    if (on && !chan.keyOn) {
        keyOnOp(chan.mod);
        keyOnOp(chan.car);
    } else if (!on && chan.keyOn) {
        keyOffOp(chan.mod);
        keyOffOp(chan.car);
    }
    chan.keyOn = on;
}

void Ym2413::writeData(u8 v) {
    const u8 old = regs[regLatch];
    regs[regLatch] = v;
    if (regLatch < 0x08) {
        userPatch[regLatch] = v;
        return;
    }
    if (regLatch == 0x0E) {
        // Mode rythme : bit 5 = activation ; bits 4-0 = key des percussions
        // (BD, SD, TOM, CYM, HH). Chaque front pilote SON opérateur.
        struct { u8 bit; Op* op1; Op* op2; } keys[] = {
            {0x10, &ch[6].mod, &ch[6].car},  // grosse caisse : 2 opérateurs
            {0x08, &ch[7].car, nullptr},     // caisse claire
            {0x04, &ch[8].mod, nullptr},     // tom
            {0x02, &ch[8].car, nullptr},     // cymbale
            {0x01, &ch[7].mod, nullptr},     // charleston
        };
        for (auto& k : keys) {
            const bool was = (old & 0x20) && (old & k.bit);
            const bool now = (v & 0x20) && (v & k.bit);
            if (now && !was) {
                keyOnOp(*k.op1);
                if (k.op2) keyOnOp(*k.op2);
            } else if (!now && was) {
                keyOffOp(*k.op1);
                if (k.op2) keyOffOp(*k.op2);
            }
        }
        return;
    }
    if (regLatch >= 0x10 && regLatch <= 0x18) {
        ch[regLatch - 0x10].fnum =
            (u16)((ch[regLatch - 0x10].fnum & 0x100) | v);
        return;
    }
    if (regLatch >= 0x20 && regLatch <= 0x28) {
        Channel& c = ch[regLatch - 0x20];
        c.fnum = (u16)((c.fnum & 0xFF) | ((v & 1) << 8));
        c.block = (v >> 1) & 7;
        c.sustainOn = (v & 0x20) != 0;
        keyOnOff(regLatch - 0x20, (v & 0x10) != 0);
        return;
    }
    if (regLatch >= 0x30 && regLatch <= 0x38) {
        ch[regLatch - 0x30].inst = v >> 4;
        ch[regLatch - 0x30].vol = v & 0x0F;
        return;
    }
    // 0x0E (rythme) : mémorisé dans regs[] mais non implémenté (TODO).
}

// -----------------------------------------------------------------------------
//  Enveloppe : 0 = repos, 1 = attaque, 2 = decay, 3 = sustain, 4 = release.
//  Cadence : un pas toutes les 2^(13 - taux/4) trames natives — forme du
//  matériel (attaque exponentielle vers 0 dB, decay linéaire en dB).
// -----------------------------------------------------------------------------
void Ym2413::advanceEnvelope(Op& o, const u8* patch, int opIdx, int c) {
    if (o.egPhase == 0) return;
    const Channel& chan = ch[c];
    const u8 flags = patch[opIdx];
    const u8 adsr1 = patch[4 + opIdx];           // AR (4 bits hauts) / DR (bas)
    const u8 adsr2 = patch[6 + opIdx];           // SL / RR
    const bool egt = (flags & 0x20) != 0;        // tenu (1) ou percussif (0)

    int rate;
    switch (o.egPhase) {
    case 1:  rate = adsr1 >> 4; break;           // attaque
    case 2:  rate = adsr1 & 15; break;           // decay
    case 3:  rate = egt ? 0 : (adsr2 & 15); break;  // sustain (percussif : RR)
    default:                                      // release
        rate = egt ? (adsr2 & 15) : (chan.sustainOn ? 5 : 7);
        if (egt && chan.sustainOn) rate = 5;
        break;
    }
    if (rate == 0) return;

    // Key scale of rate : l'octave accélère l'enveloppe.
    const int ksr = (flags & 0x10) ? (chan.block * 2 + (chan.fnum >> 8))
                                   : (chan.block >> 1);
    int eff = rate * 4 + ksr;
    if (eff > 63) eff = 63;

    const int shift = 13 - (eff >> 2) - 1;
    if (shift > 0 && (lfoCounter & ((1u << shift) - 1)) != 0)
        return;  // pas encore l'heure de ce pas d'enveloppe

    // Sous-cadence : la fraction de taux (eff & 3) module le pas suivant le
    // motif sur 8 temps de la famille OPL (moyennes 1 ; 1,25 ; 1,5 ; 1,75).
    static const u8 kEgInc[4][8] = {
        {1, 1, 1, 1, 1, 1, 1, 1},
        {1, 1, 1, 2, 1, 1, 1, 2},
        {1, 2, 1, 2, 1, 2, 1, 2},
        {1, 2, 2, 2, 1, 2, 2, 2},
    };
    const int inc = kEgInc[eff & 3]
                         [(lfoCounter >> (shift > 0 ? shift : 0)) & 7];

    if (o.egPhase == 1) {
        // Attaque : approche exponentielle de 0 dB (courbe du matériel,
        // plus rapide sur les derniers décibels).
        if (eff >= 60) o.egLevel = 0;
        else           o.egLevel -= ((o.egLevel >> 3) + 1) * inc;
        if (o.egLevel <= 0) {
            o.egLevel = 0;
            o.egPhase = 2;
        }
        return;
    }
    // Decay / sustain percussif / release : +0,375 dB par pas de base.
    o.egLevel += inc;
    const int sl = (adsr2 >> 4) * 8;             // sustain level (3 dB -> 8 pas)
    if (o.egPhase == 2 && o.egLevel >= sl)
        o.egPhase = 3;                            // palier de sustain atteint
    if (o.egLevel >= 127) {
        o.egLevel = 127;
        o.egPhase = 0;                            // repos
    }
}

// -----------------------------------------------------------------------------
//  Un opérateur : phase, LFO, atténuations, sinus. `fmInput` est l'apport de
//  phase du modulateur (0 pour le modulateur lui-même, qui utilise sa
//  rétroaction).
// -----------------------------------------------------------------------------
// Générateur de phase d'un opérateur (avec vibrato optionnel).
void Ym2413::advancePhase(Op& o, const u8* patch, int opIdx, int c) {
    const u8 flags = patch[opIdx];
    u32 fnum = ch[c].fnum;
    if (flags & 0x40) {
        const int pm = kPmWave[(lfoCounter >> 10) & 7];
        fnum = (u32)((int)fnum + (((int)fnum * pm) >> 9));
    }
    const u32 step = ((fnum * (u32)kMult2[flags & 0x0F]) << ch[c].block) >> 1;
    o.phase = (o.phase + step) & 0x7FFFF;        // 19 bits
}

int Ym2413::opOutput(Op& o, const u8* patch, int opIdx, int c, int fmInput) {
    const Channel& chan = ch[c];
    const u8 flags = patch[opIdx];

    advancePhase(o, patch, opIdx, c);

    // --- Atténuation totale (unités log2/256) -------------------------------
    int att = o.egLevel * 16;
    if (opIdx == 0)
        att += (patch[2] & 0x3F) * 32;           // TL du modulateur (0,75 dB)
    else
        att += chan.vol * 128;                   // volume du canal (3 dB)

    // Key scaling level : plus aigu = plus atténué, selon le réglage KSL.
    const int ksl = patch[opIdx == 0 ? 2 : 3] >> 6;
    if (ksl) {
        int base = kKslBase[chan.fnum >> 5] - 8 * (7 - chan.block);
        if (base > 0)
            att += (base << 3) >> (3 - ksl);     // 3 dB/oct, 1,5, 6 selon KSL
    }
    if (flags & 0x80) {
        // Trémolo : triangle ~3,7 Hz, profondeur ~1,2 dB (0..52 unités).
        const u32 t = (lfoCounter >> 6) % 420;
        att += (int)((t < 210 ? t : 420 - t) / 4);
    }

    // --- Sinus (bits DC/DM du patch[3] : demi-onde redressée) ---------------
    const bool rect = (patch[3] & (opIdx == 0 ? 0x08 : 0x10)) != 0;
    const int idx = (int)(o.phase >> 9) + fmInput;
    return opSine(idx, att, rect);
}

// -----------------------------------------------------------------------------
//  Percussions du mode rythme (canaux 6-8). Volumes : BD = vol du canal 6 ;
//  HH = nibble « instrument » du canal 7, SD = son volume ; TOM = nibble
//  instrument du canal 8, CYM = son volume (câblage réel des regs 0x36-38).
// -----------------------------------------------------------------------------
int Ym2413::rhythmMix() {
    int sum = 0;
    const int noiseBit = (int)(noiseLfsr & 1);

    // Grosse caisse : canal FM 2 opérateurs classique, patch rythme dédié.
    {
        Channel& c6 = ch[6];
        if (c6.mod.egPhase != 0 || c6.car.egPhase != 0) {
            const u8* p = kRhythmPatch[0];
            advanceEnvelope(c6.mod, p, 0, 6);
            advanceEnvelope(c6.car, p, 1, 6);
            const int fb = p[3] & 7;
            const int fbIn = fb ? ((c6.mod.fb1 + c6.mod.fb2) >> (9 - fb)) : 0;
            const int m = opOutput(c6.mod, p, 0, 6, fbIn);
            c6.mod.fb2 = c6.mod.fb1;
            c6.mod.fb1 = m;
            // Volume : opOutput (porteuse) lit ch[6].vol = reg 0x36 bas.
            sum += opOutput(c6.car, p, 1, 6, m >> 1);
        }
    }

    // Voix à une seule « tranche » : l'enveloppe module l'amplitude, le
    // signal vient du bruit et/ou d'un bit de phase.
    struct Voice { Op* op; int chIdx; int opIdx; int vol; int kind; };
    Voice voices[] = {
        {&ch[7].mod, 7, 0, ch[7].inst, 0},   // charleston : bruit pur
        {&ch[7].car, 7, 1, ch[7].vol, 1},    // caisse claire : phase ^ bruit
        {&ch[8].mod, 8, 0, ch[8].inst, 2},   // tom : sinus pur
        {&ch[8].car, 8, 1, ch[8].vol, 3},    // cymbale : phases 7m ^ 8c
    };
    for (Voice& v : voices) {
        Op& o = *v.op;
        if (o.egPhase == 0) continue;
        const u8* p = kRhythmPatch[v.chIdx == 7 ? 1 : 2];
        advanceEnvelope(o, p, v.opIdx, v.chIdx);
        advancePhase(o, p, v.opIdx, v.chIdx);
        const int att = o.egLevel * 16 + v.vol * 128;
        switch (v.kind) {
        case 0:   // charleston
            sum += noiseBit ? expLookup(att) : -expLookup(att);
            break;
        case 1: { // caisse claire
            const int bit = ((o.phase >> 18) ^ (u32)noiseBit) & 1;
            sum += bit ? expLookup(att) : -expLookup(att);
            break;
        }
        case 2:   // tom : sinus de l'opérateur, sans modulation
            sum += opSine((int)(o.phase >> 9), att, false);
            break;
        default: { // cymbale : carré métallique du XOR de deux phases
            const int bit = (int)(((ch[7].mod.phase >> 17) ^
                                   (o.phase >> 18)) & 1);
            sum += bit ? expLookup(att) : -expLookup(att);
            break;
        }
        }
    }

    // LFSR de bruit 23 bits (prises 0 et 14, façon famille OPL).
    noiseLfsr = (noiseLfsr >> 1) |
                (((noiseLfsr ^ (noiseLfsr >> 14)) & 1) << 22);
    return sum;
}

// Un échantillon natif : somme des porteuses des canaux mélodiques, plus
// les percussions quand le mode rythme est actif.
int Ym2413::computeSample() {
    const bool rhythm = (regs[0x0E] & 0x20) != 0;
    const int melodic = rhythm ? 6 : kNumChannels;
    int sum = 0;
    for (int c = 0; c < melodic; ++c) {
        Channel& chan = ch[c];
        if (chan.mod.egPhase == 0 && chan.car.egPhase == 0)
            continue;  // canal muet (ni tenu ni queue de relâchement)
        const u8* patch = patchFor(c);

        advanceEnvelope(chan.mod, patch, 0, c);
        advanceEnvelope(chan.car, patch, 1, c);

        // Rétroaction du modulateur : moyenne des deux dernières sorties,
        // dosée par FB (patch[3] bits 0-2, 0 = coupée).
        const int fb = patch[3] & 7;
        const int fbIn = fb ? ((chan.mod.fb1 + chan.mod.fb2) >> (9 - fb)) : 0;
        const int modOut = opOutput(chan.mod, patch, 0, c, fbIn);
        chan.mod.fb2 = chan.mod.fb1;
        chan.mod.fb1 = modOut;

        // La sortie du modulateur MODULE LA PHASE de la porteuse : c'est la
        // synthèse FM (2 opérateurs en série).
        sum += opOutput(chan.car, patch, 1, c, modOut >> 1);
    }
    if (rhythm)
        sum += rhythmMix();
    lfoCounter++;
    // 9 canaux x ±4084 : mise à l'échelle pour cohabiter avec le PSG.
    return sum >> 1;
}

void Ym2413::runCycles(int cpuCycles) {
    clockAcc += (u64)cpuCycles;
    while (clockAcc >= (u64)kClockDiv) {
        clockAcc -= (u64)kClockDiv;
        sampleSum += computeSample();
        sampleCount++;
    }
}

s16 Ym2413::takeSample() {
    if (sampleCount == 0)
        return 0;
    s32 v = sampleSum / sampleCount;
    sampleSum = 0;
    sampleCount = 0;
    if (v < -32768) v = -32768;
    if (v > 32767)  v = 32767;
    return (s16)v;
}

// -----------------------------------------------------------------------------
//  Save-state.
// -----------------------------------------------------------------------------
void Ym2413::serialize(StateIO& s) {
    s.u8v(regLatch);
    s.u8v(control);
    s.bytes(regs, sizeof(regs));
    s.bytes(userPatch, sizeof(userPatch));
    for (Channel& c : ch) {
        s.u16v(c.fnum);
        s.u8v(c.block);
        s.boolv(c.keyOn);
        s.boolv(c.sustainOn);
        s.u8v(c.inst);
        s.u8v(c.vol);
        Op* ops[2] = {&c.mod, &c.car};
        for (Op* o : ops) {
            s.u32v(o->phase);
            s.intv(o->egLevel);
            s.u8v(o->egPhase);
            s.s32v(o->fb1);
            s.s32v(o->fb2);
        }
    }
    s.u32v(lfoCounter);
    s.u32v(noiseLfsr);
    s.u64v(clockAcc);
    // Les échantillons natifs accumulés entre deux trames 44,1 kHz font
    // partie de l'état : les vider casserait le déterminisme de la reprise.
    s.s32v(sampleSum);
    s.intv(sampleCount);
}
