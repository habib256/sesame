// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#pragma once
// =============================================================================
//  OpenGLShader — helper de compilation/link de shader GLSL minimal.
//  Porté de NeoST (même auteur). Utilisé par CrtEffectStack pour construire la
//  passe d'effets CRT sans tirer un framework de shader complet.
// =============================================================================
#include <string>

// Compile + linke un programme (vertex, fragment) unique. Renvoie l'objet
// programme GL en cas de succès, 0 en cas d'échec. La ligne #version GLSL est
// préfixée automatiquement — ne passer que le corps de chaque shader. Le
// dialecte est choisi à l'exécution d'après GL_SHADING_LANGUAGE_VERSION puis
// essayé en cascade 150 -> 140 -> 130 -> 120 (« 300 es » sur contexte GLES).
// Les erreurs de compilation/link sont écrites dans `errorOut`.
unsigned int compileShaderProgram(const char* vertexBody,
                                  const char* fragmentBody,
                                  std::string* errorOut = nullptr);
