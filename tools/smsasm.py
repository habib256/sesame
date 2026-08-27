#!/usr/bin/env python3
# Copyright (C) 2026 VERHILLE Arnaud
# SPDX-License-Identifier: GPL-3.0-or-later
# Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
# (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

# =============================================================================
#  smsasm.py — mini-assembleur Z80 et helpers VDP partagés par les
#  générateurs de ROM de test (make_test_rom.py, make_vdp_rom.py).
#  Aucun assembleur externe : encodage octet par octet, labels et fixups.
# =============================================================================

# --- Ports matériels ---------------------------------------------------------
VDP_DATA = 0xBE            # port données VDP
VDP_CTRL = 0xBF            # port contrôle VDP
VDP_VCOUNT = 0x7E          # VCounter (lecture)
SDSC_DATA = 0xFD           # console de débogage SDSC (données)


# =============================================================================
#  Mini-assembleur : un tampon d'octets, des labels, et des correctifs
#  (fixups) résolus en fin d'assemblage pour les sauts avant.
# =============================================================================
class Asm:
    def __init__(self, size):
        self.buf = bytearray(size)
        self.pos = 0
        self.labels = {}
        self.fixups = []   # (position, nom_du_label, genre) — genre: 'abs16'/'rel8'

    # --- gestion du flot ----------------------------------------------------
    def db(self, *vals):
        """Émet des octets bruts (int ou bytes)."""
        for v in vals:
            if isinstance(v, (bytes, bytearray)):
                self.buf[self.pos:self.pos + len(v)] = v
                self.pos += len(v)
            else:
                self.buf[self.pos] = v & 0xFF
                self.pos += 1

    def org(self, addr):
        """Positionne le pointeur d'assemblage (les trous restent à 0x00)."""
        assert addr >= self.pos, "org ne peut pas revenir en arrière"
        self.pos = addr

    def label(self, name):
        self.labels[name] = self.pos

    def _addr16(self, target):
        """Émet une adresse 16 bits (label ou entier), fixup si inconnue."""
        if isinstance(target, int):
            self.db(target & 0xFF, target >> 8)
        else:
            self.fixups.append((self.pos, target, 'abs16'))
            self.db(0, 0)

    def _rel8(self, target):
        """Émet un déplacement relatif 8 bits (pour JR/DJNZ)."""
        if isinstance(target, str) and target in self.labels:
            target = self.labels[target]
        if isinstance(target, int):
            off = target - (self.pos + 1)
            assert -128 <= off <= 127, "saut relatif hors de portée"
            self.db(off & 0xFF)
        else:
            self.fixups.append((self.pos, target, 'rel8'))
            self.db(0)

    def resolve(self):
        """Résout tous les sauts avant."""
        for pos, name, kind in self.fixups:
            addr = self.labels[name]
            if kind == 'abs16':
                self.buf[pos] = addr & 0xFF
                self.buf[pos + 1] = addr >> 8
            else:  # rel8
                off = addr - (pos + 1)
                assert -128 <= off <= 127, f"saut relatif vers {name} hors de portée"
                self.buf[pos] = off & 0xFF

    # --- instructions Z80 (encodage octet par octet) ------------------------
    def di(self):              self.db(0xF3)                  # DI
    def ei(self):              self.db(0xFB)                  # EI
    def im1(self):             self.db(0xED, 0x56)            # IM 1
    def ld_sp_nn(self, nn):    self.db(0x31, nn & 0xFF, nn >> 8)  # LD SP,nn
    def ld_a_n(self, n):       self.db(0x3E, n)               # LD A,n
    def ld_b_n(self, n):       self.db(0x06, n)               # LD B,n
    def ld_c_n(self, n):       self.db(0x0E, n)               # LD C,n
    def ld_hl_nn(self, t):     self.db(0x21); self._addr16(t) # LD HL,nn
    def ld_de_nn(self, t):     self.db(0x11); self._addr16(t) # LD DE,nn
    def ld_bc_nn(self, t):     self.db(0x01); self._addr16(t) # LD BC,nn
    def ld_a_hl(self):         self.db(0x7E)                  # LD A,(HL)
    def ld_a_de(self):         self.db(0x1A)                  # LD A,(DE)
    def ld_a_h(self):          self.db(0x7C)                  # LD A,H
    def ld_a_l(self):          self.db(0x7D)                  # LD A,L
    def ld_mem_a(self, t):     self.db(0x32); self._addr16(t) # LD (nn),A
    def ld_a_mem(self, t):     self.db(0x3A); self._addr16(t) # LD A,(nn)
    def inc_a(self):           self.db(0x3C)                  # INC A
    def inc_hl(self):          self.db(0x23)                  # INC HL
    def inc_de(self):          self.db(0x13)                  # INC DE
    def out_a(self, port):     self.db(0xD3, port)            # OUT (port),A
    def in_a(self, port):      self.db(0xDB, port)            # IN A,(port)
    def otir(self):            self.db(0xED, 0xB3)            # OTIR
    def ldir(self):            self.db(0xED, 0xB0)            # LDIR
    def add_a_n(self, n):      self.db(0xC6, n)               # ADD A,n
    def and_n(self, n):        self.db(0xE6, n)               # AND n
    def daa(self):             self.db(0x27)                  # DAA
    def cp_n(self, n):         self.db(0xFE, n)               # CP n
    def cp_hl(self):           self.db(0xBE)                  # CP (HL)
    def cp_d(self):            self.db(0xBA)                  # CP D
    def cp_e(self):            self.db(0xBB)                  # CP E
    def or_a(self):            self.db(0xB7)                  # OR A
    def xor_a(self):           self.db(0xAF)                  # XOR A
    def push_af(self):         self.db(0xF5)                  # PUSH AF
    def pop_af(self):          self.db(0xF1)                  # POP AF
    def push_hl(self):         self.db(0xE5)                  # PUSH HL
    def pop_de(self):          self.db(0xD1)                  # POP DE
    def call(self, t):         self.db(0xCD); self._addr16(t) # CALL nn
    def ret(self):             self.db(0xC9)                  # RET
    def ret_z(self):           self.db(0xC8)                  # RET Z
    def reti(self):            self.db(0xED, 0x4D)            # RETI
    def retn(self):            self.db(0xED, 0x45)            # RETN
    def jp(self, t):           self.db(0xC3); self._addr16(t) # JP nn
    def jp_nz(self, t):        self.db(0xC2); self._addr16(t) # JP NZ,nn
    def jp_nc(self, t):        self.db(0xD2); self._addr16(t) # JP NC,nn
    def jr(self, t):           self.db(0x18); self._rel8(t)   # JR e
    def jr_z(self, t):         self.db(0x28); self._rel8(t)   # JR Z,e
    def jr_nz(self, t):        self.db(0x20); self._rel8(t)   # JR NZ,e
    def jr_c(self, t):         self.db(0x38); self._rel8(t)   # JR C,e
    def jr_nc(self, t):        self.db(0x30); self._rel8(t)   # JR NC,e
    def djnz(self, t):         self.db(0x10); self._rel8(t)   # DJNZ e


# =============================================================================
#  Helpers d'assemblage de plus haut niveau (séquences VDP, SDSC).
# =============================================================================
def vdp_reg(a, reg, val):
    """Écrit `val` dans le registre VDP `reg` (2 octets sur le port 0xBF)."""
    a.ld_a_n(val)          # LD A,val
    a.out_a(VDP_CTRL)      # OUT (0xBF),A
    a.ld_a_n(0x80 | reg)   # LD A,0x80|reg
    a.out_a(VDP_CTRL)      # OUT (0xBF),A


def vdp_addr(a, addr, code):
    """Positionne le pointeur d'adresse VDP. code : 0x40 = écriture VRAM,
    0xC0 = écriture CRAM."""
    a.ld_a_n(addr & 0xFF)               # LD A,addr.lo
    a.out_a(VDP_CTRL)                   # OUT (0xBF),A
    a.ld_a_n(((addr >> 8) & 0x3F) | code)  # LD A,addr.hi | code
    a.out_a(VDP_CTRL)                   # OUT (0xBF),A


def upload(a, label, length):
    """Transfère `length` octets depuis `label` vers le port données du VDP
    (OTIR : B=0 signifie 256 itérations)."""
    a.ld_hl_nn(label)      # LD HL,label
    a.ld_c_n(VDP_DATA)     # LD C,0xBE
    full, rest = divmod(length, 256)
    if full:
        a.ld_b_n(0)        # LD B,0 (= 256)
        for _ in range(full):
            a.otir()       # OTIR (B retombe à 0, le suivant refait 256)
    if rest:
        a.ld_b_n(rest)     # LD B,rest
        a.otir()           # OTIR


def print_str(a, label):
    """CALL print avec HL = chaîne terminée par zéro (routine 'print' à
    fournir dans la ROM — voir emit_print_routine)."""
    a.ld_hl_nn(label)      # LD HL,label
    a.call('print')        # CALL print


def emit_print_routine(a):
    """Sous-routine 'print' : imprime la chaîne (HL), terminée par 0, sur la
    console SDSC."""
    a.label('print')
    a.ld_a_hl()            # LD A,(HL)
    a.or_a()               # OR A (positionne Z si fin de chaîne)
    a.ret_z()              # RET Z
    a.out_a(SDSC_DATA)     # OUT (0xFD),A
    a.inc_hl()             # INC HL
    a.jr('print')          # JR print


def tile(rows, color):
    """Encode une tuile 8x8 en mode 4 : 4 bitplanes par rangée.
    `rows` = 8 masques de pixels ; `color` = index de palette (1-15) :
    le motif est recopié sur chaque plan dont le bit de couleur est à 1."""
    out = bytearray()
    for r in rows:
        for plane in range(4):
            out.append(r if (color >> plane) & 1 else 0x00)
    return bytes(out)
