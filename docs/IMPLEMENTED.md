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
- Modes 224/240 lignes (SMS2, M4+M2+M1/M3) : `height()` dynamique relu par
  les frontends, table de noms 32 rangées basée 0x?700, scroll Y mod 256,
  terminateur de sprites 0xD0 inactif, séquences VCounter propres à chaque
  mode et norme
- Défilement X/Y + verrous (reg0 bits 6/7), colonne gauche masquable
- Sprites 8×8 et 8×16, limite 8/ligne (overflow), collision, early clock ;
  comparaison Y en 8 bits (un sprite à Y >= 0xF1 déborde en haut de l'écran)
- Interruptions VBlank et ligne (reg 10), /INT en niveau
- VCounter avec saut NTSC et PAL ; HCounter approximé (latch v1 simplifié)
- Modes TMS9918 hérités (M4=0) : Graphic I/II (tables motifs/couleurs avec
  masques de repli), texte 40 colonnes (encre/papier par reg7), multicolor
  (blocs 4×4) ; sprites TMS (SAT 4 octets, 4/ligne, drapeau 5S + numéro du
  5e, coïncidence sur les bits de motif, early clock, 16×16, MAG).
  Couleurs : moitié sprite de la CRAM (comportement 315-5124) avec REPLI
  sur la palette TMS canonique tant qu'aucune écriture CRAM n'a eu lieu —
  choix documenté pour les jeux SG-1000 (extension `.sg` acceptée)

## PSG SN76489 (`src/core/Psg.cpp`)
- 3 canaux carrés + bruit (LFSR Sega 16 bits, blanc/périodique)
- Période 0/1 = niveau constant (lecture de samples)
- Sortie 44,1 kHz s16 via anneau, en TRAMES STÉRÉO entrelacées (G,D) :
  panning Game Gear (registre 0x06) appliqué par voie dans la synthèse ;
  en SMS les deux voies sont identiques. Table de volumes 2 dB
- Rééchantillonnage par synthèse à bande limitée (façon Blip Buffer de
  Blargg, réimplémentation maison GPL) : marches band-limited pré-calculées
  (64 phases × 16 coefficients, `tools/make_blip_table.py` →
  `PsgBlipTable.inc` commité), deltas + intégrateur. Repliement mesuré à
  ~-67 dB contre ~-19 dB avec l'ancien filtre boîte (2026-08)

## Frontend WebAssembly (`src/wasm/main_wasm.cpp`, `web/index.html`)
- API C exportée (load_rom via MEMFS, run_frame, framebuffer + fenêtre de
  recadrage, audio stéréo, manette, pause/reset, région) — construite par
  `tools/build_wasm.sh` (emcc), produits de build non commités
- Page autonome : canvas 2D (pixels nets, 4:3 SMS / 10:9 GG), WebAudio,
  clavier identique au frontend natif, cadence par l'horloge réelle

## Game Gear (mode natif)
- Auto-détection par l'extension `.gg` (`Machine::loadRom` -> `setModel`,
  propagé au VDP et au Bus) ; une `.sms` reste émulée en Master System,
  sauf avec `--gg` : mode compatibilité SMS-sur-GG (`Model::GameGearSms`,
  palette SMS 6 bits mais fenêtre LCD 160×144, NMI Pause accordé)
- VDP : CRAM 64 octets, écrite PAR MOT (octet pair latché, octet impair
  commite l'entrée 12 bits ----BBBBGGGGRRRR) ; rendu de la trame 256×192
  complète, fenêtre visible 160×144 centrée recadrée par les frontends
  (GUI : 10:9 pixels carrés, sous-image via GL_UNPACK_ROW_LENGTH ;
  headless : PPM 160×144)
- Ports 0x00-0x06 (soustraits à la règle pair/impair 0x3E/0x3F) :
  0x00 = Start (actif bas, `Io::Button::Start`, touche Entrée/Select au
  GUI) + région export NTSC ; 0x01-0x05 = EXT/série non connectés ;
  0x06 = stéréo PSG (panning par canal appliqué à la sortie)
- Pas de NMI Pause (`Machine::pressPause` sans effet en Game Gear)

## YM2413 / OPLL (`src/core/Ym2413.cpp`) — unité FM des SMS japonaises
- Implémentation maison : 9 canaux mélodiques, 2 opérateurs (modulateur ->
  porteuse, rétroaction), calcul en logarithme avec les tables log-sin/exp
  du matériel OPL (rétro-ingénierie publique)
- Enveloppes ADSR (tenu/percussif), key scaling (KSL/KSR), vibrato et
  trémolo (LFO), demi-onde redressée (bits DC/DM), instrument utilisateur
  exact (registres 0x00-0x07)
- Ports 0xF0/0xF1 (registre/donnée), 0xF2 (contrôle audio, relu par les
  jeux pour détecter l'unité — présente en SMS, absente en Game Gear)
- Sortie native à horloge/72 (~49,7 kHz), moyennée par trame 44,1 kHz et
  mixée par le PSG sur les deux voies ; sérialisée dans les save-states
  (version d'état 2)
- Mode rythme (reg 0x0E) : canaux 6-8 en cinq percussions — grosse caisse
  FM 2 opérateurs, caisse claire (phase XOR bruit), charleston (LFSR
  23 bits), tom (sinus), cymbale (XOR de phases) ; volumes par le câblage
  réel des regs 0x36-0x38, key par front
- Approximations documentées : cadences d'enveloppe (forme exacte, cycle
  approché), instruments ROM et patches rythme (jeux approximatifs, dump
  vérifié en TODO), percussions plausibles mais pas cycle-exactes

## Mapper / cartouche (`src/core/Cartridge.cpp`)
- Mapper Sega standard (0xFFFC-0xFFFF), premier Ko non paginé
- Mapper Codemasters : auto-détecté (en-tête 0x7FE0 : nombre de banques +
  somme de contrôle et complément), registres aux adresses 0x0000/0x4000/
  0x8000, premier Ko paginé comme le reste, RAM 8 Ko volatile sur
  0xA000-0xBFFF via bit 7 de 0x4000 (Ernie Els Golf)
- Mapper coréen : page de la fenêtre 2 par écriture à 0xA000 ; détection
  heuristique à l'exécution (écriture à 0xA000 alors que les registres
  Sega n'ont jamais été touchés), type sérialisé dans les save-states (v3)
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

## Save-states (`src/core/StateIO.hpp`, `Machine::saveState/loadState`)
- Sérialiseur SYMÉTRIQUE : chaque puce liste ses champs une seule fois dans
  `serialize(StateIO&)` (écrits en Save, relus en Load — désynchronisation
  impossible) ; little-endian explicite, portable
- Fichier versionné : en-tête « SESAMEST » + version + modèle + région,
  vérifiés au chargement (refus d'un état d'une autre machine)
- Pris en FRONTIÈRE de trame ; non sérialisés : framebuffer VDP (reconstruit
  par la trame suivante), anneau audio (vidé), manettes (entrées vivantes),
  journal SDSC, ROM (vient du fichier chargé) ; RAM cartouche marquée
  modifiée au chargement (persistance .sav cohérente) ; ~90 Ko par état
- GUI : F5 = save, F7 = load (`<rom>.state` à côté de la ROM) ;
  headless : `--state-save N FILE`, `--state-load FILE`
- Validé : capture à la trame 120 identique octet par octet entre run
  continu et save-à-60 + reload dans un NOUVEAU processus ; idem pour
  l'audio WAV de reprise

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
