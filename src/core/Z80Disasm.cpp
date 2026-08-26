// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

// =============================================================================
//  Z80Disasm.cpp — désassembleur minimal pour la trace.
//
//  Contrat : z80Disassemble() écrit une mnémonique lisible dans `out` et
//  retourne la LONGUEUR EXACTE de l'instruction en octets — c'est elle qui
//  fait avancer la trace, elle doit donc être correcte même pour les opcodes
//  non documentés. Le texte, lui, peut rester approximatif sur ces derniers.
//
//  Conventions d'affichage : immédiats en hexadécimal préfixé '$'
//  ("LD BC,$1234"), déplacements indexés en décimal signé ("BIT 3,(IX+5)"),
//  cibles relatives (JR/DJNZ) résolues en adresse absolue.
//
//  Cas particuliers de longueur :
//   - DD ou FD suivi de DD/FD/ED : le préfixe est inopérant -> "DB $DD",
//     longueur 1 (le prochain appel redécode à partir de l'octet suivant,
//     exactement comme le CPU).
//   - ED invalide ("NONI") : longueur 2, affiché "DB $ED,$xx".
//   - DD CB d op : toujours 4 octets.
// =============================================================================
#include "Z80.hpp"
#include <cstdio>

namespace {

// --- Tables de noms ----------------------------------------------------------
const char* const R8[8]  = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};
// Variantes avec H/L substitués par les moitiés d'index (préfixes DD/FD).
const char* const R8X[3][8] = {
    {"B", "C", "D", "E", "H",   "L",   "(HL)", "A"},
    {"B", "C", "D", "E", "IXH", "IXL", "(IX)", "A"},
    {"B", "C", "D", "E", "IYH", "IYL", "(IY)", "A"},
};
const char* const RPN[3][4] = {
    {"BC", "DE", "HL", "SP"},
    {"BC", "DE", "IX", "SP"},
    {"BC", "DE", "IY", "SP"},
};
const char* const RP2N[3][4] = {
    {"BC", "DE", "HL", "AF"},
    {"BC", "DE", "IX", "AF"},
    {"BC", "DE", "IY", "AF"},
};
const char* const CCN[8]  = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};
const char* const ALUN[8] = {"ADD A,", "ADC A,", "SUB ", "SBC A,",
                             "AND ",   "XOR ",   "OR ",  "CP "};
const char* const ROTN[8] = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL"};
const char* const IXN[3]  = {"HL", "IX", "IY"};

// --- Petit curseur de lecture ------------------------------------------------
struct Dis {
    u16 addr;
    u8 (*read)(void*, u16);
    void* ctx;
    int len = 0;    // octets consommés -> valeur de retour
    char buf[64];   // mnémonique en construction

    u8 next() { return read(ctx, u16(addr + len++)); }
    u16 next16() { u8 l = next(); u8 h = next(); return u16(l | (h << 8)); }
};

// Formatage de l'opérande mémoire indexé : "(IX+5)" / "(IY-12)".
void fmtMem(char* dst, int dstSize, int prefix, int disp) {
    snprintf(dst, size_t(dstSize), "(%s%+d)", IXN[prefix], disp);
}

// =============================================================================
//  Préfixe CB (sans DD/FD)
// =============================================================================
void disCB(Dis& d) {
    u8 op = d.next();
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    switch (x) {
        case 0: snprintf(d.buf, sizeof d.buf, "%s %s", ROTN[y], R8[z]); break;
        case 1: snprintf(d.buf, sizeof d.buf, "BIT %d,%s", y, R8[z]); break;
        case 2: snprintf(d.buf, sizeof d.buf, "RES %d,%s", y, R8[z]); break;
        default: snprintf(d.buf, sizeof d.buf, "SET %d,%s", y, R8[z]); break;
    }
}

// =============================================================================
//  DD CB d op / FD CB d op — 4 octets, opérande toujours (IX+d).
//  Les variantes non documentées recopient le résultat dans un registre :
//  affichées "RLC (IX+5),B", "SET 0,(IY-2),C"…
// =============================================================================
void disIndexedCB(Dis& d, int prefix) {
    int disp = s8(d.next());
    u8 op = d.next();
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    char mem[16];
    fmtMem(mem, sizeof mem, prefix, disp);
    switch (x) {
        case 0:
            if (z == 6) snprintf(d.buf, sizeof d.buf, "%s %s", ROTN[y], mem);
            else snprintf(d.buf, sizeof d.buf, "%s %s,%s", ROTN[y], mem, R8[z]);
            break;
        case 1:  // BIT : pas de recopie, quel que soit z
            snprintf(d.buf, sizeof d.buf, "BIT %d,%s", y, mem);
            break;
        case 2:
            if (z == 6) snprintf(d.buf, sizeof d.buf, "RES %d,%s", y, mem);
            else snprintf(d.buf, sizeof d.buf, "RES %d,%s,%s", y, mem, R8[z]);
            break;
        default:
            if (z == 6) snprintf(d.buf, sizeof d.buf, "SET %d,%s", y, mem);
            else snprintf(d.buf, sizeof d.buf, "SET %d,%s,%s", y, mem, R8[z]);
            break;
    }
}

// =============================================================================
//  Préfixe ED
// =============================================================================
void disED(Dis& d) {
    u8 op = d.next();
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = (op >> 4) & 3;
    if (x == 1) {
        switch (z) {
            case 0:
                if (y == 6) snprintf(d.buf, sizeof d.buf, "IN (C)");
                else snprintf(d.buf, sizeof d.buf, "IN %s,(C)", R8[y]);
                return;
            case 1:
                if (y == 6) snprintf(d.buf, sizeof d.buf, "OUT (C),0");
                else snprintf(d.buf, sizeof d.buf, "OUT (C),%s", R8[y]);
                return;
            case 2:
                snprintf(d.buf, sizeof d.buf, "%s HL,%s",
                         (op & 0x08) ? "ADC" : "SBC", RPN[0][p]);
                return;
            case 3: {
                u16 nn = d.next16();
                if (op & 0x08)
                    snprintf(d.buf, sizeof d.buf, "LD %s,($%04X)",
                             RPN[0][p], unsigned(nn));
                else
                    snprintf(d.buf, sizeof d.buf, "LD ($%04X),%s",
                             unsigned(nn), RPN[0][p]);
                return;
            }
            case 4: snprintf(d.buf, sizeof d.buf, "NEG"); return;
            case 5:
                snprintf(d.buf, sizeof d.buf, "%s", (op == 0x4D) ? "RETI" : "RETN");
                return;
            case 6: {
                static const int imTab[8] = {0, 0, 1, 2, 0, 0, 1, 2};
                snprintf(d.buf, sizeof d.buf, "IM %d", imTab[y]);
                return;
            }
            default:  // z == 7
                switch (y) {
                    case 0: snprintf(d.buf, sizeof d.buf, "LD I,A"); return;
                    case 1: snprintf(d.buf, sizeof d.buf, "LD R,A"); return;
                    case 2: snprintf(d.buf, sizeof d.buf, "LD A,I"); return;
                    case 3: snprintf(d.buf, sizeof d.buf, "LD A,R"); return;
                    case 4: snprintf(d.buf, sizeof d.buf, "RRD"); return;
                    case 5: snprintf(d.buf, sizeof d.buf, "RLD"); return;
                    default:
                        snprintf(d.buf, sizeof d.buf, "DB $ED,$%02X", unsigned(op));
                        return;
                }
        }
    }
    if (x == 2 && z <= 3 && y >= 4) {  // instructions de bloc
        static const char* const T[4][4] = {
            {"LDI",  "CPI",  "INI",  "OUTI"},
            {"LDD",  "CPD",  "IND",  "OUTD"},
            {"LDIR", "CPIR", "INIR", "OTIR"},
            {"LDDR", "CPDR", "INDR", "OTDR"},
        };
        snprintf(d.buf, sizeof d.buf, "%s", T[y - 4][z]);
        return;
    }
    // "NONI" : NOP à deux octets sur le vrai silicium.
    snprintf(d.buf, sizeof d.buf, "DB $ED,$%02X", unsigned(op));
}

// =============================================================================
//  Bloc principal (avec ou sans préfixe DD/FD)
// =============================================================================
void disMain(Dis& d, u8 op, int prefix) {
    char mem[16];
    // Lit le déplacement (si préfixe) et formate l'opérande mémoire.
    // ATTENTION : consomme un octet quand prefix != 0 -> à appeler dans
    // l'ordre réel des octets de l'instruction.
    auto memOp = [&]() -> const char* {
        if (prefix == 0) return "(HL)";
        int disp = s8(d.next());
        fmtMem(mem, sizeof mem, prefix, disp);
        return mem;
    };
    const char* hlN = IXN[prefix];
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = (op >> 4) & 3;

    // ---- 0x40-0x7F : LD r,r' / HALT -----------------------------------------
    if (x == 1) {
        if (op == 0x76) { snprintf(d.buf, sizeof d.buf, "HALT"); return; }
        if (y == 6) {  // LD (HL/IX+d),r — la source garde les vrais H/L
            const char* m = memOp();
            snprintf(d.buf, sizeof d.buf, "LD %s,%s", m, R8[z]);
            return;
        }
        if (z == 6) {
            const char* m = memOp();
            snprintf(d.buf, sizeof d.buf, "LD %s,%s", R8[y], m);
            return;
        }
        snprintf(d.buf, sizeof d.buf, "LD %s,%s", R8X[prefix][y], R8X[prefix][z]);
        return;
    }
    // ---- 0x80-0xBF : ALU A,r ------------------------------------------------
    if (x == 2) {
        if (z == 6) {
            const char* m = memOp();
            snprintf(d.buf, sizeof d.buf, "%s%s", ALUN[y], m);
        } else {
            snprintf(d.buf, sizeof d.buf, "%s%s", ALUN[y], R8X[prefix][z]);
        }
        return;
    }
    // ---- 0x00-0x3F ----------------------------------------------------------
    if (x == 0) {
        switch (z) {
            case 0:
                switch (y) {
                    case 0: snprintf(d.buf, sizeof d.buf, "NOP"); return;
                    case 1: snprintf(d.buf, sizeof d.buf, "EX AF,AF'"); return;
                    case 2: {
                        int disp = s8(d.next());
                        snprintf(d.buf, sizeof d.buf, "DJNZ $%04X",
                                 unsigned(u16(d.addr + d.len + disp)));
                        return;
                    }
                    case 3: {
                        int disp = s8(d.next());
                        snprintf(d.buf, sizeof d.buf, "JR $%04X",
                                 unsigned(u16(d.addr + d.len + disp)));
                        return;
                    }
                    default: {  // JR cc,d
                        int disp = s8(d.next());
                        snprintf(d.buf, sizeof d.buf, "JR %s,$%04X", CCN[y - 4],
                                 unsigned(u16(d.addr + d.len + disp)));
                        return;
                    }
                }
            case 1:
                if (op & 0x08)
                    snprintf(d.buf, sizeof d.buf, "ADD %s,%s", hlN, RPN[prefix][p]);
                else
                    snprintf(d.buf, sizeof d.buf, "LD %s,$%04X",
                             RPN[prefix][p], unsigned(d.next16()));
                return;
            case 2:
                switch (y) {
                    case 0: snprintf(d.buf, sizeof d.buf, "LD (BC),A"); return;
                    case 1: snprintf(d.buf, sizeof d.buf, "LD A,(BC)"); return;
                    case 2: snprintf(d.buf, sizeof d.buf, "LD (DE),A"); return;
                    case 3: snprintf(d.buf, sizeof d.buf, "LD A,(DE)"); return;
                    case 4:
                        snprintf(d.buf, sizeof d.buf, "LD ($%04X),%s",
                                 unsigned(d.next16()), hlN);
                        return;
                    case 5:
                        snprintf(d.buf, sizeof d.buf, "LD %s,($%04X)",
                                 hlN, unsigned(d.next16()));
                        return;
                    case 6:
                        snprintf(d.buf, sizeof d.buf, "LD ($%04X),A",
                                 unsigned(d.next16()));
                        return;
                    default:
                        snprintf(d.buf, sizeof d.buf, "LD A,($%04X)",
                                 unsigned(d.next16()));
                        return;
                }
            case 3:
                snprintf(d.buf, sizeof d.buf, "%s %s",
                         (op & 0x08) ? "DEC" : "INC", RPN[prefix][p]);
                return;
            case 4:
            case 5: {
                const char* nm = (z == 4) ? "INC" : "DEC";
                if (y == 6) {
                    const char* m = memOp();
                    snprintf(d.buf, sizeof d.buf, "%s %s", nm, m);
                } else {
                    snprintf(d.buf, sizeof d.buf, "%s %s", nm, R8X[prefix][y]);
                }
                return;
            }
            case 6:
                if (y == 6) {  // LD (HL/IX+d),n — d AVANT n, comme le CPU
                    const char* m = memOp();
                    snprintf(d.buf, sizeof d.buf, "LD %s,$%02X",
                             m, unsigned(d.next()));
                } else {
                    snprintf(d.buf, sizeof d.buf, "LD %s,$%02X",
                             R8X[prefix][y], unsigned(d.next()));
                }
                return;
            default: {  // z == 7 : rotations sur A et drapeaux
                static const char* const T[8] = {"RLCA", "RRCA", "RLA", "RRA",
                                                 "DAA",  "CPL",  "SCF", "CCF"};
                snprintf(d.buf, sizeof d.buf, "%s", T[y]);
                return;
            }
        }
    }
    // ---- 0xC0-0xFF ----------------------------------------------------------
    switch (z) {
        case 0:
            snprintf(d.buf, sizeof d.buf, "RET %s", CCN[y]);
            return;
        case 1:
            if (!(op & 0x08)) {
                snprintf(d.buf, sizeof d.buf, "POP %s", RP2N[prefix][p]);
            } else {
                switch (p) {
                    case 0: snprintf(d.buf, sizeof d.buf, "RET"); break;
                    case 1: snprintf(d.buf, sizeof d.buf, "EXX"); break;
                    case 2: snprintf(d.buf, sizeof d.buf, "JP (%s)", hlN); break;
                    default: snprintf(d.buf, sizeof d.buf, "LD SP,%s", hlN); break;
                }
            }
            return;
        case 2:
            snprintf(d.buf, sizeof d.buf, "JP %s,$%04X",
                     CCN[y], unsigned(d.next16()));
            return;
        case 3:
            switch (y) {
                case 0:
                    snprintf(d.buf, sizeof d.buf, "JP $%04X", unsigned(d.next16()));
                    return;
                case 2:
                    snprintf(d.buf, sizeof d.buf, "OUT ($%02X),A",
                             unsigned(d.next()));
                    return;
                case 3:
                    snprintf(d.buf, sizeof d.buf, "IN A,($%02X)",
                             unsigned(d.next()));
                    return;
                case 4: snprintf(d.buf, sizeof d.buf, "EX (SP),%s", hlN); return;
                case 5: snprintf(d.buf, sizeof d.buf, "EX DE,HL"); return;
                case 6: snprintf(d.buf, sizeof d.buf, "DI"); return;
                default: snprintf(d.buf, sizeof d.buf, "EI"); return;
                // y == 1 (préfixe CB) n'arrive jamais ici.
            }
        case 4:
            snprintf(d.buf, sizeof d.buf, "CALL %s,$%04X",
                     CCN[y], unsigned(d.next16()));
            return;
        case 5:
            if (!(op & 0x08))
                snprintf(d.buf, sizeof d.buf, "PUSH %s", RP2N[prefix][p]);
            else  // p == 0 seulement : CALL nn (p=1,2,3 sont les préfixes)
                snprintf(d.buf, sizeof d.buf, "CALL $%04X", unsigned(d.next16()));
            return;
        case 6:
            snprintf(d.buf, sizeof d.buf, "%s$%02X", ALUN[y], unsigned(d.next()));
            return;
        default:  // z == 7 : RST
            snprintf(d.buf, sizeof d.buf, "RST $%02X", unsigned(y * 8));
            return;
    }
}

// Copie tronquée vers le buffer de l'appelant.
void copyOut(char* out, int outSize, const char* s) {
    if (out == nullptr || outSize <= 0) return;
    snprintf(out, size_t(outSize), "%s", s);
}

}  // namespace

// =============================================================================
//  Point d'entrée public
// =============================================================================
int z80Disassemble(u16 addr, u8 (*read)(void* ctx, u16), void* ctx,
                   char* out, int outSize) {
    Dis d{addr, read, ctx, 0, {0}};
    u8 op = d.next();
    int prefix = 0;  // 0 = HL, 1 = IX, 2 = IY

    if (op == 0xDD || op == 0xFD) {
        // Un DD/FD suivi d'un autre préfixe (DD/FD/ED) est inopérant : on le
        // décode seul, longueur 1, comme le CPU qui redémarre au suivant.
        u8 nx = read(ctx, u16(addr + 1));
        if (nx == 0xDD || nx == 0xFD || nx == 0xED) {
            snprintf(d.buf, sizeof d.buf, "DB $%02X", unsigned(op));
            copyOut(out, outSize, d.buf);
            return 1;
        }
        prefix = (op == 0xDD) ? 1 : 2;
        op = d.next();
    }

    if (op == 0xCB) {
        if (prefix) disIndexedCB(d, prefix);
        else disCB(d);
    } else if (op == 0xED) {
        disED(d);
    } else {
        disMain(d, op, prefix);
    }

    copyOut(out, outSize, d.buf);
    return d.len;
}
