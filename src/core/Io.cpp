// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

// =============================================================================
//  Io.cpp — ports manettes (0xDC/0xDD), contrôle E/S 0x3F et console de
//  débogage SDSC (0xFC/0xFD). Le contrôle mémoire 0x3E vit dans le Bus.
//
//  Référence : SMS Power! (« I/O ports », « SDSC Debug Console »).
//  Convention : sur les ports, les bits des boutons sont ACTIFS À L'ÉTAT BAS
//  (0 = appuyé) ; l'API publique setPad() reçoit elle des bits actifs à 1
//  (enum Io::Button), l'inversion est faite ici.
// =============================================================================
#include "Io.hpp"

#include "StateIO.hpp"

#include <cstdio>

void Io::reset() {
    pad[0] = pad[1] = 0;  // manettes relâchées
    thInput[0] = thInput[1] = true;  // aucun périphérique ne tire TH
    ioControl  = 0xFF;    // toutes les broches en entrée, niveaux hauts
    sdscText.clear();     // le journal SDSC repart de zéro
    // sdscEnabled est un réglage du frontend : il survit au reset.
}

u8 Io::readPort(u8 port) {
    if (port == 0xDC) {
        // 0xDC : bits 0-5 = P1 up/down/left/right/B1/B2, bits 6-7 = P2 up/down.
        // L'ordre de l'enum Button suit celui du port : les 6 bits bas de
        // pad[0] s'inversent d'un bloc.
        u8 r = 0xFF;
        r &= (u8)~(pad[0] & 0x3F);
        if (pad[1] & Up)   r &= (u8)~0x40;
        if (pad[1] & Down) r &= (u8)~0x80;
        return r;
    }

    // 0xDD : bits 0-3 = P2 left/right/B1/B2, bit 4 = bouton Reset,
    // bit 5 = toujours 1, bits 6-7 = broches TH des ports A et B.
    u8 r = 0xFF;
    r &= (u8)~((pad[1] >> 2) & 0x0F);  // Left(b2)..B2(b5) -> bits 0-3
    // bit 4 : Reset non pressé = 1 (pas de bouton Reset exposé en v1).
    // bit 5 : câblé à 1.
    //
    // Bits 6-7 : broches TH, comportement console EXPORT. Registre 0x3F
    // (réf. SMS Power!) : bit1/bit3 = direction TH port A/B (1 = entrée),
    // bit5/bit7 = niveau de sortie TH port A/B. Sur une console export,
    // une TH configurée en SORTIE relit la valeur écrite ; en ENTRÉE elle
    // lit le niveau poussé par le périphérique (1 sans rien de branché,
    // 0 quand le Light Phaser voit le faisceau). C'est aussi le test de
    // région classique des jeux ($F5 puis $55 sur 0x3F).
    // NB : les bits 0/2 et 4/6 de 0x3F concernent les broches TR, sans
    // effet sur 0xDD.
    if (ioControl & 0x02) {
        if (!thInput[0]) r &= (u8)~0x40;   // TH A en entrée : niveau du périph.
    } else if (!(ioControl & 0x20)) {
        r &= (u8)~0x40;                    // TH A en sortie, niveau bas
    }
    if (ioControl & 0x08) {
        if (!thInput[1]) r &= (u8)~0x80;   // TH B en entrée
    } else if (!(ioControl & 0x80)) {
        r &= (u8)~0x80;                    // TH B en sortie, niveau bas
    }
    return r;
}

void Io::setThLevel(int port, bool level) {
    if (port == 0 || port == 1)
        thInput[port] = level;
}

u8 Io::readGgPort(u8 port) {
    // Ports propres à la Game Gear (réf. SMS Power!, « Game Gear hardware »).
    switch (port) {
    case 0x00:
        // bit 7 = Start (ACTIF BAS), bit 6 = NJAP (1 = export),
        // bit 5 = NNTS (0 = NTSC — la GG est NTSC-only). Bits 0-4 : non
        // câblés, lus à 0. Console émulée : export.
        return (pad[0] & Start) ? 0x40 : 0xC0;
    case 0x01:
        // Port EXT (câble Gear-to-Gear) : rien de connecté, broches hautes.
        return 0x7F;
    default:
        // 0x02-0x05 : registres du lien série — liaison absente, tout à 0.
        // 0x06 (stéréo PSG) est en écriture seule : lecture = 0xFF.
        return (port == 0x06) ? 0xFF : 0x00;
    }
}

void Io::writeIoControl(u8 v) {
    // TODO(v2) : détecter ici le front MONTANT d'une broche TH (comparer
    // l'ancien ioControl au nouveau) et verrouiller le HCounter du VDP —
    // requis pour le Light Phaser. v1 : on mémorise seulement.
    ioControl = v;
}

void Io::writeSdscControl(u8 v) {
    // 0xFC : contrôle SDSC (1 = suspendre l'émulation, 2 = effacer la
    // console, 3/4 = curseur/attributs). v1 : ignoré — rien ne le relit.
    (void)v;
}

void Io::writeSdscData(u8 v) {
    // 0xFD : donnée SDSC. On ne traite que le texte simple : tout octet
    // imprimable (>= 32) plus '\n' et '\t'. Les séquences de contrôle
    // exotiques de la convention (positionnement, couleurs, formats %) sont
    // ignorées en v1.
    if (v < 32 && v != '\n' && v != '\t')
        return;
    // Toujours accumulé (les tests headless lisent sdscLog())...
    sdscText.push_back((char)v);
    // ...et relayé immédiatement sur stdout si demandé (--sdsc).
    if (sdscEnabled) {
        std::fputc(v, stdout);
        if (v == '\n')
            std::fflush(stdout);
    }
}

void Io::setPad(int padIndex, u8 buttons) {
    if (padIndex == 0 || padIndex == 1)
        pad[padIndex] = buttons;
}

// -----------------------------------------------------------------------------
//  Save-state — contrôle E/S seulement (manettes = entrées vivantes,
//  journal SDSC = artefact de debug).
// -----------------------------------------------------------------------------
void Io::serialize(StateIO& s) {
    s.u8v(ioControl);
}
