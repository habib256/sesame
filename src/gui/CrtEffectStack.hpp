// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#pragma once
// =============================================================================
//  CrtEffectStack — pile d'effets CRT (portée de NeoST/POM2, même auteur).
//
//  Prend une texture RGBA déjà rendue (ici l'écran SMS) et applique par-dessus,
//  en une passe FBO, la façade « verre » d'un moniteur CRT : géométrie de
//  baril, luminosité/contraste/saturation, teinte, courbe phosphore,
//  scanlines, shadow mask, vignette, luminance gain et rémanence phosphore.
//  Rend à la résolution de sortie pour que scanlines/masque soient
//  anti-aliasés analytiquement (fwidth) sans moiré.
//
//  Sûreté : opt-in. Si le shader ne compile pas ou que les points d'entrée GL
//  manquent, available() reste false et process() est un no-op (renvoie 0) →
//  l'appelant présente la texture brute.
// =============================================================================
#include <string>

#include "CrtParams.hpp"

class CrtEffectStack {
public:
    CrtEffectStack();
    ~CrtEffectStack();
    CrtEffectStack(const CrtEffectStack&) = delete;
    CrtEffectStack& operator=(const CrtEffectStack&) = delete;

    // Compile le shader + alloue le VBO (et un VAO si le contexte le permet).
    // texture/FBO est paresseuse (au 1er process(), on a besoin des dims).
    // Renvoie true en cas de succès ; sur échec GL available() reste false et
    // process() devient un no-op (renvoie 0).
    bool initialize();
    bool available() const { return ready; }

    void setParams(const CrtParams& p) { params = p; }
    const CrtParams& getParams() const { return params; }

    // Applique les couches d'effets à la texture RGBA `srcTex` (taille logique
    // srcW × srcH — pilote la fréquence scanline/masque), et rend le résultat
    // à la taille écran dstW × dstH. Renvoie un nom de texture GL (dstW×dstH),
    // ou 0 si !available().
    unsigned int process(unsigned int srcTex, int srcW, int srcH,
                         int dstW, int dstH);

    const std::string& lastError() const { return errorMsg; }

private:
    bool        ready       = false;
    bool        initialized = false;
    std::string errorMsg;

    unsigned int program      = 0;
    unsigned int outputTex[2] = {0, 0};   // ping-pong pour la persistance
    unsigned int fbo[2]       = {0, 0};
    unsigned int vao          = 0;
    unsigned int vbo          = 0;
    bool         useVertexArray = true;

    int uSrc         = -1;
    int uPrevFrame   = -1;
    int uSrcSize     = -1;
    int uOutSize     = -1;
    int uBrightness  = -1;
    int uContrast    = -1;
    int uSaturation  = -1;
    int uHue         = -1;
    int uSharpness   = -1;
    int uPersistence = -1;
    int uScanlines   = -1;
    int uBarrel      = -1;
    int uShadowMask  = -1;
    int uShadowStr   = -1;
    int uLuminanceGain = -1;
    int uCenterLighting = -1;
    int uPhosphorGamma = -1;

    int  outW = 0, outH = 0;
    // Dernière taille de FBO REFUSÉE par le pilote : on ne retente pas à
    // taille identique (spam GL), mais un changement de taille redonne sa
    // chance à l'effet — l'échec n'est pas définitif pour la session.
    int  failedW = -1, failedH = -1;
    int  pingPongIdx = 0;
    bool firstFrame  = true;

    CrtParams params{};

    bool createTextures(int w, int h);
};
