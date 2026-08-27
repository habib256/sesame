// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

// =============================================================================
//  sesame — frontend fenêtré : GLFW3 + OpenGL immediate mode.
//  Une itération de boucle = une trame émulée, téléversée dans une texture
//  256×192 puis dessinée en quad 4:3 (letterbox). Le vsync 60 Hz cadence tout.
//
//  Usage : sesame [rom.sms|rom.gg|rom.sg] [--bios fichier] [--no-bios]
//                 [--pal] [--gg] [--no-crt] [--kiosk] [--kiosk-monitor N]
//  Sans ROM : BIOS auto-détecté dans le dossier courant + menu borne.
//  Filtre CRT actif par défaut (--no-crt ou touche C pour l'image brute).
//  Un fichier dont le nom contient « BIOS » est chargé dans le slot BIOS.
//  --kiosk = mode borne : plein écran exclusif, curseur masqué, CRT activé.
// =============================================================================

// CMake définit GL_SILENCE_DEPRECATION sur macOS ; la garde évite de dépendre
// du système de build pour une simple vérification de syntaxe.
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION 1
#endif

#include "core/Machine.hpp"

#include "AudioOut.hpp"
#include "Config.hpp"
#include "CrtEffectStack.hpp"
#include "KioskMenu.hpp"

#include <GLFW/glfw3.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

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
//  Cherche une image BIOS (nom contenant « BIOS », extension voulue) dans
//  les répertoires candidats, dans l'ordre ; à l'intérieur d'un répertoire,
//  choix stable : premier nom par ordre alphabétique. Chaîne vide si rien.
// -----------------------------------------------------------------------------
std::string findBiosImage(const std::vector<std::string>& dirs,
                          const std::string& wantExt)
{
    for (const auto& dir : dirs) {
        std::string best;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            if (!e.is_regular_file(ec)) continue;
            const std::string name = e.path().filename().string();
            std::string ext = e.path().extension().string();
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext != wantExt || !looksLikeBios(name.c_str())) continue;
            const std::string p = e.path().string();
            if (best.empty() || p < best) best = p;
        }
        if (!best.empty()) return best;
    }
    return {};
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
//  Lecture d'une manette USB -> masque de boutons SMS + état des boutons
//  Start et Select. API « gamepad » de GLFW (mappings SDL intégrés) : D-pad
//  OU stick gauche = directions, A/X = bouton 1, B/Y = bouton 2,
//  Start = menu kiosk, Select (Back) = bouton Pause de la console.
//  Retourne 0 si aucune manette reconnue sur ce slot.
// -----------------------------------------------------------------------------
u8 readGamepadMask(int jid, bool* startDown, bool* selectDown)
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
    if (st.buttons[GLFW_GAMEPAD_BUTTON_BACK] == GLFW_PRESS)
        *selectDown = true;
    return mask;
}

// -----------------------------------------------------------------------------
//  Calcule le rectangle letterboxé de ratio `target` dans une fenêtre fbW×fbH.
//  SMS : 4:3 (pixels non carrés, 256×192 étiré sur un tube 4:3) ;
//  Game Gear : 10:9 (160×144 en pixels carrés, choix classique des
//  émulateurs pour la dalle LCD).
// -----------------------------------------------------------------------------
void computeViewport(int fbW, int fbH, double target,
                     int& x, int& y, int& w, int& h)
{
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
    // Sans ROM, le frontend démarre quand même : BIOS auto-détecté dans le
    // dossier courant et menu borne ouvert pour choisir un jeu (usage
    // typique : `sesame --kiosk` depuis le dossier des ROM).
    // Fichier de configuration : pré-scan de --config, puis chargement —
    // les valeurs du fichier deviennent les défauts, la ligne de commande
    // PRIME, et l'état effectif sera resauvegardé à la sortie propre.
    const char* configPath = nullptr;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--config") == 0)
            configPath = argv[i + 1];
    Config cfg = Config::locate(configPath, argv[0]);

    const char* romPath  = nullptr;
    const char* biosPath = nullptr;
    bool        pal      = cfg.pal;
    bool        crtOn    = cfg.crt;      // --no-crt / touche C : image brute
    bool        kiosk    = cfg.kiosk;
    int         kioskMonitor = cfg.kioskMonitor;
    long        shotAtFrame  = -1;       // --shot-at : validation du rendu GL
    long        exitAtFrame  = -1;       // --exit-at : sortie propre à la trame N
    const char* shotPath     = nullptr;
    bool        openMenu     = false;    // --menu : menu borne ouvert au départ
    bool        forceGg      = cfg.gameGear;  // --gg : matériel GG forcé
    bool        noBios       = (cfg.bios == "none");
    bool        cliBiosSeen  = false;   // --bios explicite (persisté)
    if (!cfg.bios.empty() && cfg.bios != "none")
        biosPath = cfg.bios.c_str();
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--menu") == 0) {
            openMenu = true;
        } else if (std::strcmp(argv[i], "--shot-at") == 0) {
            // Outil de validation : capture le framebuffer OpenGL réellement
            // affiché (donc APRÈS la passe CRT éventuelle) à la trame N.
            if (i + 2 >= argc) {
                std::fprintf(stderr, "error: --shot-at requires N and FILE.ppm\n");
                return 1;
            }
            shotAtFrame = std::atol(argv[++i]);
            shotPath    = argv[++i];
        } else if (std::strcmp(argv[i], "--exit-at") == 0) {
            // Outil de validation : fermeture PROPRE (config sauvegardée)
            // après N trames affichées.
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --exit-at requires N\n");
                return 1;
            }
            exitAtFrame = std::atol(argv[++i]);
        } else if (std::strcmp(argv[i], "--pal") == 0) {
            pal = true;
        } else if (std::strcmp(argv[i], "--gg") == 0) {
            // Matériel Game Gear forcé : une cartouche SMS tourne en mode
            // compatibilité (palette SMS, fenêtre LCD 160x144).
            forceGg = true;
        } else if (std::strcmp(argv[i], "--no-bios") == 0) {
            noBios = true;
            cliBiosSeen = true;   // « none » sera persisté
        } else if (std::strcmp(argv[i], "--config") == 0) {
            ++i;   // déjà traité par le pré-scan
        } else if (std::strcmp(argv[i], "--crt") == 0) {
            crtOn = true;   // déjà le défaut ; accepté pour compatibilité
        } else if (std::strcmp(argv[i], "--no-crt") == 0) {
            crtOn = false;  // image brute (la touche C rebascule)
        } else if (std::strcmp(argv[i], "--kiosk") == 0) {
            // Mode borne : plein écran exclusif, curseur masqué.
            kiosk = true;
        } else if (std::strcmp(argv[i], "--kiosk-monitor") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --kiosk-monitor requires a number\n");
                return 1;
            }
            kioskMonitor = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--bios") == 0) {
            cliBiosSeen = true;
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

    // Pas de --bios ? On cherche une image BIOS — comme une vraie console,
    // qui démarre toujours sur son BIOS ; il détecte et boote ensuite la
    // cartouche lui-même (contrôle mémoire 0x3E réel). Répertoires
    // candidats, dans l'ordre : <dossier ROM>/bios, <dossier ROM>, ./bios,
    // ., puis <dossier exécutable>/bios et .../../bios (couvre le
    // lancement depuis build/). Extension selon le matériel : .sms pour la
    // SMS, .gg pour la Game Gear (bios.gg). --no-bios s'en passe ;
    // SG-1000 (.sg) : pas de BIOS.
    std::string autoBios;
    std::string romExt = romPath ? romPath : "";
    if (auto d = romExt.rfind('.'); d != std::string::npos)
        romExt = romExt.substr(d);
    for (char& c : romExt) c = (char)std::tolower((unsigned char)c);
    if (!biosPath && !noBios && romExt != ".sg") {
        const bool ggHardware = forceGg || romExt == ".gg";
        std::vector<std::string> dirs;
        if (romPath) {
            std::string dir = romPath;
            const auto slash = dir.find_last_of("/\\");
            dir = (slash == std::string::npos) ? "." : dir.substr(0, slash);
            dirs.push_back(dir + "/bios");
            dirs.push_back(dir);
        }
        dirs.push_back("bios");
        dirs.push_back(".");
        std::string exe = argv[0];
        if (const auto slash = exe.find_last_of("/\\");
            slash != std::string::npos) {
            exe.resize(slash);
            dirs.push_back(exe + "/bios");
            dirs.push_back(exe + "/../bios");
        }
        autoBios = findBiosImage(dirs, ggHardware ? ".gg" : ".sms");
        if (!autoBios.empty()) {
            biosPath = autoBios.c_str();
            std::fprintf(stderr, "bios: auto-detected %s\n", biosPath);
        }
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
    if (forceGg && machine.model() == Model::Sms)
        machine.setModel(Model::GameGearSms);
    machine.vdp.setSpriteLimit(!cfg.noSpriteLimit);
    const char* titlePath = romPath ? romPath : biosPath;  // peut rester nul

    // Sans ROM : le menu borne s'ouvre au lancement pour choisir un jeu.
    if (!romPath)
        openMenu = true;

    // --- Fenêtre GLFW (contexte OpenGL de compatibilité par défaut) ----------
    if (!glfwInit()) {
        std::fprintf(stderr, "error: failed to initialize GLFW\n");
        return 1;
    }

    const char* consoleName =
        (machine.model() == Model::GameGear)    ? "Game Gear" :
        (machine.model() == Model::GameGearSms) ? "Game Gear (SMS mode)"
                                                : "Master System";
    const std::string title =
        std::string("Sesame — ") + consoleName +
        (titlePath ? " — " + baseName(titlePath) : std::string());

    // Mode borne : fenêtre créée DIRECTEMENT en plein écran exclusif sur le
    // moniteur choisi (reste au-dessus des panneaux/dock, garde le focus),
    // curseur masqué. Sinon : fenêtre bureau classique.
    GLFWmonitor* kioskMon = nullptr;
    if (kiosk) {
        int monCount = 0;
        GLFWmonitor** mons = glfwGetMonitors(&monCount);
        if (monCount <= 0) {
            std::fprintf(stderr, "error: no monitor found\n");
            glfwTerminate();
            return 1;
        }
        if (kioskMonitor < 0 || kioskMonitor >= monCount) {
            std::fprintf(stderr,
                         "warning: monitor %d not found (%d available), using 0\n",
                         kioskMonitor, monCount);
            kioskMonitor = 0;
        }
        kioskMon = mons[kioskMonitor];
    }

    GLFWwindow* win = nullptr;
    if (kioskMon) {
        const GLFWvidmode* mode = glfwGetVideoMode(kioskMon);
        win = glfwCreateWindow(mode->width, mode->height, title.c_str(),
                               kioskMon, nullptr);
    } else {
        win = glfwCreateWindow(768, 576, title.c_str(), nullptr, nullptr);
    }
    if (!win) {
        std::fprintf(stderr, "error: failed to create window\n");
        glfwTerminate();
        return 1;
    }
    if (kiosk)
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
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

    // Bascule plein écran (touche F ou action du menu) : on mémorise la
    // géométrie fenêtrée pour la restaurer au retour. En kiosk, on démarre
    // plein écran avec une géométrie fenêtrée par défaut en cas de sortie.
    bool fullscreen = kiosk;
    bool fsWasDown  = false;
    int  winX = 100, winY = 100, winW = 768, winH = 576;
    auto toggleFullscreen = [&]() {
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
        // En borne, le curseur ne reste masqué qu'en plein écran.
        if (kiosk)
            glfwSetInputMode(win, GLFW_CURSOR,
                             fullscreen ? GLFW_CURSOR_HIDDEN
                                        : GLFW_CURSOR_NORMAL);
    };

    // Menu borne : liste des jeux du dossier de la ROM chargée + actions.
    // Ouverture : F9 partout ; Start manette et Échap en mode borne.
    KioskMenu menu;
    if (!cfg.romDir.empty()) {
        // rom_dir : un ou plusieurs dossiers séparés par « ; ».
        std::vector<std::string> dirs;
        std::string rest = cfg.romDir;
        while (!rest.empty()) {
            const auto semi = rest.find(';');
            const std::string d = rest.substr(0, semi);
            if (!d.empty()) dirs.push_back(d);
            if (semi == std::string::npos) break;
            rest.erase(0, semi + 1);
        }
        menu.setRomDirs(dirs);
    } else {
        std::string dir = titlePath ? titlePath : "";
        const std::string::size_type slash = dir.find_last_of("/\\");
        menu.setRomDir(slash == std::string::npos ? "."
                                                  : dir.substr(0, slash));
    }
    bool menuToggleWasDown = false;
    if (cfg.fullscreen && !fullscreen)
        toggleFullscreen();
    if (openMenu)
        menu.open();

    long displayedFrames = 0;   // compteur d'itérations d'affichage (--shot-at)

    // Pile d'effets CRT : active par DÉFAUT (initialisée ici), --no-crt ou
    // la touche C rendent l'image brute. En cas d'échec (shader/FBO),
    // process() renvoie 0 et on retombe sur le rendu brut.
    CrtEffectStack crt;
    crt.setParams(cfg.crtParams);
    // Préréglage « dalle LCD » pour la Game Gear : la rémanence simule le
    // ghosting de l'écran d'origine, le masque à points dessine la grille
    // de pixels ; ni scanlines ni baril (ce n'est pas un tube).
    CrtParams lcdParams;
    lcdParams.persistence        = cfg.lcdPersistence;
    lcdParams.scanlines          = 0.0f;
    lcdParams.barrel             = 0.0f;
    lcdParams.shadowMask         = CrtParams::ShadowMask::Dot;
    lcdParams.shadowMaskStrength = cfg.lcdGridStrength;
    lcdParams.luminanceGain      = 1.2f;   // compense la grille
    bool crtWasDown = false;
    if (crtOn && !crt.initialize())
        std::fprintf(stderr, "crt: unavailable (%s), raw output\n",
                     crt.lastError().c_str());

    // --- Cadencement : une trame dure lignes×228 cycles à l'horloge de la
    // région (NTSC ~16,69 ms / 59,92 Hz ; PAL ~20,12 ms / 49,70 Hz). On émule
    // une trame quand son échéance réelle est atteinte, quel que soit le taux
    // de rafraîchissement de l'écran.
    const double framePeriod =
        static_cast<double>(Machine::kCyclesPerLine) *
        machine.vdp.linesPerFrame() / machine.cpuClock();
    double nextFrameDue = glfwGetTime();

    // Chemin de la ROM courante (mis à jour par le menu borne) et fichier
    // d'état associé : <rom sans extension>.state, à côté de la ROM.
    std::string currentRomPath = romPath ? romPath : "";
    auto statePathFor = [](const std::string& p) {
        const auto dot   = p.rfind('.');
        const auto slash = p.find_last_of("/\\");
        if (dot == std::string::npos ||
            (slash != std::string::npos && dot < slash))
            return p + ".state";
        return p.substr(0, dot) + ".state";
    };
    bool f5WasDown = false, f7WasDown = false;
    // Rewind : un save-state en mémoire par trame émulée, dans un anneau
    // borné ; maintenir Retour arrière remonte le temps une trame par trame
    // affichée (le framebuffer se reconstruit en rejouant la trame).
    std::deque<std::vector<u8>> rewindHist;
    const size_t rewindMax = cfg.rewind
        ? (size_t)(cfg.rewindSeconds > 0 ? cfg.rewindSeconds : 1) * 60 : 0;
    // Viewport de la trame précédente (letterbox) : sert à convertir la
    // position souris en coordonnées écran SMS pour le Light Phaser.
    int lastVx = 0, lastVy = 0, lastVw = 1, lastVh = 1;
    int lastSrcW = Vdp::kWidth, lastSrcH = Vdp::kHeight;

    // --- Boucle principale ---------------------------------------------------
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        // Échap : bureau = quitter ; borne = ouvrir/fermer le menu (une borne
        // ne doit pas être tuable d'une touche — Quit passe par le menu).
        const bool escDown = glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (escDown && !kiosk)
            glfwSetWindowShouldClose(win, GLFW_TRUE);

        // R = reset matériel de la console (pas pendant le menu).
        if (!menu.isOpen() && glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS)
            machine.reset();

        // F = bascule plein écran (sur FRONT d'appui).
        const bool fsDown = glfwGetKey(win, GLFW_KEY_F) == GLFW_PRESS;
        if (fsDown && !fsWasDown)
            toggleFullscreen();
        fsWasDown = fsDown;

        // C = bascule du filtre CRT (sur FRONT d'appui).
        const bool crtDown = glfwGetKey(win, GLFW_KEY_C) == GLFW_PRESS;
        if (crtDown && !crtWasDown) {
            crtOn = !crtOn;
            if (crtOn && !crt.initialize()) {
                std::fprintf(stderr, "crt: unavailable (%s), raw output\n",
                             crt.lastError().c_str());
                crtOn = false;
            }
        }
        crtWasDown = crtDown;

        // F5/F7 = save/load state dans <rom>.state (sur FRONT, pas pendant
        // le menu). L'émulation étant en frontière de trame ici, l'état est
        // pris au bon moment (le framebuffer n'est pas sérialisé).
        const bool f5Down = glfwGetKey(win, GLFW_KEY_F5) == GLFW_PRESS;
        const bool f7Down = glfwGetKey(win, GLFW_KEY_F7) == GLFW_PRESS;
        if (!menu.isOpen() && !currentRomPath.empty()) {
            const std::string statePath = statePathFor(currentRomPath);
            if (f5Down && !f5WasDown && machine.saveState(statePath))
                std::fprintf(stderr, "state: saved %s\n", statePath.c_str());
            if (f7Down && !f7WasDown && !machine.loadState(statePath))
                machine.reset();  // chargement raté : état mixte, on repart
        }
        f5WasDown = f5Down;
        f7WasDown = f7Down;

        // Manettes USB : slot GLFW 1 -> pad 1 (fusionné avec le clavier),
        // slot 2 -> pad 2.
        bool startDown = false, selectDown = false;
        const u8 kbMask = readPadMask(win);
        const u8 gpA = readGamepadMask(GLFW_JOYSTICK_1, &startDown, &selectDown);
        const u8 gpB = readGamepadMask(GLFW_JOYSTICK_2, &startDown, &selectDown);
        // swap_gamepads (config + menu) : échange les slots physiques 1/2.
        const u8 gp0 = cfg.swapGamepads ? gpB : gpA;
        const u8 gp1 = cfg.swapGamepads ? gpA : gpB;
        const bool enterDown = glfwGetKey(win, GLFW_KEY_ENTER) == GLFW_PRESS;

        // Ouverture/fermeture du menu borne : Start manette ou F9, partout ;
        // Échap aussi en mode borne. Start n'atteint donc jamais le jeu — le
        // bouton Pause de la console reste la touche Entrée.
        const bool menuToggleDown =
            startDown ||
            glfwGetKey(win, GLFW_KEY_F9) == GLFW_PRESS ||
            (kiosk && escDown);
        if (menuToggleDown && !menuToggleWasDown) {
            if (menu.isOpen()) menu.close();
            else               menu.open();
        }
        menuToggleWasDown = menuToggleDown;

        if (menu.isOpen()) {
            // Jeu en PAUSE : manettes relâchées côté console, pas de trame
            // émulée. Le menu navigue avec pads + clavier fusionnés.
            machine.io.setPad(0, 0);
            machine.io.setPad(1, 0);
            pauseWasDown = true;   // avale le front Pause à la fermeture

            KioskMenu::Input mi;
            const u8 all = static_cast<u8>(kbMask | gp0 | gp1);
            mi.up    = (all & Io::Up)    != 0;
            mi.down  = (all & Io::Down)  != 0;
            mi.left  = (all & Io::Left)  != 0;
            mi.right = (all & Io::Right) != 0;
            mi.fire  = (all & Io::B1) != 0 || enterDown;

            switch (menu.update(mi)) {
            case KioskMenu::Action::LoadRom:
                // Insérer une cartouche = sauvegarde de l'ancienne RAM puis
                // power-cycle (le BIOS éventuel reste en place et boote le jeu).
                machine.cart.persistSaveRam();
                if (machine.loadRom(menu.chosenRom())) {
                    currentRomPath = menu.chosenRom();
                    // loadRom() peut changer de modèle (.sms <-> .gg) ; en
                    // session --gg, une .sms repasse en compatibilité.
                    if (forceGg && machine.model() == Model::Sms)
                        machine.setModel(Model::GameGearSms);
                    glfwSetWindowTitle(
                        win, (std::string("Sesame — ") +
                              (machine.model() == Model::GameGear    ? "Game Gear" :
                               machine.model() == Model::GameGearSms ? "Game Gear (SMS mode)"
                                                                     : "Master System") +
                              " — " + baseName(menu.chosenRom())).c_str());
                } else {
                    std::fprintf(stderr, "error: cannot load ROM '%s'\n",
                                 menu.chosenRom().c_str());
                }
                break;
            case KioskMenu::Action::Restart:
                machine.reset();
                break;
            case KioskMenu::Action::ToggleFullscreen:
                toggleFullscreen();
                break;
            case KioskMenu::Action::SwapGamepads:
                cfg.swapGamepads = !cfg.swapGamepads;   // persisté à la sortie
                break;
            case KioskMenu::Action::Quit:
                glfwSetWindowShouldClose(win, GLFW_TRUE);
                break;
            case KioskMenu::Action::Resume:
            case KioskMenu::Action::None:
                break;
            }
        } else {
            u8 p0 = static_cast<u8>(kbMask | gp0);

            // Light Phaser : la souris vise (coordonnées écran via le
            // letterbox de la trame précédente), le clic gauche presse la
            // gâchette (TL = bouton 1 du port A). SMS uniquement.
            if (cfg.lightPhaser && machine.model() == Model::Sms) {
                double mx = 0, my = 0;
                glfwGetCursorPos(win, &mx, &my);
                int winW = 1, winH = 1;
                glfwGetWindowSize(win, &winW, &winH);
                int fbWNow = 1, fbHNow = 1;
                glfwGetFramebufferSize(win, &fbWNow, &fbHNow);
                // Souris en points -> pixels framebuffer (écrans Retina).
                const double sxf = (mx * fbWNow / (winW > 0 ? winW : 1) - lastVx)
                                   * lastSrcW / (lastVw > 0 ? lastVw : 1);
                const double syf = (my * fbHNow / (winH > 0 ? winH : 1) - lastVy)
                                   * lastSrcH / (lastVh > 0 ? lastVh : 1);
                if (sxf >= 0 && sxf < lastSrcW && syf >= 0 && syf < lastSrcH)
                    machine.setLightPhaser((int)sxf, (int)syf);
                else
                    machine.setLightPhaser(-1, -1);
                if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) ==
                    GLFW_PRESS)
                    p0 |= Io::B1;   // gâchette
            }

            // Entrée ou Select manette : Pause SMS (NMI sur FRONT) — ou
            // bouton Start de la Game Gear (simple NIVEAU lu sur le port
            // 0x00, les jeux GG s'en servent comme d'un bouton normal).
            const bool pauseDown = enterDown || selectDown;
            if (machine.model() == Model::GameGear) {
                if (pauseDown)
                    p0 |= Io::Start;
            } else if (pauseDown && !pauseWasDown) {
                machine.pressPause();
            }
            pauseWasDown = pauseDown;

            machine.io.setPad(0, p0);
            machine.io.setPad(1, gp1);

            // Rewind : tant que la touche est tenue et qu'il reste de
            // l'historique, on remonte d'une trame par trame affichée au
            // lieu d'émuler en avant.
            const bool rewindDown =
                rewindMax > 0 &&
                glfwGetKey(win, GLFW_KEY_BACKSPACE) == GLFW_PRESS;
            if (rewindDown && rewindHist.size() >= 2) {
                rewindHist.pop_back();
                const std::vector<u8>& top = rewindHist.back();
                if (machine.loadStateBuffer(top.data(), top.size()))
                    machine.runFrame();   // rejoue la trame pour l'image
                int rn;
                while ((rn = machine.psg.readSamples(
                            audioScratch,
                            static_cast<int>(sizeof(audioScratch) /
                                             sizeof(audioScratch[0]) / 2))) > 0) {
                    if (audioOn)
                        audio.push(audioScratch, rn);
                }
                nextFrameDue = glfwGetTime();   // pas de rattrapage après
            } else {

            // Émule les trames arrivées à échéance (0 sur un écran plus
            // rapide que 60 Hz, parfois 2 pour rattraper un hoquet — borné
            // pour ne pas spiraler). Après un vrai décrochage (fenêtre
            // déplacée, veille, menu ouvert…), on abandonne le retard.
            double now = glfwGetTime();
            if (now - nextFrameDue > 0.25)
                nextFrameDue = now;
            for (int burst = 0; burst < 4 && now >= nextFrameDue; ++burst) {
                if (rewindMax > 0) {
                    // Un état par trame émulée, historique borné.
                    std::vector<u8> snap;
                    if (machine.saveStateBuffer(snap)) {
                        rewindHist.push_back(std::move(snap));
                        while (rewindHist.size() > rewindMax)
                            rewindHist.pop_front();
                    }
                }
                machine.runFrame();
                nextFrameDue += framePeriod;
                now = glfwGetTime();

                // Vidange de l'anneau du PSG vers la sortie audio (ou dans le
                // vide si aucun backend : il ne faut pas le laisser saturer).
                // Trames stéréo : 2 s16 par trame dans audioScratch.
                int n;
                while ((n = machine.psg.readSamples(
                            audioScratch,
                            static_cast<int>(sizeof(audioScratch) /
                                             sizeof(audioScratch[0]) / 2))) > 0) {
                    if (audioOn)
                        audio.push(audioScratch, n);
                }
            }
            }   // fin du bloc émulation avant (else du rewind)
        }

        // Téléversement du framebuffer RGBA (u32 LE : octets R,G,B,A en
        // mémoire). En Game Gear, seule la fenêtre visible 160×144 (centrée
        // dans la trame 256×192 du VDP) est téléversée : GL_UNPACK_ROW_LENGTH
        // fait lire la sous-image en place, sans copie.
        const bool gearGear = (machine.model() != Model::Sms);
        const int vdpH = machine.vdp.height();   // 192/224/240 selon le mode
        const int srcW = gearGear ? Vdp::kGgWidth : Vdp::kWidth;
        const int srcH = gearGear ? Vdp::kGgHeight : vdpH;
        const u32* srcPx = machine.vdp.frameBuffer() +
            (gearGear ? ((vdpH - Vdp::kGgHeight) / 2) * Vdp::kWidth +
                            Vdp::kGgOffsetX
                      : 0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, Vdp::kWidth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, srcW, srcH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, srcPx);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

        // Rendu : fond noir + quad texturé letterboxé (4:3 SMS, 10:9 GG).
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(win, &fbW, &fbH);

        int vx, vy, vw, vh;
        computeViewport(fbW, fbH, gearGear ? 10.0 / 9.0 : 4.0 / 3.0,
                        vx, vy, vw, vh);
        lastVx = vx; lastVy = vy; lastVw = vw; lastVh = vh;
        lastSrcW = srcW; lastSrcH = srcH;

        // Passe CRT optionnelle : transforme la texture SMS brute en écran
        // « verre » rendu à la taille du viewport. En cas d'échec (FBO refusé),
        // process() renvoie 0 et on présente la texture brute.
        GLuint drawTex = tex;
        crt.setParams(gearGear ? lcdParams : cfg.crtParams);
        if (crtOn && crt.available()) {
            if (const unsigned int out =
                    crt.process(tex, srcW, srcH, vw, vh))
                drawTex = out;
        }

        glViewport(0, 0, fbW, fbH);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glViewport(vx, vy, vw, vh);
        glBindTexture(GL_TEXTURE_2D, drawTex);

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

        // Menu borne par-dessus le jeu figé (net, hors passe CRT).
        menu.render(fbW, fbH, fullscreen, cfg.swapGamepads);

        // --shot-at : relit le framebuffer GL (bas vers haut) et écrit un PPM.
        // Compte les trames AFFICHÉES (pas émulées) : la capture fonctionne
        // aussi quand le jeu est en pause derrière le menu borne.
        ++displayedFrames;
        if (shotPath && displayedFrames >= shotAtFrame) {
            std::vector<unsigned char> px(
                static_cast<size_t>(fbW) * fbH * 3);
            glReadPixels(0, 0, fbW, fbH, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            if (FILE* f = std::fopen(shotPath, "wb")) {
                std::fprintf(f, "P6\n%d %d\n255\n", fbW, fbH);
                for (int y = fbH - 1; y >= 0; --y)  // GL est bas->haut
                    std::fwrite(&px[static_cast<size_t>(y) * fbW * 3], 1,
                                static_cast<size_t>(fbW) * 3, f);
                std::fclose(f);
                std::fprintf(stderr, "shot: wrote %s (%dx%d)\n",
                             shotPath, fbW, fbH);
            }
            shotPath = nullptr;  // une seule capture
        }

        glfwSwapBuffers(win);

        if (exitAtFrame >= 0 && displayedFrames >= exitAtFrame)
            glfwSetWindowShouldClose(win, GLFW_TRUE);

        // Sauvegarde périodique de la RAM cartouche (~toutes les 5 s ; no-op
        // si rien n'a changé) : une fermeture brutale ne perd presque rien.
        if (machine.frameCount % 300 == 0)
            machine.cart.persistSaveRam();
    }

    machine.cart.persistSaveRam();

    // « Garde la config » : l'état effectif est réécrit dans sesame.cfg —
    // les réglages (CRT, plein écran, PAL, borne…) survivent au prochain
    // lancement. Un --bios/--no-bios explicite est persisté ; l'auto-
    // détection, elle, reste « auto » (champ bios laissé tel quel).
    cfg.pal = pal;
    cfg.gameGear = forceGg;
    cfg.crt = crtOn;
    cfg.fullscreen = fullscreen && !kiosk;   // la borne est déjà plein écran
    cfg.kiosk = kiosk;
    cfg.kioskMonitor = kioskMonitor;
    if (cliBiosSeen)
        cfg.bios = noBios ? "none" : (biosPath ? biosPath : "");
    if (cfg.save())
        std::fprintf(stderr, "config: saved %s\n", cfg.path.c_str());
    else
        std::fprintf(stderr, "config: cannot write %s\n", cfg.path.c_str());

    glDeleteTextures(1, &tex);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
