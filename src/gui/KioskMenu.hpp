// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#pragma once
// =============================================================================
//  KioskMenu — menu in-game du mode borne (inspiré du kiosk de NeoST, sans
//  ImGui : rendu immediate mode + police bitmap 8×8 embarquée).
//
//  Plein écran, jeu en PAUSE. Deux colonnes basculées gauche/droite :
//   - la liste des jeux (les .sms du dossier de la ROM chargée, hors BIOS) —
//     en choisir un insère la cartouche (= reset machine, comme sur la vraie
//     console ; le BIOS chargé, lui, reste en place et boote le jeu) ;
//   - les actions : Resume · Restart machine · Desktop/Fullscreen · Quit.
//  Navigation : flèches/D-pad (répétition au maintien), FEU/Entrée valide.
//  L'ouverture/fermeture (Start manette, F9, Échap en borne) est gérée par
//  l'appelant ; libellés en ANGLAIS (convention du projet).
// =============================================================================
#include <string>
#include <vector>

class KioskMenu {
public:
    enum class Action {
        None,             // rien à faire cette trame
        Resume,           // fermer le menu, reprendre le jeu
        LoadRom,          // insérer chosenRom() (l'appelant charge + reset)
        Restart,          // reset machine
        ToggleFullscreen, // bureau <-> plein écran
        Quit,             // quitter l'émulateur
    };

    // État BRUT des entrées (niveaux, pas de fronts) : le menu fait sa propre
    // détection de front et la répétition au maintien.
    struct Input {
        bool up = false, down = false, left = false, right = false;
        bool fire = false;   // bouton 1 / A / Entrée : valider
    };

    // Dossier scanné pour la liste des jeux (celui de la ROM chargée).
    void setRomDir(const std::string& dir);

    void open();
    void close() { openFlag = false; }
    bool isOpen() const { return openFlag; }

    // À appeler chaque trame quand le menu est ouvert.
    Action update(const Input& in);
    const std::string& chosenRom() const { return chosen; }

    // Dessine le menu par-dessus la trame courante (projection pixel fbW×fbH).
    // `fullscreen` choisit le libellé Desktop mode / Fullscreen.
    void render(int fbW, int fbH, bool fullscreen);

private:
    struct Entry {
        std::string path;   // chemin complet
        std::string name;   // nom affiché (sans extension)
    };

    void scanRoms();
    void ensureFont();
    void drawText(float x, float y, float scale, const char* text,
                  float r, float g, float b, float a);

    std::string romDir;
    std::vector<Entry> games;
    std::string chosen;

    bool openFlag  = false;
    int  column    = 0;      // 0 = jeux, 1 = actions
    int  gameSel   = 0;
    int  actionSel = 0;
    int  scrollTop = 0;      // premier jeu affiché

    Input prev{};            // pour la détection de fronts
    int   repeatCounter = 0; // répétition au maintien (haut/bas)

    unsigned int fontTex = 0;
};
