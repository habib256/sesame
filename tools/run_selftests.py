#!/usr/bin/env python3
# Copyright (C) 2026 VERHILLE Arnaud
# SPDX-License-Identifier: GPL-3.0-or-later
# Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
# (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

# =============================================================================
#  run_selftests.py — suite de validation de Sesame.
#
#  1. Régénère roms/selftest.sms via make_test_rom.py.
#  2. Lance ./build/sesame-headless dessus (60 trames, capture PPM, SDSC).
#  3. Vérifie : code retour, sortie SDSC ("SESAME SELFTEST", aucun "FAIL"),
#     capture PPM 256x192 avec au moins 4 couleurs distinctes, non uniforme.
#
#  Sortie en anglais (convention du projet), code retour global 0/1.
# =============================================================================
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADLESS = ROOT / 'build' / 'sesame-headless'
ROM = ROOT / 'roms' / 'selftest.sms'
SCREENSHOT = Path('/tmp/sesame_selftest.ppm')
FRAMES = 60

results = []  # liste de (nom, ok, détail)


def check(name, ok, detail=''):
    results.append((name, ok, detail))
    status = 'PASS' if ok else 'FAIL'
    print(f'[{status}] {name}' + (f' — {detail}' if detail else ''))
    return ok


def parse_ppm(data):
    """Parse minimal des PPM P6 (binaire) et P3 (ASCII).
    Retourne (largeur, hauteur, liste_de_pixels_RGB)."""
    # L'en-tête est en ASCII : magic, largeur, hauteur, maxval, avec
    # d'éventuels commentaires '#'. On lit token par token.
    tokens = []
    i = 0
    while len(tokens) < 4 and i < len(data):
        c = data[i:i + 1]
        if c == b'#':                       # commentaire : sauter la ligne
            while i < len(data) and data[i:i + 1] != b'\n':
                i += 1
        elif c.isspace():
            i += 1
        else:
            j = i
            while j < len(data) and not data[j:j + 1].isspace():
                j += 1
            tokens.append(data[i:j])
            i = j
    if len(tokens) < 4:
        raise ValueError('truncated PPM header')
    magic = tokens[0].decode()
    width, height, maxval = int(tokens[1]), int(tokens[2]), int(tokens[3])
    if magic == 'P6':
        i += 1                              # exactement un blanc après maxval
        raw = data[i:i + width * height * 3]
        if len(raw) < width * height * 3:
            raise ValueError('truncated PPM pixel data')
        pixels = [tuple(raw[k:k + 3]) for k in range(0, len(raw), 3)]
    elif magic == 'P3':
        vals = data[i:].split()
        if len(vals) < width * height * 3:
            raise ValueError('truncated PPM pixel data')
        nums = [int(v) for v in vals[:width * height * 3]]
        pixels = [tuple(nums[k:k + 3]) for k in range(0, len(nums), 3)]
    else:
        raise ValueError(f'unsupported PPM magic {magic!r}')
    return width, height, pixels


def main():
    # --- 1. Régénérer la ROM de test ----------------------------------------
    gen = subprocess.run(
        [sys.executable, str(ROOT / 'tools' / 'make_test_rom.py')],
        capture_output=True, text=True)
    if not check('generate selftest ROM', gen.returncode == 0,
                 gen.stderr.strip() or gen.stdout.strip()):
        return 1
    if not check('ROM file exists', ROM.is_file(), str(ROM)):
        return 1

    # --- 2. Lancer l'émulateur headless -------------------------------------
    if not HEADLESS.is_file():
        check('headless binary exists', False,
              f'{HEADLESS} not found — build first: '
              f'cmake -B build && cmake --build build')
        return 1

    SCREENSHOT.unlink(missing_ok=True)
    try:
        run = subprocess.run(
            [str(HEADLESS), str(ROM), '--frames', str(FRAMES),
             '--screenshot', str(SCREENSHOT), '--sdsc'],
            capture_output=True, text=True, timeout=120, cwd=ROOT)
    except subprocess.TimeoutExpired:
        check('emulator run', False, 'timed out after 120 s')
        return 1

    stdout = run.stdout
    check('emulator exit code is 0', run.returncode == 0,
          f'got {run.returncode}' + (f'; stderr: {run.stderr.strip()}'
                                     if run.returncode else ''))

    # --- 3. Vérifier la sortie SDSC -----------------------------------------
    check('SDSC banner "SESAME SELFTEST" present', 'SESAME SELFTEST' in stdout)
    check('no "FAIL" in SDSC output', 'FAIL' not in stdout,
          '' if 'FAIL' not in stdout else
          '; '.join(l for l in stdout.splitlines() if 'FAIL' in l))

    # --- 4. Vérifier la capture d'écran -------------------------------------
    if check('screenshot file exists', SCREENSHOT.is_file(), str(SCREENSHOT)):
        try:
            w, h, pixels = parse_ppm(SCREENSHOT.read_bytes())
            check('screenshot is 256x192', (w, h) == (256, 192),
                  f'got {w}x{h}')
            colors = set(pixels)
            check('screenshot has at least 4 distinct colors',
                  len(colors) >= 4, f'found {len(colors)}')
            check('screenshot is not uniform', len(colors) > 1)
        except ValueError as e:
            check('screenshot is a valid PPM', False, str(e))

    # --- Verdict global ------------------------------------------------------
    failed = [name for name, ok, _ in results if not ok]
    print()
    if failed:
        print(f'FAILED: {len(failed)}/{len(results)} checks failed '
              f'({", ".join(failed)})')
        return 1
    print(f'SUCCESS: all {len(results)} checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
