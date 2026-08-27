#!/usr/bin/env python3
# Copyright (C) 2026 VERHILLE Arnaud
# SPDX-License-Identifier: GPL-3.0-or-later
# Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
# (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

# =============================================================================
#  make_test_rom.py — génère roms/selftest.sms (32 Ko), une ROM de test pour
#  Sesame, assemblée à la main (aucun assembleur ni dépendance externe).
#
#  Ce que fait la ROM :
#   1. DI, SP = 0xDFF0.
#   2. Initialise le VDP (mode 4, affichage éteint pendant le chargement).
#   3. Charge une petite palette en CRAM (noir, blanc, rouge, vert, bleu).
#   4. Charge 6 tuiles en VRAM 0x0000 (vide, pleine, damier, rayures H,
#      rayures V, cadre) — chacune dans une couleur différente.
#   5. Remplit la table de noms (0x3800) : bordure blanche + bandes de motifs.
#   6. Écrit "SESAME SELFTEST" sur la console SDSC (ports 0xFC/0xFD), puis
#      exécute 5 auto-tests CPU (ADD+retenue, DAA, LDIR, PUSH/POP, XOR A) qui
#      impriment chacun "OK n" ou "FAIL n" via de vrais branchements.
#   7. Allume l'affichage (reg1 = 0x40) et boucle à l'infini, IRQ coupées.
#
#  Le Z80 démarre à PC = 0x0000 ; les vecteurs 0x0038 (IM1) et 0x0066 (NMI)
#  sont remplis par sécurité même si aucune interruption n'est activée.
# =============================================================================
import sys
from pathlib import Path

ROM_SIZE = 0x8000          # 32 Ko
NAME_TABLE = 0x3800        # adresse VRAM de la table de noms (reg2 = 0xFF)

from smsasm import (Asm, tile, vdp_reg, vdp_addr, upload, print_str,
                    emit_print_routine, SDSC_DATA)

# =============================================================================
#  Données graphiques construites en Python, embarquées dans la ROM et
#  transférées au VDP par OTIR (port 0xBE).
# =============================================================================
def build_palette():
    """CRAM[0..4] — format SMS : bits 0-1 rouge, 2-3 vert, 4-5 bleu."""
    return bytes([
        0x00,   # 0 : noir
        0x3F,   # 1 : blanc
        0x03,   # 2 : rouge
        0x0C,   # 3 : vert
        0x30,   # 4 : bleu
    ])


def build_tiles():
    """6 tuiles : vide, pleine (blanc), damier (rouge), rayures H (vert),
    rayures V (bleu), cadre (blanc)."""
    tiles = bytearray()
    # tuile 0 : vide (fond noir)
    tiles += tile([0x00] * 8, 1)
    # tuile 1 : pleine, couleur 1 (blanc) — sert de bordure
    tiles += tile([0xFF] * 8, 1)
    # tuile 2 : damier, couleur 2 (rouge)
    tiles += tile([0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55], 2)
    # tuile 3 : rayures horizontales, couleur 3 (vert)
    tiles += tile([0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00], 3)
    # tuile 4 : rayures verticales, couleur 4 (bleu)
    tiles += tile([0xCC] * 8, 4)
    # tuile 5 : cadre, couleur 1 (blanc), centre transparent (noir)
    tiles += tile([0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFF], 1)
    return bytes(tiles)


def build_name_table():
    """Table de noms 32x24, 2 octets par entrée (petit-boutiste, drapeaux=0).
    Bordure de tuiles pleines + bandes horizontales de motifs au centre :
    une image structurée, reconnaissable et non uniforme."""
    entries = []
    for row in range(24):
        for col in range(32):
            if row in (0, 23) or col in (0, 31):
                t = 1                       # bordure : tuile pleine blanche
            elif row in (3, 4):
                t = 2                       # bande damier rouge
            elif row in (7, 8):
                t = 3                       # bande rayures horizontales vertes
            elif row in (11, 12):
                t = 4                       # bande rayures verticales bleues
            elif row in (15, 16):
                t = 5                       # bande de cadres blancs
            elif row in (19, 20):
                t = 1 if (col % 2) == 0 else 0   # damier de tuiles pleines
            else:
                t = 0                       # fond vide
            entries += [t & 0xFF, 0x00]
    assert len(entries) == 32 * 24 * 2 == 1536
    return bytes(entries)


# =============================================================================
#  Construction de la ROM.
# =============================================================================
def build_rom():
    a = Asm(ROM_SIZE)

    # --- 0x0000 : point d'entrée (le Z80 boote ici) --------------------------
    a.di()                 # DI
    a.jp('main')           # JP main

    # --- 0x0038 : vecteur IM1 — sécurité : DI + boucle infinie ---------------
    a.org(0x0038)
    a.di()                 # DI
    a.jr(0x0039)           # JR $ (boucle sur soi-même)

    # --- 0x0066 : vecteur NMI (bouton Pause) — retour immédiat ---------------
    a.org(0x0066)
    a.retn()               # RETN

    # --- Programme principal -------------------------------------------------
    a.org(0x0100)
    a.label('main')
    a.ld_sp_nn(0xDFF0)     # LD SP,0xDFF0

    # 2. Initialisation du VDP, affichage ÉTEINT pendant le chargement.
    vdp_reg(a, 0, 0x04)    # reg0 : mode 4
    vdp_reg(a, 1, 0x00)    # reg1 : affichage off (allumé tout à la fin)
    vdp_reg(a, 2, 0xFF)    # reg2 : table de noms à 0x3800
    vdp_reg(a, 5, 0xFF)    # reg5 : table d'attributs de sprites
    vdp_reg(a, 6, 0xFF)    # reg6 : tuiles de sprites
    vdp_reg(a, 7, 0x00)    # reg7 : couleur de bordure
    vdp_reg(a, 8, 0x00)    # reg8 : défilement X = 0
    vdp_reg(a, 9, 0x00)    # reg9 : défilement Y = 0
    vdp_reg(a, 10, 0xFF)   # reg10 : compteur d'IRQ ligne (désarmé)

    # 3. Palette (CRAM à partir de l'index 0).
    vdp_addr(a, 0x0000, 0xC0)          # pointeur CRAM[0], code écriture CRAM
    upload(a, 'palette', len(PALETTE))

    # 4. Tuiles en VRAM 0x0000.
    vdp_addr(a, 0x0000, 0x40)          # pointeur VRAM 0x0000, code écriture
    upload(a, 'tiles', len(TILES))

    # 5. Table de noms à 0x3800.
    vdp_addr(a, NAME_TABLE, 0x40)
    upload(a, 'nametable', len(NAMETABLE))

    # 6. Bannière SDSC puis auto-tests CPU.
    print_str(a, 'str_banner')          # "SESAME SELFTEST\n"

    # ---- Test 1 : ADD avec retenue (0xFF + 1 -> 0, C=1, Z=1) ----------------
    a.ld_a_n(0xFF)         # LD A,0xFF
    a.add_a_n(0x01)        # ADD A,1
    a.jp_nc('fail1')       # JP NC,fail1 (la retenue doit être levée)
    a.jp_nz('fail1')       # JP NZ,fail1 (le résultat doit être nul)
    print_str(a, 'str_ok1')
    a.jp('test2')          # JP test2
    a.label('fail1')
    print_str(a, 'str_fail1')

    # ---- Test 2 : DAA (0x15 + 0x27 en BCD = 0x42) ---------------------------
    a.label('test2')
    a.ld_a_n(0x15)         # LD A,0x15
    a.add_a_n(0x27)        # ADD A,0x27 -> 0x3C
    a.daa()                # DAA -> 0x42
    a.cp_n(0x42)           # CP 0x42
    a.jp_nz('fail2')       # JP NZ,fail2
    print_str(a, 'str_ok2')
    a.jp('test3')
    a.label('fail2')
    print_str(a, 'str_fail2')

    # ---- Test 3 : LDIR copie 8 octets ROM -> RAM, puis vérification ---------
    a.label('test3')
    a.ld_hl_nn('copysrc')  # LD HL,copysrc
    a.ld_de_nn(0xC100)     # LD DE,0xC100
    a.ld_bc_nn(8)          # LD BC,8
    a.ldir()               # LDIR
    a.ld_hl_nn('copysrc')  # LD HL,copysrc (relecture pour comparer)
    a.ld_de_nn(0xC100)     # LD DE,0xC100
    a.ld_b_n(8)            # LD B,8
    a.label('t3_loop')
    a.ld_a_de()            # LD A,(DE)
    a.cp_hl()              # CP (HL)
    a.jp_nz('fail3')       # JP NZ,fail3
    a.inc_hl()             # INC HL
    a.inc_de()             # INC DE
    a.djnz('t3_loop')      # DJNZ t3_loop
    print_str(a, 'str_ok3')
    a.jp('test4')
    a.label('fail3')
    print_str(a, 'str_fail3')

    # ---- Test 4 : PUSH/POP aller-retour par la pile -------------------------
    a.label('test4')
    a.ld_hl_nn(0x1234)     # LD HL,0x1234
    a.push_hl()            # PUSH HL
    a.pop_de()             # POP DE
    a.ld_a_h()             # LD A,H
    a.cp_d()               # CP D
    a.jp_nz('fail4')       # JP NZ,fail4
    a.ld_a_l()             # LD A,L
    a.cp_e()               # CP E
    a.jp_nz('fail4')       # JP NZ,fail4
    print_str(a, 'str_ok4')
    a.jp('test5')
    a.label('fail4')
    print_str(a, 'str_fail4')

    # ---- Test 5 : XOR A doit donner A=0 et Z=1 ------------------------------
    a.label('test5')
    a.ld_a_n(0x5A)         # LD A,0x5A
    a.xor_a()              # XOR A
    a.jp_nz('fail5')       # JP NZ,fail5
    print_str(a, 'str_ok5')
    a.jp('done')
    a.label('fail5')
    print_str(a, 'str_fail5')

    # 7. Affichage ON (reg1 = 0x40), puis boucle infinie, IRQ coupées.
    a.label('done')
    vdp_reg(a, 1, 0x40)    # reg1 : display on (pas d'IRQ VBlank : bit5 = 0)
    a.label('halt')
    a.jp('halt')           # JP halt (boucle infinie)

    # --- Sous-routine : imprime la chaîne (HL), terminée par 0, sur SDSC -----
    emit_print_routine(a)

    # --- Données -------------------------------------------------------------
    a.label('palette');   a.db(PALETTE)
    a.label('tiles');     a.db(TILES)
    a.label('nametable'); a.db(NAMETABLE)
    a.label('copysrc');   a.db(b'MAPPER!!')       # 8 octets pour le test LDIR
    a.label('str_banner'); a.db(b'SESAME SELFTEST\n\0')
    for n in range(1, 6):
        a.label(f'str_ok{n}');   a.db(f'OK {n}\n\0'.encode())
        a.label(f'str_fail{n}'); a.db(f'FAIL {n}\n\0'.encode())

    a.resolve()
    return bytes(a.buf)    # déjà remplie de 0x00 jusqu'à 32 Ko


PALETTE = build_palette()
TILES = build_tiles()
NAMETABLE = build_name_table()


def main():
    out = Path(__file__).resolve().parent.parent / 'roms' / 'selftest.sms'
    out.parent.mkdir(parents=True, exist_ok=True)
    rom = build_rom()
    assert len(rom) == ROM_SIZE
    out.write_bytes(rom)
    print(f'wrote {out} ({len(rom)} bytes)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
