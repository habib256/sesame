# CLAUDE.md

Hub d'orientation pour Claude Code. **Ces instructions priment.**

Sesame — émulateur Sega Master System pédagogique. C++17, GLFW3 + OpenGL
(immediate mode), aucune dépendance dans le cœur. Développé sur macOS Silicon.
**Commentaires et documentation en français ; interface et journaux en ANGLAIS.**

Architecture en deux mots : **le `Bus` *est* le plan mémoire** (route
read8/write8 et in/out vers les puces) et **`sesame_core` ne dépend pas du GUI**.

## Où trouver quoi

| Doc | Contenu |
|-----|---------|
| [`README.md`](README.md) | Présentation et usage (en anglais, public). |
| [`docs/IMPLEMENTED.md`](docs/IMPLEMENTED.md) | Ce qui est fait, par puce. |
| [`TODO.md`](TODO.md) | Ce qui reste, par sous-système. |
| [`docs/RECHERCHE-EMULATEURS.md`](docs/RECHERCHE-EMULATEURS.md) | État de l'art des émulateurs SMS/GG open-source, licences, pistes roadmap. |

## Build & run

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j    # cibles : sesame (GUI), sesame-headless, sesame_core
./build/sesame <rom.sms>
```

Aucun sous-module : le cœur est autonome ; le GUI requiert GLFW (brew/pacman).

⚠ Ne PAS faire `rm -rf build` ; `cmake -B build` reconfigure.

## Tester = le headless (outil n°1)

Pas de framework de test : validation par `sesame-headless` (déterministe,
traces + captures PPM) et les ROM de test générées par `tools/`.

```sh
python3 tools/make_test_rom.py            # génère roms/selftest.sms
./build/sesame-headless roms/selftest.sms --frames 120 --screenshot s.ppm
./build/sesame-headless <rom> --frames N --trace t.txt
./build/sesame-headless <rom> --frames N --sdsc    # console de debug homebrew
python3 tools/run_selftests.py            # la suite complète
```

## Conventions non négociables

- Manettes : bits **actifs à l'état bas** sur les ports 0xDC/0xDD ; l'API
  `Io::setPad` prend elle des bits actifs à 1.
- Le premier Ko de ROM (0x0000-0x03FF) n'est **jamais paginé** par le mapper
  **Sega standard** (Codemasters, lui, pagine tout).
- Écrire 0xFFFC-0xFFFF touche **à la fois** le miroir RAM et le mapper
  (Sega seulement — les autres mappers ignorent ces adresses).
- /INT du VDP est un **niveau** (tenu tant que le drapeau et son enable sont
  actifs) ; le bouton Pause est un **front** NMI.
- Interfaces publiques des `.hpp` de `src/core/` = contrat : ne changer une
  signature qu'en mettant à jour TOUS les appelants dans le même commit.

## Références matérielles

SMS Power! (docs VDP/mapper), MAME (`sms.cpp`, `sn76496.cpp`, `315_5124.cpp`)
comme référence de comportement. Convention SDSC pour la console de debug
(ports 0xFC/0xFD) — utilisée par nos ROM de test.
