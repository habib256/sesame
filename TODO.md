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
- [ ] Modes 224/240 lignes (SMS2)
- [x] PAL (313 lignes, ~49,70 Hz) — cadence, horloge CPU/PSG 3 546 893 Hz,
      séquence VCounter (`--pal` dans les deux frontends, 2026-08)
- [ ] HCounter réel + latch sur front TH (light gun)
- [ ] Modes hérités TMS9918 (0-3) pour les jeux SG-1000/F-16
- [ ] Timing fin intra-ligne (mid-line scroll/CRAM effects)
- [ ] [R] Option « no sprite limit » (débrayer la limite 8/ligne, option
      pédagogique — montrer pourquoi ça clignotait ; précédent : jgenesis,
      Snepulator, Mesen 2)

## Son
- [x] Sortie audio temps réel dans le GUI (CoreAudio/AudioQueue, 2026-08)
- [ ] Backend audio Linux (ALSA/PipeWire) — stub muet actuellement
- [x] [R] Synthèse à bande limitée pour le PSG (réimplémentation maison de
      la technique Blip Buffer de Blargg : table générée par
      `tools/make_blip_table.py`, deltas + intégrateur ; repliement mesuré
      -19 dB -> -67 dB, 2026-08) — prérequis YM2413 rempli
- [ ] YM2413 (FM japonais) — [R] références compatibles licence :
      Gearsystem, jgenesis, `ym2413` de MAME si BSD-3

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
- [ ] Game Gear, suite : mode compatibilité SMS-sur-GG (cartouche .sms via
      adaptateur : palette réduite), shader LCD (rémanence/ghosting de la
      dalle d'origine)
- [ ] Mappers exotiques (Codemasters, Korean/Janggun) ; [R] EEPROM 93C46
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
- [ ] Presets CRT configurables (--crt-preset / fichier de config)
- [ ] Build WebAssembly — [R] viabilité prouvée par jgenesis (navigateur) ;
      le cœur sans dépendance est prêt pour Emscripten, seul le frontend
      est à adapter

## Outils
- [ ] Étalons d'images (comparaison PPM avec tolérance)
- [ ] ROM de test VDP dédiée (scroll, sprites, IRQ ligne)
- [ ] [R] Serveur MCP (à la Gearsystem) : exposer registres Z80/VDP,
      mémoire, points d'arrêt et pas-à-pas à des assistants IA via STDIO —
      base naturelle : `sesame-headless` déterministe. Le chantier le plus
      aligné avec la vocation pédagogique
- [ ] [R] Step-back (recul d'exécution par ligne/frame, inspiration
      Mesen 2) — dépend des save-states déterministes
- [ ] [R] Code/Data Logger (tracer code exécuté vs données dans la ROM,
      inspiration Mesen 2/Nexen)

## Non prioritaire (noté pour mémoire)
- [ ] [R] Lunettes SegaScope 3-D (stéréoscopie ; seul MAME le gère)
