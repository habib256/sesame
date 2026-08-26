// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#pragma once
// =============================================================================
//  Z80 — cœur CPU Zilog Z80 (3,58 MHz sur SMS NTSC).
//  Jeu d'instructions complet (préfixes CB/DD/FD/ED, opcodes non documentés,
//  drapeaux X/Y), décompte de cycles standard, IM 0/1/2, registre R, délai EI.
//  Sur SMS : /INT vient du VDP (niveau), /NMI du bouton Pause (front).
// =============================================================================
#include "Types.hpp"

class Bus;

class Z80 {
public:
    explicit Z80(Bus& bus) : bus(bus) { reset(); }

    void reset();

    // Exécute UNE instruction (ou accepte une interruption en attente) et
    // retourne le nombre de cycles machine consommés. En HALT sans interruption,
    // consomme 4 cycles (NOP interne).
    int step();

    // Ligne /INT, active par NIVEAU : tenue haute tant que le VDP la demande.
    void setIrqLine(bool level) { irqLine = level; }
    // /NMI, déclenchée par FRONT (bouton Pause).
    void triggerNmi() { nmiPending = true; }

    // Registres exposés pour la trace et le débogueur.
    struct Regs {
        u16 af, bc, de, hl;      // banque principale
        u16 af2, bc2, de2, hl2;  // banque alternative (EX/EXX)
        u16 ix, iy, sp, pc;
        u8  i, r;                // vecteur d'interruption, rafraîchissement
        u8  im;                  // mode d'interruption 0/1/2
        bool iff1, iff2;
        bool halted;
    };
    Regs regs{};

    u64 cycles = 0;  // cycles cumulés depuis reset (pour la trace)

private:
    Bus& bus;
    bool irqLine    = false;
    bool nmiPending = false;
    // EI n'autorise les interruptions qu'APRÈS l'instruction suivante.
    bool eiDelay    = false;

    // MEMPTR (alias WZ) : registre interne non documenté du Z80. Sert surtout
    // aux bits X/Y de BIT n,(HL) ; mis à jour par la plupart des accès mémoire.
    u16 wz = 0;

    // Le détail de l'implémentation (dispatch, helpers de drapeaux…) vit dans
    // Z80.cpp ; seuls les points d'entrée ci-dessus font contrat.
    int executeOne();
    int acceptIrq();
    int acceptNmi();
};

// Désassembleur minimal pour la trace : décode l'instruction à `addr` (lue via
// une fonction de lecture fournie), écrit la mnémonique dans `out` (taille
// `outSize`), retourne la longueur de l'instruction en octets.
int z80Disassemble(u16 addr, u8 (*read)(void* ctx, u16), void* ctx,
                   char* out, int outSize);
