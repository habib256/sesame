// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

// =============================================================================
//  Vdp.cpp — implémentation du Sega 315-5124 (VDP de la Master System).
//  Priorité au mode 4 (SMS). Références : documentation VDP de SMS Power! et
//  comportement du pilote MAME 315_5124.
//
//  Choix / approximations documentés (v1) :
//   * VBlank : le drapeau (status bit 7) est levé quand runLine() TRAITE la
//     ligne 192, c'est-à-dire immédiatement après la fin de la ligne visible
//     191 — ce qui correspond au début de la zone de VBlank sur le matériel.
//   * hCounter() : latch simplifié — hLatch reste à 0 (aucun événement ne le
//     met à jour en v1). Suffisant tant que les light guns / effets HCounter
//     ne sont pas émulés. TODO : latcher sur TH des ports manette.
//   * Modes hérités TMS9918 (0-3) : non implémentés — lignes remplies en noir.
// =============================================================================
#include "Vdp.hpp"

#include "StateIO.hpp"

namespace {

// Conversion d'une entrée CRAM 6 bits (--BBGGRR) vers un pixel RGBA tel que
// glTexImage2D(GL_RGBA, GL_UNSIGNED_BYTE) l'affiche : octets R,G,B,A en
// little-endian, donc 0xAABBGGRR en u32. Expansion 2 bits -> 8 bits (x * 85).
inline u32 cramToRgba(u8 c) {
    const u32 r = static_cast<u32>(c & 0x03) * 85u;
    const u32 g = static_cast<u32>((c >> 2) & 0x03) * 85u;
    const u32 b = static_cast<u32>((c >> 4) & 0x03) * 85u;
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

// Conversion d'une entrée CRAM Game Gear 12 bits (----BBBBGGGGRRRR, deux
// octets little-endian) vers RGBA. Expansion 4 bits -> 8 bits (x * 17).
inline u32 gearCramToRgba(u16 c) {
    const u32 r = static_cast<u32>(c & 0x0F) * 17u;
    const u32 g = static_cast<u32>((c >> 4) & 0x0F) * 17u;
    const u32 b = static_cast<u32>((c >> 8) & 0x0F) * 17u;
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

// Noir opaque (utilisé au reset et pour les modes non gérés).
constexpr u32 kBlack = 0xFF000000u;

} // namespace

// Entrée de palette (0-31) vers pixel RGBA, selon le modèle émulé.
u32 Vdp::colorAt(int index) const {
    if (model == Model::GameGear)
        return gearCramToRgba(static_cast<u16>(
            cram[index * 2] | (cram[index * 2 + 1] << 8)));
    return cramToRgba(cram[index]);
}

// -----------------------------------------------------------------------------
//  Réinitialisation
// -----------------------------------------------------------------------------
void Vdp::reset() {
    // v1 volontairement simple : tout à zéro — les BIOS/jeux (et notre ROM de
    // test) initialisent eux-mêmes les registres.
    for (u8& b : vram) b = 0;
    for (u8& b : cram) b = 0;
    for (u8& r : regs) r = 0;
    for (u32& p : fb) p = kBlack;

    curLine = 0;
    cramLatch = 0;
    addr = 0;
    code = 0;
    readBuffer = 0;
    ctrlLatch = false;
    ctrlFirst = 0;

    status = 0;
    lineCounter = 0xFF;
    lineIrq = false;
    frameDoneFlag = false;
    hLatch = 0;
}

// -----------------------------------------------------------------------------
//  Port contrôle 0xBF (écriture) — machine à deux octets
// -----------------------------------------------------------------------------
void Vdp::writeControl(u8 v) {
    if (!ctrlLatch) {
        // Premier octet : les 8 bits bas de l'adresse sont mis à jour
        // IMMÉDIATEMENT (comportement documenté du 315-5124).
        ctrlFirst = v;
        addr = static_cast<u16>((addr & 0x3F00) | v);
        ctrlLatch = true;
        return;
    }

    // Deuxième octet : bits 7-6 = code d'opération, bits 5-0 = adresse haute.
    ctrlLatch = false;
    code = static_cast<u8>(v >> 6);
    addr = static_cast<u16>(((v & 0x3F) << 8) | ctrlFirst);

    if (code == 0) {
        // Code 0 : lecture VRAM — préchargement du tampon puis post-incrément.
        readBuffer = vram[addr];
        addr = static_cast<u16>((addr + 1) & 0x3FFF);
    } else if (code == 2) {
        // Code 2 : écriture registre — numéro dans l'octet 2, valeur dans le 1er.
        regs[v & 0x0F] = ctrlFirst;
    }
    // Code 1 (écriture VRAM) et code 3 (écriture CRAM) : rien d'autre à faire
    // ici, la destination sera choisie lors des écritures sur 0xBE.
}

// -----------------------------------------------------------------------------
//  Port données 0xBE
// -----------------------------------------------------------------------------
u8 Vdp::readData() {
    // Toute opération sur 0xBE annule la séquence de contrôle en cours.
    ctrlLatch = false;
    const u8 r = readBuffer;               // on retourne l'ancien tampon…
    readBuffer = vram[addr];               // …puis on recharge depuis VRAM[addr]
    addr = static_cast<u16>((addr + 1) & 0x3FFF);
    return r;
}

void Vdp::writeData(u8 v) {
    ctrlLatch = false;
    if (code == 3) {
        if (model == Model::GameGear) {
            // Game Gear : CRAM de 64 octets, écrite PAR MOT. L'octet PAIR
            // est latché ; l'octet IMPAIR commite les deux d'un coup
            // (comportement matériel : une entrée 12 bits ne change jamais
            // à moitié).
            if (addr & 1) {
                cram[addr & 0x3E] = cramLatch;
                cram[addr & 0x3F] = v;
            } else {
                cramLatch = v;
            }
        } else {
            // SMS : 32 entrées d'un octet, adresse repliée sur 5 bits.
            cram[addr & 0x1F] = v;
        }
    } else {
        // Codes 0/1/2 : écriture VRAM.
        vram[addr] = v;
    }
    // L'écriture met aussi à jour le tampon de lecture (comportement matériel).
    readBuffer = v;
    addr = static_cast<u16>((addr + 1) & 0x3FFF);
}

// -----------------------------------------------------------------------------
//  Port statut 0xBF (lecture)
// -----------------------------------------------------------------------------
u8 Vdp::readStatus() {
    const u8 s = status;
    status &= 0x1F;        // efface VBlank (b7), overflow sprites (b6), collision (b5)
    ctrlLatch = false;     // annule la séquence de contrôle en cours
    lineIrq = false;       // efface aussi l'interruption de ligne en attente
    return s;
}

// -----------------------------------------------------------------------------
//  Compteurs H/V
// -----------------------------------------------------------------------------
u8 Vdp::vCounter() const {
    // Mode 192 lignes (réf. SMS Power!, « VDP - V-counter values ») :
    //  - NTSC (262 lignes) : 0x00..0xDA (lignes 0..218) puis saut à 0xD5
    //    (ligne 219) et fin à 0xFF (ligne 261) ;
    //  - PAL (313 lignes)  : 0x00..0xF2 (lignes 0..242) puis saut à 0xBA
    //    (ligne 243) et fin à 0xFF (ligne 312).
    if (region == Region::Pal) {
        if (curLine <= 0xF2) return static_cast<u8>(curLine);
        return static_cast<u8>(curLine - 57);
    }
    if (curLine <= 0xDA) return static_cast<u8>(curLine);
    return static_cast<u8>(curLine - 6);
}

u8 Vdp::hCounter() const {
    // v1 : latch simplifié — hLatch n'est jamais mis à jour, retourne 0.
    // TODO : latcher la position horizontale sur les fronts TH (light gun).
    return hLatch;
}

// -----------------------------------------------------------------------------
//  Interruptions
// -----------------------------------------------------------------------------
bool Vdp::irqPending() const {
    // /INT est un NIVEAU : tenu tant qu'un drapeau ET son enable sont actifs.
    const bool vblank = (status & 0x80) != 0 && (regs[1] & 0x20) != 0;  // IE0
    const bool line   = lineIrq && (regs[0] & 0x10) != 0;               // IE1
    return vblank || line;
}

bool Vdp::frameDone() {
    // Vrai UNE fois par trame : le drapeau est consommé à la lecture.
    const bool f = frameDoneFlag;
    frameDoneFlag = false;
    return f;
}

// -----------------------------------------------------------------------------
//  Avancement d'une ligne (appelé par Machine toutes les 228 cycles CPU)
// -----------------------------------------------------------------------------
void Vdp::runLine() {
    // Drapeau VBlank : levé au moment où l'on traite la ligne 192, c.-à-d.
    // juste après la fin de la dernière ligne visible (191) — voir en-tête.
    if (curLine == kHeight) status |= 0x80;

    // Rendu des lignes visibles uniquement.
    if (curLine < kHeight) renderLine(curLine);

    // Compteur d'interruption de ligne (registre 10) :
    //  - lignes 0..192 incluses : décrément ; passage sous zéro -> rechargement
    //    depuis reg10 et levée de lineIrq ;
    //  - lignes > 192 : rechargement continu depuis reg10.
    if (curLine <= 192) {
        if (lineCounter == 0) {
            lineCounter = regs[10];
            lineIrq = true;
        } else {
            --lineCounter;
        }
    } else {
        lineCounter = regs[10];
    }

    // Ligne suivante ; bouclage de la trame (262 lignes NTSC, 313 PAL).
    if (++curLine >= linesPerFrame()) {
        curLine = 0;
        frameDoneFlag = true;
    }
}

// -----------------------------------------------------------------------------
//  Rendu d'une ligne en mode 4
// -----------------------------------------------------------------------------
void Vdp::renderLine(int y) {
    u32* dst = fb + y * kWidth;

    // Mode 4 requis (bit M4 = reg0 bit 2). Modes hérités TMS9918 (0-3) :
    // TODO — pour l'instant on remplit la ligne en noir sans planter.
    if ((regs[0] & 0x04) == 0) {
        for (int x = 0; x < kWidth; ++x) dst[x] = kBlack;
        return;
    }

    // Couleur de bord : entrée (reg7 & 0xF) de la palette SPRITE (CRAM 16-31).
    const u32 border = colorAt(16 + (regs[7] & 0x0F));

    // Affichage coupé (reg1 bit 6 = 0) : ligne entière à la couleur de bord.
    if ((regs[1] & 0x40) == 0) {
        for (int x = 0; x < kWidth; ++x) dst[x] = border;
        return;
    }

    // ---------------------------------------------------------------- Fond ---
    // Table de noms : ((reg2 >> 1) & 7) << 11 — 32×28 entrées de 2 octets.
    const int nameBase = ((regs[2] >> 1) & 0x07) << 11;

    // reg8 = scroll X (fond décalé vers la DROITE de scrollX pixels).
    // Verrou reg0 bit 6 : pas de scroll horizontal pour les lignes 0-15.
    int scrollX = regs[8];
    if ((regs[0] & 0x40) != 0 && y < 16) scrollX = 0;
    const int scrollY = regs[9];

    u8   bgIndex[kWidth];   // index de couleur 0-15 du pixel de fond
    bool bgPrio[kWidth];    // bit priorité de la tuile sous ce pixel

    for (int x = 0; x < kWidth; ++x) {
        // Pixel du plan de fond : décalage de scrollX vers la droite
        // (équivalent au couple fine scroll / starting column de la doc).
        const int bx = (x - scrollX) & 0xFF;

        // reg9 = scroll Y, ajouté à la ligne modulo 224 (28 lignes de tuiles).
        // Verrou reg0 bit 7 : pas de scroll vertical pour les colonnes
        // d'ÉCRAN 24-31 (x >= 192).
        int vy;
        if ((regs[0] & 0x80) != 0 && x >= 192) vy = y;
        else                                   vy = (y + scrollY) % 224;

        // Entrée de table de noms : bits 8-0 tuile, 9 flip H, 10 flip V,
        // 11 palette, 12 priorité.
        const int ea = nameBase + ((vy >> 3) * 32 + (bx >> 3)) * 2;
        const u16 entry = static_cast<u16>(vram[ea] | (vram[ea + 1] << 8));

        const int  tile  = entry & 0x01FF;
        const bool hflip = (entry & 0x0200) != 0;
        const bool vflip = (entry & 0x0400) != 0;
        const int  pal   = (entry & 0x0800) ? 16 : 0;   // 0 = fond, 1 = sprite
        bgPrio[x]        = (entry & 0x1000) != 0;

        // Tuiles 8×8, 4 bitplanes entrelacés par ligne, 32 octets/tuile.
        int fy = vy & 7;
        if (vflip) fy = 7 - fy;
        int bit = 7 - (bx & 7);          // bit 7 = pixel le plus à gauche
        if (hflip) bit = bx & 7;

        const int pa = tile * 32 + fy * 4;
        const int ci = ((vram[pa]     >> bit) & 1)
                     | (((vram[pa + 1] >> bit) & 1) << 1)
                     | (((vram[pa + 2] >> bit) & 1) << 2)
                     | (((vram[pa + 3] >> bit) & 1) << 3);

        bgIndex[x] = static_cast<u8>(ci);
        dst[x] = colorAt(pal + ci);
    }

    // ------------------------------------------------------------- Sprites ---
    // Table d'attributs : ((reg5 >> 1) & 0x3F) << 8. Octets 0-63 = Y ;
    // octets 128+ = paires (X, index de tuile).
    const int satBase = ((regs[5] >> 1) & 0x3F) << 8;
    const int sprH    = (regs[1] & 0x02) ? 16 : 8;      // reg1 bit 1 : 8×16
    const int patBase = (regs[6] & 0x04) ? 0x2000 : 0;  // reg6 bit 2 : base motifs
    const int xShift  = (regs[0] & 0x08) ? -8 : 0;      // reg0 bit 3 : early clock

    bool sprOpaque[kWidth] = {};   // pixel de sprite déjà posé (collision + priorité inter-sprites)
    int shown = 0;

    for (int i = 0; i < 64; ++i) {
        const int ya = vram[satBase + i];
        if (ya == 0xD0) break;                 // Y = 0xD0 termine la liste (mode 192 lignes)

        // Sprite visible sur les lignes [Y+1, Y+1+hauteur). La comparaison se
        // fait en 8 BITS comme sur le vrai VDP : un sprite avec Y >= 0xF1
        // déborde du haut de l'écran et montre ses lignes basses en haut
        // (ex. cadres de fenêtre d'Ultima IV).
        const int sl = (y - (ya + 1)) & 0xFF;
        if (sl >= sprH) continue;

        if (++shown > 8) {                     // limite matérielle : 8 sprites/ligne
            status |= 0x40;                    // bit 6 : sprite overflow
            break;                             // les sprites en trop ne sont pas affichés
        }

        const int sx0 = vram[satBase + 128 + i * 2] + xShift;
        int tile = vram[satBase + 128 + i * 2 + 1];
        if (sprH == 16) tile &= 0xFE;          // 8×16 : index pair, tuile+1 = moitié basse

        // sl (0..15 en 8×16) couvre naturellement les deux tuiles (32 octets chacune).
        const int pa = patBase + tile * 32 + sl * 4;
        const u8 b0 = vram[pa], b1 = vram[pa + 1], b2 = vram[pa + 2], b3 = vram[pa + 3];

        for (int px = 0; px < 8; ++px) {
            const int bit = 7 - px;
            const int ci = ((b0 >> bit) & 1)
                         | (((b1 >> bit) & 1) << 1)
                         | (((b2 >> bit) & 1) << 2)
                         | (((b3 >> bit) & 1) << 3);
            if (ci == 0) continue;             // couleur 0 = transparente

            const int sx = sx0 + px;
            if (sx < 0 || sx >= kWidth) continue;

            if (sprOpaque[sx]) {
                // Deux pixels opaques de sprites se recouvrent : collision.
                // Le sprite de numéro le plus bas (déjà posé) garde le pixel.
                status |= 0x20;
                continue;
            }
            sprOpaque[sx] = true;

            // Un pixel de fond prioritaire et non nul passe DEVANT le sprite
            // (mais la collision ci-dessus est détectée quand même).
            if (bgPrio[sx] && bgIndex[sx] != 0) continue;

            // Les sprites utilisent TOUJOURS la palette 1 (CRAM 16-31).
            dst[sx] = colorAt(16 + ci);
        }
    }

    // reg0 bit 5 : masquage de la colonne de gauche (8 pixels) avec la
    // couleur de bord — appliqué en dernier, par-dessus fond et sprites.
    if ((regs[0] & 0x20) != 0) {
        for (int x = 0; x < 8; ++x) dst[x] = border;
    }
}

// -----------------------------------------------------------------------------
//  Save-state — VRAM/CRAM/registres et verrous internes. Le framebuffer est
//  reconstruit ligne à ligne par la trame suivante (états pris en frontière
//  de trame par les frontends).
// -----------------------------------------------------------------------------
void Vdp::serialize(StateIO& s) {
    s.bytes(vram, sizeof(vram));
    s.bytes(cram, sizeof(cram));
    s.bytes(regs, sizeof(regs));
    s.u8v(cramLatch);
    s.intv(curLine);
    s.u16v(addr);
    s.u8v(code);
    s.u8v(readBuffer);
    s.boolv(ctrlLatch);
    s.u8v(ctrlFirst);
    s.u8v(status);
    s.u8v(lineCounter);
    s.boolv(lineIrq);
    s.boolv(frameDoneFlag);
    s.u8v(hLatch);
}
