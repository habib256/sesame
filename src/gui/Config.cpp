// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

// =============================================================================
//  Config.cpp — lecture/écriture de sesame.cfg (voir Config.hpp).
// =============================================================================
#include "Config.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Booléen permissif : true/false, 1/0, on/off, yes/no.
bool parseBool(const std::string& v, bool fallback) {
    const std::string s = lower(v);
    if (s == "true" || s == "1" || s == "on" || s == "yes") return true;
    if (s == "false" || s == "0" || s == "off" || s == "no") return false;
    return fallback;
}

float parseFloat(const std::string& v, float fallback) {
    try { return std::stof(v); } catch (...) { return fallback; }
}

int parseInt(const std::string& v, int fallback) {
    try { return std::stoi(v); } catch (...) { return fallback; }
}

const char* maskName(CrtParams::ShadowMask m) {
    switch (m) {
    case CrtParams::ShadowMask::Triad:    return "triad";
    case CrtParams::ShadowMask::Aperture: return "aperture";
    case CrtParams::ShadowMask::Dot:      return "dot";
    default:                              return "off";
    }
}

CrtParams::ShadowMask parseMask(const std::string& v,
                                CrtParams::ShadowMask fallback) {
    const std::string s = lower(v);
    if (s == "off")      return CrtParams::ShadowMask::Off;
    if (s == "triad")    return CrtParams::ShadowMask::Triad;
    if (s == "aperture") return CrtParams::ShadowMask::Aperture;
    if (s == "dot")      return CrtParams::ShadowMask::Dot;
    return fallback;
}

}  // namespace

bool Config::load(const std::string& p) {
    std::ifstream f(p);
    if (!f)
        return false;
    path = p;

    std::string line;
    while (std::getline(f, line)) {
        // Commentaires (#) et lignes vides. (Pas « ; » : c'est le
        // séparateur de liste de rom_dir.)
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        line = trim(line);
        if (line.empty()) continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = lower(trim(line.substr(0, eq)));
        const std::string val = trim(line.substr(eq + 1));
        CrtParams& c = crtParams;

        if      (key == "pal")             pal = parseBool(val, pal);
        else if (key == "game_gear")       gameGear = parseBool(val, gameGear);
        else if (key == "bios") {
            // Tolérance : « empty »/« auto » = champ laissé vide (l'ancien
            // commentaire du fichier invitait à écrire le mot littéral).
            const std::string b = lower(val);
            bios = (b == "empty" || b == "auto") ? "" : val;
        }
        else if (key == "crt")             crt = parseBool(val, crt);
        else if (key == "fullscreen")      fullscreen = parseBool(val, fullscreen);
        else if (key == "kiosk")           kiosk = parseBool(val, kiosk);
        else if (key == "kiosk_monitor")   kioskMonitor = parseInt(val, kioskMonitor);
        else if (key == "no_sprite_limit") noSpriteLimit = parseBool(val, noSpriteLimit);
        else if (key == "light_phaser")    lightPhaser = parseBool(val, lightPhaser);
        else if (key == "rewind")          rewind = parseBool(val, rewind);
        else if (key == "eeprom")          eeprom = parseBool(val, eeprom);
        else if (key == "rewind_seconds")  rewindSeconds = parseInt(val, rewindSeconds);
        else if (key == "lcd_persistence") lcdPersistence = parseFloat(val, lcdPersistence);
        else if (key == "lcd_grid_strength")
            lcdGridStrength = parseFloat(val, lcdGridStrength);
        else if (key == "rom_dir")         romDir = val;
        else if (key == "swap_gamepads")   swapGamepads = parseBool(val, swapGamepads);
        else if (key == "crt_brightness")  c.brightness = parseFloat(val, c.brightness);
        else if (key == "crt_contrast")    c.contrast = parseFloat(val, c.contrast);
        else if (key == "crt_saturation")  c.saturation = parseFloat(val, c.saturation);
        else if (key == "crt_hue")         c.hue = parseFloat(val, c.hue);
        else if (key == "crt_sharpness")   c.sharpness = parseFloat(val, c.sharpness);
        else if (key == "crt_persistence") c.persistence = parseFloat(val, c.persistence);
        else if (key == "crt_scanlines")   c.scanlines = parseFloat(val, c.scanlines);
        else if (key == "crt_barrel")      c.barrel = parseFloat(val, c.barrel);
        else if (key == "crt_shadow_mask") c.shadowMask = parseMask(val, c.shadowMask);
        else if (key == "crt_shadow_mask_strength")
            c.shadowMaskStrength = parseFloat(val, c.shadowMaskStrength);
        else if (key == "crt_luminance_gain")
            c.luminanceGain = parseFloat(val, c.luminanceGain);
        else if (key == "crt_center_lighting")
            c.centerLighting = parseFloat(val, c.centerLighting);
        else if (key == "crt_phosphor_gamma")
            c.phosphorGamma = parseFloat(val, c.phosphorGamma);
        else
            std::fprintf(stderr, "config: unknown key '%s' ignored\n",
                         key.c_str());
    }
    return true;
}

bool Config::save() const {
    std::ofstream f(path, std::ios::trunc);
    if (!f)
        return false;
    const CrtParams& c = crtParams;
    const CrtParams d;   // défauts, pour les bornes en commentaire
    (void)d;
    f << "# Sesame configuration - rewritten on clean exit.\n"
         "# Command-line flags override this file for one launch;\n"
         "# the effective settings are then saved back here.\n"
         "\n"
         "# --- Machine ---\n"
      << "pal = " << (pal ? "true" : "false") << "\n"
      << "game_gear = " << (gameGear ? "true" : "false") << "\n"
      << "# bios: leave BLANK to auto-detect (bios/ folder), 'none' to skip,\n"
      << "#       or the path of a BIOS image\n"
      << "bios = " << bios << "\n"
         "\n"
         "# --- Display ---\n"
      << "crt = " << (crt ? "true" : "false") << "\n"
      << "fullscreen = " << (fullscreen ? "true" : "false") << "\n"
      << "kiosk = " << (kiosk ? "true" : "false") << "\n"
      << "kiosk_monitor = " << kioskMonitor << "\n"
      << "# no_sprite_limit: remove the 8-sprites-per-line hardware limit\n"
      << "no_sprite_limit = " << (noSpriteLimit ? "true" : "false") << "\n"
      << "# light_phaser: mouse aims the Light Phaser (port A), click = trigger\n"
      << "light_phaser = " << (lightPhaser ? "true" : "false") << "\n"
      << "# eeprom: map a 93C46 serial EEPROM at 0x8000 (baseball cartridges)\n"
      << "eeprom = " << (eeprom ? "true" : "false") << "\n"
      << "# rewind: hold Backspace to rewind time (rewind_seconds of history,\n"
      << "#         about 5.5 MB of RAM per second)\n"
      << "rewind = " << (rewind ? "true" : "false") << "\n"
      << "rewind_seconds = " << rewindSeconds << "\n"
      << "# rom_dir: game list folders for the in-game menu, ';' separated\n"
      << "#          (empty = the loaded ROM's folder)\n"
      << "rom_dir = " << romDir << "\n"
      << "# swap_gamepads: exchange gamepad slots 1 and 2 (also in the menu)\n"
      << "swap_gamepads = " << (swapGamepads ? "true" : "false") << "\n"
         "\n"
         "# --- CRT filter ---\n"
      << "crt_brightness = " << c.brightness << "      # -0.5..0.5\n"
      << "crt_contrast = " << c.contrast << "        # 0.5..1.5\n"
      << "crt_saturation = " << c.saturation << "      # 0..2\n"
      << "crt_hue = " << c.hue << "             # -0.5..0.5\n"
      << "crt_sharpness = " << c.sharpness << "     # 0..1, 0.5 = neutral\n"
      << "crt_persistence = " << c.persistence << "   # 0..0.98 phosphor\n"
      << "crt_scanlines = " << c.scanlines << "      # 0..1\n"
      << "crt_barrel = " << c.barrel << "         # 0..0.2\n"
      << "crt_shadow_mask = " << maskName(c.shadowMask)
      << "     # off/triad/aperture/dot\n"
      << "crt_shadow_mask_strength = " << c.shadowMaskStrength << "  # 0..1\n"
      << "crt_luminance_gain = " << c.luminanceGain << "  # 1..2\n"
      << "crt_center_lighting = " << c.centerLighting << " # 0.5..1, 1 = flat\n"
      << "crt_phosphor_gamma = " << c.phosphorGamma << "  # 0.6..2.6\n"
         "\n"
         "# --- Game Gear LCD look (replaces the CRT filter in GG mode) ---\n"
      << "lcd_persistence = " << lcdPersistence << "   # 0..0.98 ghosting\n"
      << "lcd_grid_strength = " << lcdGridStrength << " # 0..1 pixel grid\n";
    return f.good();
}

Config Config::locate(const char* explicitPath, const char* argv0) {
    Config cfg;
    if (explicitPath) {
        if (!cfg.load(explicitPath))
            std::fprintf(stderr, "config: cannot read '%s', using defaults\n",
                         explicitPath);
        cfg.path = explicitPath;   // la sauvegarde ira là, existant ou non
        return cfg;
    }
    if (cfg.load("sesame.cfg"))
        return cfg;
    // À côté de l'exécutable (lancement hors du dossier de travail).
    std::string exe = argv0 ? argv0 : "";
    if (const auto slash = exe.find_last_of("/\\"); slash != std::string::npos) {
        exe.resize(slash);
        if (cfg.load(exe + "/sesame.cfg"))
            return cfg;
    }
    // Rien trouvé : les défauts, sauvegardés dans ./sesame.cfg à la sortie.
    return cfg;
}
