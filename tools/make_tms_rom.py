#!/usr/bin/env python3
# Copyright (C) 2026 VERHILLE Arnaud
# SPDX-License-Identifier: GPL-3.0-or-later
# Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
# (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

# =============================================================================
#  make_tms_rom.py — génère roms/tmstest.sg : ROM de test des modes hérités
#  TMS9918 (extension .sg = jeu SG-1000, émulé comme une SMS).
#
#  Écran Graphic II (M2, M4=0) : bandes de motifs colorées balayant les 15
#  couleurs de la palette TMS fixe (aucune écriture CRAM : le fallback
#  palette-TMS de Sesame est donc exercé). Sprites TMS 8×8 : cinq sur la
#  ligne 100 (le 5e lève le drapeau 5S avec son numéro), deux qui se
#  chevauchent ligne 40 (coïncidence). Verdicts SDSC :
#    « TMS5S OK » : statut bit 6 + numéro du 5e sprite (4) ;
#    « TMSTC OK » : statut bit 5 (coïncidence).
#  Capture de référence : tests/refs/vdp_tms.ppm (frame 20).
# =============================================================================
import sys
from pathlib import Path

from smsasm import (Asm, vdp_reg, vdp_addr, upload, print_str,
                    emit_print_routine, VDP_VCOUNT, VDP_CTRL)

ROM_SIZE = 0x8000

NAME_BASE = 0x3800     # reg2 = 0x0E
PAT_BASE = 0x0000      # reg4 = 0x03 (base 0, masque plein)
COL_BASE = 0x2000      # reg3 = 0xFF (base 0x2000, masque plein)
SAT_BASE = 0x3F00      # reg5 = 0x7E
SPR_PAT = 0x1800       # reg6 = 0x03


def build_tables():
    """Table de noms (identité ×3 tiers), motifs et couleurs Graphic II."""
    name = bytes(range(256)) * 3
    pattern = bytearray()
    color = bytearray()
    for third in range(3):
        for tile in range(256):
            for row in range(8):
                # Motif : rayures dépendant du tiers (visuellement distinct).
                pattern.append([0xF0, 0xCC, 0xAA][third])
                # Couleurs : encre et papier balayant la palette TMS.
                fg = 1 + (tile + row) % 15
                bg = 1 + (tile // 16 + third) % 15
                color.append((fg << 4) | bg)
    return name, bytes(pattern), bytes(color)


def build_sat():
    """Sprites TMS : 4 octets (Y, X, motif, EC|couleur) par sprite."""
    sat = bytearray()
    for i in range(5):                     # 5 sprites ligne 100 -> 5S
        sat += bytes([99, 20 + 30 * i, 0, 2 + i])
    sat += bytes([39, 200, 0, 8])          # paire en coïncidence ligne 40
    sat += bytes([39, 204, 0, 9])
    sat += bytes([0xD0, 0, 0, 0])          # terminateur
    return bytes(sat)


NAME, PATTERN, COLOR = build_tables()
SAT = build_sat()
SPRITE_TILE = bytes([0xFF] * 8)            # motif 0 : bloc plein 8×8


def emit_wait_frame(a):
    a.label('wait_frame')
    a.label('wf_vblank')
    a.in_a(VDP_VCOUNT)
    a.cp_n(0xC0)
    a.jr_c('wf_vblank')
    a.label('wf_top')
    a.in_a(VDP_VCOUNT)
    a.cp_n(0xC0)
    a.jr_nc('wf_top')
    a.ret()


def build_rom():
    a = Asm(ROM_SIZE)
    a.di()
    a.jp('main')
    a.org(0x0066)
    a.retn()

    a.org(0x0100)
    a.label('main')
    a.ld_sp_nn(0xDFF0)

    # Mode Graphic II, affichage éteint pendant le chargement.
    vdp_reg(a, 0, 0x02)    # M2, M4 = 0 -> mode hérité
    vdp_reg(a, 1, 0x00)
    vdp_reg(a, 2, 0x0E)    # table de noms 0x3800
    vdp_reg(a, 3, 0xFF)    # couleurs 0x2000, masque plein
    vdp_reg(a, 4, 0x03)    # motifs 0x0000, masque plein
    vdp_reg(a, 5, 0x7E)    # SAT 0x3F00
    vdp_reg(a, 6, 0x03)    # motifs de sprites 0x1800
    vdp_reg(a, 7, 0x01)    # fond noir
    vdp_reg(a, 10, 0xFF)

    vdp_addr(a, PAT_BASE, 0x40)
    upload(a, 'pattern', len(PATTERN))
    vdp_addr(a, COL_BASE, 0x40)
    upload(a, 'color', len(COLOR))
    vdp_addr(a, NAME_BASE, 0x40)
    upload(a, 'name', len(NAME))
    vdp_addr(a, SPR_PAT, 0x40)
    upload(a, 'spr_tile', len(SPRITE_TILE))
    vdp_addr(a, SAT_BASE, 0x40)
    upload(a, 'sat', len(SAT))

    print_str(a, 'str_banner')
    vdp_reg(a, 1, 0x40)    # affichage ON (sprites 8×8, MAG off)

    # Purge le statut, laisse deux trames se dessiner, lit les drapeaux.
    a.in_a(VDP_CTRL)
    a.call('wait_frame')
    a.call('wait_frame')
    a.in_a(VDP_CTRL)       # statut : b6=5S, b5=coïncidence, b4-0=numéro
    a.push_af()
    a.and_n(0x5F)          # 5S + numéro attendu : 0x40 | 4 = 0x44
    a.cp_n(0x44)
    a.jr_nz('s5_fail')
    print_str(a, 'str_s5_ok')
    a.jr('s5_done')
    a.label('s5_fail')
    print_str(a, 'str_s5_fail')
    a.label('s5_done')
    a.pop_af()
    a.and_n(0x20)
    a.jr_z('tc_fail')
    print_str(a, 'str_tc_ok')
    a.jr('tc_done')
    a.label('tc_fail')
    print_str(a, 'str_tc_fail')
    a.label('tc_done')

    a.label('halt')
    a.jr('halt')

    emit_wait_frame(a)
    emit_print_routine(a)

    a.label('spr_tile');    a.db(SPRITE_TILE)
    a.label('sat');         a.db(SAT)
    a.label('str_banner');  a.db(b'SESAME TMSTEST\n\0')
    a.label('str_s5_ok');   a.db(b'TMS5S OK\n\0')
    a.label('str_s5_fail'); a.db(b'TMS5S FAIL\n\0')
    a.label('str_tc_ok');   a.db(b'TMSTC OK\n\0')
    a.label('str_tc_fail'); a.db(b'TMSTC FAIL\n\0')
    a.label('name');        a.db(NAME)
    a.label('pattern');     a.db(PATTERN)
    a.label('color');       a.db(COLOR)

    a.resolve()
    return bytes(a.buf)


def main():
    out = Path(__file__).resolve().parent.parent / 'roms' / 'tmstest.sg'
    out.parent.mkdir(parents=True, exist_ok=True)
    rom = build_rom()
    assert len(rom) == ROM_SIZE
    out.write_bytes(rom)
    print(f'wrote {out} ({len(rom)} bytes)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
