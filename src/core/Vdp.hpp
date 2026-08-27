// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#pragma once
// =============================================================================
//  Vdp — Sega 315-5124 (VDP de la Master System), priorité au mode 4.
//  Ports CPU : 0xBE (données), 0xBF (contrôle/statut), 0x7E (VCounter),
//  0x7F (HCounter). Rendu par LIGNE dans un framebuffer RGBA 256×192.
//  NTSC : 262 lignes/trame (~59,92 Hz) ; PAL : 313 lignes (~49,70 Hz) —
//  sélection par setRegion(), la séquence du VCounter suit.
// =============================================================================
#include "Types.hpp"

class StateIO;

class Vdp {
public:
    static constexpr int kWidth  = 256;
    static constexpr int kHeight = 192;       // mode 4 standard
    static constexpr int kMaxHeight = 240;    // mode 240 lignes (SMS2, PAL)
    static constexpr int kLinesNtsc = 262;
    static constexpr int kLinesPal  = 313;

    // Hauteur visible du mode courant : 224 (M4+M2+M1) ou 240 (M4+M2+M3)
    // sur SMS2, 192 sinon. Les frontends la relisent à chaque trame.
    int height() const {
        if ((regs[0] & 0x06) == 0x06) {          // M4 et M2
            const bool m1 = (regs[1] & 0x10) != 0;
            const bool m3 = (regs[1] & 0x08) != 0;
            if (m1 && !m3) return 224;
            if (m3 && !m1) return 240;
        }
        return kHeight;
    }

    // Fenêtre visible Game Gear : 160×144 centrée dans la sortie du VDP.
    // Le VDP rend la trame complète ; les frontends recadrent (le décalage
    // vertical dépend du mode : (height() - 144) / 2, soit 24 en 192 lignes).
    static constexpr int kGgWidth   = 160;
    static constexpr int kGgHeight  = 144;
    static constexpr int kGgOffsetX = (kWidth - kGgWidth) / 2;  // 48

    void reset();

    // Save-state : VRAM/CRAM/registres et verrous internes. Le framebuffer
    // n'est PAS sérialisé : les états sont pris en frontière de trame et la
    // trame suivante le reconstruit ligne à ligne.
    void serialize(StateIO& s);

    // Norme vidéo : réglage matériel du frontend, survit au reset.
    void setRegion(Region r) { region = r; }
    // Modèle de console : réglage matériel (Machine::setModel), survit au
    // reset. En Game Gear, la CRAM passe en entrées 12 bits (2 octets).
    void setModel(Model m) { model = m; }
    int  linesPerFrame() const {
        return (region == Region::Pal) ? kLinesPal : kLinesNtsc;
    }

    // --- Interface ports CPU -------------------------------------------------
    u8   readData();            // port 0xBE en lecture (tampon + post-incrément)
    u8   readStatus();          // port 0xBF en lecture (efface VBlank/collision + latch)
    void writeData(u8 v);       // port 0xBE en écriture (VRAM/CRAM selon code)
    void writeControl(u8 v);    // port 0xBF en écriture (adresse/registre, 2 octets)
    u8   vCounter() const;      // port 0x7E (avec le saut propre à la région)
    u8   hCounter() const;      // port 0x7F (valeur latchée)

    // --- Interface Machine ---------------------------------------------------
    // Rend la ligne courante (si visible), met à jour compteurs de ligne,
    // drapeaux d'interruption et VBlank, puis passe à la ligne suivante.
    void runLine();
    // Niveau de la ligne /INT vers le CPU (VBlank et/ou interruption de ligne,
    // selon les bits d'activation des registres 0 et 1).
    bool irqPending() const;
    // Vrai UNE fois par trame complète ; consomme le drapeau.
    bool frameDone();

    int line() const { return curLine; }
    // RGBA, kWidth * height() lignes valides (tampon alloué pour kMaxHeight).
    const u32* frameBuffer() const { return fb; }

    // --- État exposé (débogueur / save-state) --------------------------------
    u8 vram[0x4000]{};
    // CRAM : 32 octets utilisés en SMS (--BBGGRR), 64 en Game Gear
    // (32 entrées de 2 octets, ----BBBBGGGGRRRR little-endian).
    u8 cram[64]{};
    u8 regs[16]{};

private:
    u32 fb[kWidth * kMaxHeight]{};
    Region region = Region::Ntsc;
    Model  model  = Model::Sms;
    u8 cramLatch = 0;   // GG : octet pair latché, commité par l'octet impair
    int curLine = 0;

    // Décodage adresse/contrôle
    u16 addr = 0;         // pointeur d'adresse courant (14 bits)
    u8  code = 0;         // code d'opération (2 bits hauts du mot de contrôle)
    u8  readBuffer = 0;   // tampon de lecture VRAM
    bool ctrlLatch = false;  // attend le 2e octet du mot de contrôle
    u8  ctrlFirst = 0;

    // Statut & interruptions
    u8  status = 0;         // bit7 VBlank, bit6 overflow sprites, bit5 collision
    u8  lineCounter = 0;    // compteur d'interruption de ligne (registre 10)
    bool lineIrq = false;
    bool frameDoneFlag = false;
    u8  hLatch = 0;

    u32  colorAt(int index) const;  // entrée CRAM 0-31 -> RGBA selon modèle
    void renderLine(int y);
};
