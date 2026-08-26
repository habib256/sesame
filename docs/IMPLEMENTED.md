# IMPLEMENTED — ce qui est fait, par puce

Répond à « Sesame gère-t-il X ? ». Mis à jour à chaque chantier.

## Z80 (`src/core/Z80.cpp`)
- Jeu d'instructions complet : base, CB, ED, DD/FD, DD CB/FD CB,
  opcodes non documentés (SLL, IXH/IXL/IYH/IYL…)
- Drapeaux X/Y non documentés sur les chemins courants
- Cycles standard par instruction ; registre R ; délai EI
- IM 0 (≡ IM 1 sur SMS, bus à 0xFF), IM 1, IM 2 ; NMI (Pause)
- Désassembleur pour la trace (`Z80Disasm.cpp`)
- Validé par ZEXDOC ET ZEXALL (79/79 tests PASS chacun, drapeaux non
  documentés compris) — `tools/fetch_zexall.py` puis `tools/run_zexall.py`

## VDP 315-5124 (`src/core/Vdp.cpp`)
- Mode 4, 256×192, rendu par ligne ; NTSC (262 lignes, ~59,92 Hz) et
  PAL (313 lignes, ~49,70 Hz, horloge CPU/PSG 3 546 893 Hz) via `--pal`
- Défilement X/Y + verrous (reg0 bits 6/7), colonne gauche masquable
- Sprites 8×8 et 8×16, limite 8/ligne (overflow), collision, early clock ;
  comparaison Y en 8 bits (un sprite à Y >= 0xF1 déborde en haut de l'écran)
- Interruptions VBlank et ligne (reg 10), /INT en niveau
- VCounter avec saut NTSC et PAL ; HCounter approximé (latch v1 simplifié)
- Modes TMS9918 hérités : NON (écran noir) — voir TODO

## PSG SN76489 (`src/core/Psg.cpp`)
- 3 canaux carrés + bruit (LFSR Sega 16 bits, blanc/périodique)
- Période 0/1 = niveau constant (lecture de samples)
- Sortie 44,1 kHz mono s16 via anneau ; table de volumes 2 dB

## Mapper / cartouche (`src/core/Cartridge.cpp`)
- Mapper Sega standard (0xFFFC-0xFFFF), premier Ko non paginé
- RAM cartouche 2×16 Ko (bit3/bit2 de 0xFFFC), persistée en `<rom>.sav`
  (GUI : toujours, avec sauvegarde périodique ~5 s ; headless : opt-in
  `--sav` pour préserver le déterminisme des traces)
- `.sms` avec en-tête parasite 512 octets toléré

## E/S (`src/core/Io.cpp`)
- Ports 0xDC/0xDD (2 manettes, actifs bas), contrôle E/S 0x3F (TH/région)
- Console SDSC 0xFC/0xFD (texte vers stdout avec `--sdsc`)
- Contrôle mémoire 0x3E : réel, géré par le Bus — sélection BIOS/cartouche
  (BIOS prioritaire), work RAM débrayable (bit 4), bus flottant 0xFF sans
  média actif ; slots carte/extension toujours vides. Slot BIOS chargeable
  (`--bios`, ou nom de fichier contenant « BIOS ») ; le BIOS SMS2 à Sonic
  intégré démarre et sait détecter/booter une cartouche.

## Frontends
- `sesame-headless` : --bios, --pal, --sav, --frames, --trace, --screenshot,
  --shot-every, --sdsc, --wav, --pause-at
- `sesame` (GLFW/OpenGL) : affichage 4:3 redimensionnable, plein écran
  (touche F), clavier, Pause/Reset, --bios, --pal ;
  filtre CRT en une passe FBO (`src/gui/CrtEffectStack.cpp`, porté de
  NeoST/POM2 : baril, scanlines, shadow mask, vignette, persistance
  phosphore ; GLSL 120→150 avec repli, no-op sûr si le pilote refuse) —
  `--crt` ou touche C ; mode borne `--kiosk [--kiosk-monitor N]` (plein
  écran exclusif, curseur masqué, CRT activé) avec menu in-game disponible
  partout (`src/gui/KioskMenu.cpp`, Start manette ou F9 — Échap aussi en
  borne : jeu en pause, liste des .sms
  du dossier de la ROM — hors BIOS —, Resume/Restart/Desktop/Quit ; rendu
  immediate mode, police 8×8 font8x8 vendorisée domaine public ; `--menu`
  l'ouvre au lancement) ; en borne, Échap ne quitte jamais ;
  `--shot-at N FILE.ppm` capture le framebuffer GL affiché (trames
  affichées, menu compris — validation du rendu) ;
  manettes USB (API gamepad GLFW : slots 1/2 -> pads 1/2, D-pad ou stick,
  A/X = bouton 1, B/Y = bouton 2, Start = menu kiosk, Select = Pause
  console — aussi sur la touche Entrée) ;
  audio temps réel via CoreAudio/AudioQueue sur macOS (`src/gui/AudioOut.cpp`,
  anneau ~185 ms, 4 tampons de 512 échantillons) — Linux : muet (TODO ALSA)
