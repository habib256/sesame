// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#pragma once
// =============================================================================
//  Config — fichier de configuration « sesame.cfg » du frontend fenêtré.
//  Format INI minimal : `clef = valeur`, commentaires « # », booléens
//  true/false (ou 1/0, on/off). Clefs et commentaires générés en ANGLAIS
//  (convention du projet : interface en anglais).
//
//  Cycle de vie : chargé au lancement (--config FILE, sinon ./sesame.cfg,
//  sinon à côté de l'exécutable) ; les options de ligne de commande PRIMENT
//  sur le fichier ; l'état effectif est RÉÉCRIT à la sortie propre — la
//  configuration se souvient donc des réglages (CRT, plein écran…) d'un
//  lancement à l'autre. Le headless n'utilise PAS ce fichier (déterminisme).
// =============================================================================
#include "CrtParams.hpp"

#include <string>

struct Config {
    // --- Machine ------------------------------------------------------------
    bool pal = false;             // pal
    bool gameGear = false;        // game_gear (matériel GG forcé, cf. --gg)
    std::string bios;             // bios : chemin, vide = auto, "none" = aucun
    // --- Affichage ----------------------------------------------------------
    bool crt = true;              // crt
    bool fullscreen = false;      // fullscreen
    bool kiosk = false;           // kiosk
    int  kioskMonitor = 0;        // kiosk_monitor
    bool noSpriteLimit = false;   // no_sprite_limit (option pédagogique)
    bool lightPhaser = false;     // light_phaser : souris = pistolet (port A)
    std::string romDir;           // rom_dir : dossier du menu borne, vide = auto
    CrtParams crtParams;          // crt_* (voir save() pour la liste)
    // Rendu « dalle LCD » appliqué automatiquement en Game Gear à la place
    // du CRT : rémanence marquée (ghosting de la dalle d'origine) + grille
    // de pixels (masque à points), sans scanlines ni baril.
    float lcdPersistence = 0.65f; // lcd_persistence (0..0.98)
    float lcdGridStrength = 0.35f;// lcd_grid_strength (0..1)

    // Chemin du fichier chargé — ou cible de la sauvegarde s'il n'existait
    // pas encore (défaut : ./sesame.cfg).
    std::string path = "sesame.cfg";

    // Charge `p` s'il existe (retourne false sinon, sans toucher aux
    // valeurs). Les clefs inconnues sont ignorées avec un avertissement.
    bool load(const std::string& p);

    // Réécrit l'état complet dans `path` (avec un en-tête de commentaires).
    bool save() const;

    // Résout puis charge le fichier de configuration : chemin explicite
    // (--config), sinon ./sesame.cfg, sinon à côté de l'exécutable.
    static Config locate(const char* explicitPath, const char* argv0);
};
