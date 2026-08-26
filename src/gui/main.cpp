// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

// =============================================================================
//  sesame — frontend fenêtré : GLFW3 + OpenGL immediate mode.
//  Une itération de boucle = une trame émulée, téléversée dans une texture
//  256×192 puis dessinée en quad 4:3 (letterbox). Le vsync 60 Hz cadence tout.
//
//  Usage : sesame <rom.sms> [--bios fichier]
//  Un fichier dont le nom contient « BIOS » est chargé dans le slot BIOS.
// =============================================================================

// CMake définit GL_SILENCE_DEPRECATION sur macOS ; la garde évite de dépendre
// du système de build pour une simple vérification de syntaxe.
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION 1
#endif

#include "core/Machine.hpp"

#include "AudioOut.hpp"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

// Nom de fichier sans son chemin, pour le titre de la fenêtre.
std::string baseName(const std::string& path)
{
    const std::string::size_type slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

// Détecte une image BIOS par son nom de fichier : le nom de base contient
// « BIOS » (insensible à la casse) — même heuristique que sesame-headless.
bool looksLikeBios(const char* path)
{
    const char* base = std::strrchr(path, '/');
    base = base ? base + 1 : path;
    for (const char* p = base; *p; ++p) {
        if ((p[0] == 'B' || p[0] == 'b') && (p[1] == 'I' || p[1] == 'i') &&
            (p[2] == 'O' || p[2] == 'o') && (p[3] == 'S' || p[3] == 's'))
            return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
//  Lecture du clavier -> masque de boutons de la manette 1.
//  Flèches = directions, Z ou W = bouton 1 (couvre QWERTY et AZERTY), X = 2.
// -----------------------------------------------------------------------------
u8 readPadMask(GLFWwindow* win)
{
    u8 mask = 0;
    if (glfwGetKey(win, GLFW_KEY_UP)    == GLFW_PRESS) mask |= Io::Up;
    if (glfwGetKey(win, GLFW_KEY_DOWN)  == GLFW_PRESS) mask |= Io::Down;
    if (glfwGetKey(win, GLFW_KEY_LEFT)  == GLFW_PRESS) mask |= Io::Left;
    if (glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS) mask |= Io::Right;
    if (glfwGetKey(win, GLFW_KEY_Z) == GLFW_PRESS ||
        glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS)     mask |= Io::B1;
    if (glfwGetKey(win, GLFW_KEY_X) == GLFW_PRESS)     mask |= Io::B2;
    return mask;
}

// -----------------------------------------------------------------------------
//  Lecture d'une manette USB -> masque de boutons SMS + état du bouton Start.
//  API « gamepad » de GLFW (mappings SDL intégrés) : D-pad OU stick gauche =
//  directions, A/X = bouton 1, B/Y = bouton 2, Start = bouton Pause de la
//  console. Retourne 0 si aucune manette reconnue sur ce slot.
// -----------------------------------------------------------------------------
u8 readGamepadMask(int jid, bool* startDown)
{
    GLFWgamepadstate st;
    if (!glfwGetGamepadState(jid, &st))
        return 0;

    u8 mask = 0;
    if (st.buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP]    == GLFW_PRESS) mask |= Io::Up;
    if (st.buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN]  == GLFW_PRESS) mask |= Io::Down;
    if (st.buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT]  == GLFW_PRESS) mask |= Io::Left;
    if (st.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT] == GLFW_PRESS) mask |= Io::Right;

    // Stick gauche avec zone morte large : la SMS n'a que du tout-ou-rien.
    const float kDead = 0.4f;
    if (st.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] < -kDead) mask |= Io::Up;
    if (st.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] >  kDead) mask |= Io::Down;
    if (st.axes[GLFW_GAMEPAD_AXIS_LEFT_X] < -kDead) mask |= Io::Left;
    if (st.axes[GLFW_GAMEPAD_AXIS_LEFT_X] >  kDead) mask |= Io::Right;

    if (st.buttons[GLFW_GAMEPAD_BUTTON_A] == GLFW_PRESS ||
        st.buttons[GLFW_GAMEPAD_BUTTON_X] == GLFW_PRESS) mask |= Io::B1;
    if (st.buttons[GLFW_GAMEPAD_BUTTON_B] == GLFW_PRESS ||
        st.buttons[GLFW_GAMEPAD_BUTTON_Y] == GLFW_PRESS) mask |= Io::B2;

    if (st.buttons[GLFW_GAMEPAD_BUTTON_START] == GLFW_PRESS)
        *startDown = true;
    return mask;
}

// -----------------------------------------------------------------------------
//  Calcule le rectangle 4:3 letterboxé dans une fenêtre de taille fbW×fbH.
// -----------------------------------------------------------------------------
void computeViewport(int fbW, int fbH, int& x, int& y, int& w, int& h)
{
    // La SMS sort du 4:3 (pixels non carrés : 256×192 étiré sur un tube 4:3).
    const double target = 4.0 / 3.0;
    const double actual = (fbH > 0) ? static_cast<double>(fbW) / fbH : target;
    if (actual > target) {
        // Fenêtre trop large : bandes verticales sur les côtés.
        h = fbH;
        w = static_cast<int>(fbH * target + 0.5);
        x = (fbW - w) / 2;
        y = 0;
    } else {
        // Fenêtre trop haute : bandes horizontales en haut/bas.
        w = fbW;
        h = static_cast<int>(fbW / target + 0.5);
        x = 0;
        y = (fbH - h) / 2;
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: sesame <rom.sms> [--bios FILE] [--pal]\n");
        std::fprintf(stderr, "error: no ROM file given\n");
        return 1;
    }

    const char* romPath  = nullptr;
    const char* biosPath = nullptr;
    bool        pal      = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--pal") == 0) {
            pal = true;
        } else if (std::strcmp(argv[i], "--bios") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --bios requires a file name\n");
                return 1;
            }
            biosPath = argv[++i];
        } else if (!romPath) {
            romPath = argv[i];
        } else {
            std::fprintf(stderr, "error: unexpected argument '%s'\n", argv[i]);
            return 1;
        }
    }

    // Un fichier positionnel nommé « BIOS ... » va dans le slot BIOS ; le
    // slot cartouche reste alors vide (console sans cartouche insérée).
    if (romPath && !biosPath && looksLikeBios(romPath)) {
        biosPath = romPath;
        romPath  = nullptr;
    }

    // --- Machine -------------------------------------------------------------
    Machine machine;
    machine.setRegion(pal ? Region::Pal : Region::Ntsc);
    if (biosPath && !machine.loadBios(biosPath)) {
        std::fprintf(stderr, "error: cannot load BIOS '%s' (missing or invalid file)\n",
                     biosPath);
        return 1;
    }
    if (romPath && !machine.loadRom(romPath)) {
        std::fprintf(stderr, "error: cannot load ROM '%s' (missing or invalid file)\n",
                     romPath);
        return 1;
    }
    const char* titlePath = romPath ? romPath : biosPath;

    // --- Fenêtre GLFW (contexte OpenGL de compatibilité par défaut) ----------
    if (!glfwInit()) {
        std::fprintf(stderr, "error: failed to initialize GLFW\n");
        return 1;
    }

    const std::string title =
        "Sesame — Master System — " + baseName(titlePath);
    GLFWwindow* win = glfwCreateWindow(768, 576, title.c_str(), nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "error: failed to create window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    // Vsync pour un affichage sans déchirure — mais il ne cadence PAS
    // l'émulation : sur un écran 120 Hz ou VRR il tournerait trop vite.
    // Le rythme des trames émulées est tenu par l'horloge réelle ci-dessous.
    glfwSwapInterval(1);

    // --- Texture du framebuffer (256×192, plus-proche-voisin pour le pixel art)
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Sortie audio temps réel (CoreAudio sur macOS). En cas d'échec, on
    // continue muet : l'anneau du PSG est quand même vidé chaque trame.
    AudioOut audio;
    const bool audioOn = audio.start();
    if (!audioOn)
        std::fprintf(stderr, "audio: no backend available, running muted\n");
    s16 audioScratch[4096];

    bool pauseWasDown = false;  // détection de FRONT pour le bouton Pause (NMI)

    // Bascule plein écran (touche F, sur front) : on mémorise la géométrie
    // fenêtrée pour la restaurer au retour.
    bool fullscreen = false;
    bool fsWasDown  = false;
    int  winX = 0, winY = 0, winW = 0, winH = 0;

    // --- Cadencement : une trame dure lignes×228 cycles à l'horloge de la
    // région (NTSC ~16,69 ms / 59,92 Hz ; PAL ~20,12 ms / 49,70 Hz). On émule
    // une trame quand son échéance réelle est atteinte, quel que soit le taux
    // de rafraîchissement de l'écran.
    const double framePeriod =
        static_cast<double>(Machine::kCyclesPerLine) *
        machine.vdp.linesPerFrame() / machine.cpuClock();
    double nextFrameDue = glfwGetTime();

    // --- Boucle principale ---------------------------------------------------
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        // Échap = quitter.
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(win, GLFW_TRUE);

        // R = reset matériel de la console.
        if (glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS)
            machine.reset();

        // F = bascule plein écran (sur FRONT d'appui).
        const bool fsDown = glfwGetKey(win, GLFW_KEY_F) == GLFW_PRESS;
        if (fsDown && !fsWasDown) {
            if (!fullscreen) {
                glfwGetWindowPos(win, &winX, &winY);
                glfwGetWindowSize(win, &winW, &winH);
                GLFWmonitor* mon = glfwGetPrimaryMonitor();
                const GLFWvidmode* mode = glfwGetVideoMode(mon);
                glfwSetWindowMonitor(win, mon, 0, 0, mode->width, mode->height,
                                     mode->refreshRate);
            } else {
                glfwSetWindowMonitor(win, nullptr, winX, winY, winW, winH, 0);
            }
            fullscreen = !fullscreen;
            // Certains pilotes perdent le réglage vsync au changement de mode.
            glfwSwapInterval(1);
        }
        fsWasDown = fsDown;

        // Manettes USB : slot GLFW 1 -> pad 1 (fusionné avec le clavier),
        // slot 2 -> pad 2. Le bouton Start de l'une OU l'autre = Pause.
        bool startDown = false;
        const u8 pad0 = readPadMask(win) |
                        readGamepadMask(GLFW_JOYSTICK_1, &startDown);
        const u8 pad1 = readGamepadMask(GLFW_JOYSTICK_2, &startDown);
        machine.io.setPad(0, pad0);
        machine.io.setPad(1, pad1);

        // Entrée ou Start = bouton Pause de la console : NMI sur FRONT d'appui.
        const bool pauseDown =
            glfwGetKey(win, GLFW_KEY_ENTER) == GLFW_PRESS || startDown;
        if (pauseDown && !pauseWasDown)
            machine.pressPause();
        pauseWasDown = pauseDown;

        // Émule les trames arrivées à échéance (0 sur un écran plus rapide
        // que 60 Hz, parfois 2 pour rattraper un hoquet — borné pour ne pas
        // spiraler). Après un vrai décrochage (fenêtre déplacée, mise en
        // veille…), on abandonne le retard plutôt que de le rattraper.
        double now = glfwGetTime();
        if (now - nextFrameDue > 0.25)
            nextFrameDue = now;
        for (int burst = 0; burst < 4 && now >= nextFrameDue; ++burst) {
            machine.runFrame();
            nextFrameDue += framePeriod;
            now = glfwGetTime();

            // Vidange de l'anneau du PSG vers la sortie audio (ou dans le
            // vide si aucun backend : il ne faut pas le laisser saturer).
            int n;
            while ((n = machine.psg.readSamples(
                        audioScratch, static_cast<int>(sizeof(audioScratch) /
                                                       sizeof(audioScratch[0])))) > 0) {
                if (audioOn)
                    audio.push(audioScratch, n);
            }
        }

        // Téléversement du framebuffer RGBA (u32 LE : octets R,G,B,A en mémoire).
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Vdp::kWidth, Vdp::kHeight, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, machine.vdp.frameBuffer());

        // Rendu : fond noir + quad texturé 4:3 letterboxé.
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(win, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        int vx, vy, vw, vh;
        computeViewport(fbW, fbH, vx, vy, vw, vh);
        glViewport(vx, vy, vw, vh);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glEnable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
        // Coordonnées texture : v=0 en haut du framebuffer, d'où l'inversion.
        glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, -1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f, -1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f,  1.0f);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f,  1.0f);
        glEnd();
        glDisable(GL_TEXTURE_2D);

        glfwSwapBuffers(win);

        // Sauvegarde périodique de la RAM cartouche (~toutes les 5 s ; no-op
        // si rien n'a changé) : une fermeture brutale ne perd presque rien.
        if (machine.frameCount % 300 == 0)
            machine.cart.persistSaveRam();
    }

    machine.cart.persistSaveRam();

    glDeleteTextures(1, &tex);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
