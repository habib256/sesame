#!/usr/bin/env python3
# Copyright (C) 2026 VERHILLE Arnaud
# SPDX-License-Identifier: GPL-3.0-or-later
# Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
# (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

# =============================================================================
#  compare_ppm.py — compare deux images PPM (P6) avec tolérance.
#
#  usage: compare_ppm.py ref.ppm test.ppm [--tolerance T] [--max-diff N]
#
#  Un pixel « diffère » si l'écart d'un de ses canaux dépasse T (défaut 0).
#  La comparaison réussit si au plus N pixels diffèrent (défaut 0).
#  Sortie : statistiques + code retour 0 (identiques sous tolérance) / 1.
#  L'émulation étant déterministe, la tolérance par défaut est ZÉRO : la
#  marge n'existe que pour d'éventuels étalons pris sur un autre backend.
# =============================================================================
import argparse
import sys


def read_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    # En-tête ASCII : magic, largeur, hauteur, maxval (commentaires ignorés).
    tokens, i = [], 0
    while len(tokens) < 4:
        if data[i:i + 1] == b'#':
            i = data.index(b'\n', i) + 1
            continue
        j = i
        while data[j:j + 1] not in (b' ', b'\t', b'\n', b'\r'):
            j += 1
        if j > i:
            tokens.append(data[i:j])
        i = j + 1
    if tokens[0] != b'P6':
        raise ValueError(f'{path}: not a P6 PPM')
    w, h = int(tokens[1]), int(tokens[2])
    px = data[i:i + w * h * 3]
    if len(px) != w * h * 3:
        raise ValueError(f'{path}: truncated pixel data')
    return w, h, px


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ref')
    ap.add_argument('test')
    ap.add_argument('--tolerance', type=int, default=0,
                    help='écart par canal toléré (défaut 0)')
    ap.add_argument('--max-diff', type=int, default=0,
                    help='nombre de pixels différents tolérés (défaut 0)')
    args = ap.parse_args()

    rw, rh, rpx = read_ppm(args.ref)
    tw, th, tpx = read_ppm(args.test)
    if (rw, rh) != (tw, th):
        print(f'FAIL: size mismatch ({rw}x{rh} vs {tw}x{th})')
        return 1

    diff, worst = 0, 0
    for p in range(rw * rh):
        d = max(abs(rpx[3 * p + c] - tpx[3 * p + c]) for c in range(3))
        if d > args.tolerance:
            diff += 1
        if d > worst:
            worst = d
    ok = diff <= args.max_diff
    print(f'{"OK" if ok else "FAIL"}: {diff} differing pixels '
          f'(tolerance {args.tolerance}, worst channel delta {worst})')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
