// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

// =============================================================================
//  Z80.cpp — implémentation complète du cœur Zilog Z80.
//
//  Organisation :
//   - helpers de bas niveau (accès moitiés de paires, parité, drapeaux S/Z/X/Y)
//   - struct Ops : contexte de travail construit à chaque step() ; contient
//     TOUTE la logique (fetch, ALU, dispatch des préfixes CB/ED/DD/FD)
//   - les points d'entrée de la classe Z80 (reset/step/executeOne/acceptIrq/
//     acceptNmi) tout en bas.
//
//  Choix d'exactitude :
//   - MEMPTR (WZ) est émulé pour les accès courants : c'est lui qui fournit
//     les bits X/Y de BIT n,(HL) et BIT n,(IX+d).
//   - Les bits X (0x08) et Y (0x20) du registre F sont calculés partout selon
//     les tables de référence (dont LDI/CPI/INI/OUTI et leurs répétitions).
//     Deux approximations documentées : SCF/CCF prennent X/Y directement de A
//     (sans l'historique du registre Q), et LDIR/CPIR en cours de répétition
//     ne recopient pas PC-haut dans X/Y.
//   - Décompte de cycles : tables standard (NOP=4, CALL=17/10, LDIR=21/16…).
//   - Non documenté couvert : SLL, IXH/IXL/IYH/IYL, IN (C), OUT (C),0
//     (Z80 NMOS -> écrit 0), variantes DD CB / FD CB avec recopie du résultat
//     dans un registre, ED "NONI" traités comme des NOP de 8 cycles.
// =============================================================================
#include "Z80.hpp"
#include "Bus.hpp"

namespace {

// --- Masques du registre F ---------------------------------------------------
constexpr u8 FC = 0x01;  // Carry
constexpr u8 FN = 0x02;  // Add/Subtract
constexpr u8 FP = 0x04;  // Parity/Overflow
constexpr u8 FX = 0x08;  // bit 3 non documenté (copie du bit 3 du résultat)
constexpr u8 FH = 0x10;  // Half-carry
constexpr u8 FY = 0x20;  // bit 5 non documenté (copie du bit 5 du résultat)
constexpr u8 FZ = 0x40;  // Zero
constexpr u8 FS = 0x80;  // Sign

// --- Accès aux moitiés d'une paire 16 bits ----------------------------------
inline u8 hi8(u16 v) { return u8(v >> 8); }
inline u8 lo8(u16 v) { return u8(v & 0xFF); }
inline void setHi(u16& p, u8 v) { p = u16((p & 0x00FF) | (u16(v) << 8)); }
inline void setLo(u16& p, u8 v) { p = u16((p & 0xFF00) | v); }

// Parité PAIRE -> FP levé (convention Z80).
inline u8 parity(u8 v) {
    v = u8(v ^ (v >> 4));
    v = u8(v ^ (v >> 2));
    v = u8(v ^ (v >> 1));
    return (v & 1) ? 0 : FP;
}

// S, X, Y copiés du résultat ; Z si nul.
inline u8 szxy(u8 v) { return u8((v & (FS | FX | FY)) | (v ? 0 : FZ)); }
// Idem plus la parité (AND/OR/XOR, rotations CB, IN r,(C)…).
inline u8 szxyp(u8 v) { return u8(szxy(v) | parity(v)); }

// =============================================================================
//  Ops — contexte d'exécution. Construit sur la pile à chaque appel ; ne
//  contient que des références vers l'état persistant du Z80.
// =============================================================================
struct Ops {
    Z80::Regs& r;
    Bus& bus;
    u16& wz;        // MEMPTR
    bool& eiDelay;  // levé par EI, consommé par step()

    // ---- Accès A et F -------------------------------------------------------
    u8 A() const { return hi8(r.af); }
    u8 F() const { return lo8(r.af); }
    void setA(u8 v) { setHi(r.af, v); }
    void setF(u8 v) { setLo(r.af, v); }

    // R : 7 bits incrémentés à chaque fetch d'opcode, bit 7 préservé.
    void bumpR() { r.r = u8((r.r & 0x80) | ((r.r + 1) & 0x7F)); }

    // ---- Accès mémoire ------------------------------------------------------
    u8 rd(u16 a) { return bus.read8(a); }
    void wr(u16 a, u8 v) { bus.write8(a, v); }
    u16 rd16(u16 a) { u8 l = rd(a); u8 h = rd(u16(a + 1)); return u16(l | (h << 8)); }
    void wr16(u16 a, u16 v) { wr(a, lo8(v)); wr(u16(a + 1), hi8(v)); }

    u8 fetch8() { return rd(r.pc++); }                       // octet immédiat
    u16 fetch16() { u16 v = rd16(r.pc); r.pc = u16(r.pc + 2); return v; }
    u8 fetchOp() { bumpR(); return fetch8(); }               // opcode : R++

    void push16(u16 v) { r.sp = u16(r.sp - 2); wr16(r.sp, v); }
    u16 pop16() { u16 v = rd16(r.sp); r.sp = u16(r.sp + 2); return v; }

    // ---- Sélection de registres ---------------------------------------------
    // prefix : 0 = HL, 1 = IX, 2 = IY (pour les préfixes DD/FD).
    u16& xy(int prefix) { return prefix == 1 ? r.ix : r.iy; }
    u16& hlp(int prefix) { return prefix == 0 ? r.hl : xy(prefix); }

    // Paires pour LD rr,nn / INC rr / ADD HL,rr… (3 = SP).
    u16& rp(int i, int prefix) {
        switch (i & 3) {
            case 0: return r.bc;
            case 1: return r.de;
            case 2: return hlp(prefix);
            default: return r.sp;
        }
    }
    // Paires pour PUSH/POP (3 = AF).
    u16& rp2(int i, int prefix) {
        switch (i & 3) {
            case 0: return r.bc;
            case 1: return r.de;
            case 2: return hlp(prefix);
            default: return r.af;
        }
    }

    // Registre 8 bits par index (0..7 = B,C,D,E,H,L,-,A). L'index 6, (HL),
    // est géré par l'appelant. Avec un préfixe, H/L deviennent IXH/IXL.
    u8 getR(int i, int prefix) {
        switch (i & 7) {
            case 0: return hi8(r.bc);
            case 1: return lo8(r.bc);
            case 2: return hi8(r.de);
            case 3: return lo8(r.de);
            case 4: return hi8(hlp(prefix));
            case 5: return lo8(hlp(prefix));
            case 7: return A();
            default: return 0;  // jamais atteint (index 6 exclu)
        }
    }
    void setR(int i, u8 v, int prefix) {
        switch (i & 7) {
            case 0: setHi(r.bc, v); break;
            case 1: setLo(r.bc, v); break;
            case 2: setHi(r.de, v); break;
            case 3: setLo(r.de, v); break;
            case 4: setHi(hlp(prefix), v); break;
            case 5: setLo(hlp(prefix), v); break;
            case 7: setA(v); break;
            default: break;  // jamais atteint
        }
    }

    // Adresse effective de l'opérande mémoire : (HL) sans préfixe, sinon
    // (IX+d) / (IY+d) — le déplacement d est lu ici, et MEMPTR est mis à jour.
    u16 effAddr(int prefix) {
        if (prefix == 0) return r.hl;
        s8 d = s8(fetch8());
        wz = u16(xy(prefix) + d);
        return wz;
    }

    // Test des 8 conditions (NZ, Z, NC, C, PO, PE, P, M).
    bool cc(int i) {
        u8 f = F();
        switch (i & 7) {
            case 0: return !(f & FZ);
            case 1: return (f & FZ) != 0;
            case 2: return !(f & FC);
            case 3: return (f & FC) != 0;
            case 4: return !(f & FP);
            case 5: return (f & FP) != 0;
            case 6: return !(f & FS);
            default: return (f & FS) != 0;
        }
    }

    // =========================================================================
    //  ALU 8 bits — chaque helper positionne F au complet.
    // =========================================================================
    void add8(u8 v, u8 cin) {
        u8 a = A();
        int sum = a + v + cin;
        u8 res = u8(sum);
        u8 f = u8(szxy(res)
                  | (((a ^ v ^ res) & 0x10) ? FH : 0)                 // retenue bit 3->4
                  | ((~(a ^ v) & (a ^ res) & 0x80) ? FP : 0)          // débordement signé
                  | (sum > 0xFF ? FC : 0));
        setA(res);
        setF(f);
    }
    void sub8(u8 v, u8 cin) {
        u8 a = A();
        int dif = a - v - cin;
        u8 res = u8(dif);
        u8 f = u8(szxy(res) | FN
                  | (((a ^ v ^ res) & 0x10) ? FH : 0)                 // emprunt bit 4
                  | (((a ^ v) & (a ^ res) & 0x80) ? FP : 0)
                  | (dif < 0 ? FC : 0));
        setA(res);
        setF(f);
    }
    // CP : comme SUB mais A inchangé, et X/Y copiés de l'OPÉRANDE (non doc.).
    void cp8(u8 v) {
        u8 a = A();
        int dif = a - v;
        u8 res = u8(dif);
        u8 f = u8((res & FS) | (res ? 0 : FZ) | (v & (FX | FY)) | FN
                  | (((a ^ v ^ res) & 0x10) ? FH : 0)
                  | (((a ^ v) & (a ^ res) & 0x80) ? FP : 0)
                  | (dif < 0 ? FC : 0));
        setF(f);
    }
    void and8(u8 v) { u8 res = u8(A() & v); setA(res); setF(u8(szxyp(res) | FH)); }
    void xor8(u8 v) { u8 res = u8(A() ^ v); setA(res); setF(szxyp(res)); }
    void or8(u8 v)  { u8 res = u8(A() | v); setA(res); setF(szxyp(res)); }

    // INC/DEC 8 bits : C préservé, P = débordement (0x7F->0x80 / 0x80->0x7F).
    u8 inc8(u8 v) {
        u8 res = u8(v + 1);
        setF(u8((F() & FC) | szxy(res)
                | (((res & 0x0F) == 0) ? FH : 0)
                | (v == 0x7F ? FP : 0)));
        return res;
    }
    u8 dec8(u8 v) {
        u8 res = u8(v - 1);
        setF(u8((F() & FC) | szxy(res) | FN
                | (((v & 0x0F) == 0) ? FH : 0)
                | (v == 0x80 ? FP : 0)));
        return res;
    }

    // Dispatch des 8 opérations ALU du bloc 0x80-0xBF (et ALU A,n).
    void aluDo(int aluop, u8 v) {
        switch (aluop & 7) {
            case 0: add8(v, 0); break;                 // ADD
            case 1: add8(v, u8(F() & FC)); break;      // ADC
            case 2: sub8(v, 0); break;                 // SUB
            case 3: sub8(v, u8(F() & FC)); break;      // SBC
            case 4: and8(v); break;                    // AND
            case 5: xor8(v); break;                    // XOR
            case 6: or8(v); break;                     // OR
            default: cp8(v); break;                    // CP
        }
    }

    // =========================================================================
    //  ALU 16 bits
    // =========================================================================
    // ADD HL,rr (ou ADD IX,rr) : S/Z/P préservés, H = retenue bit 11,
    // X/Y copiés de l'octet HAUT du résultat, MEMPTR = dst+1.
    void addPair(u16& dst, u16 v) {
        u32 sum = u32(dst) + v;
        wz = u16(dst + 1);
        u8 f = u8((F() & (FS | FZ | FP))
                  | (((dst ^ v ^ u16(sum)) & 0x1000) ? FH : 0)
                  | (u8(sum >> 8) & (FX | FY))
                  | (sum > 0xFFFF ? FC : 0));
        dst = u16(sum);
        setF(f);
    }
    // ADC HL,rr : drapeaux COMPLETS (S/Z sur 16 bits, débordement signé).
    void adc16(u16 v) {
        u8 cin = u8(F() & FC);
        u32 sum = u32(r.hl) + v + cin;
        u16 res = u16(sum);
        wz = u16(r.hl + 1);
        u8 f = u8((hi8(res) & (FS | FX | FY)) | (res == 0 ? FZ : 0)
                  | (((r.hl ^ v ^ res) & 0x1000) ? FH : 0)
                  | ((~(r.hl ^ v) & (r.hl ^ res) & 0x8000) ? FP : 0)
                  | (sum > 0xFFFF ? FC : 0));
        r.hl = res;
        setF(f);
    }
    void sbc16(u16 v) {
        u8 cin = u8(F() & FC);
        s32 dif = s32(r.hl) - v - cin;
        u16 res = u16(dif);
        wz = u16(r.hl + 1);
        u8 f = u8((hi8(res) & (FS | FX | FY)) | (res == 0 ? FZ : 0) | FN
                  | (((r.hl ^ v ^ res) & 0x1000) ? FH : 0)
                  | (((r.hl ^ v) & (r.hl ^ res) & 0x8000) ? FP : 0)
                  | (dif < 0 ? FC : 0));
        r.hl = res;
        setF(f);
    }

    // =========================================================================
    //  Rotations sur A (0x07..0x1F) — S/Z/P préservés, X/Y copiés de A.
    // =========================================================================
    void opRlca() {
        u8 a = A();
        u8 c = u8(a >> 7);
        a = u8((a << 1) | c);
        setA(a);
        setF(u8((F() & (FS | FZ | FP)) | (a & (FX | FY)) | (c ? FC : 0)));
    }
    void opRrca() {
        u8 a = A();
        u8 c = u8(a & 1);
        a = u8((a >> 1) | (c << 7));
        setA(a);
        setF(u8((F() & (FS | FZ | FP)) | (a & (FX | FY)) | (c ? FC : 0)));
    }
    void opRla() {
        u8 a = A();
        u8 c = u8(a >> 7);
        a = u8((a << 1) | (F() & FC));
        setA(a);
        setF(u8((F() & (FS | FZ | FP)) | (a & (FX | FY)) | (c ? FC : 0)));
    }
    void opRra() {
        u8 a = A();
        u8 c = u8(a & 1);
        a = u8((a >> 1) | ((F() & FC) << 7));
        setA(a);
        setF(u8((F() & (FS | FZ | FP)) | (a & (FX | FY)) | (c ? FC : 0)));
    }

    // DAA — ajustement décimal, algorithme de référence (couvre ADD et SUB).
    void opDaa() {
        u8 a = A(), f = F();
        u8 adj = 0;
        u8 c = u8(f & FC);
        if ((f & FH) || (a & 0x0F) > 9) adj |= 0x06;
        if (c || a > 0x99) { adj |= 0x60; c = FC; }
        u8 res = (f & FN) ? u8(a - adj) : u8(a + adj);
        u8 h;
        if (f & FN) h = ((f & FH) && (a & 0x0F) < 6) ? FH : 0;
        else        h = ((a & 0x0F) > 9) ? FH : 0;
        setA(res);
        setF(u8(szxyp(res) | (f & FN) | h | c));
    }
    void opCpl() {
        setA(u8(~A()));
        setF(u8((F() & (FS | FZ | FP | FC)) | FH | FN | (A() & (FX | FY))));
    }
    // SCF/CCF : approximation classique — X/Y copiés de A (le vrai silicium
    // mêle F précédent et A via le registre Q ; sans conséquence sur SMS).
    void opScf() {
        setF(u8((F() & (FS | FZ | FP)) | FC | (A() & (FX | FY))));
    }
    void opCcf() {
        u8 c = u8(F() & FC);
        setF(u8((F() & (FS | FZ | FP)) | (c ? FH : 0) | (c ? 0 : FC)
                | (A() & (FX | FY))));
    }

    // =========================================================================
    //  Rotations/décalages CB — retourne le résultat, F complet (dont SLL).
    // =========================================================================
    u8 rot(int op, u8 v) {
        u8 c = 0, res = 0;
        switch (op & 7) {
            case 0: res = u8((v << 1) | (v >> 7)); c = u8(v >> 7); break;   // RLC
            case 1: res = u8((v >> 1) | (v << 7)); c = u8(v & 1);  break;   // RRC
            case 2: res = u8((v << 1) | (F() & FC)); c = u8(v >> 7); break; // RL
            case 3: res = u8((v >> 1) | ((F() & FC) << 7)); c = u8(v & 1); break; // RR
            case 4: res = u8(v << 1); c = u8(v >> 7); break;                // SLA
            case 5: res = u8((v >> 1) | (v & 0x80)); c = u8(v & 1); break;  // SRA
            case 6: res = u8((v << 1) | 1); c = u8(v >> 7); break;          // SLL (non doc.)
            default: res = u8(v >> 1); c = u8(v & 1); break;                // SRL
        }
        setF(u8(szxyp(res) | (c ? FC : 0)));
        return res;
    }

    // BIT n : Z/P = bit nul, S = bit 7 testé et non nul, H=1, C préservé.
    // X/Y viennent de xySrc : le registre pour BIT n,r ; l'octet HAUT de
    // MEMPTR pour BIT n,(HL) et BIT n,(IX+d) (comportement réel).
    void bitTest(int n, u8 v, u8 xySrc) {
        u8 m = u8(v & (1u << (n & 7)));
        setF(u8((F() & FC) | FH
                | (m ? 0 : (FZ | FP))
                | (m & FS)
                | (xySrc & (FX | FY))));
    }

    // =========================================================================
    //  RRD / RLD — rotation de quartets entre A et (HL).
    // =========================================================================
    void opRrd() {
        u8 m = rd(r.hl), a = A();
        wr(r.hl, u8((a << 4) | (m >> 4)));
        a = u8((a & 0xF0) | (m & 0x0F));
        setA(a);
        setF(u8((F() & FC) | szxyp(a)));
        wz = u16(r.hl + 1);
    }
    void opRld() {
        u8 m = rd(r.hl), a = A();
        wr(r.hl, u8((m << 4) | (a & 0x0F)));
        a = u8((a & 0xF0) | (m >> 4));
        setA(a);
        setF(u8((F() & FC) | szxyp(a)));
        wz = u16(r.hl + 1);
    }

    // =========================================================================
    //  Instructions de bloc (ED, x=2) — drapeaux non documentés inclus.
    // =========================================================================
    // LDI/LDD/LDIR/LDDR. n = A + octet transféré : Y = bit 1 de n, X = bit 3.
    int ldBlock(int dir, bool rep) {
        u8 v = rd(r.hl);
        wr(r.de, v);
        r.hl = u16(r.hl + dir);
        r.de = u16(r.de + dir);
        r.bc = u16(r.bc - 1);
        u8 n = u8(A() + v);
        setF(u8((F() & (FS | FZ | FC))
                | (r.bc ? FP : 0)
                | (n & FX)
                | ((n & 0x02) ? FY : 0)));
        if (rep && r.bc) {
            r.pc = u16(r.pc - 2);  // ré-exécute l'instruction
            wz = u16(r.pc + 1);
            return 21;
        }
        return 16;
    }
    // CPI/CPD/CPIR/CPDR. n = (A - (HL)) - H : Y = bit 1, X = bit 3.
    int cpBlock(int dir, bool rep) {
        u8 m = rd(r.hl);
        u8 a = A();
        u8 res = u8(a - m);
        bool hb = ((a ^ m ^ res) & 0x10) != 0;  // emprunt de quartet
        r.hl = u16(r.hl + dir);
        r.bc = u16(r.bc - 1);
        u8 n = u8(res - (hb ? 1 : 0));
        setF(u8((F() & FC) | FN
                | (res & FS) | (res ? 0 : FZ)
                | (hb ? FH : 0)
                | (r.bc ? FP : 0)
                | (n & FX)
                | ((n & 0x02) ? FY : 0)));
        wz = u16(wz + dir);
        if (rep && r.bc && res) {
            r.pc = u16(r.pc - 2);
            wz = u16(r.pc + 1);
            return 21;
        }
        return 16;
    }
    // INI/IND/INIR/INDR. Drapeaux étranges de référence :
    //   N = bit 7 de la valeur lue ; k = valeur + ((C±1) & 0xFF) ;
    //   H = C = (k > 255) ; P = parité((k & 7) ^ B') ; S/Z/X/Y depuis B'.
    int inBlock(int dir, bool rep) {
        wz = u16(r.bc + dir);                   // MEMPTR = BC±1 (avant décrément de B)
        u8 v = bus.ioRead(lo8(r.bc));
        wr(r.hl, v);
        r.hl = u16(r.hl + dir);
        u8 b = u8(hi8(r.bc) - 1);
        int k = v + ((lo8(r.bc) + dir) & 0xFF);
        setHi(r.bc, b);
        setF(u8(szxy(b)
                | ((v & 0x80) ? FN : 0)
                | (k > 0xFF ? (FH | FC) : 0)
                | parity(u8((k & 7) ^ b))));
        if (rep && b) { r.pc = u16(r.pc - 2); return 21; }
        return 16;
    }
    // OUTI/OUTD/OTIR/OTDR. B est décrémenté AVANT la sortie ;
    //   k = valeur + L (après inc/dec de HL) ; mêmes drapeaux que ci-dessus.
    int outBlock(int dir, bool rep) {
        u8 v = rd(r.hl);
        u8 b = u8(hi8(r.bc) - 1);
        setHi(r.bc, b);
        bus.ioWrite(lo8(r.bc), v);
        r.hl = u16(r.hl + dir);
        wz = u16(r.bc + dir);                   // MEMPTR = BC±1 (B déjà décrémenté)
        int k = v + lo8(r.hl);
        setF(u8(szxy(b)
                | ((v & 0x80) ? FN : 0)
                | (k > 0xFF ? (FH | FC) : 0)
                | parity(u8((k & 7) ^ b))));
        if (rep && b) { r.pc = u16(r.pc - 2); return 21; }
        return 16;
    }
    int execBlock(int y, int z) {
        int dir = (y & 1) ? -1 : +1;  // y=4,6 : incrémente ; y=5,7 : décrémente
        bool rep = y >= 6;            // y=6,7 : variantes répétitives
        switch (z & 3) {
            case 0: return ldBlock(dir, rep);
            case 1: return cpBlock(dir, rep);
            case 2: return inBlock(dir, rep);
            default: return outBlock(dir, rep);
        }
    }

    // =========================================================================
    //  Préfixe CB — rotations, BIT, RES, SET.
    // =========================================================================
    int execCB() {
        u8 op = fetchOp();  // second fetch d'opcode : R incrémenté à nouveau
        int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
        if (z == 6) {
            // Opérande mémoire (HL).
            u16 a = r.hl;
            u8 v = rd(a);
            switch (x) {
                case 0: wr(a, rot(y, v)); return 15;
                case 1: bitTest(y, v, hi8(wz)); return 12;  // X/Y depuis MEMPTR
                case 2: wr(a, u8(v & ~(1u << y))); return 15;
                default: wr(a, u8(v | (1u << y))); return 15;
            }
        }
        u8 v = getR(z, 0);
        switch (x) {
            case 0: setR(z, rot(y, v), 0); return 8;
            case 1: bitTest(y, v, v); return 8;             // X/Y depuis le registre
            case 2: setR(z, u8(v & ~(1u << y)), 0); return 8;
            default: setR(z, u8(v | (1u << y)), 0); return 8;
        }
    }

    // =========================================================================
    //  Préfixes DD CB / FD CB — l'opérande est TOUJOURS (IX+d)/(IY+d).
    //  Séquence : DD CB d op — d et op sont lus SANS incrément de R.
    //  Non documenté : si z != 6, le résultat est recopié dans le registre z.
    //  Cycles retournés SANS les 4 du préfixe (ajoutés par run()) :
    //  BIT = 16 (20 au total), autres = 19 (23 au total).
    // =========================================================================
    int execIndexedCB(int prefix) {
        s8 d = s8(fetch8());
        u8 op = fetch8();
        u16 a = u16(xy(prefix) + d);
        wz = a;
        int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
        u8 v = rd(a);
        if (x == 1) {                     // BIT n,(IX+d) — quel que soit z
            bitTest(y, v, hi8(wz));
            return 16;
        }
        u8 res;
        switch (x) {
            case 0: res = rot(y, v); break;
            case 2: res = u8(v & ~(1u << y)); break;
            default: res = u8(v | (1u << y)); break;
        }
        wr(a, res);
        if (z != 6) setR(z, res, 0);      // recopie non documentée
        return 19;
    }

    // =========================================================================
    //  Préfixe ED
    // =========================================================================
    int execED() {
        u8 op = fetchOp();
        int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = (op >> 4) & 3;
        if (x == 1) {
            switch (z) {
                case 0: {  // IN r,(C) — y=6 : IN (C), drapeaux seulement
                    wz = u16(r.bc + 1);
                    u8 v = bus.ioRead(lo8(r.bc));
                    setF(u8((F() & FC) | szxyp(v)));
                    if (y != 6) setR(y, v, 0);
                    return 12;
                }
                case 1:    // OUT (C),r — y=6 : OUT (C),0 (Z80 NMOS -> 0)
                    wz = u16(r.bc + 1);
                    bus.ioWrite(lo8(r.bc), y == 6 ? u8(0) : getR(y, 0));
                    return 12;
                case 2: {  // SBC HL,rr / ADC HL,rr
                    u16 v = rp(p, 0);
                    if (op & 0x08) adc16(v); else sbc16(v);
                    return 15;
                }
                case 3: {  // LD (nn),rr / LD rr,(nn)
                    u16 nn = fetch16();
                    if (op & 0x08) rp(p, 0) = rd16(nn);
                    else           wr16(nn, rp(p, 0));
                    wz = u16(nn + 1);
                    return 20;
                }
                case 4: {  // NEG (toutes les copies non documentées)
                    u8 a = A();
                    setA(0);
                    sub8(a, 0);
                    return 8;
                }
                case 5:    // RETN / RETI (même effet : IFF1 <- IFF2)
                    r.iff1 = r.iff2;
                    r.pc = pop16();
                    wz = r.pc;
                    return 14;
                case 6: {  // IM 0/1/2 (avec les copies non documentées)
                    static const u8 imTab[8] = {0, 0, 1, 2, 0, 0, 1, 2};
                    r.im = imTab[y];
                    return 8;
                }
                default:   // z == 7
                    switch (y) {
                        case 0: r.i = A(); return 9;   // LD I,A
                        case 1: r.r = A(); return 9;   // LD R,A
                        case 2:                        // LD A,I : P = IFF2
                            setA(r.i);
                            setF(u8((F() & FC) | szxy(r.i) | (r.iff2 ? FP : 0)));
                            return 9;
                        case 3:                        // LD A,R : P = IFF2
                            setA(r.r);
                            setF(u8((F() & FC) | szxy(r.r) | (r.iff2 ? FP : 0)));
                            return 9;
                        case 4: opRrd(); return 18;
                        case 5: opRld(); return 18;
                        default: return 8;             // ED 77/7F : NOP
                    }
            }
        }
        if (x == 2 && z <= 3 && y >= 4) return execBlock(y, z);
        return 8;  // "NONI" : les autres opcodes ED sont des NOP de 8 cycles
    }

    // =========================================================================
    //  Bloc principal (opcodes sans préfixe, ou derrière DD/FD).
    //  Les cycles retournés N'INCLUENT PAS les 4 cycles d'un préfixe DD/FD
    //  (ajoutés par run()) — d'où les valeurs 15/19 pour les formes (IX+d).
    // =========================================================================
    int execMain(u8 op, int prefix) {
        // ---- 0x40-0x7F : LD r,r' (et HALT en 0x76) --------------------------
        if (op >= 0x40 && op <= 0x7F) {
            if (op == 0x76) {           // HALT : boucle sur des NOP internes
                r.halted = true;
                return 4;
            }
            int d = (op >> 3) & 7, s = op & 7;
            // Quand un opérande est mémoire, l'AUTRE utilise les vrais H/L
            // (jamais IXH/IXL) — comportement réel du préfixe.
            if (d == 6) {               // LD (HL/IX+d),r
                u16 a = effAddr(prefix);
                wr(a, getR(s, 0));
                return prefix ? 15 : 7;
            }
            if (s == 6) {               // LD r,(HL/IX+d)
                u16 a = effAddr(prefix);
                setR(d, rd(a), 0);
                return prefix ? 15 : 7;
            }
            setR(d, getR(s, prefix), prefix);
            return 4;
        }
        // ---- 0x80-0xBF : ALU A,r --------------------------------------------
        if (op >= 0x80 && op <= 0xBF) {
            int s = op & 7;
            if (s == 6) {
                u8 v = rd(effAddr(prefix));
                aluDo((op >> 3) & 7, v);
                return prefix ? 15 : 7;
            }
            aluDo((op >> 3) & 7, getR(s, prefix));
            return 4;
        }
        // ---- 0x00-0x3F et 0xC0-0xFF -----------------------------------------
        switch (op) {
            // --- Divers x=0, z=0 ---
            case 0x00: return 4;                                   // NOP
            case 0x08: {                                           // EX AF,AF'
                u16 t = r.af; r.af = r.af2; r.af2 = t;
                return 4;
            }
            case 0x10: {                                           // DJNZ d
                s8 d = s8(fetch8());
                u8 b = u8(hi8(r.bc) - 1);
                setHi(r.bc, b);
                if (b) { r.pc = u16(r.pc + d); wz = r.pc; return 13; }
                return 8;
            }
            case 0x18: {                                           // JR d
                s8 d = s8(fetch8());
                r.pc = u16(r.pc + d);
                wz = r.pc;
                return 12;
            }
            case 0x20: case 0x28: case 0x30: case 0x38: {          // JR cc,d
                s8 d = s8(fetch8());
                if (cc((op >> 3) & 3)) {
                    r.pc = u16(r.pc + d);
                    wz = r.pc;
                    return 12;
                }
                return 7;
            }

            // --- LD rr,nn / ADD HL,rr ---
            case 0x01: case 0x11: case 0x21: case 0x31:            // LD rr,nn
                rp((op >> 4) & 3, prefix) = fetch16();
                return 10;
            case 0x09: case 0x19: case 0x29: case 0x39: {          // ADD HL,rr
                u16 v = rp((op >> 4) & 3, prefix);  // lu AVANT (cas ADD IX,IX)
                addPair(hlp(prefix), v);
                return 11;
            }

            // --- LD indirects (A, HL) — avec mise à jour de MEMPTR ---
            case 0x02:                                             // LD (BC),A
                wr(r.bc, A());
                wz = u16((u16(A()) << 8) | ((r.bc + 1) & 0xFF));
                return 7;
            case 0x12:                                             // LD (DE),A
                wr(r.de, A());
                wz = u16((u16(A()) << 8) | ((r.de + 1) & 0xFF));
                return 7;
            case 0x22: {                                           // LD (nn),HL
                u16 nn = fetch16();
                wr16(nn, hlp(prefix));
                wz = u16(nn + 1);
                return 16;
            }
            case 0x32: {                                           // LD (nn),A
                u16 nn = fetch16();
                wr(nn, A());
                wz = u16((u16(A()) << 8) | ((nn + 1) & 0xFF));
                return 13;
            }
            case 0x0A: setA(rd(r.bc)); wz = u16(r.bc + 1); return 7;   // LD A,(BC)
            case 0x1A: setA(rd(r.de)); wz = u16(r.de + 1); return 7;   // LD A,(DE)
            case 0x2A: {                                           // LD HL,(nn)
                u16 nn = fetch16();
                hlp(prefix) = rd16(nn);
                wz = u16(nn + 1);
                return 16;
            }
            case 0x3A: {                                           // LD A,(nn)
                u16 nn = fetch16();
                setA(rd(nn));
                wz = u16(nn + 1);
                return 13;
            }

            // --- INC/DEC rr ---
            case 0x03: case 0x13: case 0x23: case 0x33:            // INC rr
                rp((op >> 4) & 3, prefix)++;
                return 6;
            case 0x0B: case 0x1B: case 0x2B: case 0x3B:            // DEC rr
                rp((op >> 4) & 3, prefix)--;
                return 6;

            // --- INC/DEC r ---
            case 0x04: case 0x0C: case 0x14: case 0x1C:
            case 0x24: case 0x2C: case 0x3C: {                     // INC r
                int i = (op >> 3) & 7;
                setR(i, inc8(getR(i, prefix)), prefix);
                return 4;
            }
            case 0x34: {                                           // INC (HL/IX+d)
                u16 a = effAddr(prefix);
                wr(a, inc8(rd(a)));
                return prefix ? 19 : 11;
            }
            case 0x05: case 0x0D: case 0x15: case 0x1D:
            case 0x25: case 0x2D: case 0x3D: {                     // DEC r
                int i = (op >> 3) & 7;
                setR(i, dec8(getR(i, prefix)), prefix);
                return 4;
            }
            case 0x35: {                                           // DEC (HL/IX+d)
                u16 a = effAddr(prefix);
                wr(a, dec8(rd(a)));
                return prefix ? 19 : 11;
            }

            // --- LD r,n ---
            case 0x06: case 0x0E: case 0x16: case 0x1E:
            case 0x26: case 0x2E: case 0x3E:                       // LD r,n
                setR((op >> 3) & 7, fetch8(), prefix);
                return 7;
            case 0x36: {                                           // LD (HL/IX+d),n
                u16 a = effAddr(prefix);  // d lu AVANT n (ordre réel DD 36 d n)
                wr(a, fetch8());
                return prefix ? 15 : 10;
            }

            // --- Rotations sur A et divers drapeaux ---
            case 0x07: opRlca(); return 4;
            case 0x0F: opRrca(); return 4;
            case 0x17: opRla();  return 4;
            case 0x1F: opRra();  return 4;
            case 0x27: opDaa();  return 4;
            case 0x2F: opCpl();  return 4;
            case 0x37: opScf();  return 4;
            case 0x3F: opCcf();  return 4;

            // --- RET cc ---
            case 0xC0: case 0xC8: case 0xD0: case 0xD8:
            case 0xE0: case 0xE8: case 0xF0: case 0xF8:
                if (cc((op >> 3) & 7)) {
                    r.pc = pop16();
                    wz = r.pc;
                    return 11;
                }
                return 5;

            // --- POP / RET / EXX / JP (HL) / LD SP,HL ---
            case 0xC1: case 0xD1: case 0xE1: case 0xF1:            // POP rr
                rp2((op >> 4) & 3, prefix) = pop16();
                return 10;
            case 0xC9: r.pc = pop16(); wz = r.pc; return 10;       // RET
            case 0xD9: {                                           // EXX
                u16 t;
                t = r.bc; r.bc = r.bc2; r.bc2 = t;
                t = r.de; r.de = r.de2; r.de2 = t;
                t = r.hl; r.hl = r.hl2; r.hl2 = t;
                return 4;
            }
            case 0xE9: r.pc = hlp(prefix); return 4;               // JP (HL)
            case 0xF9: r.sp = hlp(prefix); return 6;               // LD SP,HL

            // --- JP cc,nn / JP nn ---
            case 0xC2: case 0xCA: case 0xD2: case 0xDA:
            case 0xE2: case 0xEA: case 0xF2: case 0xFA: {          // JP cc,nn
                u16 nn = fetch16();
                wz = nn;
                if (cc((op >> 3) & 7)) r.pc = nn;
                return 10;
            }
            case 0xC3: {                                           // JP nn
                u16 nn = fetch16();
                wz = nn;
                r.pc = nn;
                return 10;
            }

            // --- E/S immédiates ---
            case 0xD3: {                                           // OUT (n),A
                u8 n = fetch8();
                bus.ioWrite(n, A());
                wz = u16(((n + 1) & 0xFF) | (u16(A()) << 8));
                return 11;
            }
            case 0xDB: {                                           // IN A,(n)
                u8 n = fetch8();
                wz = u16((u16(A()) << 8) + n + 1);
                setA(bus.ioRead(n));
                return 11;
            }

            // --- Échanges / interruptions ---
            case 0xE3: {                                           // EX (SP),HL
                u16 t = rd16(r.sp);
                wr16(r.sp, hlp(prefix));
                hlp(prefix) = t;
                wz = t;
                return 19;
            }
            case 0xEB: {              // EX DE,HL — JAMAIS affecté par DD/FD
                u16 t = r.de; r.de = r.hl; r.hl = t;
                return 4;
            }
            case 0xF3:                                             // DI
                r.iff1 = r.iff2 = false;
                return 4;
            case 0xFB:                                             // EI
                r.iff1 = r.iff2 = true;
                eiDelay = true;  // pas d'IRQ avant la PROCHAINE instruction
                return 4;

            // --- CALL cc,nn / PUSH / CALL nn ---
            case 0xC4: case 0xCC: case 0xD4: case 0xDC:
            case 0xE4: case 0xEC: case 0xF4: case 0xFC: {          // CALL cc,nn
                u16 nn = fetch16();
                wz = nn;
                if (cc((op >> 3) & 7)) {
                    push16(r.pc);
                    r.pc = nn;
                    return 17;
                }
                return 10;
            }
            case 0xC5: case 0xD5: case 0xE5: case 0xF5:            // PUSH rr
                push16(rp2((op >> 4) & 3, prefix));
                return 11;
            case 0xCD: {                                           // CALL nn
                u16 nn = fetch16();
                wz = nn;
                push16(r.pc);
                r.pc = nn;
                return 17;
            }

            // --- ALU A,n ---
            case 0xC6: case 0xCE: case 0xD6: case 0xDE:
            case 0xE6: case 0xEE: case 0xF6: case 0xFE:
                aluDo((op >> 3) & 7, fetch8());
                return 7;

            // --- RST ---
            case 0xC7: case 0xCF: case 0xD7: case 0xDF:
            case 0xE7: case 0xEF: case 0xF7: case 0xFF:
                push16(r.pc);
                r.pc = u16(op & 0x38);
                wz = r.pc;
                return 11;

            default:
                break;  // 0xCB/0xDD/0xED/0xFD sont interceptés par run()
        }
        return 4;  // inatteignable
    }

    // =========================================================================
    //  Point d'entrée : une instruction complète, préfixes compris.
    // =========================================================================
    int run() {
        int prefix = 0;        // 0 = HL, 1 = IX, 2 = IY
        int prefixCycles = 0;  // chaque octet DD/FD coûte 4 cycles et bump R
        u8 op = fetchOp();
        while (op == 0xDD || op == 0xFD) {
            prefix = (op == 0xDD) ? 1 : 2;  // seul le DERNIER préfixe compte
            prefixCycles += 4;
            op = fetchOp();
        }
        if (op == 0xCB)
            return prefixCycles + (prefix ? execIndexedCB(prefix) : execCB());
        if (op == 0xED)
            return prefixCycles + execED();  // DD/FD devant ED est sans effet
        return prefixCycles + execMain(op, prefix);
    }
};

}  // namespace

// =============================================================================
//  Points d'entrée de la classe Z80
// =============================================================================

void Z80::reset() {
    // Valeurs classiques de mise sous tension : AF=SP=0xFFFF, tout le reste
    // à zéro, interruptions coupées, mode IM 0.
    regs = Regs{};
    regs.af = 0xFFFF;
    regs.sp = 0xFFFF;
    irqLine = false;
    nmiPending = false;
    eiDelay = false;
    wz = 0;
    cycles = 0;
}

int Z80::step() {
    // 1) NMI en attente (front mémorisé) — prioritaire, insensible à IFF1.
    if (nmiPending) {
        nmiPending = false;
        int c = acceptNmi();
        cycles += u64(c);
        return c;
    }
    // 2) IRQ masquable : ligne active, IFF1, et pas juste après un EI.
    bool blocked = eiDelay;
    eiDelay = false;  // le délai d'EI ne dure qu'une instruction
    if (irqLine && regs.iff1 && !blocked) {
        int c = acceptIrq();
        cycles += u64(c);
        return c;
    }
    // 3) HALT sans interruption : le CPU exécute des NOP internes (4 cycles),
    //    le rafraîchissement (registre R) continue.
    if (regs.halted) {
        regs.r = u8((regs.r & 0x80) | ((regs.r + 1) & 0x7F));
        cycles += 4;
        return 4;
    }
    // 4) Instruction normale.
    int c = executeOne();
    cycles += u64(c);
    return c;
}

int Z80::executeOne() {
    Ops o{regs, bus, wz, eiDelay};
    return o.run();
}

int Z80::acceptNmi() {
    Ops o{regs, bus, wz, eiDelay};
    regs.halted = false;      // une interruption sort toujours du HALT
    o.bumpR();                // le cycle d'acceptation compte comme un fetch
    regs.iff1 = false;        // IFF2 conserve l'ancien IFF1 (restauré par RETN)
    o.push16(regs.pc);
    regs.pc = 0x0066;
    wz = regs.pc;
    return 11;
}

int Z80::acceptIrq() {
    Ops o{regs, bus, wz, eiDelay};
    regs.halted = false;
    o.bumpR();
    regs.iff1 = regs.iff2 = false;
    if (regs.im == 2) {
        // IM 2 : vecteur = (I << 8) | octet du bus. Sur SMS le bus de données
        // flotte à 0xFF, donc l'adresse de table est (I << 8) | 0xFF.
        o.push16(regs.pc);
        u16 vec = u16((u16(regs.i) << 8) | 0xFF);
        regs.pc = o.rd16(vec);
        wz = regs.pc;
        return 19;
    }
    // IM 0 : l'instruction lue sur le bus est 0xFF sur SMS -> RST 38h,
    // soit exactement le comportement d'IM 1.
    o.push16(regs.pc);
    regs.pc = 0x0038;
    wz = regs.pc;
    return 13;
}
