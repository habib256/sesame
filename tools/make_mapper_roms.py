#!/usr/bin/env python3
# Copyright (C) 2026 VERHILLE Arnaud
# SPDX-License-Identifier: GPL-3.0-or-later
# Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
# (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

# =============================================================================
#  make_mapper_roms.py — génère les ROM de test des mappers exotiques :
#
#   roms/cmtest.sms (64 Ko, mapper CODEMASTERS — en-tête 0x7FE0 valide) :
#     CM1 : page 2 mappée en fenêtre 1 (écriture à 0x4000), signature lue ;
#     CM2 : page 3 mappée en fenêtre 2 (écriture à 0x8000), signature lue ;
#     CM3 : RAM Ernie Els (bit 7 de 0x4000) : écriture/relecture à 0xA123 ;
#     CM4 : RAM démappée : 0xA123 relit la ROM, pas la valeur écrite.
#
#   roms/krtest.sms (64 Ko, mapper COREEN — détection heuristique) :
#     KR1 : écriture de 3 à 0xA000 (aucune écriture 0xFFFD-0xFFFF avant) ->
#           bascule sur le mapper coréen, page 3 en fenêtre 2, signature lue.
#
#   roms/jgtest.sms (64 Ko, mapper JANGGUN — heuristique 0x6000) :
#     JG1 : page 8 Ko n°3 en 0x6000-0x7FFF, signature lue ;
#     JG2 : page 8 Ko n°5 MIROIR (bit 6) en 0xA000-0xBFFF : l'octet lu est
#           à bits inversés ;
#     JG3 : 0xFFFF repose la paire 16 Ko (pages 8 Ko 6/7), signature lue.
#
#   roms/eetest.sms (32 Ko, EEPROM 93C46 — lancer avec --eeprom) :
#     EE1 : EWEN + WRITE 0xBEEF au mot 5, puis READ : relecture exacte.
#
#  Chaque page N porte une signature 0xC0+N à l'offset 0x100 de la page.
#  Verdicts imprimés sur la console SDSC (« ... OK » / « ... FAIL »).
# =============================================================================
import sys
from pathlib import Path

from smsasm import Asm, print_str, emit_print_routine

ROM_SIZE = 0x10000   # 64 Ko = 4 pages de 16 Ko


def check_byte(a, addr, expected, tag):
    """LD A,(addr) ; CP expected ; imprime OK/FAIL."""
    a.ld_a_mem(addr)
    a.cp_n(expected)
    a.jr_nz(f'{tag}_fail')
    print_str(a, f'str_{tag}_ok')
    a.jr(f'{tag}_done')
    a.label(f'{tag}_fail')
    print_str(a, f'str_{tag}_fail')
    a.label(f'{tag}_done')


def emit_strings(a, tags):
    for t in tags:
        a.label(f'str_{t}_ok');   a.db(f'{t.upper()} OK\n\0'.encode())
        a.label(f'str_{t}_fail'); a.db(f'{t.upper()} FAIL\n\0'.encode())


def out_val(a, port_addr, val):
    """LD A,val ; LD (addr),A — écriture mémoire (registre de mapper)."""
    a.ld_a_n(val)
    a.ld_mem_a(port_addr)


def build_cm():
    a = Asm(ROM_SIZE)
    a.di()
    a.jp('main')
    a.org(0x0066)
    a.retn()

    a.org(0x0180)
    a.label('main')
    a.ld_sp_nn(0xDFF0)
    print_str(a, 'str_banner')

    # CM1 : page 2 en fenêtre 1 -> signature 0xC2 à 0x4100.
    out_val(a, 0x4000, 2)
    check_byte(a, 0x4100, 0xC2, 'cm1')

    # CM2 : page 3 en fenêtre 2 -> signature 0xC3 à 0x8100.
    out_val(a, 0x8000, 3)
    check_byte(a, 0x8100, 0xC3, 'cm2')

    # CM3 : RAM Ernie Els (bit 7 de 0x4000) : écrire puis relire 0xA123.
    out_val(a, 0x4000, 0x80 | 2)     # RAM ON, fenêtre 1 -> page 2
    a.ld_a_n(0x5A)
    a.ld_mem_a(0xA123)
    check_byte(a, 0xA123, 0x5A, 'cm3')

    # CM4 : RAM démappée -> 0xA123 relit la ROM (page 3 : 0xB1 à l'offset
    # 0x2123 de la page, posé par le générateur).
    out_val(a, 0x4000, 2)            # RAM OFF
    check_byte(a, 0xA123, 0xB1, 'cm4')

    a.label('halt')
    a.jr('halt')

    emit_print_routine(a)
    a.label('str_banner'); a.db(b'SESAME CMTEST\n\0')
    emit_strings(a, ['cm1', 'cm2', 'cm3', 'cm4'])
    a.resolve()

    rom = bytearray(a.buf)
    # Signatures de page + octet ROM vu par CM4 (page 3, offset 0x2123).
    for page in range(4):
        rom[page * 0x4000 + 0x100] = 0xC0 + page
    rom[3 * 0x4000 + 0x2123] = 0xB1
    # En-tête Codemasters à 0x7FE0 : nombre de banques + somme de contrôle
    # et son complément à 0x10000 (valeurs arbitraires cohérentes).
    rom[0x7FE0] = ROM_SIZE // 0x4000     # 4 banques
    checksum = 0x1234
    rom[0x7FE6] = checksum & 0xFF
    rom[0x7FE7] = checksum >> 8
    inv = (0x10000 - checksum) & 0xFFFF
    rom[0x7FE8] = inv & 0xFF
    rom[0x7FE9] = inv >> 8
    return bytes(rom)


def build_kr():
    a = Asm(ROM_SIZE)
    a.di()
    a.jp('main')
    a.org(0x0066)
    a.retn()

    a.org(0x0180)
    a.label('main')
    a.ld_sp_nn(0xDFF0)
    print_str(a, 'str_banner')

    # KR1 : écriture à 0xA000 sans avoir touché 0xFFFD-0xFFFF -> mapper
    # coréen, page 3 en fenêtre 2 -> signature 0xC3 à 0x8100.
    out_val(a, 0xA000, 3)
    check_byte(a, 0x8100, 0xC3, 'kr1')

    a.label('halt')
    a.jr('halt')

    emit_print_routine(a)
    a.label('str_banner'); a.db(b'SESAME KRTEST\n\0')
    emit_strings(a, ['kr1'])
    a.resolve()

    rom = bytearray(a.buf)
    for page in range(4):
        rom[page * 0x4000 + 0x100] = 0xC0 + page
    return bytes(rom)


def build_jg():
    a = Asm(ROM_SIZE)
    a.di()
    a.jp('main')
    a.org(0x0066)
    a.retn()

    a.org(0x0180)
    a.label('main')
    a.ld_sp_nn(0xDFF0)
    print_str(a, 'str_banner')

    # JG1 : page 8 Ko n°3 en fenêtre 0x6000 (bascule heuristique Janggun).
    out_val(a, 0x6000, 3)
    check_byte(a, 0x6110, 0xD3, 'jg1')

    # JG2 : page 8 Ko n°5, bit 6 levé -> lecture à octets MIROIRS.
    out_val(a, 0xA000, 0x40 | 5)
    check_byte(a, 0xA110, 0xAB, 'jg2')   # bitrev(0xD5 = 11010101) = 0xAB

    # JG3 : 0xFFFF repose la paire 16 Ko n°3 (pages 8 Ko 6 et 7).
    out_val(a, 0xFFFF, 3)
    check_byte(a, 0x8110, 0xD6, 'jg3')

    a.label('halt')
    a.jr('halt')

    emit_print_routine(a)
    a.label('str_banner'); a.db(b'SESAME JGTEST\n\0')
    emit_strings(a, ['jg1', 'jg2', 'jg3'])
    a.resolve()

    rom = bytearray(a.buf)
    # Signatures par page de 8 Ko : 0xD0+n à l'offset 0x110.
    for page in range(8):
        rom[page * 0x2000 + 0x110] = 0xD0 + page
    return bytes(rom)


# Bit-banging Microwire : CS=bit2, CLK=bit1, DI=bit0, à l'adresse 0x8000.
def ee_bit(a, bit):
    """Envoie un bit : DI posé, puis front montant d'horloge (CS haut)."""
    a.ld_a_n(0x04 | bit)      # CS=1, CLK=0, DI=bit
    a.ld_mem_a(0x8000)
    a.ld_a_n(0x06 | bit)      # CS=1, CLK=1 (front montant)
    a.ld_mem_a(0x8000)


def ee_cmd(a, bits):
    """Envoie une suite de bits (start + opcode + adresse [+ données])."""
    for b in bits:
        ee_bit(a, b)


def ee_cs_low(a):
    a.ld_a_n(0x00)
    a.ld_mem_a(0x8000)


def bits_of(value, count):
    return [(value >> (count - 1 - i)) & 1 for i in range(count)]


def build_ee():
    a = Asm(0x8000)
    a.di()
    a.jp('main')
    a.org(0x0066)
    a.retn()

    a.org(0x0180)
    a.label('main')
    a.ld_sp_nn(0xDFF0)
    print_str(a, 'str_banner')

    # EWEN : start + 00 + 11xxxx.
    ee_cmd(a, [1, 0, 0, 1, 1, 0, 0, 0, 0])
    ee_cs_low(a)
    # WRITE mot 5 = 0xBEEF : start + 01 + 000101 + 16 bits.
    ee_cmd(a, [1, 0, 1] + bits_of(5, 6) + bits_of(0xBEEF, 16))
    ee_cs_low(a)
    # READ mot 5 : start + 10 + 000101, puis 17 horloges (0 factice + 16).
    ee_cmd(a, [1, 1, 0] + bits_of(5, 6))
    a.ld_hl_imm(0x0000)
    a.ld_b_n(16)
    a.label('rdloop')
    ee_bit(a, 0)              # front d'horloge (DI indifférent)
    a.add_hl_hl()             # HL <<= 1
    a.ld_a_mem(0x8000)        # DO en bit 0
    a.and_n(0x01)
    a.or_l()
    a.ld_l_a()                # L |= bit
    a.djnz('rdloop')
    ee_cs_low(a)

    a.ld_a_h()
    a.cp_n(0xBE)
    a.jr_nz('ee1_fail')
    a.ld_a_l()
    a.cp_n(0xEF)
    a.jr_nz('ee1_fail')
    print_str(a, 'str_ee1_ok')
    a.jr('ee1_done')
    a.label('ee1_fail')
    print_str(a, 'str_ee1_fail')
    a.label('ee1_done')

    a.label('halt')
    a.jr('halt')

    emit_print_routine(a)
    a.label('str_banner'); a.db(b'SESAME EETEST\n\0')
    emit_strings(a, ['ee1'])
    a.resolve()
    return bytes(a.buf)


def main():
    roms = Path(__file__).resolve().parent.parent / 'roms'
    roms.mkdir(parents=True, exist_ok=True)
    for name, rom in (('cmtest.sms', build_cm()), ('krtest.sms', build_kr()),
                      ('jgtest.sms', build_jg()), ('eetest.sms', build_ee())):
        (roms / name).write_bytes(rom)
        print(f'wrote {roms / name} ({len(rom)} bytes)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
