#!/usr/bin/env python3
# Copyright (C) 2026 VERHILLE Arnaud
# SPDX-License-Identifier: GPL-3.0-or-later
# Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
# (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.
"""Génère src/core/PsgBlipTable.inc — table de « marches à bande limitée ».

Principe (synthèse band-limited à la Blargg / blip_buf, réimplémentation
maison) : une onde carrée idéale contient des harmoniques au-delà de la
fréquence de Nyquist de la sortie (22,05 kHz à 44,1 kHz). Échantillonner
naïvement ses fronts replie ces harmoniques dans le spectre audible
(aliasing). La parade : remplacer chaque front instantané par la réponse
indicielle d'un filtre passe-bas idéal fenêtré (intégrale d'un sinus
cardinal), étalée sur quelques échantillons de sortie.

La table contient cette marche sous forme de DELTAS (différences entre
échantillons successifs), pré-calculée pour kPhases positions
sous-échantillon du front. À l'exécution, chaque transition du PSG ajoute
`delta_amplitude x ligne[phase]` dans un tampon ; l'intégrale de ce tampon
reconstruit le signal filtré. Chaque ligne somme EXACTEMENT à kScale pour
que l'intégrale retombe juste sur la nouvelle amplitude (aucune dérive).

Sortie commitée dans le dépôt : la génération n'est PAS une étape du build
(déterminisme et absence de dépendance Python pour compiler).
"""

import math
import os

KPHASES = 64        # positions sous-échantillon du front
KTAPS = 16          # étalement de la marche, en échantillons de sortie
KHALF = KTAPS // 2  # demi-largeur : le front est centré au milieu
KSCALE = 1 << 14    # point fixe : une ligne somme à 2^14
FC = 0.45           # coupure en fraction du taux d'échantillonnage
                    # (~19,8 kHz à 44,1 kHz : sous Nyquist, marge de garde)
OVERSAMPLE = 4096   # pas d'intégration numérique par échantillon

OUT = os.path.join(os.path.dirname(__file__), os.pardir,
                   "src", "core", "PsgBlipTable.inc")


def impulse(t: float) -> float:
    """Réponse impulsionnelle : sinc passe-bas fenêtré (Blackman)."""
    if abs(t) >= KHALF:
        return 0.0
    # sinc normalisé du passe-bas idéal de coupure FC
    x = 2.0 * FC * t
    s = 2.0 * FC if x == 0.0 else 2.0 * FC * math.sin(math.pi * x) / (math.pi * x)
    # fenêtre de Blackman sur [-KHALF, +KHALF]
    w = (0.42 + 0.5 * math.cos(math.pi * t / KHALF)
         + 0.08 * math.cos(2.0 * math.pi * t / KHALF))
    return s * w


def step_deltas(phase: int) -> list[int]:
    """Ligne de la table : deltas de la marche pour un front à `phase`."""
    center = phase / KPHASES  # position du front dans [0, 1)
    # Intégration numérique (point milieu) de l'impulsionnelle sur chaque
    # intervalle [t, t+1) de la grille de sortie, front centré en
    # KHALF + center.
    raw = []
    for t in range(KTAPS):
        a = t - KHALF - center
        acc = 0.0
        for k in range(OVERSAMPLE):
            acc += impulse(a + (k + 0.5) / OVERSAMPLE)
        raw.append(acc / OVERSAMPLE)
    total = sum(raw)
    # Quantification par cumul arrondi : garantit sum(ligne) == KSCALE.
    deltas, cum, prev = [], 0.0, 0
    for t in range(KTAPS):
        cum += raw[t]
        q = round(KSCALE * cum / total)
        deltas.append(q - prev)
        prev = q
    assert sum(deltas) == KSCALE
    return deltas


def main() -> None:
    rows = [step_deltas(p) for p in range(KPHASES)]
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(
            "// Généré par tools/make_blip_table.py — NE PAS ÉDITER À LA MAIN.\n"
            "// Marche à bande limitée (deltas), voir l'en-tête du générateur.\n"
            f"// {KPHASES} phases x {KTAPS} coefficients ; chaque ligne somme "
            f"à {KSCALE}.\n"
            f"static constexpr int kBlipPhases = {KPHASES};\n"
            f"static constexpr int kBlipTaps   = {KTAPS};\n"
            f"static constexpr int kBlipScaleBits = 14;  // lignes sommées à "
            "1 << 14\n"
            f"static constexpr s16 kBlipStep[{KPHASES}][{KTAPS}] = {{\n")
        for row in rows:
            f.write("    {" + ", ".join(f"{v:6d}" for v in row) + "},\n")
        f.write("};\n")
    print(f"OK: {os.path.normpath(OUT)}")


if __name__ == "__main__":
    main()
