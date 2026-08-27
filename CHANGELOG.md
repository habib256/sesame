# CHANGELOG — travaux réalisés

Journal des chantiers terminés, par sous-système. Le détail technique de
« ce que Sesame gère » vit dans [`docs/IMPLEMENTED.md`](docs/IMPLEMENTED.md) ;
ce fichier ne garde que l'historique. Les entrées `[R]` viennent de l'état de
l'art [`docs/RECHERCHE-EMULATEURS.md`](docs/RECHERCHE-EMULATEURS.md).

## 2026-08

### Phase 1 de l'audit — huit corrections de bugs (2026-08-27)
- Timing intra-ligne : `bus.lineCycles` est désormais publié APRÈS le repli
  de ligne (`Machine.cpp`) — la première écriture VDP suivant une frontière
  de ligne voyait une position ≥ 228 : `catchUp` rendait la ligne entière
  d'un coup et `latchHCounter` produisait une valeur aberrante. Clamps
  défensifs ajoutés dans `catchUp`/`latchHCounter`. Étalons inchangés
  (vérifié : 32/32 checks, 0 pixel de différence)
- `--shot-at` (GUI) : `glPixelStorei(GL_PACK_ALIGNMENT, 1)` avant
  `glReadPixels` — l'alignement 4 par défaut débordait du buffer dès que la
  largeur du framebuffer n'était pas multiple de 4
- `tools/compare_ppm.py` : parseur d'en-tête borné — un PPM tronqué ou vide
  lève `ValueError` au lieu de boucler à l'infini (et de figer la suite)
- Rewind : historique vidé au changement de ROM, au reset (touche R et menu
  Restart) et au F7 ; empreinte ROM (taille ^ somme, `Cartridge::
  fingerprint()`) dans l'en-tête des save-states — un état pris sur une
  autre cartouche est refusé (« state is for another ROM »). Version d'état
  v7. Validé : aller-retour fichier pixel-perfect entre deux processus,
  refus inter-ROM effectif
- Heuristique Janggun gardée par « 0xFFFD jamais écrit » : ce registre
  n'existe pas sur le matériel Janggun mais le boot d'un jeu Sega standard
  l'écrit toujours — une écriture parasite à 0x6000 ne bascule plus le
  mapper (garde plus fin que `segaRegsSeen`, que la Janggun elle-même
  déclencherait via 0xFFFE/0xFFFF)
- `Cartridge::load` : invariant « échec ⇒ cartouche VIDE » (`rom.clear()`
  sur tous les chemins d'échec — plus de ROM fantôme à moitié chargée
  sélectionnable par le Bus) ; borne de taille à 8 Mo
- `sesame.cfg` : écriture ATOMIQUE (`.tmp` + rename — une coupure de
  courant ne détruit plus le fichier) et bornage de toutes les valeurs
  relues aux plages documentées (`rewind_seconds` ≤ 120 : une valeur folle
  était un épuisement mémoire garanti à ~5,5 Mo/s)
- Mode `GameGearSms` : les ports 0x00-0x06 sont décodés comme sur le vrai
  matériel (la puce GG les décode toujours, cartouche SMS ou pas) — une
  écriture au port 0x06 ne tombe plus dans `memControl` (elle pouvait
  débrayer la cartouche ou la work RAM)

### CPU (Z80)
- Validé avec ZEXDOC/ZEXALL (port SMS de Maxim Zhao via SDSC :
  `tools/fetch_zexall.py` puis `tools/run_zexall.py`) — 79/79 tests PASS
- MEMPTR/WZ exact pour BIT n,(HL) — validé par le test « bit n,r/(hl) »
  de ZEXALL

### VDP
- Modes 224/240 lignes (SMS2) — sélection M1/M3 (M4+M2), table de noms
  0x?700 32 rangées, scroll Y mod 256, terminateur de sprites 0xD0 inactif,
  séquences VCounter par mode/norme, hauteur dynamique dans les deux
  frontends ; scène 224 + étalon dans la ROM de test VDP
- PAL (313 lignes, ~49,70 Hz) — cadence, horloge CPU/PSG 3 546 893 Hz,
  séquence VCounter (`--pal` dans les deux frontends)
- HCounter réel + latch sur front TH — séquence 0x00-0x93/0xE9-0xFF depuis
  la position CPU dans la ligne, latch sur écriture 0x3F (bits TH) et par le
  Light Phaser ; pistolet émulé (fenêtre de ±2 lignes, TH d'entrée sur 0xDD,
  HC = X/2 + 0x28) — souris + clic au GUI (`light_phaser` dans sesame.cfg),
  `--gun X Y` au headless. NB : la ROM de validation « trois visées » n'a
  pas été commitée — validation à rejouer (cf. TODO, Phase 3)
- Modes hérités TMS9918 pour les jeux SG-1000/F-16 — Graphic I/II, texte
  (40 col.), multicolor, sprites TMS (4/ligne, drapeau 5S + numéro,
  coïncidence, early clock, 16×16, MAG) ; couleurs CRAM sprite
  (comportement 315-5124) avec repli palette TMS fixe tant que la CRAM n'a
  pas été écrite ; `.sg` accepté partout ; ROM de test `tmstest.sg` + étalon
- Timing fin intra-ligne — rendu du mode 4 par tranches avec rattrapage de
  faisceau (le Bus rend la ligne jusqu'à la position CPU avant toute
  écriture VDP) : effets CRAM mid-line au pixel près, scroll X latché en
  début de ligne, sprites évalués une fois par ligne (SAT lue au hblank)
- [R] Option « no sprite limit » (mode 4 : 8/ligne ; TMS : 4/ligne ; les
  drapeaux overflow/5S restent levés) — `no_sprite_limit` dans sesame.cfg,
  `--no-sprite-limit` au headless

### Son
- Sortie audio temps réel dans le GUI (CoreAudio/AudioQueue)
- Backend audio Linux ALSA (thread d'écriture bloquante, stéréo s16,
  ~50 ms de latence ; stub muet si ALSA absent au configure) —
  ÉCRIT SANS ÊTRE TESTÉ (cf. TODO)
- [R] Synthèse à bande limitée pour le PSG (réimplémentation maison de la
  technique Blip Buffer de Blargg : table générée par
  `tools/make_blip_table.py`, deltas + intégrateur ; repliement mesuré
  -19 dB -> -67 dB)
- YM2413 (FM japonais) — implémentation maison (`src/core/Ym2413.cpp`) :
  9 canaux mélodiques 2 opérateurs, tables log-sin/exp du matériel OPL,
  ADSR, KSL/KSR, vibrato/trémolo, détection port 0xF2, mixage via le PSG,
  save-state. Instruments ROM et patches rythme vérifiés (valeurs
  communautaires issues de l'analyse du die — doc andete, publiées dans
  emu2413/MIT, attribution dans le code) ; cadences d'enveloppe alignées
  sur les motifs de sous-taux OPL. Validation à l'écoute et au spectre —
  non automatisée (cf. TODO, Phase 3)

### Machine
- Save-states — sérialiseur symétrique `StateIO`, fichier versionné
  « SESAMEST » (modèle/région vérifiés), pris en frontière de trame ;
  GUI : F5/F7 -> `<rom>.state` ; headless : `--state-save N FILE` /
  `--state-load FILE` ; validé pixel-perfect et octet-perfect audio à
  travers la coupe, processus séparés
- BIOS optionnel + contrôle mémoire 0x3E réel (reste : bouton Reset console
  et lecture-arrière 0x3E côté SMS1 — cf. TODO)
- Game Gear — mode natif : auto-détection `.gg`, CRAM 12 bits écrite par
  mot (latch pair/impair), fenêtre 160x144 recadrée dans les deux frontends,
  ports 0x00-0x06, pas de NMI Pause ; stéréo PSG de bout en bout (registre
  0x06, CoreAudio 2 canaux, WAV stéréo) ; mode compatibilité SMS-sur-GG
  (`Model::GameGearSms`, drapeau `--gg`) ; shader LCD (ghosting + grille de
  pixels, `lcd_persistence`/`lcd_grid_strength` dans sesame.cfg)
- Mappers Codemasters (auto-détecté par l'en-tête 0x7FE0, premier Ko
  paginé, RAM Ernie Els Golf) et coréen (page fenêtre 2 par 0xA000,
  heuristique gardée par « registres Sega jamais écrits ») — ROMs de test
  `cmtest.sms`/`krtest.sms` dans la suite
- Mapper Janggun : 4 fenêtres de 8 Ko (regs 0x4000/0x6000/0x8000/0xA000,
  paires 16 Ko via 0xFFFE/0xFFFF), pages à octets miroirs (bit 6) ;
  [R] EEPROM 93C46 : Microwire complet, fenêtre 0x8000, persistée en
  `<rom>.eeprom` — activation par `eeprom` (cfg) / `--eeprom`.
  ROMs jgtest/eetest dans la suite
- Sauvegarde RAM cartouche sur disque (.sav — GUI toujours, headless
  opt-in via `--sav` pour rester déterministe)

### Frontends
- Manette USB (API gamepad GLFW, 2 pads + Start = Pause)
- Redimensionnement + plein écran (touche F)
- Filtre CRT (pile d'effets portée de NeoST/POM2 : baril, scanlines,
  shadow mask, persistance… — `--crt` / touche C)
- Mode kiosk (`--kiosk [--kiosk-monitor N]` : plein écran exclusif, curseur
  masqué, CRT activé) + menu in-game (Start/F9 : liste de jeux, restart,
  desktop, quit) ; parité NeoST : dossiers ROM multiples (`rom_dir` liste
  « ; »), échange des manettes 1/2 (`swap_gamepads` persisté)
- Fichier de configuration `sesame.cfg` : chargé au lancement, CLI
  prioritaire, état effectif réécrit à la sortie propre
- Build WebAssembly — `src/wasm/main_wasm.cpp` (API C exportée) +
  `web/index.html` (canvas 2D, WebAudio, clavier) + `tools/build_wasm.sh`
  (emcc) ; validé sous Node

### Outils
- Étalons d'images — `tools/compare_ppm.py` (tolérance zéro par défaut),
  références commitées dans `tests/refs/`
- ROM de test VDP dédiée — `tools/make_vdp_rom.py` -> `roms/vdptest.sms`
  (3 scènes : scroll+colonne masquée, sprites 8x16 overflow/collision,
  split-screen IRQ ligne) ; mini-assembleur factorisé dans `tools/smsasm.py`
- [R] Rewind / step-back par trame (inspiration Mesen 2) : un save-state
  mémoire par trame dans un anneau borné (`rewind_seconds`, ~5,5 Mo/s),
  touche Retour arrière au GUI ; `--rewind-check` headless dans la suite
