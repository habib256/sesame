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

// Palette TMS9918 canonique (RGB publiés par la communauté), en RGBA
// little-endian (0xAABBGGRR). L'entrée 0 (« transparent ») est rendue en
// noir quand elle sert de couleur de fond.
constexpr u32 kTmsPalette[16] = {
    0xFF000000,  //  0 transparent
    0xFF000000,  //  1 noir
    0xFF42C821,  //  2 vert moyen        (33,200,66)
    0xFF78DC5E,  //  3 vert clair        (94,220,120)
    0xFFED5554,  //  4 bleu foncé        (84,85,237)
    0xFFFC767D,  //  5 bleu clair        (125,118,252)
    0xFF4D52D4,  //  6 rouge foncé       (212,82,77)
    0xFFF5EB42,  //  7 cyan              (66,235,245)
    0xFF5455FC,  //  8 rouge moyen       (252,85,84)
    0xFF7879FF,  //  9 rouge clair       (255,121,120)
    0xFF54C1D4,  // 10 jaune foncé       (212,193,84)
    0xFF80CEE6,  // 11 jaune clair       (230,206,128)
    0xFF3BB021,  // 12 vert foncé        (33,176,59)
    0xFFBA5BC9,  // 13 magenta           (201,91,186)
    0xFFCCCCCC,  // 14 gris              (204,204,204)
    0xFFFFFFFF,  // 15 blanc
};

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
    beamX = 0;
    sprLineReady = false;
    lineScrollX = 0;
    cramLatch = 0;
    cramTouched = false;
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
        cramTouched = true;   // les modes hérités basculent sur la CRAM
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
    // Séquences par norme ET par mode (réf. SMS Power!, « VDP - V-counter
    // values ») : le compteur suit la ligne puis « saute » en arrière pour
    // finir la trame à 0xFF (sauf NTSC 240, qui boucle sans saut).
    const int h = height();
    if (region == Region::Pal) {
        if (h == 240) {   // PAL 240 : 0x00-0xFF, 0x00-0x0A, 0xD2-0xFF
            if (curLine <= 0xFF) return static_cast<u8>(curLine);
            if (curLine <= 0x10A) return static_cast<u8>(curLine - 0x100);
            return static_cast<u8>(curLine - 57);
        }
        if (h == 224) {   // PAL 224 : 0x00-0xFF, 0x00-0x02, 0xCA-0xFF
            if (curLine <= 0xFF) return static_cast<u8>(curLine);
            if (curLine <= 0x102) return static_cast<u8>(curLine - 0x100);
            return static_cast<u8>(curLine - 57);
        }
        // PAL 192 : 0x00-0xF2 puis 0xBA-0xFF.
        if (curLine <= 0xF2) return static_cast<u8>(curLine);
        return static_cast<u8>(curLine - 57);
    }
    if (h == 240)         // NTSC 240 : 0x00-0xFF puis 0x00-0x05, sans saut
        return static_cast<u8>(curLine);
    if (h == 224) {       // NTSC 224 : 0x00-0xEA puis 0xE5-0xFF
        if (curLine <= 0xEA) return static_cast<u8>(curLine);
        return static_cast<u8>(curLine - 6);
    }
    // NTSC 192 : 0x00-0xDA puis 0xD5-0xFF.
    if (curLine <= 0xDA) return static_cast<u8>(curLine);
    return static_cast<u8>(curLine - 6);
}

u8 Vdp::hCounter() const {
    // Retourne la valeur LATCHÉE (le port 0x7F ne suit pas le faisceau en
    // continu : il fige la position au dernier front TH — light gun, ou
    // écriture du contrôle E/S 0x3F qui bascule une broche TH).
    return hLatch;
}

// Latch du HCounter à partir de la position CPU dans la ligne (0..227).
// Le compteur matériel suit les 342 pixels de la ligne, incrémenté un coup
// sur deux : 171 valeurs, séquence 0x00-0x93 puis saut à 0xE9-0xFF
// (réf. SMS Power!, « VDP - H-counter values »).
void Vdp::latchHCounter(int cycleInLine) {
    // Garde : l'appelant fournit une position repliée (0..227) ; on borne
    // par sécurité pour ne jamais sortir de la séquence matérielle.
    if (cycleInLine < 0)    cycleInLine = 0;
    if (cycleInLine >= 228) cycleInLine = 227;
    const int pixel = cycleInLine * 342 / 228;   // horloge pixel = 1,5x CPU
    int hc = pixel >> 1;
    if (hc > 0x93)
        hc += 0xE9 - 0x94;                        // zone de retour ligne
    hLatch = static_cast<u8>(hc);
}

// Latch pour le Light Phaser : les jeux convertissent la valeur lue en
// position écran avec X = (HC - 0x28) * 2 — on fige donc l'inverse.
void Vdp::latchHCounterForX(int screenX) {
    hLatch = static_cast<u8>(((screenX & 0xFF) >> 1) + 0x28);
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
// Rattrapage du faisceau : rend la ligne courante jusqu'à la position CPU.
// Seul le mode 4 est rendu par tranches (les modes TMS restent en ligne
// entière — aucun jeu SG-1000 connu ne fait d'effet mid-line).
void Vdp::catchUp(int cycleInLine) {
    if (curLine >= height() || (regs[0] & 0x04) == 0)
        return;
    if (cycleInLine < 0) cycleInLine = 0;   // garde : position repliée attendue
    int target = cycleInLine * 342 / 228;   // horloge pixel = 1,5x CPU
    if (target > kWidth) target = kWidth;
    if (target <= beamX) return;
    renderSpan(curLine, beamX, target);
    beamX = target;
}

void Vdp::runLine() {
    const int h = height();   // 192, 224 ou 240 selon le mode courant

    // Drapeau VBlank : levé au moment où l'on traite la première ligne
    // après la zone visible — voir en-tête.
    if (curLine == h) status |= 0x80;

    // Rendu des lignes visibles uniquement : fin de la ligne en cours
    // (mode 4, le début a pu être rendu par catchUp), ou ligne entière
    // (modes TMS hérités).
    if (curLine < h) {
        if ((regs[0] & 0x04) == 0)
            renderLineTms(curLine);
        else if (beamX < kWidth)
            renderSpan(curLine, beamX, kWidth);
    }
    beamX = 0;
    sprLineReady = false;

    // Compteur d'interruption de ligne (registre 10) :
    //  - zone active (+1) : décrément ; passage sous zéro -> rechargement
    //    depuis reg10 et levée de lineIrq ;
    //  - au-delà : rechargement continu depuis reg10.
    if (curLine <= h) {
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
//  Sprites d'une ligne (mode 4). Évalués UNE fois par ligne : sur le vrai
//  VDP, la SAT est lue pendant le hblank précédent — les sprites ne peuvent
//  pas changer en pleine ligne. Le résultat est un index de couleur par
//  pixel (0 = pas de sprite ; bit 7 = pixel posé, pour la priorité
//  inter-sprites), résolu en RGBA au moment du composite (renderSpan) pour
//  que les écritures CRAM mid-line teintent aussi les sprites.
// -----------------------------------------------------------------------------
void Vdp::prepareSpriteLine(int y) {
    for (int x = 0; x < kWidth; ++x) sprColor[x] = 0;
    sprLineReady = true;

    const bool tall   = (height() != kHeight);
    const int satBase = ((regs[5] >> 1) & 0x3F) << 8;
    const int sprH    = (regs[1] & 0x02) ? 16 : 8;      // reg1 bit 1 : 8×16
    const int patBase = (regs[6] & 0x04) ? 0x2000 : 0;  // reg6 bit 2 : base motifs
    const int xShift  = (regs[0] & 0x08) ? -8 : 0;      // reg0 bit 3 : early clock

    int shown = 0;

    for (int i = 0; i < 64; ++i) {
        const int ya = vram[satBase + i];
        // Y = 0xD0 termine la liste — en mode 192 lignes SEULEMENT (en
        // 224/240, 0xD0 est une position valide et les 64 sprites défilent).
        if (!tall && ya == 0xD0) break;

        // Sprite visible sur les lignes [Y+1, Y+1+hauteur). La comparaison se
        // fait en 8 BITS comme sur le vrai VDP : un sprite avec Y >= 0xF1
        // déborde du haut de l'écran et montre ses lignes basses en haut
        // (ex. cadres de fenêtre d'Ultima IV).
        const int sl = (y - (ya + 1)) & 0xFF;
        if (sl >= sprH) continue;

        if (++shown > 8) {                     // limite matérielle : 8 sprites/ligne
            status |= 0x40;                    // bit 6 : sprite overflow
            if (spriteLimit)
                break;                         // les sprites en trop ne sont pas affichés
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

            if (sprColor[sx] & 0x80) {
                // Deux pixels opaques de sprites se recouvrent : collision.
                // Le sprite de numéro le plus bas (déjà posé) garde le pixel.
                status |= 0x20;
                continue;
            }
            sprColor[sx] = static_cast<u8>(0x80 | ci);
        }
    }
}

// -----------------------------------------------------------------------------
//  Rendu d'une tranche [x0, x1) de la ligne y en mode 4. Les registres et la
//  CRAM sont lus à l'appel : deux tranches d'une même ligne peuvent donc
//  différer (effets mid-line). Le scroll X, lui, est LATCHÉ en début de
//  ligne comme sur le matériel.
// -----------------------------------------------------------------------------
void Vdp::renderSpan(int y, int x0, int x1) {
    u32* dst = fb + y * kWidth;

    // Couleur de bord : entrée (reg7 & 0xF) de la palette SPRITE (CRAM 16-31).
    const u32 border = colorAt(16 + (regs[7] & 0x0F));

    // Affichage coupé (reg1 bit 6 = 0) : tranche à la couleur de bord.
    if ((regs[1] & 0x40) == 0) {
        for (int x = x0; x < x1; ++x) dst[x] = border;
        return;
    }

    // Début de ligne : latch du scroll X (verrou reg0 bit 6 : pas de
    // défilement horizontal pour les lignes 0-15) et sprites de la ligne.
    if (x0 == 0) {
        lineScrollX = regs[8];
        if ((regs[0] & 0x40) != 0 && y < 16) lineScrollX = 0;
    }
    if (!sprLineReady)
        prepareSpriteLine(y);

    // Table de noms — 192 lignes : ((reg2 >> 1) & 7) << 11, 32×28 entrées ;
    // 224/240 lignes : (((reg2 >> 2) & 3) << 12) | 0x700, 32×32 entrées et
    // le défilement vertical boucle sur 256 au lieu de 224.
    const bool tall     = (height() != kHeight);
    const int  nameBase = tall ? ((((regs[2] >> 2) & 0x03) << 12) | 0x700)
                               : (((regs[2] >> 1) & 0x07) << 11);
    const int  scrollY  = regs[9];

    for (int x = x0; x < x1; ++x) {
        // Pixel du plan de fond : décalage de scrollX vers la droite
        // (équivalent au couple fine scroll / starting column de la doc).
        const int bx = (x - lineScrollX) & 0xFF;

        // reg9 = scroll Y, ajouté à la ligne modulo 224 (28 rangées de
        // tuiles) en 192 lignes, modulo 256 (32 rangées) en 224/240.
        // Verrou reg0 bit 7 : pas de scroll vertical pour les colonnes
        // d'ÉCRAN 24-31 (x >= 192).
        int vy;
        if ((regs[0] & 0x80) != 0 && x >= 192) vy = y;
        else if (tall)                         vy = (y + scrollY) & 0xFF;
        else                                   vy = (y + scrollY) % 224;

        // Entrée de table de noms : bits 8-0 tuile, 9 flip H, 10 flip V,
        // 11 palette, 12 priorité.
        const int ea = nameBase + ((vy >> 3) * 32 + (bx >> 3)) * 2;
        const u16 entry = static_cast<u16>(vram[ea] | (vram[ea + 1] << 8));

        const int  tile  = entry & 0x01FF;
        const bool hflip = (entry & 0x0200) != 0;
        const bool vflip = (entry & 0x0400) != 0;
        const int  pal   = (entry & 0x0800) ? 16 : 0;   // 0 = fond, 1 = sprite
        const bool prio  = (entry & 0x1000) != 0;

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

        // Composite : un pixel de sprite passe devant le fond, SAUF fond
        // prioritaire et non nul. Les sprites utilisent TOUJOURS la
        // palette 1 (CRAM 16-31).
        const int sc = sprColor[x] & 0x0F;
        if (sc != 0 && !(prio && ci != 0))
            dst[x] = colorAt(16 + sc);
        else
            dst[x] = colorAt(pal + ci);

        // reg0 bit 5 : colonne de gauche (8 pixels) masquée par le bord.
        if (x < 8 && (regs[0] & 0x20) != 0)
            dst[x] = border;
    }
}

// -----------------------------------------------------------------------------
//  Modes hérités TMS9918 (M4 = 0) — utilisés par les jeux SG-1000 et
//  quelques titres SMS (F-16 Fighting Falcon, mode texte).
//
//  Couleurs : le 315-5124 pioche dans la moitié SPRITE de la CRAM
//  (entrées 16-31) — les jeux SMS qui utilisent ces modes y chargent une
//  palette. Les jeux SG-1000, eux, ignorent la CRAM (leur TMS9918 a une
//  palette FIXE) : tant qu'aucune écriture CRAM n'a eu lieu depuis le
//  reset, on sert la palette TMS canonique — choix pragmatique documenté
//  pour que les jeux SG-1000 s'affichent avec leurs couleurs d'origine.
// -----------------------------------------------------------------------------
u32 Vdp::tmsColor(int c) const {
    if (cramTouched)
        return colorAt(16 + c);
    return kTmsPalette[c];
}

void Vdp::renderLineTms(int y) {
    u32* dst = fb + y * kWidth;

    // Couleur de fond : nibble bas de reg7 (0 = « transparent » -> noir).
    const u32 backdrop = tmsColor(regs[7] & 0x0F);

    if ((regs[1] & 0x40) == 0) {   // affichage coupé
        for (int x = 0; x < kWidth; ++x) dst[x] = backdrop;
        return;
    }

    const bool textMode  = (regs[1] & 0x10) != 0;   // M1
    const bool graphic2  = (regs[0] & 0x02) != 0;   // M2 (Graphic II)
    const bool multicolor = (regs[1] & 0x08) != 0;  // M3

    const int nameBase = (regs[2] & 0x0F) << 10;

    if (textMode) {
        // ---- Mode texte : 40 colonnes de 6 pixels, pas de sprites --------
        // Couleurs fixes par reg7 : nibble haut = encre, bas = papier.
        const u32 ink   = tmsColor((regs[7] >> 4) & 0x0F);
        const int patBase = (regs[4] & 0x07) << 11;
        for (int x = 0; x < kWidth; ++x) dst[x] = backdrop;  // bordures 8 px
        for (int col = 0; col < 40; ++col) {
            const int name = vram[nameBase + (y >> 3) * 40 + col];
            const u8 bits = vram[patBase + name * 8 + (y & 7)];
            u32* p = dst + 8 + col * 6;
            for (int px = 0; px < 6; ++px)
                if (bits & (0x80 >> px)) p[px] = ink;
        }
        return;
    }

    if (multicolor) {
        // ---- Mode multicolor : blocs de 4×4 pixels -----------------------
        const int patBase = (regs[4] & 0x07) << 11;
        for (int col = 0; col < 32; ++col) {
            const int name = vram[nameBase + (y >> 3) * 32 + col];
            // Un octet = deux blocs 4×4 ; la rangée d'octets suit (y/4).
            const u8 byte = vram[patBase + name * 8 + (((y >> 2) & 1) |
                                                       (((y >> 3) & 3) << 1))];
            const int left = (byte >> 4) & 0x0F, right = byte & 0x0F;
            u32* p = dst + col * 8;
            for (int px = 0; px < 4; ++px) {
                p[px]     = left  ? tmsColor(left)  : backdrop;
                p[px + 4] = right ? tmsColor(right) : backdrop;
            }
        }
        renderTmsSprites(y, dst);
        return;
    }

    // ---- Modes Graphic I / II ------------------------------------------
    // Graphic II : trois tiers d'écran, tables motifs/couleurs de 6 Ko avec
    // masques de repli (reg4 bits 0-1, reg3 bits 0-6) ; Graphic I : un seul
    // jeu de 256 motifs, un octet de couleurs pour 8 tuiles.
    for (int col = 0; col < 32; ++col) {
        const int name = vram[nameBase + (y >> 3) * 32 + col];
        u8 bits, colors;
        if (graphic2) {
            const int index = ((y >> 6) << 8) | name;    // tiers d'écran
            const int ofs = (index << 3) | (y & 7);
            const int patBase = (regs[4] & 0x04) ? 0x2000 : 0x0000;
            const int colBase = (regs[3] & 0x80) ? 0x2000 : 0x0000;
            bits   = vram[patBase + (ofs & (((regs[4] & 0x03) << 11) | 0x7FF))];
            colors = vram[colBase + (ofs & (((regs[3] & 0x7F) << 3) | 0x007))];
        } else {
            const int patBase = (regs[4] & 0x07) << 11;
            const int colBase = regs[3] << 6;
            bits   = vram[patBase + name * 8 + (y & 7)];
            colors = vram[colBase + (name >> 3)];
        }
        const int fg = (colors >> 4) & 0x0F, bg = colors & 0x0F;
        const u32 fgc = fg ? tmsColor(fg) : backdrop;
        const u32 bgc = bg ? tmsColor(bg) : backdrop;
        u32* p = dst + col * 8;
        for (int px = 0; px < 8; ++px)
            p[px] = (bits & (0x80 >> px)) ? fgc : bgc;
    }
    renderTmsSprites(y, dst);
}

// -----------------------------------------------------------------------------
//  Sprites TMS9918 : SAT de 32 entrées de 4 octets (Y, X, motif, EC+couleur),
//  limite de 4 par ligne (le 5e lève le drapeau 5S et son numéro dans le
//  statut), coïncidence sur les BITS de motif (même transparents).
// -----------------------------------------------------------------------------
void Vdp::renderTmsSprites(int y, u32* dst) {
    const int satBase = (regs[5] & 0x7F) << 7;
    const int patBase = (regs[6] & 0x07) << 11;
    const bool size16 = (regs[1] & 0x02) != 0;
    const int  mag    = (regs[1] & 0x01) ? 1 : 0;
    const int  span   = (size16 ? 16 : 8) << mag;   // hauteur/largeur écran

    bool hit[kWidth] = {};   // bits de motif déjà posés (coïncidence)
    int shown = 0;

    for (int i = 0; i < 32; ++i) {
        const int ya = vram[satBase + i * 4];
        if (ya == 0xD0) break;               // fin de liste

        // Y : la première ligne du sprite est ya+1 ; les valeurs hautes
        // (> 0xE0) débordent du haut de l'écran (position négative).
        const int sy = (ya > 0xE0) ? (ya - 255) : (ya + 1);
        const int row = (y - sy) >> mag;
        if (row < 0 || row >= (size16 ? 16 : 8) || (y - sy) < 0) continue;

        if (++shown > 4) {
            // 5e sprite de la ligne : drapeau 5S (bit 6) + son numéro
            // (posé une seule fois, pour le premier sprite en trop).
            if (shown == 5)
                status = static_cast<u8>((status & 0xE0) | 0x40 | (i & 0x1F));
            if (spriteLimit)
                break;
        }

        const u8 flags = vram[satBase + i * 4 + 3];
        int sx = vram[satBase + i * 4 + 1];
        if (flags & 0x80) sx -= 32;          // early clock : décalé à gauche
        int pat = vram[satBase + i * 4 + 2];
        if (size16) pat &= 0xFC;             // 16×16 : 4 motifs consécutifs

        const int color = flags & 0x0F;
        const u32 rgba = tmsColor(color);

        // Motif de la rangée : 8 bits (8×8) ou 16 bits (16×16, deux
        // colonnes de 8 rangées côte à côte).
        u16 rowBits = static_cast<u16>(vram[patBase + pat * 8 + row] << 8);
        if (size16)
            rowBits |= vram[patBase + pat * 8 + row + 16];

        for (int px = 0; px < span; ++px) {
            if (!(rowBits & (0x8000 >> (px >> mag)))) continue;
            const int x = sx + px;
            if (x < 0 || x >= kWidth) continue;
            if (hit[x]) {
                status |= 0x20;              // coïncidence (collision)
                continue;
            }
            hit[x] = true;
            if (color != 0)                  // couleur 0 : invisible mais
                dst[x] = rgba;               // participe à la coïncidence
        }
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
    s.boolv(cramTouched);
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
    if (s.loading()) {
        // États pris en frontière de trame : faisceau au repos.
        beamX = 0;
        sprLineReady = false;
    }
}
