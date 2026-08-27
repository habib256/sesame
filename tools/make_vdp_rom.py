#!/usr/bin/env python3
# Copyright (C) 2026 VERHILLE Arnaud
# SPDX-License-Identifier: GPL-3.0-or-later
# Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
# (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

# =============================================================================
#  make_vdp_rom.py — génère roms/vdptest.sms (32 Ko), la ROM de test VDP :
#  trois scènes successives, chacune capturée en PPM par run_selftests.py et
#  comparée aux images étalons commitées dans tests/refs/.
#
#   Scène A (trames ~2-31)  : fond structuré + défilement X=0xF8 / Y=16 +
#     colonne gauche masquée (reg0 bit 5). Capture à la trame 20.
#   Scène B (trames ~32-61) : sprites 8x16 — 10 sprites sur la même ligne
#     (les 2 derniers tombent sous la limite de 8 -> drapeau overflow), une
#     paire qui se chevauche (-> drapeau collision). Les drapeaux lus dans
#     le registre de statut sont imprimés sur SDSC (« OVF OK », « COL OK »).
#     Capture à la trame 50.
#   Scène C (trames ~62-...) : split-screen par IRQ LIGNE (reg 10) — le
#     handler pousse le défilement X à 0x80 en pleine trame, la boucle le
#     remet à 0 à chaque VBlank : cisaillement stable, « IRQ OK » si le
#     handler a tourné. Capture à la trame 80.
#
#  L'attente de trame se fait en scrutant le VCounter (port 0x7E) et jamais
#  le registre de statut : le lire consommerait les drapeaux que la scène B
#  veut vérifier et l'IRQ que la scène C veut recevoir.
# =============================================================================
import sys
from pathlib import Path

from smsasm import (Asm, tile, vdp_reg, vdp_addr, upload, print_str,
                    emit_print_routine, VDP_VCOUNT, VDP_CTRL)

ROM_SIZE = 0x8000
NAME_TABLE = 0x3800        # reg2 = 0xFF
SAT = 0x3F00               # table d'attributs de sprites (reg5 = 0xFF)
FRAME_COUNT_IRQ = 0xC001   # compteur d'IRQ ligne (RAM)


# =============================================================================
#  Données graphiques (mêmes conventions que la ROM selftest).
# =============================================================================
PALETTE = bytes([
    0x00,   # 0 : noir
    0x3F,   # 1 : blanc
    0x03,   # 2 : rouge
    0x0C,   # 3 : vert
    0x30,   # 4 : bleu
])

# Palette sprites (CRAM 16+) : mêmes couleurs (l'index 0 reste transparent).
SPRITE_PALETTE = bytes([0x00, 0x3F, 0x03, 0x0C, 0x30])


def build_tiles():
    """6 tuiles : vide, pleine, damier, rayures H, rayures V, cadre."""
    tiles = bytearray()
    tiles += tile([0x00] * 8, 1)                                   # 0 vide
    tiles += tile([0xFF] * 8, 1)                                   # 1 pleine
    tiles += tile([0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55], 2)  # 2 damier
    tiles += tile([0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00], 3)  # 3 rayures H
    tiles += tile([0xCC] * 8, 4)                                   # 4 rayures V
    tiles += tile([0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFF], 1)  # 5 cadre
    return bytes(tiles)


def build_name_table():
    """Fond NON uniforme et asymétrique, pour que tout défilement (X et Y)
    déplace des motifs visibles : colonnes numérotées par des bandes
    verticales, rangées par des bandes horizontales."""
    entries = []
    for row in range(24):
        for col in range(32):
            if row == 0 or col == 0:
                t = 1                        # règles : bord haut/gauche plein
            elif row % 6 == 3:
                t = 3                        # bandes horizontales vertes
            elif col % 8 == 4:
                t = 4                        # bandes verticales bleues
            elif (row + col) % 7 == 0:
                t = 2                        # diagonale de damiers rouges
            elif row == 22 and col % 2:
                t = 5                        # pointillé de cadres en bas
            else:
                t = 0
            entries += [t & 0xFF, 0x00]
    return bytes(entries)


def build_sat():
    """Table d'attributs de sprites (mode 8x16, tuile paire = 2 -> 2 et 3
    empilées) :
      - sprites 0-9 : Y=99 (ligne écran 100), X = 8 + 22*i — DIX sprites sur
        la même ligne, la limite matérielle de 8 en laisse tomber deux et
        lève le drapeau overflow ;
      - sprites 10-11 : paire qui se CHEVAUCHE (X=200/204, Y=39) -> collision ;
      - sprite 12 : Y=0xD0, terminateur (fin de la liste en mode 192 lignes).
    """
    ys = bytearray(64)
    xs = bytearray(128)
    for i in range(10):
        ys[i] = 99
        xs[2 * i] = 8 + 22 * i
        xs[2 * i + 1] = 2          # tuile 2 (paire 2/3 en 8x16)
    ys[10] = 39
    xs[20] = 200
    xs[21] = 2
    ys[11] = 39
    xs[22] = 204
    xs[23] = 2
    ys[12] = 0xD0                  # terminateur
    return bytes(ys), bytes(xs)


TILES = build_tiles()
NAMETABLE = build_name_table()
SAT_Y, SAT_X = build_sat()


# =============================================================================
#  Attente de trame par VCounter (port 0x7E), sans toucher au statut.
# =============================================================================
def emit_wait_frame(a):
    """Sous-routine 'wait_frame' : attend d'être dans le VBlank (V >= 0xC0)
    puis le retour en haut d'écran (V < 0xC0) — soit exactement une
    frontière de trame."""
    a.label('wait_frame')
    a.label('wf_vblank')
    a.in_a(VDP_VCOUNT)     # IN A,(0x7E)
    a.cp_n(0xC0)           # CP 0xC0
    a.jr_c('wf_vblank')    # JR C — encore dans la zone visible
    a.label('wf_top')
    a.in_a(VDP_VCOUNT)
    a.cp_n(0xC0)
    a.jr_nc('wf_top')      # JR NC — encore dans le VBlank
    a.ret()


def wait_frames(a, n, tag):
    """Attend `n` trames (DJNZ sur wait_frame, qui ne touche que A/drapeaux)."""
    a.ld_b_n(n)            # LD B,n
    a.label(f'wl_{tag}')
    a.call('wait_frame')   # CALL wait_frame
    a.djnz(f'wl_{tag}')    # DJNZ


# =============================================================================
#  Construction de la ROM.
# =============================================================================
def build_rom():
    a = Asm(ROM_SIZE)

    # --- 0x0000 : point d'entrée ---------------------------------------------
    a.di()
    a.jp('main')

    # --- 0x0038 : vecteur IM1 = handler d'IRQ ligne (scène C) ----------------
    a.org(0x0038)
    a.jp('line_irq')

    # --- 0x0066 : vecteur NMI — retour immédiat ------------------------------
    a.org(0x0066)
    a.retn()

    a.org(0x0100)
    a.label('main')
    a.ld_sp_nn(0xDFF0)

    # Init VDP, affichage éteint pendant le chargement.
    vdp_reg(a, 0, 0x04)    # mode 4
    vdp_reg(a, 1, 0x00)    # affichage off
    vdp_reg(a, 2, 0xFF)    # table de noms 0x3800
    vdp_reg(a, 5, 0xFF)    # SAT 0x3F00
    vdp_reg(a, 6, 0xFF)    # tuiles de sprites en 0x2000... (bit2=1)
    vdp_reg(a, 7, 0x00)
    vdp_reg(a, 8, 0x00)
    vdp_reg(a, 9, 0x00)
    vdp_reg(a, 10, 0xFF)   # IRQ ligne désarmée

    # Palette fond (CRAM 0..) + palette sprites (CRAM 16..).
    vdp_addr(a, 0x0000, 0xC0)
    upload(a, 'palette', len(PALETTE))
    vdp_addr(a, 0x0010, 0xC0)
    upload(a, 'spr_palette', len(SPRITE_PALETTE))

    # Tuiles : le fond ET les sprites pointent sur la même banque 0x0000
    # (reg6 bit 2 = 0).
    vdp_reg(a, 6, 0x00)
    vdp_addr(a, 0x0000, 0x40)
    upload(a, 'tiles', len(TILES))

    # Table de noms.
    vdp_addr(a, NAME_TABLE, 0x40)
    upload(a, 'nametable', len(NAMETABLE))

    a.xor_a()
    a.ld_mem_a(FRAME_COUNT_IRQ)   # compteur d'IRQ ligne = 0

    print_str(a, 'str_banner')    # "SESAME VDPTEST\n"

    # ---- Scène A : défilement + colonne gauche masquée ----------------------
    vdp_reg(a, 8, 0xF8)    # défilement X (décale le fond de 8 px)
    vdp_reg(a, 9, 0x10)    # défilement Y = 16
    vdp_reg(a, 0, 0x24)    # mode 4 + colonne gauche masquée (bit 5)
    vdp_reg(a, 1, 0x40)    # affichage ON
    wait_frames(a, 30, 'sceneA')

    # ---- Scène B : sprites 8x16, overflow et collision ----------------------
    vdp_reg(a, 8, 0x00)    # scroll neutre
    vdp_reg(a, 9, 0x00)
    vdp_reg(a, 0, 0x04)    # colonne démasquée
    vdp_reg(a, 1, 0x42)    # affichage ON + sprites 8x16 (bit 1)
    vdp_addr(a, SAT, 0x40)
    upload(a, 'sat_y', len(SAT_Y))
    vdp_addr(a, SAT + 0x80, 0x40)
    upload(a, 'sat_x', len(SAT_X))

    # Purge le statut (drapeaux hérités des scènes précédentes), laisse le
    # VDP dessiner deux trames, puis lit les drapeaux frais.
    a.in_a(VDP_CTRL)       # IN A,(0xBF) — lecture du statut (purge)
    wait_frames(a, 2, 'sceneB_settle')
    a.in_a(VDP_CTRL)       # IN A,(0xBF) — statut : b6=overflow, b5=collision
    a.push_af()
    a.and_n(0x40)          # bit 6 : overflow sprites
    a.jr_z('ovf_fail')
    print_str(a, 'str_ovf_ok')
    a.jr('ovf_done')
    a.label('ovf_fail')
    print_str(a, 'str_ovf_fail')
    a.label('ovf_done')
    a.pop_af()
    a.and_n(0x20)          # bit 5 : collision
    a.jr_z('col_fail')
    print_str(a, 'str_col_ok')
    a.jr('col_done')
    a.label('col_fail')
    print_str(a, 'str_col_fail')
    a.label('col_done')
    wait_frames(a, 28, 'sceneB')

    # ---- Scène C : split-screen par IRQ ligne -------------------------------
    # Sprites hors écran (terminateur en tête de SAT) pour une image nette.
    vdp_addr(a, SAT, 0x40)
    a.ld_a_n(0xD0)
    a.out_a(0xBE)          # SAT[0].Y = terminateur
    vdp_reg(a, 10, 95)     # compteur d'IRQ ligne : fenêtre à la ligne ~96
    vdp_reg(a, 0, 0x14)    # mode 4 + IE1 (bit 4 : IRQ ligne)
    a.im1()
    a.ei()
    a.ld_b_n(25)           # 25 trames de split, puis vérification
    a.label('sceneC_loop')
    vdp_reg(a, 8, 0x00)    # en début de trame : scroll X = 0 (haut d'écran)
    a.call('wait_frame')   # l'IRQ ligne mettra 0x80 en pleine trame
    a.djnz('sceneC_loop')
    a.ld_a_mem(FRAME_COUNT_IRQ)
    a.or_a()
    a.jr_z('irq_fail')
    print_str(a, 'str_irq_ok')
    a.jr('irq_done')
    a.label('irq_fail')
    print_str(a, 'str_irq_fail')
    a.label('irq_done')

    # Boucle finale : le split continue indéfiniment (captures tardives OK).
    a.label('forever')
    vdp_reg(a, 8, 0x00)
    a.call('wait_frame')
    a.jr('forever')

    # ---- Handler d'IRQ ligne : split du défilement X ------------------------
    a.label('line_irq')
    a.push_af()
    a.in_a(VDP_CTRL)       # ack : la lecture du statut retombe /INT
    vdp_reg(a, 8, 0x80)    # défilement X = 0x80 pour le bas de l'écran
    a.ld_a_mem(FRAME_COUNT_IRQ)
    a.inc_a()
    a.ld_mem_a(FRAME_COUNT_IRQ)
    a.pop_af()
    a.ei()
    a.reti()

    # ---- Sous-routines et données -------------------------------------------
    emit_wait_frame(a)
    emit_print_routine(a)

    a.label('palette');      a.db(PALETTE)
    a.label('spr_palette');  a.db(SPRITE_PALETTE)
    a.label('tiles');        a.db(TILES)
    a.label('nametable');    a.db(NAMETABLE)
    a.label('sat_y');        a.db(SAT_Y)
    a.label('sat_x');        a.db(SAT_X)
    a.label('str_banner');   a.db(b'SESAME VDPTEST\n\0')
    a.label('str_ovf_ok');   a.db(b'OVF OK\n\0')
    a.label('str_ovf_fail'); a.db(b'OVF FAIL\n\0')
    a.label('str_col_ok');   a.db(b'COL OK\n\0')
    a.label('str_col_fail'); a.db(b'COL FAIL\n\0')
    a.label('str_irq_ok');   a.db(b'IRQ OK\n\0')
    a.label('str_irq_fail'); a.db(b'IRQ FAIL\n\0')

    a.resolve()
    return bytes(a.buf)


def main():
    out = Path(__file__).resolve().parent.parent / 'roms' / 'vdptest.sms'
    out.parent.mkdir(parents=True, exist_ok=True)
    rom = build_rom()
    assert len(rom) == ROM_SIZE
    out.write_bytes(rom)
    print(f'wrote {out} ({len(rom)} bytes)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
