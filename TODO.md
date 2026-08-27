# TODO — par sous-système

Les entrées marquées `[R]` viennent de l'état de l'art
[`docs/RECHERCHE-EMULATEURS.md`](docs/RECHERCHE-EMULATEURS.md) (références
et contraintes de licence détaillées là-bas).

## CPU (Z80)
- [x] Valider avec ZEXDOC/ZEXALL (port SMS de Maxim Zhao via SDSC :
      `tools/fetch_zexall.py` puis `tools/run_zexall.py`, 2026-08)
- [x] MEMPTR/WZ exact pour BIT n,(HL) — validé par le test
      « bit n,r/(hl) » de ZEXALL (2026-08)

## VDP
- [x] Modes 224/240 lignes (SMS2) — sélection M1/M3 (M4+M2), table de noms
      0x?700 32 rangées, scroll Y mod 256, terminateur de sprites 0xD0
      inactif, séquences VCounter par mode/norme, hauteur dynamique dans
      les deux frontends ; scène 224 + étalon dans la ROM de test VDP
      (17 checks) (2026-08)
- [x] PAL (313 lignes, ~49,70 Hz) — cadence, horloge CPU/PSG 3 546 893 Hz,
      séquence VCounter (`--pal` dans les deux frontends, 2026-08)
- [x] HCounter réel + latch sur front TH — séquence 0x00-0x93/0xE9-0xFF
      depuis la position CPU dans la ligne, latch sur écriture 0x3F (bits
      TH) et par le Light Phaser ; pistolet émulé (fenêtre de ±2 lignes,
      TH d'entrée sur 0xDD, HC = X/2 + 0x28) — souris + clic au GUI
      (`light_phaser` dans sesame.cfg), `--gun X Y` au headless, validé
      par ROM de test sur trois visées (2026-08)
- [x] Modes hérités TMS9918 pour les jeux SG-1000/F-16 — Graphic I/II,
      texte (40 col.), multicolor, sprites TMS (4/ligne, drapeau 5S +
      numéro, coïncidence, early clock, 16×16, MAG) ; couleurs CRAM
      sprite (comportement 315-5124) avec repli palette TMS fixe tant que
      la CRAM n'a pas été écrite (jeux SG-1000) ; `.sg` accepté partout ;
      ROM de test `tmstest.sg` + étalon (27 checks), état v4 (2026-08)
- [ ] Timing fin intra-ligne (mid-line scroll/CRAM effects)
- [x] [R] Option « no sprite limit » (mode 4 : 8/ligne ; TMS : 4/ligne ;
      les drapeaux overflow/5S restent levés) — `no_sprite_limit` dans
      sesame.cfg, `--no-sprite-limit` au headless (2026-08)

## Son
- [x] Sortie audio temps réel dans le GUI (CoreAudio/AudioQueue, 2026-08)
- [x] Backend audio Linux ALSA (thread d'écriture bloquante, stéréo s16,
      ~50 ms de latence ; stub muet si ALSA absent au configure) —
      ÉCRIT SANS ÊTRE TESTÉ (pas de machine Linux sous la main) (2026-08)
- [x] [R] Synthèse à bande limitée pour le PSG (réimplémentation maison de
      la technique Blip Buffer de Blargg : table générée par
      `tools/make_blip_table.py`, deltas + intégrateur ; repliement mesuré
      -19 dB -> -67 dB, 2026-08) — prérequis YM2413 rempli
- [x] YM2413 (FM japonais) — implémentation maison (`src/core/Ym2413.cpp`) :
      9 canaux mélodiques 2 opérateurs, tables log-sin/exp du matériel OPL,
      ADSR, KSL/KSR, vibrato/trémolo, détection port 0xF2, mixage via le
      PSG, save-state (version d'état 2). Validé : fréquence porteuse
      exacte (formule matérielle), spectre sinus propre, bandes latérales
      FM cohérentes, reprise d'état octet-perfect (2026-08)
- [ ] YM2413, suite : mode rythme (canaux 6-8 percussions), cadences
      d'enveloppe exactes au cycle, jeu d'instruments ROM vérifié contre un
      dump matériel (le jeu actuel est une approximation documentée)

## Machine
- [x] Save-states — sérialiseur symétrique `StateIO`, fichier versionné
      « SESAMEST » (modèle/région vérifiés), pris en frontière de trame ;
      GUI : F5/F7 -> `<rom>.state` ; headless : `--state-save N FILE` /
      `--state-load FILE` ; validé pixel-perfect et octet-perfect audio à
      travers la coupe, processus séparés (2026-08)
- [x] BIOS optionnel + contrôle mémoire 0x3E réel (2026-08 ; reste : bouton
      Reset console et lecture-arrière 0x3E côté SMS1)
- [x] Game Gear — mode natif : auto-détection `.gg`, CRAM 12 bits écrite
      par mot (latch pair/impair), fenêtre 160x144 recadrée dans les deux
      frontends (GUI 10:9, PPM headless), ports 0x00-0x06 (Start actif bas,
      EXT/série stub, stéréo PSG mémorisée), pas de NMI Pause (2026-08)
- [x] Game Gear, stéréo : chaîne audio en trames stéréo entrelacées de bout
      en bout (PSG par voie selon le registre 0x06, CoreAudio 2 canaux,
      WAV stéréo ; en SMS les deux voies sont identiques) (2026-08)
- [x] Mode compatibilité SMS-sur-GG (`Model::GameGearSms`, drapeau `--gg`
      des deux frontends : palette SMS, fenêtre LCD 160x144, NMI Pause
      accordé) (2026-08)
- [x] Game Gear, shader LCD : en mode GG la pile d'effets bascule sur un
      préréglage « dalle » — rémanence marquée (ghosting), grille de pixels
      (masque à points), ni scanlines ni baril ; `lcd_persistence` et
      `lcd_grid_strength` dans sesame.cfg (2026-08)
- [x] Mappers Codemasters (auto-détecté par l'en-tête 0x7FE0, registres
      0x0000/0x4000/0x8000, premier Ko paginé, RAM Ernie Els Golf) et
      coréen (page fenêtre 2 par 0xA000, heuristique gardée par « registres
      Sega jamais écrits ») — ROMs de test `cmtest.sms`/`krtest.sms` dans
      la suite (23 checks), état v3 (2026-08)
- [ ] Mappers restants : Janggun (8 Ko, octets miroirs), [R] EEPROM 93C46
      (cartouches de baseball, sauvegarde des stats)
- [x] Sauvegarde RAM cartouche sur disque (.sav — GUI toujours, headless
      opt-in via --sav pour rester déterministe, 2026-08)

## Frontends
- [x] Manette USB (API gamepad GLFW, 2 pads + Start = Pause, 2026-08)
- [x] Redimensionnement + plein écran (touche F, 2026-08)
- [x] Filtre CRT (pile d'effets portée de NeoST/POM2 : baril, scanlines,
      shadow mask, persistance… — --crt / touche C, 2026-08)
- [x] Mode kiosk (--kiosk [--kiosk-monitor N] : plein écran exclusif,
      curseur masqué, CRT activé, 2026-08) + menu in-game (Start/F9 :
      liste de jeux, restart, desktop, quit — 2026-08)
- [ ] Menu kiosk : dossiers ROM additionnels + affectation des manettes
      (parité NeoST)
- [x] Fichier de configuration `sesame.cfg` : chargé au lancement
      (--config FILE, ./sesame.cfg ou à côté de l'exécutable), CLI
      prioritaire, état effectif RÉÉCRIT à la sortie propre (les réglages
      survivent) ; machine (pal, game_gear, bios), affichage (crt,
      fullscreen, kiosk, rom_dir, no_sprite_limit) et les 13 paramètres du
      filtre CRT (2026-08)
- [x] Build WebAssembly — `src/wasm/main_wasm.cpp` (API C exportée) +
      `web/index.html` (canvas 2D, WebAudio, clavier) + `tools/build_wasm.sh`
      (emcc) ; recadrage GG et .sms/.gg/.sg gérés ; validé sous Node
      (produits de build non commités) (2026-08)

## Outils
- [x] Étalons d'images — `tools/compare_ppm.py` (tolérance/max-diff,
      défaut zéro), références commitées dans `tests/refs/` (2026-08)
- [x] ROM de test VDP dédiée — `tools/make_vdp_rom.py` -> `roms/vdptest.sms`
      (3 scènes : scroll+colonne masquée, sprites 8x16 overflow/collision,
      split-screen IRQ ligne ; verdicts SDSC + comparaison aux étalons,
      intégrée à `run_selftests.py` — 16 checks ; mini-assembleur factorisé
      dans `tools/smsasm.py`) (2026-08)
- [ ] [R] Serveur MCP (à la Gearsystem) : exposer registres Z80/VDP,
      mémoire, points d'arrêt et pas-à-pas à des assistants IA via STDIO —
      base naturelle : `sesame-headless` déterministe. Le chantier le plus
      aligné avec la vocation pédagogique
- [x] [R] Rewind / step-back par trame (inspiration Mesen 2) : un
      save-state MÉMOIRE par trame émulée dans un anneau borné
      (`rewind_seconds`, ~5,5 Mo/s), touche Retour arrière maintenue au
      GUI ; `--rewind-check` headless (rejeu pixel-exact) dans la suite
      (28 checks) (2026-08). Reste : recul par LIGNE (débogueur)
- [ ] [R] Code/Data Logger (tracer code exécuté vs données dans la ROM,
      inspiration Mesen 2/Nexen)

## Non prioritaire (noté pour mémoire)
- [ ] [R] Lunettes SegaScope 3-D (stéréoscopie ; seul MAME le gère)
