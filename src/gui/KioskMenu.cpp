// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

// =============================================================================
//  KioskMenu.cpp — menu borne : scan des ROM, navigation, rendu immediate
//  mode avec l'atlas de la police 8×8 (font8x8, domaine public, vendorisée).
// =============================================================================
#include "KioskMenu.hpp"

#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION 1
#endif
#if defined(__APPLE__)
#  include <OpenGL/gl.h>
#else
#  include <GL/gl.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

#include "font8x8_basic.h"

namespace {

// Libellés d'actions (anglais — convention du projet). L'entrée 2 est
// remplacée dynamiquement selon l'état plein écran.
const char* kActionResume  = "Resume";
const char* kActionRestart = "Restart machine";
const char* kActionQuit    = "Quit";
constexpr int kActionCount = 4;

constexpr int kVisibleGames = 14;   // lignes de jeux affichées

// Répétition au maintien haut/bas : délai initial puis cadence, en trames.
constexpr int kRepeatDelay = 18;
constexpr int kRepeatRate  = 5;

bool endsWithSms(const std::string& n)
{
    if (n.size() < 4) return false;
    std::string ext = n.substr(n.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext == ".sms";
}

bool nameContainsBios(const std::string& n)
{
    std::string low = n;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return low.find("bios") != std::string::npos;
}

} // namespace

void KioskMenu::setRomDir(const std::string& dir)
{
    romDir = dir;
    scanRoms();
}

void KioskMenu::open()
{
    scanRoms();              // le dossier a pu changer entre deux ouvertures
    openFlag = true;
    column   = games.empty() ? 1 : 0;
    prev     = Input{};      // pas de front parasite à l'ouverture
    prev.fire = true;        // avale le bouton qui a ouvert le menu
    repeatCounter = 0;
}

void KioskMenu::scanRoms()
{
    games.clear();
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(romDir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string fname = e.path().filename().string();
        // Les images BIOS sont exclues de la liste : insérées comme cartouche
        // elles afficheraient SOFTWARE ERROR (auto-checksum du BIOS).
        if (!endsWithSms(fname) || nameContainsBios(fname)) continue;
        Entry en;
        en.path = e.path().string();
        en.name = e.path().stem().string();
        games.push_back(std::move(en));
    }
    std::sort(games.begin(), games.end(), [](const Entry& a, const Entry& b) {
        return std::lexicographical_compare(
            a.name.begin(), a.name.end(), b.name.begin(), b.name.end(),
            [](unsigned char x, unsigned char y) {
                return std::tolower(x) < std::tolower(y);
            });
    });
    if (gameSel >= static_cast<int>(games.size()))
        gameSel = games.empty() ? 0 : static_cast<int>(games.size()) - 1;
    if (scrollTop > gameSel) scrollTop = gameSel;
}

KioskMenu::Action KioskMenu::update(const Input& in)
{
    Action out = Action::None;

    // Fronts : appui cette trame et pas la précédente.
    const bool upEdge    = in.up    && !prev.up;
    const bool downEdge  = in.down  && !prev.down;
    const bool leftEdge  = in.left  && !prev.left;
    const bool rightEdge = in.right && !prev.right;
    const bool fireEdge  = in.fire  && !prev.fire;

    // Répétition au maintien pour haut/bas (listes longues).
    bool upStep = upEdge, downStep = downEdge;
    if (in.up || in.down) {
        ++repeatCounter;
        if (repeatCounter >= kRepeatDelay &&
            (repeatCounter - kRepeatDelay) % kRepeatRate == 0) {
            upStep   = upStep   || in.up;
            downStep = downStep || in.down;
        }
    } else {
        repeatCounter = 0;
    }

    if (leftEdge || rightEdge)
        column = (column == 0) ? 1 : 0;
    if (games.empty())
        column = 1;

    if (column == 0) {
        const int n = static_cast<int>(games.size());
        if (downStep && gameSel < n - 1) ++gameSel;
        if (upStep   && gameSel > 0)     --gameSel;
        if (gameSel < scrollTop)                   scrollTop = gameSel;
        if (gameSel >= scrollTop + kVisibleGames)  scrollTop = gameSel - kVisibleGames + 1;
        if (fireEdge && n > 0) {
            chosen = games[static_cast<size_t>(gameSel)].path;
            out = Action::LoadRom;
            openFlag = false;
        }
    } else {
        if (downStep && actionSel < kActionCount - 1) ++actionSel;
        if (upStep   && actionSel > 0)                --actionSel;
        if (fireEdge) {
            switch (actionSel) {
            case 0: out = Action::Resume;           openFlag = false; break;
            case 1: out = Action::Restart;          openFlag = false; break;
            case 2: out = Action::ToggleFullscreen;                   break;
            case 3: out = Action::Quit;                               break;
            }
        }
    }

    prev = in;
    return out;
}

// -----------------------------------------------------------------------------
//  Rendu
// -----------------------------------------------------------------------------
void KioskMenu::ensureFont()
{
    if (fontTex) return;
    // Atlas 16×8 glyphes de 8×8 (ASCII 0-127) : blanc sur transparent.
    // font8x8_basic : bit 0 = pixel le plus à GAUCHE de la ligne.
    constexpr int kCols = 16, kRows = 8;
    unsigned char rgba[kRows * 8][kCols * 8][4] = {};
    for (int c = 0; c < 128; ++c) {
        const int gx = (c % kCols) * 8, gy = (c / kCols) * 8;
        for (int row = 0; row < 8; ++row) {
            const unsigned char bits =
                static_cast<unsigned char>(font8x8_basic[c][row]);
            for (int col = 0; col < 8; ++col) {
                if ((bits >> col) & 1) {
                    unsigned char* px = rgba[gy + row][gx + col];
                    px[0] = px[1] = px[2] = px[3] = 255;
                }
            }
        }
    }
    glGenTextures(1, &fontTex);
    glBindTexture(GL_TEXTURE_2D, fontTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kCols * 8, kRows * 8, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void KioskMenu::drawText(float x, float y, float scale, const char* text,
                         float r, float g, float b, float a)
{
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    float cx = x;
    for (const char* p = text; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p) & 0x7F;
        const float u0 = (c % 16) * 8.0f / 128.0f;
        const float v0 = (c / 16) * 8.0f / 64.0f;
        const float u1 = u0 + 8.0f / 128.0f;
        const float v1 = v0 + 8.0f / 64.0f;
        const float s  = 8.0f * scale;
        glTexCoord2f(u0, v0); glVertex2f(cx,     y);
        glTexCoord2f(u1, v0); glVertex2f(cx + s, y);
        glTexCoord2f(u1, v1); glVertex2f(cx + s, y + s);
        glTexCoord2f(u0, v1); glVertex2f(cx,     y + s);
        cx += s;
    }
    glEnd();
}

void KioskMenu::render(int fbW, int fbH, bool fullscreen)
{
    if (!openFlag) return;
    ensureFont();

    // Projection pixel, origine en HAUT-gauche (y vers le bas).
    glViewport(0, 0, fbW, fbH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, fbW, fbH, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Voile sombre par-dessus le jeu figé.
    glDisable(GL_TEXTURE_2D);
    glColor4f(0.02f, 0.02f, 0.08f, 0.85f);
    glBegin(GL_QUADS);
    glVertex2f(0, 0); glVertex2f(static_cast<float>(fbW), 0);
    glVertex2f(static_cast<float>(fbW), static_cast<float>(fbH));
    glVertex2f(0, static_cast<float>(fbH));
    glEnd();

    // Échelle de police calée sur la hauteur (8 px -> ~1/40 de l'écran).
    const float s   = std::max(2.0f, std::floor(fbH / 320.0f));
    const float lh  = 12.0f * s;                   // interligne
    const float top = fbH * 0.10f;
    const float colGames   = fbW * 0.08f;
    const float colActions = fbW * 0.62f;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, fontTex);

    drawText(colGames, top - 2.0f * lh, s * 1.5f, "SESAME",
             1.0f, 1.0f, 1.0f, 1.0f);
    drawText(colGames + 8.0f * s * 1.5f * 7.0f, top - 2.0f * lh, s,
             "kiosk menu", 0.6f, 0.6f, 0.7f, 1.0f);

    // --- Colonne des jeux ----------------------------------------------------
    const bool gamesActive = (column == 0);
    drawText(colGames, top, s, "GAMES",
             gamesActive ? 1.0f : 0.5f, gamesActive ? 0.9f : 0.5f, 0.3f, 1.0f);
    if (games.empty()) {
        drawText(colGames, top + lh, s, "(no .sms found)",
                 0.6f, 0.6f, 0.6f, 1.0f);
    }
    const int last = std::min(static_cast<int>(games.size()),
                              scrollTop + kVisibleGames);
    for (int i = scrollTop; i < last; ++i) {
        const bool sel = gamesActive && i == gameSel;
        std::string label = games[static_cast<size_t>(i)].name;
        if (label.size() > 30) label = label.substr(0, 29) + "~";
        const float y = top + lh * (1.0f + static_cast<float>(i - scrollTop));
        if (sel)
            drawText(colGames - 12.0f * s, y, s, ">", 1.0f, 1.0f, 0.4f, 1.0f);
        drawText(colGames, y, s, label.c_str(),
                 sel ? 1.0f : 0.75f, sel ? 1.0f : 0.75f,
                 sel ? 0.6f : 0.80f, gamesActive ? 1.0f : 0.6f);
    }
    if (scrollTop > 0)
        drawText(colGames, top + lh * 0.35f, s * 0.8f, "^^^",
                 0.6f, 0.6f, 0.7f, 1.0f);
    if (last < static_cast<int>(games.size()))
        drawText(colGames, top + lh * (kVisibleGames + 1.2f), s * 0.8f, "vvv",
                 0.6f, 0.6f, 0.7f, 1.0f);

    // --- Colonne des actions -------------------------------------------------
    const bool actionsActive = (column == 1);
    drawText(colActions, top, s, "ACTIONS",
             actionsActive ? 1.0f : 0.5f, actionsActive ? 0.9f : 0.5f,
             0.3f, 1.0f);
    const char* actions[kActionCount] = {
        kActionResume, kActionRestart,
        fullscreen ? "Desktop mode" : "Fullscreen", kActionQuit,
    };
    for (int i = 0; i < kActionCount; ++i) {
        const bool sel = actionsActive && i == actionSel;
        const float y = top + lh * (1.0f + static_cast<float>(i));
        if (sel)
            drawText(colActions - 12.0f * s, y, s, ">", 1.0f, 1.0f, 0.4f, 1.0f);
        drawText(colActions, y, s, actions[i],
                 sel ? 1.0f : 0.75f, sel ? 1.0f : 0.75f,
                 sel ? 0.6f : 0.80f, actionsActive ? 1.0f : 0.6f);
    }

    // Aide en pied d'écran.
    drawText(colGames, fbH - 2.0f * lh, s * 0.8f,
             "arrows: move   left/right: column   fire: select   F9: close",
             0.55f, 0.55f, 0.65f, 1.0f);

    glDisable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);   // ne pas teinter le quad du jeu
}
