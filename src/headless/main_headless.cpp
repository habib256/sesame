// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

// =============================================================================
//  sesame-headless — frontend de validation n°1 du projet.
//  Exécute une ROM sans interface : traces déterministes, captures PPM,
//  enregistrement WAV du PSG, console SDSC. Tous les messages sont en anglais.
//
//  Usage : sesame-headless <rom.sms> [options]   (voir --help)
// =============================================================================
#include "core/Machine.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// -----------------------------------------------------------------------------
//  Aide (anglais, sur stdout).
// -----------------------------------------------------------------------------
void printUsage(FILE* out)
{
    std::fprintf(out,
        "usage: sesame-headless <rom.sms|rom.gg|rom.sg> [options]\n"
        "\n"
        "options:\n"
        "  --bios FILE           load FILE into the BIOS slot (boots before the\n"
        "                        cartridge; a ROM whose name contains \"BIOS\" is\n"
        "                        loaded there automatically)\n"
        "  --pal                 emulate a PAL console (313 lines, ~49.70 Hz,\n"
        "                        CPU 3546893 Hz) instead of NTSC\n"
        "  --gg                  force Game Gear hardware: an SMS cartridge\n"
        "                        runs in compatibility mode (160x144 window)\n"
        "  --no-sprite-limit     remove the sprites-per-scanline hardware limit\n"
        "  --gun X Y             aim the Light Phaser at screen position X,Y\n"
        "  --rewind-check        exercise the in-memory rewind states and verify\n"
        "                        that rewinding then replaying is pixel-exact\n"
        "  --frames N            number of frames to run (default 60)\n"
        "  --trace FILE          write a per-instruction CPU trace to FILE\n"
        "  --screenshot FILE.ppm capture the final framebuffer as a PPM image\n"
        "  --shot-every N PREFIX capture PREFIX%%04d.ppm every N frames\n"
        "  --sav                 load and persist cartridge save RAM (<rom>.sav);\n"
        "                        off by default to keep runs deterministic\n"
        "  --sdsc                enable the SDSC debug console (ports 0xFC/0xFD)\n"
        "  --exit-sdsc TEXT      stop as soon as the SDSC output contains TEXT\n"
        "                        (implies --sdsc; for test harnesses)\n"
        "  --wav FILE            record PSG audio (44100 Hz stereo s16 WAV;\n"
        "                        both channels identical on SMS, panned on GG)\n"
        "  --pause-at N          press the Pause button (NMI) at frame N\n"
        "  --state-save N FILE   save a machine state to FILE after frame N\n"
        "  --state-load FILE     load a machine state before the first frame\n"
        "  --help                show this help and exit\n");
}

// -----------------------------------------------------------------------------
//  Écrit le framebuffer RGBA du VDP dans un PPM binaire (P6).
//  Chaque pixel est un u32 little-endian : R = v & 0xFF, puis G, puis B.
//  `vdpHeight` est la hauteur visible du mode courant (192/224/240) ;
//  en Game Gear, seule la fenêtre visible 160×144 (centrée) est écrite.
// -----------------------------------------------------------------------------
bool writePpm(const char* path, const u32* fb, Model model, int vdpHeight)
{
    FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::fprintf(stderr, "error: cannot open '%s' for writing\n", path);
        return false;
    }
    const bool gg = (model != Model::Sms);   // GG natif OU compat SMS
    const int w  = gg ? Vdp::kGgWidth   : Vdp::kWidth;
    const int h  = gg ? Vdp::kGgHeight  : vdpHeight;
    const int x0 = gg ? Vdp::kGgOffsetX : 0;
    const int y0 = gg ? (vdpHeight - Vdp::kGgHeight) / 2 : 0;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    // Conversion RGBA -> RGB, un pixel à la fois (simple et déterministe).
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const u32 v = fb[(y0 + y) * Vdp::kWidth + x0 + x];
            const unsigned char rgb[3] = {
                static_cast<unsigned char>(v & 0xFF),
                static_cast<unsigned char>((v >> 8) & 0xFF),
                static_cast<unsigned char>((v >> 16) & 0xFF),
            };
            std::fwrite(rgb, 1, 3, f);
        }
    }
    std::fclose(f);
    return true;
}

// -----------------------------------------------------------------------------
//  En-tête WAV écrit à la main : RIFF/WAVE, fmt PCM 16 bits STÉRÉO 44100 Hz
//  (trames entrelacées G,D du PSG — identiques en SMS, pannées en Game Gear).
//  Les tailles sont bouchées à zéro puis corrigées à la fin (via fseek).
// -----------------------------------------------------------------------------
void writeU32le(FILE* f, u32 v)
{
    const unsigned char b[4] = {
        static_cast<unsigned char>(v & 0xFF),
        static_cast<unsigned char>((v >> 8) & 0xFF),
        static_cast<unsigned char>((v >> 16) & 0xFF),
        static_cast<unsigned char>((v >> 24) & 0xFF),
    };
    std::fwrite(b, 1, 4, f);
}

void writeU16le(FILE* f, u16 v)
{
    const unsigned char b[2] = {
        static_cast<unsigned char>(v & 0xFF),
        static_cast<unsigned char>((v >> 8) & 0xFF),
    };
    std::fwrite(b, 1, 2, f);
}

void writeWavHeader(FILE* f, u32 dataBytes)
{
    const u32 sampleRate = Psg::kSampleRate;
    const u16 channels   = 2;
    const u16 bitsPerSmp = 16;
    const u16 blockAlign = channels * bitsPerSmp / 8;
    const u32 byteRate   = sampleRate * blockAlign;

    std::fwrite("RIFF", 1, 4, f);
    writeU32le(f, 36 + dataBytes);      // taille RIFF = 4 + (8+16) + (8+data)
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    writeU32le(f, 16);                  // taille du bloc fmt
    writeU16le(f, 1);                   // format PCM
    writeU16le(f, channels);
    writeU32le(f, sampleRate);
    writeU32le(f, byteRate);
    writeU16le(f, blockAlign);
    writeU16le(f, bitsPerSmp);
    std::fwrite("data", 1, 4, f);
    writeU32le(f, dataBytes);
}

// Écrit des échantillons s16 en little-endian explicite (portable).
void writeSamplesLe(FILE* f, const s16* smp, int n)
{
    for (int i = 0; i < n; ++i) {
        const u16 v = static_cast<u16>(smp[i]);
        writeU16le(f, v);
    }
}

// Taille d'un fichier (pour le message "rom: ..."), -1 si introuvable.
long fileSize(const char* path)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) return -1;
    long size = -1;
    if (std::fseek(f, 0, SEEK_END) == 0) size = std::ftell(f);
    std::fclose(f);
    return size;
}

// Détecte une image BIOS par son nom de fichier : le nom de base contient
// « BIOS » (insensible à la casse). Permet de charger une image renommée
// « BIOS xxx.sms » sans option explicite.
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

// Lit un entier positif pour une option ; quitte avec un message sinon.
long parseCount(const char* opt, const char* arg)
{
    if (!arg) {
        std::fprintf(stderr, "error: %s requires a value\n", opt);
        std::exit(1);
    }
    char* end = nullptr;
    const long v = std::strtol(arg, &end, 10);
    if (end == arg || *end != '\0' || v < 0) {
        std::fprintf(stderr, "error: %s expects a non-negative integer, got '%s'\n",
                     opt, arg);
        std::exit(1);
    }
    return v;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "error: no ROM file given\n");
        printUsage(stderr);
        return 1;
    }
    if (std::strcmp(argv[1], "--help") == 0) {
        printUsage(stdout);
        return 0;
    }

    const char* romPath = argv[1];

    // --- Analyse des options -------------------------------------------------
    const char* biosPath      = nullptr;
    bool        pal           = false;
    bool        sav           = false;
    long        frames        = 60;
    const char* tracePath     = nullptr;
    const char* screenshotPath = nullptr;
    long        shotEvery     = 0;        // 0 = désactivé
    const char* shotPrefix    = nullptr;
    bool        sdsc          = false;
    const char* exitSdsc      = nullptr;
    const char* wavPath       = nullptr;
    long        pauseAt       = -1;       // -1 = jamais
    bool        forceGg       = false;    // --gg : matériel Game Gear forcé
    bool        noSpriteLimit = false;    // --no-sprite-limit (pédagogique)
    long        gunX = -1, gunY = -1;     // --gun X Y : Light Phaser visé
    bool        rewindCheck = false;      // --rewind-check : test du rewind
    const char* stateLoadPath = nullptr;  // --state-load : avant la 1re trame
    long        stateSaveAt   = -1;       // --state-save : après la trame N
    const char* stateSavePath = nullptr;

    for (int i = 2; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--help") == 0) {
            printUsage(stdout);
            return 0;
        } else if (std::strcmp(a, "--pal") == 0) {
            pal = true;
        } else if (std::strcmp(a, "--bios") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --bios requires a file name\n");
                return 1;
            }
            biosPath = argv[++i];
        } else if (std::strcmp(a, "--frames") == 0) {
            frames = parseCount(a, (i + 1 < argc) ? argv[++i] : nullptr);
        } else if (std::strcmp(a, "--trace") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --trace requires a file name\n");
                return 1;
            }
            tracePath = argv[++i];
        } else if (std::strcmp(a, "--screenshot") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --screenshot requires a file name\n");
                return 1;
            }
            screenshotPath = argv[++i];
        } else if (std::strcmp(a, "--shot-every") == 0) {
            shotEvery = parseCount(a, (i + 1 < argc) ? argv[++i] : nullptr);
            if (shotEvery <= 0) {
                std::fprintf(stderr, "error: --shot-every expects a positive interval\n");
                return 1;
            }
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --shot-every requires N and PREFIX\n");
                return 1;
            }
            shotPrefix = argv[++i];
        } else if (std::strcmp(a, "--state-load") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --state-load requires a file name\n");
                return 1;
            }
            stateLoadPath = argv[++i];
        } else if (std::strcmp(a, "--state-save") == 0) {
            stateSaveAt = parseCount(a, (i + 1 < argc) ? argv[++i] : nullptr);
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --state-save requires N and FILE\n");
                return 1;
            }
            stateSavePath = argv[++i];
        } else if (std::strcmp(a, "--gg") == 0) {
            forceGg = true;
        } else if (std::strcmp(a, "--no-sprite-limit") == 0) {
            noSpriteLimit = true;
        } else if (std::strcmp(a, "--rewind-check") == 0) {
            rewindCheck = true;
        } else if (std::strcmp(a, "--gun") == 0) {
            gunX = parseCount(a, (i + 1 < argc) ? argv[++i] : nullptr);
            gunY = parseCount(a, (i + 1 < argc) ? argv[++i] : nullptr);
        } else if (std::strcmp(a, "--sav") == 0) {
            sav = true;
        } else if (std::strcmp(a, "--sdsc") == 0) {
            sdsc = true;
        } else if (std::strcmp(a, "--exit-sdsc") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --exit-sdsc requires a text\n");
                return 1;
            }
            exitSdsc = argv[++i];
            sdsc = true;
        } else if (std::strcmp(a, "--wav") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --wav requires a file name\n");
                return 1;
            }
            wavPath = argv[++i];
        } else if (std::strcmp(a, "--pause-at") == 0) {
            pauseAt = parseCount(a, (i + 1 < argc) ? argv[++i] : nullptr);
        } else {
            std::fprintf(stderr, "error: unknown option '%s'\n", a);
            printUsage(stderr);
            return 1;
        }
    }

    // --- Chargement des médias -----------------------------------------------
    // Un fichier positionnel dont le nom contient « BIOS » va dans le slot
    // BIOS (sauf si --bios est déjà donné) ; le slot cartouche reste alors
    // vide, comme une console sans cartouche insérée.
    Machine machine;
    machine.setRegion(pal ? Region::Pal : Region::Ntsc);
    // Persistance .sav opt-in seulement : un .sav qui traîne changerait les
    // traces, et le headless est l'outil de validation déterministe.
    machine.cart.savEnabled = sav;
    if (!biosPath && looksLikeBios(romPath)) {
        biosPath = romPath;
        romPath  = nullptr;
    }
    if (biosPath) {
        if (!machine.loadBios(biosPath)) {
            std::fprintf(stderr, "error: cannot load BIOS '%s' (missing or invalid file)\n",
                         biosPath);
            return 1;
        }
        std::fprintf(stderr, "bios: %s (%ld bytes)\n", biosPath, fileSize(biosPath));
    }
    if (romPath) {
        if (!machine.loadRom(romPath)) {
            std::fprintf(stderr, "error: cannot load ROM '%s' (missing or invalid file)\n",
                         romPath);
            return 1;
        }
        std::fprintf(stderr, "rom: %s (%ld bytes)\n", romPath, fileSize(romPath));
    }
    // --gg : force le matériel Game Gear ; une cartouche SMS y passe en
    // mode compatibilité (palette SMS, fenêtre LCD 160×144).
    if (forceGg && machine.model() == Model::Sms)
        machine.setModel(Model::GameGearSms);
    machine.vdp.setSpriteLimit(!noSpriteLimit);
    if (gunX >= 0)
        machine.setLightPhaser((int)gunX, (int)gunY);
    const bool gg = (machine.model() != Model::Sms);
    std::fprintf(stderr, "video: %dx%d @ %s%s\n",
                 gg ? Vdp::kGgWidth : Vdp::kWidth,
                 gg ? Vdp::kGgHeight : Vdp::kHeight,
                 pal ? "50 Hz PAL" : "60 Hz NTSC",
                 machine.model() == Model::GameGear ? " (Game Gear)" :
                 machine.model() == Model::GameGearSms
                     ? " (Game Gear, SMS mode)" : "");

    machine.io.sdscEnabled = sdsc;

    // --state-load : restaure AVANT la première trame (le fichier doit
    // correspondre au modèle et à la région de la machine courante).
    if (stateLoadPath) {
        if (!machine.loadState(stateLoadPath))
            return 1;
        std::fprintf(stderr, "state: loaded %s\n", stateLoadPath);
    }

    // --- Fichiers de sortie --------------------------------------------------
    FILE* traceFile = nullptr;
    if (tracePath) {
        traceFile = std::fopen(tracePath, "w");
        if (!traceFile) {
            std::fprintf(stderr, "error: cannot open trace file '%s'\n", tracePath);
            return 1;
        }
        machine.traceFile = traceFile;
    }

    FILE* wavFile = nullptr;
    u32 wavDataBytes = 0;
    if (wavPath) {
        wavFile = std::fopen(wavPath, "wb");
        if (!wavFile) {
            std::fprintf(stderr, "error: cannot open WAV file '%s'\n", wavPath);
            if (traceFile) std::fclose(traceFile);
            return 1;
        }
        // En-tête provisoire (tailles nulles), corrigé après le run.
        writeWavHeader(wavFile, 0);
    }

    // --rewind-check : la base du rewind — 40 trames avec un état MÉMOIRE
    // par trame, retour à la trame 20, rejeu jusqu'à 40 — doit reproduire
    // exactement le même framebuffer (déterminisme des états en mémoire).
    if (rewindCheck) {
        std::vector<std::vector<u8>> hist;
        for (int fr = 0; fr < 40; ++fr) {
            std::vector<u8> snap;
            if (!machine.saveStateBuffer(snap)) {
                std::fprintf(stderr, "REWIND FAIL (save buffer)\n");
                return 1;
            }
            hist.push_back(std::move(snap));
            machine.runFrame();
        }
        const size_t fbBytes =
            (size_t)Vdp::kWidth * machine.vdp.height() * sizeof(u32);
        std::vector<u8> ref((const u8*)machine.vdp.frameBuffer(),
                            (const u8*)machine.vdp.frameBuffer() + fbBytes);
        if (!machine.loadStateBuffer(hist[20].data(), hist[20].size())) {
            std::fprintf(stderr, "REWIND FAIL (load buffer)\n");
            return 1;
        }
        for (int fr = 20; fr < 40; ++fr)
            machine.runFrame();
        const bool same =
            std::memcmp(ref.data(), machine.vdp.frameBuffer(), fbBytes) == 0;
        std::printf(same ? "REWIND OK\n" : "REWIND FAIL\n");
        return same ? 0 : 1;
    }

    // --- Boucle principale : une itération = une trame vidéo -----------------
    std::vector<s16> audioBuf(8192);
    // --exit-sdsc : on ne rescanne que le texte SDSC arrivé depuis la
    // dernière trame (avec un chevauchement de la longueur du motif, au cas
    // où il serait à cheval sur deux trames).
    size_t sdscScanned = 0;
    const size_t exitLen = exitSdsc ? std::strlen(exitSdsc) : 0;
    for (long f = 0; f < frames; ++f) {
        // Bouton Pause = front NMI, déclenché AVANT d'exécuter la trame N.
        if (pauseAt >= 0 && f == pauseAt)
            machine.pressPause();

        machine.runFrame();

        // --state-save : capture en frontière de trame, après la trame N.
        if (stateSavePath && f + 1 == stateSaveAt) {
            if (!machine.saveState(stateSavePath))
                return 1;
            std::fprintf(stderr, "state: saved %s after frame %ld\n",
                         stateSavePath, f + 1);
        }

        if (exitSdsc) {
            const std::string& log = machine.io.sdscLog();
            const size_t from =
                (sdscScanned > exitLen) ? sdscScanned - exitLen : 0;
            if (log.find(exitSdsc, from) != std::string::npos) {
                std::fprintf(stderr, "exit-sdsc: matched after frame %ld\n",
                             f + 1);
                break;
            }
            sdscScanned = log.size();
        }

        // Vidange de l'anneau audio du PSG vers le WAV (évite la saturation).
        // readSamples() compte en TRAMES stéréo : 2 s16 écrits par trame.
        if (wavFile) {
            int n;
            while ((n = machine.psg.readSamples(audioBuf.data(),
                                                static_cast<int>(audioBuf.size() / 2))) > 0) {
                writeSamplesLe(wavFile, audioBuf.data(), n * 2);
                wavDataBytes += static_cast<u32>(n) * 4;
            }
        }

        // Captures périodiques : PREFIX%04d.ppm, numérotées par trame (1-based).
        if (shotPrefix && ((f + 1) % shotEvery) == 0) {
            char name[1024];
            std::snprintf(name, sizeof(name), "%s%04ld.ppm", shotPrefix, f + 1);
            if (!writePpm(name, machine.vdp.frameBuffer(), machine.model(),
                          machine.vdp.height())) {
                if (traceFile) std::fclose(traceFile);
                if (wavFile) std::fclose(wavFile);
                return 1;
            }
        }
    }

    // --- Sorties finales -----------------------------------------------------
    if (screenshotPath) {
        if (!writePpm(screenshotPath, machine.vdp.frameBuffer(),
                      machine.model(), machine.vdp.height())) {
            if (traceFile) std::fclose(traceFile);
            if (wavFile) std::fclose(wavFile);
            return 1;
        }
    }

    if (wavFile) {
        // Retour au début pour écrire les vraies tailles dans l'en-tête.
        std::fseek(wavFile, 0, SEEK_SET);
        writeWavHeader(wavFile, wavDataBytes);
        std::fclose(wavFile);
    }

    if (traceFile) {
        machine.traceFile = nullptr;
        std::fclose(traceFile);
    }

    machine.cart.persistSaveRam();  // no-op sans --sav ou si rien n'a changé

    std::fprintf(stderr, "frames: %llu\n",
                 static_cast<unsigned long long>(machine.frameCount));
    std::fprintf(stderr, "cycles: %llu\n",
                 static_cast<unsigned long long>(machine.cpu.cycles));
    return 0;
}
