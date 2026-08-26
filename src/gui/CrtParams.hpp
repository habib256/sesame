// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#pragma once
// =============================================================================
//  CrtParams — paramètres de la passe d'effets CRT (façade « verre » du
//  moniteur). Porté de NeoST (lui-même porté de POM2, même auteur).
//
//  Défauts = « neutre visible » : géométrie plate, BCS identité, scanlines et
//  baril légers — le rendu type téléviseur des années Master System.
// =============================================================================

struct CrtParams {
    float brightness  = 0.0f;   // -0.5..+0.5 ajouté à la luma
    float contrast    = 1.0f;   //  0.5..1.5 autour de 0.5
    float saturation  = 1.0f;   //  0..2 multiplicateur chroma
    float hue         = 0.0f;   // -0.5..+0.5 rotation chroma (±π)

    // Sharpness : 0.5 = neutre (passthrough) ; >0.5 accentue (unsharp mask
    // contre un flou 4-taps), <0.5 adoucit.
    float sharpness   = 0.5f;

    // Rémanence phosphore : 0 = aucune, 0.98 = quasi infinie (facteur de
    // rétention par trame).
    float persistence = 0.4f;

    // Post-effets purs.
    float scanlines   = 0.25f;  // 0 = off, 1 = noir entre chaque ligne
    float barrel      = 0.05f;  // 0 = plat, 0.2 = vieux CRT bombé

    enum class ShadowMask : int {
        Off      = 0,
        Triad    = 1,   // triade 3 bandes
        Aperture = 2,   // grille d'ouverture (Trinitron)
        Dot      = 3,   // masque à points (triades décalées)
    };
    ShadowMask shadowMask         = ShadowMask::Off;
    float      shadowMaskStrength = 0.5f;  // 0..1

    // Re-brillance post-verre : compense l'assombrissement scanlines+masque.
    float luminanceGain = 1.0f;  // 1.0..2.0

    // Vignette / center-lighting : 1.0 = plat (off), plus bas assombrit les bords.
    float centerLighting = 1.0f; // 0.5..1.0

    // Courbe de réponse phosphore (gamma CRT) : 1.0 = plat, >1 creuse les ombres.
    float phosphorGamma = 1.0f;  // 0.6..2.6
};
